#ifndef XAYAH_SPECTRA_PATHTRACER_CAMERA_CONTROLLER_H
#define XAYAH_SPECTRA_PATHTRACER_CAMERA_CONTROLLER_H

#include <src/util/transform.h>
#include <src/util/vecmath.h>

#include <array>

namespace xayah::pathtracer {
    struct SceneSession;

    struct InteractiveCameraPose {
        spectra::Point3f eye{};
        spectra::Point3f center{};
        spectra::Vector3f up{};
        float basis_handedness{1.0f};
    };

    [[nodiscard]] float interactive_camera_fov_degrees(const SceneSession& scene);
    [[nodiscard]] InteractiveCameraPose interactive_camera_pose_from_base_transform(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds);
    [[nodiscard]] spectra::Transform moving_from_camera_from_interactive_pose(const spectra::Transform& base_camera_from_world, const InteractiveCameraPose& pose);
    bool interactive_camera_pan(InteractiveCameraPose& pose, const std::array<float, 2>& displacement, float fov_degrees, const std::array<float, 2>& viewport_size);
    bool interactive_camera_dolly(InteractiveCameraPose& pose, const std::array<float, 2>& displacement);
    bool interactive_camera_orbit(InteractiveCameraPose& pose, std::array<float, 2> displacement, bool invert);
    bool interactive_camera_key_motion(InteractiveCameraPose& pose, const std::array<float, 2>& delta, float speed, bool dolly);
} // namespace xayah::pathtracer

#endif
