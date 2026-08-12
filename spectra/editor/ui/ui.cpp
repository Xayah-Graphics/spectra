module;

#include <imgui.h>
#include <imgui_internal.h>

#include <ImGuizmo.h>

module spectra.editor.ui;
import std;

namespace spectra {
    namespace {
        constexpr float top_strip_height       = 36.0f;
        constexpr float transform_tools_left   = 10.0f;
        constexpr float transform_tools_width  = 48.0f;
        constexpr float transform_tools_height = 136.0f;
        constexpr float floating_panel_top     = 52.0f;
        constexpr float floating_panel_right   = 12.0f;
        constexpr float floating_panel_width   = 320.0f;
        constexpr float transform_hud_height   = 250.0f;
        constexpr float inspector_gap          = 12.0f;
        constexpr float inspector_bottom       = 12.0f;

        enum class Icon : std::uint8_t {
            Translate,
            Rotate,
            Scale,
            Close,
            Play,
            Pause,
            Step,
            Reset,
        };

        struct FlatButtonInteraction {
            bool clicked{};
            bool hovered{};
            bool active{};
            ImVec2 minimum{};
            ImVec2 maximum{};
            ImU32 color{};
            ImU32 shadow{};
            float vertical_offset{};
        };

        void draw_floating_surface(ImDrawList& draw_list, const ImVec2 minimum, const ImVec2 maximum, const float rounding, const float alpha = 0.94f) {
            draw_list.PushClipRectFullScreen();
            draw_list.AddRectFilled(ImVec2{minimum.x + 3.0f, minimum.y + 4.0f}, ImVec2{maximum.x + 4.0f, maximum.y + 5.0f}, ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.28f}), rounding + 2.0f);
            draw_list.AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImVec4{0.025f, 0.035f, 0.045f, alpha}), rounding);
            draw_list.AddRect(minimum, maximum, ImGui::GetColorU32(ImVec4{0.40f, 0.49f, 0.57f, 0.24f}), rounding, ImDrawFlags_None, 1.0f);
            draw_list.PopClipRect();
        }

        [[nodiscard]] std::array<float, 16> column_major(const std::array<float, 16>& row_major) noexcept {
            std::array<float, 16> result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result[column * 4u + row] = row_major[row * 4u + column];
            return result;
        }

        [[nodiscard]] math::Transform row_major_transform(const std::array<float, 16>& column_matrix) noexcept {
            math::Transform result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result.matrix[row * 4u + column] = column_matrix[column * 4u + row];
            return result;
        }

        [[nodiscard]] const SceneEntityReference* active_entity(const EditorUi& editor) noexcept {
            return editor.context.viewport.view.selection.active ? &*editor.context.viewport.view.selection.active : nullptr;
        }

        [[nodiscard]] bool transform_editable(const EditorUi& editor, const SceneEntityReference entity) noexcept {
            if (entity.kind == SceneEntityKind::Volume) return !editor.context.dynamics.initialized() || !editor.context.dynamics.controls(scene::VolumeId{entity.id});
            if (entity.kind != SceneEntityKind::Instance && entity.kind != SceneEntityKind::AreaEmitter) return entity.kind == SceneEntityKind::Camera || entity.kind == SceneEntityKind::Light;
            const scene::InstanceId instance_id{entity.kind == SceneEntityKind::Instance ? entity.id : entity.owner};
            return !editor.context.dynamics.initialized() || !editor.context.dynamics.controls(instance_id);
        }

        [[nodiscard]] std::optional<math::Transform> entity_transform(const scene::Scene& source, const SceneEntityReference entity) noexcept {
            if (entity.kind == SceneEntityKind::Instance || entity.kind == SceneEntityKind::AreaEmitter) {
                const scene::InstanceId id{entity.kind == SceneEntityKind::Instance ? entity.id : entity.owner};
                return std::ranges::find(source.resources.instances, id, &scene::Instance::id)->transform;
            }
            if (entity.kind == SceneEntityKind::Camera) return std::ranges::find(source.resources.cameras, scene::CameraId{entity.id}, &scene::Camera::id)->transform;
            if (entity.kind == SceneEntityKind::Volume) return std::ranges::find(source.resources.volumes, scene::VolumeId{entity.id}, &scene::Volume::id)->transform;
            if (entity.kind != SceneEntityKind::Light) return std::nullopt;
            const scene::Light& light = *std::ranges::find(source.resources.lights, scene::LightId{entity.id}, &scene::Light::id);
            return std::visit(
                [](const auto& data) -> std::optional<math::Transform> {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                        return data.environment.transform;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                        return std::nullopt;
                    else
                        return data.transform;
                },
                light.data);
        }

        [[nodiscard]] const std::string& entity_name(const scene::Scene& source, const SceneEntityReference entity) noexcept {
            if (entity.kind == SceneEntityKind::Instance) return std::ranges::find(source.resources.instances, scene::InstanceId{entity.id}, &scene::Instance::id)->name;
            if (entity.kind == SceneEntityKind::Camera) return std::ranges::find(source.resources.cameras, scene::CameraId{entity.id}, &scene::Camera::id)->name;
            if (entity.kind == SceneEntityKind::Volume) return std::ranges::find(source.resources.volumes, scene::VolumeId{entity.id}, &scene::Volume::id)->name;
            if (entity.kind == SceneEntityKind::AreaEmitter || entity.kind == SceneEntityKind::Light) return std::ranges::find(source.resources.lights, scene::LightId{entity.id}, &scene::Light::id)->name;
            std::unreachable();
        }

        [[nodiscard]] const char* entity_kind_name(const SceneEntityReference entity) noexcept {
            if (entity.kind == SceneEntityKind::Instance) return "INSTANCE";
            if (entity.kind == SceneEntityKind::Camera) return "CAMERA";
            if (entity.kind == SceneEntityKind::Light) return "LIGHT";
            if (entity.kind == SceneEntityKind::AreaEmitter) return "AREA EMITTER";
            if (entity.kind == SceneEntityKind::Volume) return "VOLUME";
            return "ENTITY";
        }

        [[nodiscard]] FlatButtonInteraction flat_button(const char* identifier, const ImVec2 size, const ImVec4 semantic_color = {}, const bool dangerous = false, const float default_alpha = 0.68f) {
            FlatButtonInteraction interaction{};
            interaction.clicked = ImGui::InvisibleButton(identifier, size);
            interaction.hovered = ImGui::IsItemHovered();
            interaction.active  = ImGui::IsItemActive();
            interaction.minimum = ImGui::GetItemRectMin();
            interaction.maximum = ImGui::GetItemRectMax();
            const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
            ImVec4 color{0.88f, 0.91f, 0.95f, default_alpha};
            if (disabled)
                color.w = 0.28f / ImGui::GetStyle().DisabledAlpha;
            else if (dangerous && interaction.hovered)
                color = {0.94f, 0.35f, 0.35f, interaction.active ? 0.85f : 1.0f};
            else if (semantic_color.w > 0.0f) {
                color = semantic_color;
                if (interaction.active) color.w = 0.85f;
            } else if (interaction.active)
                color.w = 0.85f;
            else if (interaction.hovered)
                color.w = 1.0f;
            interaction.color           = ImGui::GetColorU32(color);
            interaction.shadow          = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, disabled ? color.w : std::min(color.w, 0.78f)});
            interaction.vertical_offset = interaction.active && !disabled ? 1.0f : 0.0f;
            return interaction;
        }

        [[nodiscard]] bool text_button(const char* identifier, const char* text, const ImVec2 size, const bool selected = false, const bool dangerous = false) {
            const FlatButtonInteraction interaction = flat_button(identifier, size, selected ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, dangerous);
            const ImVec2 text_size                  = ImGui::CalcTextSize(text);
            const ImVec2 text_position{
                interaction.minimum.x + (size.x - text_size.x) * 0.5f,
                interaction.minimum.y + (size.y - text_size.y) * 0.5f + interaction.vertical_offset,
            };
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->PushClipRect(interaction.minimum, interaction.maximum, true);
            draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, interaction.shadow, text);
            draw_list->AddText(text_position, interaction.color, text);
            draw_list->PopClipRect();
            if (selected) {
                const float center     = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                const float half_width = std::clamp(text_size.x, 28.0f, 36.0f) * 0.5f;
                draw_list->AddLine(ImVec2{center - half_width, interaction.maximum.y - 3.0f}, ImVec2{center + half_width, interaction.maximum.y - 3.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 2.0f);
            }
            return interaction.clicked;
        }

        [[nodiscard]] bool icon_button(const char* identifier, const ImVec2 size, const Icon icon, const char* text = nullptr, const ImVec4 semantic_color = {}, const bool selected = false, const bool dangerous = false, const float default_alpha = 0.68f) {
            const FlatButtonInteraction interaction = flat_button(identifier, size, semantic_color, dangerous, default_alpha);
            const ImVec2 text_size                  = text ? ImGui::CalcTextSize(text) : ImVec2{};
            const float content_width               = 14.0f + (text ? text_size.x + 4.0f : 0.0f);
            const ImVec2 center{
                interaction.minimum.x + (size.x - content_width) * 0.5f + 7.0f,
                interaction.minimum.y + size.y * 0.5f + interaction.vertical_offset,
            };
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            if (selected) {
                draw_list->AddRectFilled(interaction.minimum, interaction.maximum, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.13f}), 7.0f);
                draw_list->AddRectFilled(ImVec2{interaction.minimum.x + 1.0f, interaction.minimum.y + 8.0f}, ImVec2{interaction.minimum.x + 3.0f, interaction.maximum.y - 8.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 1.0f);
            }
            const auto draw_icon = [&](const ImVec2 icon_center, const ImU32 color) {
                if (icon == Icon::Translate) {
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y - 8.0f}, color, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 7.0f, icon_center.y - 8.0f}, ImVec2{icon_center.x + 2.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 5.0f, icon_center.y - 3.0f}, color);
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 7.0f}, color, 1.7f);
                    draw_list->AddLine(ImVec2{icon_center.x - 8.0f, icon_center.y + 7.0f}, ImVec2{icon_center.x - 8.0f, icon_center.y - 6.0f}, color, 1.7f);
                } else if (icon == Icon::Rotate) {
                    draw_list->PathArcTo(icon_center, 8.0f, 0.25f, 5.5f, 24);
                    draw_list->PathStroke(color, ImDrawFlags_None, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 7.0f, icon_center.y - 5.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y + 1.0f}, ImVec2{icon_center.x + 3.0f, icon_center.y - 1.0f}, color);
                } else if (icon == Icon::Scale) {
                    draw_list->AddLine(ImVec2{icon_center.x - 6.0f, icon_center.y + 6.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, color, 1.7f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 9.0f, icon_center.y + 3.0f}, ImVec2{icon_center.x - 3.0f, icon_center.y + 9.0f}, color, 1.0f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x + 3.0f, icon_center.y - 9.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y - 3.0f}, color, 1.0f);
                } else if (icon == Icon::Close) {
                    draw_list->AddLine(ImVec2{icon_center.x - 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 6.0f}, color, 1.6f);
                    draw_list->AddLine(ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x - 6.0f, icon_center.y + 6.0f}, color, 1.6f);
                } else if (icon == Icon::Play) {
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x - 5.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y}, ImVec2{icon_center.x - 5.0f, icon_center.y + 7.0f}, color);
                } else if (icon == Icon::Pause) {
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 6.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x - 2.0f, icon_center.y + 7.0f}, color, 1.0f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x + 2.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 6.0f, icon_center.y + 7.0f}, color, 1.0f);
                } else if (icon == Icon::Step) {
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x - 7.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 4.0f, icon_center.y}, ImVec2{icon_center.x - 7.0f, icon_center.y + 7.0f}, color);
                    draw_list->AddLine(ImVec2{icon_center.x + 7.0f, icon_center.y - 7.0f}, ImVec2{icon_center.x + 7.0f, icon_center.y + 7.0f}, color, 1.8f);
                } else {
                    draw_list->PathArcTo(icon_center, 7.0f, 0.55f, 5.7f, 24);
                    draw_list->PathStroke(color, ImDrawFlags_None, 1.7f);
                    draw_list->AddTriangleFilled(ImVec2{icon_center.x + 6.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y - 1.0f}, ImVec2{icon_center.x + 3.0f, icon_center.y - 1.0f}, color);
                }
            };
            draw_icon(ImVec2{center.x + 1.0f, center.y + 1.0f}, interaction.shadow);
            draw_icon(center, interaction.color);
            if (text) {
                const ImVec2 text_position{center.x + 11.0f, center.y - text_size.y * 0.5f};
                draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, interaction.shadow, text);
                draw_list->AddText(text_position, interaction.color, text);
            }
            return interaction.clicked;
        }
    } // namespace

    EditorUi::EditorUi(SceneDocument& document, SceneDiagnosticSettings& diagnostic_settings, DynamicsRuntime& dynamics, RenderEngine& render_engine, ViewportInteraction& viewport, ViewportPicker& picker, ImGuiBackend& imgui) noexcept : context{document, diagnostic_settings, dynamics, render_engine, viewport, picker, imgui} {}

    void EditorUi::notify(std::string message, const bool error) {
        this->controls.status       = std::move(message);
        this->controls.status_error = error;
    }

    EditorActions EditorUi::draw_editor_ui() {
        ImGuizmo::BeginFrame();
        EditorActions actions{};
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position         = viewport->Pos;
        const ImVec2 size             = viewport->Size;
        const float aspect            = size.x / size.y;
        const ImGuiIO& io             = ImGui::GetIO();
        if (!this->context.document.content.loaded) {
            this->controls.inspector_open = false;
            this->controls.parameter_drafts.clear();
            this->controls.reset_pending = false;
            this->handle_shortcuts(actions, aspect, false);
            this->draw_top_strip(position, size, actions);
            this->draw_status_toast(position, size);
            return actions;
        }
        actions.show_axes                  = ImGui::IsKeyDown(ImGuiKey_G) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && !io.WantTextInput;
        const SceneEntityReference* entity = active_entity(*this);
        const bool transform_visible       = entity && entity_transform(this->context.document.content.source, *entity).has_value();
        const bool editable                = entity && transform_editable(*this, *entity);

        this->synchronize_transform();
        this->handle_shortcuts(actions, aspect, true);
        this->draw_viewport(position, size, actions.show_axes, editable);
        this->draw_top_strip(position, size, actions);
        if (transform_visible) {
            this->draw_transform_tools(position, size, editable);
            this->draw_transform_hud(position, size, editable);
        }
        if (this->controls.inspector_open) this->draw_inspector(position, size);
        this->draw_status_toast(position, size);
        return actions;
    }
    void EditorUi::apply_dynamic_parameters(std::vector<scene::DynamicParameterSetting> parameters, const bool reset) {
        try {
            this->context.document.update_dynamic_system_parameters(this->context.document.content.source, this->controls.selected_dynamic_system, std::move(parameters));
            this->context.dynamics.apply_parameter_changes(this->controls.selected_dynamic_system, this->context.document.content.source.dynamic_setup->systems[this->controls.selected_dynamic_system].parameters, reset);
            this->controls.status                    = reset ? "Parameters applied and Dynamic Setup reset" : "Parameter applied";
            this->controls.status_error              = false;
            this->controls.observed_dynamic_revision = this->context.document.content.source.revision().number;
            if (reset) {
                this->controls.parameter_drafts.clear();
                this->controls.reset_pending = false;
            }
        } catch (const std::exception& error) {
            this->controls.status       = error.what();
            this->controls.status_error = true;
        }
    }

    bool EditorUi::pointer_over_interface(const ImVec2 position, const ImVec2 size, const bool show_axes) const noexcept {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.y <= position.y + top_strip_height + 5.0f) return true;
        if (show_axes && mouse.x >= position.x + size.x - 116.0f && mouse.y <= position.y + 126.0f) return true;
        const SceneEntityReference* entity = active_entity(*this);
        const bool transform_visible       = entity && entity_transform(this->context.document.content.source, *entity).has_value();
        if (transform_visible) {
            const float tools_top = position.y + size.y * 0.5f - transform_tools_height * 0.5f;
            if (mouse.x >= position.x + transform_tools_left && mouse.x <= position.x + transform_tools_left + transform_tools_width && mouse.y >= tools_top && mouse.y <= tools_top + transform_tools_height) return true;
            if (mouse.x >= position.x + size.x - floating_panel_right - floating_panel_width && mouse.x <= position.x + size.x - floating_panel_right && mouse.y >= position.y + floating_panel_top && mouse.y <= position.y + floating_panel_top + transform_hud_height) return true;
        }
        const float inspector_top = transform_visible ? floating_panel_top + transform_hud_height + inspector_gap : floating_panel_top;
        if (this->controls.inspector_open && mouse.x >= position.x + size.x - floating_panel_right - floating_panel_width && mouse.x <= position.x + size.x - floating_panel_right && mouse.y >= position.y + inspector_top && mouse.y <= position.y + size.y - inspector_bottom) return true;
        return false;
    }

    void EditorUi::synchronize_transform() {
        const SceneEntityReference* entity = active_entity(*this);
        if (!entity) {
            this->controls.transform_entity.reset();
            return;
        }
        if (this->controls.transform_drag_active || this->controls.gizmo_active) return;
        const bool editable                            = transform_editable(*this, *entity);
        const scene::Scene& transform_scene            = editable ? this->context.document.content.source : this->context.document.content.evaluated;
        const std::optional<math::Transform> transform = entity_transform(transform_scene, *entity);
        if (!transform) {
            this->controls.transform_entity.reset();
            return;
        }
        const std::uint64_t revision = transform_scene.revision().number;
        if (this->controls.transform_entity == *entity && this->controls.transform_revision == revision && this->controls.transform_editable == editable) return;
        std::array<float, 16> matrix = column_major(transform->matrix);
        ImGuizmo::DecomposeMatrixToComponents(matrix.data(), this->controls.translation.data(), this->controls.rotation.data(), this->controls.scale.data());
        this->controls.transform_editable = editable;
        this->controls.transform_entity   = *entity;
        this->controls.transform_revision = revision;
    }

    void EditorUi::apply_transform(const SceneEntityReference entity, math::Transform transform) {
        if (entity.kind == SceneEntityKind::Instance || entity.kind == SceneEntityKind::AreaEmitter) {
            const scene::InstanceId instance_id{entity.kind == SceneEntityKind::Instance ? entity.id : entity.owner};
            this->context.document.update_transform(this->context.document.content.source, instance_id, transform);
            this->context.document.update_transform(this->context.document.content.evaluated, instance_id, std::move(transform));
        } else if (entity.kind == SceneEntityKind::Camera) {
            this->context.document.update_camera_transform(this->context.document.content.source, scene::CameraId{entity.id}, transform);
            this->context.document.update_camera_transform(this->context.document.content.evaluated, scene::CameraId{entity.id}, std::move(transform));
        } else if (entity.kind == SceneEntityKind::Light) {
            this->context.document.update_light_transform(this->context.document.content.source, scene::LightId{entity.id}, transform);
            this->context.document.update_light_transform(this->context.document.content.evaluated, scene::LightId{entity.id}, std::move(transform));
        } else if (entity.kind == SceneEntityKind::Volume) {
            this->context.document.update_volume_transform(this->context.document.content.source, scene::VolumeId{entity.id}, transform);
            this->context.document.update_volume_transform(this->context.document.content.evaluated, scene::VolumeId{entity.id}, std::move(transform));
        }
    }

    void EditorUi::transform_row(const char* identifier, const char* title, std::array<float, 3>& value, const float speed, const char* format) {
        constexpr std::array axis_names{"X", "Y", "Z"};
        constexpr std::array axis_colors{
            ImVec4{0.94f, 0.34f, 0.32f, 1.0f},
            ImVec4{0.34f, 0.82f, 0.48f, 1.0f},
            ImVec4{0.30f, 0.58f, 0.96f, 1.0f},
        };
        ImGui::PushID(identifier);
        ImGui::TextDisabled("%s", title);
        const float spacing         = 4.0f;
        const float component_width = (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
        for (std::size_t component = 0; component != value.size(); ++component) {
            ImGui::PushID(static_cast<int>(component));
            const ImVec2 axis_minimum = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2{22.0f, 28.0f});
            const ImVec2 axis_maximum = ImGui::GetItemRectMax();
            ImDrawList* draw_list     = ImGui::GetWindowDrawList();
            ImVec4 axis_background    = axis_colors[component];
            axis_background.w         = 0.18f;
            draw_list->AddRectFilled(axis_minimum, axis_maximum, ImGui::GetColorU32(axis_background), 6.0f, ImDrawFlags_RoundCornersLeft);
            const ImVec2 axis_text_size = ImGui::CalcTextSize(axis_names[component]);
            draw_list->AddText(ImVec2{axis_minimum.x + (22.0f - axis_text_size.x) * 0.5f, axis_minimum.y + (28.0f - axis_text_size.y) * 0.5f}, ImGui::GetColorU32(axis_colors[component]), axis_names[component]);
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::SetNextItemWidth(component_width - 22.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{5.0f, 6.0f});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.075f, 0.095f, 0.115f, 0.94f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.11f, 0.14f, 0.17f, 0.98f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.10f, 0.18f, 0.21f, 1.0f});
            const bool changed = ImGui::DragFloat("##Value", &value[component], speed, 0.0f, 0.0f, format);
            if (ImGui::IsItemActivated() && this->controls.transform_entity) this->controls.transform_drag_active = true;
            if (changed) {
                std::array<float, 16> matrix{};
                ImGuizmo::RecomposeMatrixFromComponents(this->controls.translation.data(), this->controls.rotation.data(), this->controls.scale.data(), matrix.data());
                this->apply_transform(*this->controls.transform_entity, row_major_transform(matrix));
            }
            if (ImGui::IsItemDeactivated() && this->controls.transform_drag_active) this->controls.transform_drag_active = false;
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGui::PopID();
            if (component + 1 != value.size()) ImGui::SameLine(0.0f, spacing);
        }
        ImGui::PopID();
    }

    void EditorUi::handle_shortcuts(EditorActions& actions, const float aspect, const bool inspector_available) {
        ImGuiIO& io = ImGui::GetIO();
        if (inspector_available && ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.WantTextInput) this->controls.inspector_open = !this->controls.inspector_open;
        if (this->controls.wireframe_restore_mode && !ImGui::IsKeyDown(ImGuiKey_W)) {
            actions.raster_display_mode = *this->controls.wireframe_restore_mode;
            this->controls.wireframe_restore_mode.reset();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            actions.exit_application = true;
            return;
        }
        if (io.WantTextInput) return;

        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) actions.open_scene_file = true;
        if (!this->context.document.content.loaded) return;
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) actions.save_scene = true;
        if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) actions.save_scene_as = true;
        const bool no_modifiers = !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_1, false)) actions.renderer = std::string{rasterizer_descriptor.id};
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_2, false)) actions.renderer = std::string{pathtracer_descriptor.id};
        if (this->context.render_engine.selected_descriptor() == rasterizer_descriptor && no_modifiers && ImGui::IsKeyDown(ImGuiKey_W) && !this->controls.wireframe_restore_mode) {
            this->controls.wireframe_restore_mode = this->context.render_engine.raster_display_mode();
            actions.raster_display_mode           = RasterDisplayMode::Wireframe;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) this->context.viewport.frame_selection(aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) this->context.viewport.frame_scene(aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->context.viewport.view_axis({0.0f, 0.0f, 1.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->context.viewport.view_axis({1.0f, 0.0f, 0.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->context.viewport.view_axis({0.0f, 1.0f, 0.0f}, aspect);
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) this->context.viewport.view.source = this->context.viewport.view.source == CameraSource::Scene ? CameraSource::Viewport : CameraSource::Scene;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_B, false)) this->context.diagnostic_settings.selected_bounds = !this->context.diagnostic_settings.selected_bounds;
        if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_B, false)) this->context.diagnostic_settings.all_bounds = !this->context.diagnostic_settings.all_bounds;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_C, false)) this->context.diagnostic_settings.cameras = !this->context.diagnostic_settings.cameras;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_L, false)) this->context.diagnostic_settings.lights = !this->context.diagnostic_settings.lights;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_N, false)) this->context.diagnostic_settings.normals = !this->context.diagnostic_settings.normals;
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_T, false)) this->context.diagnostic_settings.tangents = !this->context.diagnostic_settings.tangents;
        if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            this->context.viewport.view.overlays_visible = !this->context.viewport.view.overlays_visible;
            this->context.diagnostic_settings.enabled    = this->context.viewport.view.overlays_visible;
        }
        if (this->context.dynamics.initialized() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            if (this->context.dynamics.running())
                this->context.dynamics.pause();
            else
                this->context.dynamics.start();
        }
    }

    void EditorUi::draw_orientation(const ImVec2 position, const ImVec2 size, const bool show_axes) {
        if (!show_axes) return;
        const ImVec2 center{position.x + size.x - 56.0f, position.y + 70.0f};
        const scene::CameraFrame frame = this->context.viewport.view.render_camera.frame();
        struct Axis {
            const char* label;
            math::Float3 world;
            ImVec4 color;
        };
        constexpr std::array axes{
            Axis{"X", {1.0f, 0.0f, 0.0f}, {0.95f, 0.28f, 0.24f, 1.0f}},
            Axis{"Y", {0.0f, 1.0f, 0.0f}, {0.30f, 0.83f, 0.38f, 1.0f}},
            Axis{"Z", {0.0f, 0.0f, 1.0f}, {0.29f, 0.50f, 0.96f, 1.0f}},
        };
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (const Axis& axis : axes) {
            const float x = axis.world.dot(frame.right);
            const float y = axis.world.dot(frame.up);
            const ImVec2 tip{center.x + x * 25.0f, center.y - y * 25.0f};
            ImGui::SetCursorScreenPos(ImVec2{tip.x - 11.0f, tip.y - 11.0f});
            ImGui::PushID(axis.label);
            const FlatButtonInteraction interaction = flat_button("##Axis", ImVec2{22.0f, 22.0f});
            if (interaction.clicked) this->context.viewport.view_axis(axis.world, size.x / size.y);
            ImGui::PopID();
            const ImVec2 rendered_tip{tip.x, tip.y + interaction.vertical_offset};
            const float radius = interaction.hovered ? 9.775f : 8.5f;
            const ImU32 shadow = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.72f});
            draw_list->AddLine(center, rendered_tip, shadow, 3.6f);
            draw_list->AddCircleFilled(rendered_tip, radius + 1.0f, shadow, 20);
            draw_list->AddLine(center, rendered_tip, ImGui::GetColorU32(axis.color), 2.0f);
            draw_list->AddCircleFilled(rendered_tip, radius, ImGui::GetColorU32(axis.color), 20);
            const ImVec2 text_size = ImGui::CalcTextSize(axis.label);
            const ImVec2 text_position{rendered_tip.x - text_size.x * 0.5f, rendered_tip.y - text_size.y * 0.5f};
            draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, shadow, axis.label);
            draw_list->AddText(text_position, IM_COL32_WHITE, axis.label);
        }
    }

    void EditorUi::draw_gizmo(const ImVec2 minimum, const ImVec2 size, const bool blocked, const bool transform_editable) {
        const SceneEntityReference* entity = active_entity(*this);
        if (!entity) return;
        const scene::Scene& transform_scene            = transform_editable ? this->context.document.content.source : this->context.document.content.evaluated;
        const std::optional<math::Transform> transform = entity_transform(transform_scene, *entity);
        if (!transform) return;
        math::Float3 pivot{};
        if (entity->kind == SceneEntityKind::Instance || entity->kind == SceneEntityKind::AreaEmitter) {
            const scene::InstanceId instance_id{entity->kind == SceneEntityKind::Instance ? entity->id : entity->owner};
            const std::optional<math::Bounds3> local_bounds = transform_scene.view().local_bounds(instance_id);
            if (local_bounds) pivot = local_bounds->center();
        }
        math::Transform to_pivot{};
        to_pivot.matrix[3]  = pivot.x;
        to_pivot.matrix[7]  = pivot.y;
        to_pivot.matrix[11] = pivot.z;
        math::Transform from_pivot{};
        from_pivot.matrix[3]                 = -pivot.x;
        from_pivot.matrix[7]                 = -pivot.y;
        from_pivot.matrix[11]                = -pivot.z;
        std::array<float, 16> matrix         = column_major((*transform * to_pivot).matrix);
        const scene::CameraMatrices matrices = this->context.viewport.view.render_camera.matrices();
        std::array<float, 16> view           = column_major(matrices.view);
        std::array<float, 16> projection     = column_major(matrices.projection);
        ImGuizmo::SetOrthographic(std::holds_alternative<scene::OrthographicCameraData>(this->context.viewport.view.render_camera.data));
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(minimum.x, minimum.y, size.x, size.y);
        ImGuizmo::Enable(!blocked && transform_editable);
        const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), this->controls.gizmo_operation, ImGuizmo::WORLD, matrix.data());
        if (changed) this->apply_transform(*entity, row_major_transform(matrix) * from_pivot);
        this->controls.gizmo_active = ImGuizmo::IsUsing();
        ImGuizmo::Enable(true);
    }

    void EditorUi::handle_viewport_input(const ImVec2 minimum, const ImVec2 size, const bool blocked) {
        const ImGuiIO& io = ImGui::GetIO();
        const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
        const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(minimum, maximum);
        if (!hovered || blocked || io.WantTextInput) {
            this->context.viewport.clear_hover();
            return;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            this->context.picker.cancel_selection_requests();
            this->context.viewport.clear_selection();
            return;
        }
        if (this->context.viewport.view.source == CameraSource::Viewport && !ImGuizmo::IsUsing()) {
            if (io.MouseWheel != 0.0f) this->context.viewport.zoom_viewport_camera(io.MouseWheel);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                if (io.KeyShift)
                    this->context.viewport.pan_viewport_camera(io.MouseDelta.x, io.MouseDelta.y, size.y);
                else
                    this->context.viewport.orbit_viewport_camera(io.MouseDelta.x, io.MouseDelta.y);
            }
        }
        const bool pick_surface = !ImGuizmo::IsOver() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        if (!pick_surface) {
            this->context.viewport.clear_hover();
            return;
        }
        const float x = std::clamp((io.MousePos.x - minimum.x) / size.x, 0.0f, 1.0f);
        const float y = std::clamp((io.MousePos.y - minimum.y) / size.y, 0.0f, 1.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
            this->context.picker.submit_pick(x, y, true, io.KeyShift);
        else
            this->context.picker.submit_pick(x, y, false, false);
    }

    void EditorUi::draw_viewport(const ImVec2 position, const ImVec2 size, const bool show_axes, const bool transform_editable) {
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin("##SpectraViewport", nullptr, flags);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddImage(static_cast<ImTextureID>(this->context.imgui.viewport_texture_id), position, ImVec2{position.x + size.x, position.y + size.y});
        ImGui::PushClipRect(position, ImVec2{position.x + size.x, position.y + size.y}, true);
        const bool blocked = this->pointer_over_interface(position, size, show_axes);
        this->draw_gizmo(position, size, blocked, transform_editable);
        this->draw_orientation(position, size, show_axes);
        this->handle_viewport_input(position, size, blocked);
        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2{position.x, position.y + size.y});
        ImGui::Dummy({});
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    float EditorUi::draw_render_status(const float right_edge, const std::optional<RenderProgress>& render_progress, const std::optional<PathTracerPreparationProgress>& pathtracer_preparation) {
        const bool dynamic = this->context.dynamics.initialized();
        if (!dynamic && !render_progress && !pathtracer_preparation) return right_edge;

        const float spacing    = ImGui::GetStyle().ItemSpacing.x;
        const auto button_size = [](const char* label) {
            const ImVec2 text_size = ImGui::CalcTextSize(label);
            const ImVec2 padding   = ImGui::GetStyle().FramePadding;
            return ImVec2{text_size.x + padding.x * 2.0f, 27.0f};
        };
        const auto draw_status_text = [](const std::string& text, const ImVec4 color = ImVec4{0.88f, 0.91f, 0.95f, 0.55f}) {
            const ImVec2 minimum   = ImGui::GetCursorScreenPos();
            const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
            const ImVec2 text_position{minimum.x, minimum.y + (27.0f - text_size.y) * 0.5f};
            ImGui::GetWindowDrawList()->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.55f}), text.c_str());
            ImGui::GetWindowDrawList()->AddText(text_position, ImGui::GetColorU32(color), text.c_str());
            ImGui::Dummy(ImVec2{text_size.x, 27.0f});
        };
        const auto preparation_status = [](const PathTracerPreparationProgress& progress) {
            const char* stage{};
            switch (progress.stage) {
            case PathTracerPreparationStage::LoadingShaders: stage = "loading shaders"; break;
            case PathTracerPreparationStage::CreatingRayTracingModules: stage = "creating RT shader modules"; break;
            case PathTracerPreparationStage::CompilingRayTracingPipeline: stage = "compiling GPU RT pipeline"; break;
            case PathTracerPreparationStage::CreatingComputeShaders: stage = "creating compute shaders"; break;
            case PathTracerPreparationStage::CreatingShaderBindingTable: stage = "creating shader binding table"; break;
            case PathTracerPreparationStage::CompilingSampler: stage = "compiling sampler"; break;
            case PathTracerPreparationStage::CompilingFilter: stage = "compiling film filter"; break;
            case PathTracerPreparationStage::CompilingTextures: stage = "compiling textures"; break;
            case PathTracerPreparationStage::CompilingMaterials: stage = "compiling materials"; break;
            case PathTracerPreparationStage::CompilingMedia: stage = "compiling media"; break;
            case PathTracerPreparationStage::CompilingLights: stage = "compiling lights"; break;
            case PathTracerPreparationStage::CompilingGeometry: stage = "compiling geometry"; break;
            case PathTracerPreparationStage::BuildingLightBvh: stage = "building light BVH"; break;
            case PathTracerPreparationStage::AssemblingScene: stage = "assembling scene"; break;
            case PathTracerPreparationStage::UploadingScene: stage = "uploading scene"; break;
            case PathTracerPreparationStage::AllocatingRenderSession: stage = "allocating render session"; break;
            case PathTracerPreparationStage::Ready: stage = "ready"; break;
            }
            if (progress.total != 0) return std::format("Path Tracer · {} · {} / {}", stage, progress.completed, progress.total);
            const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - progress.started).count();
            return std::format("Path Tracer · {} · {:.1f} s", stage, seconds);
        };

        if (!dynamic && pathtracer_preparation) {
            const std::string status = preparation_status(*pathtracer_preparation);
            const float status_start = right_edge - ImGui::CalcTextSize(status.c_str()).x - 8.0f;
            ImGui::SameLine(status_start);
            draw_status_text(status);
            return status_start;
        }

        const dynamics::SimulationTimeline timeline = dynamic ? this->context.dynamics.timeline() : dynamics::SimulationTimeline{};
        const std::string status                    = dynamic ? std::format("step {}  ·  {:.3f} s{}", timeline.step, timeline.seconds, pathtracer_preparation ? std::format("  ·  {}", preparation_status(*pathtracer_preparation)) : std::string{}) : std::format("{} / {} spp", render_progress->completed, render_progress->target);
        const bool simulation_playing               = dynamic && this->context.dynamics.running();
        const char* playback_label                  = dynamic ? simulation_playing ? "Pause" : "Play" : render_progress->paused ? "Resume" : "Pause";
        const char* secondary_label                 = dynamic ? "Step" : "Reset";
        const char* reset_label                     = "Reset";
        ImVec2 playback_size                        = button_size(playback_label);
        playback_size.x                             = std::max(button_size("Pause").x, button_size(dynamic ? "Play" : "Resume").x);
        const ImVec2 secondary_size                 = button_size(secondary_label);
        const ImVec2 reset_size                     = button_size(reset_label);
        const float status_width                    = ImGui::CalcTextSize(status.c_str()).x + playback_size.x + secondary_size.x + (dynamic ? reset_size.x + spacing : 0.0f) + spacing * 2.0f;
        const float status_start                    = right_edge - status_width - 8.0f;
        ImGui::SameLine(status_start);

        if (dynamic) {
            if (icon_button("##SimulationPlayback", playback_size, simulation_playing ? Icon::Pause : Icon::Play, playback_label, simulation_playing ? ImVec4{0.35f, 0.84f, 0.55f, 1.0f} : ImVec4{})) {
                if (simulation_playing)
                    this->context.dynamics.pause();
                else
                    this->context.dynamics.start();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            if (icon_button("##SimulationStep", secondary_size, Icon::Step, secondary_label)) this->context.dynamics.step();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            if (icon_button("##SimulationReset", reset_size, Icon::Reset, reset_label)) this->context.dynamics.reset();
            ImGui::EndDisabled();
            ImGui::SameLine();
            draw_status_text(status);
        } else {
            draw_status_text(status);
            ImGui::SameLine();
            if (icon_button("##PathPlayback", playback_size, render_progress->paused ? Icon::Play : Icon::Pause, playback_label, render_progress->paused ? ImVec4{0.91f, 0.72f, 0.29f, 1.0f} : ImVec4{})) this->context.render_engine.set_paused(!render_progress->paused);
            ImGui::SameLine();
            if (icon_button("##PathReset", secondary_size, Icon::Reset, secondary_label)) this->context.render_engine.reset();
        }
        return status_start;
    }

    void EditorUi::draw_top_strip(const ImVec2 position, const ImVec2 size, EditorActions& actions) {
        ImGui::SetNextWindowPos(ImVec2{position.x + 6.0f, position.y + 5.0f});
        ImGui::SetNextWindowSize(ImVec2{size.x - 12.0f, top_strip_height});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f, 4.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0f, 4.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##ApplicationStrip", nullptr, flags);
        const bool loaded                                                         = this->context.document.content.loaded;
        const std::optional<RenderProgress> render_progress                       = loaded ? this->context.render_engine.progress() : std::nullopt;
        const std::optional<PathTracerPreparationProgress> pathtracer_preparation = loaded ? this->context.render_engine.pathtracer_preparation() : std::nullopt;
        if ((render_progress && render_progress->completed < render_progress->target) || pathtracer_preparation) {
            const ImVec2 minimum = ImGui::GetWindowPos();
            const ImVec2 maximum{minimum.x + ImGui::GetWindowWidth(), minimum.y + ImGui::GetWindowHeight()};
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->PushClipRect(minimum, maximum, false);
            if (pathtracer_preparation && pathtracer_preparation->total == 0) {
                const float width         = maximum.x - minimum.x;
                const float segment_width = std::min(width * 0.18f, 180.0f);
                const float travel        = width + segment_width;
                const float segment_x     = minimum.x - segment_width + static_cast<float>(std::fmod(ImGui::GetTime() * 180.0, static_cast<double>(travel)));
                draw_list->AddRectFilled(ImVec2{segment_x, minimum.y}, ImVec2{segment_x + segment_width, maximum.y}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.10f}));
            } else {
                const float progress   = pathtracer_preparation ? static_cast<float>(pathtracer_preparation->completed) / static_cast<float>(pathtracer_preparation->total) : static_cast<float>(render_progress->completed) / static_cast<float>(render_progress->target);
                const float progress_x = minimum.x + (maximum.x - minimum.x) * std::clamp(progress, 0.0f, 1.0f);
                draw_list->AddRectFilled(minimum, ImVec2{progress_x, maximum.y}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.07f}));
                if (progress_x > minimum.x) draw_list->AddLine(ImVec2{progress_x, minimum.y + 2.0f}, ImVec2{progress_x, maximum.y - 2.0f}, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.72f}), 1.0f);
            }
            draw_list->PopClipRect();
        }

        std::string identity = loaded ? this->context.document.content.path.filename().string() : "Open Scene...";
        if (loaded && identity.empty()) identity = this->context.document.content.source.name;
        const float identity_width = std::clamp(ImGui::CalcTextSize(identity.c_str()).x + 24.0f, 96.0f, 240.0f);
        if (text_button("##SceneIdentity", identity.c_str(), ImVec2{identity_width, 27.0f})) {
            if (loaded)
                ImGui::OpenPopup("##SceneMenu");
            else
                actions.open_scene_file = true;
        }
        const float source_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        if (!loaded) {
            const float strip_offset_x  = ImGui::GetWindowPos().x - position.x;
            const float drag_start      = strip_offset_x + source_end + 4.0f;
            const float drag_end        = strip_offset_x + ImGui::GetWindowWidth();
            actions.window_drag_regions = {{{drag_start, 0.0f, drag_end, top_strip_height + 5.0f}, {}}};
            ImGui::End();
            ImGui::PopStyleVar(2);
            return;
        }
        if (this->context.document.content.modified) {
            const ImVec2 minimum           = ImGui::GetItemRectMin();
            const ImVec2 maximum           = ImGui::GetItemRectMax();
            const float visible_text_width = std::min(ImGui::CalcTextSize(identity.c_str()).x, identity_width - 20.0f);
            const float text_right         = minimum.x + (identity_width + visible_text_width) * 0.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2{std::min(text_right + 7.0f, maximum.x - 4.0f), (minimum.y + maximum.y) * 0.5f}, 2.5f, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 12);
        }
        if (ImGui::BeginPopup("##SceneMenu")) {
            if (ImGui::MenuItem("Open File...", "Ctrl+O")) actions.open_scene_file = true;
            if (ImGui::MenuItem("Reload")) actions.reload_scene = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Save", "Ctrl+S")) actions.save_scene = true;
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) actions.save_scene_as = true;
            ImGui::EndPopup();
        }

        const float center                = ImGui::GetWindowWidth() * 0.5f;
        const float right                 = ImGui::GetWindowWidth();
        constexpr float mode_button_width = 80.0f;
        constexpr std::array renderer_descriptors{rasterizer_descriptor, pathtracer_descriptor};
        const float mode_width = mode_button_width * static_cast<float>(renderer_descriptors.size()) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(renderer_descriptors.size() - 1u);
        const float mode_start = center - mode_width * 0.5f;
        ImGui::SameLine(mode_start);
        const RendererDescriptor selected_renderer = this->context.render_engine.selected_descriptor();
        for (std::size_t index = 0; index < renderer_descriptors.size(); ++index) {
            const RendererDescriptor renderer = renderer_descriptors[index];
            const std::string identifier      = std::format("##Renderer{}", renderer.id);
            if (text_button(identifier.c_str(), renderer.name.data(), ImVec2{mode_button_width, 27.0f}, selected_renderer == renderer)) actions.renderer = std::string{renderer.id};
            if (index + 1u < renderer_descriptors.size()) ImGui::SameLine();
        }

        constexpr float exposure_width = 84.0f;
        constexpr float edge_margin    = 4.0f;
        const float exposure_start     = right - edge_margin - exposure_width;
        const float status_start       = this->draw_render_status(exposure_start, render_progress, pathtracer_preparation);
        ImGui::SameLine(exposure_start);
        const FlatButtonInteraction exposure = flat_button("##Exposure", ImVec2{exposure_width, 27.0f});
        if (exposure.active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) this->controls.exposure = std::clamp(this->controls.exposure + ImGui::GetIO().MouseDelta.x * 0.05f, -20.0f, 20.0f);
        if (exposure.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) this->controls.exposure = 0.0f;
        const std::string exposure_text = std::format("{:+.2f} EV", this->controls.exposure);
        const ImVec2 exposure_text_size = ImGui::CalcTextSize(exposure_text.c_str());
        const ImVec2 exposure_text_position{
            exposure.minimum.x + (exposure_width - exposure_text_size.x) * 0.5f,
            exposure.minimum.y + (27.0f - exposure_text_size.y) * 0.5f + exposure.vertical_offset,
        };
        ImGui::GetWindowDrawList()->AddText(ImVec2{exposure_text_position.x + 1.0f, exposure_text_position.y + 1.0f}, exposure.shadow, exposure_text.c_str());
        ImGui::GetWindowDrawList()->AddText(exposure_text_position, exposure.color, exposure_text.c_str());
        const float strip_offset_x      = ImGui::GetWindowPos().x - position.x;
        const float left_drag_start     = strip_offset_x + source_end + 4.0f;
        const float mode_client_start   = strip_offset_x + mode_start;
        const float mode_client_end     = mode_client_start + mode_width;
        const float right_drag_start    = mode_client_end + 4.0f;
        const float status_client_start = strip_offset_x + status_start;
        actions.window_drag_regions     = {{
            {left_drag_start, 0.0f, std::max(left_drag_start, mode_client_start - 4.0f), top_strip_height + 5.0f},
            {right_drag_start, 0.0f, std::max(right_drag_start, status_client_start - 4.0f), top_strip_height + 5.0f},
        }};
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorUi::draw_transform_tools(const ImVec2 position, const ImVec2 size, const bool transform_editable) {
        ImGui::SetNextWindowPos(ImVec2{position.x + transform_tools_left, position.y + size.y * 0.5f - transform_tools_height * 0.5f});
        ImGui::SetNextWindowSize(ImVec2{transform_tools_width, transform_tools_height});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f, 7.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{0.0f, 4.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##TransformTools", nullptr, flags);
        draw_floating_surface(*ImGui::GetWindowDrawList(), ImGui::GetWindowPos(), ImVec2{ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight()}, 10.0f, 0.90f);
        ImGui::BeginDisabled(!transform_editable);
        const bool translating = this->controls.gizmo_operation == ImGuizmo::TRANSLATE;
        const bool rotating    = this->controls.gizmo_operation == ImGuizmo::ROTATE;
        const bool scaling     = this->controls.gizmo_operation == ImGuizmo::SCALE;
        if (icon_button("##Translate", ImVec2{36.0f, 38.0f}, Icon::Translate, nullptr, translating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, translating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::TRANSLATE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate");
        if (icon_button("##Rotate", ImVec2{36.0f, 38.0f}, Icon::Rotate, nullptr, rotating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, rotating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::ROTATE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate");
        if (icon_button("##Scale", ImVec2{36.0f, 38.0f}, Icon::Scale, nullptr, scaling ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, scaling, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::SCALE;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale");
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorUi::draw_transform_hud(const ImVec2 position, const ImVec2 size, const bool transform_editable) {
        const SceneEntityReference* entity = active_entity(*this);
        if (!entity || !entity_transform(this->context.document.content.source, *entity)) return;
        const std::string& name = entity_name(this->context.document.content.source, *entity);
        this->synchronize_transform();
        ImGui::SetNextWindowPos(ImVec2{position.x + size.x - floating_panel_right - floating_panel_width, position.y + floating_panel_top});
        ImGui::SetNextWindowSize(ImVec2{floating_panel_width, transform_hud_height});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f, 6.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##TransformHud", nullptr, flags);
        ImDrawList* draw_list        = ImGui::GetWindowDrawList();
        const ImVec2 window_position = ImGui::GetWindowPos();
        draw_floating_surface(*draw_list, window_position, ImVec2{window_position.x + ImGui::GetWindowWidth(), window_position.y + ImGui::GetWindowHeight()}, 10.0f);

        const ImVec2 header_position = ImGui::GetCursorScreenPos();
        const float header_width     = ImGui::GetContentRegionAvail().x;
        constexpr float status_width = 88.0f;
        const float name_right       = header_position.x + header_width - (transform_editable ? 0.0f : status_width + 8.0f);
        draw_list->PushClipRect(header_position, ImVec2{name_right, header_position.y + ImGui::GetTextLineHeight()}, true);
        draw_list->AddText(header_position, ImGui::GetColorU32(ImVec4{0.92f, 0.95f, 0.98f, 1.0f}), name.c_str());
        draw_list->PopClipRect();
        ImGui::Dummy(ImVec2{header_width, ImGui::GetTextLineHeight()});
        ImGui::TextDisabled("%s", entity_kind_name(*entity));
        if (!transform_editable) {
            const ImVec2 status_minimum{header_position.x + header_width - status_width, header_position.y - 2.0f};
            const ImVec2 status_maximum{status_minimum.x + status_width, status_minimum.y + 20.0f};
            draw_list->AddRectFilled(status_minimum, status_maximum, ImGui::GetColorU32(ImVec4{0.91f, 0.65f, 0.24f, 0.14f}), 10.0f);
            draw_list->AddRect(status_minimum, status_maximum, ImGui::GetColorU32(ImVec4{0.91f, 0.65f, 0.24f, 0.42f}), 10.0f);
            const char* status            = "SIMULATED";
            const ImVec2 status_text_size = ImGui::CalcTextSize(status);
            draw_list->AddText(ImVec2{status_minimum.x + (status_width - status_text_size.x) * 0.5f, status_minimum.y + (20.0f - status_text_size.y) * 0.5f}, ImGui::GetColorU32(ImVec4{0.96f, 0.74f, 0.36f, 1.0f}), status);
        }
        const ImVec2 separator = ImGui::GetCursorScreenPos();
        draw_list->AddLine(ImVec2{separator.x, separator.y + 2.0f}, ImVec2{separator.x + header_width, separator.y + 2.0f}, ImGui::GetColorU32(ImVec4{0.40f, 0.49f, 0.57f, 0.22f}));
        ImGui::Dummy(ImVec2{header_width, 8.0f});
        ImGui::BeginDisabled(!transform_editable);
        this->transform_row("Position", "POSITION", this->controls.translation, 0.01f, "%.3f");
        this->transform_row("Rotation", "ROTATION", this->controls.rotation, 0.1f, "%.1f\xc2\xb0");
        this->transform_row("Scale", "SCALE", this->controls.scale, 0.01f, "%.3f");
        ImGui::EndDisabled();
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorUi::draw_inspector(const ImVec2 position, const ImVec2 size) {
        if (this->controls.observed_dynamic_revision != this->context.document.content.source.revision().number && !ImGui::IsAnyItemActive()) {
            this->controls.observed_dynamic_revision = this->context.document.content.source.revision().number;
            this->controls.parameter_drafts.clear();
            this->controls.reset_pending = false;
        }

        const scene::DynamicSetup* setup = this->context.document.content.source.dynamic_setup ? &*this->context.document.content.source.dynamic_setup : nullptr;
        std::vector<std::size_t> parameter_systems{};
        if (setup)
            for (std::size_t index = 0; index < setup->systems.size(); ++index)
                if (const dynamics::ProviderDescriptor& provider = this->context.dynamics.provider_descriptor(setup->systems[index].provider_id); setup->systems[index].enabled && (!provider.parameters.empty() || !provider.telemetry.empty())) parameter_systems.emplace_back(index);
        if (!parameter_systems.empty() && std::ranges::find(parameter_systems, this->controls.selected_dynamic_system) == parameter_systems.end()) this->controls.selected_dynamic_system = parameter_systems.front();

        const SceneEntityReference* entity = active_entity(*this);
        const bool transform_visible       = entity && entity_transform(this->context.document.content.source, *entity).has_value();
        const float inspector_top          = transform_visible ? floating_panel_top + transform_hud_height + inspector_gap : floating_panel_top;
        ImGui::SetNextWindowPos(ImVec2{position.x + size.x - floating_panel_right - floating_panel_width, position.y + inspector_top});
        ImGui::SetNextWindowSize(ImVec2{floating_panel_width, size.y - inspector_top - inspector_bottom});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##Inspector", nullptr, flags);
        const ImVec2 inspector_position = ImGui::GetWindowPos();
        draw_floating_surface(*ImGui::GetWindowDrawList(), inspector_position, ImVec2{inspector_position.x + ImGui::GetWindowWidth(), inspector_position.y + ImGui::GetWindowHeight()}, 10.0f);
        ImGui::TextUnformatted("Inspector");
        ImGui::Separator();

        if (entity) {
            ImGui::TextDisabled("SCENE ENTITY");
            ImGui::TextUnformatted(entity_name(this->context.document.content.source, *entity).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s · %llu", entity_kind_name(*entity), entity->id);
            if (entity->kind == SceneEntityKind::Camera) {
                const scene::Camera& camera = *std::ranges::find(this->context.document.content.source.resources.cameras, scene::CameraId{entity->id}, &scene::Camera::id);
                std::visit(
                    [](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>)
                            ImGui::Text("Perspective · %.2f deg", data.vertical_fov);
                        else
                            ImGui::TextUnformatted("Orthographic");
                        ImGui::TextDisabled("Near %.5g · Far %.5g", data.near_plane, data.far_plane);
                        ImGui::TextDisabled("Focus %.5g · Lens %.5g", data.focal_distance, data.lens_radius);
                    },
                    camera.data);
            } else if (entity->kind == SceneEntityKind::Light || entity->kind == SceneEntityKind::AreaEmitter) {
                const scene::Light& light = *std::ranges::find(this->context.document.content.source.resources.lights, scene::LightId{entity->id}, &scene::Light::id);
                std::visit(
                    [entity](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight>)
                            ImGui::Text("Point · Scale %.5g", data.scale);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>)
                            ImGui::Text("Spot · %.3g / %.3g deg · Scale %.5g", data.cone_angle - data.cone_delta, data.cone_angle, data.scale);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>)
                            ImGui::Text("Distant · Scale %.5g", data.scale);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                            ImGui::Text("Diffuse Area · %s · Instance %llu", data.sidedness == scene::EmissionSidedness::Both ? "two-sided" : "front", entity->owner);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InfiniteLight>)
                            ImGui::Text("Infinite · Scale %.5g", data.scale);
                        else
                            ImGui::Text("Portal Infinite · %zu portals · Scale %.5g", data.portals.size(), data.environment.scale);
                    },
                    light.data);
            }
            ImGui::Spacing();
        }

        ImGui::TextDisabled("SCENE DIAGNOSTICS");
        SceneDiagnosticSettings& diagnostics = this->context.diagnostic_settings;
        ImGui::Checkbox("Enabled", &diagnostics.enabled);
        ImGui::Checkbox("Selected bounds  B", &diagnostics.selected_bounds);
        ImGui::Checkbox("All bounds  Shift+B", &diagnostics.all_bounds);
        ImGui::Checkbox("Pivots", &diagnostics.pivots);
        ImGui::Checkbox("Geometry edges", &diagnostics.geometry_edges);
        ImGui::Checkbox("Vertices", &diagnostics.vertices);
        ImGui::Checkbox("Normals  N", &diagnostics.normals);
        ImGui::Checkbox("Tangents  T", &diagnostics.tangents);
        ImGui::Checkbox("Orientation", &diagnostics.orientation);
        ImGui::Checkbox("Cameras  C", &diagnostics.cameras);
        ImGui::Indent();
        ImGui::BeginDisabled(!diagnostics.cameras);
        ImGui::Checkbox("Focal planes", &diagnostics.camera_focal_plane);
        ImGui::Checkbox("Lenses", &diagnostics.camera_lens);
        ImGui::EndDisabled();
        ImGui::Unindent();
        ImGui::Checkbox("Lights  L", &diagnostics.lights);
        ImGui::Checkbox("Area emitters", &diagnostics.area_emitters);
        ImGui::Checkbox("Volume bounds", &diagnostics.volume_bounds);
        ImGui::Checkbox("Volume grid", &diagnostics.volume_grid);
        ImGui::Checkbox("Medium boundaries", &diagnostics.medium_boundaries);
        const char* depth_modes[] = {"Tested", "X-Ray", "Overlay"};
        int depth_mode            = static_cast<int>(std::to_underlying(diagnostics.depth_mode));
        if (ImGui::Combo("Depth", &depth_mode, depth_modes, 3)) {
            diagnostics.depth_mode = static_cast<scene::VisualizationDepthMode>(depth_mode);
        }
        ImGui::DragFloat("Line width", &diagnostics.line_width, 0.1f, 0.25f, 8.0f, "%.2f px");
        ImGui::DragFloat("Point size", &diagnostics.point_size, 0.25f, 1.0f, 32.0f, "%.2f px");
        ImGui::DragFloat("Normal scale", &diagnostics.normal_scale, 0.01f, 0.001f, 100.0f, "%.4g");
        constexpr std::uint32_t minimum_sampling = 1;
        constexpr std::uint32_t maximum_sampling = 1024;
        ImGui::DragScalar("Attribute sampling", ImGuiDataType_U32, &diagnostics.attribute_sampling, 1.0f, &minimum_sampling, &maximum_sampling, "%u");
        ImGui::DragScalar("Volume grid sampling", ImGuiDataType_U32, &diagnostics.volume_grid_sampling, 1.0f, &minimum_sampling, &maximum_sampling, "%u");

        if (parameter_systems.empty()) {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        const scene::DynamicSetup& dynamic_setup = *setup;
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("DYNAMIC SYSTEM");

        if (parameter_systems.size() > 1) {
            if (ImGui::BeginCombo("##DynamicSystem", dynamic_setup.systems[this->controls.selected_dynamic_system].name.c_str())) {
                for (const std::size_t index : parameter_systems)
                    if (ImGui::Selectable(dynamic_setup.systems[index].name.c_str(), index == this->controls.selected_dynamic_system)) {
                        this->controls.selected_dynamic_system = index;
                        this->controls.parameter_drafts.clear();
                        this->controls.reset_pending = false;
                    }
                ImGui::EndCombo();
            }
        } else
            ImGui::TextUnformatted(dynamic_setup.systems[this->controls.selected_dynamic_system].name.c_str());

        const scene::DynamicSystem& scene_system     = dynamic_setup.systems[this->controls.selected_dynamic_system];
        const dynamics::ProviderDescriptor& provider = this->context.dynamics.provider_descriptor(scene_system.provider_id);
        const dynamics::TelemetrySnapshot& telemetry = this->context.dynamics.telemetry(this->controls.selected_dynamic_system);
        if (!telemetry.phase.empty()) ImGui::TextColored(ImVec4{0.96f, 0.72f, 0.32f, 1.0f}, "%s", telemetry.phase.c_str());
        if (!telemetry.headline.empty()) ImGui::TextWrapped("%s", telemetry.headline.c_str());
        if (!telemetry.message.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", telemetry.message.c_str());
            ImGui::Spacing();
        }
        if (this->controls.parameter_drafts.empty()) {
            this->controls.parameter_drafts.reserve(provider.parameters.size());
            for (const dynamics::ParameterDescriptor& descriptor : provider.parameters) {
                const auto configured = std::ranges::find(scene_system.parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
                this->controls.parameter_drafts.emplace_back(descriptor.id, configured == scene_system.parameters.end() ? descriptor.value : configured->value);
            }
        }
        const auto parameter_values = [this, &scene_system, &provider](const bool include_reset_drafts) {
            std::vector<scene::DynamicParameterSetting> values{};
            values.reserve(provider.parameters.size());
            for (std::size_t index = 0; index < provider.parameters.size(); ++index) {
                const dynamics::ParameterDescriptor& descriptor = provider.parameters[index];
                const auto stored                               = std::ranges::find(scene_system.parameters, descriptor.id, &scene::DynamicParameterSetting::parameter_id);
                scene::DynamicParameterValue value              = stored == scene_system.parameters.end() ? descriptor.value : stored->value;
                if (include_reset_drafts || descriptor.application_mode == dynamics::ParameterApplication::Live) value = this->controls.parameter_drafts[index].value;
                values.emplace_back(descriptor.id, value);
            }
            return values;
        };

        std::string parameter_section{};
        for (std::size_t parameter_index = 0; parameter_index < provider.parameters.size(); ++parameter_index) {
            const dynamics::ParameterDescriptor& parameter = provider.parameters[parameter_index];
            scene::DynamicParameterValue& value            = this->controls.parameter_drafts[parameter_index].value;
            if (parameter.section_id != parameter_section) {
                parameter_section  = parameter.section_id;
                const auto section = std::ranges::find(provider.sections, parameter_section, &dynamics::SectionDescriptor::id);
                ImGui::Spacing();
                ImGui::TextDisabled("PARAMETERS / %s", section == provider.sections.end() ? parameter_section.c_str() : section->name.c_str());
            }
            ImGui::PushID(static_cast<int>(parameter_index));
            ImGui::Text("%s", parameter.name.c_str());
            if (!parameter.unit.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", parameter.unit.c_str());
            }
            if (!parameter.description.empty()) ImGui::TextDisabled("%s", parameter.description.c_str());

            bool changed{};
            if (parameter.value.kind == scene::DynamicParameterKind::Boolean) {
                bool selected = value.integer != 0;
                changed       = ImGui::Checkbox("##Value", &selected);
                if (changed) value.integer = selected ? 1 : 0;
            } else if (parameter.value.kind == scene::DynamicParameterKind::Integer)
                changed = ImGui::DragScalar("##Value", ImGuiDataType_S64, &value.integer, static_cast<float>(parameter.step.integer), &parameter.minimum.integer, &parameter.maximum.integer, "%lld");
            else if (parameter.value.kind == scene::DynamicParameterKind::Float)
                changed = ImGui::DragScalar("##Value", ImGuiDataType_Double, value.floating.data(), static_cast<float>(parameter.step.floating[0]), parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.6g");
            else if (parameter.value.kind == scene::DynamicParameterKind::Float3)
                changed = ImGui::DragScalarN("##Value", ImGuiDataType_Double, value.floating.data(), 3, static_cast<float>(parameter.step.floating[0]), parameter.minimum.floating.data(), parameter.maximum.floating.data(), "%.5g");
            else {
                const char* preview = value.integer >= 0 && static_cast<std::size_t>(value.integer) < parameter.enumerators.size() ? parameter.enumerators[value.integer].c_str() : "";
                if (ImGui::BeginCombo("##Value", preview)) {
                    for (std::size_t option = 0; option < parameter.enumerators.size(); ++option)
                        if (ImGui::Selectable(parameter.enumerators[option].c_str(), value.integer == static_cast<std::int64_t>(option))) {
                            value.integer = static_cast<std::int64_t>(option);
                            changed       = true;
                        }
                    ImGui::EndCombo();
                }
            }

            if (parameter.application_mode == dynamics::ParameterApplication::ResetRequired) {
                ImGui::SameLine();
                ImGui::TextDisabled("reset");
                if (changed) this->controls.reset_pending = true;
            } else if ((changed && !ImGui::IsItemActive()) || ImGui::IsItemDeactivatedAfterEdit()) {
                std::vector<scene::DynamicParameterSetting> parameters = parameter_values(false);
                this->apply_dynamic_parameters(std::move(parameters), false);
                ImGui::PopID();
                ImGui::End();
                ImGui::PopStyleVar();
                return;
            }
            ImGui::PopID();
        }

        std::string telemetry_section{};
        for (std::size_t metric_index = 0; metric_index < provider.telemetry.size(); ++metric_index) {
            if (!telemetry.values[metric_index]) continue;
            const dynamics::TelemetryDescriptor& metric = provider.telemetry[metric_index];
            if (metric.section_id != telemetry_section) {
                telemetry_section  = metric.section_id;
                const auto section = std::ranges::find(provider.sections, telemetry_section, &dynamics::SectionDescriptor::id);
                ImGui::Spacing();
                ImGui::TextDisabled("TELEMETRY / %s", section == provider.sections.end() ? telemetry_section.c_str() : section->name.c_str());
            }
            const dynamics::TelemetryValue& value = *telemetry.values[metric_index];
            std::string formatted{};
            if (value.kind == dynamics::TelemetryKind::Boolean)
                formatted = value.integer == 0 ? "false" : "true";
            else if (value.kind == dynamics::TelemetryKind::Integer)
                formatted = std::to_string(value.integer);
            else if (value.kind == dynamics::TelemetryKind::Float)
                formatted = std::format("{:.6g}", value.floating[0]);
            else if (value.kind == dynamics::TelemetryKind::Float3)
                formatted = std::format("[{:+.5g}, {:+.5g}, {:+.5g}]", value.floating[0], value.floating[1], value.floating[2]);
            ImGui::TextUnformatted(metric.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s%s", formatted.c_str(), metric.unit.empty() ? "" : " ", metric.unit.c_str());
            if (metric.plot && !telemetry.history.empty()) {
                std::vector<float> samples{};
                samples.reserve(telemetry.history.size());
                for (const dynamics::TelemetrySample& sample : telemetry.history) {
                    const dynamics::TelemetryValue& historical = sample.values[metric_index];
                    if (historical.kind == dynamics::TelemetryKind::Boolean || historical.kind == dynamics::TelemetryKind::Integer)
                        samples.push_back(static_cast<float>(historical.integer));
                    else if (historical.kind == dynamics::TelemetryKind::Float)
                        samples.push_back(static_cast<float>(historical.floating[0]));
                    else
                        samples.push_back(static_cast<float>(std::sqrt(historical.floating[0] * historical.floating[0] + historical.floating[1] * historical.floating[1] + historical.floating[2] * historical.floating[2])));
                }
                ImGui::PlotLines("##History", samples.data(), static_cast<int>(samples.size()), 0, nullptr, std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), ImVec2{-1.0f, 44.0f});
            }
        }

        if (this->controls.reset_pending)
            if (text_button("##ApplySystemReset", "Apply & Reset", ImVec2{124.0f, 27.0f})) {
                std::vector<scene::DynamicParameterSetting> parameters = parameter_values(true);
                this->apply_dynamic_parameters(std::move(parameters), true);
            }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    void EditorUi::draw_status_toast(const ImVec2 position, const ImVec2 size) {
        std::string& status = this->controls.status;
        bool& status_error  = this->controls.status_error;
        if (status != this->controls.observed_status || status_error != this->controls.observed_status_error) {
            this->controls.observed_status       = status;
            this->controls.observed_status_error = status_error;
            this->controls.status_since          = ImGui::GetTime();
        }
        if (status.empty()) return;
        const double age = ImGui::GetTime() - this->controls.status_since;
        if (!status_error && age > 3.0) {
            status.clear();
            this->controls.observed_status.clear();
            return;
        }
        const float alpha      = status_error ? 1.0f : std::clamp(static_cast<float>((3.0 - age) / 0.35), 0.0f, 1.0f);
        const ImVec2 text_size = ImGui::CalcTextSize(status.c_str());
        const float width      = std::min(size.x - 24.0f, text_size.x + (status_error ? 54.0f : 30.0f));
        ImGui::SetNextWindowPos(ImVec2{position.x + (size.x - width) * 0.5f, position.y + 18.0f});
        ImGui::SetNextWindowSize(ImVec2{width, 38.0f});
        ImGui::SetNextWindowBgAlpha((status_error ? 0.96f : 0.88f) * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 8.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##StatusToast", nullptr, flags);
        ImGui::TextColored(status_error ? ImVec4{1.0f, 0.38f, 0.33f, 1.0f} : ImVec4{0.55f, 0.94f, 0.80f, 1.0f}, "%s", status.c_str());
        if (status_error) {
            ImGui::SameLine(ImGui::GetWindowWidth() - 26.0f);
            if (icon_button("##DismissError", ImVec2{18.0f, 18.0f}, Icon::Close, nullptr, {}, false, true)) {
                status.clear();
                this->controls.observed_status.clear();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

} // namespace spectra
