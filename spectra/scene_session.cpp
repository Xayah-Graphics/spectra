module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module xayah.spectra.scene_session;

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
} // namespace

namespace xayah {
    SpectraSceneSession::SpectraSceneSession() : workspace(std::make_shared<spectra::scene::SceneWorkspace>()) {
        this->scene_catalog = spectra::scene::DiscoverSceneCatalog();
        std::optional<std::size_t> active_scene_index{};
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            if (this->scene_catalog.entries[scene_index].state == spectra::scene::SceneCatalogEntryState::Pending) spectra::scene::ValidateSceneCatalogEntry(this->scene_catalog.entries[scene_index]);
            if (this->scene_catalog.entries[scene_index].state == spectra::scene::SceneCatalogEntryState::Ready && this->scene_catalog.entries[scene_index].document != nullptr) {
                active_scene_index = scene_index;
                break;
            }
        }
        if (!active_scene_index.has_value()) throw std::runtime_error("Spectra scene catalog does not contain a parseable scene");
        this->active_scene_index           = *active_scene_index;
        this->scene_library.selected_index = this->active_scene_index;
        *this->workspace                   = spectra::scene::SceneWorkspace{*this->scene_catalog.entries[this->active_scene_index].document};
        this->refresh_scene_catalog_counts();
    }

    SpectraSceneSession::~SpectraSceneSession() noexcept {
        this->detach();
    }

    void SpectraSceneSession::detach() noexcept {
        if (this->scene_catalog_validator.joinable()) {
            this->scene_catalog_validator.request_stop();
            this->scene_catalog_validator.join();
        }
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
        std::scoped_lock lock{this->scene_catalog_mutex};
        for (const spectra::scene::SceneTranslationTarget& existing_target : this->translation_targets) {
            if (existing_target.rendererName == target.rendererName) throw std::runtime_error(std::format("Duplicate scene translation target \"{}\"", target.rendererName));
        }
        this->translation_targets.push_back(std::move(target));
    }

    void SpectraSceneSession::set_active_renderer(const std::string_view renderer_name) {
        std::scoped_lock lock{this->scene_catalog_mutex};
        this->ensure_translation_target_exists(renderer_name);
        this->active_renderer = std::string{renderer_name};
    }

    void SpectraSceneSession::load_first_supported_scene(const std::string_view renderer_name) {
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            this->ensure_translation_target_exists(renderer_name);
        }
        for (std::size_t scene_index = 0; scene_index < this->scene_catalog.entries.size(); ++scene_index) {
            spectra::scene::SceneCatalogEntry entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                entry = this->scene_catalog.entries[scene_index];
            }
            if (entry.state == spectra::scene::SceneCatalogEntryState::Pending) {
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
        this->start_scene_catalog_validation();
    }

    void SpectraSceneSession::start_scene_catalog_validation() {
        if (this->scene_catalog_validator.joinable()) throw std::runtime_error("Spectra scene catalog validator is already running");
        this->scene_catalog_validator = std::jthread{[this](const std::stop_token stop_token) { this->validate_scene_catalog(stop_token); }};
    }

    void SpectraSceneSession::validate_scene_catalog(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::optional<std::size_t> pending_index{};
            spectra::scene::SceneCatalogEntry pending_entry{};
            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                const auto iter = std::ranges::find_if(this->scene_catalog.entries, [](const spectra::scene::SceneCatalogEntry& entry) { return entry.state == spectra::scene::SceneCatalogEntryState::Pending; });
                if (iter == this->scene_catalog.entries.end()) return;
                pending_index = static_cast<std::size_t>(std::distance(this->scene_catalog.entries.begin(), iter));
                pending_entry = *iter;
            }

            spectra::scene::ValidateSceneCatalogEntry(pending_entry);
            if (stop_token.stop_requested()) return;

            {
                std::scoped_lock lock{this->scene_catalog_mutex};
                if (*pending_index >= this->scene_catalog.entries.size()) throw std::runtime_error("Spectra scene catalog validation index is out of range");
                if (this->scene_catalog.entries[*pending_index].id != pending_entry.id) throw std::runtime_error("Spectra scene catalog changed while validating");
                this->scene_catalog.entries[*pending_index] = std::move(pending_entry);
                this->clear_translation_cache_for_scene(this->scene_catalog.entries[*pending_index].id);
                this->refresh_scene_catalog_counts();
            }
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

    SpectraSceneSession::DisplayState SpectraSceneSession::display_state(const spectra::scene::SceneCatalogEntry& entry, const std::optional<spectra::scene::SceneTranslationReport>& report) const {
        if (entry.state == spectra::scene::SceneCatalogEntryState::Pending) return DisplayState::Checking;
        if (entry.state == spectra::scene::SceneCatalogEntryState::Invalid) return DisplayState::Invalid;
        if (entry.state != spectra::scene::SceneCatalogEntryState::Ready) throw std::runtime_error("Unknown Spectra scene catalog entry state");
        if (report.has_value() && !report->supported) return DisplayState::Unsupported;
        return DisplayState::Ready;
    }

    std::optional<spectra::scene::SceneTranslationReport> SpectraSceneSession::cached_entry_report(const spectra::scene::SceneCatalogEntry& entry) {
        std::string renderer_name{};
        {
            std::scoped_lock lock{this->scene_catalog_mutex};
            renderer_name = this->active_renderer;
        }
        if (renderer_name.empty() || entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) return {};
        return this->analyze_document(renderer_name, *entry.document);
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
        if (entry.state != spectra::scene::SceneCatalogEntryState::Ready || entry.document == nullptr) throw std::runtime_error(std::format("Cannot load disabled Spectra scene \"{}\"", entry.id));
        if (!renderer_name.empty()) {
            const spectra::scene::SceneTranslationReport report = this->analyze_document(renderer_name, *entry.document);
            if (!report.supported) throw std::runtime_error(std::format("Cannot load Spectra scene \"{}\" for renderer \"{}\"", entry.id, renderer_name));
        }
        this->commit_document(scene_index, *entry.document);
    }

    void SpectraSceneSession::draw_scene_library_window() {
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
                const std::optional<spectra::scene::SceneTranslationReport> report = this->cached_entry_report(entry);
                const DisplayState state                                          = this->display_state(entry, report);
                if (!display_filter_matches(state, filter_snapshot)) continue;
                if (!contains_case_insensitive(entry.id, search_text) && !contains_case_insensitive(entry.displayName, search_text) && !contains_case_insensitive(entry.group, search_text)) continue;

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
        const std::optional<spectra::scene::SceneTranslationReport> selected_report = this->cached_entry_report(selected_entry);
        const DisplayState selected_state                                           = this->display_state(selected_entry, selected_report);
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
