module;

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

module spectra.app.workspace_ui;

import spectra.scene;
import std;

namespace spectra::app {
    namespace {
        constexpr float top_strip_height = 36.0f;

        enum class Icon : std::uint8_t {
            Translate,
            Rotate,
            Scale,
            Capture,
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

        [[nodiscard]] std::array<float, 16> column_major(const std::array<float, 16>& row_major) noexcept {
            std::array<float, 16> result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result[column * 4u + row] = row_major[row * 4u + column];
            return result;
        }

        [[nodiscard]] scene::Transform row_major_transform(const std::array<float, 16>& column_matrix) noexcept {
            scene::Transform result{};
            for (std::uint32_t row = 0; row < 4; ++row)
                for (std::uint32_t column = 0; column < 4; ++column) result.matrix[row * 4u + column] = column_matrix[column * 4u + row];
            return result;
        }

        [[nodiscard]] const scene::Instance* selected_instance(const workspace::Workspace& workspace) noexcept {
            if (!workspace.selection.active) return nullptr;
            const std::vector<scene::Instance>::const_iterator found = std::ranges::find(workspace.scene.resources.instances, *workspace.selection.active, &scene::Instance::id);
            return found == workspace.scene.resources.instances.end() ? nullptr : std::to_address(found);
        }

        [[nodiscard]] std::optional<ImVec2> project(const scene::Float3 point, const scene::CameraMatrices& matrices, const ImVec2 minimum, const ImVec2 size) noexcept {
            const std::array<float, 4> clip{
                matrices.view_projection[0] * point.x + matrices.view_projection[1] * point.y + matrices.view_projection[2] * point.z + matrices.view_projection[3],
                matrices.view_projection[4] * point.x + matrices.view_projection[5] * point.y + matrices.view_projection[6] * point.z + matrices.view_projection[7],
                matrices.view_projection[8] * point.x + matrices.view_projection[9] * point.y + matrices.view_projection[10] * point.z + matrices.view_projection[11],
                matrices.view_projection[12] * point.x + matrices.view_projection[13] * point.y + matrices.view_projection[14] * point.z + matrices.view_projection[15],
            };
            if (clip[3] <= 0.0f) return std::nullopt;
            return ImVec2{
                minimum.x + (clip[0] / clip[3] * 0.5f + 0.5f) * size.x,
                minimum.y + (0.5f - clip[1] / clip[3] * 0.5f) * size.y,
            };
        }

        [[nodiscard]] FlatButtonInteraction flat_button(
            const char* identifier,
            const ImVec2 size,
            const ImVec4 semantic_color = {},
            const bool dangerous = false,
            const float default_alpha = 0.68f) {
            FlatButtonInteraction interaction{};
            interaction.clicked = ImGui::InvisibleButton(identifier, size);
            interaction.hovered = ImGui::IsItemHovered();
            interaction.active = ImGui::IsItemActive();
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
            interaction.color = ImGui::GetColorU32(color);
            interaction.shadow = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, disabled ? color.w : std::min(color.w, 0.78f)});
            interaction.vertical_offset = interaction.active && !disabled ? 1.0f : 0.0f;
            if (ImGui::IsItemFocused()) {
                const float center = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2{center - 6.0f, interaction.maximum.y - 1.0f},
                    ImVec2{center + 6.0f, interaction.maximum.y - 1.0f},
                    ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, 0.85f}),
                    1.0f);
            }
            return interaction;
        }

        [[nodiscard]] bool text_button(
            const char* identifier,
            const char* text,
            const ImVec2 size,
            const bool selected = false,
            const bool dangerous = false) {
            const FlatButtonInteraction interaction = flat_button(
                identifier,
                size,
                selected ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{},
                dangerous);
            const ImVec2 text_size = ImGui::CalcTextSize(text);
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
                const float center = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                const float half_width = std::clamp(text_size.x, 28.0f, 36.0f) * 0.5f;
                draw_list->AddLine(
                    ImVec2{center - half_width, interaction.maximum.y - 3.0f},
                    ImVec2{center + half_width, interaction.maximum.y - 3.0f},
                    ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}),
                    2.0f);
            }
            return interaction.clicked;
        }

        void draw_scene_camera(const workspace::Workspace& workspace, const ImVec2 minimum, const ImVec2 size, ImDrawList& draw_list) {
            if (!workspace.overlays_visible || workspace.camera_source == workspace::CameraSource::Scene) return;
            const scene::CameraResource& camera = workspace.scene.camera();
            const scene::CameraFrame frame = camera.frame();
            constexpr float distance = 0.5f;
            const scene::Float3 plane_center = frame.position + frame.forward * distance;
            std::array<scene::Float2, 4> screen_corners{};
            if (const scene::PerspectiveCameraData* perspective = std::get_if<scene::PerspectiveCameraData>(&camera.data)) {
                const float scale = std::tan(perspective->vertical_fov * std::numbers::pi_v<float> / 360.0f) * distance;
                screen_corners = {{
                    {perspective->screen.minimum.x * scale, perspective->screen.minimum.y * scale},
                    {perspective->screen.maximum.x * scale, perspective->screen.minimum.y * scale},
                    {perspective->screen.maximum.x * scale, perspective->screen.maximum.y * scale},
                    {perspective->screen.minimum.x * scale, perspective->screen.maximum.y * scale},
                }};
            } else {
                const scene::CameraScreen& screen = std::get<scene::OrthographicCameraData>(camera.data).screen;
                screen_corners = {{{screen.minimum.x, screen.minimum.y}, {screen.maximum.x, screen.minimum.y}, {screen.maximum.x, screen.maximum.y}, {screen.minimum.x, screen.maximum.y}}};
            }
            const bool orthographic = std::holds_alternative<scene::OrthographicCameraData>(camera.data);
            std::array<scene::Float3, 4> bases{};
            std::array<scene::Float3, 4> corners{};
            for (std::size_t index = 0; index < corners.size(); ++index) {
                bases[index] = orthographic
                    ? frame.position + frame.right * screen_corners[index].x + frame.up * screen_corners[index].y
                    : frame.position;
                corners[index] = plane_center + frame.right * screen_corners[index].x + frame.up * screen_corners[index].y;
            }
            const scene::CameraMatrices matrices = workspace.active_camera().matrices();
            const ImU32 color = ImGui::GetColorU32(ImVec4{1.0f, 0.67f, 0.16f, 0.72f});
            for (std::size_t index = 0; index < corners.size(); ++index) {
                const std::optional<ImVec2> base = project(bases[index], matrices, minimum, size);
                const std::optional<ImVec2> next_base = project(bases[(index + 1) % bases.size()], matrices, minimum, size);
                const std::optional<ImVec2> corner = project(corners[index], matrices, minimum, size);
                const std::optional<ImVec2> next = project(corners[(index + 1) % corners.size()], matrices, minimum, size);
                if (base && corner) draw_list.AddLine(*base, *corner, color, 1.4f);
                if (orthographic && base && next_base) draw_list.AddLine(*base, *next_base, color, 1.4f);
                if (corner && next) draw_list.AddLine(*corner, *next, color, 1.4f);
            }
        }

        [[nodiscard]] bool icon_button(
            const char* identifier,
            const ImVec2 size,
            const Icon icon,
            const char* text = nullptr,
            const ImVec4 semantic_color = {},
            const bool selected = false,
            const bool dangerous = false,
            const float default_alpha = 0.68f) {
            const FlatButtonInteraction interaction = flat_button(identifier, size, semantic_color, dangerous, default_alpha);
            const ImVec2 text_size = text ? ImGui::CalcTextSize(text) : ImVec2{};
            const float content_width = 14.0f + (text ? text_size.x + 4.0f : 0.0f);
            const ImVec2 center{
                interaction.minimum.x + (size.x - content_width) * 0.5f + 7.0f,
                interaction.minimum.y + size.y * 0.5f + interaction.vertical_offset,
            };
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
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
                } else if (icon == Icon::Capture) {
                    draw_list->AddRect(ImVec2{icon_center.x - 9.0f, icon_center.y - 6.0f}, ImVec2{icon_center.x + 9.0f, icon_center.y + 7.0f}, color, 2.0f, ImDrawFlags_None, 1.6f);
                    draw_list->AddCircle(icon_center, 3.5f, color, 16, 1.5f);
                    draw_list->AddRectFilled(ImVec2{icon_center.x - 5.0f, icon_center.y - 9.0f}, ImVec2{icon_center.x + 1.0f, icon_center.y - 6.0f}, color, 1.0f);
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
            if (selected) {
                const float item_center = (interaction.minimum.x + interaction.maximum.x) * 0.5f;
                draw_list->AddLine(
                    ImVec2{item_center - 8.0f, interaction.maximum.y - 3.0f},
                    ImVec2{item_center + 8.0f, interaction.maximum.y - 3.0f},
                    ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}),
                    2.0f);
            }
            return interaction.clicked;
        }
    } // namespace

    struct WorkspaceUi::State {
        bool expanded{};
        ImGuizmo::OPERATION gizmo_operation{ImGuizmo::TRANSLATE};
        bool gizmo_using{};
        bool transform_interaction{};
        std::optional<scene::InstanceId> transform_instance{};
        std::uint64_t transform_revision{};
        std::array<float, 3> translation{};
        std::array<float, 3> rotation{};
        std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
        std::string observed_status{};
        bool observed_status_error{};
        double status_since{};

        [[nodiscard]] bool path_progress_visible(const workspace::Workspace& workspace) const noexcept {
            return workspace.provider == workspace::SceneProvider::File &&
                   workspace.mode == workspace::RenderMode::PathTracer &&
                   workspace.accumulated_path_samples() < workspace.scene.sampler().samples_per_pixel;
        }

        [[nodiscard]] bool pointer_over_interface(const ImVec2 position, const ImVec2 size, const bool show_axes) const noexcept {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.y <= position.y + top_strip_height + 5.0f) return true;
            if (show_axes && mouse.x >= position.x + size.x - 116.0f && mouse.y <= position.y + 126.0f) return true;
            if (!this->expanded) return false;
            if (mouse.x <= position.x + 56.0f && mouse.y >= position.y + size.y * 0.5f - 74.0f && mouse.y <= position.y + size.y * 0.5f + 74.0f) return true;
            if (mouse.x >= position.x + size.x - 270.0f && mouse.y >= position.y + 52.0f && mouse.y <= position.y + 274.0f) return true;
            return false;
        }

        void synchronize_transform(const workspace::Workspace& workspace) {
            const scene::Instance* instance = selected_instance(workspace);
            if (!instance) {
                this->transform_instance.reset();
                return;
            }
            if (this->transform_interaction || this->gizmo_using) return;
            if (this->transform_instance == instance->id && this->transform_revision == workspace.scene.revision().value) return;
            std::array<float, 16> matrix = column_major(instance->transform.matrix);
            ImGuizmo::DecomposeMatrixToComponents(matrix.data(), this->translation.data(), this->rotation.data(), this->scale.data());
            this->transform_instance = instance->id;
            this->transform_revision = workspace.scene.revision().value;
        }

        void apply_transform(workspace::Workspace& workspace) {
            std::array<float, 16> matrix{};
            ImGuizmo::RecomposeMatrixFromComponents(this->translation.data(), this->rotation.data(), this->scale.data(), matrix.data());
            workspace.update_transform_edit(row_major_transform(matrix));
        }

        void transform_field(workspace::Workspace& workspace, const char* label, std::array<float, 3>& value, const float speed) {
            ImGui::SetNextItemWidth(-1.0f);
            const bool changed = ImGui::DragFloat3(label, value.data(), speed, 0.0f, 0.0f, "%.3f");
            if (ImGui::IsItemActivated() && this->transform_instance) {
                workspace.begin_transform_edit(*this->transform_instance);
                this->transform_interaction = true;
            }
            if (changed) this->apply_transform(workspace);
            if (ImGui::IsItemDeactivated() && this->transform_interaction) {
                workspace.finish_transform_edit();
                this->transform_interaction = false;
                this->transform_revision = 0;
            }
        }

        void handle_shortcuts(workspace::Workspace& workspace, WorkspaceUiActions& actions, const float aspect) {
            ImGuiIO& io = ImGui::GetIO();
            if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.WantTextInput) this->expanded = !this->expanded;

            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                actions.exit_application = true;
                return;
            }
            if (io.WantTextInput) return;

            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) actions.open_scene = true;
            if (workspace.provider == workspace::SceneProvider::File && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) actions.save_scene = true;
            if (workspace.provider == workspace::SceneProvider::File && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) actions.save_scene_as = true;
            if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) && workspace.can_undo()) workspace.undo();
            if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) || (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
                if (workspace.can_redo()) workspace.redo();
            if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_1, false)) workspace.mode = workspace::RenderMode::Rasterizer;
            if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_2, false)) workspace.mode = workspace::RenderMode::PathTracer;
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) this->gizmo_operation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) this->gizmo_operation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) this->gizmo_operation = ImGuizmo::SCALE;
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) workspace.frame_selection(aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) workspace.frame_scene(aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) workspace.view_axis({0.0f, 0.0f, 1.0f}, aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) workspace.view_axis({1.0f, 0.0f, 0.0f}, aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) workspace.view_axis({0.0f, 1.0f, 0.0f}, aspect);
            if (ImGui::IsKeyPressed(ImGuiKey_Keypad0, false))
                workspace.camera_source = workspace.camera_source == workspace::CameraSource::Scene ? workspace::CameraSource::Viewport : workspace::CameraSource::Scene;
            if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false))
                workspace.overlays_visible = !workspace.overlays_visible;
            if (workspace.provider == workspace::SceneProvider::Plugin && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
                const workspace::PlaybackControls controls = workspace.playback_controls();
                if (controls.running)
                    workspace.stop_playback();
                else
                    workspace.start_playback();
            }
        }

        void draw_orientation(workspace::Workspace& workspace, const ImVec2 position, const ImVec2 size, const bool show_axes) {
            if (!show_axes) return;
            const ImVec2 center{position.x + size.x - 56.0f, position.y + 70.0f};
            const scene::CameraFrame frame = workspace.active_camera().frame();
            struct Axis {
                const char* label;
                scene::Float3 world;
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
                if (interaction.clicked) workspace.view_axis(axis.world, size.x / size.y);
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

        void draw_gizmo(workspace::Workspace& workspace, const ImVec2 minimum, const ImVec2 size, const bool blocked) {
            if (!this->expanded || workspace.provider == workspace::SceneProvider::Plugin) return;
            const scene::Instance* instance = selected_instance(workspace);
            if (!instance) return;
            const std::optional<scene::Bounds3> local_bounds = workspace.scene.view().local_bounds(instance->id);
            if (!local_bounds) return;
            const scene::Float3 pivot = local_bounds->center();
            scene::Transform to_pivot{};
            to_pivot.matrix[3] = pivot.x;
            to_pivot.matrix[7] = pivot.y;
            to_pivot.matrix[11] = pivot.z;
            scene::Transform from_pivot{};
            from_pivot.matrix[3] = -pivot.x;
            from_pivot.matrix[7] = -pivot.y;
            from_pivot.matrix[11] = -pivot.z;
            std::array<float, 16> matrix = column_major((instance->transform * to_pivot).matrix);
            const scene::CameraMatrices matrices = workspace.active_camera().matrices();
            std::array<float, 16> view = column_major(matrices.view);
            std::array<float, 16> projection = column_major(matrices.projection);
            ImGuizmo::SetOrthographic(std::holds_alternative<scene::OrthographicCameraData>(workspace.active_camera().data));
            ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo::SetRect(minimum.x, minimum.y, size.x, size.y);
            ImGuizmo::Enable(!blocked);
            const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), this->gizmo_operation, ImGuizmo::WORLD, matrix.data());
            const bool using_now = ImGuizmo::IsUsing();
            if (using_now && !this->gizmo_using) workspace.begin_transform_edit(instance->id);
            if (changed) workspace.update_transform_edit(row_major_transform(matrix) * from_pivot);
            if (!using_now && this->gizmo_using) {
                workspace.finish_transform_edit();
                this->transform_revision = 0;
            }
            this->gizmo_using = using_now;
            ImGuizmo::Enable(true);
        }

        void handle_viewport_input(workspace::Workspace& workspace, const ImVec2 minimum, const ImVec2 size, const bool blocked) {
            const ImGuiIO& io = ImGui::GetIO();
            const ImVec2 maximum{minimum.x + size.x, minimum.y + size.y};
            const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(minimum, maximum);
            if (!hovered || blocked || io.WantTextInput) {
                workspace.clear_hover();
                return;
            }
            if (workspace.camera_source == workspace::CameraSource::Viewport && !ImGuizmo::IsUsing()) {
                if (io.MouseWheel != 0.0f) workspace.zoom_viewport_camera(io.MouseWheel);
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    if (io.KeyShift)
                        workspace.pan_viewport_camera(io.MouseDelta.x, io.MouseDelta.y, size.y);
                    else
                        workspace.orbit_viewport_camera(io.MouseDelta.x, io.MouseDelta.y);
                }
            }
            const bool pick_surface = !ImGuizmo::IsOver() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
            if (!pick_surface) {
                workspace.clear_hover();
                return;
            }
            const float x = std::clamp((io.MousePos.x - minimum.x) / size.x, 0.0f, 1.0f);
            const float y = std::clamp((io.MousePos.y - minimum.y) / size.y, 0.0f, 1.0f);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
                workspace.request_pick(x, y, true, io.KeyShift);
            else
                workspace.request_pick(x, y, false, false);
        }

        void draw_viewport(workspace::Workspace& workspace, const std::uint64_t texture, const ImVec2 position, const ImVec2 size, const bool show_axes) {
            ImGui::SetNextWindowPos(position);
            ImGui::SetNextWindowSize(size);
            ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            constexpr ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus |
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse;
            ImGui::Begin("##SpectraViewport", nullptr, flags);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddImage(static_cast<ImTextureID>(texture), position, ImVec2{position.x + size.x, position.y + size.y});
            ImGui::PushClipRect(position, ImVec2{position.x + size.x, position.y + size.y}, true);
            if (this->expanded) draw_scene_camera(workspace, position, size, *draw_list);
            const bool blocked = this->pointer_over_interface(position, size, show_axes);
            this->draw_gizmo(workspace, position, size, blocked);
            this->draw_orientation(workspace, position, size, show_axes);
            this->handle_viewport_input(workspace, position, size, blocked);
            ImGui::PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2{position.x, position.y + size.y});
            ImGui::Dummy({});
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        [[nodiscard]] float draw_render_status(workspace::Workspace& workspace, const float end) {
            const bool plugin = workspace.provider == workspace::SceneProvider::Plugin;
            const bool path = !plugin && workspace.mode == workspace::RenderMode::PathTracer;
            if (!plugin && !path) return end;

            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const auto button_size = [](const char* label) {
                const ImVec2 text_size = ImGui::CalcTextSize(label);
                const ImVec2 padding = ImGui::GetStyle().FramePadding;
                return ImVec2{text_size.x + padding.x * 2.0f, 27.0f};
            };
            const auto draw_status_text = [](const std::string& text) {
                const ImVec2 minimum = ImGui::GetCursorScreenPos();
                const ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
                const ImVec2 text_position{minimum.x, minimum.y + (27.0f - text_size.y) * 0.5f};
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2{text_position.x + 1.0f, text_position.y + 1.0f},
                    ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.55f}),
                    text.c_str());
                ImGui::GetWindowDrawList()->AddText(text_position, ImGui::GetColorU32(ImVec4{0.88f, 0.91f, 0.95f, 0.55f}), text.c_str());
                ImGui::Dummy(ImVec2{text_size.x, 27.0f});
            };

            const std::string status = plugin
                ? std::format("{:.3f} s", workspace.timeline().seconds)
                : std::format("{} / {} spp", workspace.accumulated_path_samples(), workspace.scene.sampler().samples_per_pixel);
            const workspace::PlaybackControls controls = plugin ? workspace.playback_controls() : workspace::PlaybackControls{};
            const char* playback_label = plugin
                ? controls.running ? "Pause" : "Play"
                : workspace.pathtracer_paused ? "Resume" : "Pause";
            const char* secondary_label = plugin ? "Step" : "Reset";
            ImVec2 playback_size = button_size(playback_label);
            playback_size.x = std::max(
                button_size("Pause").x,
                button_size(plugin ? "Play" : "Resume").x);
            const ImVec2 secondary_size = button_size(secondary_label);
            const float status_width =
                ImGui::CalcTextSize(status.c_str()).x +
                playback_size.x +
                secondary_size.x +
                spacing * 2.0f;
            const float status_start = end - status_width - 8.0f;
            ImGui::SameLine(status_start);

            if (plugin) {
                ImGui::BeginDisabled(controls.running ? !controls.can_stop : !controls.can_start);
                if (icon_button(
                        "##PluginPlayback",
                        playback_size,
                        controls.running ? Icon::Pause : Icon::Play,
                        playback_label,
                        controls.running ? ImVec4{0.35f, 0.84f, 0.55f, 1.0f} : ImVec4{})) {
                    if (controls.running)
                        workspace.stop_playback();
                    else
                        workspace.start_playback();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!controls.can_advance);
                if (icon_button("##PluginStep", secondary_size, Icon::Step, secondary_label))
                    workspace.advance_playback(1.0 / 60.0);
                ImGui::EndDisabled();
                ImGui::SameLine();
                draw_status_text(status);
            } else {
                draw_status_text(status);
                ImGui::SameLine();
                if (icon_button(
                        "##PathPlayback",
                        playback_size,
                        workspace.pathtracer_paused ? Icon::Play : Icon::Pause,
                        playback_label,
                        workspace.pathtracer_paused ? ImVec4{0.91f, 0.72f, 0.29f, 1.0f} : ImVec4{}))
                    workspace.pathtracer_paused = !workspace.pathtracer_paused;
                ImGui::SameLine();
                if (icon_button("##PathReset", secondary_size, Icon::Reset, secondary_label))
                    workspace.reset_accumulation();
            }
            return status_start;
        }

        void draw_top_strip(workspace::Workspace& workspace, const ImVec2 position, const ImVec2 size, WorkspaceUiActions& actions) {
            ImGui::SetNextWindowPos(ImVec2{position.x + 6.0f, position.y + 5.0f});
            ImGui::SetNextWindowSize(ImVec2{size.x - 12.0f, top_strip_height});
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f, 4.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0f, 4.0f});
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
            ImGui::Begin("##ApplicationStrip", nullptr, flags);
            if (this->path_progress_visible(workspace)) {
                const float progress = static_cast<float>(workspace.accumulated_path_samples()) / static_cast<float>(workspace.scene.sampler().samples_per_pixel);
                const ImVec2 minimum = ImGui::GetWindowPos();
                const ImVec2 maximum{minimum.x + ImGui::GetWindowWidth(), minimum.y + ImGui::GetWindowHeight()};
                const float progress_x = minimum.x + (maximum.x - minimum.x) * std::clamp(progress, 0.0f, 1.0f);
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->PushClipRect(minimum, maximum, false);
                draw_list->AddRectFilled(
                    minimum,
                    ImVec2{progress_x, maximum.y},
                    ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.07f}));
                if (progress_x > minimum.x)
                    draw_list->AddLine(
                        ImVec2{progress_x, minimum.y + 2.0f},
                        ImVec2{progress_x, maximum.y - 2.0f},
                        ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 0.72f}),
                        1.0f);
                draw_list->PopClipRect();
            }

            std::string identity = workspace.source_path.filename().string();
            if (identity.empty()) identity = std::string{workspace.provider_name()};
            const float identity_width = std::clamp(ImGui::CalcTextSize(identity.c_str()).x + 24.0f, 96.0f, 240.0f);
            if (text_button("##SceneIdentity", identity.c_str(), ImVec2{identity_width, 27.0f})) ImGui::OpenPopup("##SceneMenu");
            const float source_end = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
            if (workspace.dirty()) {
                const ImVec2 minimum = ImGui::GetItemRectMin();
                const ImVec2 maximum = ImGui::GetItemRectMax();
                const float visible_text_width = std::min(ImGui::CalcTextSize(identity.c_str()).x, identity_width - 20.0f);
                const float text_right = minimum.x + (identity_width + visible_text_width) * 0.5f;
                ImGui::GetWindowDrawList()->AddCircleFilled(
                    ImVec2{std::min(text_right + 7.0f, maximum.x - 4.0f), (minimum.y + maximum.y) * 0.5f},
                    2.5f,
                    ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}),
                    12);
            }
            if (ImGui::BeginPopup("##SceneMenu")) {
                if (ImGui::MenuItem("Open…", "Ctrl+O")) actions.open_scene = true;
                if (ImGui::MenuItem("Reload")) actions.reload_scene = true;
                ImGui::Separator();
                ImGui::BeginDisabled(workspace.provider == workspace::SceneProvider::Plugin);
                if (ImGui::MenuItem("Save", "Ctrl+S")) actions.save_scene = true;
                if (ImGui::MenuItem("Save As…", "Ctrl+Shift+S")) actions.save_scene_as = true;
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            const float center = ImGui::GetWindowWidth() * 0.5f;
            const float right = ImGui::GetWindowWidth();
            constexpr float mode_button_width = 80.0f;
            constexpr float mode_width = mode_button_width * 2.0f + 4.0f;
            const float mode_start = center - mode_width * 0.5f;
            ImGui::SameLine(mode_start);
            const bool rasterizer = workspace.mode == workspace::RenderMode::Rasterizer;
            if (text_button("##Raster", "Raster", ImVec2{mode_button_width, 27.0f}, rasterizer)) workspace.mode = workspace::RenderMode::Rasterizer;
            ImGui::SameLine();
            if (text_button("##Path", "Path", ImVec2{mode_button_width, 27.0f}, !rasterizer)) workspace.mode = workspace::RenderMode::PathTracer;

            constexpr float exposure_width = 84.0f;
            constexpr float capture_width = 28.0f;
            constexpr float edge_margin = 4.0f;
            const float exposure_start = right - edge_margin - capture_width - ImGui::GetStyle().ItemSpacing.x - exposure_width;
            const float status_start = this->draw_render_status(workspace, exposure_start);
            ImGui::SameLine(exposure_start);
            const FlatButtonInteraction exposure = flat_button("##Exposure", ImVec2{exposure_width, 27.0f});
            if (exposure.active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
                workspace.exposure = std::clamp(workspace.exposure + ImGui::GetIO().MouseDelta.x * 0.05f, -20.0f, 20.0f);
            if (exposure.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) workspace.exposure = 0.0f;
            const std::string exposure_text = std::format("{:+.2f} EV", workspace.exposure);
            const ImVec2 exposure_text_size = ImGui::CalcTextSize(exposure_text.c_str());
            const ImVec2 exposure_text_position{
                exposure.minimum.x + (exposure_width - exposure_text_size.x) * 0.5f,
                exposure.minimum.y + (27.0f - exposure_text_size.y) * 0.5f + exposure.vertical_offset,
            };
            ImGui::GetWindowDrawList()->AddText(
                ImVec2{exposure_text_position.x + 1.0f, exposure_text_position.y + 1.0f},
                exposure.shadow,
                exposure_text.c_str());
            ImGui::GetWindowDrawList()->AddText(exposure_text_position, exposure.color, exposure_text.c_str());
            ImGui::SameLine();
            if (icon_button("##Capture", ImVec2{capture_width, 27.0f}, Icon::Capture)) ImGui::OpenPopup("##CaptureMenu");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Capture");
            if (ImGui::BeginPopup("##CaptureMenu")) {
                if (ImGui::MenuItem("Viewport PNG")) actions.capture = CaptureFormat::Png;
                if (ImGui::MenuItem("Linear EXR")) actions.capture = CaptureFormat::Exr;
                ImGui::EndPopup();
            }

            const float strip_offset_x = ImGui::GetWindowPos().x - position.x;
            const float left_drag_start = strip_offset_x + source_end + 4.0f;
            const float mode_client_start = strip_offset_x + mode_start;
            const float mode_client_end = mode_client_start + mode_width;
            const float right_drag_start = mode_client_end + 4.0f;
            const float status_client_start = strip_offset_x + status_start;
            actions.drag_regions = {{
                {left_drag_start, 0.0f, std::max(left_drag_start, mode_client_start - 4.0f), top_strip_height + 5.0f},
                {right_drag_start, 0.0f, std::max(right_drag_start, status_client_start - 4.0f), top_strip_height + 5.0f},
            }};
            ImGui::End();
            ImGui::PopStyleVar(2);
        }

        void draw_transform_tools(const ImVec2 position, const ImVec2 size) {
            ImGui::SetNextWindowPos(ImVec2{position.x + 8.0f, position.y + size.y * 0.5f - 68.0f});
            ImGui::SetNextWindowSize(ImVec2{44.0f, 136.0f});
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{4.0f, 4.0f});
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
            ImGui::Begin("##TransformTools", nullptr, flags);
            const bool translating = this->gizmo_operation == ImGuizmo::TRANSLATE;
            const bool rotating = this->gizmo_operation == ImGuizmo::ROTATE;
            const bool scaling = this->gizmo_operation == ImGuizmo::SCALE;
            if (icon_button("##Translate", ImVec2{36.0f, 38.0f}, Icon::Translate, nullptr, translating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, translating, false, 0.55f))
                this->gizmo_operation = ImGuizmo::TRANSLATE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate  W");
            if (icon_button("##Rotate", ImVec2{36.0f, 38.0f}, Icon::Rotate, nullptr, rotating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, rotating, false, 0.55f))
                this->gizmo_operation = ImGuizmo::ROTATE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate  E");
            if (icon_button("##Scale", ImVec2{36.0f, 38.0f}, Icon::Scale, nullptr, scaling ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, scaling, false, 0.55f))
                this->gizmo_operation = ImGuizmo::SCALE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale  R");
            ImGui::End();
            ImGui::PopStyleVar();
        }

        void draw_transform_hud(workspace::Workspace& workspace, const ImVec2 position, const ImVec2 size) {
            const scene::Instance* instance = selected_instance(workspace);
            if (!instance) return;
            this->synchronize_transform(workspace);
            ImGui::SetNextWindowPos(ImVec2{position.x + size.x - 260.0f, position.y + 52.0f});
            ImGui::SetNextWindowSize(ImVec2{248.0f, 214.0f});
            ImGui::SetNextWindowBgAlpha(0.91f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
            ImGui::Begin("##TransformHud", nullptr, flags);
            ImGui::TextUnformatted(instance->name.c_str());
            ImGui::Separator();
            ImGui::TextDisabled("POSITION");
            this->transform_field(workspace, "##Position", this->translation, 0.01f);
            ImGui::TextDisabled("ROTATION");
            this->transform_field(workspace, "##Rotation", this->rotation, 0.1f);
            ImGui::TextDisabled("SCALE");
            this->transform_field(workspace, "##Scale", this->scale, 0.01f);
            ImGui::End();
            ImGui::PopStyleVar();
        }

        void draw_status_toast(std::string& status, bool& status_error, const ImVec2 position, const ImVec2 size) {
            if (status != this->observed_status || status_error != this->observed_status_error) {
                this->observed_status = status;
                this->observed_status_error = status_error;
                this->status_since = ImGui::GetTime();
            }
            if (status.empty()) return;
            const double age = ImGui::GetTime() - this->status_since;
            if (!status_error && age > 3.0) {
                status.clear();
                this->observed_status.clear();
                return;
            }
            const float alpha = status_error ? 1.0f : std::clamp(static_cast<float>((3.0 - age) / 0.35), 0.0f, 1.0f);
            const ImVec2 text_size = ImGui::CalcTextSize(status.c_str());
            const float width = std::min(size.x - 24.0f, text_size.x + (status_error ? 54.0f : 30.0f));
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
                    this->observed_status.clear();
                }
            }
            ImGui::End();
            ImGui::PopStyleVar(2);
        }
    };

    WorkspaceUi::WorkspaceUi() : state(std::make_unique<State>()) {}
    WorkspaceUi::~WorkspaceUi() = default;

    WorkspaceUiActions WorkspaceUi::draw(workspace::Workspace& workspace, const std::uint64_t viewport_texture) {
        ImGuizmo::BeginFrame();
        WorkspaceUiActions actions{};
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position = viewport->Pos;
        const ImVec2 size = viewport->Size;
        const float aspect = size.x / size.y;
        const ImGuiIO& io = ImGui::GetIO();
        actions.show_axes = ImGui::IsKeyDown(ImGuiKey_G) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && !io.WantTextInput;

        this->state->synchronize_transform(workspace);
        this->state->handle_shortcuts(workspace, actions, aspect);
        this->state->draw_viewport(workspace, viewport_texture, position, size, actions.show_axes);
        this->state->draw_top_strip(workspace, position, size, actions);
        if (this->state->expanded && selected_instance(workspace) && workspace.provider == workspace::SceneProvider::File) {
            this->state->draw_transform_tools(position, size);
            this->state->draw_transform_hud(workspace, position, size);
        }
        this->state->draw_status_toast(this->status, this->status_error, position, size);
        return actions;
    }
} // namespace spectra::app
