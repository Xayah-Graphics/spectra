module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module spectra.pathtracer.pbrt.library;

import spectra.pathtracer.pbrt;
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

    [[nodiscard]] const char* display_state_text(const spectra::pathtracer::PbrtSceneLibrary::DisplayState state) {
        switch (state) {
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Checking: return "Checking";
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Candidate: return "Candidate";
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Loaded: return "Loaded";
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Unsupported: return "Unsupported";
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Invalid: return "Invalid";
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] ImVec4 display_state_color(const spectra::pathtracer::PbrtSceneLibrary::DisplayState state) {
        switch (state) {
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Checking: return ImVec4{0.620f, 0.660f, 0.720f, 1.0f};
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Candidate: return ImVec4{0.220f, 0.720f, 0.480f, 1.0f};
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Loaded: return ImVec4{0.300f, 0.760f, 0.950f, 1.0f};
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Unsupported: return ImVec4{0.930f, 0.680f, 0.230f, 1.0f};
        case spectra::pathtracer::PbrtSceneLibrary::DisplayState::Invalid: return ImVec4{0.900f, 0.300f, 0.300f, 1.0f};
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    void draw_display_state_label(const spectra::pathtracer::PbrtSceneLibrary::DisplayState state) {
        const ImVec4 color        = display_state_color(state);
        const ImVec2 cursor       = ImGui::GetCursorScreenPos();
        const float text_height   = ImGui::GetTextLineHeight();
        const float dot_radius    = 3.5f;
        const ImVec2 dot_position = ImVec2{cursor.x + dot_radius, cursor.y + text_height * 0.5f};
        ImGui::GetWindowDrawList()->AddCircleFilled(dot_position, dot_radius, ImGui::GetColorU32(color));
        ImGui::SetCursorScreenPos(ImVec2{cursor.x + dot_radius * 2.0f + 7.0f, cursor.y});
        ImGui::TextColored(color, "%s", display_state_text(state));
    }

    [[nodiscard]] bool display_filter_matches(const spectra::pathtracer::PbrtSceneLibrary::DisplayState state, const int filter) {
        if (filter == 0) return true;
        if (filter == 1) return state == spectra::pathtracer::PbrtSceneLibrary::DisplayState::Candidate || state == spectra::pathtracer::PbrtSceneLibrary::DisplayState::Loaded;
        if (filter == 2) return state == spectra::pathtracer::PbrtSceneLibrary::DisplayState::Unsupported;
        if (filter == 3) return state == spectra::pathtracer::PbrtSceneLibrary::DisplayState::Invalid;
        if (filter == 4) return state == spectra::pathtracer::PbrtSceneLibrary::DisplayState::Checking;
        throw std::runtime_error("Unknown Spectra scene display filter");
    }

    [[nodiscard]] std::string source_location_text(const spectra::pathtracer::SceneSourceLocation& source) {
        return std::format("{}:{}:{}", source.filename, source.line, source.column);
    }

    [[nodiscard]] std::size_t visible_scene_count(const spectra::pathtracer::PbrtSceneCatalog& catalog) {
        if (catalog.non_scene_count > catalog.entries.size()) throw std::runtime_error("Spectra scene catalog non-scene count exceeds total entry count");
        return catalog.entries.size() - catalog.non_scene_count;
    }

    [[nodiscard]] std::string first_diagnostic_message(const std::vector<spectra::pathtracer::SceneDiagnostic>& diagnostics) {
        if (diagnostics.empty()) return "no diagnostics";
        return diagnostics.front().message;
    }

    [[nodiscard]] std::string last_probe_feature_type(const spectra::pathtracer::SceneProbeReport& probe, const spectra::pathtracer::SceneProbeFeatureCategory category, std::string fallback) {
        for (const spectra::pathtracer::SceneProbeFeature& feature : probe.features) {
            if (feature.category == category && !feature.type.empty()) fallback = feature.type;
        }
        return fallback;
    }

    constexpr std::string_view initial_scene_id = "pbrt-book/book.pbrt";
    constexpr std::size_t scene_background_worker_count = 2;
} // namespace

namespace spectra::pathtracer {
    PbrtSceneLibrary::PbrtSceneLibrary() : workspace(std::make_shared<SceneWorkspace>()) {
        this->scene_catalog = spectra::pathtracer::DiscoverPbrtSceneCatalog();
        this->scene_catalog_probe_claimed.assign(this->scene_catalog.entries.size(), false);
        const std::string initial_scene_id_string{initial_scene_id};
        const auto active_scene_iter = std::ranges::find_if(this->scene_catalog.entries, [&initial_scene_id_string](const spectra::pathtracer::PbrtSceneCatalogEntry& entry) { return entry.id == initial_scene_id_string; });
        if (active_scene_iter == this->scene_catalog.entries.end()) throw std::runtime_error(std::format("Spectra scene catalog does not contain required initial scene \"{}\"", initial_scene_id));
        if (active_scene_iter->state == spectra::pathtracer::PbrtSceneCatalogEntryState::Invalid) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not parseable: {}", initial_scene_id, first_diagnostic_message(active_scene_iter->issues)));
        spectra::pathtracer::ProbePbrtSceneCatalogEntry(*active_scene_iter);
        if (active_scene_iter->state == spectra::pathtracer::PbrtSceneCatalogEntryState::Invalid) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not probeable: {}", initial_scene_id, first_diagnostic_message(active_scene_iter->issues)));
        if (active_scene_iter->state == spectra::pathtracer::PbrtSceneCatalogEntryState::NonScene) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not a top-level PBRT scene", initial_scene_id));
        if (active_scene_iter->state != spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("Spectra initial scene \"{}\" did not produce a candidate scene probe", initial_scene_id));
        spectra::pathtracer::SceneSnapshot initial_document = spectra::pathtracer::ParsePbrtSceneCatalogEntry(*active_scene_iter);
        const spectra::pathtracer::SceneTranslationReport initial_report = this->analyze_document(initial_document);
        if (!initial_report.supported) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not supported by pathtracer: {}", initial_scene_id, first_diagnostic_message(initial_report.diagnostics)));
        active_scene_iter->revision = initial_document.revision;
        if (active_scene_iter->probe.has_value()) active_scene_iter->probe->revision = initial_document.revision;
        active_scene_iter->state = spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate;
        active_scene_iter->issues.clear();
        this->active_scene_index           = static_cast<std::size_t>(std::distance(this->scene_catalog.entries.begin(), active_scene_iter));
        this->scene_library.selected_index = this->active_scene_index;
        *this->workspace                   = SceneWorkspace{std::move(initial_document)};
        this->refresh_scene_catalog_counts();
    }

    PbrtSceneLibrary::~PbrtSceneLibrary() noexcept {
        this->detach();
    }

    void PbrtSceneLibrary::detach() noexcept {
        this->stop_scene_background_workers_noexcept();
        this->translation_requests.clear();
        this->translation_requests_in_progress.clear();
        this->probe_translation_cache.clear();
        this->document_translation_cache.clear();
        this->scene_catalog_probe_claimed.clear();
        this->attached = false;
    }

    std::shared_ptr<SceneWorkspace> PbrtSceneLibrary::scene_workspace() const {
        return this->workspace;
    }

    void PbrtSceneLibrary::attach_sidebar_host(std::move_only_function<void(PathtracerSidebarTab)> register_tab) {
        if (this->attached) throw std::runtime_error("Spectra scene session is already attached");
        if (!register_tab) throw std::runtime_error("Spectra scene session requires a sidebar tab registration callback");
        register_tab(PathtracerSidebarTab{
            .id             = "scene.library",
            .title          = "Scenes",
            .icon           = ICON_MS_FOLDER_OPEN,
            .shortcut_label = "F2",
            .shortcut_key   = ImGuiKey_F2,
            .draw           = [this] { this->draw_scene_library_window(); },
        });
        this->attached = true;
        this->start_scene_background_workers();
    }

    void PbrtSceneLibrary::start_scene_background_workers() {
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

    void PbrtSceneLibrary::stop_scene_background_workers_noexcept() noexcept {
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

    void PbrtSceneLibrary::stop_scene_background_workers_if_idle() noexcept {
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

    void PbrtSceneLibrary::run_scene_background_worker(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::optional<std::size_t> probe_index{};
            spectra::pathtracer::PbrtSceneCatalogEntry probe_entry{};
            std::optional<TranslationRequest> translation_request{};
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
                }
            }

            if (probe_index.has_value()) {
                if (stop_token.stop_requested()) return;
                spectra::pathtracer::ProbePbrtSceneCatalogEntry(probe_entry, stop_token);
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

            spectra::pathtracer::SceneTranslationReport report{.target = std::string{spectra::pathtracer::PathtracerRenderer::name()}};
            try {
                if (stop_token.stop_requested()) return;
                report = spectra::pathtracer::AnalyzePathtracerSceneProbe(translation_request->probe);
                if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};
            } catch (const std::exception& error) {
                if (stop_token.stop_requested()) return;
                report.supported = false;
                report.diagnostics.push_back(spectra::pathtracer::SceneDiagnostic{
                    .source  = spectra::pathtracer::SceneSourceLocation{.filename = translation_request->probe.source, .line = 1, .column = 1},
                    .message = error.what(),
                });
            }

            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                bool catalog_entry_still_matches = false;
                std::erase_if(this->translation_requests_in_progress, [this, &translation_request](const TranslationRequestKey& key) { return this->translation_request_key_matches(key, translation_request->key); });
                if (translation_request->sceneIndex < this->scene_catalog.entries.size()) {
                    const spectra::pathtracer::PbrtSceneCatalogEntry& entry = this->scene_catalog.entries[translation_request->sceneIndex];
                    catalog_entry_still_matches = entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate && entry.id == translation_request->key.sceneId && entry.revision == translation_request->key.revision;
                }
                if (catalog_entry_still_matches && !this->has_probe_translation_cache_entry_locked(translation_request->key)) {
                    this->probe_translation_cache.push_back(ProbeTranslationCacheEntry{
                        .sceneId  = translation_request->key.sceneId,
                        .revision = translation_request->key.revision,
                        .report   = std::move(report),
                    });
                }
            }
        }
    }

    void PbrtSceneLibrary::refresh_scene_catalog_counts() {
        this->scene_catalog.pending_count = 0;
        this->scene_catalog.candidate_count = 0;
        this->scene_catalog.non_scene_count = 0;
        this->scene_catalog.invalid_count = 0;
        for (const spectra::pathtracer::PbrtSceneCatalogEntry& entry : this->scene_catalog.entries) {
            switch (entry.state) {
            case spectra::pathtracer::PbrtSceneCatalogEntryState::Pending: ++this->scene_catalog.pending_count; break;
            case spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate: ++this->scene_catalog.candidate_count; break;
            case spectra::pathtracer::PbrtSceneCatalogEntryState::NonScene: ++this->scene_catalog.non_scene_count; break;
            case spectra::pathtracer::PbrtSceneCatalogEntryState::Invalid: ++this->scene_catalog.invalid_count; break;
            }
        }
    }

    void PbrtSceneLibrary::clear_scene_translation_caches(const std::string_view scene_id) {
        std::erase_if(this->probe_translation_cache, [scene_id](const ProbeTranslationCacheEntry& entry) { return entry.sceneId == scene_id; });
        std::erase_if(this->document_translation_cache, [scene_id](const DocumentTranslationCacheEntry& entry) { return entry.sceneId == scene_id; });
    }

    bool PbrtSceneLibrary::has_scene_background_work_locked() const {
        if (!this->translation_requests.empty()) return true;
        return this->next_catalog_probe_index_locked().has_value();
    }

    std::optional<std::size_t> PbrtSceneLibrary::next_catalog_probe_index_locked() const {
        if (this->scene_catalog_probe_claimed.size() != this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog probe claim table is out of sync");
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            if (this->scene_catalog.entries[scene_index].state != spectra::pathtracer::PbrtSceneCatalogEntryState::Pending) continue;
            if (this->scene_catalog_probe_claimed[scene_index]) continue;
            return scene_index;
        }
        return {};
    }

    bool PbrtSceneLibrary::translation_request_key_matches(const TranslationRequestKey& lhs, const TranslationRequestKey& rhs) {
        return lhs.sceneId == rhs.sceneId && lhs.revision == rhs.revision;
    }

    bool PbrtSceneLibrary::has_probe_translation_cache_entry_locked(const TranslationRequestKey& key) const {
        for (const ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
            const TranslationRequestKey cache_key{
                .sceneId  = cache_entry.sceneId,
                .revision = cache_entry.revision,
            };
            if (translation_request_key_matches(cache_key, key)) return true;
        }
        return false;
    }

    bool PbrtSceneLibrary::has_translation_request_locked(const TranslationRequestKey& key) const {
        for (const TranslationRequest& request : this->translation_requests)
            if (translation_request_key_matches(request.key, key)) return true;
        for (const TranslationRequestKey& request_key : this->translation_requests_in_progress)
            if (translation_request_key_matches(request_key, key)) return true;
        return false;
    }

    spectra::pathtracer::SceneTranslationReport PbrtSceneLibrary::analyze_probe(const spectra::pathtracer::SceneProbeReport& probe) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
                if (cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
        }

        spectra::pathtracer::SceneTranslationReport report = spectra::pathtracer::AnalyzePathtracerSceneProbe(probe);
        if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};

        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
                if (cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
            this->probe_translation_cache.push_back(ProbeTranslationCacheEntry{
                .sceneId  = probe.name,
                .revision = probe.revision,
                .report   = report,
            });
        }
        return report;
    }

    spectra::pathtracer::SceneTranslationReport PbrtSceneLibrary::analyze_document(const spectra::pathtracer::SceneSnapshot& document) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (DocumentTranslationCacheEntry& cache_entry : this->document_translation_cache) {
                if (cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
        }

        spectra::pathtracer::SceneTranslationReport report = spectra::pathtracer::AnalyzePathtracerSceneSupport(document);
        if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};

        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            for (DocumentTranslationCacheEntry& cache_entry : this->document_translation_cache) {
                if (cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            this->document_translation_cache.push_back(DocumentTranslationCacheEntry{
                .sceneId  = document.name,
                .revision = document.revision,
                .report   = report,
            });
        }
        return report;
    }

    PbrtSceneLibrary::DisplayState PbrtSceneLibrary::display_state(const spectra::pathtracer::PbrtSceneCatalogEntry& entry, const std::optional<spectra::pathtracer::SceneTranslationReport>& report, const bool loaded) const {
        if (entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::Pending) return DisplayState::Checking;
        if (entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::Invalid) return DisplayState::Invalid;
        if (entry.state != spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error("Unknown Spectra scene catalog entry state");
        if (!report.has_value()) return DisplayState::Checking;
        if (report.has_value() && !report->supported) return DisplayState::Unsupported;
        if (loaded) return DisplayState::Loaded;
        return DisplayState::Candidate;
    }

    std::optional<spectra::pathtracer::SceneTranslationReport> PbrtSceneLibrary::cached_entry_report(const spectra::pathtracer::PbrtSceneCatalogEntry& entry) const {
        if (entry.state != spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate || entry.revision.value == 0) return {};
        std::scoped_lock lock{this->scene_catalog_mutex};
        for (const ProbeTranslationCacheEntry& cache_entry : this->probe_translation_cache) {
            if (cache_entry.sceneId == entry.id && cache_entry.revision == entry.revision) return cache_entry.report;
        }
        return {};
    }

    void PbrtSceneLibrary::request_entry_report_analysis(const std::size_t scene_index, const spectra::pathtracer::PbrtSceneCatalogEntry& entry) {
        if (entry.state != spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate) return;
        if (!entry.probe.has_value()) return;
        if (entry.revision.value == 0) throw std::runtime_error(std::format("Candidate Spectra scene \"{}\" has no catalog revision", entry.id));
        TranslationRequest request{
            .key =
                TranslationRequestKey{
                    .sceneId  = entry.id,
                    .revision = entry.revision,
                },
            .sceneIndex = scene_index,
            .probe      = *entry.probe,
        };
        bool should_start_workers = false;
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (this->has_probe_translation_cache_entry_locked(request.key)) return;
            if (this->has_translation_request_locked(request.key)) return;
            this->translation_requests.push_back(std::move(request));
            should_start_workers = this->scene_background_workers.empty();
        }
        if (should_start_workers) this->start_scene_background_workers();
        else this->scene_background_condition.notify_one();
    }

    void PbrtSceneLibrary::commit_document(const std::size_t scene_index, spectra::pathtracer::SceneSnapshot document) {
        if (this->workspace == nullptr) throw std::runtime_error("Spectra pathtracer PBRT scene workspace is unavailable");
        if (!this->workspace->loaded()) {
            *this->workspace = SceneWorkspace{std::move(document)};
        } else {
            SceneEditBuilder edit{};
            edit.replaceSnapshot(std::move(document), SceneDirtyFlags::Snapshot);
            const SceneEditBatch edit_batch = this->workspace->commit(std::move(edit));
            if (edit_batch.dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Spectra pathtracer failed to commit a PBRT scene replacement");
        }
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            this->active_scene_index           = scene_index;
            this->scene_library.selected_index = scene_index;
            this->scene_library.load_error     = {};
        }
    }

    void PbrtSceneLibrary::load_scene(const std::size_t scene_index) {
        spectra::pathtracer::PbrtSceneCatalogEntry entry{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            if (scene_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene load index is out of range");
            entry = this->scene_catalog.entries[scene_index];
        }
        if (entry.state != spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("Cannot load disabled Spectra scene \"{}\"", entry.id));
        spectra::pathtracer::SceneSnapshot document = spectra::pathtracer::ParsePbrtSceneCatalogEntry(entry);
        const spectra::pathtracer::SceneTranslationReport report = this->analyze_document(document);
        if (!report.supported) {
            throw std::runtime_error(std::format("Cannot load Spectra pathtracer PBRT scene \"{}\": {}", entry.id, first_diagnostic_message(report.diagnostics)));
        }
        this->commit_document(scene_index, std::move(document));
    }

    void PbrtSceneLibrary::draw_scene_library_window() {
        this->stop_scene_background_workers_if_idle();

        spectra::pathtracer::PbrtSceneCatalog catalog_snapshot{};
        std::size_t active_scene_index_snapshot{};
        std::size_t selected_scene_index_snapshot{};
        int filter_snapshot{};
        std::optional<std::string> load_error_snapshot{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            catalog_snapshot              = this->scene_catalog;
            active_scene_index_snapshot   = this->active_scene_index;
            selected_scene_index_snapshot = this->scene_library.selected_index;
            filter_snapshot               = this->scene_library.filter;
            load_error_snapshot           = this->scene_library.load_error;
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) selected_scene_index_snapshot = active_scene_index_snapshot;
        if (selected_scene_index_snapshot < catalog_snapshot.entries.size() && catalog_snapshot.entries[selected_scene_index_snapshot].state == spectra::pathtracer::PbrtSceneCatalogEntryState::NonScene) selected_scene_index_snapshot = active_scene_index_snapshot;

        ImGui::TextUnformatted("Scene Library");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu", catalog_snapshot.candidate_count, visible_scene_count(catalog_snapshot));

        const float filter_width  = 142.0f;
        const float control_width = ImGui::GetContentRegionAvail().x;
        const bool compact_layout = control_width < 360.0f;
        ImGui::SetNextItemWidth(compact_layout ? -1.0f : std::max(120.0f, control_width - filter_width - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputTextWithHint("##SpectraSceneSearch", ICON_MS_SEARCH " Search scenes", this->scene_library.search.data(), this->scene_library.search.size(), ImGuiInputTextFlags_AutoSelectAll)) {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->scene_library.selected_index = active_scene_index_snapshot;
        }

        int filter = filter_snapshot;
        constexpr std::array<const char*, 5> filter_labels{"All", "Candidate", "Unsupported", "Invalid", "Checking"};
        if (!compact_layout) ImGui::SameLine();
        ImGui::SetNextItemWidth(compact_layout ? -1.0f : filter_width);
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
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBodyUntilResize;
        const float table_height              = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.58f);
        if (ImGui::BeginTable("SpectraSceneLibraryTable", 4, table_flags, ImVec2{0.0f, table_height})) {
            ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Integrator", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();
            for (std::size_t scene_index = 0; scene_index < catalog_snapshot.entries.size(); ++scene_index) {
                const spectra::pathtracer::PbrtSceneCatalogEntry& entry = catalog_snapshot.entries[scene_index];
                if (entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::NonScene) continue;
                if (!contains_case_insensitive(entry.id, search_text) && !contains_case_insensitive(entry.displayName, search_text) && !contains_case_insensitive(entry.group, search_text)) continue;
                std::optional<spectra::pathtracer::SceneTranslationReport> report = this->cached_entry_report(entry);
                const bool selected = scene_index == selected_scene_index_snapshot;
                const bool active   = scene_index == active_scene_index_snapshot;
                if (entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate && !report.has_value()) this->request_entry_report_analysis(scene_index, entry);
                const DisplayState state = this->display_state(entry, report, active);
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
                draw_display_state_label(state);
                ImGui::TableSetColumnIndex(3);
                if (entry.probe.has_value())
                    ImGui::TextUnformatted(last_probe_feature_type(*entry.probe, spectra::pathtracer::SceneProbeFeatureCategory::Integrator, "-").c_str());
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) return;
        if (catalog_snapshot.entries[selected_scene_index_snapshot].state == spectra::pathtracer::PbrtSceneCatalogEntryState::NonScene) return;
        const spectra::pathtracer::PbrtSceneCatalogEntry& selected_entry = catalog_snapshot.entries[selected_scene_index_snapshot];
        std::optional<spectra::pathtracer::SceneTranslationReport> selected_report = this->cached_entry_report(selected_entry);
        const bool selected_active = selected_scene_index_snapshot == active_scene_index_snapshot;
        if (selected_entry.state == spectra::pathtracer::PbrtSceneCatalogEntryState::Candidate && !selected_report.has_value()) this->request_entry_report_analysis(selected_scene_index_snapshot, selected_entry);
        const DisplayState selected_state = this->display_state(selected_entry, selected_report, selected_active);
        ImGui::Spacing();
        ImGui::SeparatorText("Selected Scene");
        ImGui::TextWrapped("%s", selected_entry.displayName.c_str());
        ImGui::TextDisabled("%s", selected_entry.id.c_str());
        draw_display_state_label(selected_state);

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

        if (load_error_snapshot.has_value()) {
            ImGui::SeparatorText("Last Load Error");
            ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", load_error_snapshot->c_str());
        }

        if (selected_entry.probe.has_value()) {
            constexpr ImGuiTableFlags info_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("SpectraSceneLibraryInfo", 2, info_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 112.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                const auto row = [](const char* label, const std::string& value) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(value.c_str());
                };
                const auto count_feature = [&selected_entry](const spectra::pathtracer::SceneProbeFeatureCategory category) {
                    return static_cast<std::size_t>(std::ranges::count_if(selected_entry.probe->features, [category](const spectra::pathtracer::SceneProbeFeature& feature) { return feature.category == category; }));
                };
                row("Camera", last_probe_feature_type(*selected_entry.probe, spectra::pathtracer::SceneProbeFeatureCategory::Camera, "-"));
                row("Sampler", last_probe_feature_type(*selected_entry.probe, spectra::pathtracer::SceneProbeFeatureCategory::Sampler, "-"));
                row("Integrator", last_probe_feature_type(*selected_entry.probe, spectra::pathtracer::SceneProbeFeatureCategory::Integrator, "-"));
                row("Accelerator", last_probe_feature_type(*selected_entry.probe, spectra::pathtracer::SceneProbeFeatureCategory::Accelerator, "-"));
                row("Shapes", std::format("{}", count_feature(spectra::pathtracer::SceneProbeFeatureCategory::Shape)));
                row("Materials", std::format("{}", count_feature(spectra::pathtracer::SceneProbeFeatureCategory::Material)));
                row("Lights", std::format("{}", count_feature(spectra::pathtracer::SceneProbeFeatureCategory::Light)));
                ImGui::EndTable();
            }
        }

        if (selected_report.has_value() && !selected_report->diagnostics.empty()) {
            if (ImGui::CollapsingHeader("Translation", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                if (ImGui::BeginChild("SpectraSceneLibraryTranslation", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const spectra::pathtracer::SceneDiagnostic& diagnostic : selected_report->diagnostics) {
                        ImGui::TextColored(display_state_color(DisplayState::Unsupported), "%s", source_location_text(diagnostic.source).c_str());
                        ImGui::TextWrapped("%s", diagnostic.message.c_str());
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
        }

        if (!selected_entry.issues.empty()) {
            if (ImGui::CollapsingHeader("Probe Issues", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                if (ImGui::BeginChild("SpectraSceneLibraryIssues", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const spectra::pathtracer::SceneDiagnostic& issue : selected_entry.issues) {
                        ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", source_location_text(issue.source).c_str());
                        ImGui::TextWrapped("%s", issue.message.c_str());
                        ImGui::Spacing();
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar();
            }
        }
    }
} // namespace spectra::pathtracer
