export module spectra.editor.viewport.interaction;

import spectra.scene;
import spectra.scene.document;
import spectra.dynamics.runtime;
import spectra.render.gpu_scene;
import spectra.render.composition.diagnostics;
import std;

namespace spectra {
    export enum class CameraSource : std::uint8_t {
        Scene,
        Viewport,
    };

    export enum class AxesPlane : std::uint8_t {
        Xz,
        Xy,
        Yz,
    };

    export struct ViewportInteraction {
        ViewportInteraction(SceneDocument& document, DynamicsRuntime& dynamics, GpuScene& gpu_scene) noexcept;

        void initialize_from_scene();
        void camera_changed() noexcept;
        void orbit_viewport_camera(float x_pixels, float y_pixels) noexcept;
        void pan_viewport_camera(float x_pixels, float y_pixels, float viewport_height) noexcept;
        void zoom_viewport_camera(float steps) noexcept;
        void frame_scene(float aspect) noexcept;
        void frame_selection(float aspect) noexcept;
        void view_axis(math::Float3 direction, float aspect) noexcept;
        void select(SceneEntityReference entity, bool additive);
        void clear_selection() noexcept;
        void clear_hover() noexcept;
        void prune_selection() noexcept;

        struct {
            SceneDocument& document;
            DynamicsRuntime& dynamics;
            GpuScene& gpu_scene;
        } context;

        struct {
            SelectionState selection{};
            bool overlays_visible{true};
            scene::Camera camera{};
            math::Float3 focus{};
            math::Float3 navigation_up{0.0f, 1.0f, 0.0f};
            std::uint64_t camera_revision{1};
            CameraSource source{CameraSource::Viewport};
            AxesPlane axes_plane{AxesPlane::Xz};
            scene::Camera render_camera{};
            std::uint64_t render_camera_revision{};
            float aspect{1.0f};
            CameraSource synchronized_source{CameraSource::Viewport};
            std::uint64_t synchronized_camera_revision{};
            scene::ResourceRevision synchronized_scene_camera_revision{};
        } view;

    private:
        void frame_viewport_camera(math::Bounds3 bounds, float aspect) noexcept;
        [[nodiscard]] math::Bounds3 navigation_bounds() const noexcept;
        [[nodiscard]] math::Bounds3 effective_scene_bounds() const noexcept;
        [[nodiscard]] bool entity_exists(SceneEntityReference entity) const noexcept;
        [[nodiscard]] std::optional<math::Bounds3> entity_bounds(SceneEntityReference entity) const noexcept;
    };
} // namespace spectra
