export module spectra.scene.spatial;

export import spectra.scene.math;
import std;

namespace spectra::scene {
    export enum class CameraProjectionKind : std::uint32_t {
        Perspective  = 0u,
        Orthographic = 1u,
        Pinhole      = 2u,
    };

    export struct CameraProjection {
        CameraProjectionKind kind{CameraProjectionKind::Perspective};
        float vertical_fov_degrees{45.0f};
        float orthographic_height{1.0f};
        std::uint32_t image_width{};
        std::uint32_t image_height{};
        float fx{};
        float fy{};
        float cx{};
        float cy{};
        float near_plane{0.01f};
        float far_plane{200.0f};
    };

    export struct CameraPose {
        Vector3 position{};
        Quaternion orientation{};
    };

    export struct CameraFrame {
        Vector3 position{};
        Vector3 right{1.0f, 0.0f, 0.0f};
        Vector3 down{0.0f, 1.0f, 0.0f};
        Vector3 forward{0.0f, 0.0f, 1.0f};
    };

    export struct ViewportCamera {
        CameraPose pose{};
        Vector3 focus{};
        Vector3 navigation_up{0.0f, 1.0f, 0.0f};
        CameraProjection projection{};
    };

    export struct ViewportNavigationTarget {
        std::uint64_t revision{1u};
        Vector3 focus{};
        Vector3 bounds_minimum{};
        Vector3 bounds_maximum{};
        Vector3 navigation_up{0.0f, 1.0f, 0.0f};
    };

    export struct VulkanCameraMatrices {
        std::array<float, 16> world_to_clip{};
        std::array<float, 16> clip_to_world{};
        CameraFrame frame{};
        float far_plane{};
    };

    export struct ViewportCameraDelta {
        float x_pixels{};
        float y_pixels{};
    };

    export struct ViewportCameraSize {
        float width{};
        float height{};
    };

    export struct CameraRevision {
        std::uint64_t value{};

        friend auto operator<=>(const CameraRevision&, const CameraRevision&) = default;
    };

    export struct CameraSnapshot {
        CameraRevision revision{};
        ViewportCamera state{};
        std::uint64_t seed_revision{};
    };

    export class CameraWorkspace {
    public:
        CameraWorkspace() = default;

        CameraWorkspace(const CameraWorkspace& other) = delete;
        CameraWorkspace(CameraWorkspace&& other) = delete;
        CameraWorkspace& operator=(const CameraWorkspace& other) = delete;
        CameraWorkspace& operator=(CameraWorkspace&& other) = delete;
        ~CameraWorkspace() = default;

        void ensure_camera(std::string scene_id, ViewportCamera state, std::uint64_t seed_revision);
        [[nodiscard]] CameraSnapshot reset_camera(std::string scene_id, ViewportCamera state, std::uint64_t seed_revision);
        [[nodiscard]] CameraSnapshot snapshot(std::string_view scene_id) const;
        [[nodiscard]] CameraSnapshot commit(std::string_view scene_id, ViewportCamera state);

    private:
        mutable std::mutex mutex{};
        std::map<std::string, CameraSnapshot> cameras{};
    };

    export [[nodiscard]] CameraPose camera_pose_from_look_at(Vector3 eye, Vector3 target, Vector3 navigation_up);
    export [[nodiscard]] CameraPose camera_pose_from_frame(Vector3 position, Vector3 right, Vector3 down, Vector3 forward);
    export [[nodiscard]] ViewportCamera viewport_camera_from_look_at(Vector3 eye, Vector3 target, Vector3 navigation_up, CameraProjection projection);
    export void validate_viewport_navigation_target(const ViewportNavigationTarget& target, std::string_view context);
    export [[nodiscard]] ViewportCamera viewport_camera_from_navigation_target(CameraPose pose, CameraProjection projection, const ViewportNavigationTarget& target);
    export [[nodiscard]] ViewportCamera frame_viewport_camera_to_navigation_target(ViewportCamera state, const ViewportNavigationTarget& target, float distance_scale);
    export [[nodiscard]] CameraFrame camera_frame(const CameraPose& pose);
    export [[nodiscard]] SceneTransform camera_world_from_camera(const CameraPose& pose);
    export [[nodiscard]] CameraPose camera_pose_from_world_from_camera(const SceneTransform& world_from_camera);
    export [[nodiscard]] float camera_projection_vertical_fov_degrees(const CameraProjection& projection);
    export [[nodiscard]] VulkanCameraMatrices make_vulkan_camera_matrices(const CameraPose& pose, const CameraProjection& projection, float aspect, float far_plane);
    export [[nodiscard]] float viewport_drag_zoom_steps(ViewportCameraDelta delta);
    export [[nodiscard]] ViewportCamera orbit_viewport_camera(ViewportCamera state, ViewportCameraDelta delta);
    export [[nodiscard]] ViewportCamera pan_viewport_camera(ViewportCamera state, ViewportCameraDelta delta, ViewportCameraSize viewport);
    export [[nodiscard]] ViewportCamera zoom_viewport_camera(ViewportCamera state, float steps);
} // namespace spectra::scene
