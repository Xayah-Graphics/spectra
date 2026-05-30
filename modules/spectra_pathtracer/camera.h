#ifndef XAYAH_MODULES_SPECTRA_GPU_CAMERA_H
#define XAYAH_MODULES_SPECTRA_GPU_CAMERA_H

#include "backend.h"

#include <src/util/transform.h>
#include <src/util/vecmath.h>

#include <array>

namespace xayah::spectra_pathtracer {
    struct CameraPose {
        spectra::Point3f eye{};
        spectra::Point3f center{};
        spectra::Vector3f up{};
        float basis_handedness{1.0f};
    };

    [[nodiscard]] float camera_fov_degrees(const SceneSession& scene);
    [[nodiscard]] CameraPose camera_pose_from_base_transform(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds);
    [[nodiscard]] spectra::Transform moving_from_camera_from_pose(const spectra::Transform& base_camera_from_world, const CameraPose& pose);
    bool camera_pan(CameraPose& pose, const std::array<float, 2>& displacement, float fov_degrees, const std::array<float, 2>& viewport_size);
    bool camera_dolly(CameraPose& pose, const std::array<float, 2>& displacement);
    bool camera_orbit(CameraPose& pose, std::array<float, 2> displacement, bool invert);
    bool camera_key_motion(CameraPose& pose, const std::array<float, 2>& delta, float speed, bool dolly);
} // namespace xayah::spectra_pathtracer

#endif
