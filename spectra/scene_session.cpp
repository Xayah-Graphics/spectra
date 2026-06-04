module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module xayah.spectra.scene_session;

import std;

namespace {
    constexpr std::string_view console_reset = "\x1b[0m";
    constexpr std::string_view console_dim = "\x1b[2m";
    constexpr std::string_view console_blue = "\x1b[38;5;75m";
    constexpr std::string_view console_cyan = "\x1b[38;5;81m";
    constexpr std::string_view console_yellow = "\x1b[38;5;221m";
    constexpr std::string_view console_red = "\x1b[38;5;203m";
    constexpr std::chrono::steady_clock::duration background_console_interval = std::chrono::seconds{2};
    constexpr std::size_t console_task_text_limit = 88;

    std::mutex console_mutex{};

    void console_line(const std::string_view label, const std::string_view color, const std::string_view message) {
        std::scoped_lock lock{console_mutex};
        std::clog << console_dim << "spectra" << console_reset << " " << color << "[" << label << "]" << console_reset << " " << message << std::endl;
    }

    void console_status(const std::string_view label, const std::string_view message) {
        console_line(label, console_cyan, message);
    }

    void console_progress(const std::string_view label, const std::string_view message) {
        console_line(label, console_blue, message);
    }

    void console_warning(const std::string_view label, const std::string_view message) {
        console_line(label, console_yellow, message);
    }

    void console_error(const std::string_view label, const std::string_view message) {
        console_line(label, console_red, message);
    }

    void console_status_noexcept(const std::string_view label, const std::string_view message) noexcept {
        try {
            console_status(label, message);
        } catch (...) {
        }
    }

    [[nodiscard]] std::string compact_console_text(const std::string_view text) {
        if (text.size() <= console_task_text_limit) return std::string{text};
        constexpr std::size_t prefix_size = 42;
        constexpr std::size_t suffix_size = console_task_text_limit - prefix_size - 3;
        return std::format("{}...{}", text.substr(0, prefix_size), text.substr(text.size() - suffix_size));
    }

    [[nodiscard]] std::string elapsed_seconds_text(const std::chrono::steady_clock::duration duration) {
        return std::format("{:.1f}s", std::chrono::duration<double>{duration}.count());
    }

    [[nodiscard]] std::string lowercase_copy(std::string_view value) {
        std::string result{value};
        for (char& character : result) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return result;
    }

    [[nodiscard]] bool contains_case_insensitive(const std::string_view value, const std::string_view pattern) {
        if (pattern.empty()) return true;
        return lowercase_copy(value).find(lowercase_copy(pattern)) != std::string::npos;
    }

    [[nodiscard]] const char* display_state_text(const xayah::SpectraSceneSession::DisplayState state) {
        switch (state) {
        case xayah::SpectraSceneSession::DisplayState::Checking: return "Checking";
        case xayah::SpectraSceneSession::DisplayState::Ready: return "Ready";
        case xayah::SpectraSceneSession::DisplayState::Unsupported: return "Unsupported";
        case xayah::SpectraSceneSession::DisplayState::Invalid: return "Invalid";
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] ImVec4 display_state_color(const xayah::SpectraSceneSession::DisplayState state) {
        switch (state) {
        case xayah::SpectraSceneSession::DisplayState::Checking: return ImVec4{0.620f, 0.660f, 0.720f, 1.0f};
        case xayah::SpectraSceneSession::DisplayState::Ready: return ImVec4{0.220f, 0.720f, 0.480f, 1.0f};
        case xayah::SpectraSceneSession::DisplayState::Unsupported: return ImVec4{0.930f, 0.680f, 0.230f, 1.0f};
        case xayah::SpectraSceneSession::DisplayState::Invalid: return ImVec4{0.900f, 0.300f, 0.300f, 1.0f};
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] bool display_filter_matches(const xayah::SpectraSceneSession::DisplayState state, const int filter) {
        if (filter == 0) return true;
        if (filter == 1) return state == xayah::SpectraSceneSession::DisplayState::Ready;
        if (filter == 2) return state == xayah::SpectraSceneSession::DisplayState::Unsupported;
        if (filter == 3) return state == xayah::SpectraSceneSession::DisplayState::Invalid;
        if (filter == 4) return state == xayah::SpectraSceneSession::DisplayState::Checking;
        throw std::runtime_error("Unknown Spectra scene display filter");
    }

    [[nodiscard]] std::string source_location_text(const spectra::scene::SceneSourceLocation& source) {
        return std::format("{}:{}:{}", source.filename, source.line, source.column);
    }

    [[nodiscard]] std::string catalog_status_text(const spectra::scene::SceneCatalog& catalog) {
        return std::format("{} ready, {} invalid, {} pending, {} total", catalog.ready_count, catalog.invalid_count, catalog.pending_count, catalog.entries.size());
    }

    [[nodiscard]] std::string first_diagnostic_message(const std::vector<spectra::scene::SceneDiagnostic>& diagnostics) {
        if (diagnostics.empty()) return "no diagnostics";
        return diagnostics.front().message;
    }

    constexpr std::string_view initial_scene_id = "pbrt-book/book.pbrt";
    constexpr std::size_t scene_background_worker_count = 30;
} // namespace

namespace xayah {
    SpectraSceneSession::SpectraSceneSession() : workspace(std::make_shared<spectra::scene::SceneWorkspace>()) {
        console_status("scene", "catalog scan started");
        this->scene_catalog = spectra::scene::DiscoverSceneCatalog();
        console_status("scene", std::format("catalog scan complete: {}", catalog_status_text(this->scene_catalog)));
        this->scene_catalog_validation_claimed.assign(this->scene_catalog.entries.size(), false);
        const std::string initial_scene_id_string{initial_scene_id};
        const auto active_scene_iter = std::ranges::find_if(this->scene_catalog.entries, [&initial_scene_id_string](const spectra::scene::SceneCatalogEntry& entry) { return entry.id == initial_scene_id_string; });
        if (active_scene_iter == this->scene_catalog.entries.end()) throw std::runtime_error(std::format("Spectra scene catalog does not contain required initial scene \"{}\"", initial_scene_id));
        console_progress("scene", std::format("initial scene: {}", initial_scene_id));
        if (active_scene_iter->state == spectra::scene::SceneCatalogEntryState::Pending) spectra::scene::ValidateSceneCatalogEntry(*active_scene_iter);
        if (active_scene_iter->state != spectra::scene::SceneCatalogEntryState::Ready || active_scene_iter->document == nullptr) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not parseable", initial_scene_id));
        this->active_scene_index           = static_cast<std::size_t>(std::distance(this->scene_catalog.entries.begin(), active_scene_iter));
        this->scene_library.selected_index = this->active_scene_index;
        *this->workspace                   = spectra::scene::SceneWorkspace{*this->scene_catalog.entries[this->active_scene_index].document};
        this->refresh_scene_catalog_counts();
        console_status("scene", std::format("initial scene ready: {}", active_scene_iter->id));
    }

    SpectraSceneSession::~SpectraSceneSession() noexcept {
        this->detach();
    }

    void SpectraSceneSession::detach() noexcept {
        if (!this->scene_background_workers.empty()) console_status_noexcept("scene", "stopping background workers");
        for (std::jthread& worker : this->scene_background_workers) worker.request_stop();
        this->scene_background_condition.notify_all();
        for (std::jthread& worker : this->scene_background_workers) {
            if (worker.joinable()) worker.join();
        }
        this->scene_background_workers.clear();
        this->translation_requests.clear();
        this->translation_requests_in_progress.clear();
        this->scene_catalog_validation_claimed.clear();
        this->attached = false;
    }

    void SpectraSceneSession::before_imgui_shutdown() noexcept {}

    void SpectraSceneSession::after_imgui_created() {}

    std::shared_ptr<spectra::scene::SceneWorkspace> SpectraSceneSession::document_workspace() const {
        return this->workspace;
    }

    void SpectraSceneSession::register_translation_target(spectra::scene::SceneTranslationTarget target) {
        if (target.rendererName.empty()) throw std::runtime_error("Scene translation target renderer name must not be empty");
        if (!target.analyze) throw std::runtime_error(std::format("Scene translation target \"{}\" has no analyzer", target.rendererName));
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (const spectra::scene::SceneTranslationTarget& existing_target : this->translation_targets) {
                if (existing_target.rendererName == target.rendererName) throw std::runtime_error(std::format("Duplicate scene translation target \"{}\"", target.rendererName));
            }
            this->translation_targets.push_back(std::move(target));
        }
    }

    void SpectraSceneSession::set_active_renderer(const std::string_view renderer_name) {
        std::string next_renderer{renderer_name};
        bool changed = false;
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
            changed = this->active_renderer != next_renderer;
            this->active_renderer = next_renderer;
        }
        if (changed) console_status("renderer", std::format("active: {}", next_renderer));
    }

    void SpectraSceneSession::load_first_supported_scene(const std::string_view renderer_name) {
        console_progress("scene", std::format("selecting startup scene for {}", renderer_name));
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
        }
        {
            spectra::scene::SceneCatalogEntry entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                if (this->active_scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra active scene index is out of range while selecting the initial renderer scene");
                entry = this->scene_catalog.entries[this->active_scene_index];
            }
            if (entry.state == spectra::scene::SceneCatalogEntryState::Ready && entry.document != nullptr) {
                const spectra::scene::SceneTranslationReport report = this->analyze_document(renderer_name, *entry.document);
                if (report.supported) {
                    this->set_active_renderer(renderer_name);
                    console_status("scene", std::format("startup scene: {}", entry.id));
                    return;
                }
                console_warning("scene", std::format("initial scene unsupported by {}: {}", renderer_name, first_diagnostic_message(report.diagnostics)));
            }
        }
        std::chrono::steady_clock::time_point next_startup_progress = std::chrono::steady_clock::now() + background_console_interval;
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            spectra::scene::SceneCatalogEntry entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                entry = this->scene_catalog.entries[scene_index];
            }
            if (entry.state == spectra::scene::SceneCatalogEntryState::Pending) {
                const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                if (now >= next_startup_progress) {
                    console_progress("scene", std::format("startup search: {}/{} checked", scene_index, this->scene_catalog.entries.size()));
                    next_startup_progress = now + background_console_interval;
                }
                spectra::scene::ValidateSceneCatalogEntry(entry);
                {
                    std::scoped_lock lock{this->scene_catalog_mutex};
                    if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog changed while selecting the initial scene");
                    if (this->scene_catalog.entries[scene_index].id != entry.id) throw std::runtime_error("Spectra scene catalog order changed while selecting the initial scene");
                    this->scene_catalog.entries[scene_index] = entry;
                    this->clear_translation_cache_for_scene(entry.id);
                    this->refresh_scene_catalog_counts();
                }
            }
            if (entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) continue;
            const spectra::scene::SceneTranslationReport report = this->analyze_document(renderer_name, *entry.document);
            if (!report.supported) continue;
            this->commit_document(scene_index, *entry.document);
            this->set_active_renderer(renderer_name);
            console_status("scene", std::format("startup scene: {} (fallback)", entry.id));
            return;
        }
        throw std::runtime_error(std::format("No Spectra scene can be translated for renderer \"{}\"", renderer_name));
    }

    SpectraRendererAvailability SpectraSceneSession::renderer_availability(const std::string_view renderer_name) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            bool found = false;
            for (const spectra::scene::SceneTranslationTarget& target : this->translation_targets)
                if (target.rendererName == renderer_name) found = true;
            if (!found) {
                return SpectraRendererAvailability{
                    .available = false,
                    .detail    = std::format("Renderer \"{}\" has no scene translator", renderer_name),
                };
            }
        }

        try {
            const std::shared_ptr<const spectra::scene::SceneSnapshot> document = this->workspace->snapshot();
            const spectra::scene::SceneTranslationReport report                 = this->analyze_document(renderer_name, *document);
            return this->availability_from_report(renderer_name, report);
        } catch (const std::exception& error) {
            return SpectraRendererAvailability{
                .available = false,
                .detail    = error.what(),
            };
        }
    }

    void SpectraSceneSession::attach_panel_host(std::move_only_function<void(SpectraPanel)> register_panel) {
        if (this->attached) throw std::runtime_error("Spectra scene session is already attached");
        if (!register_panel) throw std::runtime_error("Spectra scene session requires a panel registration callback");
        register_panel(SpectraPanel{
            .id             = "scene.library",
            .title          = "Scene Library",
            .icon           = ICON_MS_FOLDER_OPEN,
            .shortcut_label = "F2",
            .shortcut_key   = ImGuiKey_F2,
            .dock_slot      = SpectraDockSlot::Left,
            .closable       = false,
            .show_in_menu   = false,
            .draw           = [this] { this->draw_scene_library_window(); },
        });
        this->attached = true;
        this->start_scene_background_workers();
    }

    void SpectraSceneSession::start_scene_background_workers() {
        if (!this->scene_background_workers.empty()) throw std::runtime_error("Spectra scene background workers are already running");
        std::string start_message{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->scene_catalog_validation_claimed.assign(this->scene_catalog.entries.size(), false);
            this->reset_background_console_state_locked(std::chrono::steady_clock::now());
            start_message = std::format("background check: {} pending scenes, {} workers", this->scene_catalog.pending_count, scene_background_worker_count);
        }
        console_status("scene", start_message);
        this->scene_background_workers.reserve(scene_background_worker_count);
        for (std::size_t worker_index = 0; worker_index < scene_background_worker_count; ++worker_index) {
            this->scene_background_workers.emplace_back([this, worker_index](const std::stop_token stop_token) { this->run_scene_background_worker(stop_token, worker_index); });
        }
        this->scene_background_condition.notify_all();
    }

    void SpectraSceneSession::run_scene_background_worker(const std::stop_token stop_token, const std::size_t worker_index) {
        while (!stop_token.stop_requested()) {
            std::optional<std::size_t> validation_index{};
            spectra::scene::SceneCatalogEntry validation_entry{};
            std::optional<TranslationRequest> translation_request{};
            std::function<spectra::scene::SceneTranslationReport(const spectra::scene::SceneSnapshot&)> analyze{};
            {
                std::unique_lock lock{this->scene_catalog_mutex};
                if (!this->scene_background_condition.wait(lock, stop_token, [this] { return this->has_scene_background_work_locked(); })) return;
                validation_index = this->next_catalog_validation_index_locked();
                if (validation_index.has_value()) {
                    this->scene_catalog_validation_claimed[*validation_index] = true;
                    validation_entry = this->scene_catalog.entries[*validation_index];
                    this->begin_background_scene_task_locked(worker_index, validation_entry.id, std::chrono::steady_clock::now());
                } else {
                    translation_request = std::move(this->translation_requests.front());
                    this->translation_requests.pop_front();
                    this->translation_requests_in_progress.push_back(translation_request->key);
                    this->begin_background_translation_task_locked(worker_index, translation_request->key, std::chrono::steady_clock::now());
                    for (const spectra::scene::SceneTranslationTarget& target : this->translation_targets) {
                        if (target.rendererName != translation_request->key.rendererName) continue;
                        analyze = target.analyze;
                        break;
                    }
                    if (!analyze) throw std::runtime_error(std::format("Renderer \"{}\" has no scene translator", translation_request->key.rendererName));
                }
            }

            if (validation_index.has_value()) {
                if (stop_token.stop_requested()) return;
                spectra::scene::ValidateSceneCatalogEntry(validation_entry, stop_token);
                {
                    std::scoped_lock lock{this->scene_catalog_mutex};
                    this->finish_background_scene_task_locked(worker_index, std::chrono::steady_clock::now());
                    if (stop_token.stop_requested()) return;
                    if (*validation_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog validation index is out of range");
                    if (this->scene_catalog.entries[*validation_index].id != validation_entry.id) throw std::runtime_error("Spectra scene catalog changed while validating");
                    this->scene_catalog.entries[*validation_index] = std::move(validation_entry);
                    this->scene_catalog_validation_claimed[*validation_index] = false;
                    this->clear_translation_cache_for_scene(this->scene_catalog.entries[*validation_index].id);
                    this->refresh_scene_catalog_counts();
                }
                this->maybe_log_background_heartbeat();
                this->scene_background_condition.notify_all();
                continue;
            }

            spectra::scene::SceneTranslationReport report{.target = translation_request->key.rendererName};
            try {
                if (stop_token.stop_requested()) return;
                report = analyze(*translation_request->document);
                if (report.target.empty()) report.target = translation_request->key.rendererName;
            } catch (const std::exception& error) {
                report.supported = false;
                report.diagnostics.push_back(spectra::scene::SceneDiagnostic{
                    .source  = spectra::scene::SceneSourceLocation{.filename = translation_request->document->source, .line = 1, .column = 1},
                    .message = error.what(),
                });
            }

            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                bool catalog_entry_still_matches = false;
                std::erase_if(this->translation_requests_in_progress, [this, &translation_request](const TranslationRequestKey& key) { return this->translation_request_key_matches(key, translation_request->key); });
                if (translation_request->sceneIndex < this->scene_catalog.entries.size()) {
                    const spectra::scene::SceneCatalogEntry& entry = this->scene_catalog.entries[translation_request->sceneIndex];
                    catalog_entry_still_matches                    = entry.document != nullptr && entry.document->name == translation_request->key.sceneId && entry.document->revision == translation_request->key.revision;
                }
                if (catalog_entry_still_matches && !this->has_translation_cache_entry_locked(translation_request->key)) {
                    this->translation_cache.push_back(TranslationCacheEntry{
                        .rendererName = translation_request->key.rendererName,
                        .sceneId      = translation_request->key.sceneId,
                        .revision     = translation_request->key.revision,
                        .report       = std::move(report),
                    });
                }
                this->finish_background_translation_task_locked(worker_index, std::chrono::steady_clock::now());
            }
            this->maybe_log_background_heartbeat();
        }
    }

    void SpectraSceneSession::refresh_scene_catalog_counts() {
        this->scene_catalog.pending_count = 0;
        this->scene_catalog.ready_count   = 0;
        this->scene_catalog.invalid_count = 0;
        for (const spectra::scene::SceneCatalogEntry& entry : this->scene_catalog.entries) {
            switch (entry.state) {
            case spectra::scene::SceneCatalogEntryState::Pending: ++this->scene_catalog.pending_count; break;
            case spectra::scene::SceneCatalogEntryState::Ready: ++this->scene_catalog.ready_count; break;
            case spectra::scene::SceneCatalogEntryState::Invalid: ++this->scene_catalog.invalid_count; break;
            }
        }
    }

    void SpectraSceneSession::clear_translation_cache_for_scene(const std::string_view scene_id) {
        std::erase_if(this->translation_cache, [scene_id](const TranslationCacheEntry& entry) { return entry.sceneId == scene_id; });
    }

    void SpectraSceneSession::ensure_translation_target_exists(const std::string_view renderer_name) const {
        for (const spectra::scene::SceneTranslationTarget& target : this->translation_targets)
            if (target.rendererName == renderer_name) return;
        throw std::runtime_error(std::format("Renderer \"{}\" has no scene translator", renderer_name));
    }

    bool SpectraSceneSession::has_scene_background_work_locked() const {
        if (!this->translation_requests.empty()) return true;
        return this->next_catalog_validation_index_locked().has_value();
    }

    std::optional<std::size_t> SpectraSceneSession::next_catalog_validation_index_locked() const {
        if (this->scene_catalog_validation_claimed.size() != this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog validation claim table is out of sync");
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            if (this->scene_catalog.entries[scene_index].state != spectra::scene::SceneCatalogEntryState::Pending) continue;
            if (this->scene_catalog_validation_claimed[scene_index]) continue;
            return scene_index;
        }
        return {};
    }

    void SpectraSceneSession::reset_background_console_state_locked(const std::chrono::steady_clock::time_point now) {
        this->background_console.workerTasks.assign(scene_background_worker_count, {});
        this->background_console.lastProgressAt          = now;
        this->background_console.lastHeartbeatAt         = now;
        this->background_console.sceneCheckCompleteLogged = this->scene_catalog.pending_count == 0;
    }

    void SpectraSceneSession::begin_background_scene_task_locked(const std::size_t worker_index, const std::string_view scene_id, const std::chrono::steady_clock::time_point now) {
        if (worker_index >= this->background_console.workerTasks.size()) throw std::runtime_error("Spectra background scene worker index is out of range");
        this->background_console.workerTasks[worker_index] = BackgroundWorkerTask{
            .kind         = BackgroundTaskKind::SceneValidation,
            .sceneId      = std::string{scene_id},
            .rendererName = {},
            .startedAt    = now,
        };
        this->background_console.lastProgressAt = now;
    }

    void SpectraSceneSession::finish_background_scene_task_locked(const std::size_t worker_index, const std::chrono::steady_clock::time_point now) {
        if (worker_index >= this->background_console.workerTasks.size()) throw std::runtime_error("Spectra background scene worker index is out of range");
        this->background_console.workerTasks[worker_index] = BackgroundWorkerTask{};
        this->background_console.lastProgressAt            = now;
    }

    void SpectraSceneSession::begin_background_translation_task_locked(const std::size_t worker_index, const TranslationRequestKey& key, const std::chrono::steady_clock::time_point now) {
        if (worker_index >= this->background_console.workerTasks.size()) throw std::runtime_error("Spectra background translation worker index is out of range");
        this->background_console.workerTasks[worker_index] = BackgroundWorkerTask{
            .kind         = BackgroundTaskKind::Translation,
            .sceneId      = key.sceneId,
            .rendererName = key.rendererName,
            .startedAt    = now,
        };
        this->background_console.lastProgressAt = now;
    }

    void SpectraSceneSession::finish_background_translation_task_locked(const std::size_t worker_index, const std::chrono::steady_clock::time_point now) {
        if (worker_index >= this->background_console.workerTasks.size()) throw std::runtime_error("Spectra background translation worker index is out of range");
        this->background_console.workerTasks[worker_index] = BackgroundWorkerTask{};
        this->background_console.lastProgressAt            = now;
    }

    bool SpectraSceneSession::scene_check_complete_locked() const {
        if (this->scene_catalog.pending_count != 0) return false;
        for (const BackgroundWorkerTask& task : this->background_console.workerTasks)
            if (task.kind == BackgroundTaskKind::SceneValidation) return false;
        return true;
    }

    std::size_t SpectraSceneSession::active_background_task_count_locked(const BackgroundTaskKind kind) const {
        return static_cast<std::size_t>(std::ranges::count_if(this->background_console.workerTasks, [kind](const BackgroundWorkerTask& task) { return task.kind == kind; }));
    }

    std::size_t SpectraSceneSession::cached_translation_report_count_locked(const std::string_view renderer_name) const {
        if (renderer_name.empty()) return 0;
        return static_cast<std::size_t>(std::ranges::count_if(this->translation_cache, [renderer_name](const TranslationCacheEntry& entry) { return entry.rendererName == renderer_name; }));
    }

    std::optional<std::string> SpectraSceneSession::active_background_task_text_locked(const std::chrono::steady_clock::time_point now) const {
        const BackgroundWorkerTask* selected_task = nullptr;
        for (const BackgroundWorkerTask& task : this->background_console.workerTasks) {
            if (task.kind == BackgroundTaskKind::Idle) continue;
            if (selected_task == nullptr || task.startedAt < selected_task->startedAt) selected_task = &task;
        }
        if (selected_task == nullptr) return {};
        const std::string elapsed_text = elapsed_seconds_text(now - selected_task->startedAt);
        if (selected_task->kind == BackgroundTaskKind::SceneValidation) return std::format("{} ({})", compact_console_text(selected_task->sceneId), elapsed_text);
        if (selected_task->kind == BackgroundTaskKind::Translation) return std::format("{} -> {} ({})", compact_console_text(selected_task->sceneId), selected_task->rendererName, elapsed_text);
        throw std::runtime_error("Unknown Spectra background task kind");
    }

    std::optional<SpectraSceneSession::BackgroundConsoleMessage> SpectraSceneSession::next_background_console_message_locked(const std::chrono::steady_clock::time_point now) {
        const std::size_t active_scene_count       = this->active_background_task_count_locked(BackgroundTaskKind::SceneValidation);
        const std::size_t active_translation_count = this->active_background_task_count_locked(BackgroundTaskKind::Translation);
        const bool has_scene_work                  = this->scene_catalog.pending_count != 0 || active_scene_count != 0;
        const bool has_translation_work            = !this->translation_requests.empty() || active_translation_count != 0;

        if (!this->background_console.sceneCheckCompleteLogged && this->scene_check_complete_locked()) {
            this->background_console.sceneCheckCompleteLogged = true;
            this->background_console.lastHeartbeatAt          = now;
            return BackgroundConsoleMessage{
                .text     = std::format("scene check complete: {}", catalog_status_text(this->scene_catalog)),
                .progress = false,
            };
        }

        if (!has_scene_work && !has_translation_work) return {};
        if (now - this->background_console.lastHeartbeatAt < background_console_interval) return {};

        this->background_console.lastHeartbeatAt = now;
        const std::optional<std::string> active_task_text = this->active_background_task_text_locked(now);
        const std::string current_text                    = active_task_text.value_or("idle");
        const std::size_t active_task_count               = active_scene_count + active_translation_count;
        const bool still_working                          = active_task_count != 0 && now - this->background_console.lastProgressAt >= background_console_interval;
        if (has_scene_work) {
            const std::size_t completed_scene_count = this->scene_catalog.ready_count + this->scene_catalog.invalid_count;
            if (still_working) {
                return BackgroundConsoleMessage{
                    .text     = std::format("still working: {} active, {} pending, last progress {} ago | current: {}", active_scene_count, this->scene_catalog.pending_count, elapsed_seconds_text(now - this->background_console.lastProgressAt), current_text),
                    .progress = true,
                };
            }
            return BackgroundConsoleMessage{
                .text     = std::format("checking scenes: {}/{} done, {} ready, {} invalid, {} pending, {} active | current: {}", completed_scene_count, this->scene_catalog.entries.size(), this->scene_catalog.ready_count, this->scene_catalog.invalid_count, this->scene_catalog.pending_count, active_scene_count, current_text),
                .progress = true,
            };
        }

        if (still_working) {
            return BackgroundConsoleMessage{
                .text     = std::format("still working: {} active, {} queued, last progress {} ago | current: {}", active_translation_count, this->translation_requests.size(), elapsed_seconds_text(now - this->background_console.lastProgressAt), current_text),
                .progress = true,
            };
        }
        return BackgroundConsoleMessage{
            .text     = std::format("checking renderer support: {} cached, {} queued, {} active | current: {}", this->cached_translation_report_count_locked(this->active_renderer), this->translation_requests.size(), active_translation_count, current_text),
            .progress = true,
        };
    }

    void SpectraSceneSession::maybe_log_background_heartbeat() {
        std::optional<BackgroundConsoleMessage> message{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            message = this->next_background_console_message_locked(std::chrono::steady_clock::now());
        }
        if (!message.has_value()) return;
        if (message->progress) console_progress("scene", message->text);
        else console_status("scene", message->text);
    }

    bool SpectraSceneSession::translation_request_key_matches(const TranslationRequestKey& lhs, const TranslationRequestKey& rhs) {
        return lhs.rendererName == rhs.rendererName && lhs.sceneId == rhs.sceneId && lhs.revision == rhs.revision;
    }

    bool SpectraSceneSession::has_translation_cache_entry_locked(const TranslationRequestKey& key) const {
        for (const TranslationCacheEntry& cache_entry : this->translation_cache) {
            const TranslationRequestKey cache_key{
                .rendererName = cache_entry.rendererName,
                .sceneId      = cache_entry.sceneId,
                .revision     = cache_entry.revision,
            };
            if (translation_request_key_matches(cache_key, key)) return true;
        }
        return false;
    }

    bool SpectraSceneSession::has_translation_request_locked(const TranslationRequestKey& key) const {
        for (const TranslationRequest& request : this->translation_requests)
            if (translation_request_key_matches(request.key, key)) return true;
        for (const TranslationRequestKey& request_key : this->translation_requests_in_progress)
            if (translation_request_key_matches(request_key, key)) return true;
        return false;
    }

    spectra::scene::SceneTranslationReport SpectraSceneSession::analyze_document(const std::string_view renderer_name, const spectra::scene::SceneSnapshot& document) {
        std::function<spectra::scene::SceneTranslationReport(const spectra::scene::SceneSnapshot&)> analyze{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (TranslationCacheEntry& cache_entry : this->translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            for (spectra::scene::SceneTranslationTarget& target : this->translation_targets) {
                if (target.rendererName != renderer_name) continue;
                analyze = target.analyze;
                break;
            }
        }

        if (!analyze) throw std::runtime_error(std::format("Renderer \"{}\" has no scene translator", renderer_name));

        spectra::scene::SceneTranslationReport report = analyze(document);
        if (report.target.empty()) report.target = std::string{renderer_name};

        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (TranslationCacheEntry& cache_entry : this->translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            this->translation_cache.push_back(TranslationCacheEntry{
                .rendererName = std::string{renderer_name},
                .sceneId      = document.name,
                .revision     = document.revision,
                .report       = report,
            });
        }
        return report;
    }

    SpectraRendererAvailability SpectraSceneSession::availability_from_report(const std::string_view renderer_name, const spectra::scene::SceneTranslationReport& report) const {
        if (report.supported) {
            return SpectraRendererAvailability{
                .available = true,
                .detail    = std::format("{} can render the active scene", renderer_name),
            };
        }
        std::string detail = std::format("{} cannot translate the active scene", renderer_name);
        if (!report.diagnostics.empty()) detail = std::format("{}: {}", detail, report.diagnostics.front().message);
        return SpectraRendererAvailability{
            .available = false,
            .detail    = std::move(detail),
        };
    }

    SpectraSceneSession::DisplayState SpectraSceneSession::display_state(const spectra::scene::SceneCatalogEntry& entry, const std::optional<spectra::scene::SceneTranslationReport>& report, const bool renderer_report_required) const {
        if (entry.state == spectra::scene::SceneCatalogEntryState::Pending) return DisplayState::Checking;
        if (entry.state == spectra::scene::SceneCatalogEntryState::Invalid) return DisplayState::Invalid;
        if (entry.state != spectra::scene::SceneCatalogEntryState::Ready) throw std::runtime_error("Unknown Spectra scene catalog entry state");
        if (renderer_report_required && !report.has_value()) return DisplayState::Checking;
        if (report.has_value() && !report->supported) return DisplayState::Unsupported;
        return DisplayState::Ready;
    }

    std::optional<spectra::scene::SceneTranslationReport> SpectraSceneSession::cached_entry_report(const std::string_view renderer_name, const spectra::scene::SceneCatalogEntry& entry) const {
        if (renderer_name.empty() || entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) return {};
        std::scoped_lock lock{this->scene_catalog_mutex};
        for (const TranslationCacheEntry& cache_entry : this->translation_cache) {
            if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == entry.document->name && cache_entry.revision == entry.document->revision) return cache_entry.report;
        }
        return {};
    }

    void SpectraSceneSession::request_entry_report_analysis(const std::string_view renderer_name, const std::size_t scene_index, const spectra::scene::SceneCatalogEntry& entry) {
        if (renderer_name.empty() || entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) return;
        TranslationRequest request{
            .key =
                TranslationRequestKey{
                    .rendererName = std::string{renderer_name},
                    .sceneId      = entry.document->name,
                    .revision     = entry.document->revision,
                },
            .sceneIndex = scene_index,
            .document   = entry.document,
        };
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
            if (this->has_translation_cache_entry_locked(request.key)) return;
            if (this->has_translation_request_locked(request.key)) return;
            this->translation_requests.push_back(std::move(request));
        }
        this->scene_background_condition.notify_one();
    }

    void SpectraSceneSession::commit_document(const std::size_t scene_index, const spectra::scene::SceneSnapshot& document) {
        if (this->workspace == nullptr) throw std::runtime_error("Spectra scene session document workspace is unavailable");
        if (!this->workspace->loaded()) {
            *this->workspace = spectra::scene::SceneWorkspace{document};
        } else {
            spectra::scene::SceneEditBuilder edit{};
            edit.replaceSnapshot(document, spectra::scene::SceneDirtyFlags::Snapshot);
            const spectra::scene::SceneEditBatch edit_batch = this->workspace->commit(std::move(edit));
            if (edit_batch.dirty != spectra::scene::SceneDirtyFlags::Snapshot) throw std::runtime_error("Spectra scene session failed to commit a scene snapshot replacement");
        }
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            this->active_scene_index           = scene_index;
            this->scene_library.selected_index = scene_index;
            this->scene_library.load_error     = {};
        }
    }

    void SpectraSceneSession::load_scene(const std::size_t scene_index) {
        spectra::scene::SceneCatalogEntry entry{};
        std::string renderer_name{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            entry         = this->scene_catalog.entries[scene_index];
            renderer_name = this->active_renderer;
        }
        console_progress("scene", std::format("loading: {}", entry.id));
        if (entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) throw std::runtime_error(std::format("Cannot load disabled Spectra scene \"{}\"", entry.id));
        if (!renderer_name.empty()) {
            const spectra::scene::SceneTranslationReport report = this->analyze_document(renderer_name, *entry.document);
            if (!report.supported) {
                throw std::runtime_error(std::format("Cannot load Spectra scene \"{}\" for renderer \"{}\": {}", entry.id, renderer_name, first_diagnostic_message(report.diagnostics)));
            }
        }
        this->commit_document(scene_index, *entry.document);
        const std::string renderer_suffix = renderer_name.empty() ? std::string{} : std::format(" for {}", renderer_name);
        console_status("scene", std::format("loaded: {}{}", entry.id, renderer_suffix));
    }

    void SpectraSceneSession::draw_scene_library_window() {
        this->maybe_log_background_heartbeat();

        spectra::scene::SceneCatalog catalog_snapshot{};
        std::size_t active_scene_index_snapshot{};
        std::size_t selected_scene_index_snapshot{};
        int filter_snapshot{};
        std::optional<std::string> load_error_snapshot{};
        std::string active_renderer_snapshot{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            catalog_snapshot              = this->scene_catalog;
            active_scene_index_snapshot   = this->active_scene_index;
            selected_scene_index_snapshot = this->scene_library.selected_index;
            filter_snapshot               = this->scene_library.filter;
            load_error_snapshot           = this->scene_library.load_error;
            active_renderer_snapshot      = this->active_renderer;
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) selected_scene_index_snapshot = active_scene_index_snapshot;

        ImGui::TextUnformatted("Scenes");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu parsed / %zu total", catalog_snapshot.ready_count, catalog_snapshot.entries.size());
        if (!active_renderer_snapshot.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", active_renderer_snapshot.c_str());
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##SpectraSceneSearch", this->scene_library.search.data(), this->scene_library.search.size(), ImGuiInputTextFlags_AutoSelectAll)) {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->scene_library.selected_index = active_scene_index_snapshot;
        }

        int filter = filter_snapshot;
        ImGui::RadioButton("All", &filter, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Ready", &filter, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Unsupported", &filter, 2);
        ImGui::RadioButton("Invalid", &filter, 3);
        ImGui::SameLine();
        ImGui::RadioButton("Checking", &filter, 4);
        if (filter != filter_snapshot) {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->scene_library.filter = filter;
            filter_snapshot            = filter;
        }

        const std::string_view search_text{this->scene_library.search.data()};
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
        const float table_height              = std::max(180.0f, ImGui::GetContentRegionAvail().y * 0.55f);
        if (ImGui::BeginTable("SpectraSceneLibraryTable", 4, table_flags, ImVec2{0.0f, table_height})) {
            ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Renderer", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (std::size_t scene_index = 0; scene_index < catalog_snapshot.entries.size(); ++scene_index) {
                const spectra::scene::SceneCatalogEntry& entry = catalog_snapshot.entries[scene_index];
                if (!contains_case_insensitive(entry.id, search_text) && !contains_case_insensitive(entry.displayName, search_text) && !contains_case_insensitive(entry.group, search_text)) continue;
                std::optional<spectra::scene::SceneTranslationReport> report = this->cached_entry_report(active_renderer_snapshot, entry);
                if (!active_renderer_snapshot.empty() && entry.state == spectra::scene::SceneCatalogEntryState::Ready && entry.document != nullptr && !report.has_value()) this->request_entry_report_analysis(active_renderer_snapshot, scene_index, entry);
                const DisplayState state = this->display_state(entry, report, !active_renderer_snapshot.empty());
                if (!display_filter_matches(state, filter_snapshot)) continue;

                const bool selected = scene_index == selected_scene_index_snapshot;
                const bool active   = scene_index == active_scene_index_snapshot;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string label = active ? std::format("{} {}", ICON_MS_RADIO_BUTTON_CHECKED, entry.displayName) : entry.displayName;
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_scene_index_snapshot = scene_index;
                    std::scoped_lock lock{this->scene_catalog_mutex};
                    this->scene_library.selected_index = scene_index;
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.id.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", entry.group.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(display_state_color(state), "%s", display_state_text(state));
                ImGui::TableSetColumnIndex(3);
                if (entry.info.has_value())
                    ImGui::TextUnformatted(entry.info->integrator.c_str());
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) return;
        const spectra::scene::SceneCatalogEntry& selected_entry = catalog_snapshot.entries[selected_scene_index_snapshot];
        std::optional<spectra::scene::SceneTranslationReport> selected_report = this->cached_entry_report(active_renderer_snapshot, selected_entry);
        if (!active_renderer_snapshot.empty() && selected_entry.state == spectra::scene::SceneCatalogEntryState::Ready && selected_entry.document != nullptr && !selected_report.has_value()) this->request_entry_report_analysis(active_renderer_snapshot, selected_scene_index_snapshot, selected_entry);
        const DisplayState selected_state = this->display_state(selected_entry, selected_report, !active_renderer_snapshot.empty());
        ImGui::SeparatorText("Selected");
        ImGui::TextUnformatted(selected_entry.displayName.c_str());
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        ImGui::TextColored(display_state_color(selected_state), "%s", display_state_text(selected_state));

        const bool selected_ready  = selected_state == DisplayState::Ready;
        const bool selected_active = selected_scene_index_snapshot == active_scene_index_snapshot;
        ImGui::BeginDisabled(!selected_ready || selected_active);
        if (ImGui::Button(ICON_MS_PLAY_ARROW " Load", ImVec2{-1.0f, 0.0f})) {
            try {
                this->load_scene(selected_scene_index_snapshot);
            } catch (const std::exception& error) {
                console_error("scene", std::format("load failed: {}", error.what()));
                std::scoped_lock lock{this->scene_catalog_mutex};
                this->scene_library.load_error = error.what();
            }
        }
        ImGui::EndDisabled();
        if (selected_active) ImGui::TextColored(display_state_color(DisplayState::Ready), "Loaded");

        if (load_error_snapshot.has_value()) {
            ImGui::SeparatorText("Last Load Error");
            ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", load_error_snapshot->c_str());
        }

        if (selected_entry.info.has_value()) {
            constexpr ImGuiTableFlags info_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("SpectraSceneLibraryInfo", 2, info_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                const auto row = [](const char* label, const std::string& value) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(value.c_str());
                };
                row("Camera", selected_entry.info->camera);
                row("Sampler", selected_entry.info->sampler);
                row("Integrator", selected_entry.info->integrator);
                row("Shapes", std::format("{}", selected_entry.info->shape_count));
                row("Materials", std::format("{}", selected_entry.info->material_count));
                row("Lights", std::format("{}", selected_entry.info->light_count));
                ImGui::EndTable();
            }
        }

        if (selected_report.has_value() && !selected_report->diagnostics.empty()) {
            ImGui::SeparatorText("Translation");
            if (ImGui::BeginChild("SpectraSceneLibraryTranslation", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                for (const spectra::scene::SceneDiagnostic& diagnostic : selected_report->diagnostics) {
                    ImGui::TextColored(display_state_color(DisplayState::Unsupported), "%s", source_location_text(diagnostic.source).c_str());
                    ImGui::TextWrapped("%s", diagnostic.message.c_str());
                    ImGui::Spacing();
                }
            }
            ImGui::EndChild();
        }

        if (!selected_entry.issues.empty()) {
            ImGui::SeparatorText("Parse Issues");
            if (ImGui::BeginChild("SpectraSceneLibraryIssues", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                for (const spectra::scene::SceneDiagnostic& issue : selected_entry.issues) {
                    ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", source_location_text(issue.source).c_str());
                    ImGui::TextWrapped("%s", issue.message.c_str());
                    ImGui::Spacing();
                }
            }
            ImGui::EndChild();
        }
    }
} // namespace xayah
