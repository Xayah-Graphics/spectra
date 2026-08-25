module;

#include <imgui.h>
#include <imgui_internal.h>

#include <ImGuizmo.h>

module spectra.editor.ui;
import std;

namespace spectra::editor {
    namespace {
        constexpr float top_strip_height    = 36.0f;
        constexpr float panel_top           = 52.0f;
        constexpr float panel_margin        = 12.0f;
        constexpr float panel_minimum_width = 280.0f;
        constexpr float panel_maximum_width = 320.0f;
        constexpr ImVec2 playback_button_size{76.0f, 34.0f};
        constexpr float playback_spacing       = 7.0f;
        constexpr float playback_bottom_margin = 18.0f;

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

        void push_panel_style() {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {12.0f, 10.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {6.0f, 6.0f});
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 2.5f);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.02f, 0.028f, 0.038f, 0.22f});
            ImGui::PushStyleColor(ImGuiCol_Border, {0.55f, 0.65f, 0.72f, 0.14f});
            ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, {0.0f, 0.0f, 0.0f, 0.0f});
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, {0.52f, 0.62f, 0.70f, 0.20f});
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, {0.52f, 0.62f, 0.70f, 0.30f});
            ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, {0.52f, 0.62f, 0.70f, 0.40f});
        }

        void pop_panel_style() {
            ImGui::PopStyleColor(6);
            ImGui::PopStyleVar(6);
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

        [[nodiscard]] const scene::EntityReference* active_entity(const Ui& editor) noexcept {
            return editor.context.viewport.view.selection.active ? &*editor.context.viewport.view.selection.active : nullptr;
        }

        [[nodiscard]] bool transform_editable(const Ui& editor, const scene::EntityReference entity) noexcept {
            if (std::holds_alternative<scene::NeuralFieldId>(entity.data)) return false;
            if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) return !editor.context.simulation.initialized() || !editor.context.simulation.controls(*id);
            if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) return !editor.context.simulation.initialized() || !editor.context.simulation.controls(*id);
            if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) return !editor.context.simulation.initialized() || !editor.context.simulation.controls(*id);
            if (!std::holds_alternative<scene::InstanceId>(entity.data) && !std::holds_alternative<scene::EntityReference::AreaEmitter>(entity.data)) return std::holds_alternative<scene::LightId>(entity.data);
            const scene::InstanceId instance_id = std::holds_alternative<scene::InstanceId>(entity.data) ? std::get<scene::InstanceId>(entity.data) : std::get<scene::EntityReference::AreaEmitter>(entity.data).instance;
            return !editor.context.simulation.initialized() || !editor.context.simulation.controls(instance_id);
        }

        [[nodiscard]] std::optional<math::Transform> entity_transform(const scene::Scene& source, const scene::EntityReference entity) noexcept {
            if (std::holds_alternative<scene::InstanceId>(entity.data) || std::holds_alternative<scene::EntityReference::AreaEmitter>(entity.data)) {
                const scene::InstanceId id = std::holds_alternative<scene::InstanceId>(entity.data) ? std::get<scene::InstanceId>(entity.data) : std::get<scene::EntityReference::AreaEmitter>(entity.data).instance;
                return std::ranges::find(source.resources.instances, id, &scene::Instance::id)->transform;
            }
            if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) return std::ranges::find(source.resources.cameras, *id, &scene::Camera::id)->transform;
            if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) return std::ranges::find(source.resources.particle_sets, *id, &scene::ParticleSet::id)->transform;
            if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) return std::ranges::find(source.resources.volumes, *id, &scene::Volume::id)->transform;
            if (const scene::NeuralFieldId* id = std::get_if<scene::NeuralFieldId>(&entity.data)) return std::ranges::find(source.resources.neural_fields, *id, &scene::NeuralField::id)->transform;
            const scene::LightId* light_id = std::get_if<scene::LightId>(&entity.data);
            if (!light_id) return std::nullopt;
            const scene::Light& light = *std::ranges::find(source.resources.lights, *light_id, &scene::Light::id);
            return std::visit(
                [](const auto& data) -> std::optional<math::Transform> {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) return data.environment.transform;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>) return std::nullopt;
                    else return data.transform;
                },
                light.data);
        }

        [[nodiscard]] const std::string& entity_name(const scene::Scene& source, const scene::EntityReference entity) noexcept {
            if (const scene::InstanceId* id = std::get_if<scene::InstanceId>(&entity.data)) return std::ranges::find(source.resources.instances, *id, &scene::Instance::id)->name;
            if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) return std::ranges::find(source.resources.cameras, *id, &scene::Camera::id)->name;
            if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) return std::ranges::find(source.resources.particle_sets, *id, &scene::ParticleSet::id)->name;
            if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) return std::ranges::find(source.resources.volumes, *id, &scene::Volume::id)->name;
            if (const scene::NeuralFieldId* id = std::get_if<scene::NeuralFieldId>(&entity.data)) return std::ranges::find(source.resources.neural_fields, *id, &scene::NeuralField::id)->name;
            if (const scene::EntityReference::AreaEmitter* emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity.data)) return std::ranges::find(source.resources.lights, emitter->light, &scene::Light::id)->name;
            if (const scene::LightId* id = std::get_if<scene::LightId>(&entity.data)) return std::ranges::find(source.resources.lights, *id, &scene::Light::id)->name;
            std::unreachable();
        }

        [[nodiscard]] const char* entity_kind_name(const scene::EntityReference entity) noexcept {
            if (std::holds_alternative<scene::InstanceId>(entity.data)) return "INSTANCE";
            if (std::holds_alternative<scene::CameraId>(entity.data)) return "CAMERA";
            if (std::holds_alternative<scene::LightId>(entity.data)) return "LIGHT";
            if (std::holds_alternative<scene::EntityReference::AreaEmitter>(entity.data)) return "AREA EMITTER";
            if (std::holds_alternative<scene::ParticleSetId>(entity.data)) return "PARTICLE SET";
            if (std::holds_alternative<scene::VolumeId>(entity.data)) return "VOLUME";
            if (std::holds_alternative<scene::NeuralFieldId>(entity.data)) return "NEURAL FIELD";
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
            if (disabled) color.w = 0.28f / ImGui::GetStyle().DisabledAlpha;
            else if (dangerous && interaction.hovered) color = {0.94f, 0.35f, 0.35f, interaction.active ? 0.85f : 1.0f};
            else if (semantic_color.w > 0.0f) {
                color = semantic_color;
                if (interaction.active) color.w = 0.85f;
            } else if (interaction.active) color.w = 0.85f;
            else if (interaction.hovered) color.w = 1.0f;
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

    Ui::Ui(Document& document, simulation::Runtime& simulation, render::Engine& render_engine, Viewport& viewport, Picker& picker, ImGuiBackend& imgui) noexcept : context{document, simulation, render_engine, viewport, picker, imgui}, inspector{document, viewport}, simulation_panel{document, simulation} {}

    void Ui::notify(std::string message, const bool error) {
        this->controls.status       = std::move(message);
        this->controls.status_error = error;
    }

    Actions Ui::draw() {
        ImGuizmo::BeginFrame();
        Actions actions{};
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 position         = viewport->Pos;
        const ImVec2 size             = viewport->Size;
        const float aspect            = size.x / size.y;
        const ImGuiIO& io             = ImGui::GetIO();
        if (!this->context.document.loaded) {
            this->controls.global_panel_open = false;
            this->simulation_panel.reset();
            this->handle_shortcuts(actions, aspect, false);
            this->draw_top_strip(position, size, actions);
            this->draw_status_toast(position, size);
            return actions;
        }
        actions.show_axes                    = ImGui::IsKeyDown(ImGuiKey_G) && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper && !io.WantTextInput;
        const scene::EntityReference* entity = active_entity(*this);
        const bool editable                  = entity && transform_editable(*this, *entity);

        if (this->simulation_panel.synchronize()) this->controls.simulation_sample_initialized = false;
        this->synchronize_transform();
        this->handle_shortcuts(actions, aspect, true);
        const ViewportLayout layout = this->make_layout(position, size);
        this->draw_viewport(layout, actions.show_axes, editable);
        this->draw_top_strip(position, size, actions);
        if (layout.selection_panel.visible) this->draw_selection_panel(layout.selection_panel, editable);
        if (layout.global_panel.visible) this->draw_global_panel(layout.global_panel, actions);
        actions.rebuild_simulation_rendering = std::exchange(this->simulation_panel.rebuild_rendering, false);
        if (!this->simulation_panel.notification.empty()) {
            this->notify(std::exchange(this->simulation_panel.notification, std::string{}), this->simulation_panel.notification_error);
            this->simulation_panel.notification_error = false;
        }
        this->draw_status_toast(position, size);
        return actions;
    }
    Ui::ViewportLayout Ui::make_layout(const ImVec2 position, const ImVec2 size) const noexcept {
        const bool selection_visible  = active_entity(*this) != nullptr;
        const bool simulation_visible = this->context.simulation.initialized();
        const bool playback_visible   = simulation_visible || this->context.render_engine.progress().has_value();
        const float playback_count    = simulation_visible ? 3.0f : 2.0f;
        const float playback_width    = playback_button_size.x * playback_count + playback_spacing * (playback_count - 1.0f);
        const float width             = std::clamp(size.x * 0.18f, panel_minimum_width, panel_maximum_width);
        const float maximum_height    = size.y - panel_top - panel_margin;
        return {
            .position = position,
            .size     = size,
            .global_panel{
                .position       = {position.x + panel_margin, position.y + panel_top},
                .size           = {width, std::min(this->controls.global_panel_height, maximum_height)},
                .maximum_height = maximum_height,
                .visible        = this->controls.global_panel_open,
            },
            .selection_panel{
                .position       = {position.x + size.x - panel_margin - width, position.y + panel_top},
                .size           = {width, std::min(this->controls.selection_panel_height, maximum_height)},
                .maximum_height = maximum_height,
                .visible        = selection_visible,
            },
            .playback_controls{
                .position = {position.x + (size.x - playback_width) * 0.5f, position.y + size.y - playback_bottom_margin - playback_button_size.y},
                .size     = {playback_width, playback_button_size.y},
                .visible  = playback_visible,
            },
        };
    }

    bool Ui::pointer_over_interface(const ViewportLayout& layout, const bool show_axes) const noexcept {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        if (mouse.y <= layout.position.y + top_strip_height + 5.0f) return true;
        if (show_axes && mouse.x >= layout.position.x + layout.size.x - 116.0f && mouse.y <= layout.position.y + 126.0f) return true;
        const auto contains = [mouse](const auto& region) { return region.visible && mouse.x >= region.position.x && mouse.x <= region.position.x + region.size.x && mouse.y >= region.position.y && mouse.y <= region.position.y + region.size.y; };
        if (contains(layout.global_panel) || contains(layout.selection_panel) || contains(layout.playback_controls)) return true;
        return false;
    }

    void Ui::synchronize_transform() {
        const scene::EntityReference* entity = active_entity(*this);
        if (!entity) {
            this->controls.transform_entity.reset();
            return;
        }
        if (this->controls.transform_drag_active || this->controls.gizmo_active) return;
        const bool editable                            = transform_editable(*this, *entity);
        const scene::Scene& transform_scene            = this->context.document.evaluated;
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

    void Ui::apply_transform(const scene::EntityReference entity, math::Transform transform) {
        if (std::holds_alternative<scene::InstanceId>(entity.data) || std::holds_alternative<scene::EntityReference::AreaEmitter>(entity.data)) {
            const scene::InstanceId instance_id = std::holds_alternative<scene::InstanceId>(entity.data) ? std::get<scene::InstanceId>(entity.data) : std::get<scene::EntityReference::AreaEmitter>(entity.data).instance;
            this->context.document.update_transform(instance_id, std::move(transform));
        } else if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) this->context.document.update_camera_transform(*id, std::move(transform));
        else if (const scene::LightId* id = std::get_if<scene::LightId>(&entity.data)) this->context.document.update_light_transform(*id, std::move(transform));
        else if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) this->context.document.update_volume_transform(*id, std::move(transform));
        else if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) this->context.document.update_particle_set_transform(*id, std::move(transform));
    }

    void Ui::transform_row(const char* identifier, const char* title, std::array<float, 3>& value, const float speed, const char* format) {
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
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.075f, 0.095f, 0.115f, 0.36f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.11f, 0.14f, 0.17f, 0.50f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.10f, 0.18f, 0.21f, 0.62f});
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

    void Ui::handle_shortcuts(Actions& actions, const float aspect, const bool global_panel_available) {
        ImGuiIO& io = ImGui::GetIO();
        if (this->controls.wireframe_restore_mode && !ImGui::IsKeyDown(ImGuiKey_W)) {
            actions.raster_display_mode = *this->controls.wireframe_restore_mode;
            this->controls.wireframe_restore_mode.reset();
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            actions.exit_application = true;
            return;
        }
        if (io.WantTextInput) return;

        const bool no_modifiers = !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper;
        if (global_panel_available && no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) this->controls.global_panel_open = !this->controls.global_panel_open;
        if (!this->context.document.loaded) return;
        if (this->context.render_engine.selected_descriptor() == render::rasterizer_descriptor && no_modifiers && ImGui::IsKeyDown(ImGuiKey_W) && !this->controls.wireframe_restore_mode) {
            this->controls.wireframe_restore_mode = this->context.render_engine.raster_display_mode();
            actions.raster_display_mode           = render::RasterDisplayMode::Wireframe;
        }
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_F, false)) this->context.viewport.frame_selection(aspect);
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->context.viewport.view_axis({0.0f, 0.0f, 1.0f}, aspect);
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->context.viewport.view_axis({1.0f, 0.0f, 0.0f}, aspect);
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->context.viewport.view_axis({0.0f, 1.0f, 0.0f}, aspect);
        if (no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Keypad0, false)) this->context.viewport.toggle_scene_camera();
        if (this->context.simulation.initialized() && !this->context.simulation.faulted() && no_modifiers && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
            try {
                if (this->context.simulation.running()) this->context.simulation.pause();
                else this->context.simulation.start();
            } catch (const std::exception& error) {
                this->notify(error.what(), true);
            }
        }
    }

    void Ui::draw_orientation(const ImVec2 position, const ImVec2 size, const bool show_axes) {
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

    void Ui::draw_gizmo(const ImVec2 minimum, const ImVec2 size, const bool blocked, const bool transform_editable) {
        this->controls.gizmo_active = false;
        if (!transform_editable) return;
        const scene::EntityReference* entity = active_entity(*this);
        if (!entity) return;
        const scene::Scene& transform_scene            = this->context.document.evaluated;
        const std::optional<math::Transform> transform = entity_transform(transform_scene, *entity);
        if (!transform) return;
        math::Float3 pivot{};
        if (std::holds_alternative<scene::InstanceId>(entity->data) || std::holds_alternative<scene::EntityReference::AreaEmitter>(entity->data)) {
            const scene::InstanceId instance_id             = std::holds_alternative<scene::InstanceId>(entity->data) ? std::get<scene::InstanceId>(entity->data) : std::get<scene::EntityReference::AreaEmitter>(entity->data).instance;
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
        ImGuizmo::Enable(!blocked);
        const bool changed = ImGuizmo::Manipulate(view.data(), projection.data(), this->controls.gizmo_operation, ImGuizmo::WORLD, matrix.data());
        if (changed) this->apply_transform(*entity, row_major_transform(matrix) * from_pivot);
        this->controls.gizmo_active = ImGuizmo::IsUsing();
        ImGuizmo::Enable(true);
    }

    void Ui::handle_viewport_input(const ImVec2 minimum, const ImVec2 size, const bool blocked) {
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
                if (io.KeyShift) this->context.viewport.pan_viewport_camera(io.MouseDelta.x, io.MouseDelta.y, size.y);
                else this->context.viewport.orbit_viewport_camera(io.MouseDelta.x, io.MouseDelta.y);
            }
        }
        const bool pick_surface = !ImGuizmo::IsOver() && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        if (!pick_surface) {
            this->context.viewport.clear_hover();
            return;
        }
        const float x = std::clamp((io.MousePos.x - minimum.x) / size.x, 0.0f, 1.0f);
        const float y = std::clamp((io.MousePos.y - minimum.y) / size.y, 0.0f, 1.0f);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) this->context.picker.submit_pick(x, y, true, io.KeyShift);
        else this->context.picker.submit_pick(x, y, false, false);
    }

    void Ui::draw_viewport(const ViewportLayout& layout, const bool show_axes, const bool transform_editable) {
        const ImVec2 position = layout.position;
        const ImVec2 size     = layout.size;
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
        this->draw_camera_gate(position, size, *draw_list);
        const bool blocked = this->pointer_over_interface(layout, show_axes);
        this->draw_gizmo(position, size, blocked, transform_editable);
        this->draw_orientation(position, size, show_axes);
        this->draw_viewport_hud(layout, *draw_list);
        this->draw_playback_controls(layout.playback_controls);
        this->handle_viewport_input(position, size, blocked);
        ImGui::PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2{position.x, position.y + size.y});
        ImGui::Dummy({});
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void Ui::draw_camera_gate(const ImVec2 position, const ImVec2 size, ImDrawList& draw_list) const {
        if (!this->context.viewport.view.camera_gate) return;
        const math::Float4 gate = *this->context.viewport.view.camera_gate;
        const ImVec2 minimum{position.x + gate.x * size.x, position.y + gate.y * size.y};
        const ImVec2 maximum{minimum.x + gate.z * size.x, minimum.y + gate.w * size.y};
        const ImVec2 viewport_maximum{position.x + size.x, position.y + size.y};
        const ImU32 shade = ImGui::GetColorU32(ImVec4{0.0f, 0.0f, 0.0f, 0.42f});
        draw_list.AddRectFilled(position, {viewport_maximum.x, minimum.y}, shade);
        draw_list.AddRectFilled({position.x, maximum.y}, viewport_maximum, shade);
        draw_list.AddRectFilled({position.x, minimum.y}, {minimum.x, maximum.y}, shade);
        draw_list.AddRectFilled({maximum.x, minimum.y}, {viewport_maximum.x, maximum.y}, shade);
        draw_list.AddRect(minimum, maximum, ImGui::GetColorU32(ImVec4{0.67f, 0.75f, 0.80f, 0.58f}), 0.0f, 0, 1.0f);
    }

    void Ui::draw_viewport_hud(const ViewportLayout& layout, ImDrawList& draw_list) {
        if (!this->context.viewport.settings.hud_visible) return;
        const float viewport_left  = layout.position.x + panel_margin;
        const float viewport_right = layout.position.x + layout.size.x - panel_margin;
        const float status_left    = layout.global_panel.visible ? layout.global_panel.position.x + layout.global_panel.size.x + panel_margin : viewport_left;
        const float status_right   = layout.selection_panel.visible ? layout.selection_panel.position.x - panel_margin : viewport_right;
        const float top            = layout.position.y + panel_top;
        const float bottom         = layout.position.y + layout.size.y - panel_margin;
        draw_list.PushClipRect({viewport_left, top}, {viewport_right, bottom}, true);
        draw_list.PushClipRect({status_left, top}, {status_right, bottom}, true);
        const float line_height = ImGui::GetTextLineHeight() + 3.0f;
        const ImU32 primary     = ImGui::GetColorU32({0.92f, 0.95f, 0.98f, 0.96f});
        const ImU32 secondary   = ImGui::GetColorU32({0.72f, 0.78f, 0.84f, 0.84f});
        const ImU32 heading     = ImGui::GetColorU32({0.28f, 0.79f, 0.90f, 0.92f});
        const ImU32 shadow      = ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.88f});
        const auto add_text     = [&draw_list, shadow](const ImVec2 position, const ImU32 color, const std::string_view value) {
            draw_list.AddText({position.x + 1.0f, position.y + 1.0f}, shadow, value.data(), value.data() + value.size());
            draw_list.AddText(position, color, value.data(), value.data() + value.size());
        };
        float y             = top;
        const auto add_line = [&add_text, &y, line_height, status_left](const std::string_view value, const ImU32 color) {
            add_text({status_left, y}, color, value);
            y += line_height;
        };
        const auto add_section = [&add_line, &y, line_height, heading, top](const std::string_view value) {
            if (y != top) y += line_height * 0.35f;
            add_line(value, heading);
        };
        const scene::EntityReference* entity                                   = active_entity(*this);
        const bool simulation_active                                           = this->context.simulation.initialized();
        const scene::SimulationSystem* selected_simulation_system              = simulation_active ? &this->context.document.authored.simulation->systems[this->simulation_panel.selected_system] : nullptr;
        const simulation::ProviderDescriptor* selected_simulation_provider     = simulation_active ? &this->context.simulation.provider_descriptor(selected_simulation_system->provider_id) : nullptr;
        const std::optional<render::RenderProgress> render_progress            = this->context.render_engine.progress();
        const std::optional<render::PathTracerPreparationProgress> preparation = this->context.render_engine.pathtracer_preparation();
        const float frames_per_second                                          = ImGui::GetIO().Framerate;
        const auto viewport_extent                                             = this->context.imgui.viewport_extent;
        add_line(std::format("Render {:.1f} FPS  ·  {:.2f} ms  ·  {} × {}", frames_per_second, 1000.0f / frames_per_second, viewport_extent.width, viewport_extent.height), primary);
        add_section("SHORTCUTS");
        const float keycap_height        = line_height + 2.0f;
        constexpr float keycap_padding   = 6.0f;
        constexpr float label_spacing    = 6.0f;
        constexpr float shortcut_spacing = 16.0f;
        const ImU32 keycap_shadow        = ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.28f});
        const ImU32 keycap_background    = ImGui::GetColorU32({0.14f, 0.18f, 0.21f, 0.22f});
        const ImU32 keycap_border        = ImGui::GetColorU32({0.55f, 0.64f, 0.72f, 0.24f});
        const auto add_shortcut          = [&draw_list, &add_text, &y, keycap_height, keycap_shadow, keycap_background, keycap_border, primary, secondary](float& x, const std::string_view key, const std::string_view action) {
            const ImVec2 key_size = ImGui::CalcTextSize(key.data(), key.data() + key.size());
            const ImVec2 minimum{x, y};
            const ImVec2 maximum{x + key_size.x + keycap_padding * 2.0f, y + keycap_height};
            draw_list.AddRectFilled({minimum.x + 1.0f, minimum.y + 1.0f}, {maximum.x + 1.0f, maximum.y + 1.0f}, keycap_shadow, 4.0f);
            draw_list.AddRectFilled(minimum, maximum, keycap_background, 4.0f);
            draw_list.AddRect(minimum, maximum, keycap_border, 4.0f, 0, 1.0f);
            add_text({minimum.x + keycap_padding, minimum.y + (keycap_height - key_size.y) * 0.5f}, primary, key);
            const ImVec2 action_size = ImGui::CalcTextSize(action.data(), action.data() + action.size());
            const ImVec2 action_position{maximum.x + label_spacing, minimum.y + (keycap_height - action_size.y) * 0.5f};
            add_text(action_position, secondary, action);
            x = action_position.x + action_size.x + shortcut_spacing;
        };
        float shortcut_x = status_left;
        add_shortcut(shortcut_x, "Esc", "Exit");
        add_shortcut(shortcut_x, "Tab", "Settings");
        add_shortcut(shortcut_x, "F", "Frame");
        y += keycap_height + 5.0f;
        shortcut_x = status_left;
        add_shortcut(shortcut_x, "Num1", "Front");
        add_shortcut(shortcut_x, "Num3", "Right");
        add_shortcut(shortcut_x, "Num7", "Top");
        add_shortcut(shortcut_x, "Num0", "Camera");
        y += keycap_height + 5.0f;
        shortcut_x = status_left;
        add_shortcut(shortcut_x, "G", "Axes · Hold");
        if (this->context.render_engine.selected_descriptor() == render::rasterizer_descriptor) add_shortcut(shortcut_x, "W", "Wireframe · Hold");
        if (simulation_active && !this->context.simulation.faulted()) add_shortcut(shortcut_x, "Space", "Play / Pause");
        y += keycap_height;

        if (simulation_active) {
            const simulation::SimulationTimeline timeline = this->context.simulation.timeline();
            const auto simulation_sample_time             = std::chrono::steady_clock::now();
            const bool simulation_running                 = this->context.simulation.running() && !this->context.simulation.faulted();
            if (!simulation_running || !this->controls.simulation_sample_initialized || timeline.step < this->controls.simulation_sample_step) {
                this->controls.simulation_sample_time        = simulation_sample_time;
                this->controls.simulation_sample_step        = timeline.step;
                this->controls.simulation_frames_per_second  = 0.0f;
                this->controls.simulation_sample_initialized = true;
            } else if (const double sample_seconds = std::chrono::duration<double>(simulation_sample_time - this->controls.simulation_sample_time).count(); sample_seconds >= 0.5) {
                this->controls.simulation_frames_per_second = static_cast<float>(static_cast<double>(timeline.step - this->controls.simulation_sample_step) / sample_seconds);
                this->controls.simulation_sample_time       = simulation_sample_time;
                this->controls.simulation_sample_step       = timeline.step;
            }
            add_section("SIMULATION");
            const char* state = this->context.simulation.faulted() ? "STOPPED" : this->context.simulation.running() ? "PLAYING" : "PAUSED";
            add_line(std::format("Simulation {:.1f} FPS", this->controls.simulation_frames_per_second), primary);
            add_line(std::format("Step {}  ·  {:.3f} s  ·  {}", timeline.step, timeline.seconds, state), this->context.simulation.faulted() ? ImGui::GetColorU32({0.96f, 0.38f, 0.33f, 1.0f}) : this->context.simulation.running() ? ImGui::GetColorU32({0.35f, 0.84f, 0.55f, 1.0f}) : secondary);
            add_line(std::format("System {}  ·  Provider {}", selected_simulation_system->name, selected_simulation_provider->id), secondary);
            add_line(std::format("DLL built {:%F %T %Ez}", std::chrono::zoned_time{std::chrono::current_zone(), selected_simulation_provider->build_time}), secondary);
        } else this->controls.simulation_sample_initialized = false;
        if (render_progress) {
            add_section("PATH TRACER");
            add_line(std::format("{} / {} spp{}", render_progress->completed, render_progress->target, render_progress->paused ? "  ·  PAUSED" : ""), primary);
        }
        if (preparation) {
            const char* stage{};
            switch (preparation->stage) {
            case render::PathTracerPreparationStage::LoadingShaders: stage = "Loading shaders"; break;
            case render::PathTracerPreparationStage::CreatingRayTracingModules: stage = "Creating ray tracing modules"; break;
            case render::PathTracerPreparationStage::CompilingRayTracingPipeline: stage = "Compiling ray tracing pipeline"; break;
            case render::PathTracerPreparationStage::CreatingComputeShaders: stage = "Creating compute shaders"; break;
            case render::PathTracerPreparationStage::CreatingShaderBindingTable: stage = "Creating shader binding table"; break;
            case render::PathTracerPreparationStage::CompilingSampler: stage = "Compiling sampler"; break;
            case render::PathTracerPreparationStage::CompilingFilter: stage = "Compiling filter"; break;
            case render::PathTracerPreparationStage::CompilingTextures: stage = "Compiling textures"; break;
            case render::PathTracerPreparationStage::CompilingMaterials: stage = "Compiling materials"; break;
            case render::PathTracerPreparationStage::CompilingMedia: stage = "Compiling media"; break;
            case render::PathTracerPreparationStage::CompilingLights: stage = "Compiling lights"; break;
            case render::PathTracerPreparationStage::CompilingGeometry: stage = "Compiling geometry"; break;
            case render::PathTracerPreparationStage::BuildingLightBvh: stage = "Building light BVH"; break;
            case render::PathTracerPreparationStage::AssemblingScene: stage = "Assembling scene"; break;
            case render::PathTracerPreparationStage::UploadingScene: stage = "Uploading scene"; break;
            case render::PathTracerPreparationStage::AllocatingRenderSession: stage = "Allocating render session"; break;
            case render::PathTracerPreparationStage::Ready: stage = "Ready"; break;
            }
            add_section("PATH TRACER PREPARATION");
            if (preparation->total == 0) add_line(std::format("{}  ·  {:.1f} s", stage, std::chrono::duration<float>(std::chrono::steady_clock::now() - preparation->started).count()), primary);
            else add_line(std::format("{}  ·  {} / {}", stage, preparation->completed, preparation->total), primary);
        }
        if (entity) {
            add_section("SELECTION");
            const scene::Scene& scene           = this->context.document.evaluated;
            const scene::EntityKind entity_type = scene::entity_kind(*entity);
            const std::uint64_t entity_id       = scene::entity_id(*entity);
            add_line(std::format("{}  ·  {} {}", entity_name(scene, *entity), entity_kind_name(*entity), entity_id), primary);
            if (this->context.viewport.view.selection.selected.size() > 1) add_line(std::format("{} objects selected", this->context.viewport.view.selection.selected.size()), secondary);
            if (entity_type == scene::EntityKind::Instance) {
                const scene::Instance& instance = *std::ranges::find(scene.resources.instances, std::get<scene::InstanceId>(entity->data), &scene::Instance::id);
                add_line(std::format("Prototype {}  ·  {}", instance.prototype.value, instance.visible ? "Visible" : "Hidden"), secondary);
            } else if (entity_type == scene::EntityKind::Camera) {
                const scene::Camera& camera = *std::ranges::find(scene.resources.cameras, std::get<scene::CameraId>(entity->data), &scene::Camera::id);
                if (const simulation::CameraReferenceImage* reference = this->context.simulation.camera_reference(camera.id)) {
                    add_line(std::format("{}  ·  Camera {} / {}", reference->group, reference->index + 1u, reference->count), primary);
                    add_line(std::format("{} × {}  ·  fx {:.3f}  fy {:.3f}", reference->extent[0], reference->extent[1], reference->focal.x, reference->focal.y), secondary);
                    add_line(std::format("cx {:.3f}  cy {:.3f}", reference->principal.x, reference->principal.y), secondary);
                } else
                    std::visit(
                        [&add_line, primary, secondary](const auto& data) {
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) add_line(std::format("Perspective  ·  {:.2f} deg", data.vertical_fov), primary);
                            else add_line("Orthographic", primary);
                            add_line(std::format("Near {:.5g}  ·  Far {:.5g}", data.near_plane, data.far_plane), secondary);
                            add_line(std::format("Focus {:.5g}  ·  Lens {:.5g}", data.focal_distance, data.lens_radius), secondary);
                        },
                        camera.data);
            } else if (entity_type == scene::EntityKind::Light || entity_type == scene::EntityKind::AreaEmitter) {
                const scene::Light& light = *std::ranges::find(scene.resources.lights, scene::LightId{entity_id}, &scene::Light::id);
                std::visit(
                    [&add_line, entity, entity_type, primary](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight>) add_line(std::format("Point  ·  Scale {:.5g}", data.scale), primary);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) add_line(std::format("Spot  ·  {:.3g} / {:.3g} deg  ·  Scale {:.5g}", data.cone_angle - data.cone_delta, data.cone_angle, data.scale), primary);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>) add_line(std::format("Distant  ·  Scale {:.5g}", data.scale), primary);
                        else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>) {
                            if (entity_type == scene::EntityKind::AreaEmitter) add_line(std::format("Diffuse Area  ·  {}  ·  Instance {}", data.sidedness == scene::EmissionSidedness::Both ? "two-sided" : "front", std::get<scene::EntityReference::AreaEmitter>(entity->data).instance.value), primary);
                            else add_line(std::format("Diffuse Area  ·  {}", data.sidedness == scene::EmissionSidedness::Both ? "two-sided" : "front"), primary);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InfiniteLight>) add_line(std::format("Infinite  ·  Scale {:.5g}", data.scale), primary);
                        else add_line(std::format("Portal Infinite  ·  {} portals  ·  Scale {:.5g}", data.portals.size(), data.environment.scale), primary);
                    },
                    light.data);
            } else if (entity_type == scene::EntityKind::Volume) {
                const scene::Scene& evaluated = this->context.document.evaluated;
                const scene::Volume& volume   = *std::ranges::find(evaluated.resources.volumes, std::get<scene::VolumeId>(entity->data), &scene::Volume::id);
                if (const auto* grid = std::get_if<scene::GridVolume>(&volume.data)) {
                    add_line(std::format("{} × {} × {}  ·  {} fields", grid->resolution.x, grid->resolution.y, grid->resolution.z, grid->fields.size()), primary);
                    for (const scene::VolumeField& field : grid->fields) {
                        const scene::FieldKind field_type = scene::field_kind(field);
                        const char* kind                  = field_type == scene::FieldKind::Float ? "scalar" : field_type == scene::FieldKind::Float3 ? "vector" : field_type == scene::FieldKind::UInt32 ? "category" : "MAC vector";
                        add_line(std::format("{}  ·  {}{}{}", field.name, kind, field.unit.empty() ? "" : "  ·  ", field.unit), secondary);
                    }
                }
            } else if (entity_type == scene::EntityKind::ParticleSet) {
                const scene::ParticleSet& particles = *std::ranges::find(scene.resources.particle_sets, std::get<scene::ParticleSetId>(entity->data), &scene::ParticleSet::id);
                add_line(std::format("Radius {:.5g}  ·  {} fields", particles.radius, particles.fields.size()), primary);
                for (const scene::ParticleField& field : particles.fields) {
                    const scene::FieldKind field_type = scene::field_kind(field);
                    const char* kind                  = field_type == scene::FieldKind::Float ? "scalar" : field_type == scene::FieldKind::Float3 ? "vector" : "category";
                    add_line(std::format("{}  ·  {}{}{}", field.name, kind, field.unit.empty() ? "" : "  ·  ", field.unit), secondary);
                }
            } else if (entity_type == scene::EntityKind::NeuralField) {
                add_line("Hash Grid Radiance Field  ·  canonical v1", primary);
            }
        }
        const float status_bottom = y;
        draw_list.PopClipRect();

        if (this->context.viewport.settings.telemetry_visible && simulation_active) {
            if (!selected_simulation_provider->telemetry.empty() && bottom - status_bottom >= line_height * 3.0f) {
                const simulation::TelemetrySnapshot& telemetry = this->context.simulation.telemetry(this->simulation_panel.selected_system);
                std::size_t row_count                          = 2;
                std::string section{};
                for (std::size_t index = 0; index < selected_simulation_provider->telemetry.size(); ++index) {
                    if (!telemetry.values[index]) continue;
                    if (selected_simulation_provider->telemetry[index].section_id != section) {
                        section = selected_simulation_provider->telemetry[index].section_id;
                        ++row_count;
                    }
                    ++row_count;
                }

                constexpr float column_width      = 370.0f;
                const float telemetry_top         = status_bottom + line_height;
                const std::size_t rows_per_column = std::max<std::size_t>(2, static_cast<std::size_t>((bottom - telemetry_top) / line_height));
                const float content_height        = static_cast<float>(std::min(row_count, rows_per_column)) * line_height;
                const float start_y               = bottom - content_height;
                const float telemetry_left        = layout.global_panel.visible && layout.global_panel.position.y + layout.global_panel.size.y > start_y ? status_left : viewport_left;
                const float telemetry_right       = layout.selection_panel.visible && layout.selection_panel.position.y + layout.selection_panel.size.y > start_y ? status_right : viewport_right;
                float column_x                    = telemetry_left;
                float telemetry_y                 = start_y;
                std::size_t row                   = 0;
                draw_list.PushClipRect({telemetry_left, telemetry_top}, {telemetry_right, bottom}, true);
                const auto next_column = [&column_x, &telemetry_y, &row, start_y] {
                    column_x += column_width;
                    telemetry_y = start_y;
                    row         = 0;
                };
                const auto add_telemetry_line = [&add_text, &column_x, &telemetry_y, &row, line_height](const std::string_view value, const ImU32 color) {
                    add_text({column_x, telemetry_y}, color, value);
                    telemetry_y += line_height;
                    ++row;
                };
                const auto scalar_value = [](const simulation::TelemetryValue& value) {
                    if (value.kind == simulation::TelemetryKind::Boolean || value.kind == simulation::TelemetryKind::Integer) return static_cast<float>(value.integer);
                    if (value.kind == simulation::TelemetryKind::Float) return static_cast<float>(value.floating[0]);
                    return static_cast<float>(std::sqrt(value.floating[0] * value.floating[0] + value.floating[1] * value.floating[1] + value.floating[2] * value.floating[2]));
                };

                add_telemetry_line("TELEMETRY", heading);
                add_telemetry_line(selected_simulation_system->name, primary);
                section.clear();
                for (std::size_t index = 0; index < selected_simulation_provider->telemetry.size(); ++index) {
                    if (!telemetry.values[index]) continue;
                    const simulation::TelemetryDescriptor& metric = selected_simulation_provider->telemetry[index];
                    if (metric.section_id != section) {
                        section = metric.section_id;
                        if (row + 2 > rows_per_column) next_column();
                        add_telemetry_line(section, heading);
                    } else if (row == rows_per_column) {
                        next_column();
                    }

                    const simulation::TelemetryValue& value = *telemetry.values[index];
                    std::string formatted{};
                    if (value.kind == simulation::TelemetryKind::Boolean) formatted = value.integer == 0 ? "false" : "true";
                    else if (value.kind == simulation::TelemetryKind::Integer) formatted = std::to_string(value.integer);
                    else if (value.kind == simulation::TelemetryKind::Float) formatted = std::format("{:.6g}", value.floating[0]);
                    else formatted = std::format("[{:+.4g}, {:+.4g}, {:+.4g}]", value.floating[0], value.floating[1], value.floating[2]);
                    if (!metric.unit.empty()) formatted = std::format("{} {}", formatted, metric.unit);

                    draw_list.PushClipRect({column_x, telemetry_y}, {column_x + 148.0f, telemetry_y + line_height}, true);
                    add_text({column_x, telemetry_y}, secondary, metric.name);
                    draw_list.PopClipRect();
                    draw_list.PushClipRect({column_x + 152.0f, telemetry_y}, {column_x + 262.0f, telemetry_y + line_height}, true);
                    add_text({column_x + 152.0f, telemetry_y}, primary, formatted);
                    draw_list.PopClipRect();

                    if (metric.plot && telemetry.history.size() > 1) {
                        const ImVec2 plot_minimum{column_x + 268.0f, telemetry_y + 2.0f};
                        const ImVec2 plot_maximum{column_x + 364.0f, telemetry_y + line_height - 2.0f};
                        float minimum = scalar_value(telemetry.history.front().values[index]);
                        float maximum = minimum;
                        for (const simulation::TelemetrySample& sample : telemetry.history) {
                            const float sample_value = scalar_value(sample.values[index]);
                            minimum                  = std::min(minimum, sample_value);
                            maximum                  = std::max(maximum, sample_value);
                        }
                        const float range = maximum == minimum ? 1.0f : maximum - minimum;
                        draw_list.AddLine({plot_minimum.x, (plot_minimum.y + plot_maximum.y) * 0.5f}, {plot_maximum.x, (plot_minimum.y + plot_maximum.y) * 0.5f}, ImGui::GetColorU32({0.58f, 0.66f, 0.72f, 0.24f}));
                        draw_list.PathClear();
                        for (std::size_t sample_index = 0; sample_index < telemetry.history.size(); ++sample_index) {
                            const float normalized_x = static_cast<float>(sample_index) / static_cast<float>(telemetry.history.size() - 1);
                            const float normalized_y = (scalar_value(telemetry.history[sample_index].values[index]) - minimum) / range;
                            draw_list.PathLineTo({std::lerp(plot_minimum.x, plot_maximum.x, normalized_x), std::lerp(plot_maximum.y, plot_minimum.y, normalized_y)});
                        }
                        draw_list.PathStroke(ImGui::GetColorU32({0.28f, 0.79f, 0.90f, 0.88f}), 0, 1.25f);
                    }
                    telemetry_y += line_height;
                    ++row;
                }
                draw_list.PopClipRect();
            }
        }
        draw_list.PopClipRect();
    }

    void Ui::draw_playback_controls(const ControlRect& controls) {
        if (!controls.visible) return;
        const bool simulation_active                                = this->context.simulation.initialized();
        const std::optional<render::RenderProgress> render_progress = simulation_active ? std::nullopt : this->context.render_engine.progress();
        if (!simulation_active && !render_progress) return;

        ImGui::SetCursorScreenPos(controls.position);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{playback_spacing, 0.0f});
        ImDrawList* draw_list          = ImGui::GetWindowDrawList();
        const auto draw_button_surface = [draw_list] {
            const ImVec2 minimum = ImGui::GetCursorScreenPos();
            const ImVec2 maximum{minimum.x + playback_button_size.x, minimum.y + playback_button_size.y};
            draw_list->AddRectFilled({minimum.x + 1.0f, minimum.y + 2.0f}, {maximum.x + 1.0f, maximum.y + 2.0f}, ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.24f}), 6.0f);
            draw_list->AddRectFilled(minimum, maximum, ImGui::GetColorU32({0.025f, 0.035f, 0.045f, 0.34f}), 6.0f);
            draw_list->AddRect(minimum, maximum, ImGui::GetColorU32({0.56f, 0.65f, 0.72f, 0.18f}), 6.0f, 0, 1.0f);
        };

        if (simulation_active) {
            const bool simulation_faulted = this->context.simulation.faulted();
            const bool simulation_playing = this->context.simulation.running();
            ImGui::BeginDisabled(simulation_faulted);
            draw_button_surface();
            if (icon_button("##SimulationPlayback", playback_button_size, simulation_playing ? Icon::Pause : Icon::Play, simulation_playing ? "Pause" : "Play", simulation_playing ? ImVec4{0.35f, 0.84f, 0.55f, 1.0f} : ImVec4{0.28f, 0.79f, 0.90f, 0.92f})) {
                try {
                    if (simulation_playing) this->context.simulation.pause();
                    else this->context.simulation.start();
                } catch (const std::exception& error) {
                    this->notify(error.what(), true);
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            draw_button_surface();
            if (icon_button("##SimulationStep", playback_button_size, Icon::Step, "Step")) {
                try {
                    this->context.simulation.step();
                } catch (const std::exception& error) {
                    this->notify(error.what(), true);
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(simulation_playing);
            draw_button_surface();
            if (icon_button("##SimulationReset", playback_button_size, Icon::Reset, "Reset")) {
                try {
                    this->context.simulation.reset();
                } catch (const std::exception& error) {
                    this->notify(error.what(), true);
                }
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
        } else {
            draw_button_surface();
            if (icon_button("##PathPlayback", playback_button_size, render_progress->paused ? Icon::Play : Icon::Pause, render_progress->paused ? "Resume" : "Pause", render_progress->paused ? ImVec4{0.91f, 0.72f, 0.29f, 1.0f} : ImVec4{})) this->context.render_engine.set_paused(!render_progress->paused);
            ImGui::SameLine();
            draw_button_surface();
            if (icon_button("##PathReset", playback_button_size, Icon::Reset, "Reset")) this->context.render_engine.reset();
        }
        ImGui::PopStyleVar();
    }

    void Ui::draw_top_strip(const ImVec2 position, const ImVec2 size, Actions& actions) {
        ImGui::SetNextWindowPos(ImVec2{position.x + 6.0f, position.y + 5.0f});
        ImGui::SetNextWindowSize(ImVec2{size.x - 12.0f, top_strip_height});
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{6.0f, 4.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{4.0f, 4.0f});
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus;
        ImGui::Begin("##ApplicationStrip", nullptr, flags);
        const bool loaded                                                                 = this->context.document.loaded;
        const std::optional<render::RenderProgress> render_progress                       = loaded ? this->context.render_engine.progress() : std::nullopt;
        const std::optional<render::PathTracerPreparationProgress> pathtracer_preparation = loaded ? this->context.render_engine.pathtracer_preparation() : std::nullopt;
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

        std::string identity = loaded ? this->context.document.path.filename().string() : "Open Scene...";
        if (loaded && identity.empty()) identity = this->context.document.authored.name;
        const float identity_width = std::clamp(ImGui::CalcTextSize(identity.c_str()).x + 24.0f, 96.0f, 240.0f);
        if (text_button("##SceneIdentity", identity.c_str(), ImVec2{identity_width, 27.0f})) {
            if (loaded) ImGui::OpenPopup("##SceneMenu");
            else actions.open_scene_file = true;
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
        if (this->context.document.modified) {
            const ImVec2 minimum           = ImGui::GetItemRectMin();
            const ImVec2 maximum           = ImGui::GetItemRectMax();
            const float visible_text_width = std::min(ImGui::CalcTextSize(identity.c_str()).x, identity_width - 20.0f);
            const float text_right         = minimum.x + (identity_width + visible_text_width) * 0.5f;
            ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2{std::min(text_right + 7.0f, maximum.x - 4.0f), (minimum.y + maximum.y) * 0.5f}, 2.5f, ImGui::GetColorU32(ImVec4{0.16f, 0.72f, 0.84f, 1.0f}), 12);
        }
        if (ImGui::BeginPopup("##SceneMenu")) {
            if (ImGui::MenuItem("Open File...")) actions.open_scene_file = true;
            if (ImGui::MenuItem("Reload")) actions.reload_scene = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Save")) actions.save_scene = true;
            if (ImGui::MenuItem("Save As...")) actions.save_scene_as = true;
            ImGui::EndPopup();
        }

        const float center                = ImGui::GetWindowWidth() * 0.5f;
        const float right                 = ImGui::GetWindowWidth();
        constexpr float mode_button_width = 80.0f;
        constexpr std::array renderer_descriptors{render::rasterizer_descriptor, render::pathtracer_descriptor};
        const float mode_width = mode_button_width * static_cast<float>(renderer_descriptors.size()) + ImGui::GetStyle().ItemSpacing.x * static_cast<float>(renderer_descriptors.size() - 1u);
        const float mode_start = center - mode_width * 0.5f;
        ImGui::SameLine(mode_start);
        const render::RendererDescriptor selected_renderer = this->context.render_engine.selected_descriptor();
        for (std::size_t index = 0; index < renderer_descriptors.size(); ++index) {
            const render::RendererDescriptor renderer = renderer_descriptors[index];
            const std::string identifier              = std::format("##Renderer{}", renderer.id);
            if (text_button(identifier.c_str(), renderer.name.data(), ImVec2{mode_button_width, 27.0f}, selected_renderer == renderer)) actions.renderer = renderer.kind;
            if (index + 1u < renderer_descriptors.size()) ImGui::SameLine();
        }

        constexpr float exposure_width = 84.0f;
        constexpr float edge_margin    = 4.0f;
        const float exposure_start     = right - edge_margin - exposure_width;
        ImGui::SameLine(exposure_start);
        const FlatButtonInteraction exposure = flat_button("##Exposure", ImVec2{exposure_width, 27.0f});
        if (exposure.active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) this->context.viewport.settings.exposure = std::clamp(this->context.viewport.settings.exposure + ImGui::GetIO().MouseDelta.x * 0.05f, -20.0f, 20.0f);
        if (exposure.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) this->context.viewport.settings.exposure = 0.0f;
        const std::string exposure_text = std::format("{:+.2f} EV", this->context.viewport.settings.exposure);
        const ImVec2 exposure_text_size = ImGui::CalcTextSize(exposure_text.c_str());
        const ImVec2 exposure_text_position{
            exposure.minimum.x + (exposure_width - exposure_text_size.x) * 0.5f,
            exposure.minimum.y + (27.0f - exposure_text_size.y) * 0.5f + exposure.vertical_offset,
        };
        ImGui::GetWindowDrawList()->AddText(ImVec2{exposure_text_position.x + 1.0f, exposure_text_position.y + 1.0f}, exposure.shadow, exposure_text.c_str());
        ImGui::GetWindowDrawList()->AddText(exposure_text_position, exposure.color, exposure_text.c_str());
        const float strip_offset_x        = ImGui::GetWindowPos().x - position.x;
        const float left_drag_start       = strip_offset_x + source_end + 4.0f;
        const float mode_client_start     = strip_offset_x + mode_start;
        const float mode_client_end       = mode_client_start + mode_width;
        const float right_drag_start      = mode_client_end + 4.0f;
        const float exposure_client_start = strip_offset_x + exposure_start;
        actions.window_drag_regions       = {{
            {left_drag_start, 0.0f, std::max(left_drag_start, mode_client_start - 4.0f), top_strip_height + 5.0f},
            {right_drag_start, 0.0f, std::max(right_drag_start, exposure_client_start - 4.0f), top_strip_height + 5.0f},
        }};
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void Ui::draw_selection_panel(const PanelRect& panel, const bool transform_editable) {
        const scene::EntityReference* entity = active_entity(*this);
        if (!entity) return;
        const bool has_transform = entity_transform(this->context.document.evaluated, *entity).has_value();
        const std::string& name  = entity_name(this->context.document.evaluated, *entity);
        if (has_transform) this->synchronize_transform();
        ImGui::SetNextWindowPos(panel.position);
        ImGui::SetNextWindowSizeConstraints({panel.size.x, 0.0f}, {panel.size.x, panel.maximum_height});
        push_panel_style();
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::Begin("##SelectionPanel", nullptr, flags);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const ImVec2 header_position = ImGui::GetCursorScreenPos();
        const float header_width     = ImGui::GetContentRegionAvail().x;
        constexpr float status_width = 88.0f;
        const float name_right       = header_position.x + header_width - (has_transform && !transform_editable ? status_width + 8.0f : 0.0f);
        draw_list->PushClipRect(header_position, ImVec2{name_right, header_position.y + ImGui::GetTextLineHeight()}, true);
        draw_list->AddText(header_position, ImGui::GetColorU32(ImVec4{0.92f, 0.95f, 0.98f, 1.0f}), name.c_str());
        draw_list->PopClipRect();
        ImGui::Dummy(ImVec2{header_width, ImGui::GetTextLineHeight()});
        ImGui::TextDisabled("%s  ·  %llu", entity_kind_name(*entity), scene::entity_id(*entity));
        if (has_transform && !transform_editable) {
            const ImVec2 status_minimum{header_position.x + header_width - status_width, header_position.y - 2.0f};
            const ImVec2 status_maximum{status_minimum.x + status_width, status_minimum.y + 20.0f};
            draw_list->AddRectFilled(status_minimum, status_maximum, ImGui::GetColorU32(ImVec4{0.91f, 0.65f, 0.24f, 0.14f}), 10.0f);
            draw_list->AddRect(status_minimum, status_maximum, ImGui::GetColorU32(ImVec4{0.91f, 0.65f, 0.24f, 0.42f}), 10.0f);
            const scene::CameraId* selected_camera = std::get_if<scene::CameraId>(&entity->data);
            const char* status                     = selected_camera && this->context.simulation.controls(*selected_camera) ? "PROVIDER" : "SIMULATED";
            const ImVec2 status_text_size          = ImGui::CalcTextSize(status);
            draw_list->AddText(ImVec2{status_minimum.x + (status_width - status_text_size.x) * 0.5f, status_minimum.y + (20.0f - status_text_size.y) * 0.5f}, ImGui::GetColorU32(ImVec4{0.96f, 0.74f, 0.36f, 1.0f}), status);
        }
        const ImVec2 separator = ImGui::GetCursorScreenPos();
        draw_list->AddLine(ImVec2{separator.x, separator.y + 2.0f}, ImVec2{separator.x + header_width, separator.y + 2.0f}, ImGui::GetColorU32(ImVec4{0.40f, 0.49f, 0.57f, 0.22f}));
        ImGui::Dummy(ImVec2{header_width, 8.0f});
        if (has_transform) {
            ImGui::BeginDisabled(!transform_editable);
            const bool translating = this->controls.gizmo_operation == ImGuizmo::TRANSLATE;
            const bool rotating    = this->controls.gizmo_operation == ImGuizmo::ROTATE;
            const bool scaling     = this->controls.gizmo_operation == ImGuizmo::SCALE;
            if (icon_button("##Translate", ImVec2{38.0f, 34.0f}, Icon::Translate, nullptr, translating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, translating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::TRANSLATE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate");
            ImGui::SameLine();
            if (icon_button("##Rotate", ImVec2{38.0f, 34.0f}, Icon::Rotate, nullptr, rotating ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, rotating, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::ROTATE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate");
            ImGui::SameLine();
            if (icon_button("##Scale", ImVec2{38.0f, 34.0f}, Icon::Scale, nullptr, scaling ? ImVec4{0.16f, 0.72f, 0.84f, 1.0f} : ImVec4{}, scaling, false, 0.55f)) this->controls.gizmo_operation = ImGuizmo::SCALE;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale");
            ImGui::Spacing();
            this->transform_row("Position", "POSITION", this->controls.translation, 0.01f, "%.3f");
            this->transform_row("Rotation", "ROTATION", this->controls.rotation, 0.1f, "%.1f\xc2\xb0");
            this->transform_row("Scale", "SCALE", this->controls.scale, 0.01f, "%.3f");
            ImGui::EndDisabled();
        }
        if (has_transform) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        const scene::CameraId* selected_camera        = std::get_if<scene::CameraId>(&entity->data);
        render::EntityDiagnostics& entity_diagnostics = this->context.viewport.settings.entity_diagnostics.try_emplace(*entity).first->second;
        if (selected_camera && this->context.simulation.camera_reference(*selected_camera)) {
            ImGui::TextDisabled("CAMERA");
            const bool viewing = this->context.viewport.view.source == CameraSource::Scene && this->context.viewport.view.scene_camera == *selected_camera;
            if (text_button("##ViewThrough", viewing ? "Return to Viewport" : "View Through", ImVec2{ImGui::GetContentRegionAvail().x, 27.0f})) {
                if (viewing) this->context.viewport.toggle_scene_camera();
                else this->context.viewport.view_camera(*selected_camera);
            }
            ImGui::Spacing();
            ImGui::TextDisabled("REFERENCE");
            ImGui::Checkbox("GT Overlay", &entity_diagnostics.camera_gt_overlay);
            ImGui::Checkbox("GT Image Plane", &entity_diagnostics.camera_gt_plane);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        this->inspector.draw(*entity);
        this->controls.selection_panel_height = ImGui::GetWindowHeight();
        ImGui::End();
        pop_panel_style();
    }

    void Ui::draw_view_settings(Actions& actions) {
        ImGui::TextDisabled("HUD");
        ImGui::Checkbox("Viewport HUD", &this->context.viewport.settings.hud_visible);
        ImGui::BeginDisabled(!this->context.viewport.settings.hud_visible);
        ImGui::Checkbox("Telemetry", &this->context.viewport.settings.telemetry_visible);
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("VIEWPORT");
        int camera_source                      = static_cast<int>(this->context.viewport.view.source);
        constexpr const char* camera_sources[] = {"Scene Camera", "Viewport Camera"};
        if (ImGui::Combo("Camera", &camera_source, camera_sources, 2)) this->context.viewport.view.source = static_cast<CameraSource>(camera_source);
        ImGui::Checkbox("Scene guides", &this->context.viewport.settings.guides_visible);
        ImGui::BeginDisabled(!this->context.viewport.settings.guides_visible);
        ImGui::Checkbox("Selection outline", &this->context.viewport.settings.selection_outline);
        ImGui::Checkbox("All bounds", &this->context.viewport.settings.scene_guides.all_bounds);
        ImGui::Checkbox("Cameras", &this->context.viewport.settings.scene_guides.cameras);
        ImGui::Checkbox("Lights", &this->context.viewport.settings.scene_guides.lights);
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::TextDisabled("RASTERIZER");
        ImGui::BeginDisabled(this->context.render_engine.selected_descriptor() != render::rasterizer_descriptor);
        int display_mode                      = static_cast<int>(this->context.render_engine.raster_display_mode());
        constexpr const char* display_modes[] = {"Material", "Wireframe"};
        if (ImGui::Combo("Display", &display_mode, display_modes, 2)) actions.raster_display_mode = static_cast<render::RasterDisplayMode>(display_mode);
        ImGui::EndDisabled();
    }

    void Ui::draw_global_panel(const PanelRect& panel, Actions& actions) {
        ImGui::SetNextWindowPos(panel.position);
        ImGui::SetNextWindowSizeConstraints({panel.size.x, 0.0f}, {panel.size.x, panel.maximum_height});
        push_panel_style();
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_AlwaysAutoResize;
        ImGui::Begin("##GlobalControls", nullptr, flags);
        ImGui::TextDisabled("GLOBAL  ·  TAB");
        if (ImGui::BeginTabBar("##GlobalControlTabs")) {
            if (ImGui::BeginTabItem("View")) {
                ImGui::Spacing();
                this->draw_view_settings(actions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Simulation")) {
                ImGui::Spacing();
                this->simulation_panel.draw();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        this->controls.global_panel_height = ImGui::GetWindowHeight();
        ImGui::End();
        pop_panel_style();
    }

    void Ui::draw_status_toast(const ImVec2 position, const ImVec2 size) {
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

} // namespace spectra::editor
