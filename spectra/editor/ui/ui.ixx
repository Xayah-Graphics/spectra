module;

#include <imgui.h>

#include <ImGuizmo.h>

export module spectra.editor.ui;

import spectra.editor.output.capture;
import spectra.editor.output.frozen_scene;
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
    export struct EditorActions {
        bool exit_application{};
        bool open_scene_file{};
        bool reload_scene{};
        bool save_scene{};
        bool save_scene_as{};
        bool export_frozen_scene{};
        bool show_axes{};
        std::optional<CaptureFormat> capture_format{};
        std::optional<std::string> renderer{};
        std::optional<RasterDisplayMode> raster_display_mode{};
        std::array<std::array<float, 4>, 2> window_drag_regions{};
    };

    export struct EditorUi {
        EditorUi(SceneDocument& document, SceneDiagnosticSettings& diagnostic_settings, DynamicsRuntime& dynamics, RenderEngine& render_engine, ViewportInteraction& viewport, ViewportPicker& picker, FrozenSceneExporter& frozen_export, ImGuiBackend& imgui) noexcept;

        [[nodiscard]] EditorActions draw_editor_ui();
        void notify(std::string message, bool error = false);

        struct {
            SceneDocument& document;
            SceneDiagnosticSettings& diagnostic_settings;
            DynamicsRuntime& dynamics;
            RenderEngine& render_engine;
            ViewportInteraction& viewport;
            ViewportPicker& picker;
            FrozenSceneExporter& frozen_export;
            ImGuiBackend& imgui;
        } context;

        struct {
            std::string status{};
            bool status_error{};
            bool inspector_open{};
            float exposure{};
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
            std::vector<scene::DynamicParameterSetting> parameter_drafts{};
            bool reset_pending{};
        } controls;

    private:
        void apply_dynamic_parameters(std::vector<scene::DynamicParameterSetting> parameters, bool reset);
        [[nodiscard]] bool inspector_available() noexcept;
        [[nodiscard]] bool pointer_over_interface(ImVec2 position, ImVec2 size, bool show_axes) const noexcept;
        void synchronize_transform();
        void apply_transform(SceneEntityReference entity, math::Transform transform);
        void transform_row(const char* identifier, const char* title, std::array<float, 3>& value, float speed, const char* format);
        void handle_shortcuts(EditorActions& actions, float aspect, bool inspector_available);
        void draw_orientation(ImVec2 position, ImVec2 size, bool show_axes);
        void draw_gizmo(ImVec2 minimum, ImVec2 size, bool blocked, bool transform_editable);
        void handle_viewport_input(ImVec2 minimum, ImVec2 size, bool blocked);
        void draw_viewport(ImVec2 position, ImVec2 size, bool show_axes, bool transform_editable);
        [[nodiscard]] float draw_render_status(float right_edge, const std::optional<RenderProgress>& render_progress, const std::optional<PathTracerPreparationProgress>& pathtracer_preparation);
        void draw_top_strip(ImVec2 position, ImVec2 size, EditorActions& actions);
        void draw_transform_tools(ImVec2 position, ImVec2 size, bool transform_editable);
        void draw_transform_hud(ImVec2 position, ImVec2 size, bool transform_editable);
        void draw_inspector(ImVec2 position, ImVec2 size);
        void draw_status_toast(ImVec2 position, ImVec2 size);
    };
} // namespace spectra
