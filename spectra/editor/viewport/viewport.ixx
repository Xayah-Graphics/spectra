export module spectra.editor.viewport;

import spectra.editor.document;
import spectra.simulation.runtime;
import spectra.render.display;
import spectra.scene;
import std;

namespace spectra::editor {
    export enum class CameraSource : std::uint8_t {
        Scene,
        Viewport,
    };

    export struct ViewportSelection {
        std::vector<scene::EntityReference> selected{};
        std::optional<scene::EntityReference> active{};
        std::optional<scene::EntityReference> hovered{};
    };

    export struct ViewportSettings {
        float exposure{};
        bool guides_visible{true};
        bool hud_visible{true};
        bool telemetry_visible{true};
        bool selection_outline{true};
        render::SceneGuideSettings scene_guides{};
        std::map<scene::EntityReference, render::EntityDiagnostics> entity_diagnostics{};
    };

    export struct Viewport {
        Viewport(Document& document, simulation::Runtime& simulation) noexcept;

        void initialize_from_scene();
        void synchronize_bounds(math::Bounds3 scene_bounds, std::span<const math::Bounds3> instance_bounds);
        void camera_changed() noexcept;
        void view_camera(scene::CameraId camera_id) noexcept;
        void toggle_scene_camera() noexcept;
        void orbit_viewport_camera(float x_pixels, float y_pixels);
        void pan_viewport_camera(float x_pixels, float y_pixels, float viewport_height);
        void zoom_viewport_camera(float steps);
        void frame_selection(float aspect);
        void view_axis(math::Float3 direction, float aspect);
        void select(scene::EntityReference entity, bool additive);
        void clear_selection() noexcept;
        void clear_hover() noexcept;
        void prune_selection() noexcept;

        struct {
            Document& document;
            simulation::Runtime& simulation;
        } context;

        struct {
            ViewportSelection selection{};
            scene::Camera camera{};
            math::Float3 focus{};
            math::Float3 navigation_up{0.0f, 1.0f, 0.0f};
            std::uint64_t camera_revision{1};
            CameraSource source{CameraSource::Viewport};
            scene::CameraId scene_camera{};
            std::optional<math::Float4> camera_gate{};
            render::AxesPlane axes_plane{render::AxesPlane::Xz};
            scene::Camera render_camera{};
            std::uint64_t render_camera_revision{};
            float aspect{1.0f};
            CameraSource synchronized_source{CameraSource::Viewport};
            std::uint64_t synchronized_camera_revision{};
            scene::ResourceRevision synchronized_scene_camera_revision{};
        } view;

        ViewportSettings settings{};

        struct {
            math::Bounds3 scene{};
            std::vector<math::Bounds3> instances{};
        } bounds;

    private:
        void frame_viewport_camera(math::Bounds3 bounds, float aspect);
        [[nodiscard]] bool entity_exists(scene::EntityReference entity) const noexcept;
        [[nodiscard]] math::Bounds3 navigation_bounds() const noexcept;
        [[nodiscard]] math::Bounds3 effective_scene_bounds() const noexcept;
        [[nodiscard]] std::optional<math::Bounds3> entity_bounds(scene::EntityReference entity) const noexcept;
    };
} // namespace spectra::editor
