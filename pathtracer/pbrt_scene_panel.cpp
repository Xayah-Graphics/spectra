module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module spectra.pathtracer.pbrt.panel;

import spectra.pathtracer;
import spectra.scene;
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

    [[nodiscard]] const char* display_state_text(const spectra::pathtracer::PbrtScenePanel::DisplayState state) {
        switch (state) {
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Checking: return "Checking";
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Candidate: return "Candidate";
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Loaded: return "Loaded";
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Unsupported: return "Unsupported";
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Invalid: return "Invalid";
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    [[nodiscard]] ImVec4 display_state_color(const spectra::pathtracer::PbrtScenePanel::DisplayState state) {
        switch (state) {
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Checking: return ImVec4{0.620f, 0.660f, 0.720f, 1.0f};
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Candidate: return ImVec4{0.220f, 0.720f, 0.480f, 1.0f};
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Loaded: return ImVec4{0.300f, 0.760f, 0.950f, 1.0f};
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Unsupported: return ImVec4{0.930f, 0.680f, 0.230f, 1.0f};
        case spectra::pathtracer::PbrtScenePanel::DisplayState::Invalid: return ImVec4{0.900f, 0.300f, 0.300f, 1.0f};
        }
        throw std::runtime_error("Unknown Spectra scene display state");
    }

    void draw_display_state_label(const spectra::pathtracer::PbrtScenePanel::DisplayState state) {
        const ImVec4 color        = display_state_color(state);
        const ImVec2 cursor       = ImGui::GetCursorScreenPos();
        const float text_height   = ImGui::GetTextLineHeight();
        const float dot_radius    = 3.5f;
        const ImVec2 dot_position = ImVec2{cursor.x + dot_radius, cursor.y + text_height * 0.5f};
        ImGui::GetWindowDrawList()->AddCircleFilled(dot_position, dot_radius, ImGui::GetColorU32(color));
        ImGui::SetCursorScreenPos(ImVec2{cursor.x + dot_radius * 2.0f + 7.0f, cursor.y});
        ImGui::TextColored(color, "%s", display_state_text(state));
    }

    [[nodiscard]] bool display_filter_matches(const spectra::pathtracer::PbrtScenePanel::DisplayState state, const int filter) {
        if (filter == 0) return true;
        if (filter == 1) return state == spectra::pathtracer::PbrtScenePanel::DisplayState::Candidate || state == spectra::pathtracer::PbrtScenePanel::DisplayState::Loaded;
        if (filter == 2) return state == spectra::pathtracer::PbrtScenePanel::DisplayState::Unsupported;
        if (filter == 3) return state == spectra::pathtracer::PbrtScenePanel::DisplayState::Invalid;
        if (filter == 4) return state == spectra::pathtracer::PbrtScenePanel::DisplayState::Checking;
        throw std::runtime_error("Unknown Spectra scene display filter");
    }

    [[nodiscard]] std::string source_location_text(const spectra::scene::SceneSourceLocation& source) {
        return std::format("{}:{}:{}", source.filename, source.line, source.column);
    }

    [[nodiscard]] std::size_t visible_scene_count(const spectra::scene::PbrtSceneCatalog& catalog) {
        if (catalog.non_scene_count > catalog.entries.size()) throw std::runtime_error("Spectra scene catalog non-scene count exceeds total entry count");
        return catalog.entries.size() - catalog.non_scene_count;
    }

    [[nodiscard]] std::string first_diagnostic_message(const std::vector<spectra::scene::PbrtSceneDiagnostic>& diagnostics) {
        if (diagnostics.empty()) return "no diagnostics";
        return diagnostics.front().message;
    }

    [[nodiscard]] std::string last_probe_feature_type(const spectra::scene::PbrtSceneProbeReport& probe, const spectra::scene::PbrtSceneProbeFeatureCategory category, std::string default_value) {
        for (const spectra::scene::PbrtSceneProbeFeature& feature : probe.features) {
            if (feature.category == category && !feature.type.empty()) default_value = feature.type;
        }
        return default_value;
    }

    constexpr std::size_t support_analysis_background_worker_count = 2;
} // namespace

namespace spectra::pathtracer {
    PbrtScenePanel::PbrtScenePanel(std::shared_ptr<scene::PbrtSceneBrowserSession> browser_session) : browser_session(std::move(browser_session)) {
        if (this->browser_session == nullptr) throw std::runtime_error("Spectra PBRT scene panel requires a browser session");
        const std::shared_ptr<const scene::PbrtSceneSnapshot> initial_document = this->browser_session->workspace()->snapshot();
        if (initial_document == nullptr) throw std::runtime_error("Spectra PBRT scene panel requires a loaded initial scene");
        const spectra::pathtracer::PathtracerSceneSupportReport initial_report = this->analyze_document(*initial_document);
        if (!initial_report.supported) throw std::runtime_error(std::format("Spectra initial scene \"{}\" is not supported by pathtracer: {}", initial_document->name, first_diagnostic_message(initial_report.diagnostics)));
    }

    PbrtScenePanel::~PbrtScenePanel() noexcept {
        this->detach();
    }

    void PbrtScenePanel::detach() noexcept {
        this->stop_support_analysis_workers();
        if (this->browser_session != nullptr) this->browser_session->stop_background_probe_workers();
        this->supportAnalysisRequests.clear();
        this->supportAnalysisRequestsInProgress.clear();
        this->probeSupportCache.clear();
        this->documentSupportCache.clear();
        this->attached = false;
    }

    void PbrtScenePanel::attach_sidebar_host(std::move_only_function<void(PathtracerSidebarTab)> register_tab) {
        if (this->attached) throw std::runtime_error("Spectra scene session is already attached");
        if (!register_tab) throw std::runtime_error("Spectra scene session requires a sidebar tab registration callback");
        register_tab(PathtracerSidebarTab{
            .id             = "scene.library",
            .title          = "Scenes",
            .icon           = ICON_MS_FOLDER_OPEN,
            .shortcut_label = "F2",
            .shortcut_key   = ImGuiKey_F2,
            .draw           = [this] { this->draw_scene_browser_panel(); },
        });
        this->attached = true;
        this->browser_session->start_background_probe_workers();
        this->start_support_analysis_workers();
    }

    void PbrtScenePanel::start_support_analysis_workers() {
        if (!this->supportAnalysisBackgroundWorkers.empty()) throw std::runtime_error("Spectra scene support analysis workers are already running");
        this->supportAnalysisBackgroundWorkers.reserve(support_analysis_background_worker_count);
        for (std::size_t worker_index = 0; worker_index < support_analysis_background_worker_count; ++worker_index) {
            this->supportAnalysisBackgroundWorkers.emplace_back([this](const std::stop_token stop_token) { this->run_support_analysis_worker(stop_token); });
        }
        this->supportAnalysisBackgroundCondition.notify_all();
    }

    void PbrtScenePanel::stop_support_analysis_workers() noexcept {
        for (std::jthread& worker : this->supportAnalysisBackgroundWorkers) worker.request_stop();
        this->supportAnalysisBackgroundCondition.notify_all();
        for (std::jthread& worker : this->supportAnalysisBackgroundWorkers) {
            if (worker.joinable()) worker.join();
        }
        this->supportAnalysisBackgroundWorkers.clear();
        std::scoped_lock lock{this->supportAnalysisMutex};
        this->supportAnalysisRequestsInProgress.clear();
    }

    void PbrtScenePanel::stop_support_analysis_workers_if_idle() noexcept {
        bool should_stop = false;
        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            should_stop = !this->supportAnalysisBackgroundWorkers.empty() && this->supportAnalysisRequests.empty() && this->supportAnalysisRequestsInProgress.empty();
        }
        if (should_stop) this->stop_support_analysis_workers();
    }

    void PbrtScenePanel::run_support_analysis_worker(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::optional<SupportAnalysisRequest> support_analysis_request{};
            {
                std::unique_lock lock{this->supportAnalysisMutex};
                if (!this->supportAnalysisBackgroundCondition.wait(lock, stop_token, [this] { return this->has_support_analysis_work_locked(); })) return;
                support_analysis_request = std::move(this->supportAnalysisRequests.front());
                this->supportAnalysisRequests.pop_front();
                this->supportAnalysisRequestsInProgress.push_back(support_analysis_request->key);
            }

            spectra::pathtracer::PathtracerSceneSupportReport report{.target = std::string{spectra::pathtracer::PathtracerRenderer::name()}};
            try {
                if (stop_token.stop_requested()) return;
                report = spectra::pathtracer::AnalyzePathtracerSceneProbe(support_analysis_request->probe);
                if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};
            } catch (const std::exception& error) {
                if (stop_token.stop_requested()) return;
                report.supported = false;
                report.diagnostics.push_back(spectra::scene::PbrtSceneDiagnostic{
                    .source  = spectra::scene::SceneSourceLocation{.filename = support_analysis_request->probe.source, .line = 1, .column = 1},
                    .message = error.what(),
                });
            }

            {
                std::scoped_lock lock{this->supportAnalysisMutex};
                std::erase_if(this->supportAnalysisRequestsInProgress, [this, &support_analysis_request](const SupportAnalysisRequestKey& key) { return this->support_analysis_request_key_matches(key, support_analysis_request->key); });
                if (!this->has_probe_support_cache_entry_locked(support_analysis_request->key)) {
                    this->probeSupportCache.push_back(ProbeSupportCacheEntry{
                        .sceneId  = support_analysis_request->key.sceneId,
                        .revision = support_analysis_request->key.revision,
                        .report   = std::move(report),
                    });
                }
            }
        }
    }

    bool PbrtScenePanel::has_support_analysis_work_locked() const {
        return !this->supportAnalysisRequests.empty();
    }

    bool PbrtScenePanel::support_analysis_request_key_matches(const SupportAnalysisRequestKey& lhs, const SupportAnalysisRequestKey& rhs) {
        return lhs.sceneId == rhs.sceneId && lhs.revision == rhs.revision;
    }

    bool PbrtScenePanel::has_probe_support_cache_entry_locked(const SupportAnalysisRequestKey& key) const {
        for (const ProbeSupportCacheEntry& cache_entry : this->probeSupportCache) {
            const SupportAnalysisRequestKey cache_key{
                .sceneId  = cache_entry.sceneId,
                .revision = cache_entry.revision,
            };
            if (support_analysis_request_key_matches(cache_key, key)) return true;
        }
        return false;
    }

    bool PbrtScenePanel::has_support_analysis_request_locked(const SupportAnalysisRequestKey& key) const {
        for (const SupportAnalysisRequest& request : this->supportAnalysisRequests)
            if (support_analysis_request_key_matches(request.key, key)) return true;
        for (const SupportAnalysisRequestKey& request_key : this->supportAnalysisRequestsInProgress)
            if (support_analysis_request_key_matches(request_key, key)) return true;
        return false;
    }

    spectra::pathtracer::PathtracerSceneSupportReport PbrtScenePanel::analyze_probe(const spectra::scene::PbrtSceneProbeReport& probe) {
        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            for (ProbeSupportCacheEntry& cache_entry : this->probeSupportCache) {
                if (cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
        }

        spectra::pathtracer::PathtracerSceneSupportReport report = spectra::pathtracer::AnalyzePathtracerSceneProbe(probe);
        if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};

        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            for (ProbeSupportCacheEntry& cache_entry : this->probeSupportCache) {
                if (cache_entry.sceneId == probe.name && cache_entry.revision == probe.revision) return cache_entry.report;
            }
            this->probeSupportCache.push_back(ProbeSupportCacheEntry{
                .sceneId  = probe.name,
                .revision = probe.revision,
                .report   = report,
            });
        }
        return report;
    }

    spectra::pathtracer::PathtracerSceneSupportReport PbrtScenePanel::analyze_document(const spectra::scene::PbrtSceneSnapshot& document) {
        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            for (DocumentSupportCacheEntry& cache_entry : this->documentSupportCache) {
                if (cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
        }

        spectra::pathtracer::PathtracerSceneSupportReport report = spectra::pathtracer::AnalyzePathtracerSceneSupport(document);
        if (report.target.empty()) report.target = std::string{spectra::pathtracer::PathtracerRenderer::name()};

        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            for (DocumentSupportCacheEntry& cache_entry : this->documentSupportCache) {
                if (cache_entry.sceneId == document.name && cache_entry.revision == document.revision) return cache_entry.report;
            }
            this->documentSupportCache.push_back(DocumentSupportCacheEntry{
                .sceneId  = document.name,
                .revision = document.revision,
                .report   = report,
            });
        }
        return report;
    }

    PbrtScenePanel::DisplayState PbrtScenePanel::display_state(const spectra::scene::PbrtSceneCatalogEntry& entry, const std::optional<spectra::pathtracer::PathtracerSceneSupportReport>& report, const bool loaded) const {
        if (entry.state == spectra::scene::PbrtSceneCatalogEntryState::Pending) return DisplayState::Checking;
        if (entry.state == spectra::scene::PbrtSceneCatalogEntryState::Invalid) return DisplayState::Invalid;
        if (entry.state != spectra::scene::PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error("Unknown Spectra scene catalog entry state");
        if (!report.has_value()) return DisplayState::Checking;
        if (report.has_value() && !report->supported) return DisplayState::Unsupported;
        if (loaded) return DisplayState::Loaded;
        return DisplayState::Candidate;
    }

    std::optional<spectra::pathtracer::PathtracerSceneSupportReport> PbrtScenePanel::cached_entry_report(const spectra::scene::PbrtSceneCatalogEntry& entry) const {
        if (entry.state != spectra::scene::PbrtSceneCatalogEntryState::Candidate || entry.revision.value == 0) return {};
        std::scoped_lock lock{this->supportAnalysisMutex};
        for (const ProbeSupportCacheEntry& cache_entry : this->probeSupportCache) {
            if (cache_entry.sceneId == entry.id && cache_entry.revision == entry.revision) return cache_entry.report;
        }
        return {};
    }

    void PbrtScenePanel::request_entry_report_analysis(const std::size_t scene_index, const spectra::scene::PbrtSceneCatalogEntry& entry) {
        if (entry.state != spectra::scene::PbrtSceneCatalogEntryState::Candidate) return;
        if (!entry.probe.has_value()) return;
        if (entry.revision.value == 0) throw std::runtime_error(std::format("Candidate Spectra scene \"{}\" has no catalog revision", entry.id));
        SupportAnalysisRequest request{
            .key =
                SupportAnalysisRequestKey{
                    .sceneId  = entry.id,
                    .revision = entry.revision,
                },
            .sceneIndex = scene_index,
            .probe      = *entry.probe,
        };
        bool should_start_workers = false;
        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            if (this->has_probe_support_cache_entry_locked(request.key)) return;
            if (this->has_support_analysis_request_locked(request.key)) return;
            this->supportAnalysisRequests.push_back(std::move(request));
            should_start_workers = this->supportAnalysisBackgroundWorkers.empty();
        }
        if (should_start_workers) this->start_support_analysis_workers();
        else this->supportAnalysisBackgroundCondition.notify_one();
    }

    void PbrtScenePanel::load_scene(const std::size_t scene_index) {
        if (this->browser_session == nullptr) throw std::runtime_error("Spectra pathtracer PBRT scene browser session is unavailable");
        this->browser_session->select_scene(scene_index);
        spectra::scene::PbrtSceneSnapshot document = this->browser_session->parse_selected_scene();
        const spectra::pathtracer::PathtracerSceneSupportReport report = this->analyze_document(document);
        if (!report.supported) {
            throw std::runtime_error(std::format("Cannot load Spectra pathtracer PBRT scene \"{}\": {}", document.name, first_diagnostic_message(report.diagnostics)));
        }
        const scene::PbrtSceneEditBatch edit_batch = this->browser_session->commit_selected_scene(std::move(document));
        if (edit_batch.dirty != scene::PbrtSceneDirtyFlags::Snapshot) throw std::runtime_error("Spectra pathtracer failed to commit a PBRT scene replacement");
        std::scoped_lock lock{this->supportAnalysisMutex};
        this->sceneBrowser.load_error = {};
    }

    void PbrtScenePanel::draw_scene_browser_panel() {
        if (this->browser_session == nullptr) throw std::runtime_error("Spectra pathtracer PBRT scene browser session is unavailable");
        this->browser_session->stop_background_probe_workers_if_idle();
        this->stop_support_analysis_workers_if_idle();

        spectra::scene::PbrtSceneCatalog catalog_snapshot = this->browser_session->catalog_snapshot();
        std::size_t active_scene_index_snapshot = this->browser_session->active_scene_index();
        std::size_t selected_scene_index_snapshot = this->browser_session->selected_scene_index();
        int filter_snapshot{};
        std::optional<std::string> load_error_snapshot{};
        {
            std::scoped_lock lock{this->supportAnalysisMutex};
            filter_snapshot     = this->sceneBrowser.filter;
            load_error_snapshot = this->sceneBrowser.load_error;
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) selected_scene_index_snapshot = active_scene_index_snapshot;
        if (selected_scene_index_snapshot < catalog_snapshot.entries.size() && catalog_snapshot.entries[selected_scene_index_snapshot].state == spectra::scene::PbrtSceneCatalogEntryState::NonScene) selected_scene_index_snapshot = active_scene_index_snapshot;

        ImGui::TextUnformatted("Scene Browser");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu / %zu", catalog_snapshot.candidate_count, visible_scene_count(catalog_snapshot));

        const float filter_width  = 142.0f;
        const float control_width = ImGui::GetContentRegionAvail().x;
        const bool compact_layout = control_width < 360.0f;
        ImGui::SetNextItemWidth(compact_layout ? -1.0f : std::max(120.0f, control_width - filter_width - ImGui::GetStyle().ItemSpacing.x));
        if (ImGui::InputTextWithHint("##SpectraSceneSearch", ICON_MS_SEARCH " Search scenes", this->sceneBrowser.search.data(), this->sceneBrowser.search.size(), ImGuiInputTextFlags_AutoSelectAll)) {
            this->browser_session->select_scene(active_scene_index_snapshot);
            selected_scene_index_snapshot = active_scene_index_snapshot;
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
            std::scoped_lock lock{this->supportAnalysisMutex};
            this->sceneBrowser.filter = filter;
            filter_snapshot            = filter;
        }

        const std::string_view search_text{this->sceneBrowser.search.data()};
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoBordersInBodyUntilResize;
        const float table_height              = std::max(220.0f, ImGui::GetContentRegionAvail().y * 0.58f);
        if (ImGui::BeginTable("SpectraSceneBrowserTable", 4, table_flags, ImVec2{0.0f, table_height})) {
            ImGui::TableSetupColumn("Scene", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 104.0f);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Integrator", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();
            for (std::size_t scene_index = 0; scene_index < catalog_snapshot.entries.size(); ++scene_index) {
                const spectra::scene::PbrtSceneCatalogEntry& entry = catalog_snapshot.entries[scene_index];
                if (entry.state == spectra::scene::PbrtSceneCatalogEntryState::NonScene) continue;
                if (!contains_case_insensitive(entry.id, search_text) && !contains_case_insensitive(entry.displayName, search_text) && !contains_case_insensitive(entry.group, search_text)) continue;
                std::optional<spectra::pathtracer::PathtracerSceneSupportReport> report = this->cached_entry_report(entry);
                const bool selected = scene_index == selected_scene_index_snapshot;
                const bool active   = scene_index == active_scene_index_snapshot;
                if (entry.state == spectra::scene::PbrtSceneCatalogEntryState::Candidate && !report.has_value()) this->request_entry_report_analysis(scene_index, entry);
                const DisplayState state = this->display_state(entry, report, active);
                if (!display_filter_matches(state, filter_snapshot)) continue;

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string label = active ? std::format("{} {}", ICON_MS_RADIO_BUTTON_CHECKED, entry.displayName) : entry.displayName;
                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_scene_index_snapshot = scene_index;
                    this->browser_session->select_scene(scene_index);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", entry.id.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::TextDisabled("%s", entry.group.c_str());
                ImGui::TableSetColumnIndex(2);
                draw_display_state_label(state);
                ImGui::TableSetColumnIndex(3);
                if (entry.probe.has_value())
                    ImGui::TextUnformatted(last_probe_feature_type(*entry.probe, spectra::scene::PbrtSceneProbeFeatureCategory::Integrator, "-").c_str());
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }

        if (selected_scene_index_snapshot >= catalog_snapshot.entries.size()) return;
        if (catalog_snapshot.entries[selected_scene_index_snapshot].state == spectra::scene::PbrtSceneCatalogEntryState::NonScene) return;
        const spectra::scene::PbrtSceneCatalogEntry& selected_entry = catalog_snapshot.entries[selected_scene_index_snapshot];
        std::optional<spectra::pathtracer::PathtracerSceneSupportReport> selected_report = this->cached_entry_report(selected_entry);
        const bool selected_active = selected_scene_index_snapshot == active_scene_index_snapshot;
        if (selected_entry.state == spectra::scene::PbrtSceneCatalogEntryState::Candidate && !selected_report.has_value()) this->request_entry_report_analysis(selected_scene_index_snapshot, selected_entry);
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
                std::scoped_lock lock{this->supportAnalysisMutex};
                this->sceneBrowser.load_error = error.what();
            }
        }
        ImGui::EndDisabled();

        if (load_error_snapshot.has_value()) {
            ImGui::SeparatorText("Last Load Error");
            ImGui::TextColored(display_state_color(DisplayState::Invalid), "%s", load_error_snapshot->c_str());
        }

        if (selected_entry.probe.has_value()) {
            constexpr ImGuiTableFlags info_flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("SpectraSceneBrowserInfo", 2, info_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 112.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                const auto row = [](const char* label, const std::string& value) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(value.c_str());
                };
                const auto count_feature = [&selected_entry](const spectra::scene::PbrtSceneProbeFeatureCategory category) {
                    return static_cast<std::size_t>(std::ranges::count_if(selected_entry.probe->features, [category](const spectra::scene::PbrtSceneProbeFeature& feature) { return feature.category == category; }));
                };
                row("Camera", last_probe_feature_type(*selected_entry.probe, spectra::scene::PbrtSceneProbeFeatureCategory::Camera, "-"));
                row("Sampler", last_probe_feature_type(*selected_entry.probe, spectra::scene::PbrtSceneProbeFeatureCategory::Sampler, "-"));
                row("Integrator", last_probe_feature_type(*selected_entry.probe, spectra::scene::PbrtSceneProbeFeatureCategory::Integrator, "-"));
                row("Accelerator", last_probe_feature_type(*selected_entry.probe, spectra::scene::PbrtSceneProbeFeatureCategory::Accelerator, "-"));
                row("Shapes", std::format("{}", count_feature(spectra::scene::PbrtSceneProbeFeatureCategory::Shape)));
                row("Materials", std::format("{}", count_feature(spectra::scene::PbrtSceneProbeFeatureCategory::Material)));
                row("Lights", std::format("{}", count_feature(spectra::scene::PbrtSceneProbeFeatureCategory::Light)));
                ImGui::EndTable();
            }
        }

        if (selected_report.has_value() && !selected_report->diagnostics.empty()) {
            if (ImGui::CollapsingHeader("Pathtracer Support", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
                if (ImGui::BeginChild("SpectraSceneBrowserSupport", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const spectra::scene::PbrtSceneDiagnostic& diagnostic : selected_report->diagnostics) {
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
                if (ImGui::BeginChild("SpectraSceneBrowserIssues", ImVec2{0.0f, 120.0f}, ImGuiChildFlags_Borders)) {
                    for (const spectra::scene::PbrtSceneDiagnostic& issue : selected_entry.issues) {
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

