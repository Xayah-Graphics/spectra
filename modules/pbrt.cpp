module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <pbrt_gpu/base/film.h>
#include <pbrt_gpu/base/sampler.h>
#include <pbrt_gpu/gpu/memory.h>
#include <pbrt_gpu/gpu/util.h>
#include <pbrt_gpu/options.h>
#include <pbrt_gpu/parser.h>
#include <pbrt_gpu/pbrt.h>
#include <pbrt_gpu/scene.h>
#include <pbrt_gpu/util/transform.h>
#include <pbrt_gpu/util/vecmath.h>
#include <pbrt_gpu/wavefront/integrator.h>
#include <vulkan/vulkan_raii.hpp>

module pbrt;
import std;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::ImageAspectFlags aspect, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type");
    }
    void validate_finite_point(const pbrt::Point3f& point, const char* message) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::runtime_error(message);
    }

    void validate_finite_vector(const pbrt::Vector3f& vector, const char* message) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) throw std::runtime_error(message);
    }

    void validate_transform_matrix(const pbrt::Transform& transform, const char* message) {
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        const pbrt::SquareMatrix<4>& inverse = transform.GetInverseMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                if (!std::isfinite(static_cast<float>(matrix[row][column])) || !std::isfinite(static_cast<float>(inverse[row][column]))) throw std::runtime_error(message);
            }
        }
    }

    [[nodiscard]] std::array<float, 16> raw_matrix_array_from_transform(const pbrt::Transform& transform) {
        validate_transform_matrix(transform, "PBRT transform matrix contains a non-finite value");
        std::array<float, 16> values{};
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = static_cast<float>(matrix[row][column]);
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                values[row * 4 + column] = value;
            }
        }
        return values;
    }

    [[nodiscard]] float finite_length(const pbrt::Vector3f& vector, const char* error_message) {
        validate_finite_vector(vector, error_message);
        const float length = pbrt::Length(vector);
        if (!std::isfinite(length)) throw std::runtime_error(error_message);
        return length;
    }

    [[nodiscard]] pbrt::Vector3f normalized_vector(const pbrt::Vector3f& vector, const char* error_message) {
        const float length = finite_length(vector, error_message);
        if (!(length > 1.0e-20f)) throw std::runtime_error(error_message);
        return vector / length;
    }

    [[nodiscard]] pbrt::Vector3f camera_effective_up(const pbrt::Point3f& eye, const pbrt::Point3f& center, const pbrt::Vector3f& up) {
        const pbrt::Vector3f view_direction = center - eye;
        if (finite_length(pbrt::Cross(view_direction, up), "Camera view/up cross product is invalid") > 1.0e-10f) return up;
        return std::abs(up.y) < 0.9f ? pbrt::Vector3f{0.0f, 1.0f, 0.0f} : pbrt::Vector3f{1.0f, 0.0f, 0.0f};
    }

    struct RawSpectraCameraFrame {
        pbrt::Vector3f forward{};
        pbrt::Vector3f right{};
        pbrt::Vector3f up{};
    };

    struct RawSpectraCameraPose {
        pbrt::Point3f eye{};
        pbrt::Point3f center{};
        pbrt::Vector3f up{};
        float basis_handedness{1.0f};
    };

    [[nodiscard]] RawSpectraCameraFrame raw_camera_frame_from_pose(const pbrt::Point3f& eye, const pbrt::Point3f& center, const pbrt::Vector3f& up, const float basis_handedness) {
        if (basis_handedness != -1.0f && basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        RawSpectraCameraFrame frame{};
        frame.forward = normalized_vector(center - eye, "Camera eye and center must not overlap");
        const pbrt::Vector3f effective_up = camera_effective_up(eye, center, up);
        const pbrt::Vector3f positive_right = normalized_vector(pbrt::Cross(effective_up, frame.forward), "Camera right vector is invalid");
        frame.right                         = positive_right * basis_handedness;
        frame.up                            = pbrt::Cross(frame.forward, positive_right);
        return frame;
    }

    [[nodiscard]] pbrt::Transform raw_camera_from_world_transform_from_pose(const pbrt::Point3f& eye, const pbrt::Point3f& center, const pbrt::Vector3f& up, const float basis_handedness) {
        const RawSpectraCameraFrame frame = raw_camera_frame_from_pose(eye, center, up, basis_handedness);
        const pbrt::Vector3f eye_vector{eye.x, eye.y, eye.z};
        pbrt::Transform transform{pbrt::SquareMatrix<4>{
            frame.right.x, frame.right.y, frame.right.z, -pbrt::Dot(frame.right, eye_vector),
            frame.up.x, frame.up.y, frame.up.z, -pbrt::Dot(frame.up, eye_vector),
            frame.forward.x, frame.forward.y, frame.forward.z, -pbrt::Dot(frame.forward, eye_vector),
            0.0f, 0.0f, 0.0f, 1.0f,
        }};
        validate_transform_matrix(transform, "Camera transform contains a non-finite value");
        return transform;
    }

    void raw_validate_bounds(const pbrt::Bounds3f& bounds, const char* message) {
        validate_finite_point(bounds.pMin, message);
        validate_finite_point(bounds.pMax, message);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (bounds.pMin[axis] > bounds.pMax[axis]) throw std::runtime_error(message);
        }
    }

    [[nodiscard]] pbrt::Point3f raw_camera_focus_center_from_bounds(const pbrt::Point3f& eye, const pbrt::Vector3f& forward, const pbrt::Bounds3f& focus_bounds) {
        raw_validate_bounds(focus_bounds, "Camera focus bounds are invalid");

        const pbrt::Point3f bounds_center{
            (focus_bounds.pMin.x + focus_bounds.pMax.x) * 0.5f,
            (focus_bounds.pMin.y + focus_bounds.pMax.y) * 0.5f,
            (focus_bounds.pMin.z + focus_bounds.pMax.z) * 0.5f,
        };
        float focus_distance = pbrt::Dot(bounds_center - eye, forward);

        constexpr float parallel_epsilon = 1.0e-7f;
        constexpr float distance_epsilon = 1.0e-5f;
        float ray_min = 0.0f;
        float ray_max = std::numeric_limits<float>::infinity();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::abs(forward[axis]) <= parallel_epsilon) {
                if (eye[axis] < focus_bounds.pMin[axis] || eye[axis] > focus_bounds.pMax[axis]) throw std::runtime_error("Camera focus bounds do not intersect the initial view ray");
            } else {
                float t0 = (focus_bounds.pMin[axis] - eye[axis]) / forward[axis];
                float t1 = (focus_bounds.pMax[axis] - eye[axis]) / forward[axis];
                if (t0 > t1) std::swap(t0, t1);
                ray_min = std::max(ray_min, t0);
                ray_max = std::min(ray_max, t1);
                if (ray_min > ray_max) throw std::runtime_error("Camera focus bounds do not intersect the initial view ray");
            }
        }
        if (!(ray_max > distance_epsilon)) throw std::runtime_error("Camera focus bounds must be in front of the initial camera");
        const float lower_bound = std::max(ray_min, distance_epsilon);
        focus_distance = std::clamp(focus_distance, lower_bound, ray_max);
        if (!(focus_distance > distance_epsilon) || !std::isfinite(focus_distance)) throw std::runtime_error("Camera focus distance is invalid");
        return eye + forward * focus_distance;
    }

    [[nodiscard]] RawSpectraCameraPose raw_camera_pose_from_base_transform(const pbrt::Transform& camera_from_world, const pbrt::Bounds3f& focus_bounds) {
        const pbrt::Transform world_from_camera = pbrt::Inverse(camera_from_world);
        RawSpectraCameraPose pose{};
        pose.eye                          = world_from_camera(pbrt::Point3f{0.0f, 0.0f, 0.0f});
        const pbrt::Vector3f right        = normalized_vector(world_from_camera(pbrt::Vector3f{1.0f, 0.0f, 0.0f}), "Base camera right vector is invalid");
        const pbrt::Vector3f forward      = normalized_vector(world_from_camera(pbrt::Vector3f{0.0f, 0.0f, 1.0f}), "Base camera forward vector is invalid");
        pose.up                           = normalized_vector(world_from_camera(pbrt::Vector3f{0.0f, 1.0f, 0.0f}), "Base camera up vector is invalid");
        const pbrt::Vector3f positive_right = normalized_vector(pbrt::Cross(camera_effective_up(pose.eye, pose.eye + forward, pose.up), forward), "Base camera positive right vector is invalid");
        pose.basis_handedness             = pbrt::Dot(right, positive_right) < 0.0f ? -1.0f : 1.0f;
        pose.center                        = raw_camera_focus_center_from_bounds(pose.eye, forward, focus_bounds);
        return pose;
    }

    [[nodiscard]] std::array<float, 2> raw_camera_view_dimensions(const pbrt::Point3f& eye, const pbrt::Point3f& center, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (!std::isfinite(fov_degrees) || !(fov_degrees > 0.0f) || !(fov_degrees < 180.0f)) throw std::runtime_error("Camera fov must be finite and inside (0, 180)");
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        constexpr float radians_per_degree = 0.017453292519943295769f;
        const float distance               = finite_length(eye - center, "Camera view distance is invalid");
        const float half_height            = distance * std::tan(fov_degrees * radians_per_degree * 0.5f);
        const float height                 = half_height * 2.0f;
        const float width                  = height * std::max(viewport_size[0] / viewport_size[1], 0.001f);
        if (!std::isfinite(width) || !std::isfinite(height) || !(width > 0.0f) || !(height > 0.0f)) throw std::runtime_error("Camera view dimensions are invalid");
        return {width, height};
    }

    bool raw_camera_pan(RawSpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        const RawSpectraCameraFrame frame       = raw_camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 2> view_size = raw_camera_view_dimensions(pose.eye, pose.center, fov_degrees, viewport_size);
        const pbrt::Vector3f offset          = frame.right * (-displacement[0] * view_size[0]) + frame.up * (displacement[1] * view_size[1]);
        pose.eye += offset;
        pose.center += offset;
        return true;
    }

    bool raw_camera_dolly(RawSpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        const float larger_displacement = std::abs(displacement[0]) > std::abs(displacement[1]) ? displacement[0] : -displacement[1];
        if (larger_displacement == 0.0f) return false;
        if (larger_displacement >= 0.99f) return false;
        const pbrt::Vector3f direction = pose.center - pose.eye;
        if (!(finite_length(direction, "Camera dolly direction is invalid") > 1.0e-6f)) return false;
        pose.eye += direction * larger_displacement;
        return true;
    }

    bool raw_camera_orbit(RawSpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        if (pose.basis_handedness != -1.0f && pose.basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        constexpr float two_pi   = 6.2831853071795864769f;
        constexpr float pole_pad = 1.0e-3f;
        displacement[0] *= -pose.basis_handedness;
        displacement[0] *= two_pi;
        displacement[1] *= two_pi;

        const pbrt::Point3f origin   = invert ? pose.eye : pose.center;
        const pbrt::Point3f position = invert ? pose.center : pose.eye;
        pbrt::Vector3f center_to_eye = position - origin;
        const float radius           = finite_length(center_to_eye, "Camera orbit radius is invalid");
        if (!(radius > 1.0e-6f)) return false;
        center_to_eye /= radius;

        const pbrt::Vector3f normalized_up = normalized_vector(pose.up, "Camera up vector is invalid");
        const float cos_elevation          = pbrt::Dot(center_to_eye, normalized_up);
        pbrt::Vector3f horizontal          = center_to_eye - normalized_up * cos_elevation;
        const float sin_elevation          = finite_length(horizontal, "Camera orbit horizontal vector is invalid");
        const float elevation                    = std::atan2(sin_elevation, cos_elevation);
        if (sin_elevation < 1.0e-6f) {
            const pbrt::Vector3f reference = std::abs(normalized_up.x) < 0.9f ? pbrt::Vector3f{1.0f, 0.0f, 0.0f} : pbrt::Vector3f{0.0f, 0.0f, 1.0f};
            horizontal                     = normalized_vector(reference - normalized_up * pbrt::Dot(reference, normalized_up), "Camera orbit horizontal vector is invalid");
        } else {
            horizontal /= sin_elevation;
        }

        const float yaw_cos                    = std::cos(-displacement[0]);
        const float yaw_sin                    = std::sin(-displacement[0]);
        horizontal                             = horizontal * yaw_cos + pbrt::Cross(normalized_up, horizontal) * yaw_sin;
        const float new_elevation              = std::clamp(elevation - displacement[1], pole_pad, 3.14159265358979323846f - pole_pad);
        const pbrt::Vector3f new_offset        = (normalized_up * std::cos(new_elevation) + horizontal * std::sin(new_elevation)) * radius;
        const pbrt::Point3f new_position       = origin + new_offset;
        if (invert) pose.center = new_position;
        else pose.eye = new_position;
        return true;
    }

    bool raw_camera_key_motion(RawSpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        if (delta[0] == 0.0f && delta[1] == 0.0f) return false;
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        const RawSpectraCameraFrame frame = raw_camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const pbrt::Vector3f movement = dolly
            ? frame.forward * (delta[0] * speed)
            : frame.right * (delta[0] * speed) + frame.up * (delta[1] * speed);
        pose.eye += movement;
        pose.center += movement;
        return true;
    }

    [[nodiscard]] pbrt::Transform raw_moving_from_camera_from_pose(const pbrt::Transform& base_camera_from_world, const RawSpectraCameraPose& pose) {
        const pbrt::Transform current_camera_from_world = raw_camera_from_world_transform_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        return base_camera_from_world * pbrt::Inverse(current_camera_from_world);
    }

    [[nodiscard]] float raw_pbrt_camera_fov_degrees(const xayah::SpectraScene& scene) {
        if (!scene.camera.present) throw std::runtime_error("Interactive PBRT camera controls require an explicit perspective camera");
        if (scene.camera.name != "perspective") throw std::runtime_error(std::format("Interactive PBRT camera controls require a perspective camera, not \"{}\"", scene.camera.name));
        constexpr float pbrt_perspective_default_fov = 90.0f;
        for (const xayah::SpectraPbrtParameter& parameter : scene.camera.parameters) {
            if (parameter.name != "fov") continue;
            if (parameter.floats.size() != 1) throw std::runtime_error("PBRT perspective camera fov must have exactly one float value");
            return parameter.floats.front();
        }
        return pbrt_perspective_default_fov;
    }


    [[nodiscard]] pbrt::SquareMatrix<4> square_matrix_from_array(const std::array<float, 16>& values, const char* message) {
        for (const float value : values) {
            if (!std::isfinite(value)) throw std::runtime_error(message);
        }
        return pbrt::SquareMatrix<4>{
            values[0], values[1], values[2], values[3],
            values[4], values[5], values[6], values[7],
            values[8], values[9], values[10], values[11],
            values[12], values[13], values[14], values[15],
        };
    }

    [[nodiscard]] pbrt::Transform pbrt_transform_from_spectra(const xayah::SpectraPbrtTransform& transform) {
        pbrt::Transform pbrt_transform{
            square_matrix_from_array(transform.matrix, "Spectra PBRT transform matrix contains a non-finite value"),
            square_matrix_from_array(transform.inverse_matrix, "Spectra PBRT inverse transform matrix contains a non-finite value"),
        };
        validate_transform_matrix(pbrt_transform, "Spectra PBRT transform contains a non-finite value");
        return pbrt_transform;
    }

    [[nodiscard]] xayah::SpectraPbrtTransform spectra_transform_from_pbrt(const pbrt::Transform& transform) {
        xayah::SpectraPbrtTransform spectra_transform{};
        spectra_transform.matrix         = raw_matrix_array_from_transform(transform);
        spectra_transform.inverse_matrix = raw_matrix_array_from_transform(pbrt::Inverse(transform));
        return spectra_transform;
    }

    [[nodiscard]] xayah::SpectraPbrtPoint3 spectra_point3_from_pbrt(const pbrt::Point3f& point) {
        return {point.x, point.y, point.z};
    }

    [[nodiscard]] pbrt::Point3f pbrt_point3_from_spectra(const xayah::SpectraPbrtPoint3& point) {
        return {point.x, point.y, point.z};
    }

    [[nodiscard]] pbrt::Vector3f pbrt_vector3_from_spectra(const xayah::SpectraPbrtVector3& vector) {
        return {vector.x, vector.y, vector.z};
    }

    [[nodiscard]] xayah::SpectraPbrtVector3 spectra_vector3_from_pbrt(const pbrt::Vector3f& vector) {
        return {vector.x, vector.y, vector.z};
    }

    [[nodiscard]] xayah::SpectraPbrtBounds3 spectra_bounds_from_pbrt(const pbrt::Bounds3f& bounds) {
        return {spectra_point3_from_pbrt(bounds.pMin), spectra_point3_from_pbrt(bounds.pMax)};
    }

    [[nodiscard]] pbrt::Bounds3f pbrt_bounds_from_spectra(const xayah::SpectraPbrtBounds3& bounds) {
        return {pbrt_point3_from_spectra(bounds.minimum), pbrt_point3_from_spectra(bounds.maximum)};
    }

    [[nodiscard]] xayah::SpectraCameraPose spectra_camera_pose_from_raw(const RawSpectraCameraPose& pose) {
        return {spectra_point3_from_pbrt(pose.eye), spectra_point3_from_pbrt(pose.center), spectra_vector3_from_pbrt(pose.up), pose.basis_handedness};
    }

    [[nodiscard]] RawSpectraCameraPose raw_camera_pose_from_spectra(const xayah::SpectraCameraPose& pose) {
        return {pbrt_point3_from_spectra(pose.eye), pbrt_point3_from_spectra(pose.center), pbrt_vector3_from_spectra(pose.up), pose.basis_handedness};
    }

}

namespace xayah {
    [[nodiscard]] float pbrt_camera_fov_degrees(const SpectraScene& scene) {
        return raw_pbrt_camera_fov_degrees(scene);
    }

    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const SpectraPbrtTransform& camera_from_world, const SpectraPbrtBounds3& focus_bounds) {
        return spectra_camera_pose_from_raw(raw_camera_pose_from_base_transform(pbrt_transform_from_spectra(camera_from_world), pbrt_bounds_from_spectra(focus_bounds)));
    }

    [[nodiscard]] SpectraPbrtTransform moving_from_camera_from_pose(const SpectraPbrtTransform& base_camera_from_world, const SpectraCameraPose& pose) {
        return spectra_transform_from_pbrt(raw_moving_from_camera_from_pose(pbrt_transform_from_spectra(base_camera_from_world), raw_camera_pose_from_spectra(pose)));
    }

    void validate_pbrt_bounds(const SpectraPbrtBounds3& bounds, const char* message) {
        raw_validate_bounds(pbrt_bounds_from_spectra(bounds), message);
    }

    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        RawSpectraCameraPose raw_pose = raw_camera_pose_from_spectra(pose);
        const bool changed = raw_camera_pan(raw_pose, displacement, fov_degrees, viewport_size);
        if (changed) pose = spectra_camera_pose_from_raw(raw_pose);
        return changed;
    }

    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        RawSpectraCameraPose raw_pose = raw_camera_pose_from_spectra(pose);
        const bool changed = raw_camera_dolly(raw_pose, displacement);
        if (changed) pose = spectra_camera_pose_from_raw(raw_pose);
        return changed;
    }

    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        RawSpectraCameraPose raw_pose = raw_camera_pose_from_spectra(pose);
        const bool changed = raw_camera_orbit(raw_pose, displacement, invert);
        if (changed) pose = spectra_camera_pose_from_raw(raw_pose);
        return changed;
    }

    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        RawSpectraCameraPose raw_pose = raw_camera_pose_from_spectra(pose);
        const bool changed = raw_camera_key_motion(raw_pose, delta, speed, dolly);
        if (changed) pose = spectra_camera_pose_from_raw(raw_pose);
        return changed;
    }

    struct SpectraPbrtRuntimeState {
        pbrt::PBRTOptions baseline_options{};
        bool initialized{false};
    };

    SpectraPbrtRuntime::SpectraPbrtRuntime() : state{std::make_unique<SpectraPbrtRuntimeState>()} {
        if (pbrt::Options != nullptr) throw std::runtime_error("PBRT runtime is already initialized");
        this->state->baseline_options.useGPU         = true;
        this->state->baseline_options.wavefront      = false;
        this->state->baseline_options.nThreads       = 30;
        this->state->baseline_options.renderingSpace = pbrt::RenderingCoordinateSystem::CameraWorld;
        pbrt::InitPBRT(this->state->baseline_options);
        this->state->initialized = true;
    }

    SpectraPbrtRuntime::~SpectraPbrtRuntime() noexcept {
        try {
            this->wait_gpu_noexcept();
            if (this->state != nullptr && this->state->initialized && pbrt::Options != nullptr) pbrt::CleanupPBRT();
        } catch (...) {
        }
    }

    void SpectraPbrtRuntime::reset_options_for_scene() {
        if (this->state == nullptr || !this->state->initialized) throw std::runtime_error("PBRT runtime is not initialized");
        if (pbrt::Options == nullptr) throw std::runtime_error("PBRT global options are unavailable");
        *pbrt::Options = this->state->baseline_options;
#ifdef PBRT_BUILD_GPU_RENDERER
        if (pbrt::Options->useGPU) pbrt::CopyOptionsToGPU();
#endif
    }

    void SpectraPbrtRuntime::wait_gpu_noexcept() const noexcept {
        try {
            if (pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
        } catch (...) {
        }
    }
}

namespace {
    constexpr std::uint32_t start_transform_bit{1u << 0u};
    constexpr std::uint32_t end_transform_bit{1u << 1u};
    constexpr std::uint32_t all_transform_bits{start_transform_bit | end_transform_bit};

    [[nodiscard]] xayah::SpectraPbrtFileLocation copy_file_location(const pbrt::FileLoc& location) {
        return {std::string{location.filename}, location.line, location.column};
    }

    [[nodiscard]] std::vector<xayah::SpectraPbrtParameter> copy_parameters(const pbrt::ParsedParameterVector& parameters) {
        std::vector<xayah::SpectraPbrtParameter> copied_parameters{};
        copied_parameters.reserve(parameters.size());
        for (const pbrt::ParsedParameter* parameter : parameters) {
            if (parameter == nullptr) throw std::runtime_error("PBRT parser produced a null parameter");
            xayah::SpectraPbrtParameter copied_parameter{};
            copied_parameter.type          = parameter->type;
            copied_parameter.name          = parameter->name;
            copied_parameter.location      = copy_file_location(parameter->loc);
            copied_parameter.may_be_unused = parameter->mayBeUnused;
            copied_parameter.floats.reserve(parameter->floats.size());
            copied_parameter.ints.reserve(parameter->ints.size());
            copied_parameter.strings.reserve(parameter->strings.size());
            copied_parameter.bools.reserve(parameter->bools.size());
            for (const pbrt::Float value : parameter->floats) copied_parameter.floats.push_back(static_cast<float>(value));
            for (const int value : parameter->ints) copied_parameter.ints.push_back(value);
            for (const std::string& value : parameter->strings) copied_parameter.strings.push_back(value);
            for (const std::uint8_t value : parameter->bools) copied_parameter.bools.push_back(value);
            copied_parameters.push_back(std::move(copied_parameter));
        }
        return copied_parameters;
    }

    [[nodiscard]] pbrt::Transform pbrt_transform_from_parser_matrix(const pbrt::Float transform[16]) {
        return pbrt::Transpose(pbrt::Transform{pbrt::SquareMatrix<4>{pstd::MakeSpan(transform, 16)}});
    }

    [[nodiscard]] pbrt::TransformSet inverse_transform_set(const pbrt::TransformSet& transform_set) {
        pbrt::TransformSet inverse_set{};
        for (int index = 0; index < pbrt::MaxTransforms; ++index) inverse_set[index] = pbrt::Inverse(transform_set[index]);
        return inverse_set;
    }

    [[nodiscard]] std::string first_string_parameter_value(const std::vector<xayah::SpectraPbrtParameter>& parameters, const std::string& parameter_name) {
        for (const xayah::SpectraPbrtParameter& parameter : parameters) {
            if (parameter.name == parameter_name && !parameter.strings.empty()) return parameter.strings.front();
        }
        return {};
    }

    template <typename Value>
    void append_vector(std::vector<Value>& destination, std::vector<Value>& source) {
        destination.reserve(destination.size() + source.size());
        for (Value& value : source) destination.push_back(std::move(value));
        source.clear();
    }

    void merge_setting(xayah::SpectraSceneRenderSetting& destination, xayah::SpectraSceneRenderSetting& source) {
        if (source.present) destination = std::move(source);
        source = {};
    }

    std::size_t find_or_create_object_definition(std::vector<xayah::SpectraSceneObjectDefinition>& object_definitions, const std::string& name, const xayah::SpectraPbrtFileLocation& location) {
        for (std::size_t index = 0; index < object_definitions.size(); ++index) {
            if (object_definitions[index].name == name) return index;
        }
        xayah::SpectraSceneObjectDefinition object_definition{};
        object_definition.name     = name;
        object_definition.location = location;
        object_definitions.push_back(std::move(object_definition));
        return object_definitions.size() - 1;
    }

    struct SpectraSceneBuildChunk {
        xayah::SpectraSceneRenderSetting pixel_filter{};
        xayah::SpectraSceneRenderSetting film{};
        xayah::SpectraSceneRenderSetting sampler{};
        xayah::SpectraSceneRenderSetting accelerator{};
        xayah::SpectraSceneRenderSetting integrator{};
        xayah::SpectraSceneRenderSetting camera{};
        std::vector<xayah::SpectraSceneTexture> textures{};
        std::vector<xayah::SpectraSceneMaterial> materials{};
        std::vector<xayah::SpectraSceneMedium> mediums{};
        std::vector<xayah::SpectraSceneMediumBinding> medium_bindings{};
        std::vector<xayah::SpectraSceneLight> lights{};
        std::vector<xayah::SpectraSceneShape> shapes{};
        std::vector<xayah::SpectraSceneObjectDefinition> object_definitions{};
        std::vector<xayah::SpectraSceneObjectInstance> object_instances{};
    };

    void append_scene_build_chunk(xayah::SpectraScene& scene, SpectraSceneBuildChunk chunk) {
        merge_setting(scene.pixel_filter, chunk.pixel_filter);
        merge_setting(scene.film, chunk.film);
        merge_setting(scene.sampler, chunk.sampler);
        merge_setting(scene.accelerator, chunk.accelerator);
        merge_setting(scene.integrator, chunk.integrator);
        merge_setting(scene.camera, chunk.camera);
        append_vector(scene.textures, chunk.textures);
        append_vector(scene.materials, chunk.materials);
        append_vector(scene.mediums, chunk.mediums);
        append_vector(scene.medium_bindings, chunk.medium_bindings);
        append_vector(scene.lights, chunk.lights);
        append_vector(scene.object_definitions, chunk.object_definitions);
        for (xayah::SpectraSceneShape& shape : chunk.shapes) {
            const std::string object_definition_name = shape.object_definition_name;
            const xayah::SpectraPbrtFileLocation shape_location = shape.location;
            const std::size_t shape_index = scene.shapes.size();
            scene.shapes.push_back(std::move(shape));
            if (!object_definition_name.empty()) {
                const std::size_t object_definition_index = find_or_create_object_definition(scene.object_definitions, object_definition_name, shape_location);
                scene.object_definitions[object_definition_index].shape_indices.push_back(shape_index);
            }
        }
        chunk.shapes.clear();
        append_vector(scene.object_instances, chunk.object_instances);
    }

    struct SpectraPbrtBuilderGraphicsState {
        std::string current_inside_medium{};
        std::string current_outside_medium{};
        std::string current_material_name{};
        int current_material_index{-1};
        std::string area_light_type{};
        xayah::SpectraPbrtFileLocation area_light_location{};
        std::vector<xayah::SpectraPbrtParameter> area_light_parameters{};
        bool reverse_orientation{false};
        pbrt::TransformSet ctm{};
        std::uint32_t active_transform_bits{all_transform_bits};
        pbrt::Float transform_start_time{0.0f};
        pbrt::Float transform_end_time{1.0f};
    };

    struct SpectraPbrtSceneBuilder final : pbrt::ParserTarget {
        xayah::SpectraScene* spectra_scene{nullptr};
        SpectraSceneBuildChunk chunk{};
        SpectraPbrtBuilderGraphicsState graphics_state{};
        std::vector<SpectraPbrtBuilderGraphicsState> pushed_graphics_states{};
        std::map<std::string, pbrt::TransformSet> named_coordinate_systems{};
        std::vector<std::unique_ptr<pbrt::ParserTarget>> imported_builders{};
        std::string active_object_definition_name{};
        std::size_t material_index_base{0};
        bool root_builder{true};
        bool world_begun{false};

        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene) : spectra_scene{&scene} {}
        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene, const SpectraPbrtBuilderGraphicsState& parent_graphics_state, const std::vector<SpectraPbrtBuilderGraphicsState>& parent_pushed_graphics_states, const std::map<std::string, pbrt::TransformSet>& parent_named_coordinate_systems, const std::string& parent_active_object_definition_name, const std::size_t parent_material_index_base, const bool parent_world_begun) : spectra_scene{&scene}, graphics_state{parent_graphics_state}, pushed_graphics_states{parent_pushed_graphics_states}, named_coordinate_systems{parent_named_coordinate_systems}, active_object_definition_name{parent_active_object_definition_name}, material_index_base{parent_material_index_base}, root_builder{false}, world_begun{parent_world_begun} {}

        ~SpectraPbrtSceneBuilder() override = default;

        SpectraPbrtSceneBuilder(const SpectraPbrtSceneBuilder& other)                = delete;
        SpectraPbrtSceneBuilder(SpectraPbrtSceneBuilder&& other) noexcept            = delete;
        SpectraPbrtSceneBuilder& operator=(const SpectraPbrtSceneBuilder& other)     = delete;
        SpectraPbrtSceneBuilder& operator=(SpectraPbrtSceneBuilder&& other) noexcept = delete;

        void merge_chunk(SpectraSceneBuildChunk& imported_chunk) {
            merge_setting(this->chunk.pixel_filter, imported_chunk.pixel_filter);
            merge_setting(this->chunk.film, imported_chunk.film);
            merge_setting(this->chunk.sampler, imported_chunk.sampler);
            merge_setting(this->chunk.accelerator, imported_chunk.accelerator);
            merge_setting(this->chunk.integrator, imported_chunk.integrator);
            merge_setting(this->chunk.camera, imported_chunk.camera);
            append_vector(this->chunk.textures, imported_chunk.textures);
            append_vector(this->chunk.materials, imported_chunk.materials);
            append_vector(this->chunk.mediums, imported_chunk.mediums);
            append_vector(this->chunk.medium_bindings, imported_chunk.medium_bindings);
            append_vector(this->chunk.lights, imported_chunk.lights);
            append_vector(this->chunk.shapes, imported_chunk.shapes);
            append_vector(this->chunk.object_definitions, imported_chunk.object_definitions);
            append_vector(this->chunk.object_instances, imported_chunk.object_instances);
        }

        [[nodiscard]] bool current_transform_is_animated() const {
            return this->graphics_state.ctm.IsAnimated();
        }

        [[nodiscard]] pbrt::Transform current_transform() const {
            return this->graphics_state.ctm[0];
        }

        [[nodiscard]] xayah::SpectraSceneRenderSetting make_render_setting(const std::string& type, const std::string& name, const std::vector<xayah::SpectraPbrtParameter>& parameters, const pbrt::FileLoc& location, const bool include_transform) const {
            xayah::SpectraSceneRenderSetting setting{};
            setting.present    = true;
            setting.type       = type;
            setting.name       = name;
            setting.location   = copy_file_location(location);
            setting.transform  = spectra_transform_from_pbrt(include_transform ? this->current_transform() : pbrt::Transform{});
            setting.parameters = parameters;
            return setting;
        }

        void apply_transform_to_active(const pbrt::Transform& transform) {
            for (int index = 0; index < pbrt::MaxTransforms; ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = this->graphics_state.ctm[index] * transform;
            }
        }

        void replace_active_transform(const pbrt::Transform& transform) {
            for (int index = 0; index < pbrt::MaxTransforms; ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = transform;
            }
        }

        void Scale(const pbrt::Float sx, const pbrt::Float sy, const pbrt::Float sz, const pbrt::FileLoc loc) override {
            (void)loc;
            this->apply_transform_to_active(pbrt::Scale(sx, sy, sz));
        }

        void Shape(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);

            const bool animated_transform = this->current_transform_is_animated();
            xayah::SpectraSceneShape shape{};
            shape.type                   = name;
            shape.material_name          = this->graphics_state.current_material_name;
            shape.material_index         = this->graphics_state.current_material_index;
            shape.inside_medium          = this->graphics_state.current_inside_medium;
            shape.outside_medium         = this->graphics_state.current_outside_medium;
            shape.object_definition_name = this->active_object_definition_name;
            shape.area_light_type        = this->graphics_state.area_light_type;
            shape.reverse_orientation    = this->graphics_state.reverse_orientation;
            shape.animated_transform     = animated_transform;
            shape.location               = copy_file_location(loc);
            shape.transform              = spectra_transform_from_pbrt(this->current_transform());
            shape.parameters             = copied_parameters;
            this->chunk.shapes.push_back(std::move(shape));

            if (!this->graphics_state.area_light_type.empty()) {
                xayah::SpectraSceneLight light{};
                light.type           = this->graphics_state.area_light_type;
                light.area           = true;
                light.outside_medium = this->graphics_state.current_outside_medium;
                light.location       = this->graphics_state.area_light_location;
                light.transform      = spectra_transform_from_pbrt(this->current_transform());
                light.parameters     = this->graphics_state.area_light_parameters;
                this->chunk.lights.push_back(std::move(light));
            }
        }

        void Option(const std::string& name, const std::string& value, const pbrt::FileLoc loc) override {
            (void)name;
            (void)value;
            (void)loc;
        }

        void Identity(const pbrt::FileLoc loc) override {
            (void)loc;
            this->replace_active_transform(pbrt::Transform{});
        }

        void Translate(const pbrt::Float dx, const pbrt::Float dy, const pbrt::Float dz, const pbrt::FileLoc loc) override {
            (void)loc;
            this->apply_transform_to_active(pbrt::Translate(pbrt::Vector3f{dx, dy, dz}));
        }

        void Rotate(const pbrt::Float angle, const pbrt::Float ax, const pbrt::Float ay, const pbrt::Float az, const pbrt::FileLoc loc) override {
            (void)loc;
            this->apply_transform_to_active(pbrt::Rotate(angle, pbrt::Vector3f{ax, ay, az}));
        }

        void LookAt(const pbrt::Float ex, const pbrt::Float ey, const pbrt::Float ez, const pbrt::Float lx, const pbrt::Float ly, const pbrt::Float lz, const pbrt::Float ux, const pbrt::Float uy, const pbrt::Float uz, const pbrt::FileLoc loc) override {
            (void)loc;
            this->apply_transform_to_active(pbrt::LookAt(pbrt::Point3f{ex, ey, ez}, pbrt::Point3f{lx, ly, lz}, pbrt::Vector3f{ux, uy, uz}));
        }

        void ConcatTransform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            (void)loc;
            this->apply_transform_to_active(pbrt_transform_from_parser_matrix(transform));
        }

        void Transform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            (void)loc;
            this->replace_active_transform(pbrt_transform_from_parser_matrix(transform));
        }

        void CoordinateSystem(const std::string& name, const pbrt::FileLoc loc) override {
            (void)loc;
            this->named_coordinate_systems[name] = this->graphics_state.ctm;
        }

        void CoordSysTransform(const std::string& name, const pbrt::FileLoc loc) override {
            (void)loc;
            const auto found = this->named_coordinate_systems.find(name);
            if (found != this->named_coordinate_systems.end()) this->graphics_state.ctm = found->second;
        }

        void ActiveTransformAll(const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.active_transform_bits = all_transform_bits;
        }

        void ActiveTransformEndTime(const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.active_transform_bits = end_transform_bit;
        }

        void ActiveTransformStartTime(const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.active_transform_bits = start_transform_bit;
        }

        void TransformTimes(const pbrt::Float start, const pbrt::Float end, const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.transform_start_time = start;
            this->graphics_state.transform_end_time   = end;
        }

        void ColorSpace(const std::string& name, const pbrt::FileLoc loc) override {
            (void)name;
            (void)loc;
        }

        void PixelFilter(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.pixel_filter = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.film = this->make_render_setting(type, {}, copied_parameters, loc, false);
        }

        void Accelerator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.accelerator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Integrator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.integrator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Camera(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.camera = this->make_render_setting(name, name, copied_parameters, loc, true);
            this->named_coordinate_systems["camera"] = inverse_transform_set(this->graphics_state.ctm);
        }

        void MakeNamedMedium(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraSceneMedium medium{};
            medium.name       = name;
            medium.type       = first_string_parameter_value(copied_parameters, "type");
            medium.location   = copy_file_location(loc);
            medium.transform  = spectra_transform_from_pbrt(this->current_transform());
            medium.parameters = copied_parameters;
            this->chunk.mediums.push_back(std::move(medium));
        }

        void MediumInterface(const std::string& inside_name, const std::string& outside_name, const pbrt::FileLoc loc) override {
            this->graphics_state.current_inside_medium  = inside_name;
            this->graphics_state.current_outside_medium = outside_name;
            xayah::SpectraSceneMediumBinding binding{};
            binding.inside   = inside_name;
            binding.outside  = outside_name;
            binding.location = copy_file_location(loc);
            this->chunk.medium_bindings.push_back(std::move(binding));
        }

        void Sampler(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->chunk.sampler = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void WorldBegin(const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state = {};
            this->pushed_graphics_states.clear();
            this->active_object_definition_name.clear();
            this->named_coordinate_systems["world"] = this->graphics_state.ctm;
            this->world_begun = true;
        }

        void AttributeBegin(const pbrt::FileLoc loc) override {
            (void)loc;
            this->pushed_graphics_states.push_back(this->graphics_state);
        }

        void AttributeEnd(const pbrt::FileLoc loc) override {
            (void)loc;
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
        }

        void Attribute(const std::string& target, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            (void)target;
            (void)parameters;
            (void)loc;
        }

        void Texture(const std::string& name, const std::string& type, const std::string& texture_name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraSceneTexture texture{};
            texture.name           = name;
            texture.value_type     = type == "float" ? xayah::SpectraSceneTextureValueType::Float : type == "spectrum" ? xayah::SpectraSceneTextureValueType::Spectrum : xayah::SpectraSceneTextureValueType::Unknown;
            texture.implementation = texture_name;
            texture.location       = copy_file_location(loc);
            texture.transform      = spectra_transform_from_pbrt(this->current_transform());
            texture.parameters     = copied_parameters;
            this->chunk.textures.push_back(std::move(texture));
        }

        void Material(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraSceneMaterial material{};
            material.name       = {};
            material.type       = name;
            material.named      = false;
            material.location   = copy_file_location(loc);
            material.parameters = copied_parameters;
            this->chunk.materials.push_back(std::move(material));
            this->graphics_state.current_material_index = static_cast<int>(this->material_index_base + this->chunk.materials.size() - 1);
            this->graphics_state.current_material_name.clear();
        }

        void MakeNamedMaterial(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraSceneMaterial material{};
            material.name       = name;
            material.type       = first_string_parameter_value(copied_parameters, "type");
            material.named      = true;
            material.location   = copy_file_location(loc);
            material.parameters = copied_parameters;
            this->chunk.materials.push_back(std::move(material));
        }

        void NamedMaterial(const std::string& name, const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.current_material_name  = name;
            this->graphics_state.current_material_index = -1;
        }

        void LightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraSceneLight light{};
            light.type           = name;
            light.area           = false;
            light.outside_medium = this->graphics_state.current_outside_medium;
            light.location       = copy_file_location(loc);
            light.transform      = spectra_transform_from_pbrt(this->current_transform());
            light.parameters     = copied_parameters;
            this->chunk.lights.push_back(std::move(light));
        }

        void AreaLightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            this->graphics_state.area_light_type       = name;
            this->graphics_state.area_light_location   = copy_file_location(loc);
            this->graphics_state.area_light_parameters = copied_parameters;
        }

        void ReverseOrientation(const pbrt::FileLoc loc) override {
            (void)loc;
            this->graphics_state.reverse_orientation = !this->graphics_state.reverse_orientation;
        }

        void ObjectBegin(const std::string& name, const pbrt::FileLoc loc) override {
            this->pushed_graphics_states.push_back(this->graphics_state);
            this->active_object_definition_name = name;
            find_or_create_object_definition(this->chunk.object_definitions, name, copy_file_location(loc));
        }

        void ObjectEnd(const pbrt::FileLoc loc) override {
            (void)loc;
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
            this->active_object_definition_name.clear();
        }

        void ObjectInstance(const std::string& name, const pbrt::FileLoc loc) override {
            const bool animated_transform = this->current_transform_is_animated();
            xayah::SpectraSceneObjectInstance object_instance{};
            object_instance.name               = name;
            object_instance.animated_transform = animated_transform;
            object_instance.location           = copy_file_location(loc);
            object_instance.transform          = spectra_transform_from_pbrt(this->current_transform());
            this->chunk.object_instances.push_back(std::move(object_instance));
        }

        void EndOfFiles() override {
            if (!this->pushed_graphics_states.empty()) throw std::runtime_error("Missing AttributeEnd before EndOfFiles in Spectra scene parser");
            if (this->root_builder) {
                if (this->spectra_scene == nullptr) throw std::runtime_error("Spectra scene builder has no target scene at EndOfFiles");
                append_scene_build_chunk(*this->spectra_scene, std::move(this->chunk));
            }
        }

        bool IsImportAllowed() const override {
            return this->world_begun;
        }

        std::unique_ptr<pbrt::ParserTarget> CopyForImport() override {
            if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot copy Spectra scene parser import target without a scene");
            return std::make_unique<SpectraPbrtSceneBuilder>(*this->spectra_scene, this->graphics_state, this->pushed_graphics_states, this->named_coordinate_systems, this->active_object_definition_name, this->material_index_base + this->chunk.materials.size(), this->world_begun);
        }

        void MergeImported(std::unique_ptr<pbrt::ParserTarget> imported) override {
            SpectraPbrtSceneBuilder* imported_builder = dynamic_cast<SpectraPbrtSceneBuilder*>(imported.get());
            if (imported_builder == nullptr) throw std::runtime_error("PBRT import target type does not match Spectra scene builder");
            this->merge_chunk(imported_builder->chunk);
            this->imported_builders.push_back(std::move(imported));
        }

        bool UsesAsyncImport() const override {
            return false;
        }
    };

    [[nodiscard]] pbrt::ParsedParameter* make_integer_parameter(const std::string_view name, const int value, const pbrt::FileLoc& location) {
        pbrt::ParsedParameter* parameter = new pbrt::ParsedParameter(location);
        parameter->type                  = "integer";
        parameter->name                  = std::string{name};
        parameter->AddInt(value);
        return parameter;
    }

    [[nodiscard]] pbrt::ParsedParameterVector film_parameters_with_resolution(pbrt::ParsedParameterVector parameters, const std::array<int, 2>& resolution, const pbrt::FileLoc& location) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT backend resolution must be positive");
        for (auto iterator = parameters.begin(); iterator != parameters.end();) {
            pbrt::ParsedParameter* parameter = *iterator;
            if (parameter == nullptr) throw std::runtime_error("PBRT Film parameter is null");
            if (parameter->name == "xresolution" || parameter->name == "yresolution") {
                delete parameter;
                iterator = parameters.erase(iterator);
            } else
                ++iterator;
        }
        parameters.push_back(make_integer_parameter("xresolution", resolution[0], location));
        parameters.push_back(make_integer_parameter("yresolution", resolution[1], location));
        return parameters;
    }

    struct SpectraResolutionOverrideSceneBuilder final : pbrt::BasicSceneBuilder {
        SpectraResolutionOverrideSceneBuilder(pbrt::BasicScene* scene, const std::array<int, 2>& resolution) : pbrt::BasicSceneBuilder{scene}, resolution{resolution} {
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT backend resolution must be positive");
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector parameters, pbrt::FileLoc location) override {
            this->film_seen = true;
            pbrt::BasicSceneBuilder::Film(type, film_parameters_with_resolution(std::move(parameters), this->resolution, location), location);
        }

        void WorldBegin(pbrt::FileLoc location) override {
            if (!this->film_seen) pbrt::BasicSceneBuilder::Film("rgb", film_parameters_with_resolution(pbrt::ParsedParameterVector{}, this->resolution, location), location);
            pbrt::BasicSceneBuilder::WorldBegin(location);
        }

        std::array<int, 2> resolution{0, 0};
        bool film_seen{false};
    };
}

namespace xayah {
    void SpectraScene::load(const std::filesystem::path& path) {
        if (!this->scene_path.empty()) throw std::runtime_error("Spectra scene is already loaded");
        if (path.empty()) throw std::runtime_error("Spectra scene path is empty");
        if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"Spectra scene does not exist: "} + path.string());

        try {
            this->scene_path      = path;
            this->scene_label     = path.filename().string();
            this->scene_path_text = path.string();
            SpectraPbrtSceneBuilder builder{*this};
            std::vector<std::string> filenames{this->scene_path_text};
            pbrt::ParseFiles(&builder, filenames);
            this->parsed = true;
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraScene::set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const SpectraPbrtTransform& camera_transform) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT film resolution must be positive");
        if (samples_per_pixel <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
        this->film_resolution      = resolution;
        this->sampler_sample_count = samples_per_pixel;
        this->camera_from_world    = camera_transform;
    }

    void SpectraScene::unload_noexcept() noexcept {
        this->scene_path.clear();
        this->scene_label = "No Scene";
        this->scene_path_text.clear();
        this->film_resolution      = {0, 0};
        this->camera_from_world    = SpectraPbrtTransform{};
        this->sampler_sample_count = 0;
        this->parsed               = false;
        this->pixel_filter = {};
        this->film = {};
        this->sampler = {};
        this->accelerator = {};
        this->integrator = {};
        this->camera = {};
        this->textures.clear();
        this->materials.clear();
        this->mediums.clear();
        this->medium_bindings.clear();
        this->lights.clear();
        this->shapes.clear();
        this->object_definitions.clear();
        this->object_instances.clear();
    }

}

namespace {
    [[nodiscard]] vk::ExternalMemoryHandleTypeFlagBits pbrt_external_memory_handle_type() {
#if defined(_WIN32)
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] vk::ExternalSemaphoreHandleTypeFlagBits pbrt_external_semaphore_handle_type() {
#if defined(_WIN32)
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalMemoryHandleType pbrt_cuda_external_memory_handle_type() {
#if defined(_WIN32)
        return cudaExternalMemoryHandleTypeOpaqueWin32;
#else
        return cudaExternalMemoryHandleTypeOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalSemaphoreHandleType pbrt_cuda_external_semaphore_handle_type() {
#if defined(_WIN32)
        return cudaExternalSemaphoreHandleTypeOpaqueWin32;
#else
        return cudaExternalSemaphoreHandleTypeOpaqueFd;
#endif
    }

}

namespace xayah {
    struct SpectraPbrtPathtracerState {
        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            cudaExternalMemory_t cuda_external_memory{};
            cudaExternalSemaphore_t cuda_external_semaphore{};
            float* cuda_pixels{nullptr};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        std::filesystem::path scene_path{};
        std::unique_ptr<pbrt::BasicScene> scene{};
        std::unique_ptr<pbrt::BasicSceneBuilder> builder{};
        std::unique_ptr<pbrt::WavefrontPathIntegrator> integrator{};
        pbrt::Bounds2i pixel_bounds{};
        pbrt::Vector2i resolution{};
        pbrt::Transform render_from_camera{};
        pbrt::Transform camera_from_render{};
        pbrt::Transform camera_from_world{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        float exposure{1.0f};
        float initial_move_scale{1.0f};
        SpectraPbrtBounds3 initial_focus_bounds{};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};
    };
}

namespace {
    [[nodiscard]] xayah::SpectraPbrtPathtracerState& require_pathtracer_state(std::unique_ptr<xayah::SpectraPbrtPathtracerState>& state) {
        if (state == nullptr) throw std::runtime_error("PBRT pathtracer state is null");
        return *state;
    }

    [[nodiscard]] const xayah::SpectraPbrtPathtracerState& require_pathtracer_state(const std::unique_ptr<xayah::SpectraPbrtPathtracerState>& state) {
        if (state == nullptr) throw std::runtime_error("PBRT pathtracer state is null");
        return *state;
    }

    void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) {
        int cuda_device = 0;
        CUDA_CHECK(cudaGetDevice(&cuda_device));
        cudaDeviceProp cuda_properties{};
        CUDA_CHECK(cudaGetDeviceProperties(&cuda_properties, cuda_device));
        const auto vulkan_properties = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
        const vk::PhysicalDeviceIDProperties& vulkan_id = vulkan_properties.get<vk::PhysicalDeviceIDProperties>();
#if defined(_WIN32)
        if (!vulkan_id.deviceLUIDValid) throw std::runtime_error("Selected Vulkan device does not expose a valid LUID for CUDA interop");
        for (std::size_t index = 0; index < VK_LUID_SIZE; ++index) {
            if (static_cast<unsigned char>(cuda_properties.luid[index]) != vulkan_id.deviceLUID[index]) throw std::runtime_error("CUDA device LUID does not match selected Vulkan device LUID");
        }
#else
        for (std::size_t index = 0; index < VK_UUID_SIZE; ++index) {
            if (static_cast<unsigned char>(cuda_properties.uuid.bytes[index]) != vulkan_id.deviceUUID[index]) throw std::runtime_error("CUDA device UUID does not match selected Vulkan device UUID");
        }
#endif
    }

    void release_pathtracer_viewport_descriptors_noexcept(xayah::SpectraPbrtPathtracerState& pathtracer) noexcept {
        for (xayah::SpectraPbrtPathtracerState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                frame.imgui_descriptor = VK_NULL_HANDLE;
            }
        }
    }

    void create_pathtracer_viewport_descriptors(xayah::SpectraPbrtPathtracerState& pathtracer) {
        for (xayah::SpectraPbrtPathtracerState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("PBRT pathtracer viewport descriptor is already allocated");
            frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate PBRT pathtracer viewport descriptor");
        }
    }

    void destroy_pathtracer_frame_resources_noexcept(xayah::SpectraPbrtPathtracerState& pathtracer) noexcept {
        release_pathtracer_viewport_descriptors_noexcept(pathtracer);
        for (xayah::SpectraPbrtPathtracerState::FrameResource& frame : pathtracer.frames) {
            if (frame.cuda_pixels != nullptr) {
                cudaFree(frame.cuda_pixels);
                frame.cuda_pixels = nullptr;
            }
            if (frame.cuda_external_semaphore != nullptr) {
                cudaDestroyExternalSemaphore(frame.cuda_external_semaphore);
                frame.cuda_external_semaphore = nullptr;
            }
            if (frame.cuda_external_memory != nullptr) {
                cudaDestroyExternalMemory(frame.cuda_external_memory);
                frame.cuda_external_memory = nullptr;
            }
        }
        pathtracer.frames.clear();
        pathtracer.active_frame_index = 0;
    }

    void create_pathtracer_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::SpectraPbrtPathtracerState::FrameResource& frame, const vk::DeviceSize rgba_bytes) {
        const vk::ExternalMemoryBufferCreateInfo external_buffer_info{pbrt_external_memory_handle_type()};
        const vk::BufferCreateInfo buffer_create_info{{}, rgba_bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 0, nullptr, &external_buffer_info};
        frame.interop_buffer = vk::raii::Buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = frame.interop_buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::ExportMemoryAllocateInfo export_allocate_info{pbrt_external_memory_handle_type()};
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type, &export_allocate_info};
        frame.interop_memory = vk::raii::DeviceMemory{device, allocate_info};
        frame.interop_buffer.bindMemory(*frame.interop_memory, 0);
        frame.interop_allocation_size = memory_requirements.size;
        frame.interop_buffer_size     = rgba_bytes;

        cudaExternalMemoryHandleDesc memory_handle_desc{};
        memory_handle_desc.type = pbrt_cuda_external_memory_handle_type();
        memory_handle_desc.size = static_cast<unsigned long long>(frame.interop_allocation_size);
#if defined(_WIN32)
        const vk::MemoryGetWin32HandleInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
        HANDLE memory_handle = device.getMemoryWin32HandleKHR(memory_handle_info);
        if (memory_handle == nullptr) throw std::runtime_error("Failed to export Vulkan memory Win32 handle for CUDA");
        memory_handle_desc.handle.win32.handle = memory_handle;
        CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
        CloseHandle(memory_handle);
#else
        const vk::MemoryGetFdInfoKHR memory_handle_info{*frame.interop_memory, pbrt_external_memory_handle_type()};
        int memory_fd = device.getMemoryFdKHR(memory_handle_info);
        if (memory_fd < 0) throw std::runtime_error("Failed to export Vulkan memory FD for CUDA");
        memory_handle_desc.handle.fd = memory_fd;
        CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
        close(memory_fd);
#endif

        cudaExternalMemoryBufferDesc buffer_desc{};
        buffer_desc.offset = 0;
        buffer_desc.size   = static_cast<unsigned long long>(frame.interop_buffer_size);
        CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(reinterpret_cast<void**>(&frame.cuda_pixels), frame.cuda_external_memory, &buffer_desc));
        if (frame.cuda_pixels == nullptr) throw std::runtime_error("CUDA external memory mapped to a null PBRT RGBA pointer");
    }

    void create_pathtracer_cuda_complete_semaphore(const vk::raii::Device& device, xayah::SpectraPbrtPathtracerState::FrameResource& frame) {
        const vk::ExportSemaphoreCreateInfo export_semaphore_info{pbrt_external_semaphore_handle_type()};
        const vk::SemaphoreCreateInfo semaphore_create_info{{}, &export_semaphore_info};
        frame.cuda_complete_semaphore = vk::raii::Semaphore{device, semaphore_create_info};

        cudaExternalSemaphoreHandleDesc semaphore_handle_desc{};
        semaphore_handle_desc.type = pbrt_cuda_external_semaphore_handle_type();
#if defined(_WIN32)
        const vk::SemaphoreGetWin32HandleInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
        HANDLE semaphore_handle = device.getSemaphoreWin32HandleKHR(semaphore_handle_info);
        if (semaphore_handle == nullptr) throw std::runtime_error("Failed to export Vulkan semaphore Win32 handle for CUDA");
        semaphore_handle_desc.handle.win32.handle = semaphore_handle;
        CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
        CloseHandle(semaphore_handle);
#else
        const vk::SemaphoreGetFdInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, pbrt_external_semaphore_handle_type()};
        int semaphore_fd = device.getSemaphoreFdKHR(semaphore_handle_info);
        if (semaphore_fd < 0) throw std::runtime_error("Failed to export Vulkan semaphore FD for CUDA");
        semaphore_handle_desc.handle.fd = semaphore_fd;
        CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
        close(semaphore_fd);
#endif
        if (frame.cuda_external_semaphore == nullptr) throw std::runtime_error("CUDA external semaphore import returned null");
    }

    void create_pathtracer_display_image(xayah::SpectraPbrtPathtracerState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::SpectraPbrtPathtracerState::FrameResource& frame) {
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            pathtracer.display_format,
            vk::Extent3D{static_cast<std::uint32_t>(pathtracer.resolution.x), static_cast<std::uint32_t>(pathtracer.resolution.y), 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        frame.image = vk::raii::Image{device, image_create_info};

        const vk::MemoryRequirements memory_requirements = frame.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
        frame.image_memory = vk::raii::DeviceMemory{device, allocate_info};
        frame.image.bindMemory(*frame.image_memory, 0);

        const vk::ImageViewCreateInfo image_view_create_info{
            {},
            *frame.image,
            vk::ImageViewType::e2D,
            pathtracer.display_format,
            {},
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        frame.image_view = vk::raii::ImageView{device, image_view_create_info};

        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eNearest,
            vk::Filter::eNearest,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eNever,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatOpaqueBlack,
            VK_FALSE,
        };
        frame.sampler = vk::raii::Sampler{device, sampler_create_info};
    }

    void create_pathtracer_frame_resources(xayah::SpectraPbrtPathtracerState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
        const vk::FormatProperties format_properties = physical_device.getFormatProperties(pathtracer.display_format);
        constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

        const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(pathtracer.resolution.x) * static_cast<vk::DeviceSize>(pathtracer.resolution.y);
        if (rgba_bytes == 0) throw std::runtime_error("PBRT pathtracer interop buffer cannot be zero bytes");
        pathtracer.frames.resize(frame_count);
        for (xayah::SpectraPbrtPathtracerState::FrameResource& frame : pathtracer.frames) {
            create_pathtracer_interop_buffer(physical_device, device, frame, rgba_bytes);
            create_pathtracer_cuda_complete_semaphore(device, frame);
            create_pathtracer_display_image(pathtracer, physical_device, device, frame);
        }
    }

    void destroy_pathtracer_resources_noexcept(xayah::SpectraPbrtPathtracerState& pathtracer) noexcept {
        try {
            if (pathtracer.device != nullptr) pathtracer.device->waitIdle();
            if (pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
        } catch (...) {
        }
        destroy_pathtracer_frame_resources_noexcept(pathtracer);
        pathtracer.integrator.reset();
        pathtracer.builder.reset();
        pathtracer.scene.reset();
    }
}

namespace xayah {

    SpectraPbrtPathtracer::SpectraPbrtPathtracer(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : state{std::make_unique<SpectraPbrtPathtracerState>()} {
        try {
            SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
            if (spectra_scene.scene_path.empty()) throw std::runtime_error("Cannot create PBRT pathtracer without a loaded Spectra scene");
            if (!std::filesystem::exists(spectra_scene.scene_path)) throw std::runtime_error(std::string{"PBRT scene does not exist: "} + spectra_scene.scene_path.string());
            if (!spectra_scene.parsed) throw std::runtime_error("Spectra scene has not completed PBRT parsing");
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create PBRT pathtracer with a non-positive resolution");
            if (frame_count == 0) throw std::runtime_error("PBRT pathtracer requires at least one frame in flight");
            if (pbrt::Options == nullptr) throw std::runtime_error("Cannot create PBRT pathtracer before PBRT runtime is initialized");

            pathtracer.scene_path       = spectra_scene.scene_path;
            pathtracer.physical_device  = &physical_device;
            pathtracer.device           = &device;
            pathtracer.frame_count      = frame_count;
            pathtracer.scene            = std::make_unique<pbrt::BasicScene>();
            pathtracer.builder          = std::make_unique<SpectraResolutionOverrideSceneBuilder>(pathtracer.scene.get(), resolution);
            std::vector<std::string> filenames{spectra_scene.scene_path_text};
            pbrt::ParseFiles(pathtracer.builder.get(), filenames);

            pathtracer.integrator = std::make_unique<pbrt::WavefrontPathIntegrator>(&pbrt::CUDATrackedMemoryResource::singleton, *pathtracer.scene);
#ifdef PBRT_BUILD_GPU_RENDERER
            if (pbrt::Options != nullptr && pbrt::Options->useGPU) pathtracer.integrator->PrefetchGPUAllocations();
#endif
            pathtracer.pixel_bounds = pathtracer.integrator->film.PixelBounds();
            pathtracer.resolution   = pathtracer.pixel_bounds.Diagonal();
            if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive");
            pathtracer.max_samples = pathtracer.integrator->sampler.SamplesPerPixel();
            if (pathtracer.max_samples <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
            pathtracer.target_samples = pathtracer.max_samples;
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, pbrt::Transform{}, pathtracer.sample_index);
            ++pathtracer.sample_index;
            pbrt::GPUWait();

            pathtracer.render_from_camera = pathtracer.integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
            pathtracer.camera_from_render = pbrt::Inverse(pathtracer.render_from_camera);
            pathtracer.camera_from_world  = pathtracer.integrator->camera.GetCameraTransform().CameraFromWorld(pathtracer.integrator->camera.SampleTime(0.0f));
            const pbrt::Bounds3f scene_bounds = pathtracer.integrator->aggregate->Bounds();
            pathtracer.initial_move_scale     = pbrt::Length(scene_bounds.Diagonal()) / 1000.0f;
            if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("PBRT scene bounds must define a positive interactive move scale");
            const pbrt::Transform world_from_render = pbrt::Inverse(pathtracer.render_from_camera * pathtracer.camera_from_world);
            pbrt::Bounds3f world_bounds{};
            bool has_world_bounds = false;
            for (const float x : std::array<float, 2>{scene_bounds.pMin.x, scene_bounds.pMax.x}) {
                for (const float y : std::array<float, 2>{scene_bounds.pMin.y, scene_bounds.pMax.y}) {
                    for (const float z : std::array<float, 2>{scene_bounds.pMin.z, scene_bounds.pMax.z}) {
                        const pbrt::Point3f corner_world = world_from_render(pbrt::Point3f{x, y, z});
                        validate_finite_point(corner_world, "PBRT scene focus bounds contain a non-finite value");
                        if (!has_world_bounds) world_bounds = pbrt::Bounds3f{corner_world};
                        else world_bounds = pbrt::Union(world_bounds, corner_world);
                        has_world_bounds = true;
                    }
                }
            }
            if (!has_world_bounds) throw std::runtime_error("PBRT scene focus bounds are unavailable");
            pathtracer.initial_focus_bounds = spectra_bounds_from_pbrt(world_bounds);

            validate_cuda_vulkan_device(physical_device);
            create_pathtracer_frame_resources(pathtracer, physical_device, device, frame_count);
            create_pathtracer_viewport_descriptors(pathtracer);
        } catch (...) {
            if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
            throw;
        }
    }

    SpectraPbrtPathtracer::~SpectraPbrtPathtracer() noexcept {
        if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
    }

    [[nodiscard]] int SpectraPbrtPathtracer::current_sample() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.reset_requested) return 0;
        return pathtracer.sample_index;
    }

    [[nodiscard]] int SpectraPbrtPathtracer::sampler_sample_count() const {
        return require_pathtracer_state(this->state).max_samples;
    }

    [[nodiscard]] int SpectraPbrtPathtracer::target_sample_count() const {
        return require_pathtracer_state(this->state).target_samples;
    }

    [[nodiscard]] float SpectraPbrtPathtracer::current_exposure() const {
        return require_pathtracer_state(this->state).exposure;
    }

    [[nodiscard]] float SpectraPbrtPathtracer::camera_initial_move_scale() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("PBRT pathtracer camera initial move scale must be positive");
        return pathtracer.initial_move_scale;
    }

    [[nodiscard]] SpectraPbrtBounds3 SpectraPbrtPathtracer::camera_initial_focus_bounds() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        validate_pbrt_bounds(pathtracer.initial_focus_bounds, "PBRT pathtracer camera initial focus bounds are invalid");
        return pathtracer.initial_focus_bounds;
    }

    [[nodiscard]] std::array<int, 2> SpectraPbrtPathtracer::film_resolution() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before metadata is queried");
        return {pathtracer.resolution.x, pathtracer.resolution.y};
    }

    [[nodiscard]] SpectraPbrtTransform SpectraPbrtPathtracer::camera_from_world_transform() const {
        return spectra_transform_from_pbrt(require_pathtracer_state(this->state).camera_from_world);
    }

    [[nodiscard]] std::uint64_t SpectraPbrtPathtracer::film_pixel_count() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before statistics are queried");
        return static_cast<std::uint64_t>(pathtracer.resolution.x) * static_cast<std::uint64_t>(pathtracer.resolution.y);
    }

    [[nodiscard]] float SpectraPbrtPathtracer::completion_ratio() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.target_samples <= 0) throw std::runtime_error("PBRT target sample count must be positive before statistics are queried");
        const int visible_sample = this->current_sample();
        if (visible_sample < 0 || visible_sample > pathtracer.target_samples) throw std::runtime_error("PBRT visible sample count is outside the target sample range");
        return static_cast<float>(visible_sample) / static_cast<float>(pathtracer.target_samples);
    }

    [[nodiscard]] VkDescriptorSet SpectraPbrtPathtracer::active_descriptor() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) return VK_NULL_HANDLE;
        return pathtracer.frames.at(pathtracer.active_frame_index).imgui_descriptor;
    }

    [[nodiscard]] vk::Semaphore SpectraPbrtPathtracer::active_cuda_complete_semaphore() const {
        const SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) throw std::runtime_error("PBRT completion semaphore requested without frame resources");
        return *pathtracer.frames.at(pathtracer.active_frame_index).cuda_complete_semaphore;
    }

    void SpectraPbrtPathtracer::set_target_sample_count(const int target_sample_count) {
        SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (target_sample_count < 1 || target_sample_count > pathtracer.max_samples) throw std::runtime_error("PBRT target sample count is outside the sampler SPP range");
        if (target_sample_count == pathtracer.target_samples) return;
        pathtracer.target_samples = target_sample_count;
        this->request_reset_accumulation();
    }

    void SpectraPbrtPathtracer::set_exposure(const float value) {
        if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("PBRT exposure must be in [0.001, 1000]");
        require_pathtracer_state(this->state).exposure = value;
    }

    void SpectraPbrtPathtracer::request_reset_accumulation() {
        require_pathtracer_state(this->state).reset_requested = true;
    }

    void SpectraPbrtPathtracer::release_viewport_descriptors_noexcept() noexcept {
        if (this->state != nullptr) release_pathtracer_viewport_descriptors_noexcept(*this->state);
    }

    void SpectraPbrtPathtracer::create_viewport_descriptors() {
        create_pathtracer_viewport_descriptors(require_pathtracer_state(this->state));
    }

    [[nodiscard]] SpectraPbrtPathtracer::RenderFrameResult SpectraPbrtPathtracer::render_frame(const std::uint32_t frame_index, const SpectraPbrtTransform& moving_from_camera) {
        SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (frame_index >= pathtracer.frames.size()) throw std::runtime_error("PBRT pathtracer frame index is out of range");
        pathtracer.active_frame_index = frame_index;
        RenderFrameResult result{};
        const pbrt::Transform camera_motion = pathtracer.render_from_camera * pbrt_transform_from_spectra(moving_from_camera) * pathtracer.camera_from_render;
        if (pathtracer.reset_requested) {
            if (pathtracer.physical_device == nullptr || pathtracer.device == nullptr) throw std::runtime_error("PBRT pathtracer Vulkan handles are not available for reset");
            pathtracer.device->waitIdle();
            destroy_pathtracer_frame_resources_noexcept(pathtracer);
            pathtracer.integrator->ResetFilm(pathtracer.pixel_bounds);
            pbrt::GPUWait();
            pathtracer.sample_index    = 0;
            pathtracer.reset_requested = false;
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, camera_motion, pathtracer.sample_index);
            ++pathtracer.sample_index;
            pbrt::GPUWait();
            create_pathtracer_frame_resources(pathtracer, *pathtracer.physical_device, *pathtracer.device, pathtracer.frame_count);
            create_pathtracer_viewport_descriptors(pathtracer);
            pathtracer.active_frame_index = frame_index;
            result.rendered_sample    = true;
            result.sample_pixels      = this->film_pixel_count() * static_cast<std::uint64_t>(pathtracer.sample_index);
            result.reset_accumulation = true;
        } else if (pathtracer.sample_index < pathtracer.target_samples) {
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, camera_motion, pathtracer.sample_index);
            ++pathtracer.sample_index;
            result.rendered_sample = true;
            result.sample_pixels   = this->film_pixel_count();
        }
        SpectraPbrtPathtracerState::FrameResource& output_frame = pathtracer.frames.at(frame_index);
        pathtracer.integrator->UpdateFramebufferFromFilm(pathtracer.pixel_bounds, pathtracer.exposure, output_frame.cuda_pixels);

        cudaExternalSemaphoreSignalParams signal_params{};
        CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
        return result;
    }

    void SpectraPbrtPathtracer::record_copy(const vk::raii::CommandBuffer& command_buffer) {
        SpectraPbrtPathtracerState& pathtracer = require_pathtracer_state(this->state);
        SpectraPbrtPathtracerState::FrameResource& frame = pathtracer.frames.at(pathtracer.active_frame_index);
        const vk::PipelineStageFlags2 src_image_stage = frame.image_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader;
        const vk::AccessFlags2 src_image_access       = frame.image_layout == vk::ImageLayout::eUndefined ? vk::AccessFlagBits2::eNone : vk::AccessFlagBits2::eShaderSampledRead;
        transition_image_layout(command_buffer, *frame.image, frame.image_layout, vk::ImageLayout::eTransferDstOptimal, vk::ImageAspectFlagBits::eColor, src_image_stage, src_image_access, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame.image_layout = vk::ImageLayout::eTransferDstOptimal;

        const vk::BufferMemoryBarrier2 buffer_barrier{
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryWrite,
            vk::PipelineStageFlagBits2::eTransfer,
            vk::AccessFlagBits2::eTransferRead,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            *frame.interop_buffer,
            0,
            frame.interop_buffer_size,
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_barrier, 0, nullptr};
        command_buffer.pipelineBarrier2(dependency_info);

        const vk::BufferImageCopy copy_region{
            0,
            0,
            0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            {static_cast<std::uint32_t>(pathtracer.resolution.x), static_cast<std::uint32_t>(pathtracer.resolution.y), 1},
        };
        command_buffer.copyBufferToImage(*frame.interop_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, copy_region);

        transition_image_layout(command_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame.image_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }


} // namespace xayah
