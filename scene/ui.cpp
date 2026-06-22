module;

#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>

module spectra.scene.ui;

import std;

namespace spectra::scene {
    namespace {
        enum class SceneControlsPhase {
            None,
            PluginLoaded,
            Active,
            Error,
        };

        struct OptionEditor {
            ControlOptionSchema schema{};
            std::string text_value{};
            std::vector<char> text_buffer{};
            bool bool_value{};
            float float_value{};
            std::uint64_t unsigned_value{};
            bool enabled{true};
        };

        struct ActionEditor {
            ControlAction action{};
            std::vector<OptionEditor> options{};
        };

        struct SettingEditor {
            ControlOptionSchema schema{};
            OptionEditor option{};
            std::string committed_value{};
        };

    } // namespace

    struct StatusState {
        std::string status_text{};
        bool status_error{};
        std::chrono::steady_clock::time_point status_expires{};
    };

    struct ControlsState {
        SceneControlsPhase phase{SceneControlsPhase::None};
        ScenePluginInfo plugin{};
        std::vector<OptionEditor> open_options{};
        std::vector<ActionEditor> actions{};
        std::vector<SettingEditor> settings{};
        std::string error{};
        std::string active_title{};
        std::string active_id{};
    };

    struct SceneUi::State {
        std::shared_ptr<Scene> scene_instance{std::make_shared<Scene>()};
        std::shared_ptr<CameraWorkspace> camera_workspace{std::make_shared<CameraWorkspace>()};
        StatusState status{};
        ControlsState controls{};
    };

    namespace {

        void set_scene_status(StatusState& status, std::string text, const bool error) {
            status.status_text = std::move(text);
            status.status_error = error;
            status.status_expires = std::chrono::steady_clock::now() + std::chrono::seconds{4};
        }

        [[nodiscard]] bool scene_status_visible(StatusState& status) {
            if (status.status_text.empty()) return false;
            if (std::chrono::steady_clock::now() < status.status_expires) return true;
            status.status_text.clear();
            status.status_error = false;
            return false;
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

        int resize_input_text_callback(ImGuiInputTextCallbackData* data) {
            if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
            auto value = static_cast<std::vector<char>*>(data->UserData);
            value->resize(static_cast<std::size_t>(data->BufTextLen) + 1u);
            data->Buf = value->data();
            return 0;
        }

        void set_text_buffer(std::vector<char>& buffer, const std::string_view value) {
            buffer.assign(value.begin(), value.end());
            buffer.push_back('\0');
        }

        [[nodiscard]] std::string text_buffer_value(const std::vector<char>& buffer) {
            if (buffer.empty()) return {};
            return std::string{buffer.data()};
        }

        bool input_text(const char* label, std::vector<char>& value) {
            if (value.empty()) value.push_back('\0');
            return ImGui::InputText(label, value.data(), value.size(), ImGuiInputTextFlags_CallbackResize, resize_input_text_callback, &value);
        }

        [[nodiscard]] bool choice_contains_value(const ControlOptionSchema& schema, const std::string& value) {
            return std::ranges::any_of(schema.choices, [&value](const ControlOptionChoice& choice) { return choice.value == value; });
        }

        [[nodiscard]] OptionEditor make_option_editor(ControlOptionSchema schema) {
            OptionEditor editor{.schema = std::move(schema)};
            editor.enabled = editor.schema.required || !editor.schema.default_value.empty();
            switch (editor.schema.kind) {
            case ControlOptionKind::Bool:
                editor.bool_value = editor.schema.default_value.empty() ? false : parse_bool_text(editor.schema.default_value, std::format("Scene option '{}'", editor.schema.key));
                break;
            case ControlOptionKind::Float:
                editor.float_value = editor.schema.default_value.empty() ? 0.0f : parse_float_text(editor.schema.default_value, std::format("Scene option '{}'", editor.schema.key));
                break;
            case ControlOptionKind::UnsignedInteger:
                editor.unsigned_value = editor.schema.default_value.empty() ? 0u : parse_unsigned_integer_text(editor.schema.default_value, std::format("Scene option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = editor.schema.default_value;
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
            }
            return editor;
        }

        [[nodiscard]] std::string option_editor_value(const OptionEditor& editor) {
            switch (editor.schema.kind) {
            case ControlOptionKind::Bool: return editor.bool_value ? "true" : "false";
            case ControlOptionKind::Float: return std::format("{:.9g}", editor.float_value);
            case ControlOptionKind::UnsignedInteger: return std::format("{}", editor.unsigned_value);
            case ControlOptionKind::Choice: return editor.text_value;
            default: return text_buffer_value(editor.text_buffer);
            }
        }

        void set_option_editor_value(OptionEditor& editor, const std::string_view value) {
            switch (editor.schema.kind) {
            case ControlOptionKind::Bool:
                editor.bool_value = parse_bool_text(value, std::format("Scene option '{}'", editor.schema.key));
                break;
            case ControlOptionKind::Float:
                editor.float_value = parse_float_text(value, std::format("Scene option '{}'", editor.schema.key));
                break;
            case ControlOptionKind::UnsignedInteger:
                editor.unsigned_value = parse_unsigned_integer_text(value, std::format("Scene option '{}'", editor.schema.key));
                break;
            default:
                editor.text_value = std::string{value};
                set_text_buffer(editor.text_buffer, editor.text_value);
                break;
            }
        }

        [[nodiscard]] std::vector<ControlOption> collect_options(const std::span<const OptionEditor> editors) {
            std::vector<ControlOption> options{};
            options.reserve(editors.size());
            for (const OptionEditor& editor : editors) {
                if (!editor.schema.required && !editor.enabled) continue;
                std::string value = option_editor_value(editor);
                if (editor.schema.required && value.empty()) throw std::runtime_error(std::format("{} is required", editor.schema.label));
                if (editor.schema.kind == ControlOptionKind::Choice && !value.empty() && !choice_contains_value(editor.schema, value)) throw std::runtime_error(std::format("{} must be one of the declared choices", editor.schema.label));
                if (!value.empty() || editor.schema.required || editor.schema.kind == ControlOptionKind::Bool || editor.schema.kind == ControlOptionKind::Float || editor.schema.kind == ControlOptionKind::UnsignedInteger) {
                    options.push_back(ControlOption{
                        .key = editor.schema.key,
                        .value = std::move(value),
                    });
                }
            }
            return options;
        }

        [[nodiscard]] ActionEditor make_action_editor(ControlAction action) {
            ActionEditor editor{.action = std::move(action)};
            editor.options.reserve(editor.action.options.size());
            for (const ControlOptionSchema& schema : editor.action.options) editor.options.push_back(make_option_editor(schema));
            return editor;
        }

        [[nodiscard]] SettingEditor make_setting_editor(ControlOptionSchema schema, std::string value) {
            schema.default_value = value;
            schema.required = true;
            SettingEditor editor{};
            editor.schema = schema;
            editor.option = make_option_editor(std::move(schema));
            editor.committed_value = std::move(value);
            return editor;
        }

        void begin_scene_controls(ControlsState& controls, ScenePluginInfo plugin);

        [[nodiscard]] const char* phase_text(const SceneControlsPhase phase) {
            switch (phase) {
            case SceneControlsPhase::None: return "No Scene Plugin";
            case SceneControlsPhase::PluginLoaded: return "Plugin Loaded";
            case SceneControlsPhase::Active: return "Active";
            case SceneControlsPhase::Error: return "Error";
            }
            throw std::runtime_error("Unknown scene controls phase");
        }

        [[nodiscard]] bool metric_has_placement(const ControlMetric& metric, const std::uint32_t placement) {
            return (metric.placement_flags & placement) != 0u;
        }

        [[nodiscard]] std::vector<const ControlMetric*> control_metrics_with_placement(const ControlState& state, const std::uint32_t placement) {
            std::vector<const ControlMetric*> metrics{};
            for (const ControlMetric& metric : state.metrics)
                if (metric_has_placement(metric, placement)) metrics.push_back(&metric);
            return metrics;
        }

        [[nodiscard]] std::vector<ActionEditor*> action_editors_for_section(std::vector<ActionEditor>& editors, const std::string_view section_id) {
            std::vector<ActionEditor*> selected{};
            for (ActionEditor& editor : editors)
                if (editor.action.section_id == section_id) selected.push_back(&editor);
            return selected;
        }

        [[nodiscard]] bool action_section_has_editors(const std::span<const ActionEditor> editors, const std::string_view section_id) {
            return std::ranges::any_of(editors, [section_id](const ActionEditor& editor) { return editor.action.section_id == section_id; });
        }

        [[nodiscard]] ImVec4 control_metric_color(const ControlMetric& metric) {
            if (!metric.has_color) return ImVec4{0.72f, 0.79f, 0.86f, 1.0f};
            return ImVec4{metric.color[0], metric.color[1], metric.color[2], metric.color[3]};
        }

        [[nodiscard]] ImVec4 phase_color(const std::string_view phase) {
            if (phase == "Error") return ImVec4{1.0f, 0.42f, 0.36f, 1.0f};
            if (phase == "Running") return ImVec4{0.16f, 0.86f, 0.55f, 1.0f};
            if (phase == "Complete") return ImVec4{0.55f, 0.85f, 1.0f, 1.0f};
            if (phase == "Paused") return ImVec4{1.0f, 0.78f, 0.32f, 1.0f};
            if (phase == "Ready" || phase == "Active") return ImVec4{0.50f, 0.82f, 0.76f, 1.0f};
            return ImVec4{0.72f, 0.79f, 0.86f, 1.0f};
        }

        void draw_status_pill(const std::string_view text) {
            const ImVec4 color = phase_color(text);
            const ImVec2 text_size = ImGui::CalcTextSize(text.data(), text.data() + text.size());
            const ImVec2 padding{8.0f, 3.0f};
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            const ImVec2 size{text_size.x + padding.x * 2.0f, text_size.y + padding.y * 2.0f};
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(cursor, ImVec2{cursor.x + size.x, cursor.y + size.y}, ImGui::GetColorU32(ImVec4{color.x * 0.18f, color.y * 0.18f, color.z * 0.18f, 0.78f}), 6.0f);
            draw_list->AddRect(cursor, ImVec2{cursor.x + size.x, cursor.y + size.y}, ImGui::GetColorU32(ImVec4{color.x, color.y, color.z, 0.42f}), 6.0f);
            draw_list->AddText(ImVec2{cursor.x + padding.x, cursor.y + padding.y}, ImGui::GetColorU32(color), text.data(), text.data() + text.size());
            ImGui::Dummy(size);
        }

        [[nodiscard]] bool draw_option_value(OptionEditor& editor) {
            bool changed{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::BeginDisabled(!editor.enabled);
            switch (editor.schema.kind) {
            case ControlOptionKind::Choice: {
                const char* preview = editor.text_value.empty() ? "Select..." : editor.text_value.c_str();
                if (ImGui::BeginCombo("##value", preview)) {
                    for (const ControlOptionChoice& choice : editor.schema.choices) {
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
            case ControlOptionKind::Bool:
                changed = ImGui::Checkbox("##value", &editor.bool_value) || changed;
                break;
            case ControlOptionKind::Float:
                changed = ImGui::InputFloat("##value", &editor.float_value, 0.0f, 0.0f, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            case ControlOptionKind::UnsignedInteger:
                changed = ImGui::InputScalar("##value", ImGuiDataType_U64, &editor.unsigned_value, nullptr, nullptr, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                break;
            default:
                changed = input_text("##value", editor.text_buffer) || changed;
                break;
            }
            ImGui::EndDisabled();
            return changed;
        }

        [[nodiscard]] bool draw_option_editor(OptionEditor& editor, const bool compact) {
            bool changed{};
            ImGui::PushID(editor.schema.key.c_str());
            if (compact) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", editor.schema.label.c_str());
            if (!editor.schema.required && editor.schema.default_value.empty()) {
                ImGui::SameLine();
                changed = ImGui::Checkbox("Set", &editor.enabled) || changed;
            }
            if (!editor.schema.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", editor.schema.description.c_str());
            if (compact) ImGui::TableSetColumnIndex(1);
            changed = draw_option_value(editor) || changed;
            ImGui::PopID();
            return changed;
        }

        [[nodiscard]] bool option_section_has_editors(const std::span<const OptionEditor> editors, const std::string_view section_id) {
            return std::ranges::any_of(editors, [section_id](const OptionEditor& editor) { return editor.schema.section_id == section_id; });
        }

        [[nodiscard]] std::size_t option_section_count(const ScenePluginInfo& plugin, const std::span<const OptionEditor> editors) {
            std::size_t count{};
            for (const ControlSection& section : plugin.sections)
                if (option_section_has_editors(editors, section.id)) ++count;
            return count;
        }

        void draw_option_section(const std::span<OptionEditor> editors, const std::string_view section_id, const bool compact) {
            if (!compact) {
                for (OptionEditor& editor : editors) {
                    if (editor.schema.section_id != section_id) continue;
                    static_cast<void>(draw_option_editor(editor, false));
                    ImGui::Spacing();
                }
                return;
            }
            ImGui::PushID(section_id.data(), section_id.data() + section_id.size());
            if (ImGui::BeginTable("OptionTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.42f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.58f);
                for (OptionEditor& editor : editors) {
                    if (editor.schema.section_id != section_id) continue;
                    static_cast<void>(draw_option_editor(editor, true));
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }

        void draw_option_sections(const ScenePluginInfo& plugin, const std::span<OptionEditor> editors, const bool compact) {
            const std::size_t section_count = option_section_count(plugin, editors);
            for (const ControlSection& section : plugin.sections) {
                if (!option_section_has_editors(editors, section.id)) continue;
                if (section_count > 1u) ImGui::TextDisabled("%s", section.label.c_str());
                draw_option_section(editors, section.id, compact);
            }
        }

        [[nodiscard]] bool option_editor_should_commit(const OptionEditor& editor, const bool changed) {
            if (!changed) return false;
            if (editor.schema.kind == ControlOptionKind::Bool) return true;
            if (editor.schema.kind == ControlOptionKind::Choice) return true;
            if (editor.schema.kind == ControlOptionKind::Float || editor.schema.kind == ControlOptionKind::UnsignedInteger) {
                if (ImGui::IsItemDeactivatedAfterEdit()) return true;
                return ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false));
            }
            return true;
        }

        [[nodiscard]] bool action_enabled(const ControlState& state, const std::string& action_id) {
            const auto action_state = std::ranges::find_if(state.action_states, [&action_id](const ControlActionState& candidate) { return candidate.action_id == action_id; });
            return action_state != state.action_states.end() && action_state->enabled;
        }

        [[nodiscard]] std::string_view action_disabled_reason(const ControlState& state, const std::string& action_id) {
            const auto action_state = std::ranges::find_if(state.action_states, [&action_id](const ControlActionState& candidate) { return candidate.action_id == action_id; });
            if (action_state == state.action_states.end()) return "Action disabled by control state";
            if (action_state->disabled_reason.empty()) return "Action disabled by control state";
            return action_state->disabled_reason;
        }

        [[nodiscard]] int push_action_button_style(const std::uint32_t style) {
            if (style == ControlActionStylePrimary) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.10f, 0.34f, 0.38f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.13f, 0.45f, 0.50f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.10f, 0.56f, 0.60f, 1.0f});
                return 3;
            }
            if (style == ControlActionStyleDanger) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.32f, 0.12f, 0.12f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.46f, 0.16f, 0.15f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.60f, 0.18f, 0.16f, 1.0f});
                return 3;
            }
            return 0;
        }

        bool execute_action(Scene& scene_instance, StatusState& status, ControlsState& controls, ActionEditor& editor);

        bool draw_action_button(Scene& scene_instance, StatusState& status, ControlsState& controls, ActionEditor& editor, const ControlState& control_state, const ImVec2 size) {
            ImGui::PushID(editor.action.id.c_str());
            const bool enabled = action_enabled(control_state, editor.action.id);
            const std::string_view disabled_reason = action_disabled_reason(control_state, editor.action.id);
            const int style_count = push_action_button_style(editor.action.style);
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(editor.action.label.c_str(), size);
            ImGui::EndDisabled();
            if (style_count != 0) ImGui::PopStyleColor(style_count);
            if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%.*s", static_cast<int>(disabled_reason.size()), disabled_reason.data());
            if (enabled && !editor.action.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", editor.action.description.c_str());
            if (clicked) static_cast<void>(execute_action(scene_instance, status, controls, editor));
            ImGui::PopID();
            return clicked;
        }

        void draw_action_section(Scene& scene_instance, StatusState& status, ControlsState& controls, const ControlState& control_state, const std::string_view section_id) {
            const std::vector<ActionEditor*> editors = action_editors_for_section(controls.actions, section_id);
            if (editors.empty()) return;
            std::vector<ActionEditor*> immediate{};
            std::vector<ActionEditor*> forms{};
            for (ActionEditor* editor : editors) {
                if (editor->options.empty()) immediate.push_back(editor);
                else forms.push_back(editor);
            }
            if (!immediate.empty()) {
                constexpr std::size_t max_columns = 3u;
                const std::size_t columns = std::min(max_columns, immediate.size());
                const float spacing = ImGui::GetStyle().ItemSpacing.x;
                const float available_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float button_width = std::max(74.0f, (available_width - spacing * static_cast<float>(columns - 1u)) / static_cast<float>(columns));
                for (std::size_t index = 0u; index < immediate.size(); ++index) {
                    if (index % columns != 0u) ImGui::SameLine(0.0f, spacing);
                    static_cast<void>(draw_action_button(scene_instance, status, controls, *immediate[index], control_state, ImVec2{button_width, 0.0f}));
                }
                if (!forms.empty()) ImGui::Spacing();
            }
            for (ActionEditor* editor : forms) {
                ImGui::PushID(editor->action.id.c_str());
                ImGui::TextUnformatted(editor->action.label.c_str());
                if (!editor->action.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", editor->action.description.c_str());
                draw_option_sections(controls.plugin, editor->options, true);
                static_cast<void>(draw_action_button(scene_instance, status, controls, *editor, control_state, ImVec2{-1.0f, 0.0f}));
                ImGui::PopID();
                ImGui::Spacing();
            }
        }

        [[nodiscard]] bool setting_section_has_editors(const std::span<const SettingEditor> editors, const std::string_view section_id) {
            return std::ranges::any_of(editors, [section_id](const SettingEditor& editor) { return editor.schema.section_id == section_id; });
        }

        void draw_settings(Scene& scene_instance, StatusState& status, ControlsState& controls, std::string_view section_id);
        bool draw_open_controls(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls);
        void draw_controls_panel(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls);
        void draw_controls_overlay(Scene& scene_instance, ControlsState& controls, ImVec2 viewport_position, ImVec2 viewport_size);
        void open_scene_files(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls, std::span<const std::filesystem::path> paths);
    } // namespace

    namespace {
        void begin_scene_controls(ControlsState& controls, ScenePluginInfo plugin) {
            controls.plugin = std::move(plugin);
            controls.open_options.clear();
            controls.open_options.reserve(controls.plugin.open_options.size());
            for (ControlOptionSchema& schema : controls.plugin.open_options) controls.open_options.push_back(make_option_editor(std::move(schema)));

            controls.actions.clear();
            controls.actions.reserve(controls.plugin.control_actions.size());
            for (const ControlAction& action : controls.plugin.control_actions) controls.actions.push_back(make_action_editor(action));

            controls.settings.clear();
            controls.settings.reserve(controls.plugin.control_settings.size());
            for (const ControlOptionSchema& schema : controls.plugin.control_settings) controls.settings.push_back(make_setting_editor(schema, schema.default_value));

            controls.error.clear();
            controls.active_title.clear();
            controls.active_id.clear();
            controls.phase = SceneControlsPhase::PluginLoaded;
        }

        bool execute_action(Scene& scene_instance, StatusState& status, ControlsState& controls, ActionEditor& editor) {
            try {
                const std::vector<ControlOption> options = collect_options(editor.options);
                scene_instance.execute_control_action(editor.action.id, options);
                controls.phase = SceneControlsPhase::Active;
                controls.error.clear();
                set_scene_status(status, std::format("Executed {}", editor.action.label), false);
                return true;
            } catch (const std::exception& error) {
                controls.phase = SceneControlsPhase::Error;
                controls.error = error.what();
                set_scene_status(status, controls.error, true);
                return false;
            }
        }

        void draw_settings(Scene& scene_instance, StatusState& status, ControlsState& controls, const std::string_view section_id) {
            for (SettingEditor& editor : controls.settings) {
                if (editor.schema.section_id != section_id) continue;
                if (!option_editor_should_commit(editor.option, draw_option_editor(editor.option, false))) continue;
                const std::string value = option_editor_value(editor.option);
                try {
                    scene_instance.update_control_setting(editor.schema.key, value);
                    editor.committed_value = value;
                    controls.phase = SceneControlsPhase::Active;
                    controls.error.clear();
                    set_scene_status(status, std::format("Updated {}", editor.schema.label), false);
                } catch (const std::exception& error) {
                    set_option_editor_value(editor.option, editor.committed_value);
                    controls.phase = SceneControlsPhase::Error;
                    controls.error = error.what();
                    set_scene_status(status, controls.error, true);
                }
                ImGui::Spacing();
            }
        }

        [[nodiscard]] bool draw_summary(const ControlState& state) {
            const std::vector<const ControlMetric*> metrics = control_metrics_with_placement(state, ControlPlacementPanelSummary);
            if (metrics.empty()) return false;
            if (!ImGui::BeginTable("SceneControlSummary", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return false;
            std::size_t index{};
            for (const ControlMetric* metric : metrics) {
                if (index % 2u == 0u) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(static_cast<int>(index % 2u));
                ImGui::TextDisabled("%s", metric->label.c_str());
                ImGui::TextColored(control_metric_color(*metric), "%s", metric->value.c_str());
                ++index;
            }
            ImGui::EndTable();
            return true;
        }

        void draw_detail_metrics(const ControlState& state, const std::string_view section_id) {
            const std::vector<const ControlMetric*> metrics = control_metrics_with_placement(state, ControlPlacementPanelDetail);
            for (const ControlMetric* metric : metrics) {
                if (metric->section_id != section_id) continue;
                if (metric_has_placement(*metric, ControlPlacementPanelSummary)) continue;
                ImGui::TextDisabled("%s", metric->label.c_str());
                ImGui::SameLine();
                ImGui::TextColored(control_metric_color(*metric), "%s", metric->value.c_str());
            }
        }

        [[nodiscard]] bool section_has_detail_metrics(const ControlState& state, const std::string_view section_id) {
            return std::ranges::any_of(state.metrics, [section_id](const ControlMetric& metric) {
                return metric.section_id == section_id && metric_has_placement(metric, ControlPlacementPanelDetail) && !metric_has_placement(metric, ControlPlacementPanelSummary);
            });
        }

        bool draw_open_controls(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls) {
            if (!controls.plugin.open_action_description.empty()) ImGui::TextWrapped("%s", controls.plugin.open_action_description.c_str());
            if (!controls.open_options.empty()) draw_option_sections(controls.plugin, controls.open_options, false);
            if (!ImGui::Button(controls.plugin.open_action_label.c_str(), ImVec2{-1.0f, 0.0f})) return false;
            try {
                application.clear_imgui_rgba8_images("scene-control-image://");
                scene_instance.open_plugin_scene(ScenePluginOpenRequest{
                    .plugin_path = controls.plugin.path,
                    .options = collect_options(controls.open_options),
                    .host = scene_instance.host_services(),
                });
                const SceneDescriptor& descriptor = scene_instance.descriptor();
                controls.active_id = descriptor.id;
                controls.active_title = descriptor.title;
                controls.phase = SceneControlsPhase::Active;
                controls.error.clear();
                set_scene_status(status, std::format("Loaded {}", controls.active_title), false);
                return true;
            } catch (const std::exception& error) {
                controls.phase = SceneControlsPhase::Error;
                controls.error = error.what();
                controls.active_title.clear();
                controls.active_id.clear();
                scene_instance.close();
                set_scene_status(status, controls.error, true);
                return false;
            }
        }

        [[nodiscard]] bool draw_header(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls, const ControlState* control_state) {
            const float button_size = ImGui::GetFrameHeight();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(controls.plugin.title.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", controls.plugin.path.string().c_str());
            const float close_x = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - button_size;
            if (ImGui::GetCursorPosX() < close_x) ImGui::SameLine(close_x);
            if (ImGui::Button(ICON_MS_CLOSE "##close_scene_plugin", ImVec2{button_size, button_size})) {
                application.clear_imgui_rgba8_images("scene-control-image://");
                controls = ControlsState{};
                scene_instance.close();
                set_scene_status(status, "Closed scene plugin controls", false);
                return true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("Close Scene");

            const std::string phase = control_state != nullptr ? control_state->phase : phase_text(controls.phase);
            const std::string headline = control_state != nullptr ? control_state->headline : controls.plugin.title;
            draw_status_pill(phase);
            if (!headline.empty()) {
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::AlignTextToFramePadding();
                ImGui::TextWrapped("%s", headline.c_str());
                if (control_state != nullptr && !control_state->detail.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", control_state->detail.c_str());
            }
            if (!controls.active_title.empty() && controls.active_title != controls.plugin.title) ImGui::TextColored(ImVec4{0.55f, 0.62f, 0.70f, 1.0f}, "%s", controls.active_title.c_str());
            return false;
        }

        void draw_controls_panel(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls) {
            if (controls.phase == SceneControlsPhase::None) {
                ImGui::TextDisabled("%s", "No scene plugin controls");
                return;
            }

            std::optional<ControlState> control_state{};
            if (controls.phase == SceneControlsPhase::Active || controls.phase == SceneControlsPhase::Error) {
                try {
                    if (scene_instance.has_controls()) control_state = scene_instance.control_state();
                } catch (const std::exception& error) {
                    controls.phase = SceneControlsPhase::Error;
                    controls.error = error.what();
                }
            }

            const ControlState* state = control_state.has_value() ? &*control_state : nullptr;
            if (draw_header(application, scene_instance, status, controls, state)) return;
            if (!controls.error.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controls.error.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (!control_state.has_value()) {
                static_cast<void>(draw_open_controls(application, scene_instance, status, controls));
                return;
            }

            if (draw_summary(*control_state)) {
                ImGui::Spacing();
                ImGui::Separator();
            }

            for (const ControlSection& section : controls.plugin.sections) {
                const bool has_actions = action_section_has_editors(controls.actions, section.id);
                const bool has_settings = setting_section_has_editors(controls.settings, section.id);
                const bool has_metrics = section_has_detail_metrics(*control_state, section.id);
                if (!has_actions && !has_settings && !has_metrics) continue;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", section.label.c_str());
                if (has_actions) draw_action_section(scene_instance, status, controls, *control_state, section.id);
                if (has_settings) draw_settings(scene_instance, status, controls, section.id);
                if (has_metrics) draw_detail_metrics(*control_state, section.id);
            }
        }

        [[nodiscard]] std::string overlay_text(const ControlState& state) {
            std::string text = state.phase;
            if (!state.headline.empty()) text += std::format(" | {}", state.headline);
            const std::vector<const ControlMetric*> metrics = control_metrics_with_placement(state, ControlPlacementViewportOverlay);
            std::size_t shown{};
            for (const ControlMetric* metric : metrics) {
                if (shown >= 5u) break;
                text += std::format(" | {} {}", metric->label, metric->value);
                ++shown;
            }
            constexpr std::size_t max_text_size = 220u;
            if (text.size() > max_text_size) text = text.substr(0u, max_text_size - 3u) + "...";
            return text;
        }

        void draw_controls_overlay(Scene& scene_instance, ControlsState& controls, const ImVec2 viewport_position, const ImVec2 viewport_size) {
            if (controls.phase == SceneControlsPhase::None || !scene_instance.has_controls()) return;
            ControlState state{};
            try {
                state = scene_instance.control_state();
            } catch (const std::exception& error) {
                controls.phase = SceneControlsPhase::Error;
                controls.error = error.what();
                return;
            }
            const std::string text = overlay_text(state);
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

        void open_scene_files(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls, const std::span<const std::filesystem::path> paths) {
            if (paths.empty()) throw std::runtime_error("Drop a PBRT scene file or scene plugin to load it");
            if (paths.size() != 1u) throw std::runtime_error("Drop exactly one scene file or scene plugin");
            const std::filesystem::path& scene_path = paths.front();
            if (is_plugin_file(scene_path)) {
                application.clear_imgui_rgba8_images("scene-control-image://");
                ScenePluginInfo plugin = inspect_plugin(scene_path);
                set_scene_status(status, std::format("Opened scene plugin controls {}", plugin.title), false);
                scene_instance.close();
                begin_scene_controls(controls, std::move(plugin));
                application.open_command_popover("scene.controls");
                return;
            }
            scene_instance.open_pbrt_file(scene_path);
            set_scene_status(status, std::format("Loaded {}", scene_instance.descriptor().title), false);
            application.clear_imgui_rgba8_images("scene-control-image://");
            controls = ControlsState{};
            application.close_command_popover("scene.controls");
        }
    } // namespace

    SceneUi::SceneUi() : state(std::make_shared<State>()) {}
    SceneUi::SceneUi(SceneUi&& other) noexcept = default;
    SceneUi& SceneUi::operator=(SceneUi&& other) noexcept = default;
    SceneUi::~SceneUi() noexcept = default;

    std::shared_ptr<Scene> SceneUi::scene() const {
        return this->state->scene_instance;
    }

    std::shared_ptr<CameraWorkspace> SceneUi::camera_workspace() const {
        return this->state->camera_workspace;
    }

    WorkspaceTitle make_workspace_title(Scene& scene_instance, StatusState& status) {
        const SceneDescriptor* descriptor = scene_instance.has_descriptor() ? &scene_instance.descriptor() : nullptr;
        WorkspaceTitle title{
            .detail = descriptor != nullptr ? descriptor->title : "Untitled",
            .tooltip = descriptor == nullptr
                ? "Empty Scene\nDrop a PBRT scene file or scene plugin into the window to load it"
                : std::format("{}\n{}\nDrop a PBRT scene file or scene plugin into the window to replace it", descriptor->id, descriptor->kind == SceneKind::Static ? "Static" : "Dynamic"),
        };
        if (scene_status_visible(status)) {
            title.status_text = status.status_text;
            title.status_error = status.status_error;
        }
        return title;
    }

    void handle_timeline_shortcuts(Scene& scene_instance) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) return;
        if (ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) return;
        const std::shared_ptr<const Scene::Document> document = scene_instance.document();
        if (!document->timeline_enabled) return;
        const Scene::Timeline timeline = scene_instance.timeline();
        if (timeline.mode != Scene::TimelineMode::Playback && ImGui::IsKeyPressed(ImGuiKey_Space, false)) scene_instance.toggle_timeline_playback();
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) scene_instance.request_timeline_reset();
    }

    void SceneUi::register_to(Spectra& application) {
        const std::shared_ptr<State> state = this->state;
        application.set_workspace_title_provider([state] {
            return make_workspace_title(*state->scene_instance, state->status);
        });
        application.register_file_drop_handler(FileDropHandler{
            .id = "scene.file-drop",
            .title = "Scene File Drop",
            .owner_renderer = {},
            .handle = [application = &application, state](const std::span<const std::filesystem::path> paths) {
                try {
                    open_scene_files(*application, *state->scene_instance, state->status, state->controls, paths);
                } catch (const std::exception& error) {
                    set_scene_status(state->status, error.what(), true);
                }
                return true;
            },
        });
        application.register_command_popover(CommandPopover{
            .id = "scene.controls",
            .title = "Scene",
            .icon = ICON_MS_DATASET,
            .owner_renderer = {},
            .shortcut_label = "F9",
            .shortcut_key = ImGuiKey_F9,
            .draw = [application = &application, state] {
                draw_controls_panel(*application, *state->scene_instance, state->status, state->controls);
            },
        });
        application.register_viewport_overlay(ViewportOverlay{
            .id = "scene.controls-overlay",
            .title = "Scene Controls Overlay",
            .owner_renderer = {},
            .draw = [state](const ImVec2 viewport_position, const ImVec2 viewport_size) {
                handle_timeline_shortcuts(*state->scene_instance);
                draw_controls_overlay(*state->scene_instance, state->controls, viewport_position, viewport_size);
            },
        });
    }

    void SceneUi::open_startup_file(Spectra& application, const std::optional<std::string>& initial_scene_path) {
        if (!initial_scene_path.has_value()) return;
        const std::shared_ptr<State> state = this->state;
        const std::array<std::filesystem::path, 1u> paths{std::filesystem::path{*initial_scene_path}};
        open_scene_files(application, *state->scene_instance, state->status, state->controls, std::span<const std::filesystem::path>{paths.data(), paths.size()});
    }
} // namespace spectra::scene
