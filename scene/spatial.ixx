export module spectra.scene.spatial;

export import spectra.scene.math;
import std;

namespace spectra::scene {
    export enum class CoordinateHandedness : std::uint32_t {
        Right = 0u,
        Left  = 1u,
    };

    export struct CoordinateConvention {
        Vector3 right{1.0f, 0.0f, 0.0f};
        Vector3 up{0.0f, 1.0f, 0.0f};
        Vector3 forward{0.0f, 0.0f, 1.0f};
        CoordinateHandedness handedness{CoordinateHandedness::Right};
    };

    export struct CoordinateSystem {
        std::string name{"SpectraYUp"};
        CoordinateConvention convention{};
        float unit_scale_to_meter{1.0f};
    };

    export enum class ClipDepthRange : std::uint32_t {
        ZeroToOne     = 0u,
        MinusOneToOne = 1u,
    };

    export enum class ClipYAxisDirection : std::uint32_t {
        Up   = 0u,
        Down = 1u,
    };

    export struct ClipConvention {
        std::string name{"VulkanClip"};
        ClipDepthRange depth_range{ClipDepthRange::ZeroToOne};
        ClipYAxisDirection y_axis{ClipYAxisDirection::Down};
    };

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
        CoordinateConvention local_convention{};
    };

    export struct CameraFrame {
        Vector3 position{};
        Vector3 right{1.0f, 0.0f, 0.0f};
        Vector3 up{0.0f, 1.0f, 0.0f};
        Vector3 forward{0.0f, 0.0f, 1.0f};
    };

    export struct CameraViewState {
        CameraPose pose{};
        Vector3 focus{};
        Vector3 navigation_up{0.0f, 1.0f, 0.0f};
        CameraProjection projection{};
    };

    export struct FrameTransform {
        std::string from{};
        std::string to{};
        std::array<float, 16> matrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        std::array<float, 16> inverse{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    };

    export struct VulkanCameraMatrices {
        std::array<float, 16> world_to_clip{};
        std::array<float, 16> clip_to_world{};
        CameraFrame frame{};
        float far_plane{};
    };

    export struct CameraState {
        CameraViewState view{};
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
        CameraState state{};
    };

    export class CameraWorkspace {
    public:
        CameraWorkspace() = default;

        CameraWorkspace(const CameraWorkspace& other) = delete;
        CameraWorkspace(CameraWorkspace&& other) = delete;
        CameraWorkspace& operator=(const CameraWorkspace& other) = delete;
        CameraWorkspace& operator=(CameraWorkspace&& other) = delete;
        ~CameraWorkspace() = default;

        void ensure_camera(std::string scene_id, CameraState state);
        [[nodiscard]] CameraSnapshot reset_camera(std::string scene_id, CameraState state);
        [[nodiscard]] CameraSnapshot snapshot(std::string_view scene_id) const;
        [[nodiscard]] CameraSnapshot commit(std::string_view scene_id, CameraState state);

    private:
        mutable std::mutex mutex{};
        std::map<std::string, CameraSnapshot> cameras{};
    };

    export [[nodiscard]] CoordinateSystem coordinate_system(std::string_view name);
    export [[nodiscard]] std::vector<std::string_view> coordinate_system_names();
    export [[nodiscard]] ClipConvention clip_convention(std::string_view name);
    export [[nodiscard]] std::vector<std::string_view> clip_convention_names();
    export [[nodiscard]] std::string coordinate_convention_label(const CoordinateConvention& convention);
    export [[nodiscard]] std::string coordinate_system_label(const CoordinateSystem& coordinate_system);
    export [[nodiscard]] FrameTransform frame_transform(const CoordinateSystem& from, const CoordinateSystem& to);
    export [[nodiscard]] Vector3 transform_point(const FrameTransform& transform, Vector3 point);
    export [[nodiscard]] Vector3 transform_direction(const FrameTransform& transform, Vector3 direction);
    export [[nodiscard]] CameraPose camera_pose_from_look_at(Vector3 eye, Vector3 target, Vector3 up);
    export [[nodiscard]] CameraViewState camera_view_from_look_at(Vector3 eye, Vector3 target, Vector3 up, CameraProjection projection);
    export [[nodiscard]] CameraFrame camera_frame(const CameraPose& pose);
    export [[nodiscard]] SceneTransform camera_world_from_camera(const CameraPose& pose);
    export [[nodiscard]] CameraPose camera_pose_from_world_from_camera(const SceneTransform& world_from_camera);
    export [[nodiscard]] float camera_projection_vertical_fov_degrees(const CameraProjection& projection);
    export [[nodiscard]] VulkanCameraMatrices make_vulkan_camera_matrices(const CameraViewState& view, float aspect, float far_plane);
    export [[nodiscard]] float viewport_drag_zoom_steps(ViewportCameraDelta delta);
    export [[nodiscard]] CameraState orbit_viewport_camera(CameraState state, ViewportCameraDelta delta);
    export [[nodiscard]] CameraState pan_viewport_camera(CameraState state, ViewportCameraDelta delta, ViewportCameraSize viewport);
    export [[nodiscard]] CameraState zoom_viewport_camera(CameraState state, float steps);
} // namespace spectra::scene
