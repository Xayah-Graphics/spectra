module;

#include <material_symbols/IconsMaterialSymbols.h>

module spectra.scene.ui;

import imgui;
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
        std::optional<PluginInfo> pending_plugin{};
        std::vector<OptionEditor> open_options{};
        std::vector<ActionEditor> actions{};
        std::vector<SettingEditor> settings{};
        std::string error{};
        std::string editor_plugin_key{};
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

        [[nodiscard]] bool option_uses_slider(const ControlOptionSchema& schema) {
            return schema.kind == ControlOptionKind::Float && schema.presentation == ControlOptionPresentationSlider && schema.has_numeric_range;
        }

        [[nodiscard]] int slider_decimal_precision(const ControlOptionSchema& schema) {
            if (!std::isfinite(schema.numeric_step) || schema.numeric_step <= 0.0f) throw std::runtime_error(std::format("Scene option '{}' slider step must be finite and positive", schema.key));
            double scaled_step = std::abs(static_cast<double>(schema.numeric_step));
            for (int precision = 0; precision <= 9; ++precision) {
                const double rounded_step = std::round(scaled_step);
                if (std::abs(scaled_step - rounded_step) <= 1.0e-6 * std::max(1.0, std::abs(scaled_step))) return precision;
                scaled_step *= 10.0;
            }
            return 9;
        }

        [[nodiscard]] std::string slider_float_format(const ControlOptionSchema& schema) {
            return std::format("%.{}f", slider_decimal_precision(schema));
        }

        void snap_slider_value(OptionEditor& editor) {
            if (!std::isfinite(editor.schema.numeric_step) || editor.schema.numeric_step <= 0.0f) throw std::runtime_error(std::format("Scene option '{}' slider step must be finite and positive", editor.schema.key));
            const float offset = std::round((editor.float_value - editor.schema.numeric_min) / editor.schema.numeric_step) * editor.schema.numeric_step;
            editor.float_value = std::clamp(editor.schema.numeric_min + offset, editor.schema.numeric_min, editor.schema.numeric_max);
        }

        [[nodiscard]] OptionEditor make_option_editor(ControlOptionSchema schema) {
            OptionEditor editor{.schema = std::move(schema)};
            editor.enabled = editor.schema.required || !editor.schema.default_value.empty();
            switch (editor.schema.kind) {
            case ControlOptionKind::Bool:
                editor.bool_value = editor.schema.default_value.empty() ? false : parse_bool_text(editor.schema.default_value, std::format("Scene option '{}'", editor.schema.key));
                break;
            case ControlOptionKind::Float:
                if (editor.schema.default_value.empty()) editor.float_value = option_uses_slider(editor.schema) ? editor.schema.numeric_min : 0.0f;
                else editor.float_value = parse_float_text(editor.schema.default_value, std::format("Scene option '{}'", editor.schema.key));
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

        void begin_pending_plugin_controls(ControlsState& controls, PluginInfo plugin);

        [[nodiscard]] bool metric_is_primary(const ControlMetric& metric) {
            return (metric.display_flags & ControlMetricDisplayPrimary) != 0u;
        }

        [[nodiscard]] std::vector<const ControlMetric*> primary_metrics(const ControlState& state) {
            std::vector<const ControlMetric*> metrics{};
            for (const ControlMetric& metric : state.metrics)
                if (metric_is_primary(metric)) metrics.push_back(&metric);
            return metrics;
        }

        [[nodiscard]] std::vector<const ControlMetric*> detail_metrics_for_section(const ControlState& state, const std::string_view section_id) {
            std::vector<const ControlMetric*> metrics{};
            for (const ControlMetric& metric : state.metrics)
                if (!metric_is_primary(metric) && metric.section_id == section_id) metrics.push_back(&metric);
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
                if (option_uses_slider(editor.schema)) {
                    const std::string format = slider_float_format(editor.schema);
                    changed = ImGui::SliderFloat("##value", &editor.float_value, editor.schema.numeric_min, editor.schema.numeric_max, format.c_str()) || changed;
                    if (changed) snap_slider_value(editor);
                } else {
                    changed = ImGui::InputFloat("##value", &editor.float_value, 0.0f, 0.0f, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue) || changed;
                }
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

        [[nodiscard]] std::size_t option_section_count(const PluginInfo& plugin, const std::span<const OptionEditor> editors) {
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

        void draw_option_sections(const PluginInfo& plugin, const std::span<OptionEditor> editors, const bool compact) {
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
            if (option_uses_slider(editor.schema)) return true;
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

        bool execute_action(Scene& scene_instance, StatusState& status, ControlsState& controls, ActionEditor& editor);

        bool draw_action_button(Scene& scene_instance, StatusState& status, ControlsState& controls, ActionEditor& editor, const ControlState& control_state, const ImVec2 size) {
            ImGui::PushID(editor.action.id.c_str());
            const bool enabled = action_enabled(control_state, editor.action.id);
            const std::string_view disabled_reason = action_disabled_reason(control_state, editor.action.id);
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::Button(editor.action.label.c_str(), size);
            ImGui::EndDisabled();
            if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%.*s", static_cast<int>(disabled_reason.size()), disabled_reason.data());
            if (enabled && !editor.action.description.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) ImGui::SetTooltip("%s", editor.action.description.c_str());
            if (clicked) static_cast<void>(execute_action(scene_instance, status, controls, editor));
            ImGui::PopID();
            return clicked;
        }

        void draw_action_section(Scene& scene_instance, StatusState& status, ControlsState& controls, const PluginInfo& plugin, const ControlState& control_state, const std::string_view section_id) {
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
                draw_option_sections(plugin, editor->options, true);
                static_cast<void>(draw_action_button(scene_instance, status, controls, *editor, control_state, ImVec2{-1.0f, 0.0f}));
                ImGui::PopID();
                ImGui::Spacing();
            }
        }

        [[nodiscard]] bool setting_section_has_editors(const std::span<const SettingEditor> editors, const std::string_view section_id) {
            return std::ranges::any_of(editors, [section_id](const SettingEditor& editor) { return editor.schema.section_id == section_id; });
        }

        void draw_settings(Scene& scene_instance, StatusState& status, ControlsState& controls, std::string_view section_id);
        bool draw_open_controls(Scene& scene_instance, StatusState& status, ControlsState& controls, const PluginInfo& plugin);
        void draw_controls_panel(Scene& scene_instance, StatusState& status, ControlsState& controls);
        void draw_controls_overlay(Scene& scene_instance, StatusState& status, ControlsState& controls, ImVec2 viewport_position, ImVec2 viewport_size);
        void open_scene_files(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls, std::span<const std::filesystem::path> paths);
    } // namespace

    namespace {
        [[nodiscard]] std::string plugin_editor_key(const PluginInfo& plugin) {
            return std::format("{}|{}", plugin.id, plugin.path.string());
        }

        void build_action_and_setting_editors(ControlsState& controls, const PluginInfo& plugin) {
            controls.actions.clear();
            controls.actions.reserve(plugin.control_actions.size());
            for (const ControlAction& action : plugin.control_actions) controls.actions.push_back(make_action_editor(action));

            controls.settings.clear();
            controls.settings.reserve(plugin.control_settings.size());
            for (const ControlOptionSchema& schema : plugin.control_settings) controls.settings.push_back(make_setting_editor(schema, schema.default_value));

            controls.editor_plugin_key = plugin_editor_key(plugin);
        }

        void begin_pending_plugin_controls(ControlsState& controls, PluginInfo plugin) {
            controls.pending_plugin = std::move(plugin);
            const PluginInfo& pending_plugin = *controls.pending_plugin;
            controls.open_options.clear();
            controls.open_options.reserve(pending_plugin.open_options.size());
            for (const ControlOptionSchema& schema : pending_plugin.open_options) controls.open_options.push_back(make_option_editor(schema));

            controls.actions.clear();
            controls.settings.clear();
            controls.editor_plugin_key.clear();
            controls.error.clear();
            controls.phase = SceneControlsPhase::PluginLoaded;
        }

        void begin_active_plugin_controls(ControlsState& controls, const PluginInfo& plugin) {
            controls.pending_plugin.reset();
            controls.open_options.clear();
            build_action_and_setting_editors(controls, plugin);
            controls.error.clear();
            controls.phase = SceneControlsPhase::Active;
        }

        void ensure_active_plugin_controls(ControlsState& controls, const PluginInfo& plugin) {
            if (controls.editor_plugin_key == plugin_editor_key(plugin)) return;
            begin_active_plugin_controls(controls, plugin);
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
                const bool changed = draw_option_editor(editor.option, false);
                const bool finished_edit = ImGui::IsItemDeactivatedAfterEdit();
                if (!option_editor_should_commit(editor.option, changed)) continue;
                const std::string value = option_editor_value(editor.option);
                try {
                    scene_instance.update_control_setting(editor.schema.key, value);
                    editor.committed_value = value;
                    controls.phase = SceneControlsPhase::Active;
                    controls.error.clear();
                    if (!option_uses_slider(editor.option.schema) || finished_edit) set_scene_status(status, std::format("Updated {}", editor.schema.label), false);
                } catch (const std::exception& error) {
                    set_option_editor_value(editor.option, editor.committed_value);
                    controls.phase = SceneControlsPhase::Error;
                    controls.error = error.what();
                    set_scene_status(status, controls.error, true);
                }
                ImGui::Spacing();
            }
        }

        bool draw_open_controls(Scene& scene_instance, StatusState& status, ControlsState& controls, const PluginInfo& plugin) {
            if (!controls.open_options.empty()) draw_option_sections(plugin, controls.open_options, false);
            if (!ImGui::Button(plugin.open_action_label.c_str(), ImVec2{-1.0f, 0.0f})) return false;
            try {
                scene_instance.open_plugin(PluginOpenRequest{
                    .plugin_path = plugin.path,
                    .options = collect_options(controls.open_options),
                });
                const Descriptor& descriptor = scene_instance.descriptor();
                begin_active_plugin_controls(controls, scene_instance.plugin_info());
                set_scene_status(status, std::format("Loaded {}", descriptor.title), false);
                return true;
            } catch (const std::exception& error) {
                controls.phase = SceneControlsPhase::Error;
                controls.error = error.what();
                scene_instance.close();
                set_scene_status(status, controls.error, true);
                return false;
            }
        }

        void draw_header(Scene& scene_instance, const PluginInfo& plugin) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(plugin.title.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", plugin.path.string().c_str());
            if (scene_instance.has_descriptor()) {
                const Descriptor& descriptor = scene_instance.descriptor();
                if (descriptor.title != plugin.title) ImGui::TextColored(ImVec4{0.55f, 0.62f, 0.70f, 1.0f}, "%s", descriptor.title.c_str());
            }
        }

        void draw_controls_panel(Scene& scene_instance, StatusState& status, ControlsState& controls) {
            if (controls.phase == SceneControlsPhase::None) {
                ImGui::TextDisabled("%s", "No scene plugin controls");
                return;
            }

            const PluginInfo* plugin{};
            if (scene_instance.has_plugin_info()) {
                plugin = &scene_instance.plugin_info();
                ensure_active_plugin_controls(controls, *plugin);
            } else if (controls.pending_plugin.has_value()) {
                plugin = &*controls.pending_plugin;
            }
            if (plugin == nullptr) {
                controls = ControlsState{};
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

            draw_header(scene_instance, *plugin);
            if (!controls.error.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.36f, 1.0f}, "%s", controls.error.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (!control_state.has_value()) {
                if (scene_instance.has_plugin_info()) return;
                static_cast<void>(draw_open_controls(scene_instance, status, controls, *plugin));
                return;
            }

            for (const ControlSection& section : plugin->sections) {
                const bool has_actions = action_section_has_editors(controls.actions, section.id);
                const bool has_settings = setting_section_has_editors(controls.settings, section.id);
                if (!has_actions && !has_settings) continue;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", section.label.c_str());
                if (has_actions) draw_action_section(scene_instance, status, controls, *plugin, *control_state, section.id);
                if (has_settings) draw_settings(scene_instance, status, controls, section.id);
            }
        }

        void draw_hud_metric_row(const ControlMetric& metric) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", metric.label.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(control_metric_color(metric), "%s", metric.value.c_str());
        }

        void draw_hud_primary_metrics(const ControlState& state, const bool compact) {
            const std::vector<const ControlMetric*> metrics = primary_metrics(state);
            if (metrics.empty()) return;
            if (compact) {
                for (std::size_t index = 0u; index < std::min<std::size_t>(metrics.size(), 3u); ++index) {
                    if (index != 0u) ImGui::SameLine(0.0f, 10.0f);
                    ImGui::TextColored(control_metric_color(*metrics[index]), "%s %s", metrics[index]->label.c_str(), metrics[index]->value.c_str());
                }
                return;
            }
            constexpr int columns = 2;
            if (!ImGui::BeginTable("SceneStatusHudPrimary", columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_PadOuterX)) return;
            for (std::size_t index = 0u; index < metrics.size(); ++index) {
                if (index % static_cast<std::size_t>(columns) == 0u) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(static_cast<int>(index % static_cast<std::size_t>(columns)));
                ImGui::TextDisabled("%s", metrics[index]->label.c_str());
                ImGui::TextColored(control_metric_color(*metrics[index]), "%s", metrics[index]->value.c_str());
            }
            ImGui::EndTable();
        }

        void draw_hud_detail_metrics(const PluginInfo& plugin, const ControlState& state) {
            for (const ControlSection& section : plugin.sections) {
                const std::vector<const ControlMetric*> metrics = detail_metrics_for_section(state, section.id);
                if (metrics.empty()) continue;
                ImGui::Spacing();
                ImGui::TextDisabled("%s", section.label.c_str());
                if (!ImGui::BeginTable(section.id.c_str(), 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX)) continue;
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.48f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.52f);
                for (const ControlMetric* metric : metrics) draw_hud_metric_row(*metric);
                ImGui::EndTable();
            }
        }

        void draw_status_hud(const PluginInfo& plugin, const ControlState& state, const ImVec2 viewport_position, const ImVec2 viewport_size) {
            if (viewport_size.x < 180.0f || viewport_size.y < 120.0f) return;
            const bool compact = viewport_size.x < 380.0f || viewport_size.y < 220.0f;
            const float hud_width = std::min(420.0f, std::max(220.0f, viewport_size.x - 24.0f));
            const float hud_max_height = std::max(86.0f, viewport_size.y * 0.45f);
            ImGui::SetNextWindowPos(ImVec2{viewport_position.x + 12.0f, viewport_position.y + viewport_size.y - 12.0f}, ImGuiCond_Always, ImVec2{0.0f, 1.0f});
            ImGui::SetNextWindowSizeConstraints(ImVec2{std::min(hud_width, viewport_size.x - 24.0f), 0.0f}, ImVec2{hud_width, hud_max_height});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.035f, 0.047f, 0.060f, 0.88f});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.24f, 0.50f, 0.54f, 0.36f});
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("##SceneStatusHud", nullptr, flags)) {
                draw_status_pill(state.phase);
                if (!state.headline.empty()) {
                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::TextWrapped("%s", state.headline.c_str());
                }
                if (!compact && !state.detail.empty()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4{0.62f, 0.69f, 0.76f, 1.0f}, "%s", state.detail.c_str());
                }
                draw_hud_primary_metrics(state, compact);
                if (!compact) draw_hud_detail_metrics(plugin, state);
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }

        void draw_status_hud_error(const std::string_view message, const ImVec2 viewport_position, const ImVec2 viewport_size) {
            ControlState state{
                .phase = "Error",
                .headline = "Scene controls unavailable",
                .detail = std::string{message},
            };
            PluginInfo plugin{
                .sections = {ControlSection{.id = "error", .label = "Error"}},
            };
            draw_status_hud(plugin, state, viewport_position, viewport_size);
        }

        [[nodiscard]] const char* timeline_kind_label(const Scene::TimelineKind kind) {
            switch (kind) {
            case Scene::TimelineKind::Static: return "Static";
            case Scene::TimelineKind::Indexed: return "Indexed";
            }
            throw std::runtime_error("Scene timeline kind is invalid");
        }

        void set_timeline_error(StatusState& status, ControlsState& controls, const std::exception& error) {
            controls.phase = SceneControlsPhase::Error;
            controls.error = error.what();
            set_scene_status(status, controls.error, true);
        }

        void draw_timeline_overlay(Scene& scene_instance, StatusState& status, ControlsState& controls, const ImVec2 viewport_position, const ImVec2 viewport_size) {
            const Scene::Timeline timeline = scene_instance.timeline();
            const Scene::UpdateClock update = scene_instance.update_clock();
            const bool show_update_controls = update.descriptor.enabled;
            const bool show_timeline_controls = timeline.descriptor.kind == Scene::TimelineKind::Indexed;
            if (!show_update_controls && !show_timeline_controls) return;
            if (viewport_size.x < 260.0f || viewport_size.y < 140.0f) return;

            const float width = show_timeline_controls ? std::min(760.0f, viewport_size.x - 32.0f) : std::min(360.0f, viewport_size.x - 32.0f);
            ImGui::SetNextWindowPos(ImVec2{viewport_position.x + viewport_size.x * 0.5f, viewport_position.y + viewport_size.y - 14.0f}, ImGuiCond_Always, ImVec2{0.5f, 1.0f});
            ImGui::SetNextWindowSize(ImVec2{width, 0.0f}, ImGuiCond_Always);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 8.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.035f, 0.047f, 0.060f, 0.90f});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.24f, 0.50f, 0.54f, 0.36f});
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing;
            if (ImGui::Begin("##SceneTimelineOverlay", nullptr, flags)) {
                if (show_update_controls) {
                    const char* const play_label = update.running ? ICON_MS_PAUSE : ICON_MS_PLAY_ARROW;
                    if (ImGui::Button(play_label, ImVec2{32.0f, 0.0f})) {
                        try {
                            scene_instance.toggle_update_running();
                            controls.phase = SceneControlsPhase::Active;
                            controls.error.clear();
                            set_scene_status(status, update.running ? "Paused updates" : "Started updates", false);
                        } catch (const std::exception& error) {
                            set_timeline_error(status, controls, error);
                        }
                    }
                    ImGui::SameLine(0.0f, 10.0f);
                    ImGui::BeginDisabled(update.running);
                    if (ImGui::Button(ICON_MS_STEP_OVER, ImVec2{32.0f, 0.0f})) {
                        try {
                            scene_instance.step_update();
                            controls.phase = SceneControlsPhase::Active;
                            controls.error.clear();
                            set_scene_status(status, "Stepped updates", false);
                        } catch (const std::exception& error) {
                            set_timeline_error(status, controls, error);
                        }
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine(0.0f, 10.0f);
                    ImGui::TextDisabled("Updates");
                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("%s", update.running ? "Running" : "Paused");
                }
                if (show_timeline_controls) {
                    if (show_update_controls) ImGui::SameLine(0.0f, 14.0f);
                    ImGui::TextDisabled("%s", timeline_kind_label(timeline.descriptor.kind));
                    ImGui::SameLine(0.0f, 10.0f);
                    std::uint64_t frame_value = timeline.cursor.frame_index;
                    const std::uint64_t frame_min = 0u;
                    const std::uint64_t frame_max = timeline.descriptor.frame_count - 1u;
                    const float slider_width = std::max(120.0f, ImGui::GetContentRegionAvail().x - 118.0f);
                    ImGui::SetNextItemWidth(slider_width);
                    if (ImGui::SliderScalar("##SceneTimelineFrame", ImGuiDataType_U64, &frame_value, &frame_min, &frame_max, "%llu")) {
                        try {
                            scene_instance.seek_timeline_frame(frame_value);
                            controls.phase = SceneControlsPhase::Active;
                            controls.error.clear();
                            set_scene_status(status, std::format("Timeline frame {}", frame_value), false);
                        } catch (const std::exception& error) {
                            set_timeline_error(status, controls, error);
                        }
                    }
                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("%llu/%llu", static_cast<unsigned long long>(timeline.cursor.frame_index + 1u), static_cast<unsigned long long>(timeline.descriptor.frame_count));
                } else if (show_update_controls) {
                    ImGui::SameLine(0.0f, 14.0f);
                    ImGui::TextDisabled("%s", timeline_kind_label(timeline.descriptor.kind));
                    ImGui::SameLine(0.0f, 8.0f);
                    ImGui::Text("f0");
                }
            }
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }

        void draw_controls_overlay(Scene& scene_instance, StatusState& status, ControlsState& controls, const ImVec2 viewport_position, const ImVec2 viewport_size) {
            if (controls.phase == SceneControlsPhase::None || !scene_instance.has_controls() || !scene_instance.has_plugin_info()) return;
            if (controls.phase == SceneControlsPhase::Error && !controls.error.empty()) {
                draw_status_hud_error(controls.error, viewport_position, viewport_size);
                draw_timeline_overlay(scene_instance, status, controls, viewport_position, viewport_size);
                return;
            }
            ControlState state{};
            try {
                state = scene_instance.control_state();
            } catch (const std::exception& error) {
                controls.phase = SceneControlsPhase::Error;
                controls.error = error.what();
                draw_status_hud_error(controls.error, viewport_position, viewport_size);
                draw_timeline_overlay(scene_instance, status, controls, viewport_position, viewport_size);
                return;
            }
            draw_status_hud(scene_instance.plugin_info(), state, viewport_position, viewport_size);
            draw_timeline_overlay(scene_instance, status, controls, viewport_position, viewport_size);
        }

        void open_scene_files(Spectra& application, Scene& scene_instance, StatusState& status, ControlsState& controls, const std::span<const std::filesystem::path> paths) {
            if (paths.empty()) throw std::runtime_error("Drop a PBRT scene file or scene plugin to load it");
            if (paths.size() != 1u) throw std::runtime_error("Drop exactly one scene file or scene plugin");
            const std::filesystem::path& scene_path = paths.front();
            if (is_plugin_file(scene_path)) {
                PluginInfo plugin = inspect_plugin(scene_path);
                set_scene_status(status, std::format("Opened scene plugin controls {}", plugin.title), false);
                scene_instance.close();
                begin_pending_plugin_controls(controls, std::move(plugin));
                application.open_command_popover("scene.controls");
                return;
            }
            scene_instance.open_pbrt_file(scene_path);
            set_scene_status(status, std::format("Loaded {}", scene_instance.descriptor().title), false);
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
        const Descriptor* descriptor = scene_instance.has_descriptor() ? &scene_instance.descriptor() : nullptr;
        WorkspaceTitle title{
            .detail = descriptor != nullptr ? descriptor->title : "Untitled",
            .tooltip = descriptor == nullptr
                ? "Empty Scene\nDrop a PBRT scene file or scene plugin into the window to load it"
                : std::format("{}\n{}\nDrop a PBRT scene file or scene plugin into the window to replace it", descriptor->id, descriptor->kind == Kind::Static ? "Static" : "Dynamic"),
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
        if (!document->update.enabled) return;
        if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) scene_instance.toggle_update_running();
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
            .draw = [state] {
                draw_controls_panel(*state->scene_instance, state->status, state->controls);
            },
        });
        application.register_viewport_overlay(ViewportOverlay{
            .id = "scene.controls-overlay",
            .title = "Scene Controls Overlay",
            .owner_renderer = {},
            .draw = [state](const ImVec2 viewport_position, const ImVec2 viewport_size) {
                handle_timeline_shortcuts(*state->scene_instance);
                draw_controls_overlay(*state->scene_instance, state->status, state->controls, viewport_position, viewport_size);
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
