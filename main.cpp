#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer.renderer;
import spectra.rasterizer.renderer;
import spectra.scene_runtime;
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

    struct DynamicSceneOptionEditor {
        spectra::scene_runtime::DynamicSceneOptionSchema schema{};
        std::string text_value{};
        std::vector<char> text_buffer{};
        bool bool_value{};
        float float_value{};
        std::uint64_t unsigned_value{};
        bool enabled{true};
    };

    struct DynamicSceneControlActionEditor {
        spectra::scene_runtime::DynamicSceneControlAction action{};
        std::vector<DynamicSceneOptionEditor> editors{};
    };

    struct DynamicSceneControlSettingEditor {
        spectra::scene_runtime::DynamicSceneControlSetting setting{};
        DynamicSceneOptionEditor editor{};
        std::string committed_value{};
    };

    enum class DynamicSceneControlsPhase {
        None,
        PluginLoaded,
        Active,
        Error,
    };

    struct DynamicSceneControlsState {
        DynamicSceneControlsPhase phase{DynamicSceneControlsPhase::None};
        spectra::scene_runtime::DynamicScenePluginInfo plugin{};
        std::vector<DynamicSceneOptionEditor> editors{};
        std::vector<DynamicSceneControlActionEditor> action_editors{};
        std::vector<DynamicSceneControlSettingEditor> setting_editors{};
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

    [[nodiscard]] bool choice_contains_value(const spectra::scene_runtime::DynamicSceneOptionSchema& schema, const std::string& value) {
        return std::ranges::any_of(schema.choices, [&value](const spectra::scene_runtime::DynamicSceneOptionChoice& choice) { return choice.value == value; });
    }

    [[nodiscard]] DynamicSceneOptionEditor make_dynamic_scene_option_editor(spectra::scene_runtime::DynamicSceneOptionSchema schema) {
        DynamicSceneOptionEditor editor{.schema = std::move(schema)};
        editor.enabled = editor.schema.required || !editor.schema.default_value.empty();
        switch (editor.schema.kind) {
            case spectra::scene_runtime::DynamicSceneOptionKind::Bool:
                editor.bool_value = editor.schema.default_value.empty() ? false : parse_bool_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::Float:
                editor.float_value = editor.schema.default_value.empty() ? 0.0f : parse_float_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger:
                editor.unsigned_value = editor.schema.default_value.empty() ? 0u : parse_unsigned_integer_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = editor.schema.default_value;
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
        }
        return editor;
    }

    [[nodiscard]] DynamicSceneControlActionEditor make_control_action_editor(spectra::scene_runtime::DynamicSceneControlAction action) {
        DynamicSceneControlActionEditor editor{.action = std::move(action)};
        editor.editors.reserve(editor.action.options.size());
        for (const spectra::scene_runtime::DynamicSceneOptionSchema& schema : editor.action.options) editor.editors.push_back(make_dynamic_scene_option_editor(schema));
        return editor;
    }

    [[nodiscard]] spectra::scene_runtime::DynamicSceneOptionSchema setting_as_option_schema(const spectra::scene_runtime::DynamicSceneControlSetting& setting) {
        return spectra::scene_runtime::DynamicSceneOptionSchema{
            .key = setting.key,
            .label = setting.label,
            .description = setting.description,
            .kind = setting.kind,
            .required = true,
            .default_value = setting.value,
            .group = setting.group,
            .advanced = setting.advanced,
            .priority = setting.priority,
            .choices = setting.choices,
        };
    }

    [[nodiscard]] DynamicSceneControlSettingEditor make_control_setting_editor(spectra::scene_runtime::DynamicSceneControlSetting setting) {
        const std::string value = setting.value;
        spectra::scene_runtime::DynamicSceneOptionSchema schema = setting_as_option_schema(setting);
        DynamicSceneControlSettingEditor editor{};
        editor.setting = std::move(setting);
        editor.editor = make_dynamic_scene_option_editor(std::move(schema));
        editor.committed_value = value;
        return editor;
    }

    [[nodiscard]] std::string dynamic_scene_option_editor_value(const DynamicSceneOptionEditor& editor) {
        switch (editor.schema.kind) {
            case spectra::scene_runtime::DynamicSceneOptionKind::Bool:
                return editor.bool_value ? "true" : "false";
            case spectra::scene_runtime::DynamicSceneOptionKind::Float:
                return std::format("{:.9g}", editor.float_value);
            case spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger:
                return std::format("{}", editor.unsigned_value);
            case spectra::scene_runtime::DynamicSceneOptionKind::Choice:
                return editor.text_value;
            default:
                return text_buffer_value(editor.text_buffer);
        }
    }

    void set_dynamic_scene_option_editor_value(DynamicSceneOptionEditor& editor, const std::string_view value) {
        switch (editor.schema.kind) {
            case spectra::scene_runtime::DynamicSceneOptionKind::Bool:
                editor.bool_value = parse_bool_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::Float:
                editor.float_value = parse_float_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger:
                editor.unsigned_value = parse_unsigned_integer_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = std::string{value};
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
        }
    }

    [[nodiscard]] std::vector<spectra::scene_runtime::DynamicSceneOption> collect_dynamic_scene_options(const std::span<const DynamicSceneOptionEditor> editors) {
        std::vector<spectra::scene_runtime::DynamicSceneOption> options{};
        options.reserve(editors.size());
        for (const DynamicSceneOptionEditor& editor : editors) {
            if (!editor.schema.required && !editor.enabled) continue;
            std::string value = dynamic_scene_option_editor_value(editor);
            if (editor.schema.required && value.empty()) throw std::runtime_error(std::format("{} is required", editor.schema.label));
            if (editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Choice && !value.empty() && !choice_contains_value(editor.schema, value)) throw std::runtime_error(std::format("{} must be one of the declared choices", editor.schema.label));
            if (!value.empty() || editor.schema.required || editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Bool || editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Float || editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger) {
                options.push_back(spectra::scene_runtime::DynamicSceneOption{
                    .key = editor.schema.key,
                    .value = std::move(value),
                });
            }
        }
        return options;
    }

    [[nodiscard]] bool dynamic_scene_controls_loaded(const DynamicSceneControlsState& controls) {
        return controls.phase != DynamicSceneControlsPhase::None;
    }

    [[nodiscard]] bool scene_status_visible(SceneWorkspaceStatusState& state) {
        if (state.status_text.empty()) return false;
        if (std::chrono::steady_clock::now() < state.status_expires) return true;
        state.status_text.clear();
        state.status_error = false;
        return false;
    }

    [[nodiscard]] std::string activate_pbrt_scene_path(spectra::scene_runtime::SceneController& controller, const std::filesystem::path& scene_path) {
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

    [[nodiscard]] DynamicSceneActivationResult activate_dynamic_scene_plugin(spectra::scene_runtime::SceneController& controller, spectra::scene_runtime::DynamicSceneOpenRequest request) {
        if (request.host_services == nullptr) request.host_services = controller.dynamic_host_services();
        spectra::scene_runtime::DynamicScenePluginSource plugin = spectra::scene_runtime::load_dynamic_scene_plugin(std::move(request));
        const std::string id = plugin.id;
        const std::string title = plugin.title;
        const bool activated = controller.activate_dynamic_scene(std::move(plugin.id), std::move(plugin.title), std::move(plugin.create_source));
        if (!activated) throw std::runtime_error(controller.activation_error().empty() ? "Failed to load dynamic scene plugin" : controller.activation_error());
        return DynamicSceneActivationResult{
            .id = id,
            .title = title,
        };
    }

    [[nodiscard]] std::string activate_scene_path(spectra::scene_runtime::SceneController& controller, const std::filesystem::path& scene_path) {
        if (is_pbrt_scene_file(scene_path)) return activate_pbrt_scene_path(controller, scene_path);
        throw std::runtime_error(std::format("{}: drop a .pbrt/.pbrt.gz scene or a dynamic scene plugin library", scene_path.string()));
    }

    void clear_dynamic_scene_controls(DynamicSceneControlsState& controls) {
        controls = DynamicSceneControlsState{};
    }

    void begin_dynamic_scene_controls(DynamicSceneControlsState& controls, spectra::scene_runtime::DynamicScenePluginInfo plugin) {
        controls.plugin = std::move(plugin);
        controls.editors.clear();
        controls.editors.reserve(controls.plugin.open_options.size());
        for (spectra::scene_runtime::DynamicSceneOptionSchema& schema : controls.plugin.open_options) controls.editors.push_back(make_dynamic_scene_option_editor(std::move(schema)));
        std::ranges::stable_sort(controls.editors, [](const DynamicSceneOptionEditor& left, const DynamicSceneOptionEditor& right) {
            if (left.schema.advanced != right.schema.advanced) return !left.schema.advanced && right.schema.advanced;
            if (left.schema.priority != right.schema.priority) return left.schema.priority < right.schema.priority;
            if (left.schema.group != right.schema.group) return left.schema.group < right.schema.group;
            return left.schema.key < right.schema.key;
        });
        controls.action_editors.clear();
        controls.action_editors.reserve(controls.plugin.control_actions.size());
        for (const spectra::scene_runtime::DynamicSceneControlAction& action : controls.plugin.control_actions) {
            controls.action_editors.push_back(make_control_action_editor(action));
            std::ranges::stable_sort(controls.action_editors.back().editors, [](const DynamicSceneOptionEditor& left, const DynamicSceneOptionEditor& right) {
                if (left.schema.advanced != right.schema.advanced) return !left.schema.advanced && right.schema.advanced;
                if (left.schema.priority != right.schema.priority) return left.schema.priority < right.schema.priority;
                if (left.schema.group != right.schema.group) return left.schema.group < right.schema.group;
                return left.schema.key < right.schema.key;
            });
        }
        std::ranges::stable_sort(controls.action_editors, [](const DynamicSceneControlActionEditor& left, const DynamicSceneControlActionEditor& right) {
            if (left.action.group != right.action.group) return left.action.group < right.action.group;
            if (left.action.priority != right.action.priority) return left.action.priority < right.action.priority;
            return left.action.id < right.action.id;
        });
        controls.setting_editors.clear();
        controls.error.clear();
        controls.active_title.clear();
        controls.active_id.clear();
        controls.phase = DynamicSceneControlsPhase::PluginLoaded;
    }

    [[nodiscard]] bool dynamic_scene_option_editor_has_advanced_state(const std::span<const DynamicSceneOptionEditor> editors, const bool advanced) {
        return std::ranges::any_of(editors, [advanced](const DynamicSceneOptionEditor& editor) { return editor.schema.advanced == advanced; });
    }

    [[nodiscard]] std::string dynamic_scene_option_editor_group_label(const DynamicSceneOptionEditor& editor) {
        return editor.schema.group.empty() ? "Options" : editor.schema.group;
    }

    [[nodiscard]] std::vector<std::string> dynamic_scene_option_editor_groups(const std::span<const DynamicSceneOptionEditor> editors, const bool advanced) {
        std::vector<std::string> groups{};
        for (const DynamicSceneOptionEditor& editor : editors) {
            if (editor.schema.advanced != advanced) continue;
            const std::string group = dynamic_scene_option_editor_group_label(editor);
            if (!std::ranges::contains(groups, group)) groups.push_back(group);
        }
        return groups;
    }

    [[nodiscard]] bool metric_has_placement(const spectra::scene_runtime::DynamicSceneControlMetric& metric, const std::uint32_t placement) {
        return (metric.placement_flags & placement) != 0u;
    }

    [[nodiscard]] std::vector<const spectra::scene_runtime::DynamicSceneControlMetric*> control_metrics_with_placement(const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::uint32_t placement) {
        std::vector<const spectra::scene_runtime::DynamicSceneControlMetric*> metrics{};
        for (const spectra::scene_runtime::DynamicSceneControlMetric& metric : status.metrics) {
            if (metric_has_placement(metric, placement)) metrics.push_back(&metric);
        }
        std::ranges::stable_sort(metrics, [](const spectra::scene_runtime::DynamicSceneControlMetric* left, const spectra::scene_runtime::DynamicSceneControlMetric* right) {
            if (left->priority != right->priority) return left->priority < right->priority;
            return left->key < right->key;
        });
        return metrics;
    }

    [[nodiscard]] std::vector<DynamicSceneControlActionEditor*> control_action_editors_for_group(std::vector<DynamicSceneControlActionEditor>& editors, const std::uint32_t group) {
        std::vector<DynamicSceneControlActionEditor*> selected{};
        for (DynamicSceneControlActionEditor& editor : editors) {
            if (editor.action.group == group) selected.push_back(&editor);
        }
        std::ranges::stable_sort(selected, [](const DynamicSceneControlActionEditor* left, const DynamicSceneControlActionEditor* right) {
            if (left->action.priority != right->action.priority) return left->action.priority < right->action.priority;
            return left->action.id < right->action.id;
        });
        return selected;
    }

    [[nodiscard]] std::vector<const spectra::scene_runtime::DynamicSceneControlScalarSeries*> scalar_series_for_group(const std::span<const spectra::scene_runtime::DynamicSceneControlScalarSeries> series, const std::uint32_t group) {
        std::vector<const spectra::scene_runtime::DynamicSceneControlScalarSeries*> selected{};
        for (const spectra::scene_runtime::DynamicSceneControlScalarSeries& chart : series) {
            if (chart.group == group) selected.push_back(&chart);
        }
        std::ranges::stable_sort(selected, [](const spectra::scene_runtime::DynamicSceneControlScalarSeries* left, const spectra::scene_runtime::DynamicSceneControlScalarSeries* right) {
            if (left->priority != right->priority) return left->priority < right->priority;
            return left->id < right->id;
        });
        return selected;
    }

    [[nodiscard]] std::string control_setting_group_label(const DynamicSceneControlSettingEditor& editor) {
        return editor.setting.group.empty() ? "Settings" : editor.setting.group;
    }

    [[nodiscard]] std::vector<std::string> control_setting_groups(const std::span<const DynamicSceneControlSettingEditor> editors, const bool advanced) {
        std::vector<std::string> groups{};
        for (const DynamicSceneControlSettingEditor& editor : editors) {
            if (editor.setting.advanced != advanced) continue;
            const std::string group = control_setting_group_label(editor);
            if (!std::ranges::contains(groups, group)) groups.push_back(group);
        }
        return groups;
    }

    void sort_control_setting_editors(std::vector<DynamicSceneControlSettingEditor>& editors) {
        std::ranges::stable_sort(editors, [](const DynamicSceneControlSettingEditor& left, const DynamicSceneControlSettingEditor& right) {
            if (left.setting.advanced != right.setting.advanced) return !left.setting.advanced && right.setting.advanced;
            if (left.setting.priority != right.setting.priority) return left.setting.priority < right.setting.priority;
            if (left.setting.group != right.setting.group) return left.setting.group < right.setting.group;
            return left.setting.key < right.setting.key;
        });
    }

    void sync_dynamic_control_setting_editors(DynamicSceneControlsState& controls, const std::span<const spectra::scene_runtime::DynamicSceneControlSetting> settings) {
        std::vector<DynamicSceneControlSettingEditor> next{};
        next.reserve(settings.size());
        for (const spectra::scene_runtime::DynamicSceneControlSetting& setting : settings) {
            const auto existing = std::ranges::find_if(controls.setting_editors, [&setting](const DynamicSceneControlSettingEditor& editor) { return editor.setting.key == setting.key; });
            if (existing == controls.setting_editors.end()) {
                next.push_back(make_control_setting_editor(setting));
                continue;
            }
            DynamicSceneControlSettingEditor editor = std::move(*existing);
            editor.setting = setting;
            editor.editor.schema = setting_as_option_schema(setting);
            if (editor.committed_value != setting.value || dynamic_scene_option_editor_value(editor.editor) != setting.value) {
                set_dynamic_scene_option_editor_value(editor.editor, setting.value);
                editor.committed_value = setting.value;
            }
            next.push_back(std::move(editor));
        }
        sort_control_setting_editors(next);
        controls.setting_editors = std::move(next);
    }

    [[nodiscard]] std::string open_dynamic_scene_controls(spectra::scene_runtime::SceneController& controller, DynamicSceneControlsState& controls, const std::filesystem::path& plugin_path) {
        spectra::scene_runtime::DynamicScenePluginInfo plugin = spectra::scene_runtime::inspect_dynamic_scene_plugin(plugin_path);
        const std::string title = plugin.title;
        controller.activate_empty_workspace();
        begin_dynamic_scene_controls(controls, std::move(plugin));
        return title;
    }

    [[nodiscard]] bool handle_scene_file_drop(spectra::Spectra& application, spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, const std::span<const std::filesystem::path> paths) {
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
            if (spectra::scene_runtime::is_dynamic_scene_plugin_file(scene_path)) {
                application.clear_imgui_rgba8_images("scene-control-image://");
                const std::string title = open_dynamic_scene_controls(controller, controls, scene_path);
                set_scene_status(state, std::format("Opened dynamic scene controls {}", title), false);
                application.open_command_popover("scene.dynamic-controls");
                return true;
            }
            const std::string title = activate_scene_path(controller, scene_path);
            application.clear_imgui_rgba8_images("scene-control-image://");
            clear_dynamic_scene_controls(controls);
            application.close_command_popover("scene.dynamic-controls");
            set_scene_status(state, std::format("Loaded {}", title), false);
        } catch (const std::exception& error) {
            set_scene_status(state, error.what(), true);
        }
        return true;
    }

    bool draw_dynamic_scene_option_editor(DynamicSceneOptionEditor& editor) {
        bool changed{};
        ImGui::PushID(editor.schema.key.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(editor.schema.label.c_str());
        if (!editor.schema.required && editor.schema.default_value.empty()) {
            ImGui::SameLine();
            changed = ImGui::Checkbox("Set", &editor.enabled) || changed;
        }
        if (!editor.schema.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", editor.schema.description.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::BeginDisabled(!editor.enabled);
        switch (editor.schema.kind) {
            case spectra::scene_runtime::DynamicSceneOptionKind::Choice: {
                const char* preview = editor.text_value.empty() ? "Select..." : editor.text_value.c_str();
                if (ImGui::BeginCombo("##value", preview)) {
                    for (const spectra::scene_runtime::DynamicSceneOptionChoice& choice : editor.schema.choices) {
                        const bool selected = editor.text_value == choice.value;
                        if (ImGui::Selectable(choice.label.c_str(), selected)) {
                            editor.text_value = choice.value;
                            changed = true;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                break;
            }
            case spectra::scene_runtime::DynamicSceneOptionKind::Bool:
                changed = ImGui::Checkbox("##value", &editor.bool_value) || changed;
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::Float:
                changed = ImGui::InputFloat("##value", &editor.float_value, 0.0f, 0.0f, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            case spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger:
                changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &editor.unsigned_value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            default:
                changed = input_text("##value", editor.text_buffer) || changed;
                break;
        }
        ImGui::EndDisabled();
        ImGui::PopID();
        return changed;
    }

    [[nodiscard]] bool dynamic_scene_option_editor_should_commit(const DynamicSceneOptionEditor& editor, const bool changed) {
        if (!changed) return false;
        if (editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Bool) return true;
        if (editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Choice) return true;
        if (editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::Float || editor.schema.kind == spectra::scene_runtime::DynamicSceneOptionKind::UnsignedInteger) {
            if (ImGui::IsItemDeactivatedAfterEdit()) return true;
            return ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        }
        return true;
    }

    void draw_dynamic_scene_option_editors_grouped(const std::span<DynamicSceneOptionEditor> editors, const bool advanced) {
        const std::vector<std::string> groups = dynamic_scene_option_editor_groups(editors, advanced);
        for (const std::string& group : groups) {
            ImGui::TextDisabled("%s", group.c_str());
            for (DynamicSceneOptionEditor& editor : editors) {
                if (editor.schema.advanced != advanced) continue;
                if (dynamic_scene_option_editor_group_label(editor) != group) continue;
                static_cast<void>(draw_dynamic_scene_option_editor(editor));
                ImGui::Spacing();
            }
        }
    }

    [[nodiscard]] const char* dynamic_scene_controls_phase_text(const DynamicSceneControlsPhase phase) {
        switch (phase) {
        case DynamicSceneControlsPhase::None: return "No Dynamic Scene";
        case DynamicSceneControlsPhase::PluginLoaded: return "Plugin Loaded";
        case DynamicSceneControlsPhase::Active: return "Active";
        case DynamicSceneControlsPhase::Error: return "Error";
        }
        throw std::runtime_error("Unknown dynamic scene controls phase");
    }

    [[nodiscard]] bool control_action_enabled(const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::string& action_id) {
        return std::ranges::any_of(status.enabled_action_ids, [&action_id](const std::string& enabled_action_id) { return enabled_action_id == action_id; });
    }

    [[nodiscard]] std::string_view control_action_disabled_reason(const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::string& action_id) {
        const auto reason = std::ranges::find_if(status.disabled_actions, [&action_id](const spectra::scene_runtime::DynamicSceneControlDisabledAction& disabled_action) { return disabled_action.action_id == action_id; });
        if (reason == status.disabled_actions.end()) return "Action disabled by control status";
        return reason->reason;
    }

    [[nodiscard]] ImVec4 control_log_level_color(const std::string& level) {
        if (level == "ERROR") return ImVec4{1.0f, 0.42f, 0.36f, 1.0f};
        if (level == "WARN") return ImVec4{1.0f, 0.78f, 0.32f, 1.0f};
        return ImVec4{0.68f, 0.73f, 0.80f, 1.0f};
    }

    [[nodiscard]] ImVec4 control_metric_color(const spectra::scene_runtime::DynamicSceneControlMetric& metric) {
        if (!metric.has_color) return ImVec4{0.72f, 0.79f, 0.86f, 1.0f};
        return ImVec4{metric.color[0], metric.color[1], metric.color[2], metric.color[3]};
    }

    void draw_dynamic_control_metric_row(const spectra::scene_runtime::DynamicSceneControlMetric& metric) {
        ImGui::TextDisabled("%s", metric.label.c_str());
        ImGui::SameLine();
        ImGui::TextColored(control_metric_color(metric), "%s", metric.value.c_str());
    }

    void draw_dynamic_control_metrics(const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::uint32_t placement, const char* empty_text) {
        const std::vector<const spectra::scene_runtime::DynamicSceneControlMetric*> metrics = control_metrics_with_placement(status, placement);
        if (metrics.empty()) {
            ImGui::TextDisabled("%s", empty_text);
            return;
        }
        for (const spectra::scene_runtime::DynamicSceneControlMetric* metric : metrics) draw_dynamic_control_metric_row(*metric);
    }

    void draw_dynamic_control_summary(const spectra::scene_runtime::DynamicSceneControlStatus& status) {
        ImGui::TextColored(ImVec4{0.50f, 0.82f, 0.76f, 1.0f}, "%s", status.phase.c_str());
        if (!status.headline.empty()) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s", status.headline.c_str());
        }
        if (!status.detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", status.detail.c_str());
        const std::vector<const spectra::scene_runtime::DynamicSceneControlMetric*> metrics = control_metrics_with_placement(status, spectra::scene_runtime::DynamicSceneControlPlacementPanelSummary);
        if (metrics.empty()) return;
        ImGui::Spacing();
        if (ImGui::BeginTable("DynamicSceneControlSummaryMetrics", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const spectra::scene_runtime::DynamicSceneControlMetric* metric : metrics) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", metric->label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(control_metric_color(*metric), "%s", metric->value.c_str());
            }
            ImGui::EndTable();
        }
    }

    void draw_dynamic_control_logs(const std::span<const spectra::scene_runtime::DynamicSceneControlLogEntry> logs) {
        ImGui::TextDisabled("%s", "Log");
        ImGui::BeginChild("DynamicSceneControlsLogs", ImVec2{-1.0f, 180.0f}, true);
        if (logs.empty()) {
            ImGui::TextDisabled("%s", "No log entries");
        } else {
            for (const spectra::scene_runtime::DynamicSceneControlLogEntry& entry : logs) {
                const std::string line = std::format("{:>5} {:<9} {}", entry.sequence, entry.level, entry.message);
                ImGui::TextColored(control_log_level_color(entry.level), "%s", line.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }

    [[nodiscard]] std::string control_image_cache_key(const DynamicSceneControlsState& controls, const spectra::scene_runtime::DynamicSceneControlImage& image) {
        const std::string& controls_id = controls.active_id.empty() ? controls.plugin.id : controls.active_id;
        return std::format("scene-control-image://{}/{}", controls_id, image.id);
    }

    void draw_dynamic_control_images(spectra::Spectra& application, const DynamicSceneControlsState& controls, const std::span<const spectra::scene_runtime::DynamicSceneControlImage> images) {
        ImGui::TextDisabled("%s", "Preview");
        if (images.empty()) {
            ImGui::TextDisabled("%s", "No preview images");
            return;
        }
        for (const spectra::scene_runtime::DynamicSceneControlImage& image : images) {
            ImGui::PushID(image.id.c_str());
            ImGui::TextUnformatted(image.label.c_str());
            if (!image.description.empty()) ImGui::TextWrapped("%s", image.description.c_str());
            ImGui::TextDisabled("%u x %u | rev %llu", image.width, image.height, static_cast<unsigned long long>(image.revision));
            const float available_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
            const float display_height = available_width * static_cast<float>(image.height) / static_cast<float>(image.width);
            try {
                application.draw_imgui_rgba8_image(
                    control_image_cache_key(controls, image),
                    spectra::Rgba8ImageSource{
                        .data = image.rgba8,
                        .byte_size = image.rgba8_size,
                        .width = image.width,
                        .height = image.height,
                        .revision = image.revision,
                    },
                    ImVec2{available_width, display_height});
            } catch (const std::exception& error) {
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", error.what());
            }
            ImGui::Spacing();
            ImGui::PopID();
        }
    }

    void draw_dynamic_control_scalar_series(const std::span<const spectra::scene_runtime::DynamicSceneControlScalarSeries> series, const std::uint32_t group) {
        const std::vector<const spectra::scene_runtime::DynamicSceneControlScalarSeries*> selected_series = scalar_series_for_group(series, group);
        if (selected_series.empty()) {
            ImGui::TextDisabled("%s", "No chart series");
            return;
        }
        for (const spectra::scene_runtime::DynamicSceneControlScalarSeries* chart : selected_series) {
            ImGui::PushID(chart->id.c_str());
            ImGui::TextUnformatted(chart->label.c_str());
            if (!chart->description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", chart->description.c_str());
            if (chart->samples.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", "No chart samples");
                ImGui::Spacing();
                ImGui::PopID();
                continue;
            }
            const spectra::scene_runtime::DynamicSceneControlScalarSample& latest = chart->samples.back();
            const std::string latest_text = chart->unit.empty()
                ? std::format("step {} | {:.6g}", latest.step, latest.value)
                : std::format("step {} | {:.6g} {}", latest.step, latest.value, chart->unit);
            ImGui::SameLine();
            ImGui::TextDisabled("%s | %zu samples | rev %llu", latest_text.c_str(), chart->samples.size(), static_cast<unsigned long long>(chart->revision));
            if (chart->samples.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", "Too many samples for ImGui plot");
                ImGui::Spacing();
                ImGui::PopID();
                continue;
            }
            std::vector<float> values{};
            values.reserve(chart->samples.size());
            bool values_fit_imgui = true;
            for (const spectra::scene_runtime::DynamicSceneControlScalarSample& sample : chart->samples) {
                const float value = static_cast<float>(sample.value);
                if (!std::isfinite(value)) {
                    values_fit_imgui = false;
                    break;
                }
                values.push_back(value);
            }
            if (!values_fit_imgui) {
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", "Chart values exceed ImGui plot range");
                ImGui::Spacing();
                ImGui::PopID();
                continue;
            }
            ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4{chart->color[0], chart->color[1], chart->color[2], chart->color[3]});
            ImGui::PlotLines("##plot", values.data(), static_cast<int>(values.size()), 0, nullptr, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), ImVec2{std::max(1.0f, ImGui::GetContentRegionAvail().x), 54.0f});
            ImGui::PopStyleColor();
            ImGui::Spacing();
            ImGui::PopID();
        }
    }

    [[nodiscard]] int push_dynamic_control_action_button_style(const std::uint32_t style) {
        if (style == spectra::scene_runtime::DynamicSceneControlActionStylePrimary) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.10f, 0.34f, 0.38f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.13f, 0.45f, 0.50f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.10f, 0.56f, 0.60f, 1.0f});
            return 3;
        }
        if (style == spectra::scene_runtime::DynamicSceneControlActionStyleDanger) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.32f, 0.12f, 0.12f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.46f, 0.16f, 0.15f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.60f, 0.18f, 0.16f, 1.0f});
            return 3;
        }
        return 0;
    }

    bool execute_dynamic_control_action_editor(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, DynamicSceneControlActionEditor& editor) {
        try {
            const std::vector<spectra::scene_runtime::DynamicSceneOption> options = collect_dynamic_scene_options(editor.editors);
            controller.execute_active_dynamic_scene_control_action(editor.action.id, options);
            controls.phase = DynamicSceneControlsPhase::Active;
            controls.error.clear();
            set_scene_status(state, std::format("Executed {}", editor.action.label), false);
            return true;
        } catch (const std::exception& error) {
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            set_scene_status(state, controls.error, true);
            return false;
        }
    }

    bool draw_dynamic_control_action_button(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, DynamicSceneControlActionEditor& editor, const spectra::scene_runtime::DynamicSceneControlStatus& status, const ImVec2 size) {
        ImGui::PushID(editor.action.id.c_str());
        const bool enabled = control_action_enabled(status, editor.action.id);
        const std::string_view disabled_reason = control_action_disabled_reason(status, editor.action.id);
        const int style_color_count = push_dynamic_control_action_button_style(editor.action.style);
        ImGui::BeginDisabled(!enabled);
        const bool clicked = ImGui::Button(editor.action.label.c_str(), size);
        ImGui::EndDisabled();
        if (style_color_count != 0) ImGui::PopStyleColor(style_color_count);
        if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%.*s", static_cast<int>(disabled_reason.size()), disabled_reason.data());
        if (enabled && !editor.action.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", editor.action.description.c_str());
        if (clicked) static_cast<void>(execute_dynamic_control_action_editor(controller, state, controls, editor));
        ImGui::PopID();
        return clicked;
    }

    void draw_dynamic_control_action_editor(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, DynamicSceneControlActionEditor& editor, const spectra::scene_runtime::DynamicSceneControlStatus& status) {
        if (editor.editors.empty()) {
            static_cast<void>(draw_dynamic_control_action_button(controller, state, controls, editor, status, ImVec2{-1.0f, 0.0f}));
            return;
        }
        ImGui::PushID(editor.action.id.c_str());
        const bool enabled = control_action_enabled(status, editor.action.id);
        const std::string_view disabled_reason = control_action_disabled_reason(status, editor.action.id);
        ImGui::TextUnformatted(editor.action.label.c_str());
        if (!editor.action.description.empty()) ImGui::TextWrapped("%s", editor.action.description.c_str());
        draw_dynamic_scene_option_editors_grouped(editor.editors, false);
        if (dynamic_scene_option_editor_has_advanced_state(editor.editors, true) && ImGui::CollapsingHeader("Advanced Options")) draw_dynamic_scene_option_editors_grouped(editor.editors, true);
        ImGui::BeginDisabled(!enabled);
        const std::string button_label = std::format("{}##execute", editor.action.label);
        const int style_color_count = push_dynamic_control_action_button_style(editor.action.style);
        const bool clicked = ImGui::Button(button_label.c_str(), ImVec2{-1.0f, 0.0f});
        if (style_color_count != 0) ImGui::PopStyleColor(style_color_count);
        ImGui::EndDisabled();
        if (!enabled) {
            ImGui::TextDisabled("%.*s", static_cast<int>(disabled_reason.size()), disabled_reason.data());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%.*s", static_cast<int>(disabled_reason.size()), disabled_reason.data());
        }
        if (clicked) static_cast<void>(execute_dynamic_control_action_editor(controller, state, controls, editor));
        ImGui::PopID();
    }

    void draw_dynamic_control_action_button_row(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::vector<DynamicSceneControlActionEditor*>& editors) {
        if (editors.empty()) return;
        constexpr std::size_t max_columns = 3u;
        const std::size_t columns = std::min(max_columns, editors.size());
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float available_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float button_width = std::max(74.0f, (available_width - spacing * static_cast<float>(columns - 1u)) / static_cast<float>(columns));
        for (std::size_t index = 0u; index < editors.size(); ++index) {
            if (index % columns != 0u) ImGui::SameLine(0.0f, spacing);
            static_cast<void>(draw_dynamic_control_action_button(controller, state, controls, *editors[index], status, ImVec2{button_width, 0.0f}));
        }
    }

    void draw_dynamic_control_action_group(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, const spectra::scene_runtime::DynamicSceneControlStatus& status, const std::uint32_t group, const bool include_immediate_actions = true) {
        const std::vector<DynamicSceneControlActionEditor*> editors = control_action_editors_for_group(controls.action_editors, group);
        if (editors.empty()) {
            ImGui::TextDisabled("%s", "No actions");
            return;
        }
        std::vector<DynamicSceneControlActionEditor*> immediate_editors{};
        std::vector<DynamicSceneControlActionEditor*> form_editors{};
        for (DynamicSceneControlActionEditor* editor : editors) {
            if (editor->editors.empty()) immediate_editors.push_back(editor);
            else form_editors.push_back(editor);
        }
        if (include_immediate_actions && !immediate_editors.empty()) {
            draw_dynamic_control_action_button_row(controller, state, controls, status, immediate_editors);
            if (!form_editors.empty()) ImGui::Spacing();
        }
        for (DynamicSceneControlActionEditor* editor : form_editors) draw_dynamic_control_action_editor(controller, state, controls, *editor, status);
        if (form_editors.empty() && include_immediate_actions && immediate_editors.empty()) ImGui::TextDisabled("%s", "No actions");
    }

    void draw_dynamic_control_setting_editor(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, DynamicSceneControlSettingEditor& editor) {
        const bool changed = draw_dynamic_scene_option_editor(editor.editor);
        if (!dynamic_scene_option_editor_should_commit(editor.editor, changed)) return;
        const std::string value = dynamic_scene_option_editor_value(editor.editor);
        try {
            controller.update_active_dynamic_scene_control_setting(editor.setting.key, value);
            editor.committed_value = value;
            editor.setting.value = value;
            controls.phase = DynamicSceneControlsPhase::Active;
            controls.error.clear();
            set_scene_status(state, std::format("Updated {}", editor.setting.label), false);
        } catch (const std::exception& error) {
            set_dynamic_scene_option_editor_value(editor.editor, editor.committed_value);
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            set_scene_status(state, controls.error, true);
        }
    }

    void draw_dynamic_control_settings(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, const bool advanced) {
        const std::vector<std::string> groups = control_setting_groups(controls.setting_editors, advanced);
        if (groups.empty()) {
            if (!advanced) ImGui::TextDisabled("%s", "No live settings");
            return;
        }
        for (const std::string& group : groups) {
            ImGui::TextDisabled("%s", group.c_str());
            for (DynamicSceneControlSettingEditor& editor : controls.setting_editors) {
                if (editor.setting.advanced != advanced) continue;
                if (control_setting_group_label(editor) != group) continue;
                draw_dynamic_control_setting_editor(controller, state, controls, editor);
                ImGui::Spacing();
            }
        }
    }

    [[nodiscard]] std::vector<DynamicSceneControlActionEditor*> dashboard_quick_actions(DynamicSceneControlsState& controls) {
        std::vector<DynamicSceneControlActionEditor*> candidates{};
        for (DynamicSceneControlActionEditor& editor : controls.action_editors) {
            if (editor.action.group != spectra::scene_runtime::DynamicSceneControlActionGroupRun) continue;
            if (!editor.editors.empty()) continue;
            candidates.push_back(&editor);
        }
        std::ranges::stable_sort(candidates, [](const DynamicSceneControlActionEditor* left, const DynamicSceneControlActionEditor* right) {
            if (left->action.priority != right->action.priority) return left->action.priority < right->action.priority;
            return left->action.id < right->action.id;
        });
        return candidates;
    }

    bool draw_dynamic_scene_open_controls(spectra::Spectra& application, spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls) {
        if (!controls.plugin.open_action_description.empty()) ImGui::TextWrapped("%s", controls.plugin.open_action_description.c_str());
        if (dynamic_scene_option_editor_has_advanced_state(controls.editors, false)) draw_dynamic_scene_option_editors_grouped(controls.editors, false);
        if (dynamic_scene_option_editor_has_advanced_state(controls.editors, true) && ImGui::CollapsingHeader("Advanced Options")) draw_dynamic_scene_option_editors_grouped(controls.editors, true);
        if (!ImGui::Button(controls.plugin.open_action_label.c_str(), ImVec2{-1.0f, 0.0f})) return false;
        try {
            application.clear_imgui_rgba8_images("scene-control-image://");
            spectra::scene_runtime::DynamicSceneOpenRequest request{
                .plugin_path = controls.plugin.path,
                .options = collect_dynamic_scene_options(controls.editors),
            };
            const DynamicSceneActivationResult result = activate_dynamic_scene_plugin(controller, std::move(request));
            controls.phase = DynamicSceneControlsPhase::Active;
            controls.active_id = result.id;
            controls.active_title = result.title;
            controls.error.clear();
            set_scene_status(state, std::format("Loaded {}", result.title), false);
            return true;
        } catch (const std::exception& error) {
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            controls.active_title.clear();
            controls.active_id.clear();
            controller.activate_empty_workspace();
            set_scene_status(state, controls.error, true);
            return false;
        }
    }

    void draw_dynamic_scene_controls_dashboard(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls, const spectra::scene_runtime::DynamicSceneControlStatus& status) {
        draw_dynamic_control_summary(status);
        const std::vector<DynamicSceneControlActionEditor*> quick_actions = dashboard_quick_actions(controls);
        if (quick_actions.empty()) return;
        ImGui::Spacing();
        draw_dynamic_control_action_button_row(controller, state, controls, status, quick_actions);
    }

    void draw_dynamic_scene_controls_panel(spectra::Spectra& application, spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state, DynamicSceneControlsState& controls) {
        if (!dynamic_scene_controls_loaded(controls)) {
            ImGui::TextDisabled("%s", "No dynamic scene controls");
            return;
        }

        std::optional<spectra::scene_runtime::DynamicSceneControlSnapshot> active_snapshot{};
        if (controls.phase == DynamicSceneControlsPhase::Active || controls.phase == DynamicSceneControlsPhase::Error) {
            try {
                if (controller.has_active_dynamic_scene_controls()) {
                    active_snapshot = controller.active_dynamic_scene_control_snapshot();
                    sync_dynamic_control_setting_editors(controls, active_snapshot->settings);
                }
            } catch (const std::exception& error) {
                controls.phase = DynamicSceneControlsPhase::Error;
                controls.error = error.what();
            }
        }

        ImGui::TextUnformatted(controls.plugin.controls_panel_title.c_str());
        ImGui::TextColored(ImVec4{0.55f, 0.62f, 0.70f, 1.0f}, "%s", controls.plugin.title.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", controls.plugin.path.string().c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", dynamic_scene_controls_phase_text(controls.phase));
        if (!controls.active_title.empty()) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s", controls.active_title.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!active_snapshot.has_value()) {
            static_cast<void>(draw_dynamic_scene_open_controls(application, controller, state, controls));
            if (!controls.error.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controls.error.c_str());
            }
            ImGui::Spacing();
            if (ImGui::Button("Close Scene", ImVec2{-1.0f, 0.0f})) {
                application.clear_imgui_rgba8_images("scene-control-image://");
                clear_dynamic_scene_controls(controls);
                controller.activate_empty_workspace();
                set_scene_status(state, "Closed dynamic scene controls", false);
            }
            return;
        }

        const spectra::scene_runtime::DynamicSceneControlSnapshot& snapshot = *active_snapshot;
        draw_dynamic_scene_controls_dashboard(controller, state, controls, snapshot.status);
        ImGui::Spacing();
        if (ImGui::BeginTabBar("DynamicSceneControlsTabs")) {
            if (ImGui::BeginTabItem("Run")) {
                draw_dynamic_control_action_group(controller, state, controls, snapshot.status, spectra::scene_runtime::DynamicSceneControlActionGroupRun, false);
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Charts", ImGuiTreeNodeFlags_DefaultOpen)) draw_dynamic_control_scalar_series(snapshot.scalar_series, spectra::scene_runtime::DynamicSceneControlActionGroupRun);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Preview")) {
                draw_dynamic_control_action_group(controller, state, controls, snapshot.status, spectra::scene_runtime::DynamicSceneControlActionGroupPreview);
                ImGui::Spacing();
                draw_dynamic_control_images(application, controls, snapshot.images);
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Charts")) draw_dynamic_control_scalar_series(snapshot.scalar_series, spectra::scene_runtime::DynamicSceneControlActionGroupPreview);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                draw_dynamic_control_settings(controller, state, controls, false);
                if (std::ranges::any_of(controls.setting_editors, [](const DynamicSceneControlSettingEditor& editor) { return editor.setting.advanced; }) && ImGui::CollapsingHeader("Advanced Settings")) draw_dynamic_control_settings(controller, state, controls, true);
                const bool has_debug_actions = !control_action_editors_for_group(controls.action_editors, spectra::scene_runtime::DynamicSceneControlActionGroupDebug).empty();
                if (has_debug_actions) {
                    ImGui::Spacing();
                    draw_dynamic_control_action_group(controller, state, controls, snapshot.status, spectra::scene_runtime::DynamicSceneControlActionGroupDebug);
                }
                if (!control_action_editors_for_group(controls.action_editors, spectra::scene_runtime::DynamicSceneControlActionGroupUtility).empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", "Utility");
                    draw_dynamic_control_action_group(controller, state, controls, snapshot.status, spectra::scene_runtime::DynamicSceneControlActionGroupUtility);
                }
                ImGui::Spacing();
                if (ImGui::CollapsingHeader("Details")) draw_dynamic_control_metrics(snapshot.status, spectra::scene_runtime::DynamicSceneControlPlacementPanelDetail, "No detail metrics");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log")) {
                draw_dynamic_control_logs(snapshot.logs);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (!controls.error.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controls.error.c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Close Scene", ImVec2{-1.0f, 0.0f})) {
            application.clear_imgui_rgba8_images("scene-control-image://");
            clear_dynamic_scene_controls(controls);
            controller.activate_empty_workspace();
            set_scene_status(state, "Closed dynamic scene controls", false);
        }
    }

    [[nodiscard]] std::string dynamic_scene_overlay_text(const spectra::scene_runtime::DynamicSceneControlStatus& status) {
        std::string text = status.phase;
        if (!status.headline.empty()) text += std::format(" | {}", status.headline);
        const std::vector<const spectra::scene_runtime::DynamicSceneControlMetric*> metrics = control_metrics_with_placement(status, spectra::scene_runtime::DynamicSceneControlPlacementViewportOverlay);
        std::size_t shown_metric_count = 0u;
        for (const spectra::scene_runtime::DynamicSceneControlMetric* metric : metrics) {
            if (shown_metric_count >= 5u) break;
            text += std::format(" | {} {}", metric->label, metric->value);
            ++shown_metric_count;
        }
        constexpr std::size_t max_overlay_text_size = 220u;
        if (text.size() > max_overlay_text_size) text = text.substr(0u, max_overlay_text_size - 3u) + "...";
        return text;
    }

    void draw_dynamic_scene_controls_overlay(spectra::scene_runtime::SceneController& controller, DynamicSceneControlsState& controls, const ImVec2 viewport_position, const ImVec2 viewport_size) {
        if (!dynamic_scene_controls_loaded(controls)) return;
        if (!controller.has_active_dynamic_scene_controls()) return;
        spectra::scene_runtime::DynamicSceneControlSnapshot snapshot{};
        try {
            snapshot = controller.active_dynamic_scene_control_snapshot();
        } catch (const std::exception& error) {
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            return;
        }
        const std::string text = dynamic_scene_overlay_text(snapshot.status);
        if (text.empty() || viewport_size.x < 180.0f || viewport_size.y < 120.0f) return;
        const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
        const ImVec2 padding{9.0f, 5.0f};
        const float max_width = std::max(120.0f, viewport_size.x - 24.0f);
        const ImVec2 chip_size{std::min(max_width, text_size.x + padding.x * 2.0f), text_size.y + padding.y * 2.0f};
        const ImVec2 chip_min{viewport_position.x + 12.0f, viewport_position.y + viewport_size.y - chip_size.y - 12.0f};
        const ImVec2 chip_max{chip_min.x + chip_size.x, chip_min.y + chip_size.y};
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddRectFilled(chip_min, chip_max, ImGui::GetColorU32(ImVec4{0.03f, 0.05f, 0.07f, 0.78f}), 6.0f);
        draw_list->AddRect(chip_min, chip_max, ImGui::GetColorU32(ImVec4{0.34f, 0.68f, 0.72f, 0.45f}), 6.0f);
        draw_list->AddText(ImVec2{chip_min.x + padding.x, chip_min.y + padding.y}, ImGui::GetColorU32(ImVec4{0.82f, 0.92f, 0.94f, 1.0f}), text.c_str());
    }

    void handle_scene_timeline_shortcuts(spectra::scene_runtime::SceneController& controller) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        if (controller.active_scene_timeline_streaming_enabled() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) controller.toggle_active_scene_timeline_playback();
        if (controller.active_scene_timeline_enabled() && ImGui::IsKeyPressed(ImGuiKey_R, false)) controller.request_active_scene_timeline_reset();
    }

    [[nodiscard]] std::string scene_workspace_tooltip(const spectra::scene_runtime::SceneEntry* selected_entry, const bool pending_switch) {
        if (selected_entry == nullptr) return "Empty Scene\nDrop a PBRT scene or dynamic scene plugin into the window to load it";
        return std::format(
            "{}\n{}{}\nDrop a PBRT scene or dynamic scene plugin into the window to replace it",
            selected_entry->id,
            selected_entry->kind == spectra::scene_runtime::SceneEntryKind::Static ? "Static" : "Dynamic",
            pending_switch ? "\nSwitching on next frame" : "");
    }

    [[nodiscard]] spectra::WorkspaceTitle make_scene_workspace_title(spectra::scene_runtime::SceneController& controller, SceneWorkspaceStatusState& state) {
        const std::optional<std::size_t> selected_index = controller.has_selected_entry() ? std::optional<std::size_t>{controller.selected_index()} : std::nullopt;
        const spectra::scene_runtime::SceneEntry* selected_entry = selected_index.has_value() ? &controller.entry(*selected_index) : nullptr;
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
        PathtracerRendererAdapter(std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)) {
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
            handle_scene_timeline_shortcuts(*this->scene_controller);
            this->scene_controller->update_active_scene(frame.delta_seconds);
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

        std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::pathtracer::Renderer> renderer{};
    };

    class RasterizerRendererAdapter final {
    public:
        RasterizerRendererAdapter(std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace, std::shared_ptr<spectra::scene_runtime::DynamicSceneHostServiceRouter> dynamic_host_services) : scene_controller(std::move(scene_controller)), camera_workspace(std::move(camera_workspace)), dynamic_host_services(std::move(dynamic_host_services)) {
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
            handle_scene_timeline_shortcuts(*this->scene_controller);
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

        std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller{};
        std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace{};
        std::shared_ptr<spectra::scene_runtime::DynamicSceneHostServiceRouter> dynamic_host_services{};
        std::shared_ptr<spectra::scene::Scene> active_workspace{};
        std::unique_ptr<spectra::rasterizer::Renderer> renderer{};
    };

    static_assert(spectra::RendererFor<PathtracerRendererAdapter, spectra::Spectra>);
    static_assert(spectra::RendererFor<RasterizerRendererAdapter, spectra::Spectra>);

    [[nodiscard]] std::shared_ptr<spectra::scene::Scene> make_empty_scene_workspace() {
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

    [[nodiscard]] std::optional<spectra::scene_runtime::DynamicScenePluginInfo> load_cli_scene(spectra::scene_runtime::SceneController& controller, const std::string& scene_id) {
        if (const std::size_t query_begin = scene_id.find('?'); query_begin != std::string::npos) {
            const std::string plugin_path_text = scene_id.substr(0u, query_begin);
            if (!plugin_path_text.empty() && spectra::scene_runtime::is_dynamic_scene_plugin_file(std::filesystem::path{plugin_path_text})) throw std::runtime_error("Dynamic scene plugin Scene URI query is not supported; pass the plugin path without query and configure it in the Scene popover");
        }
        const std::filesystem::path requested_path{scene_id};
        if ((is_pbrt_scene_file(requested_path) || spectra::scene_runtime::is_dynamic_scene_plugin_file(requested_path)) && !std::filesystem::is_regular_file(requested_path)) throw std::runtime_error(std::format("{}: initial scene file does not exist", requested_path.string()));
        if (spectra::scene_runtime::is_dynamic_scene_plugin_file(requested_path)) {
            controller.activate_empty_workspace();
            return spectra::scene_runtime::inspect_dynamic_scene_plugin(requested_path);
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

    void register_renderers(spectra::Spectra& application, std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller, std::shared_ptr<spectra::scene::CameraWorkspace> camera_workspace, std::shared_ptr<spectra::scene_runtime::DynamicSceneHostServiceRouter> dynamic_host_services, std::optional<spectra::scene_runtime::DynamicScenePluginInfo> initial_dynamic_scene_plugin) {
        if (scene_controller == nullptr) throw std::runtime_error("Renderer registration requires a scene controller");
        if (camera_workspace == nullptr) throw std::runtime_error("Renderer registration requires a scene camera workspace");
        if (dynamic_host_services == nullptr) throw std::runtime_error("Renderer registration requires dynamic scene host services");
        std::shared_ptr<SceneWorkspaceStatusState> scene_status_state = std::make_shared<SceneWorkspaceStatusState>();
        std::shared_ptr<DynamicSceneControlsState> dynamic_scene_controls = std::make_shared<DynamicSceneControlsState>();
        if (initial_dynamic_scene_plugin.has_value()) {
            const std::string title = initial_dynamic_scene_plugin->title;
            begin_dynamic_scene_controls(*dynamic_scene_controls, std::move(*initial_dynamic_scene_plugin));
            set_scene_status(*scene_status_state, std::format("Opened dynamic scene controls {}", title), false);
        }
        application.register_renderer(RasterizerRendererAdapter{scene_controller, camera_workspace, std::move(dynamic_host_services)});
        application.register_renderer(PathtracerRendererAdapter{scene_controller, std::move(camera_workspace)});
        std::shared_ptr<spectra::scene_runtime::SceneController> drop_scene_controller = scene_controller;
        std::shared_ptr<SceneWorkspaceStatusState> drop_scene_status_state = scene_status_state;
        std::shared_ptr<DynamicSceneControlsState> drop_dynamic_scene_controls = dynamic_scene_controls;
        spectra::Spectra* drop_application = &application;
        std::shared_ptr<spectra::scene_runtime::SceneController> controls_scene_controller = scene_controller;
        std::shared_ptr<SceneWorkspaceStatusState> controls_scene_status_state = scene_status_state;
        std::shared_ptr<DynamicSceneControlsState> panel_dynamic_scene_controls = dynamic_scene_controls;
        spectra::Spectra* controls_application = &application;
        std::shared_ptr<spectra::scene_runtime::SceneController> overlay_scene_controller = scene_controller;
        std::shared_ptr<DynamicSceneControlsState> overlay_dynamic_scene_controls = dynamic_scene_controls;
        application.set_workspace_title_provider([scene_controller = std::move(scene_controller), scene_status_state = std::move(scene_status_state)] { return make_scene_workspace_title(*scene_controller, *scene_status_state); });
        application.register_file_drop_handler(spectra::FileDropHandler{
            .id             = "scene.file-drop",
            .title          = "Scene File Drop",
            .owner_renderer = {},
            .handle         = [application = drop_application, scene_controller = std::move(drop_scene_controller), scene_status_state = std::move(drop_scene_status_state), dynamic_scene_controls = std::move(drop_dynamic_scene_controls)](const std::span<const std::filesystem::path> paths) { return handle_scene_file_drop(*application, *scene_controller, *scene_status_state, *dynamic_scene_controls, paths); },
        });
        application.register_command_popover(spectra::CommandPopover{
            .id             = "scene.dynamic-controls",
            .title          = "Scene",
            .icon           = ICON_MS_DATASET,
            .owner_renderer = {},
            .shortcut_label = "F9",
            .shortcut_key   = ImGuiKey_F9,
            .draw           = [application = controls_application, scene_controller = std::move(controls_scene_controller), scene_status_state = std::move(controls_scene_status_state), dynamic_scene_controls = std::move(panel_dynamic_scene_controls)] { draw_dynamic_scene_controls_panel(*application, *scene_controller, *scene_status_state, *dynamic_scene_controls); },
        });
        application.register_viewport_overlay(spectra::ViewportOverlay{
            .id             = "scene.dynamic-controls-overlay",
            .title          = "Dynamic Scene Controls Overlay",
            .owner_renderer = {},
            .priority       = 0,
            .draw           = [scene_controller = std::move(overlay_scene_controller), dynamic_scene_controls = std::move(overlay_dynamic_scene_controls)](const ImVec2 viewport_position, const ImVec2 viewport_size) { draw_dynamic_scene_controls_overlay(*scene_controller, *dynamic_scene_controls, viewport_position, viewport_size); },
        });
        if (initial_dynamic_scene_plugin.has_value()) application.open_command_popover("scene.dynamic-controls");
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

        spectra::scene_runtime::SceneRegistry scene_registry{};
        std::shared_ptr<spectra::scene_runtime::DynamicSceneHostServiceRouter> dynamic_host_services = std::make_shared<spectra::scene_runtime::DynamicSceneHostServiceRouter>();
        std::shared_ptr<spectra::scene_runtime::SceneController> scene_controller = std::make_shared<spectra::scene_runtime::SceneController>(std::move(scene_registry), make_empty_scene_workspace(), dynamic_host_services);
        std::optional<spectra::scene_runtime::DynamicScenePluginInfo> initial_dynamic_scene_plugin{};
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
