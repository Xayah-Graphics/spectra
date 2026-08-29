module;

#include <imgui.h>

#include <ImGuizmo.h>

export module spectra.editor.ui;

import spectra.simulation.runtime;
import spectra.editor.document;
import spectra.editor.ui.imgui;
import spectra.editor.ui.inspector;
import spectra.editor.ui.simulation;
import spectra.editor.viewport;
import spectra.editor.viewport.picker;
import spectra.render;
import spectra.render.display;
import spectra.scene;
import std;

namespace spectra::editor {
    export struct Actions {
        bool exit_application{};
        bool open_scene_file{};
        bool reload_scene{};
        bool save_scene{};
        bool save_scene_as{};
        bool rebuild_simulation_rendering{};
        bool show_axes{};
        std::optional<render::RendererKind> renderer{};
        std::optional<render::RasterDisplayMode> raster_display_mode{};
        std::array<std::array<float, 4>, 2> window_drag_regions{};
    };

    export struct Ui {
        Ui(Document& document, simulation::Runtime& simulation, render::Engine& render_engine, Viewport& viewport, Picker& picker, ImGuiBackend& imgui) noexcept;

        void notify(std::string message, bool error = false);
        [[nodiscard]] Actions draw();

        struct {
            Document& document;
            simulation::Runtime& simulation;
            render::Engine& render_engine;
            Viewport& viewport;
            Picker& picker;
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
            std::optional<scene::EntityReference> transform_entity{};
            std::optional<render::RasterDisplayMode> wireframe_restore_mode{};
            std::uint64_t transform_revision{};
            std::array<float, 3> translation{};
            std::array<float, 3> rotation{};
            std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
            std::string observed_status{};
            bool observed_status_error{};
            double status_since{};
            std::chrono::steady_clock::time_point simulation_sample_time{};
            std::uint64_t simulation_sample_step{};
            float simulation_frames_per_second{};
            bool simulation_sample_initialized{};
        } controls;

    private:
        InspectorPanel inspector;
        SimulationPanel simulation_panel;

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
            ControlRect presentation_sequence{};
        };

        [[nodiscard]] ViewportLayout make_layout(ImVec2 position, ImVec2 size) const noexcept;
        [[nodiscard]] bool pointer_over_interface(const ViewportLayout& layout, bool show_axes) const noexcept;
        void synchronize_transform();
        void apply_transform(scene::EntityReference entity, math::Transform transform);
        void transform_row(const char* identifier, const char* title, std::array<float, 3>& value, float speed, const char* format);
        void handle_shortcuts(Actions& actions, float aspect, bool global_panel_available);
        void draw_orientation(ImVec2 position, ImVec2 size, bool show_axes);
        void draw_gizmo(ImVec2 minimum, ImVec2 size, bool blocked, bool transform_editable);
        void handle_viewport_input(ImVec2 minimum, ImVec2 size, bool blocked);
        void draw_viewport(const ViewportLayout& layout, bool show_axes, bool transform_editable);
        void draw_camera_gate(ImVec2 position, ImVec2 size, ImDrawList& draw_list) const;
        void draw_viewport_hud(const ViewportLayout& layout, ImDrawList& draw_list);
        void draw_presentation_sequence(const ControlRect& controls);
        void draw_playback_controls(const ControlRect& controls);
        void draw_top_strip(ImVec2 position, ImVec2 size, Actions& actions);
        void draw_selection_panel(const PanelRect& panel, bool transform_editable);
        void draw_view_settings(Actions& actions);
        void draw_global_panel(const PanelRect& panel, Actions& actions);
        void draw_status_toast(ImVec2 position, ImVec2 size);
    };
} // namespace spectra::editor
