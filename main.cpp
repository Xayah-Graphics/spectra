#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <vulkan/vulkan_raii.hpp>

import std;
import spectra;
import spectra.pathtracer.renderer;
import spectra.rasterizer.renderer;
import spectra.scene_session;
import spectra.dynamic_scene.loader;
import spectra.scene;
import xayah.util.xcli;

namespace {
    static_assert(spectra::pathtracer::Host<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);

    struct SceneSessionStatusState {
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

    void set_scene_status(SceneSessionStatusState& state, std::string text, const bool error) {
        state.status_text = std::move(text);
        state.status_error = error;
        state.status_expires = std::chrono::steady_clock::now() + std::chrono::seconds{4};
    }

    struct OptionEditor {
        spectra::dynamic_scene::OptionSchema schema{};
        std::string text_value{};
        std::vector<char> text_buffer{};
        bool bool_value{};
        float float_value{};
        std::uint64_t unsigned_value{};
        bool enabled{true};
    };

    struct ControlActionEditor {
        spectra::dynamic_scene::ControlAction action{};
        std::vector<OptionEditor> editors{};
    };

    struct DynamicSceneControlSettingEditor {
        spectra::dynamic_scene::OptionSchema schema{};
        OptionEditor editor{};
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
        spectra::dynamic_scene::PluginInfo plugin{};
        std::vector<OptionEditor> editors{};
        std::vector<ControlActionEditor> action_editors{};
        std::vector<DynamicSceneControlSettingEditor> setting_editors{};
        std::string error{};
        std::string active_title{};
        std::string active_id{};
    };

    int resize_input_text_callback(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
        auto value = static_cast<std::vector<char>*>(data->UserData);
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

    [[nodiscard]] bool choice_contains_value(const spectra::dynamic_scene::OptionSchema& schema, const std::string& value) {
        return std::ranges::any_of(schema.choices, [&value](const spectra::dynamic_scene::OptionChoice& choice) { return choice.value == value; });
    }

    [[nodiscard]] OptionEditor make_dynamic_scene_option_editor(spectra::dynamic_scene::OptionSchema schema) {
        OptionEditor editor{.schema = std::move(schema)};
        editor.enabled = editor.schema.required || !editor.schema.default_value.empty();
        switch (editor.schema.kind) {
            case spectra::dynamic_scene::OptionKind::Bool:
                editor.bool_value = editor.schema.default_value.empty() ? false : parse_bool_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::dynamic_scene::OptionKind::Float:
                editor.float_value = editor.schema.default_value.empty() ? 0.0f : parse_float_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            case spectra::dynamic_scene::OptionKind::UnsignedInteger:
                editor.unsigned_value = editor.schema.default_value.empty() ? 0u : parse_unsigned_integer_text(editor.schema.default_value, std::format("Dynamic scene open option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = editor.schema.default_value;
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
        }
        return editor;
    }

    [[nodiscard]] ControlActionEditor make_control_action_editor(spectra::dynamic_scene::ControlAction action) {
        ControlActionEditor editor{.action = std::move(action)};
        editor.editors.reserve(editor.action.options.size());
        for (const spectra::dynamic_scene::OptionSchema& schema : editor.action.options) editor.editors.push_back(make_dynamic_scene_option_editor(schema));
        return editor;
    }

    [[nodiscard]] DynamicSceneControlSettingEditor make_control_setting_editor(spectra::dynamic_scene::OptionSchema schema, std::string value) {
        schema.default_value = value;
        schema.required = true;
        DynamicSceneControlSettingEditor editor{};
        editor.schema = schema;
        editor.editor = make_dynamic_scene_option_editor(std::move(schema));
        editor.committed_value = std::move(value);
        return editor;
    }

    [[nodiscard]] std::string dynamic_scene_option_editor_value(const OptionEditor& editor) {
        switch (editor.schema.kind) {
            case spectra::dynamic_scene::OptionKind::Bool:
                return editor.bool_value ? "true" : "false";
            case spectra::dynamic_scene::OptionKind::Float:
                return std::format("{:.9g}", editor.float_value);
            case spectra::dynamic_scene::OptionKind::UnsignedInteger:
                return std::format("{}", editor.unsigned_value);
            case spectra::dynamic_scene::OptionKind::Choice:
                return editor.text_value;
            default:
                return text_buffer_value(editor.text_buffer);
        }
    }

    void set_dynamic_scene_option_editor_value(OptionEditor& editor, const std::string_view value) {
        switch (editor.schema.kind) {
            case spectra::dynamic_scene::OptionKind::Bool:
                editor.bool_value = parse_bool_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            case spectra::dynamic_scene::OptionKind::Float:
                editor.float_value = parse_float_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            case spectra::dynamic_scene::OptionKind::UnsignedInteger:
                editor.unsigned_value = parse_unsigned_integer_text(value, std::format("Dynamic scene option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = std::string{value};
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
        }
    }

    [[nodiscard]] std::vector<spectra::dynamic_scene::Option> collect_dynamic_scene_options(const std::span<const OptionEditor> editors) {
        std::vector<spectra::dynamic_scene::Option> options{};
        options.reserve(editors.size());
        for (const OptionEditor& editor : editors) {
            if (!editor.schema.required && !editor.enabled) continue;
            std::string value = dynamic_scene_option_editor_value(editor);
            if (editor.schema.required && value.empty()) throw std::runtime_error(std::format("{} is required", editor.schema.label));
            if (editor.schema.kind == spectra::dynamic_scene::OptionKind::Choice && !value.empty() && !choice_contains_value(editor.schema, value)) throw std::runtime_error(std::format("{} must be one of the declared choices", editor.schema.label));
            if (!value.empty() || editor.schema.required || editor.schema.kind == spectra::dynamic_scene::OptionKind::Bool || editor.schema.kind == spectra::dynamic_scene::OptionKind::Float || editor.schema.kind == spectra::dynamic_scene::OptionKind::UnsignedInteger) {
                options.push_back(spectra::dynamic_scene::Option{
                    .key = editor.schema.key,
                    .value = std::move(value),
                });
            }
        }
        return options;
    }

    [[nodiscard]] bool scene_status_visible(SceneSessionStatusState& state) {
        if (state.status_text.empty()) return false;
        if (std::chrono::steady_clock::now() < state.status_expires) return true;
        state.status_text.clear();
        state.status_error = false;
        return false;
    }

    [[nodiscard]] std::string open_pbrt_scene_path(spectra::scene_session::Session& session, const std::filesystem::path& scene_path) {
        if (scene_path.empty()) throw std::runtime_error("Drop a PBRT scene file into the window to load it");
        const std::filesystem::path absolute_path = std::filesystem::absolute(scene_path).lexically_normal();
        if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a PBRT scene file, not a folder");
        if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", absolute_path.string()));
        if (!is_pbrt_scene_file(absolute_path)) throw std::runtime_error(std::format("{}: scene file must use .pbrt or .pbrt.gz", absolute_path.string()));
        const std::string title = scene_file_title(absolute_path);
        if (!session.open_static_scene(absolute_path.string(), title, [absolute_path] { return std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt_file(absolute_path)); })) throw std::runtime_error(session.activation_error().empty() ? "Failed to load static scene" : session.activation_error());
        return title;
    }

    void begin_dynamic_scene_controls(DynamicSceneControlsState& controls, spectra::dynamic_scene::PluginInfo plugin) {
        controls.plugin = std::move(plugin);
        controls.editors.clear();
        controls.editors.reserve(controls.plugin.open_options.size());
        for (spectra::dynamic_scene::OptionSchema& schema : controls.plugin.open_options) controls.editors.push_back(make_dynamic_scene_option_editor(std::move(schema)));
        std::ranges::stable_sort(controls.editors, [](const OptionEditor& left, const OptionEditor& right) {
            if (left.schema.priority != right.schema.priority) return left.schema.priority < right.schema.priority;
            if (left.schema.group != right.schema.group) return left.schema.group < right.schema.group;
            return left.schema.key < right.schema.key;
        });
        controls.action_editors.clear();
        controls.action_editors.reserve(controls.plugin.control_actions.size());
        for (const spectra::dynamic_scene::ControlAction& action : controls.plugin.control_actions) {
            controls.action_editors.push_back(make_control_action_editor(action));
            std::ranges::stable_sort(controls.action_editors.back().editors, [](const OptionEditor& left, const OptionEditor& right) {
                if (left.schema.priority != right.schema.priority) return left.schema.priority < right.schema.priority;
                if (left.schema.group != right.schema.group) return left.schema.group < right.schema.group;
                return left.schema.key < right.schema.key;
            });
        }
        std::ranges::stable_sort(controls.action_editors, [](const ControlActionEditor& left, const ControlActionEditor& right) {
            if (left.action.group != right.action.group) return left.action.group < right.action.group;
            if (left.action.priority != right.action.priority) return left.action.priority < right.action.priority;
            return left.action.id < right.action.id;
        });
        controls.setting_editors.clear();
        controls.setting_editors.reserve(controls.plugin.control_settings.size());
        for (const spectra::dynamic_scene::OptionSchema& schema : controls.plugin.control_settings) controls.setting_editors.push_back(make_control_setting_editor(schema, schema.default_value));
        std::ranges::stable_sort(controls.setting_editors, [](const DynamicSceneControlSettingEditor& left, const DynamicSceneControlSettingEditor& right) {
            if (left.schema.priority != right.schema.priority) return left.schema.priority < right.schema.priority;
            if (left.schema.group != right.schema.group) return left.schema.group < right.schema.group;
            return left.schema.key < right.schema.key;
        });
        controls.error.clear();
        controls.active_title.clear();
        controls.active_id.clear();
        controls.phase = DynamicSceneControlsPhase::PluginLoaded;
    }

    [[nodiscard]] std::string dynamic_scene_option_editor_group_label(const OptionEditor& editor) {
        return editor.schema.group.empty() ? "Options" : editor.schema.group;
    }

    [[nodiscard]] std::vector<std::string> dynamic_scene_option_editor_groups(const std::span<const OptionEditor> editors) {
        std::vector<std::string> groups{};
        for (const OptionEditor& editor : editors) {
            const std::string group = dynamic_scene_option_editor_group_label(editor);
            if (!std::ranges::contains(groups, group)) groups.push_back(group);
        }
        return groups;
    }

    [[nodiscard]] bool metric_has_placement(const spectra::dynamic_scene::ControlMetric& metric, const std::uint32_t placement) {
        return (metric.placement_flags & placement) != 0u;
    }

    [[nodiscard]] std::vector<const spectra::dynamic_scene::ControlMetric*> control_metrics_with_placement(const spectra::dynamic_scene::ControlStatus& status, const std::uint32_t placement) {
        std::vector<const spectra::dynamic_scene::ControlMetric*> metrics{};
        for (const spectra::dynamic_scene::ControlMetric& metric : status.metrics) {
            if (metric_has_placement(metric, placement)) metrics.push_back(&metric);
        }
        std::ranges::stable_sort(metrics, [](const spectra::dynamic_scene::ControlMetric* left, const spectra::dynamic_scene::ControlMetric* right) {
            if (left->priority != right->priority) return left->priority < right->priority;
            return left->key < right->key;
        });
        return metrics;
    }

    [[nodiscard]] std::vector<ControlActionEditor*> control_action_editors_for_group(std::vector<ControlActionEditor>& editors, const std::uint32_t group) {
        std::vector<ControlActionEditor*> selected{};
        for (ControlActionEditor& editor : editors) {
            if (editor.action.group == group) selected.push_back(&editor);
        }
        std::ranges::stable_sort(selected, [](const ControlActionEditor* left, const ControlActionEditor* right) {
            if (left->action.priority != right->action.priority) return left->action.priority < right->action.priority;
            return left->action.id < right->action.id;
        });
        return selected;
    }

    [[nodiscard]] bool control_action_group_has_editors(const std::span<const ControlActionEditor> editors, const std::uint32_t group) {
        return std::ranges::any_of(editors, [group](const ControlActionEditor& editor) { return editor.action.group == group; });
    }

    [[nodiscard]] std::vector<const spectra::dynamic_scene::ControlScalarSeries*> scalar_series_for_group(const std::span<const spectra::dynamic_scene::ControlScalarSeries> series, const std::uint32_t group) {
        std::vector<const spectra::dynamic_scene::ControlScalarSeries*> selected{};
        for (const spectra::dynamic_scene::ControlScalarSeries& chart : series) {
            if (chart.group == group) selected.push_back(&chart);
        }
        std::ranges::stable_sort(selected, [](const spectra::dynamic_scene::ControlScalarSeries* left, const spectra::dynamic_scene::ControlScalarSeries* right) {
            if (left->priority != right->priority) return left->priority < right->priority;
            return left->id < right->id;
        });
        return selected;
    }

    [[nodiscard]] bool scalar_series_group_has_samples(const std::span<const spectra::dynamic_scene::ControlScalarSeries> series, const std::uint32_t group) {
        return std::ranges::any_of(series, [group](const spectra::dynamic_scene::ControlScalarSeries& chart) { return chart.group == group && !chart.samples.empty(); });
    }

    [[nodiscard]] std::string control_setting_group_label(const DynamicSceneControlSettingEditor& editor) {
        return editor.schema.group.empty() ? "Settings" : editor.schema.group;
    }

    [[nodiscard]] std::vector<std::string> control_setting_groups(const std::span<const DynamicSceneControlSettingEditor> editors) {
        std::vector<std::string> groups{};
        for (const DynamicSceneControlSettingEditor& editor : editors) {
            const std::string group = control_setting_group_label(editor);
            if (!std::ranges::contains(groups, group)) groups.push_back(group);
        }
        return groups;
    }

    void sync_dynamic_control_setting_editors(DynamicSceneControlsState& controls, const std::span<const spectra::dynamic_scene::ControlSettingValue> settings) {
        for (const spectra::dynamic_scene::ControlSettingValue& setting : settings) {
            const auto editor = std::ranges::find_if(controls.setting_editors, [&setting](const DynamicSceneControlSettingEditor& candidate) { return candidate.schema.key == setting.key; });
            if (editor == controls.setting_editors.end()) continue;
            if (editor->committed_value != setting.value || dynamic_scene_option_editor_value(editor->editor) != setting.value) {
                set_dynamic_scene_option_editor_value(editor->editor, setting.value);
                editor->committed_value = setting.value;
            }
        }
    }

    [[nodiscard]] bool handle_scene_file_drop(spectra::Spectra& application, spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const std::span<const std::filesystem::path> paths) {
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
            if (spectra::dynamic_scene::is_plugin_file(scene_path)) {
                application.clear_imgui_rgba8_images("scene-control-image://");
                spectra::dynamic_scene::PluginInfo plugin = spectra::dynamic_scene::inspect_plugin(scene_path);
                set_scene_status(state, std::format("Opened dynamic scene controls {}", plugin.title), false);
                session.close_scene();
                begin_dynamic_scene_controls(controls, std::move(plugin));
                application.open_command_popover("scene.dynamic-controls");
                return true;
            }
            set_scene_status(state, std::format("Loaded {}", open_pbrt_scene_path(session, scene_path)), false);
            application.clear_imgui_rgba8_images("scene-control-image://");
            controls = DynamicSceneControlsState{};
            application.close_command_popover("scene.dynamic-controls");
        } catch (const std::exception& error) {
            set_scene_status(state, error.what(), true);
        }
        return true;
    }

    bool draw_dynamic_scene_option_editor_value(OptionEditor& editor) {
        bool changed{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::BeginDisabled(!editor.enabled);
        switch (editor.schema.kind) {
            case spectra::dynamic_scene::OptionKind::Choice: {
                const char* preview = editor.text_value.empty() ? "Select..." : editor.text_value.c_str();
                if (ImGui::BeginCombo("##value", preview)) {
                    for (const spectra::dynamic_scene::OptionChoice& choice : editor.schema.choices) {
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
            case spectra::dynamic_scene::OptionKind::Bool:
                changed = ImGui::Checkbox("##value", &editor.bool_value) || changed;
                break;
            case spectra::dynamic_scene::OptionKind::Float:
                changed = ImGui::InputFloat("##value", &editor.float_value, 0.0f, 0.0f, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            case spectra::dynamic_scene::OptionKind::UnsignedInteger:
                changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &editor.unsigned_value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            default:
                changed = input_text("##value", editor.text_buffer) || changed;
                break;
        }
        ImGui::EndDisabled();
        return changed;
    }

    bool draw_dynamic_scene_option_editor(OptionEditor& editor) {
        bool changed{};
        ImGui::PushID(editor.schema.key.c_str());
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(editor.schema.label.c_str());
        if (!editor.schema.required && editor.schema.default_value.empty()) {
            ImGui::SameLine();
            changed = ImGui::Checkbox("Set", &editor.enabled) || changed;
        }
        if (!editor.schema.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", editor.schema.description.c_str());
        changed = draw_dynamic_scene_option_editor_value(editor) || changed;
        ImGui::PopID();
        return changed;
    }

    bool draw_dynamic_scene_option_editor_compact(OptionEditor& editor) {
        bool changed{};
        ImGui::PushID(editor.schema.key.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", editor.schema.label.c_str());
        if (!editor.schema.required && editor.schema.default_value.empty()) {
            ImGui::SameLine();
            changed = ImGui::Checkbox("Set", &editor.enabled) || changed;
        }
        if (!editor.schema.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", editor.schema.description.c_str());
        ImGui::TableSetColumnIndex(1);
        changed = draw_dynamic_scene_option_editor_value(editor) || changed;
        ImGui::PopID();
        return changed;
    }

    [[nodiscard]] bool dynamic_scene_option_editor_should_commit(const OptionEditor& editor, const bool changed) {
        if (!changed) return false;
        if (editor.schema.kind == spectra::dynamic_scene::OptionKind::Bool) return true;
        if (editor.schema.kind == spectra::dynamic_scene::OptionKind::Choice) return true;
        if (editor.schema.kind == spectra::dynamic_scene::OptionKind::Float || editor.schema.kind == spectra::dynamic_scene::OptionKind::UnsignedInteger) {
            if (ImGui::IsItemDeactivatedAfterEdit()) return true;
            return ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
        }
        return true;
    }

    void draw_dynamic_scene_option_editors_grouped(const std::span<OptionEditor> editors) {
        const std::vector<std::string> groups = dynamic_scene_option_editor_groups(editors);
        for (const std::string& group : groups) {
            ImGui::TextDisabled("%s", group.c_str());
            for (OptionEditor& editor : editors) {
                if (dynamic_scene_option_editor_group_label(editor) != group) continue;
                static_cast<void>(draw_dynamic_scene_option_editor(editor));
                ImGui::Spacing();
            }
        }
    }

    void draw_dynamic_scene_option_editors_compact_grouped(const std::span<OptionEditor> editors) {
        const std::vector<std::string> groups = dynamic_scene_option_editor_groups(editors);
        for (const std::string& group : groups) {
            if (groups.size() > 1u) ImGui::TextDisabled("%s", group.c_str());
            ImGui::PushID(group.c_str());
            if (ImGui::BeginTable("OptionCompactTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                for (OptionEditor& editor : editors) {
                    if (dynamic_scene_option_editor_group_label(editor) != group) continue;
                    static_cast<void>(draw_dynamic_scene_option_editor_compact(editor));
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
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

    [[nodiscard]] bool control_action_enabled(const spectra::dynamic_scene::ControlStatus& status, const std::string& action_id) {
        const auto state = std::ranges::find_if(status.action_states, [&action_id](const spectra::dynamic_scene::ControlActionState& action_state) { return action_state.action_id == action_id; });
        return state != status.action_states.end() && state->enabled;
    }

    [[nodiscard]] std::string_view control_action_disabled_reason(const spectra::dynamic_scene::ControlStatus& status, const std::string& action_id) {
        const auto state = std::ranges::find_if(status.action_states, [&action_id](const spectra::dynamic_scene::ControlActionState& action_state) { return action_state.action_id == action_id; });
        if (state == status.action_states.end()) return "Action disabled by control status";
        if (state->disabled_reason.empty()) return "Action disabled by control status";
        return state->disabled_reason;
    }

    [[nodiscard]] ImVec4 control_log_level_color(const std::string& level) {
        if (level == "ERROR") return ImVec4{1.0f, 0.42f, 0.36f, 1.0f};
        if (level == "WARN") return ImVec4{1.0f, 0.78f, 0.32f, 1.0f};
        return ImVec4{0.68f, 0.73f, 0.80f, 1.0f};
    }

    [[nodiscard]] ImVec4 control_metric_color(const spectra::dynamic_scene::ControlMetric& metric) {
        if (!metric.has_color) return ImVec4{0.72f, 0.79f, 0.86f, 1.0f};
        return ImVec4{metric.color[0], metric.color[1], metric.color[2], metric.color[3]};
    }

    [[nodiscard]] ImVec4 dynamic_scene_control_phase_color(const std::string_view phase) {
        if (phase == "Error") return ImVec4{1.0f, 0.42f, 0.36f, 1.0f};
        if (phase == "Running") return ImVec4{0.16f, 0.86f, 0.55f, 1.0f};
        if (phase == "Complete") return ImVec4{0.55f, 0.85f, 1.0f, 1.0f};
        if (phase == "Paused") return ImVec4{1.0f, 0.78f, 0.32f, 1.0f};
        if (phase == "Ready" || phase == "Active") return ImVec4{0.50f, 0.82f, 0.76f, 1.0f};
        return ImVec4{0.72f, 0.79f, 0.86f, 1.0f};
    }

    void draw_dynamic_scene_status_pill(const std::string_view text) {
        const ImVec4 color = dynamic_scene_control_phase_color(text);
        const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
        const ImVec2 padding{8.0f, 3.0f};
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const ImVec2 size{text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec4 fill{color.x * 0.18f, color.y * 0.18f, color.z * 0.18f, 0.78f};
        const ImVec4 border{color.x, color.y, color.z, 0.42f};
        draw_list->AddRectFilled(cursor, ImVec2{cursor.x + size.x, cursor.y + size.y}, ImGui::GetColorU32(fill), 6.0f);
        draw_list->AddRect(cursor, ImVec2{cursor.x + size.x, cursor.y + size.y}, ImGui::GetColorU32(border), 6.0f);
        draw_list->AddText(ImVec2{cursor.x + padding.x, cursor.y + padding.y}, ImGui::GetColorU32(color), text.data(), text.data() + text.size());
        ImGui::Dummy(size);
    }

    void draw_dynamic_scene_section_title(const char* label) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", label);
    }

    void draw_dynamic_control_metric_row(const spectra::dynamic_scene::ControlMetric& metric) {
        ImGui::TextDisabled("%s", metric.label.c_str());
        ImGui::SameLine();
        ImGui::TextColored(control_metric_color(metric), "%s", metric.value.c_str());
    }

    [[nodiscard]] bool draw_dynamic_control_summary(const spectra::dynamic_scene::ControlStatus& status) {
        const std::vector<const spectra::dynamic_scene::ControlMetric*> metrics = control_metrics_with_placement(status, spectra::dynamic_scene::ControlPlacementPanelSummary);
        if (metrics.empty()) return false;
        if (!ImGui::BeginTable("DynamicSceneControlSummaryMetrics", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return false;
        std::size_t metric_index = 0u;
        for (const spectra::dynamic_scene::ControlMetric* metric : metrics) {
            if (metric_index % 2u == 0u) ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(static_cast<int>(metric_index % 2u));
            ImGui::TextDisabled("%s", metric->label.c_str());
            ImGui::TextColored(control_metric_color(*metric), "%s", metric->value.c_str());
            ++metric_index;
        }
        ImGui::EndTable();
        return true;
    }

    void draw_dynamic_control_diagnostics(const spectra::dynamic_scene::ControlStatus& status) {
        const std::vector<const spectra::dynamic_scene::ControlMetric*> metrics = control_metrics_with_placement(status, spectra::dynamic_scene::ControlPlacementPanelDetail);
        for (const spectra::dynamic_scene::ControlMetric* metric : metrics) {
            if (metric_has_placement(*metric, spectra::dynamic_scene::ControlPlacementPanelSummary)) continue;
            draw_dynamic_control_metric_row(*metric);
        }
    }

    [[nodiscard]] bool control_status_has_detail_metrics(const spectra::dynamic_scene::ControlStatus& status) {
        return std::ranges::any_of(status.metrics, [](const spectra::dynamic_scene::ControlMetric& metric) {
            return metric_has_placement(metric, spectra::dynamic_scene::ControlPlacementPanelDetail) && !metric_has_placement(metric, spectra::dynamic_scene::ControlPlacementPanelSummary);
        });
    }

    void draw_dynamic_control_logs(const std::span<const spectra::dynamic_scene::ControlLogEntry> logs) {
        ImGui::BeginChild("DynamicSceneControlsLogs", ImVec2{-1.0f, 180.0f}, true);
        for (const spectra::dynamic_scene::ControlLogEntry& entry : logs) {
            const std::string line = std::format("{:>5} {:<9} {}", entry.sequence, entry.level, entry.message);
            ImGui::TextColored(control_log_level_color(entry.level), "%s", line.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    [[nodiscard]] std::string control_image_cache_key(const DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlImage& image) {
        const std::string& controls_id = controls.active_id.empty() ? controls.plugin.id : controls.active_id;
        return std::format("scene-control-image://{}/{}", controls_id, image.id);
    }

    void draw_dynamic_control_images(spectra::Spectra& application, const DynamicSceneControlsState& controls, const std::span<const spectra::dynamic_scene::ControlImage> images) {
        for (const spectra::dynamic_scene::ControlImage& image : images) {
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

    void draw_dynamic_control_scalar_series(const std::span<const spectra::dynamic_scene::ControlScalarSeries> series, const std::uint32_t group) {
        const std::vector<const spectra::dynamic_scene::ControlScalarSeries*> selected_series = scalar_series_for_group(series, group);
        for (const spectra::dynamic_scene::ControlScalarSeries* chart : selected_series) {
            if (chart->samples.empty()) continue;
            ImGui::PushID(chart->id.c_str());
            ImGui::TextUnformatted(chart->label.c_str());
            if (!chart->description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", chart->description.c_str());
            const spectra::dynamic_scene::ControlScalarSample& latest = chart->samples.back();
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
            for (const spectra::dynamic_scene::ControlScalarSample& sample : chart->samples) {
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
        if (style == spectra::dynamic_scene::ControlActionStylePrimary) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.10f, 0.34f, 0.38f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.13f, 0.45f, 0.50f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.10f, 0.56f, 0.60f, 1.0f});
            return 3;
        }
        if (style == spectra::dynamic_scene::ControlActionStyleDanger) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.32f, 0.12f, 0.12f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.46f, 0.16f, 0.15f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.60f, 0.18f, 0.16f, 1.0f});
            return 3;
        }
        return 0;
    }

    bool execute_dynamic_control_action_editor(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, ControlActionEditor& editor) {
        try {
            const std::vector<spectra::dynamic_scene::Option> options = collect_dynamic_scene_options(editor.editors);
            session.execute_active_dynamic_scene_control_action(editor.action.id, options);
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

    bool draw_dynamic_control_action_button(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, ControlActionEditor& editor, const spectra::dynamic_scene::ControlStatus& status, const ImVec2 size) {
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
        if (clicked) static_cast<void>(execute_dynamic_control_action_editor(session, state, controls, editor));
        ImGui::PopID();
        return clicked;
    }

    void draw_dynamic_control_action_editor(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, ControlActionEditor& editor, const spectra::dynamic_scene::ControlStatus& status) {
        if (editor.editors.empty()) {
            static_cast<void>(draw_dynamic_control_action_button(session, state, controls, editor, status, ImVec2{-1.0f, 0.0f}));
            return;
        }
        ImGui::PushID(editor.action.id.c_str());
        const bool enabled = control_action_enabled(status, editor.action.id);
        const std::string_view disabled_reason = control_action_disabled_reason(status, editor.action.id);
        ImGui::TextUnformatted(editor.action.label.c_str());
        if (!editor.action.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", editor.action.description.c_str());
        draw_dynamic_scene_option_editors_compact_grouped(editor.editors);
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
        if (clicked) static_cast<void>(execute_dynamic_control_action_editor(session, state, controls, editor));
        ImGui::PopID();
    }

    void draw_dynamic_control_action_button_row(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlStatus& status, const std::vector<ControlActionEditor*>& editors) {
        if (editors.empty()) return;
        constexpr std::size_t max_columns = 3u;
        const std::size_t columns = std::min(max_columns, editors.size());
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float available_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float button_width = std::max(74.0f, (available_width - spacing * static_cast<float>(columns - 1u)) / static_cast<float>(columns));
        for (std::size_t index = 0u; index < editors.size(); ++index) {
            if (index % columns != 0u) ImGui::SameLine(0.0f, spacing);
            static_cast<void>(draw_dynamic_control_action_button(session, state, controls, *editors[index], status, ImVec2{button_width, 0.0f}));
        }
    }

    void draw_dynamic_control_action_group(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlStatus& status, const std::uint32_t group) {
        const std::vector<ControlActionEditor*> editors = control_action_editors_for_group(controls.action_editors, group);
        if (editors.empty()) return;
        std::vector<ControlActionEditor*> immediate_editors{};
        std::vector<ControlActionEditor*> form_editors{};
        for (ControlActionEditor* editor : editors) {
            if (editor->editors.empty()) immediate_editors.push_back(editor);
            else form_editors.push_back(editor);
        }
        if (!immediate_editors.empty()) {
            draw_dynamic_control_action_button_row(session, state, controls, status, immediate_editors);
            if (!form_editors.empty()) ImGui::Spacing();
        }
        for (ControlActionEditor* editor : form_editors) draw_dynamic_control_action_editor(session, state, controls, *editor, status);
    }

    void draw_dynamic_control_setting_editor(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, DynamicSceneControlSettingEditor& editor) {
        if (!dynamic_scene_option_editor_should_commit(editor.editor, draw_dynamic_scene_option_editor(editor.editor))) return;
        const std::string value = dynamic_scene_option_editor_value(editor.editor);
        try {
            session.update_active_dynamic_scene_control_setting(editor.schema.key, value);
            editor.committed_value = value;
            controls.phase = DynamicSceneControlsPhase::Active;
            controls.error.clear();
            set_scene_status(state, std::format("Updated {}", editor.schema.label), false);
        } catch (const std::exception& error) {
            set_dynamic_scene_option_editor_value(editor.editor, editor.committed_value);
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            set_scene_status(state, controls.error, true);
        }
    }

    void draw_dynamic_control_settings(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls) {
        const std::vector<std::string> groups = control_setting_groups(controls.setting_editors);
        for (const std::string& group : groups) {
            ImGui::TextDisabled("%s", group.c_str());
            for (DynamicSceneControlSettingEditor& editor : controls.setting_editors) {
                if (control_setting_group_label(editor) != group) continue;
                draw_dynamic_control_setting_editor(session, state, controls, editor);
                ImGui::Spacing();
            }
        }
    }

    bool draw_dynamic_scene_open_controls(spectra::Spectra& application, spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls) {
        if (!controls.plugin.open_action_description.empty()) ImGui::TextWrapped("%s", controls.plugin.open_action_description.c_str());
        if (!controls.editors.empty()) draw_dynamic_scene_option_editors_grouped(controls.editors);
        if (!ImGui::Button(controls.plugin.open_action_label.c_str(), ImVec2{-1.0f, 0.0f})) return false;
        try {
            application.clear_imgui_rgba8_images("scene-control-image://");
            spectra::dynamic_scene::OpenRequest request{
                .plugin_path = controls.plugin.path,
                .options = collect_dynamic_scene_options(controls.editors),
                .host = session.dynamic_host(),
            };
            spectra::dynamic_scene::PluginSource plugin = spectra::dynamic_scene::load_plugin(std::move(request));
            controls.active_id = plugin.id;
            controls.active_title = plugin.title;
            if (!session.open_dynamic_scene(std::move(plugin.id), std::move(plugin.title), std::move(plugin.create_source))) throw std::runtime_error(session.activation_error().empty() ? "Failed to load dynamic scene plugin" : session.activation_error());
            controls.phase = DynamicSceneControlsPhase::Active;
            controls.error.clear();
            set_scene_status(state, std::format("Loaded {}", controls.active_title), false);
            return true;
        } catch (const std::exception& error) {
            controls.phase = DynamicSceneControlsPhase::Error;
            controls.error = error.what();
            controls.active_title.clear();
            controls.active_id.clear();
            session.close_scene();
            set_scene_status(state, controls.error, true);
            return false;
        }
    }

    bool draw_dynamic_scene_controls_header(spectra::Spectra& application, spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlStatus* status) {
        const float button_size = ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(controls.plugin.controls_panel_title.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", controls.plugin.path.string().c_str());
        const float close_x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - button_size;
        if (ImGui::GetCursorPosX() < close_x) ImGui::SameLine(close_x);
        if (ImGui::Button(ICON_MS_CLOSE "##close_dynamic_scene", ImVec2{button_size, button_size})) {
            application.clear_imgui_rgba8_images("scene-control-image://");
            controls = DynamicSceneControlsState{};
            session.close_scene();
            set_scene_status(state, "Closed dynamic scene controls", false);
            return true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Close Scene");

        const std::string phase = status != nullptr ? status->phase : dynamic_scene_controls_phase_text(controls.phase);
        const std::string headline = status != nullptr ? status->headline : controls.plugin.title;
        draw_dynamic_scene_status_pill(phase);
        if (!headline.empty()) {
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextWrapped("%s", headline.c_str());
            if (status != nullptr && !status->detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", status->detail.c_str());
        }
        if (!controls.active_title.empty() && controls.active_title != controls.plugin.controls_panel_title) {
            ImGui::TextColored(ImVec4{0.55f, 0.62f, 0.70f, 1.0f}, "%s", controls.active_title.c_str());
        }
        return false;
    }

    void draw_dynamic_scene_controls_error(const DynamicSceneControlsState& controls) {
        if (controls.error.empty()) return;
        ImGui::Spacing();
        ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controls.error.c_str());
    }

    void draw_dynamic_scene_training_section(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlStatus& status) {
        if (!control_action_group_has_editors(controls.action_editors, spectra::dynamic_scene::ControlActionGroupRun)) return;
        draw_dynamic_scene_section_title("Training");
        draw_dynamic_control_action_group(session, state, controls, status, spectra::dynamic_scene::ControlActionGroupRun);
    }

    void draw_dynamic_scene_preview_section(spectra::Spectra& application, spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlSnapshot& snapshot) {
        const bool has_actions = control_action_group_has_editors(controls.action_editors, spectra::dynamic_scene::ControlActionGroupPreview);
        const bool has_charts = scalar_series_group_has_samples(snapshot.scalar_series, spectra::dynamic_scene::ControlActionGroupPreview);
        if (!has_actions && snapshot.images.empty() && !has_charts) return;
        draw_dynamic_scene_section_title("Preview");
        if (has_actions) draw_dynamic_control_action_group(session, state, controls, snapshot.status, spectra::dynamic_scene::ControlActionGroupPreview);
        if (!snapshot.images.empty()) {
            ImGui::Spacing();
            draw_dynamic_control_images(application, controls, snapshot.images);
        }
        if (has_charts) {
            ImGui::Spacing();
            draw_dynamic_control_scalar_series(snapshot.scalar_series, spectra::dynamic_scene::ControlActionGroupPreview);
        }
    }

    void draw_dynamic_scene_diagnostics_section(spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls, const spectra::dynamic_scene::ControlSnapshot& snapshot) {
        const bool has_settings = !controls.setting_editors.empty();
        const bool has_debug_actions = control_action_group_has_editors(controls.action_editors, spectra::dynamic_scene::ControlActionGroupDebug);
        const bool has_utility_actions = control_action_group_has_editors(controls.action_editors, spectra::dynamic_scene::ControlActionGroupUtility);
        const bool has_detail_metrics = control_status_has_detail_metrics(snapshot.status);
        if (!has_settings && !has_debug_actions && !has_utility_actions && !has_detail_metrics) return;
        draw_dynamic_scene_section_title("Diagnostics");
        if (has_settings) draw_dynamic_control_settings(session, state, controls);
        if (has_debug_actions) {
            ImGui::Spacing();
            draw_dynamic_control_action_group(session, state, controls, snapshot.status, spectra::dynamic_scene::ControlActionGroupDebug);
        }
        if (has_utility_actions) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", "Utility");
            draw_dynamic_control_action_group(session, state, controls, snapshot.status, spectra::dynamic_scene::ControlActionGroupUtility);
        }
        if (has_detail_metrics) {
            ImGui::Spacing();
            draw_dynamic_control_diagnostics(snapshot.status);
        }
    }

    void draw_dynamic_scene_log_section(const std::span<const spectra::dynamic_scene::ControlLogEntry> logs) {
        if (logs.empty()) return;
        draw_dynamic_scene_section_title("Log");
        draw_dynamic_control_logs(logs);
    }

    void draw_dynamic_scene_controls_panel(spectra::Spectra& application, spectra::scene_session::Session& session, SceneSessionStatusState& state, DynamicSceneControlsState& controls) {
        if (controls.phase == DynamicSceneControlsPhase::None) {
            ImGui::TextDisabled("%s", "No dynamic scene controls");
            return;
        }

        std::optional<spectra::dynamic_scene::ControlSnapshot> active_snapshot{};
        if (controls.phase == DynamicSceneControlsPhase::Active || controls.phase == DynamicSceneControlsPhase::Error) {
            try {
                if (session.has_active_dynamic_scene_controls()) {
                    active_snapshot = session.active_dynamic_scene_control_snapshot();
                    sync_dynamic_control_setting_editors(controls, active_snapshot->settings);
                }
            } catch (const std::exception& error) {
                controls.phase = DynamicSceneControlsPhase::Error;
                controls.error = error.what();
            }
        }

        const spectra::dynamic_scene::ControlStatus* active_status = active_snapshot.has_value() ? &active_snapshot->status : nullptr;
        if (draw_dynamic_scene_controls_header(application, session, state, controls, active_status)) return;
        draw_dynamic_scene_controls_error(controls);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (!active_snapshot.has_value()) {
            static_cast<void>(draw_dynamic_scene_open_controls(application, session, state, controls));
            return;
        }

        const spectra::dynamic_scene::ControlSnapshot& snapshot = *active_snapshot;
        if (draw_dynamic_control_summary(snapshot.status)) {
            ImGui::Spacing();
            ImGui::Separator();
        }
        draw_dynamic_scene_training_section(session, state, controls, snapshot.status);
        if (scalar_series_group_has_samples(snapshot.scalar_series, spectra::dynamic_scene::ControlActionGroupRun)) {
            draw_dynamic_scene_section_title("Telemetry");
            draw_dynamic_control_scalar_series(snapshot.scalar_series, spectra::dynamic_scene::ControlActionGroupRun);
        }
        draw_dynamic_scene_preview_section(application, session, state, controls, snapshot);
        draw_dynamic_scene_diagnostics_section(session, state, controls, snapshot);
        draw_dynamic_scene_log_section(snapshot.logs);
    }

    [[nodiscard]] std::string dynamic_scene_overlay_text(const spectra::dynamic_scene::ControlStatus& status) {
        std::string text = status.phase;
        if (!status.headline.empty()) text += std::format(" | {}", status.headline);
        const std::vector<const spectra::dynamic_scene::ControlMetric*> metrics = control_metrics_with_placement(status, spectra::dynamic_scene::ControlPlacementViewportOverlay);
        std::size_t shown_metric_count = 0u;
        for (const spectra::dynamic_scene::ControlMetric* metric : metrics) {
            if (shown_metric_count >= 5u) break;
            text += std::format(" | {} {}", metric->label, metric->value);
            ++shown_metric_count;
        }
        constexpr std::size_t max_overlay_text_size = 220u;
        if (text.size() > max_overlay_text_size) text = text.substr(0u, max_overlay_text_size - 3u) + "...";
        return text;
    }

    void draw_dynamic_scene_controls_overlay(spectra::scene_session::Session& session, DynamicSceneControlsState& controls, const ImVec2 viewport_position, const ImVec2 viewport_size) {
        if (controls.phase == DynamicSceneControlsPhase::None) return;
        if (!session.has_active_dynamic_scene_controls()) return;
        spectra::dynamic_scene::ControlSnapshot snapshot{};
        try {
            snapshot = session.active_dynamic_scene_control_snapshot();
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

    void handle_scene_timeline_shortcuts(spectra::scene_session::Session& session) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        if (session.active_scene_timeline_streaming_enabled() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) session.toggle_active_scene_timeline_playback();
        if (session.active_scene_timeline_enabled() && ImGui::IsKeyPressed(ImGuiKey_R, false)) session.request_active_scene_timeline_reset();
    }

    [[nodiscard]] std::string scene_session_tooltip(const spectra::scene_session::SceneDescriptor* active_scene) {
        if (active_scene == nullptr) return "Empty Scene\nDrop a PBRT scene or dynamic scene plugin into the window to load it";
        return std::format(
            "{}\n{}\nDrop a PBRT scene or dynamic scene plugin into the window to replace it",
            active_scene->id,
            active_scene->kind == spectra::scene_session::SceneKind::Static ? "Static" : "Dynamic");
    }

    [[nodiscard]] spectra::WorkspaceTitle make_scene_session_title(spectra::scene_session::Session& session, SceneSessionStatusState& state) {
        const spectra::scene_session::SceneDescriptor* active_scene = session.has_active_scene() ? &session.active_scene_descriptor() : nullptr;
        spectra::WorkspaceTitle title{
            .detail  = active_scene != nullptr ? active_scene->title : "Untitled",
            .tooltip = scene_session_tooltip(active_scene),
        };
        if (scene_status_visible(state)) {
            title.status_text = state.status_text;
            title.status_error = state.status_error;
        }
        return title;
    }

    [[nodiscard]] spectra::scene::SceneSource make_scene_source(std::shared_ptr<spectra::scene_session::Session> session) {
        return spectra::scene::SceneSource{
            .initial_scene = session->active_scene(),
            .update = [session](const double delta_seconds) {
                handle_scene_timeline_shortcuts(*session);
                session->tick(delta_seconds);
                return session->active_scene();
            },
        };
    }

    static_assert(spectra::RendererFor<spectra::pathtracer::Renderer, spectra::Spectra>);
    static_assert(spectra::RendererFor<spectra::rasterizer::Renderer, spectra::Spectra>);

    [[nodiscard]] std::shared_ptr<spectra::scene::Scene> make_empty_scene() {
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

} // namespace

int main(const int argc, const char* const* const argv) {
    try {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        std::optional<std::string> scene_id{};
        xayah::util::Command command =
            xayah::util::Command{"Open the Spectra visualization workspace."}
            | xayah::util::option({.long_name = "scene", .value_name = "scene-id-or-path", .description = "PBRT scene id/path or dynamic scene plugin path", .show_default = false}, scene_id)
            | xayah::util::example("--scene default")
            | xayah::util::example("--scene path/to/scene.pbrt")
            | xayah::util::example("--scene path/to/plugin.dll");
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

        auto dynamic_host = std::make_shared<spectra::dynamic_scene::HostServiceRouter>();
        auto scene_session = std::make_shared<spectra::scene_session::Session>(make_empty_scene(), dynamic_host);
        auto camera_workspace = std::make_shared<spectra::scene::CameraWorkspace>();
        auto scene_status_state = std::make_shared<SceneSessionStatusState>();
        auto dynamic_scene_controls = std::make_shared<DynamicSceneControlsState>();

        spectra::Spectra app{"Spectra"};
        app.register_renderer(std::make_shared<spectra::rasterizer::Renderer>(make_scene_source(scene_session), camera_workspace, dynamic_host));
        app.register_renderer(std::make_shared<spectra::pathtracer::Renderer>(make_scene_source(scene_session), camera_workspace));
        app.set_workspace_title_provider([scene_session, scene_status_state] { return make_scene_session_title(*scene_session, *scene_status_state); });
        app.register_file_drop_handler(spectra::FileDropHandler{
            .id             = "scene.file-drop",
            .title          = "Scene File Drop",
            .owner_renderer = {},
            .handle         = [application = &app, scene_session, scene_status_state, dynamic_scene_controls](const std::span<const std::filesystem::path> paths) { return handle_scene_file_drop(*application, *scene_session, *scene_status_state, *dynamic_scene_controls, paths); },
        });
        app.register_command_popover(spectra::CommandPopover{
            .id             = "scene.dynamic-controls",
            .title          = "Scene",
            .icon           = ICON_MS_DATASET,
            .owner_renderer = {},
            .shortcut_label = "F9",
            .shortcut_key   = ImGuiKey_F9,
            .draw           = [application = &app, scene_session, scene_status_state, dynamic_scene_controls] { draw_dynamic_scene_controls_panel(*application, *scene_session, *scene_status_state, *dynamic_scene_controls); },
        });
        app.register_viewport_overlay(spectra::ViewportOverlay{
            .id             = "scene.dynamic-controls-overlay",
            .title          = "Dynamic Scene Controls Overlay",
            .owner_renderer = {},
            .priority       = 0,
            .draw           = [scene_session, dynamic_scene_controls](const ImVec2 viewport_position, const ImVec2 viewport_size) { draw_dynamic_scene_controls_overlay(*scene_session, *dynamic_scene_controls, viewport_position, viewport_size); },
        });
        if (scene_id.has_value()) {
            const std::filesystem::path requested_path{*scene_id};
            if (spectra::dynamic_scene::is_plugin_file(requested_path)) {
                scene_session->close_scene();
                spectra::dynamic_scene::PluginInfo plugin = spectra::dynamic_scene::inspect_plugin(requested_path);
                set_scene_status(*scene_status_state, std::format("Opened dynamic scene controls {}", plugin.title), false);
                begin_dynamic_scene_controls(*dynamic_scene_controls, std::move(plugin));
                app.open_command_popover("scene.dynamic-controls");
            } else if (is_pbrt_scene_file(requested_path) || std::filesystem::is_regular_file(requested_path)) {
                static_cast<void>(open_pbrt_scene_path(*scene_session, requested_path));
            } else {
                auto scene = std::make_shared<spectra::scene::Scene>(spectra::scene::Scene::parse_pbrt(*scene_id));
                const std::string title = scene->info().title;
                if (!scene_session->open_static_scene(*scene_id, title, [scene = std::move(scene)] { return scene; })) throw std::runtime_error(scene_session->activation_error());
            }
        }
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
