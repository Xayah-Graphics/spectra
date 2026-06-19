#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer.renderer;
import spectra.rasterizer.renderer;
import spectra.rasterizer.scene_runtime;
import spectra.scene;
import xayah.util.xcli;

namespace {
    static_assert(spectra::pathtracer::Host<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);

    struct SceneWorkspaceStatusState {
        std::string status_text{};
        bool status_error{};
        std::chrono::steady_clock::time_point status_expires{};
    };

    [[nodiscard]] std::string lowercase_ascii(std::string value) {
        for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        return value;
    }

    [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
        return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
    }

    [[nodiscard]] bool is_pbrt_scene_file(const std::filesystem::path& path) {
        if (path_extension_is(path, ".pbrt")) return true;
        if (!path_extension_is(path, ".gz")) return false;
        return path_extension_is(path.stem(), ".pbrt");
    }

    [[nodiscard]] std::string scene_file_title(const std::filesystem::path& path) {
        std::filesystem::path filename = path.filename();
        if (path_extension_is(filename, ".gz")) filename = filename.stem();
        if (path_extension_is(filename, ".pbrt")) filename = filename.stem();
        if (filename.empty()) throw std::runtime_error("PBRT scene path has an empty filename");
        return filename.string();
    }

    void set_scene_status(SceneWorkspaceStatusState& state, std::string text, const bool error) {
        state.status_text = std::move(text);
        state.status_error = error;
        state.status_expires = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    }

    struct DynamicSceneOpenOptionEditor {
        spectra::rasterizer::DynamicSceneOpenOptionSchema schema{};
        std::string text_value{};
        std::vector<char> text_buffer{};
        bool bool_value{};
        float float_value{};
        std::uint64_t unsigned_value{};
        bool enabled{true};
    };

    struct DynamicSceneProjectActionEditor {
        spectra::rasterizer::DynamicSceneProjectAction action{};
        std::vector<DynamicSceneOpenOptionEditor> editors{};
    };

    enum class DynamicSceneProjectPhase {
        None,
        PluginLoaded,
        Active,
        Error,
    };

    struct DynamicSceneProjectState {
        DynamicSceneProjectPhase phase{DynamicSceneProjectPhase::None};
        spectra::rasterizer::DynamicScenePluginInfo plugin{};
        std::vector<DynamicSceneOpenOptionEditor> editors{};
        std::vector<DynamicSceneProjectActionEditor> action_editors{};
        std::string error{};
        std::string active_title{};
        std::string active_id{};
    };

    int resize_input_text_callback(ImGuiInputTextCallbackData* data) {
        if (data == nullptr) return 0;
        if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
        std::vector<char>* value = static_cast<std::vector<char>*>(data->UserData);
        if (value == nullptr) return 0;
        value->resize(static_cast<std::size_t>(data->BufTextLen) + 1u);
        data->Buf = value->data();
        return 0;
    }

    void set_text_buffer(std::vector<char>& buffer, const std::string& value) {
        buffer.assign(value.begin(), value.end());
        buffer.push_back('\0');
    }

    [[nodiscard]] std::string text_buffer_value(const std::vector<char>& buffer) {
        if (buffer.empty()) return {};
        return std::string{buffer.data()};
    }

    bool input_text(std::string label, std::vector<char>& value) {
        if (value.empty()) value.push_back('\0');
        return ImGui::InputText(label.c_str(), value.data(), value.size(), ImGuiInputTextFlags_CallbackResize, resize_input_text_callback, &value);
    }

    [[nodiscard]] float parse_float_text(const std::string_view value, const std::string_view context) {
        float parsed{};
        const char* const begin = value.data();
        const char* const end = value.data() + value.size();
        const std::from_chars_result result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) throw std::runtime_error(std::format("{} must be a finite float", context));
        return parsed;
    }

    [[nodiscard]] std::uint64_t parse_unsigned_integer_text(const std::string_view value, const std::string_view context) {
        std::uint64_t parsed{};
        const char* const begin = value.data();
        const char* const end = value.data() + value.size();
        const std::from_chars_result result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error(std::format("{} must be an unsigned integer", context));
        return parsed;
    }

    [[nodiscard]] bool parse_bool_text(const std::string_view value, const std::string_view context) {
        if (value == "true") return true;
        if (value == "false") return false;
        throw std::runtime_error(std::format("{} must be true or false", context));
    }

    [[nodiscard]] bool choice_contains_value(const spectra::rasterizer::DynamicSceneOpenOptionSchema& schema, const std::string& value) {
        return std::ranges::any_of(schema.choices, [&value](const spectra::rasterizer::DynamicSceneOpenOptionChoice& choice) { return choice.value == value; });
    }

    [[nodiscard]] DynamicSceneOpenOptionEditor make_open_option_editor(spectra::rasterizer::DynamicSceneOpenOptionSchema schema) {
        DynamicSceneOpenOptionEditor editor{.schema = std::move(schema)};
        editor.enabled = editor.schema.required || !editor.schema.default_value.empty();
        switch (editor.schema.kind) {
            case spectra::rasterizer::DynamicSceneOpenOptionKind::Bool:
                editor.bool_value = editor.schema.default_value.empty() ? false : parse_bool_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::rasterizer::DynamicSceneOpenOptionKind::Float:
                editor.float_value = editor.schema.default_value.empty() ? 0.0f : parse_float_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::rasterizer::DynamicSceneOpenOptionKind::UnsignedInteger:
                editor.unsigned_value = editor.schema.default_value.empty() ? 0u : parse_unsigned_integer_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = editor.schema.default_value;
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
        }
        return editor;
    }

    [[nodiscard]] DynamicSceneProjectActionEditor make_project_action_editor(spectra::rasterizer::DynamicSceneProjectAction action) {
        DynamicSceneProjectActionEditor editor{.action = std::move(action)};
        editor.editors.reserve(editor.action.options.size());
        for (const spectra::rasterizer::DynamicSceneOpenOptionSchema& schema : editor.action.options) editor.editors.push_back(make_open_option_editor(schema));
        return editor;
    }

    [[nodiscard]] std::vector<spectra::rasterizer::DynamicSceneOpenOption> collect_open_options(const std::span<const DynamicSceneOpenOptionEditor> editors) {
        std::vector<spectra::rasterizer::DynamicSceneOpenOption> options{};
        options.reserve(editors.size());
        for (const DynamicSceneOpenOptionEditor& editor : editors) {
            if (!editor.schema.required && !editor.enabled) continue;
            std::string value{};
            switch (editor.schema.kind) {
                case spectra::rasterizer::DynamicSceneOpenOptionKind::Bool:
                    value = editor.bool_value ? "true" : "false";
                    break;
                case spectra::rasterizer::DynamicSceneOpenOptionKind::Float:
                    value = std::format("{:.9g}", editor.float_value);
                    break;
                case spectra::rasterizer::DynamicSceneOpenOptionKind::UnsignedInteger:
                    value = std::format("{}", editor.unsigned_value);
                    break;
                default:
                    value = editor.schema.kind == spectra::rasterizer::DynamicSceneOpenOptionKind::Choice ? editor.text_value : text_buffer_value(editor.text_buffer);
                    break;
            }
            if (editor.schema.required && value.empty()) throw std::runtime_error(std::format("{} is required", editor.schema.label));
            if (editor.schema.kind == spectra::rasterizer::DynamicSceneOpenOptionKind::Choice && !value.empty() && !choice_contains_value(editor.schema, value)) throw std::runtime_error(std::format("{} must be one of the declared choices", editor.schema.label));
            if (!value.empty() || editor.schema.required || editor.schema.kind == spectra::rasterizer::DynamicSceneOpenOptionKind::Bool || editor.schema.kind == spectra::rasterizer::DynamicSceneOpenOptionKind::Float || editor.schema.kind == spectra::rasterizer::DynamicSceneOpenOptionKind::UnsignedInteger) {
                options.push_back(spectra::rasterizer::DynamicSceneOpenOption{
                    .key = editor.schema.key,
                    .value = std::move(value),
                });
            }
        }
        return options;
    }

    [[nodiscard]] bool dynamic_scene_project_loaded(const DynamicSceneProjectState& project) {
        return project.phase != DynamicSceneProjectPhase::None;
    }

    [[nodiscard]] bool scene_status_visible(SceneWorkspaceStatusState& state) {
        if (state.status_text.empty()) return false;
        if (std::chrono::steady_clock::now() < state.status_expires) return true;
        state.status_text.clear();
        state.status_error = false;
        return false;
    }

    [[nodiscard]] std::string activate_pbrt_scene_path(spectra::rasterizer::SceneController& controller, const std::filesystem::path& scene_path) {
        if (scene_path.empty()) throw std::runtime_error("Drop a PBRT scene file into the window to load it");
        const std::filesystem::path absolute_path = std::filesystem::absolute(scene_path).lexically_normal();
        if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a PBRT scene file, not a folder");
        if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", absolute_path.string()));
        if (!is_pbrt_scene_file(absolute_path)) throw std::runtime_error(std::format("{}: scene file must use .pbrt or .pbrt.gz", absolute_path.string()));
        const std::string id = absolute_path.string();
        const std::string title = scene_file_title(absolute_path);
        const bool activated = controller.activate_static_scene(id, title, [absolute_path] { return std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt_file(absolute_path)); });
        if (!activated) throw std::runtime_error(controller.activation_error().empty() ? "Failed to load static scene" : controller.activation_error());
        return title;
    }

    struct DynamicSceneActivationResult {
        std::string id{};
        std::string title{};
    };

    [[nodiscard]] DynamicSceneActivationResult activate_dynamic_scene_plugin(spectra::rasterizer::SceneController& controller, spectra::rasterizer::DynamicSceneOpenRequest request) {
        if (request.host_services == nullptr) request.host_services = controller.dynamic_host_services();
        spectra::rasterizer::DynamicScenePluginSource plugin = spectra::rasterizer::load_dynamic_scene_plugin(std::move(request));
        const std::string id = plugin.id;
        const std::string title = plugin.title;
        const bool activated = controller.activate_dynamic_scene(std::move(plugin.id), std::move(plugin.title), std::move(plugin.create_source));
        if (!activated) throw std::runtime_error(controller.activation_error().empty() ? "Failed to load dynamic scene plugin" : controller.activation_error());
        return DynamicSceneActivationResult{
            .id = id,
            .title = title,
        };
    }

    [[nodiscard]] std::string activate_scene_path(spectra::rasterizer::SceneController& controller, const std::filesystem::path& scene_path) {
        if (is_pbrt_scene_file(scene_path)) return activate_pbrt_scene_path(controller, scene_path);
        throw std::runtime_error(std::format("{}: drop a .pbrt/.pbrt.gz scene or a dynamic scene plugin library", scene_path.string()));
    }

    void clear_dynamic_scene_project(DynamicSceneProjectState& project) {
        project = DynamicSceneProjectState{};
    }

    void begin_dynamic_scene_project(DynamicSceneProjectState& project, spectra::rasterizer::DynamicScenePluginInfo plugin) {
        project.plugin = std::move(plugin);
        project.editors.clear();
        project.editors.reserve(project.plugin.open_options.size());
        for (spectra::rasterizer::DynamicSceneOpenOptionSchema& schema : project.plugin.open_options) project.editors.push_back(make_open_option_editor(std::move(schema)));
        project.action_editors.clear();
        project.action_editors.reserve(project.plugin.project_actions.size());
        for (const spectra::rasterizer::DynamicSceneProjectAction& action : project.plugin.project_actions) project.action_editors.push_back(make_project_action_editor(action));
        project.error.clear();
        project.active_title.clear();
        project.active_id.clear();
        project.phase = DynamicSceneProjectPhase::PluginLoaded;
    }

    [[nodiscard]] std::string open_dynamic_scene_project(spectra::rasterizer::SceneController& controller, DynamicSceneProjectState& project, const std::filesystem::path& plugin_path) {
        spectra::rasterizer::DynamicScenePluginInfo plugin = spectra::rasterizer::inspect_dynamic_scene_plugin(plugin_path);
        const std::string title = plugin.title;
        controller.activate_empty_workspace();
        begin_dynamic_scene_project(project, std::move(plugin));
        return title;
    }

    [[nodiscard]] bool handle_scene_file_drop(spectra::Spectra& application, spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneProjectState& project, const std::span<const std::filesystem::path> paths) {
        if (paths.empty()) {
            set_scene_status(state, "Drop a PBRT scene or dynamic scene plugin to load it", true);
            return true;
        }
        if (paths.size() != 1u) {
            set_scene_status(state, "Drop exactly one scene file or dynamic scene plugin", true);
            return true;
        }
        try {
            const std::filesystem::path& scene_path = paths.front();
            if (spectra::rasterizer::is_dynamic_scene_plugin_file(scene_path)) {
                const std::string title = open_dynamic_scene_project(controller, project, scene_path);
                set_scene_status(state, std::format("Opened dynamic project {}", title), false);
                application.open_command_popover("scene.dynamic-project");
                return true;
            }
            const std::string title = activate_scene_path(controller, scene_path);
            clear_dynamic_scene_project(project);
            application.close_command_popover("scene.dynamic-project");
            set_scene_status(state, std::format("Loaded {}", title), false);
        } catch (const std::exception& error) {
            set_scene_status(state, error.what(), true);
        }
        return true;
    }

    void draw_open_option_editor(DynamicSceneOpenOptionEditor& editor) {
        ImGui::PushID(editor.schema.key.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(editor.schema.label.c_str());
        if (!editor.schema.required && editor.schema.default_value.empty()) {
            ImGui::SameLine();
            ImGui::Checkbox("Set", &editor.enabled);
        }
        if (!editor.schema.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", editor.schema.description.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::BeginDisabled(!editor.enabled);
        switch (editor.schema.kind) {
            case spectra::rasterizer::DynamicSceneOpenOptionKind::Choice: {
                const char* preview = editor.text_value.empty() ? "Select..." : editor.text_value.c_str();
                if (ImGui::BeginCombo("##value", preview)) {
                    for (const spectra::rasterizer::DynamicSceneOpenOptionChoice& choice : editor.schema.choices) {
                        const bool selected = editor.text_value == choice.value;
                        if (ImGui::Selectable(choice.label.c_str(), selected)) editor.text_value = choice.value;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case spectra::rasterizer::DynamicSceneOpenOptionKind::Bool:
                ImGui::Checkbox("##value", &editor.bool_value);
                break;
            case spectra::rasterizer::DynamicSceneOpenOptionKind::Float:
                ImGui::InputFloat("##value", &editor.float_value, 0.0f, 0.0f, "%.6g");
                break;
            case spectra::rasterizer::DynamicSceneOpenOptionKind::UnsignedInteger:
                ImGui::InputScalar("##value", ImGuiDataType_U64, &editor.unsigned_value);
                break;
            default:
                input_text("##value", editor.text_buffer);
                break;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    [[nodiscard]] const char* dynamic_scene_project_phase_text(const DynamicSceneProjectPhase phase) {
        switch (phase) {
        case DynamicSceneProjectPhase::None: return "No Project";
        case DynamicSceneProjectPhase::PluginLoaded: return "Plugin Loaded";
        case DynamicSceneProjectPhase::Active: return "Active";
        case DynamicSceneProjectPhase::Error: return "Error";
        }
        throw std::runtime_error("Unknown dynamic scene project phase");
    }

    [[nodiscard]] bool project_action_enabled(const spectra::rasterizer::DynamicSceneProjectStatus& status, const std::string& action_id) {
        return std::ranges::any_of(status.enabled_action_ids, [&action_id](const std::string& enabled_action_id) { return enabled_action_id == action_id; });
    }

    [[nodiscard]] ImVec4 project_log_level_color(const std::string& level) {
        if (level == "ERROR") return ImVec4{1.0f, 0.42f, 0.36f, 1.0f};
        if (level == "WARN") return ImVec4{1.0f, 0.78f, 0.32f, 1.0f};
        if (level == "OPTIMIZE") return ImVec4{0.45f, 0.9f, 0.58f, 1.0f};
        if (level == "START") return ImVec4{0.45f, 0.78f, 1.0f, 1.0f};
        return ImVec4{0.68f, 0.73f, 0.80f, 1.0f};
    }

    void draw_dynamic_project_status(const spectra::rasterizer::DynamicSceneProjectStatus& status) {
        ImGui::TextDisabled("%s", "Project Status");
        ImGui::TextUnformatted(status.phase.c_str());
        ImGui::TextWrapped("%s", status.headline.c_str());
        if (!status.detail.empty()) ImGui::TextWrapped("%s", status.detail.c_str());
        if (!status.metrics.empty()) {
            ImGui::Spacing();
            for (const spectra::rasterizer::DynamicSceneProjectMetric& metric : status.metrics) {
                ImGui::TextDisabled("%s", metric.label.c_str());
                ImGui::SameLine();
                ImGui::TextWrapped("%s", metric.value.c_str());
            }
        }
    }

    void draw_dynamic_project_logs(const std::span<const spectra::rasterizer::DynamicSceneProjectLogEntry> logs) {
        ImGui::TextDisabled("%s", "Log");
        ImGui::BeginChild("DynamicProjectLogs", ImVec2{-1.0f, 180.0f}, true);
        if (logs.empty()) {
            ImGui::TextDisabled("%s", "No log entries");
        } else {
            for (const spectra::rasterizer::DynamicSceneProjectLogEntry& entry : logs) {
                const std::string line = std::format("{:>5} {:<9} {}", entry.sequence, entry.level, entry.message);
                ImGui::TextColored(project_log_level_color(entry.level), "%s", line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    void draw_dynamic_project_action_editor(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneProjectState& project, DynamicSceneProjectActionEditor& editor, const spectra::rasterizer::DynamicSceneProjectStatus& status) {
        ImGui::PushID(editor.action.id.c_str());
        const std::string header_label = std::format("{}##header", editor.action.label);
        const bool opened = ImGui::CollapsingHeader(header_label.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (opened) {
            if (!editor.action.description.empty()) ImGui::TextWrapped("%s", editor.action.description.c_str());
            for (DynamicSceneOpenOptionEditor& option_editor : editor.editors) {
                draw_open_option_editor(option_editor);
                ImGui::Spacing();
            }
            const bool enabled = project_action_enabled(status, editor.action.id);
            ImGui::BeginDisabled(!enabled);
            const std::string button_label = std::format("{}##execute", editor.action.label);
            const bool clicked = ImGui::Button(button_label.c_str(), ImVec2{-1.0f, 0.0f});
            ImGui::EndDisabled();
            if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", "Action disabled by project status");
            if (clicked) {
                try {
                    const std::vector<spectra::rasterizer::DynamicSceneOpenOption> options = collect_open_options(editor.editors);
                    controller.execute_active_dynamic_project_action(editor.action.id, options);
                    project.phase = DynamicSceneProjectPhase::Active;
                    project.error.clear();
                    set_scene_status(state, std::format("Executed {}", editor.action.label), false);
                } catch (const std::exception& error) {
                    project.phase = DynamicSceneProjectPhase::Error;
                    project.error = error.what();
                    set_scene_status(state, project.error, true);
                }
            }
        }
        ImGui::PopID();
    }

    void draw_dynamic_scene_project_panel(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneProjectState& project) {
        if (!dynamic_scene_project_loaded(project)) {
            ImGui::TextDisabled("%s", "No dynamic project");
            return;
        }

        std::optional<spectra::rasterizer::DynamicSceneProjectStatus> active_status{};
        std::vector<spectra::rasterizer::DynamicSceneProjectLogEntry> active_logs{};
        if (project.phase == DynamicSceneProjectPhase::Active || project.phase == DynamicSceneProjectPhase::Error) {
            try {
                if (controller.has_active_dynamic_project()) {
                    active_status = controller.active_dynamic_project_status();
                    active_logs = controller.active_dynamic_project_logs();
                }
            } catch (const std::exception& error) {
                project.phase = DynamicSceneProjectPhase::Error;
                project.error = error.what();
            }
        }

        ImGui::TextUnformatted(project.plugin.project_panel_title.c_str());
        ImGui::TextColored(ImVec4{0.55f, 0.62f, 0.70f, 1.0f}, "%s", project.plugin.title.c_str());
        ImGui::TextWrapped("%s", project.plugin.path.string().c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", "Status");
        ImGui::TextUnformatted(dynamic_scene_project_phase_text(project.phase));
        if (!project.active_title.empty()) {
            ImGui::TextDisabled("%s", "Active Scene");
            ImGui::TextWrapped("%s", project.active_title.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Dataset", ImGuiTreeNodeFlags_DefaultOpen)) {
            for (DynamicSceneOpenOptionEditor& editor : project.editors) {
                draw_open_option_editor(editor);
                ImGui::Spacing();
            }
            const bool open_disabled = active_status.has_value() && active_status->phase == "Running";
            ImGui::BeginDisabled(open_disabled);
            if (ImGui::Button(project.plugin.open_action_label.c_str(), ImVec2{-1.0f, 0.0f})) {
                try {
                    spectra::rasterizer::DynamicSceneOpenRequest request{
                        .plugin_path = project.plugin.path,
                        .options = collect_open_options(project.editors),
                    };
                    const DynamicSceneActivationResult result = activate_dynamic_scene_plugin(controller, std::move(request));
                    project.phase = DynamicSceneProjectPhase::Active;
                    project.active_id = result.id;
                    project.active_title = result.title;
                    project.error.clear();
                    set_scene_status(state, std::format("Loaded {}", result.title), false);
                } catch (const std::exception& error) {
                    project.phase = DynamicSceneProjectPhase::Error;
                    project.error = error.what();
                    project.active_title.clear();
                    project.active_id.clear();
                    controller.activate_empty_workspace();
                    set_scene_status(state, project.error, true);
                }
            }
            ImGui::EndDisabled();
            if (open_disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", "Project is running");
            else if (!project.plugin.open_action_description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", project.plugin.open_action_description.c_str());
        }

        if (active_status.has_value()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            draw_dynamic_project_status(*active_status);
            if (!project.action_editors.empty()) {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextDisabled("%s", "Actions");
                for (DynamicSceneProjectActionEditor& editor : project.action_editors) draw_dynamic_project_action_editor(controller, state, project, editor, *active_status);
            }
            ImGui::Spacing();
            draw_dynamic_project_logs(active_logs);
        }

        if (!project.error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", project.error.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Close Project", ImVec2{-1.0f, 0.0f})) {
            clear_dynamic_scene_project(project);
            controller.activate_empty_workspace();
            set_scene_status(state, "Closed dynamic project", false);
        }
    }

    [[nodiscard]] std::string scene_workspace_tooltip(const spectra::rasterizer::SceneEntry* selected_entry, const bool pending_switch) {
        if (selected_entry == nullptr) return "Empty Project\nDrop a PBRT scene or dynamic scene plugin into the window to load it";
        return std::format(
            "{}\n{}{}\nDrop a PBRT scene or dynamic scene plugin into the window to replace it",
            selected_entry->id,
            selected_entry->kind == spectra::rasterizer::SceneEntryKind::Static ? "Static" : "Dynamic",
            pending_switch ? "\nSwitching on next frame" : "");
    }

    [[nodiscard]] spectra::WorkspaceTitle make_scene_workspace_title(spectra::rasterizer::SceneController& controller, SceneWorkspaceStatusState& state) {
        const std::optional<std::size_t> selected_index = controller.has_selected_entry() ? std::optional<std::size_t>{controller.selected_index()} : std::nullopt;
        const spectra::rasterizer::SceneEntry* selected_entry = selected_index.has_value() ? &controller.entry(*selected_index) : nullptr;
        spectra::WorkspaceTitle title{
            .detail  = selected_entry != nullptr ? selected_entry->title : "Untitled",
            .tooltip = scene_workspace_tooltip(selected_entry, controller.pending_switch()),
        };
        if (scene_status_visible(state)) {
            title.status_text = state.status_text;
            title.status_error = state.status_error;
        }
        return title;
    }

    class PathtracerRendererAdapter final {
    public:
        PathtracerRendererAdapter(std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)) {
            if (this->scene_controller == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Pathtracer adapter requires a scene camera workspace");
            this->active_workspace = this->scene_controller->active_workspace();
            this->renderer = std::make_unique<spectra::pathtracer::Renderer>(this->active_workspace, this->camera_workspace);
        }

        PathtracerRendererAdapter(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter(PathtracerRendererAdapter&& other) noexcept = default;
        PathtracerRendererAdapter& operator=(const PathtracerRendererAdapter& other) = delete;
        PathtracerRendererAdapter& operator=(PathtracerRendererAdapter&& other) noexcept = default;
        ~PathtracerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::pathtracer::Renderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::pathtracer::HostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->scene_controller->apply_pending_scene());
            this->sync_scene_workspace();
            this->scene_controller->update_active_project(frame.delta_seconds);
            const spectra::pathtracer::FrameContext frame_context{
                .frame_index = frame.frame_slot_index,
                .image_index = frame.image_index,
            };
            spectra::pathtracer::FrameResult result = this->renderer->begin_frame(spectra::pathtracer::HostView{host}, frame_context);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->scene_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::pathtracer::Renderer> renderer{};
    };

    class RasterizerRendererAdapter final {
    public:
        RasterizerRendererAdapter(std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace, std::shared_ptr<spectra::rasterizer::DynamicSceneHostServiceRouter> dynamic_host_services) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)), dynamic_host_services(std::move(dynamic_host_services)) {
            if (this->scene_controller == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene controller");
            if (this->camera_workspace == nullptr) throw std::runtime_error("Rasterizer adapter requires a scene camera workspace");
            if (this->dynamic_host_services == nullptr) throw std::runtime_error("Rasterizer adapter requires dynamic scene host services");
            this->active_workspace = this->scene_controller->active_workspace();
            this->renderer = std::make_unique<spectra::rasterizer::Renderer>(this->active_workspace, this->camera_workspace, this->dynamic_host_services);
        }

        RasterizerRendererAdapter(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter(RasterizerRendererAdapter&& other) noexcept = default;
        RasterizerRendererAdapter& operator=(const RasterizerRendererAdapter& other) = delete;
        RasterizerRendererAdapter& operator=(RasterizerRendererAdapter&& other) noexcept = default;
        ~RasterizerRendererAdapter() noexcept = default;

        [[nodiscard]] static std::string_view name() {
            return spectra::rasterizer::Renderer::name();
        }

        void attach(spectra::Spectra& host) {
            this->renderer->attach(spectra::rasterizer::HostView{host});
        }

        void detach() noexcept {
            this->renderer->detach();
        }

        void before_imgui_shutdown() noexcept {
            this->renderer->before_imgui_shutdown();
        }

        void after_imgui_created() {
            this->renderer->after_imgui_created();
        }

        [[nodiscard]] spectra::FrameResult begin_frame(spectra::Spectra& host, const spectra::FrameContext& frame) {
            static_cast<void>(this->scene_controller->apply_pending_scene());
            this->sync_scene_workspace();
            this->scene_controller->update_active_project(frame.delta_seconds);
            this->scene_controller->update_active_scene(frame.delta_seconds);
            const spectra::rasterizer::FrameContext rasterizer_frame{
                .frame_index   = frame.frame_slot_index,
                .image_index   = frame.image_index,
                .frame_number  = frame.frame_number,
                .delta_seconds = frame.delta_seconds,
            };
            spectra::rasterizer::FrameResult result = this->renderer->begin_frame(spectra::rasterizer::HostView{host}, rasterizer_frame);
            return spectra::FrameResult{
                .completion_semaphore = std::move(result.completion_semaphore),
                .close_requested      = result.close_requested,
                .window_detail        = std::move(result.window_detail),
            };
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer) {
            this->renderer->record_frame(command_buffer);
        }

    private:
        void sync_scene_workspace() {
            std::shared_ptr<spectra::scene::Scene> current_workspace = this->scene_controller->active_workspace();
            if (this->active_workspace == current_workspace) return;
            this->renderer->set_scene_workspace(current_workspace, this->camera_workspace);
            this->active_workspace = std::move(current_workspace);
        }

        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::rasterizer::DynamicSceneHostServiceRouter> dynamic_host_services{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::rasterizer::Renderer> renderer{};
    };

    static_assert(spectra::RendererFor<PathtracerRendererAdapter, spectra::Spectra>);
    static_assert(spectra::RendererFor<RasterizerRendererAdapter, spectra::Spectra>);

    [[nodiscard]] std::shared_ptr<spectra::scene::Scene> make_empty_project_scene() {
        spectra::scene::Scene::Document document{
            .revision = spectra::scene::Scene::Revision{1},
            .name = "untitled",
            .title = "Untitled",
            .source = "scene://untitled",
            .timeline_enabled = false,
            .cameras = {
                spectra::scene::Scene::Camera{
                    .name = "Camera",
                    .view = spectra::scene::camera_view_from_look_at(
                        spectra::scene::Vector3{0.0f, 1.0f, 5.0f},
                        spectra::scene::Vector3{0.0f, 0.0f, 0.0f},
                        spectra::scene::Vector3{0.0f, 1.0f, 0.0f},
                        spectra::scene::CameraProjection{
                            .kind = spectra::scene::CameraProjectionKind::Perspective,
                            .vertical_fov_degrees = 45.0f,
                            .near_plane = 0.01f,
                            .far_plane = 200.0f,
                        }
                    ),
                },
            },
            .active_camera_name = "Camera",
        };
        return std::make_shared<spectra::scene::Scene>(std::move(document));
    }

    [[nodiscard]] std::optional<spectra::rasterizer::DynamicScenePluginInfo> load_cli_scene(spectra::rasterizer::SceneController& controller, const std::string& scene_id) {
        if (const std::size_t query_begin = scene_id.find('?'); query_begin != std::string::npos) {
            const std::string plugin_path_text = scene_id.substr(0u, query_begin);
            if (!plugin_path_text.empty() && spectra::rasterizer::is_dynamic_scene_plugin_file(std::filesystem::path{plugin_path_text})) throw std::runtime_error("Dynamic scene plugin Scene URI query is not supported; pass the plugin path without query and configure it in the Project popover");
        }
        const std::filesystem::path requested_path{scene_id};
        if ((is_pbrt_scene_file(requested_path) || spectra::rasterizer::is_dynamic_scene_plugin_file(requested_path)) && !std::filesystem::is_regular_file(requested_path)) throw std::runtime_error(std::format("{}: initial scene file does not exist", requested_path.string()));
        if (spectra::rasterizer::is_dynamic_scene_plugin_file(requested_path)) {
            controller.activate_empty_workspace();
            return spectra::rasterizer::inspect_dynamic_scene_plugin(requested_path);
        }
        if (std::filesystem::is_regular_file(requested_path)) {
            static_cast<void>(activate_scene_path(controller, requested_path));
            return std::nullopt;
        }
        std::shared_ptr<spectra::scene::Scene> scene = std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt(scene_id));
        const std::string title = scene->info().title;
        if (!controller.activate_static_scene(scene_id, title, [scene = std::move(scene)] { return scene; })) throw std::runtime_error(controller.activation_error());
        return std::nullopt;
    }

    void register_renderers(spectra::Spectra& application, std::shared_ptr<spectra::rasterizer::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace, std::shared_ptr<spectra::rasterizer::DynamicSceneHostServiceRouter> dynamic_host_services, std::optional<spectra::rasterizer::DynamicScenePluginInfo> initial_dynamic_scene_plugin) {
        if (scene_controller == nullptr) throw std::runtime_error("Renderer registration requires a scene controller");
        if (camera_workspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        if (dynamic_host_services == nullptr) throw std::runtime_error("Renderer registration requires dynamic scene host services");
        std::shared_ptr<SceneWorkspaceStatusState> scene_status_state = std::make_shared<SceneWorkspaceStatusState>();
        std::shared_ptr<DynamicSceneProjectState> dynamic_scene_project = std::make_shared<DynamicSceneProjectState>();
        if (initial_dynamic_scene_plugin.has_value()) {
            const std::string title = initial_dynamic_scene_plugin->title;
            begin_dynamic_scene_project(*dynamic_scene_project, std::move(*initial_dynamic_scene_plugin));
            set_scene_status(*scene_status_state, std::format("Opened dynamic project {}", title), false);
        }
        application.register_renderer(RasterizerRendererAdapter{scene_controller, camera_workspace, std::move(dynamic_host_services)});
        application.register_renderer(PathtracerRendererAdapter{scene_controller, std::move(camera_workspace)});
        std::shared_ptr<spectra::rasterizer::SceneController> drop_scene_controller = scene_controller;
        std::shared_ptr<SceneWorkspaceStatusState> drop_scene_status_state = scene_status_state;
        std::shared_ptr<DynamicSceneProjectState> drop_dynamic_scene_project = dynamic_scene_project;
        spectra::Spectra* drop_application = &application;
        std::shared_ptr<spectra::rasterizer::SceneController> project_scene_controller = scene_controller;
        std::shared_ptr<SceneWorkspaceStatusState> project_scene_status_state = scene_status_state;
        std::shared_ptr<DynamicSceneProjectState> panel_dynamic_scene_project = dynamic_scene_project;
        application.set_workspace_title_provider([scene_controller = std::move(scene_controller), scene_status_state = std::move(scene_status_state)] { return make_scene_workspace_title(*scene_controller, *scene_status_state); });
        application.register_file_drop_handler(spectra::FileDropHandler{
            .id             = "scene.file-drop",
            .title          = "Scene File Drop",
            .owner_renderer = {},
            .handle         = [application = drop_application, scene_controller = std::move(drop_scene_controller), scene_status_state = std::move(drop_scene_status_state), dynamic_scene_project = std::move(drop_dynamic_scene_project)](const std::span<const std::filesystem::path> paths) { return handle_scene_file_drop(*application, *scene_controller, *scene_status_state, *dynamic_scene_project, paths); },
        });
        application.register_command_popover(spectra::CommandPopover{
            .id             = "scene.dynamic-project",
            .title          = "Project",
            .icon           = ICON_MS_DATASET,
            .owner_renderer = {},
            .shortcut_label = "F9",
            .shortcut_key   = ImGuiKey_F9,
            .draw           = [scene_controller = std::move(project_scene_controller), scene_status_state = std::move(project_scene_status_state), dynamic_scene_project = std::move(panel_dynamic_scene_project)] { draw_dynamic_scene_project_panel(*scene_controller, *scene_status_state, *dynamic_scene_project); },
        });
        if (initial_dynamic_scene_plugin.has_value()) application.open_command_popover("scene.dynamic-project");
    }
} // namespace

int main(const int argc, const char* const* const argv) {
    try {
        const std::span<const char* const> arguments{argv, static_cast<std::size_t>(argc)};
        std::optional<std::string> scene_id{};
        xayah::util::Command command =
            xayah::util::Command{"Open the Spectra visualization workspace."}
            | xayah::util::option({.long_name = "scene", .value_name = "scene-id-or-path", .description = "PBRT scene id/path or dynamic scene plugin path", .show_default = false}, scene_id)
            | xayah::util::example("--scene default")
            | xayah::util::example("--scene scenes/pbrt-book/book.pbrt")
            | xayah::util::example("--scene C:/path/to/plugin.dll");
        const std::string usage = command.help(arguments);

        const auto cli_result = command.parse(arguments);
        if (!cli_result) {
            std::cerr << "error: " << cli_result.error() << '\n' << usage << std::endl;
            return 2;
        }
        if (cli_result->help_requested) {
            std::cout << usage << std::endl;
            return 0;
        }

        const auto cli_validation = command.validate();
        if (!cli_validation) {
            std::cerr << "error: " << cli_validation.error() << std::endl;
            return 2;
        }

        spectra::rasterizer::SceneRegistry scene_registry{};
        std::shared_ptr<spectra::rasterizer::DynamicSceneHostServiceRouter> dynamic_host_services = std::make_shared<spectra::rasterizer::DynamicSceneHostServiceRouter>();
        std::shared_ptr<spectra::rasterizer::SceneController> scene_controller = std::make_shared<spectra::rasterizer::SceneController>(std::move(scene_registry), make_empty_project_scene(), dynamic_host_services);
        std::optional<spectra::rasterizer::DynamicScenePluginInfo> initial_dynamic_scene_plugin{};
        if (scene_id.has_value()) initial_dynamic_scene_plugin = load_cli_scene(*scene_controller, *scene_id);
        std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace = std::make_shared<spectra::scene::CameraWorkspace>();

        spectra::Spectra app{"Spectra"};
        register_renderers(app, std::move(scene_controller), std::move(camera_workspace), std::move(dynamic_host_services), std::move(initial_dynamic_scene_plugin));
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
