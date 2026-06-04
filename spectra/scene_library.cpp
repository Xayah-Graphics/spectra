module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module spectra.scene.library;

import spectra;
import spectra.scene.pbrt;
import std;

namespace {
    [[nodiscard]] std::string lowercase_copy(std::string_view value) {
        std::string result{value};
        for (char& character : result) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return result;
    }

    [[nodiscard]] bool contains_case_insensitive(const std::string_view value, const std::string_view pattern) {
        if (pattern.empty()) return true;
        return lowercase_copy(value).find(lowercase_copy(pattern)) != std::string::npos;
    }

    [[nodiscard]] const char* display_state_text(const spectra::scene::SceneLibrary::DisplayState state) {
        switch (state) {
        case spectra::scene::SceneLibrary::DisplayState::Checking: return "Checking";
        case spectra::scene::SceneLibrary::DisplayState::Candidate: return "Candidate";
        case spectra::scene::SceneLibrary::DisplayState::Loaded: return "Loaded";
        case spectra::scene::SceneLibrary::DisplayState::Unsupported: return "Unsupported";
        case spectra::scene::SceneLibrary::DisplayState::Invalid: return "Invalid";
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] ImVec4 display_state_color(const spectra::scene::SceneLibrary::DisplayState state) {
        switch (state) {
        case spectra::scene::SceneLibrary::DisplayState::Checking: return ImVec4{0.620f, 0.660f, 0.720f, 1.0f};
        case spectra::scene::SceneLibrary::DisplayState::Candidate: return ImVec4{0.220f, 0.720f, 0.480f, 1.0f};
        case spectra::scene::SceneLibrary::DisplayState::Loaded: return ImVec4{0.300f, 0.760f, 0.950f, 1.0f};
        case spectra::scene::SceneLibrary::DisplayState::Unsupported: return ImVec4{0.930f, 0.680f, 0.230f, 1.0f};
        case spectra::scene::SceneLibrary::DisplayState::Invalid: return ImVec4{0.900f, 0.300f, 0.300f, 1.0f};
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] bool display_filter_matches(const spectra::scene::SceneLibrary::DisplayState state, const int filter) {
        if (filter == 0) return true;
        if (filter == 1) return state == spectra::scene::SceneLibrary::DisplayState::Candidate || state == spectra::scene::SceneLibrary::DisplayState::Loaded;
        if (filter == 2) return state == spectra::scene::SceneLibrary::DisplayState::Unsupported;
        if (filter == 3) return state == spectra::scene::SceneLibrary::DisplayState::Invalid;
        if (filter == 4) return state == spectra::scene::SceneLibrary::DisplayState::Checking;
        throw std::runtime_error("Unknown Spectra scene display filter");
    }

    [[nodiscard]] std::string source_location_text(const spectra::scene::SceneSourceLocation& source) {
        return std::format("{}:{}:{}", source.filename, source.line, source.column);
    }

    [[nodiscard]] std::size_t visible_scene_count(const spectra::scene::PbrtSceneCatalog& catalog) {
        if (catalog.non_scene_count > catalog.entries.size()) throw std::runtime_error("Spectra scene catalog non-scene count exceeds total entry count");
        return catalog.entries.size() - catalog.non_scene_count;
    }

    [[nodiscard]] std::string first_diagnostic_message(const std::vector<spectra::scene::SceneDiagnostic>& diagnostics) {
        if (diagnostics.empty()) return "no diagnostics";
        return diagnostics.front().message;
    }

    [[nodiscard]] std::string last_probe_feature_type(const spectra::scene::SceneProbeReport& probe, const spectra::scene::SceneProbeFeatureCategory category, std::string fallback) {
        for (const spectra::scene::SceneProbeFeature& feature : probe.features) {
            if (feature.category == category && !feature.type.empty()) fallback = feature.type;
        }
        return fallback;
    }

    constexpr std::string_view initial_scene_id = "pbrt-book/book.pbrt";
    constexpr std::size_t scene_background_worker_count = 2;
} // namespace

namespace spectra::scene {
    SceneLibrary::SceneLibrary() : workspace(std::make_shared<SceneWorkspace>()) {
        this->scene_catalog = DiscoverPbrtSceneCatalog();
        this->scene_catalog_probe_claimed.assign(this->scene_catalog.entries.size(), false);
        const std::string initial_scene_id_string{initial_scene_id};
        const auto active_scene_iter = std::ranges::find_if(this->scene_catalog.entries, [&initial_scene_id_string](const PbrtSceneCatalogEntry& entry) { return entry.id == initial_scene_id_string; });
        if (active_scene_iter == this->scene_catalog.entries.end()) throw std::runtime_error(std::format("Spectra scene catalog does not contain required initial scene \"{}\"", initial_scene_id));
        if (active_scene_iter->state == PbrtSceneCatalogEntryState::Invalid) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not parseable: {}", initial_scene_id, first_diagnostic_message(active_scene_iter->issues)));
        ProbePbrtSceneCatalogEntry(*active_scene_iter);
        if (active_scene_iter->state == PbrtSceneCatalogEntryState::Invalid) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not probeable: {}", initial_scene_id, first_diagnostic_message(active_scene_iter->issues)));
        if (active_scene_iter->state == PbrtSceneCatalogEntryState::NonScene) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not a top-level PBRT scene", initial_scene_id));
        if (active_scene_iter->state != PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("Spectra initial scene \"{}\" did not produce a candidate scene probe", initial_scene_id));
        SceneSnapshot initial_document = ParsePbrtSceneCatalogEntry(*active_scene_iter);
        active_scene_iter->revision = initial_document.revision;
        if (active_scene_iter->probe.has_value()) active_scene_iter->probe->revision = initial_document.revision;
        active_scene_iter->state = PbrtSceneCatalogEntryState::Candidate;
        active_scene_iter->issues.clear();
        this->active_scene_index           = static_cast<std::size_t>(std::distance(this->scene_catalog.entries.begin(), active_scene_iter));
        this->scene_library.selected_index = this->active_scene_index;
        *this->workspace                   = SceneWorkspace{std::move(initial_document)};
        this->refresh_scene_catalog_counts();
    }

    SceneLibrary::~SceneLibrary() noexcept {
        this->detach();
    }

    void SceneLibrary::detach() noexcept {
        this->stop_scene_background_workers_noexcept();
        this->translation_requests.clear();
        this->translation_requests_in_progress.clear();
        this->probe_translation_cache.clear();
        this->document_translation_cache.clear();
        this->scene_catalog_probe_claimed.clear();
        this->attached = false;
    }

    std::shared_ptr<SceneWorkspace> SceneLibrary::document_workspace() const {
        return this->workspace;
    }

    void SceneLibrary::register_translation_target(SceneTranslationTarget target) {
        if (target.rendererName.empty()) throw std::runtime_error("Scene translation target renderer name must not be empty");
        if (!target.probe) throw std::runtime_error(std::format("Scene translation target \"{}\" has no probe analyzer", target.rendererName));
        if (!target.analyze) throw std::runtime_error(std::format("Scene translation target \"{}\" has no analyzer", target.rendererName));
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (const SceneTranslationTarget& existing_target : this->translation_targets) {
                if (existing_target.rendererName == target.rendererName) throw std::runtime_error(std::format("Duplicate scene translation target \"{}\"", target.rendererName));
            }
            this->translation_targets.push_back(std::move(target));
        }
    }

    void SceneLibrary::set_active_renderer(const std::string_view renderer_name) {
        std::string next_renderer{renderer_name};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
            this->active_renderer = next_renderer;
        }
    }

    void SceneLibrary::load_first_supported_scene(const std::string_view renderer_name) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
        }
        {
            PbrtSceneCatalogEntry entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                if (this->active_scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra active scene index is out of range while selecting the initial renderer scene");
                entry = this->scene_catalog.entries[this->active_scene_index];
            }
            if (entry.state == PbrtSceneCatalogEntryState::Candidate) {
                const std::shared_ptr<const SceneSnapshot> document = this->workspace->snapshot();
                const SceneTranslationReport report                 = this->analyze_document(renderer_name, *document);
                if (report.supported) {
                    this->set_active_renderer(renderer_name);
                    return;
                }
            }
        }
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            PbrtSceneCatalogEntry entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                entry = this->scene_catalog.entries[scene_index];
            }
            if (entry.state == PbrtSceneCatalogEntryState::Pending) {
                ProbePbrtSceneCatalogEntry(entry);
                {
                    std::scoped_lock lock{this->scene_catalog_mutex};
                    if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog changed while selecting the initial scene");
                    if (this->scene_catalog.entries[scene_index].id != entry.id) throw std::runtime_error("Spectra scene catalog order changed while selecting the initial scene");
                    this->scene_catalog.entries[scene_index] = entry;
                    this->clear_scene_translation_caches(entry.id);
                    this->refresh_scene_catalog_counts();
                }
            }
            if (entry.state != PbrtSceneCatalogEntryState::Candidate || !entry.probe.has_value()) continue;
            const SceneTranslationReport probe_report = this->analyze_probe(renderer_name, *entry.probe);
            if (!probe_report.supported) continue;
            SceneSnapshot document              = ParsePbrtSceneCatalogEntry(entry);
            const SceneTranslationReport report = this->analyze_document(renderer_name, document);
            if (!report.supported) continue;
            this->commit_document(scene_index, std::move(document));
            this->set_active_renderer(renderer_name);
            return;
        }
        throw std::runtime_error(std::format("No Spectra scene can be translated for renderer \"{}\"", renderer_name));
    }

    SpectraRendererAvailability SceneLibrary::renderer_availability(const std::string_view renderer_name) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            bool found = false;
            for (const SceneTranslationTarget& target : this->translation_targets)
                if (target.rendererName == renderer_name) found = true;
            if (!found) {
                return SpectraRendererAvailability{
                    .available = false,
                    .detail    = std::format("Renderer \"{}\" has no scene translator", renderer_name),
                };
            }
        }

        try {
            const std::shared_ptr<const SceneSnapshot> document = this->workspace->snapshot();
            const SceneTranslationReport report                 = this->analyze_document(renderer_name, *document);
            return this->availability_from_report(renderer_name, report);
        } catch (const std::exception& error) {
            return SpectraRendererAvailability{
                .available = false,
                .detail    = error.what(),
            };
        }
    }

    void SceneLibrary::attach_panel_host(std::move_only_function<void(SpectraPanel)> register_panel) {
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

    void SceneLibrary::start_scene_background_workers() {
        if (!this->scene_background_workers.empty()) throw std::runtime_error("Spectra scene background workers are already running");
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->scene_catalog_probe_claimed.assign(this->scene_catalog.entries.size(), false);
        }
        this->scene_background_workers.reserve(scene_background_worker_count);
        for (std::size_t worker_index = 0; worker_index < scene_background_worker_count; ++worker_index) {
            this->scene_background_workers.emplace_back([this](const std::stop_token stop_token) { this->run_scene_background_worker(stop_token); });
        }
        this->scene_background_condition.notify_all();
    }

    void SceneLibrary::stop_scene_background_workers_noexcept() noexcept {
        try {
            for (std::jthread& worker : this->scene_background_workers) worker.request_stop();
            this->scene_background_condition.notify_all();
            for (std::jthread& worker : this->scene_background_workers) {
                if (worker.joinable()) worker.join();
            }
            this->scene_background_workers.clear();
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                this->translation_requests_in_progress.clear();
            }
        } catch (...) {
        }
    }

    void SceneLibrary::stop_scene_background_workers_if_idle() noexcept {
        bool should_stop = false;
        try {
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                should_stop = !this->scene_background_workers.empty() && this->scene_catalog.pending_count == 0 && this->translation_requests.empty() && this->translation_requests_in_progress.empty();
            }
            if (should_stop) this->stop_scene_background_workers_noexcept();
        } catch (...) {
        }
    }

    void SceneLibrary::run_scene_background_worker(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::optional<std::size_t> probe_index{};
            PbrtSceneCatalogEntry probe_entry{};
            std::optional<TranslationRequest> translation_request{};
            std::function<SceneTranslationReport(const SceneProbeReport&)> probe_analyze{};
            {
                std::unique_lock lock{this->scene_catalog_mutex};
                if (!this->scene_background_condition.wait(lock, stop_token, [this] { return this->has_scene_background_work_locked(); })) return;
                probe_index = this->next_catalog_probe_index_locked();
                if (probe_index.has_value()) {
                    this->scene_catalog_probe_claimed[*probe_index] = true;
                    probe_entry = this->scene_catalog.entries[*probe_index];
                } else {
                    translation_request = std::move(this->translation_requests.front());
                    this->translation_requests.pop_front();
                    this->translation_requests_in_progress.push_back(translation_request->key);
                    for (const SceneTranslationTarget& target : this->translation_targets) {
                        if (target.rendererName != translation_request->key.rendererName) continue;
                        probe_analyze = target.probe;
                        break;
                    }
                    if (!probe_analyze) throw std::runtime_error(std::format("Renderer \"{}\" has no scene probe translator", translation_request->key.rendererName));
                }
            }

            if (probe_index.has_value()) {
                if (stop_token.stop_requested()) return;
                ProbePbrtSceneCatalogEntry(probe_entry, stop_token);
                {
                    std::scoped_lock lock{this->scene_catalog_mutex};
                    if (stop_token.stop_requested()) return;
                    if (*probe_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog probe index is out of range");
                    if (this->scene_catalog.entries[*probe_index].id != probe_entry.id) throw std::runtime_error("Spectra scene catalog changed while probing");
                    this->scene_catalog.entries[*probe_index] = std::move(probe_entry);
                    this->scene_catalog_probe_claimed[*probe_index] = false;
                    this->clear_scene_translation_caches(this->scene_catalog.entries[*probe_index].id);
                    this->refresh_scene_catalog_counts();
                }
                this->scene_background_condition.notify_all();
                continue;
            }

            SceneTranslationReport report{.target = translation_request->key.rendererName};
            try {
                if (stop_token.stop_requested()) return;
                report = probe_analyze(translation_request->probe);
                if (report.target.empty()) report.target = translation_request->key.rendererName;
            } catch (const std::exception& error) {
                if (stop_token.stop_requested()) return;
                report.supported = false;
                report.diagnostics.push_back(SceneDiagnostic{
                    .source  = SceneSourceLocation{.filename = translation_request->probe.source, .line = 1, .column = 1},
                    .message = error.what(),
                });
            }

            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                bool catalog_entry_still_matches = false;
                std::erase_if(this->translation_requests_in_progress, [this, &translation_request](const TranslationRequestKey& key) { return this->translation_request_key_matches(key, translation_request->key); });
                if (translation_request->sceneIndex < this->scene_catalog.entries.size()) {
                    const PbrtSceneCatalogEntry& entry = this->scene_catalog.entries[translation_request->sceneIndex];
                    catalog_entry_still_matches = entry.state == PbrtSceneCatalogEntryState::Candidate && entry.id == translation_request->key.sceneId && entry.revision == translation_request->key.revision;
                }
                if (catalog_entry_still_matches && !this->has_probe_translation_cache_entry_locked(translation_request->key)) {
                    this->probe_translation_cache.push_back(ProbeTranslationCacheEntry{
                        .rendererName = translation_request->key.rendererName,
                        .sceneId      = translation_request->key.sceneId,
                        .revision     = translation_request->key.revision,
                        .report       = std::move(report),
                    });
                }
            }
        }
    }

    void SceneLibrary::refresh_scene_catalog_counts() {
        this->scene_catalog.pending_count = 0;
        this->scene_catalog.candidate_count = 0;
        this->scene_catalog.non_scene_count = 0;
        this->scene_catalog.invalid_count = 0;
        for (const PbrtSceneCatalogEntry& entry : this->scene_catalog.entries) {
            switch (entry.state) {
            case PbrtSceneCatalogEntryState::Pending: ++this->scene_catalog.pending_count; break;
            case PbrtSceneCatalogEntryState::Candidate: ++this->scene_catalog.candidate_count; break;
            case PbrtSceneCatalogEntryState::NonScene: ++this->scene_catalog.non_scene_count; break;
            case PbrtSceneCatalogEntryState::Invalid: ++this->scene_catalog.invalid_count; break;
            }
        }
    }

    void SceneLibrary::clear_scene_translation_caches(const std::string_view scene_id) {
        std::erase_if(this->probe_translation_cache, [scene_id](const ProbeTranslationCacheEntry& entry) { return entry.sceneId == scene_id; });
        std::erase_if(this->document_translation_cache, [scene_id](const DocumentTranslationCacheEntry& entry) { return entry.sceneId == scene_id; });
    }

    void SceneLibrary::ensure_translation_target_exists(const std::string_view renderer_name) const {
        for (const SceneTranslationTarget& target : this->translation_targets)
            if (target.rendererName == renderer_name) return;
        throw std::runtime_error(std::format("Renderer \"{}\" has no scene translator", renderer_name));
    }

    bool SceneLibrary::has_scene_background_work_locked() const {
        if (!this->translation_requests.empty()) return true;
        return this->next_catalog_probe_index_locked().has_value();
    }

    std::optional<std::size_t> SceneLibrary::next_catalog_probe_index_locked() const {
        if (this->scene_catalog_probe_claimed.size() != this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog probe claim table is out of sync");
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            if (this->scene_catalog.entries[scene_index].state != PbrtSceneCatalogEntryState::Pending) continue;
            if (this->scene_catalog_probe_claimed[scene_index]) continue;
            return scene_index;
        }
        return {};
    }

    bool SceneLibrary::translation_request_key_matches(const TranslationRequestKey& lhs, const TranslationRequestKey& rhs) {
        return lhs.rendererName == rhs.rendererName && lhs.sceneId == rhs.sceneId && lhs.revision == rhs.revision;
    }

    bool SceneLibrary::has_probe_translation_cache_entry_locked(const TranslationRequestKey& key) const {
        for (const ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
            const TranslationRequestKey cache_key{
                .rendererName = cache_entry.rendererName,
                .sceneId      = cache_entry.sceneId,
                .revision     = cache_entry.revision,
            };
            if (translation_request_key_matches(cache_key, key)) return true;
        }
        return false;
    }

    bool SceneLibrary::has_translation_request_locked(const TranslationRequestKey& key) const {
        for (const TranslationRequest& request : this->translation_requests)
            if (translation_request_key_matches(request.key, key)) return true;
        for (const TranslationRequestKey& request_key : this->translation_requests_in_progress)
            if (translation_request_key_matches(request_key, key)) return true;
        return false;
    }

    SceneTranslationReport SceneLibrary::analyze_probe(const std::string_view renderer_name, const SceneProbeReport& probe) {
        std::function<SceneTranslationReport(const SceneProbeReport&)> analyze{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
            for (SceneTranslationTarget& target : this->translation_targets) {
                if (target.rendererName != renderer_name) continue;
                analyze = target.probe;
                break;
            }
        }

        if (!analyze) throw std::runtime_error(std::format("Renderer \"{}\" has no scene probe translator", renderer_name));

        SceneTranslationReport report = analyze(probe);
        if (report.target.empty()) report.target = std::string{renderer_name};

        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
            this->probe_translation_cache.push_back(ProbeTranslationCacheEntry{
                .rendererName = std::string{renderer_name},
                .sceneId      = probe.name,
                .revision     = probe.revision,
                .report       = report,
            });
        }
        return report;
    }

    SceneTranslationReport SceneLibrary::analyze_document(const std::string_view renderer_name, const SceneSnapshot& document) {
        std::function<SceneTranslationReport(const SceneSnapshot&)> analyze{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (DocumentTranslationCacheEntry& cache_entry : this->document_translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            for (SceneTranslationTarget& target : this->translation_targets) {
                if (target.rendererName != renderer_name) continue;
                analyze = target.analyze;
                break;
            }
        }

        if (!analyze) throw std::runtime_error(std::format("Renderer \"{}\" has no scene translator", renderer_name));

        SceneTranslationReport report = analyze(document);
        if (report.target.empty()) report.target = std::string{renderer_name};

        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (DocumentTranslationCacheEntry& cache_entry : this->document_translation_cache) {
                if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            this->document_translation_cache.push_back(DocumentTranslationCacheEntry{
                .rendererName = std::string{renderer_name},
                .sceneId      = document.name,
                .revision     = document.revision,
                .report       = report,
            });
        }
        return report;
    }

    SpectraRendererAvailability SceneLibrary::availability_from_report(const std::string_view renderer_name, const SceneTranslationReport& report) const {
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

    SceneLibrary::DisplayState SceneLibrary::display_state(const PbrtSceneCatalogEntry& entry, const std::optional<SceneTranslationReport>& report, const bool renderer_report_required, const bool loaded) const {
        if (entry.state == PbrtSceneCatalogEntryState::Pending) return DisplayState::Checking;
        if (entry.state == PbrtSceneCatalogEntryState::Invalid) return DisplayState::Invalid;
        if (entry.state != PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error("Unknown Spectra scene catalog entry state");
        if (renderer_report_required && !report.has_value()) return DisplayState::Checking;
        if (report.has_value() && !report->supported) return DisplayState::Unsupported;
        if (loaded) return DisplayState::Loaded;
        return DisplayState::Candidate;
    }

    std::optional<SceneTranslationReport> SceneLibrary::cached_entry_report(const std::string_view renderer_name, const PbrtSceneCatalogEntry& entry) const {
        if (renderer_name.empty() || entry.state != PbrtSceneCatalogEntryState::Candidate || entry.revision.value == 0) return {};
        std::scoped_lock lock{this->scene_catalog_mutex};
        for (const ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
            if (cache_entry.rendererName == renderer_name && cache_entry.sceneId == entry.id && cache_entry.revision == entry.revision) return cache_entry.report;
        }
        return {};
    }

    void SceneLibrary::request_entry_report_analysis(const std::string_view renderer_name, const std::size_t scene_index, const PbrtSceneCatalogEntry& entry) {
        if (renderer_name.empty() || entry.state != PbrtSceneCatalogEntryState::Candidate) return;
        if (!entry.probe.has_value()) return;
        if (entry.revision.value == 0) throw std::runtime_error(std::format("Candidate Spectra scene \"{}\" has no catalog revision", entry.id));
        TranslationRequest request{
            .key =
                TranslationRequestKey{
                    .rendererName = std::string{renderer_name},
                    .sceneId      = entry.id,
                    .revision     = entry.revision,
                },
            .sceneIndex = scene_index,
            .probe      = *entry.probe,
        };
        bool should_start_workers = false;
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
            if (this->has_probe_translation_cache_entry_locked(request.key)) return;
            if (this->has_translation_request_locked(request.key)) return;
            this->translation_requests.push_back(std::move(request));
            should_start_workers = this->scene_background_workers.empty();
        }
        if (should_start_workers) this->start_scene_background_workers();
        else this->scene_background_condition.notify_one();
    }

    void SceneLibrary::commit_document(const std::size_t scene_index, SceneSnapshot document) {
        if (this->workspace == nullptr) throw std::runtime_error("Spectra scene session document workspace is unavailable");
        if (!this->workspace->loaded()) {
            *this->workspace = SceneWorkspace{std::move(document)};
        } else {
            SceneEditBuilder edit{};
            edit.replaceSnapshot(std::move(document), SceneDirtyFlags::Snapshot);
            const SceneEditBatch edit_batch = this->workspace->commit(std::move(edit));
            if (edit_batch.dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Spectra scene session failed to commit a scene snapshot replacement");
        }
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            this->active_scene_index           = scene_index;
            this->scene_library.selected_index = scene_index;
            this->scene_library.load_error     = {};
        }
    }

    void SceneLibrary::load_scene(const std::size_t scene_index) {
        PbrtSceneCatalogEntry entry{};
        std::string renderer_name{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            entry         = this->scene_catalog.entries[scene_index];
            renderer_name = this->active_renderer;
        }
        if (entry.state != PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("Cannot load disabled Spectra scene \"{}\"", entry.id));
        SceneSnapshot document = ParsePbrtSceneCatalogEntry(entry);
        if (!renderer_name.empty()) {
            const SceneTranslationReport report = this->analyze_document(renderer_name, document);
            if (!report.supported) {
                throw std::runtime_error(std::format("Cannot load Spectra scene \"{}\" for renderer \"{}\": {}", entry.id, renderer_name, first_diagnostic_message(report.diagnostics)));
            }
        }
        this->commit_document(scene_index, std::move(document));
    }

    void SceneLibrary::draw_scene_library_window() {
        this->stop_scene_background_workers_if_idle();

        PbrtSceneCatalog catalog_snapshot{};
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
        if (selected_scene_index_snapshot < catalog_snapshot.entries.size() && catalog_snapshot.entries[selected_scene_index_snapshot].state == PbrtSceneCatalogEntryState::NonScene) selected_scene_index_snapshot = active_scene_index_snapshot;

        ImGui::TextUnformatted("Scenes");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu candidate / %zu listed", catalog_snapshot.candidate_count, visible_scene_count(catalog_snapshot));
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
        constexpr std::array<const char*, 5> filter_labels{"All", "Candidate", "Unsupported", "Invalid", "Checking"};
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##SpectraSceneFilter", filter_labels.at(static_cast<std::size_t>(filter)))) {
            for (std::size_t filter_index = 0; filter_index < filter_labels.size(); ++filter_index) {
                const bool selected = filter == static_cast<int>(filter_index);
                if (ImGui::Selectable(filter_labels[filter_index], selected)) filter = static_cast<int>(filter_index);
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
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
            ImGui::TableSetupColumn("Integrator", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();
            for (std::size_t scene_index = 0; scene_index < catalog_snapshot.entries.size(); ++scene_index) {
                const PbrtSceneCatalogEntry& entry = catalog_snapshot.entries[scene_index];
                if (entry.state == PbrtSceneCatalogEntryState::NonScene) continue;
                if (!contains_case_insensitive(entry.id, search_text) && !contains_case_insensitive(entry.displayName, search_text) && !contains_case_insensitive(entry.group, search_text)) continue;
                std::optional<SceneTranslationReport> report = this->cached_entry_report(active_renderer_snapshot, entry);
                const bool selected = scene_index == selected_scene_index_snapshot;
                const bool active   = scene_index == active_scene_index_snapshot;
                if (!active_renderer_snapshot.empty() && entry.state == PbrtSceneCatalogEntryState::Candidate && !report.has_value()) this->request_entry_report_analysis(active_renderer_snapshot, scene_index, entry);
                const DisplayState state = this->display_state(entry, report, !active_renderer_snapshot.empty(), active);
                if (!display_filter_matches(state, filter_snapshot)) continue;

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
                if (entry.probe.has_value())
                    ImGui::TextUnformatted(last_probe_feature_type(*entry.probe, SceneProbeFeatureCategory::Integrator, "-").c_str());
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) return;
        if (catalog_snapshot.entries[selected_scene_index_snapshot].state == PbrtSceneCatalogEntryState::NonScene) return;
        const PbrtSceneCatalogEntry& selected_entry = catalog_snapshot.entries[selected_scene_index_snapshot];
        std::optional<SceneTranslationReport> selected_report = this->cached_entry_report(active_renderer_snapshot, selected_entry);
        const bool selected_active = selected_scene_index_snapshot == active_scene_index_snapshot;
        if (!active_renderer_snapshot.empty() && selected_entry.state == PbrtSceneCatalogEntryState::Candidate && !selected_report.has_value()) this->request_entry_report_analysis(active_renderer_snapshot, selected_scene_index_snapshot, selected_entry);
        const DisplayState selected_state = this->display_state(selected_entry, selected_report, !active_renderer_snapshot.empty(), selected_active);
        ImGui::SeparatorText("Selected");
        ImGui::TextUnformatted(selected_entry.displayName.c_str());
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        ImGui::TextColored(display_state_color(selected_state), "%s", display_state_text(selected_state));

        const bool selected_candidate = selected_state == DisplayState::Candidate || selected_state == DisplayState::Loaded;
        ImGui::BeginDisabled(!selected_candidate || selected_active);
        if (ImGui::Button(ICON_MS_PLAY_ARROW " Load", ImVec2{-1.0f, 0.0f})) {
            try {
                this->load_scene(selected_scene_index_snapshot);
            } catch (const std::exception& error) {
                std::scoped_lock lock{this->scene_catalog_mutex};
                this->scene_library.load_error = error.what();
            }
        }
        ImGui::EndDisabled();
        if (selected_active) ImGui::TextColored(display_state_color(DisplayState::Loaded), "Loaded");

        if (load_error_snapshot.has_value()) {
            ImGui::SeparatorText("Last Load Error");
            ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", load_error_snapshot->c_str());
        }

        if (selected_entry.probe.has_value()) {
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
                const auto count_feature = [&selected_entry](const SceneProbeFeatureCategory category) {
                    return static_cast<std::size_t>(std::ranges::count_if(selected_entry.probe->features, [category](const SceneProbeFeature& feature) { return feature.category == category; }));
                };
                row("Camera", last_probe_feature_type(*selected_entry.probe, SceneProbeFeatureCategory::Camera, "-"));
                row("Sampler", last_probe_feature_type(*selected_entry.probe, SceneProbeFeatureCategory::Sampler, "-"));
                row("Integrator", last_probe_feature_type(*selected_entry.probe, SceneProbeFeatureCategory::Integrator, "-"));
                row("Accelerator", last_probe_feature_type(*selected_entry.probe, SceneProbeFeatureCategory::Accelerator, "-"));
                row("Shapes", std::format("{}", count_feature(SceneProbeFeatureCategory::Shape)));
                row("Materials", std::format("{}", count_feature(SceneProbeFeatureCategory::Material)));
                row("Lights", std::format("{}", count_feature(SceneProbeFeatureCategory::Light)));
                ImGui::EndTable();
            }
        }

        if (selected_report.has_value() && !selected_report->diagnostics.empty()) {
            if (ImGui::CollapsingHeader("Translation", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginChild("SpectraSceneLibraryTranslation", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const SceneDiagnostic& diagnostic : selected_report->diagnostics) {
                        ImGui::TextColored(display_state_color(DisplayState::Unsupported), "%s", source_location_text(diagnostic.source).c_str());
                        ImGui::TextWrapped("%s", diagnostic.message.c_str());
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
            }
        }

        if (!selected_entry.issues.empty()) {
            if (ImGui::CollapsingHeader("Probe Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginChild("SpectraSceneLibraryIssues", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const SceneDiagnostic& issue : selected_entry.issues) {
                        ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", source_location_text(issue.source).c_str());
                        ImGui::TextWrapped("%s", issue.message.c_str());
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
            }
        }
    }
} // namespace spectra::scene
