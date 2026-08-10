export module spectra.editor:viewport.interaction;

import spectra.scene;
import spectra.scene.document;
import spectra.dynamics.runtime;
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

    export enum class SceneEntityKind : std::uint32_t {
        None,
        Instance,
        Camera,
        Light,
        AreaEmitter,
    };

    export struct SceneEntityReference {
        SceneEntityKind kind{SceneEntityKind::None};
        std::uint64_t id{};
        std::uint64_t owner{};
        std::uint32_t subindex{};

        friend auto operator<=>(const SceneEntityReference&, const SceneEntityReference&) = default;
    };

    export struct SelectionState {
        std::vector<SceneEntityReference> selected{};
        std::optional<SceneEntityReference> active{};
        std::optional<SceneEntityReference> hovered{};
    };

    export struct ViewportInteraction {
        ViewportInteraction(SceneDocument& document, DynamicsRuntime& dynamics) noexcept;

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
        [[nodiscard]] bool entity_exists(SceneEntityReference entity) const noexcept;
        [[nodiscard]] std::optional<math::Bounds3> entity_bounds(SceneEntityReference entity) const noexcept;
    };
} // namespace spectra
