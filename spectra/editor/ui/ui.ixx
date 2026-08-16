module;

#include <imgui.h>

#include <ImGuizmo.h>

export module spectra.editor.ui;

import spectra.editor.ui.imgui;
import spectra.editor.viewport.interaction;
import spectra.editor.viewport.picker;
import spectra.render;
import spectra.render.composition.diagnostics;
import spectra.dynamics.runtime;
import spectra.scene;
import spectra.scene.document;
import std;

namespace spectra {
    export struct EditorViewportSettings {
        float exposure{};
        bool guides_visible{true};
        bool hud_visible{true};
        bool telemetry_visible{};
        bool selection_outline{true};
        SceneGuideSettings scene_guides{};
        SelectionDiagnosticSettings selection_diagnostics{};
    };

    export struct EditorActions {
        bool exit_application{};
        bool open_scene_file{};
        bool reload_scene{};
        bool save_scene{};
        bool save_scene_as{};
        bool rebuild_dynamic_rendering{};
        bool show_axes{};
        std::optional<std::string> renderer{};
        std::optional<RasterDisplayMode> raster_display_mode{};
        std::array<std::array<float, 4>, 2> window_drag_regions{};
    };

    export struct EditorUi {
        EditorUi(SceneDocument& document, EditorViewportSettings& settings, DynamicsRuntime& dynamics, RenderEngine& render_engine, ViewportInteraction& viewport, ViewportPicker& picker, ImGuiBackend& imgui) noexcept;

        void notify(std::string message, bool error = false);
        [[nodiscard]] EditorActions draw_editor_ui();

        struct {
            SceneDocument& document;
            EditorViewportSettings& settings;
            DynamicsRuntime& dynamics;
            RenderEngine& render_engine;
            ViewportInteraction& viewport;
            ViewportPicker& picker;
            ImGuiBackend& imgui;
        } context;

        struct {
            std::string status{};
            bool status_error{};
            bool global_panel_open{};
            float global_panel_height{};
            float selection_panel_height{};
            ImGuizmo::OPERATION gizmo_operation{ImGuizmo::TRANSLATE};
            bool gizmo_active{};
            bool transform_drag_active{};
            bool transform_editable{true};
            std::optional<SceneEntityReference> transform_entity{};
            std::optional<RasterDisplayMode> wireframe_restore_mode{};
            std::uint64_t transform_revision{};
            std::array<float, 3> translation{};
            std::array<float, 3> rotation{};
            std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
            std::string observed_status{};
            bool observed_status_error{};
            double status_since{};
            std::size_t selected_dynamic_system{};
            std::uint64_t observed_dynamic_revision{};
            std::chrono::steady_clock::time_point physics_sample_time{};
            std::uint64_t physics_sample_step{};
            float physics_frames_per_second{};
            bool physics_sample_initialized{};
            std::vector<scene::DynamicParameterSetting> parameter_drafts{};
            bool reset_pending{};
            bool recreate_pending{};
            bool rebuild_dynamic_rendering{};
        } controls;

    private:
        struct PanelRect {
            ImVec2 position{};
            ImVec2 size{};
            float maximum_height{};
            bool visible{};
        };

        struct ControlRect {
            ImVec2 position{};
            ImVec2 size{};
            bool visible{};
        };

        struct ViewportLayout {
            ImVec2 position{};
            ImVec2 size{};
            PanelRect global_panel{};
            PanelRect selection_panel{};
            ControlRect playback_controls{};
        };

        void apply_dynamic_parameters(std::vector<scene::DynamicParameterSetting> parameters, bool reset);
        [[nodiscard]] ViewportLayout make_layout(ImVec2 position, ImVec2 size) const noexcept;
        [[nodiscard]] bool pointer_over_interface(const ViewportLayout& layout, bool show_axes) const noexcept;
        void synchronize_dynamic_controls();
        void synchronize_transform();
        void apply_transform(SceneEntityReference entity, math::Transform transform);
        void transform_row(const char* identifier, const char* title, std::array<float, 3>& value, float speed, const char* format);
        void handle_shortcuts(EditorActions& actions, float aspect, bool global_panel_available);
        void draw_orientation(ImVec2 position, ImVec2 size, bool show_axes);
        void draw_gizmo(ImVec2 minimum, ImVec2 size, bool blocked, bool transform_editable);
        void handle_viewport_input(ImVec2 minimum, ImVec2 size, bool blocked);
        void draw_viewport(const ViewportLayout& layout, bool show_axes, bool transform_editable);
        void draw_camera_gate(ImVec2 position, ImVec2 size, ImDrawList& draw_list) const;
        void draw_viewport_hud(const ViewportLayout& layout, ImDrawList& draw_list);
        void draw_playback_controls(const ControlRect& controls);
        void draw_top_strip(ImVec2 position, ImVec2 size, EditorActions& actions);
        void draw_selection_panel(const PanelRect& panel, bool transform_editable);
        void draw_selection_diagnostics(SceneEntityReference entity);
        void draw_particle_diagnostics(const scene::ParticleSet& particles);
        void draw_volume_diagnostics(const scene::Volume& volume);
        void draw_view_settings(EditorActions& actions);
        void draw_simulation_settings();
        void draw_global_panel(const PanelRect& panel, EditorActions& actions);
        void draw_status_toast(ImVec2 position, ImVec2 size);
    };
} // namespace spectra
