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
#define GLFW_INCLUDE_VULKAN
#include <driver_types.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_mono.h>
#include <roboto/roboto_regular.h>
#include <pbrt/gpu/util.h>
#include <pbrt/options.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>
#include <vulkan/vulkan_raii.hpp>
#include "spectra_pbrt_fwd.h"
module spectra;
import std;
#include "spectra_internal.h"

namespace xayah {
    struct SpectraPbrtRuntimeState {
        pbrt::PBRTOptions baseline_options{};
        bool initialized{false};
    };

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

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return VK_FALSE;
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

    [[nodiscard]] std::array<float, 16> identity_matrix_array() {
        return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    }

    void validate_matrix_array(const std::array<float, 16>& values) {
        for (const float value : values) {
            if (!std::isfinite(value)) throw std::runtime_error("Camera matrix contains a non-finite value");
        }
    }

    [[nodiscard]] std::array<float, 16> multiply_matrix_arrays(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs) {
        validate_matrix_array(lhs);
        validate_matrix_array(rhs);
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                float value = 0.0f;
                for (std::size_t index = 0; index < 4; ++index) value += lhs[row * 4 + index] * rhs[index * 4 + column];
                result[row * 4 + column] = value;
            }
        }
        validate_matrix_array(result);
        return result;
    }

    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform) {
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

    [[nodiscard]] pbrt::Transform transform_from_matrix_array(const std::array<float, 16>& values) {
        pbrt::Float matrix[4][4]{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = values[row * 4 + column];
                if (!std::isfinite(value)) throw std::runtime_error("Camera transform matrix contains a non-finite value");
                matrix[row][column] = static_cast<pbrt::Float>(value);
            }
        }
        return pbrt::Transform{matrix};
    }

    [[nodiscard]] std::array<float, 16> transpose_matrix_array(const std::array<float, 16>& matrix) {
        validate_matrix_array(matrix);
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) result[row * 4 + column] = matrix[column * 4 + row];
        }
        validate_matrix_array(result);
        return result;
    }

    [[nodiscard]] std::array<float, 16> inverse_matrix_array(const std::array<float, 16>& matrix) {
        validate_matrix_array(matrix);
        std::array<float, 16> inverse{};
        inverse[0]  = matrix[5] * matrix[10] * matrix[15] - matrix[5] * matrix[11] * matrix[14] - matrix[9] * matrix[6] * matrix[15] + matrix[9] * matrix[7] * matrix[14] + matrix[13] * matrix[6] * matrix[11] - matrix[13] * matrix[7] * matrix[10];
        inverse[4]  = -matrix[4] * matrix[10] * matrix[15] + matrix[4] * matrix[11] * matrix[14] + matrix[8] * matrix[6] * matrix[15] - matrix[8] * matrix[7] * matrix[14] - matrix[12] * matrix[6] * matrix[11] + matrix[12] * matrix[7] * matrix[10];
        inverse[8]  = matrix[4] * matrix[9] * matrix[15] - matrix[4] * matrix[11] * matrix[13] - matrix[8] * matrix[5] * matrix[15] + matrix[8] * matrix[7] * matrix[13] + matrix[12] * matrix[5] * matrix[11] - matrix[12] * matrix[7] * matrix[9];
        inverse[12] = -matrix[4] * matrix[9] * matrix[14] + matrix[4] * matrix[10] * matrix[13] + matrix[8] * matrix[5] * matrix[14] - matrix[8] * matrix[6] * matrix[13] - matrix[12] * matrix[5] * matrix[10] + matrix[12] * matrix[6] * matrix[9];
        inverse[1]  = -matrix[1] * matrix[10] * matrix[15] + matrix[1] * matrix[11] * matrix[14] + matrix[9] * matrix[2] * matrix[15] - matrix[9] * matrix[3] * matrix[14] - matrix[13] * matrix[2] * matrix[11] + matrix[13] * matrix[3] * matrix[10];
        inverse[5]  = matrix[0] * matrix[10] * matrix[15] - matrix[0] * matrix[11] * matrix[14] - matrix[8] * matrix[2] * matrix[15] + matrix[8] * matrix[3] * matrix[14] + matrix[12] * matrix[2] * matrix[11] - matrix[12] * matrix[3] * matrix[10];
        inverse[9]  = -matrix[0] * matrix[9] * matrix[15] + matrix[0] * matrix[11] * matrix[13] + matrix[8] * matrix[1] * matrix[15] - matrix[8] * matrix[3] * matrix[13] - matrix[12] * matrix[1] * matrix[11] + matrix[12] * matrix[3] * matrix[9];
        inverse[13] = matrix[0] * matrix[9] * matrix[14] - matrix[0] * matrix[10] * matrix[13] - matrix[8] * matrix[1] * matrix[14] + matrix[8] * matrix[2] * matrix[13] + matrix[12] * matrix[1] * matrix[10] - matrix[12] * matrix[2] * matrix[9];
        inverse[2]  = matrix[1] * matrix[6] * matrix[15] - matrix[1] * matrix[7] * matrix[14] - matrix[5] * matrix[2] * matrix[15] + matrix[5] * matrix[3] * matrix[14] + matrix[13] * matrix[2] * matrix[7] - matrix[13] * matrix[3] * matrix[6];
        inverse[6]  = -matrix[0] * matrix[6] * matrix[15] + matrix[0] * matrix[7] * matrix[14] + matrix[4] * matrix[2] * matrix[15] - matrix[4] * matrix[3] * matrix[14] - matrix[12] * matrix[2] * matrix[7] + matrix[12] * matrix[3] * matrix[6];
        inverse[10] = matrix[0] * matrix[5] * matrix[15] - matrix[0] * matrix[7] * matrix[13] - matrix[4] * matrix[1] * matrix[15] + matrix[4] * matrix[3] * matrix[13] + matrix[12] * matrix[1] * matrix[7] - matrix[12] * matrix[3] * matrix[5];
        inverse[14] = -matrix[0] * matrix[5] * matrix[14] + matrix[0] * matrix[6] * matrix[13] + matrix[4] * matrix[1] * matrix[14] - matrix[4] * matrix[2] * matrix[13] - matrix[12] * matrix[1] * matrix[6] + matrix[12] * matrix[2] * matrix[5];
        inverse[3]  = -matrix[1] * matrix[6] * matrix[11] + matrix[1] * matrix[7] * matrix[10] + matrix[5] * matrix[2] * matrix[11] - matrix[5] * matrix[3] * matrix[10] - matrix[9] * matrix[2] * matrix[7] + matrix[9] * matrix[3] * matrix[6];
        inverse[7]  = matrix[0] * matrix[6] * matrix[11] - matrix[0] * matrix[7] * matrix[10] - matrix[4] * matrix[2] * matrix[11] + matrix[4] * matrix[3] * matrix[10] + matrix[8] * matrix[2] * matrix[7] - matrix[8] * matrix[3] * matrix[6];
        inverse[11] = -matrix[0] * matrix[5] * matrix[11] + matrix[0] * matrix[7] * matrix[9] + matrix[4] * matrix[1] * matrix[11] - matrix[4] * matrix[3] * matrix[9] - matrix[8] * matrix[1] * matrix[7] + matrix[8] * matrix[3] * matrix[5];
        inverse[15] = matrix[0] * matrix[5] * matrix[10] - matrix[0] * matrix[6] * matrix[9] - matrix[4] * matrix[1] * matrix[10] + matrix[4] * matrix[2] * matrix[9] + matrix[8] * matrix[1] * matrix[6] - matrix[8] * matrix[2] * matrix[5];

        const float determinant = matrix[0] * inverse[0] + matrix[1] * inverse[4] + matrix[2] * inverse[8] + matrix[3] * inverse[12];
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1.0e-20f) throw std::runtime_error("Raster transform matrix is not invertible");
        for (float& value : inverse) value /= determinant;
        validate_matrix_array(inverse);
        return inverse;
    }

    [[nodiscard]] std::array<float, 16> normal_from_local_matrix_array(const std::array<float, 16>& object_from_local) {
        std::array<float, 16> normal_from_local = transpose_matrix_array(inverse_matrix_array(object_from_local));
        normal_from_local[3]                    = 0.0f;
        normal_from_local[7]                    = 0.0f;
        normal_from_local[11]                   = 0.0f;
        normal_from_local[12]                   = 0.0f;
        normal_from_local[13]                   = 0.0f;
        normal_from_local[14]                   = 0.0f;
        normal_from_local[15]                   = 1.0f;
        validate_matrix_array(normal_from_local);
        return normal_from_local;
    }

    [[nodiscard]] std::array<float, 3> transform_point_array(const std::array<float, 16>& matrix, const std::array<float, 3>& point) {
        validate_matrix_array(matrix);
        const float x = matrix[0] * point[0] + matrix[1] * point[1] + matrix[2] * point[2] + matrix[3];
        const float y = matrix[4] * point[0] + matrix[5] * point[1] + matrix[6] * point[2] + matrix[7];
        const float z = matrix[8] * point[0] + matrix[9] * point[1] + matrix[10] * point[2] + matrix[11];
        const float w = matrix[12] * point[0] + matrix[13] * point[1] + matrix[14] * point[2] + matrix[15];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w) || std::abs(w) <= 1.0e-20f) throw std::runtime_error("Raster transformed point is invalid");
        return {x / w, y / w, z / w};
    }

    [[nodiscard]] std::array<float, 3> transform_vector_array(const std::array<float, 16>& matrix, const std::array<float, 3>& vector) {
        validate_matrix_array(matrix);
        const float x = matrix[0] * vector[0] + matrix[1] * vector[1] + matrix[2] * vector[2];
        const float y = matrix[4] * vector[0] + matrix[5] * vector[1] + matrix[6] * vector[2];
        const float z = matrix[8] * vector[0] + matrix[9] * vector[1] + matrix[10] * vector[2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) throw std::runtime_error("Camera transformed vector is invalid");
        return {x, y, z};
    }

    [[nodiscard]] float vector3_dot(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
        return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
    }

    [[nodiscard]] std::array<float, 3> vector3_cross(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
        return {
            lhs[1] * rhs[2] - lhs[2] * rhs[1],
            lhs[2] * rhs[0] - lhs[0] * rhs[2],
            lhs[0] * rhs[1] - lhs[1] * rhs[0],
        };
    }

    [[nodiscard]] std::array<float, 3> vector3_add(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
        return {lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2]};
    }

    [[nodiscard]] std::array<float, 3> vector3_subtract(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs) {
        return {lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2]};
    }

    [[nodiscard]] std::array<float, 3> vector3_scale(const std::array<float, 3>& vector, const float scale) {
        return {vector[0] * scale, vector[1] * scale, vector[2] * scale};
    }

    [[nodiscard]] float vector3_length(const std::array<float, 3>& vector) {
        const float length_squared = vector3_dot(vector, vector);
        if (!std::isfinite(length_squared) || length_squared < 0.0f) throw std::runtime_error("Camera vector length is invalid");
        return std::sqrt(length_squared);
    }

    [[nodiscard]] std::array<float, 3> vector3_normalize(const std::array<float, 3>& vector, const char* error_message) {
        const float length = vector3_length(vector);
        if (!(length > 1.0e-20f)) throw std::runtime_error(error_message);
        return vector3_scale(vector, 1.0f / length);
    }

    [[nodiscard]] std::array<float, 3> camera_effective_up(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up) {
        const std::array<float, 3> view_direction = vector3_subtract(center, eye);
        if (vector3_length(vector3_cross(view_direction, up)) > 1.0e-10f) return up;
        return std::abs(up[1]) < 0.9f ? std::array<float, 3>{0.0f, 1.0f, 0.0f} : std::array<float, 3>{1.0f, 0.0f, 0.0f};
    }

    struct SpectraCameraFrame {
        std::array<float, 3> forward{};
        std::array<float, 3> right{};
        std::array<float, 3> up{};
    };

    struct SpectraCameraPose {
        std::array<float, 3> eye{};
        std::array<float, 3> center{};
        std::array<float, 3> up{};
        float basis_handedness{1.0f};
    };

    [[nodiscard]] SpectraCameraFrame camera_frame_from_pose(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up, const float basis_handedness) {
        if (basis_handedness != -1.0f && basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        SpectraCameraFrame frame{};
        frame.forward = vector3_normalize(vector3_subtract(center, eye), "Camera eye and center must not overlap");
        const std::array<float, 3> effective_up = camera_effective_up(eye, center, up);
        const std::array<float, 3> positive_right = vector3_normalize(vector3_cross(effective_up, frame.forward), "Camera right vector is invalid");
        frame.right                               = vector3_scale(positive_right, basis_handedness);
        frame.up                                  = vector3_cross(frame.forward, positive_right);
        return frame;
    }

    [[nodiscard]] std::array<float, 16> camera_from_world_matrix_from_pose(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up, const float basis_handedness) {
        const SpectraCameraFrame frame = camera_frame_from_pose(eye, center, up, basis_handedness);
        std::array<float, 16> matrix{
            frame.right[0], frame.right[1], frame.right[2], -vector3_dot(frame.right, eye),
            frame.up[0], frame.up[1], frame.up[2], -vector3_dot(frame.up, eye),
            frame.forward[0], frame.forward[1], frame.forward[2], -vector3_dot(frame.forward, eye),
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] std::array<float, 3> camera_focus_center_from_bounds(const std::array<float, 3>& eye, const std::array<float, 3>& forward, const std::array<float, 6>& focus_bounds) {
        std::array<float, 3> bounds_min{focus_bounds[0], focus_bounds[1], focus_bounds[2]};
        std::array<float, 3> bounds_max{focus_bounds[3], focus_bounds[4], focus_bounds[5]};
        for (const float value : focus_bounds) {
            if (!std::isfinite(value)) throw std::runtime_error("Camera focus bounds contain a non-finite value");
        }
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (bounds_min[axis] > bounds_max[axis]) throw std::runtime_error("Camera focus bounds are invalid");
        }

        const std::array<float, 3> bounds_center{
            (bounds_min[0] + bounds_max[0]) * 0.5f,
            (bounds_min[1] + bounds_max[1]) * 0.5f,
            (bounds_min[2] + bounds_max[2]) * 0.5f,
        };
        float focus_distance = vector3_dot(vector3_subtract(bounds_center, eye), forward);

        constexpr float parallel_epsilon = 1.0e-7f;
        constexpr float distance_epsilon = 1.0e-5f;
        float ray_min = 0.0f;
        float ray_max = std::numeric_limits<float>::infinity();
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (std::abs(forward[axis]) <= parallel_epsilon) {
                if (eye[axis] < bounds_min[axis] || eye[axis] > bounds_max[axis]) throw std::runtime_error("Camera focus bounds do not intersect the initial view ray");
            } else {
                float t0 = (bounds_min[axis] - eye[axis]) / forward[axis];
                float t1 = (bounds_max[axis] - eye[axis]) / forward[axis];
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
        return vector3_add(eye, vector3_scale(forward, focus_distance));
    }

    [[nodiscard]] SpectraCameraPose camera_pose_from_base_matrix(const std::array<float, 16>& camera_from_world, const std::array<float, 6>& focus_bounds) {
        const std::array<float, 16> world_from_camera = inverse_matrix_array(camera_from_world);
        SpectraCameraPose pose{};
        pose.eye                    = transform_point_array(world_from_camera, {0.0f, 0.0f, 0.0f});
        const std::array<float, 3> right   = vector3_normalize(transform_vector_array(world_from_camera, {1.0f, 0.0f, 0.0f}), "Base camera right vector is invalid");
        const std::array<float, 3> forward = vector3_normalize(transform_vector_array(world_from_camera, {0.0f, 0.0f, 1.0f}), "Base camera forward vector is invalid");
        pose.up                            = vector3_normalize(transform_vector_array(world_from_camera, {0.0f, 1.0f, 0.0f}), "Base camera up vector is invalid");
        const std::array<float, 3> positive_right = vector3_normalize(vector3_cross(camera_effective_up(pose.eye, vector3_add(pose.eye, forward), pose.up), forward), "Base camera positive right vector is invalid");
        pose.basis_handedness              = vector3_dot(right, positive_right) < 0.0f ? -1.0f : 1.0f;
        pose.center                        = camera_focus_center_from_bounds(pose.eye, forward, focus_bounds);
        return pose;
    }

    [[nodiscard]] std::array<float, 2> camera_view_dimensions(const std::array<float, 3>& eye, const std::array<float, 3>& center, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (!std::isfinite(fov_degrees) || !(fov_degrees > 0.0f) || !(fov_degrees < 180.0f)) throw std::runtime_error("Camera fov must be finite and inside (0, 180)");
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        constexpr float radians_per_degree = 0.017453292519943295769f;
        const float distance               = vector3_length(vector3_subtract(eye, center));
        const float half_height            = distance * std::tan(fov_degrees * radians_per_degree * 0.5f);
        const float height                 = half_height * 2.0f;
        const float width                  = height * std::max(viewport_size[0] / viewport_size[1], 0.001f);
        if (!std::isfinite(width) || !std::isfinite(height) || !(width > 0.0f) || !(height > 0.0f)) throw std::runtime_error("Camera view dimensions are invalid");
        return {width, height};
    }

    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        const SpectraCameraFrame frame        = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 2> view_size  = camera_view_dimensions(pose.eye, pose.center, fov_degrees, viewport_size);
        const std::array<float, 3> horizontal = vector3_scale(frame.right, -displacement[0] * view_size[0]);
        const std::array<float, 3> vertical   = vector3_scale(frame.up, displacement[1] * view_size[1]);
        const std::array<float, 3> offset     = vector3_add(horizontal, vertical);
        pose.eye                              = vector3_add(pose.eye, offset);
        pose.center                           = vector3_add(pose.center, offset);
        return true;
    }

    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        const float larger_displacement = std::abs(displacement[0]) > std::abs(displacement[1]) ? displacement[0] : -displacement[1];
        if (larger_displacement == 0.0f) return false;
        if (larger_displacement >= 0.99f) return false;
        const std::array<float, 3> direction = vector3_subtract(pose.center, pose.eye);
        if (!(vector3_length(direction) > 1.0e-6f)) return false;
        pose.eye = vector3_add(pose.eye, vector3_scale(direction, larger_displacement));
        return true;
    }

    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        if (pose.basis_handedness != -1.0f && pose.basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        constexpr float two_pi   = 6.2831853071795864769f;
        constexpr float pole_pad = 1.0e-3f;
        displacement[0] *= -pose.basis_handedness;
        displacement[0] *= two_pi;
        displacement[1] *= two_pi;

        const std::array<float, 3> origin   = invert ? pose.eye : pose.center;
        const std::array<float, 3> position = invert ? pose.center : pose.eye;
        std::array<float, 3> center_to_eye  = vector3_subtract(position, origin);
        const float radius                  = vector3_length(center_to_eye);
        if (!(radius > 1.0e-6f)) return false;
        center_to_eye = vector3_scale(center_to_eye, 1.0f / radius);

        const std::array<float, 3> normalized_up = vector3_normalize(pose.up, "Camera up vector is invalid");
        const float cos_elevation                = vector3_dot(center_to_eye, normalized_up);
        std::array<float, 3> horizontal          = vector3_subtract(center_to_eye, vector3_scale(normalized_up, cos_elevation));
        const float sin_elevation                = vector3_length(horizontal);
        const float elevation                    = std::atan2(sin_elevation, cos_elevation);
        if (sin_elevation < 1.0e-6f) {
            const std::array<float, 3> reference = std::abs(normalized_up[0]) < 0.9f ? std::array<float, 3>{1.0f, 0.0f, 0.0f} : std::array<float, 3>{0.0f, 0.0f, 1.0f};
            horizontal                           = vector3_normalize(vector3_subtract(reference, vector3_scale(normalized_up, vector3_dot(reference, normalized_up))), "Camera orbit horizontal vector is invalid");
        } else {
            horizontal = vector3_scale(horizontal, 1.0f / sin_elevation);
        }

        const float yaw_cos                    = std::cos(-displacement[0]);
        const float yaw_sin                    = std::sin(-displacement[0]);
        horizontal                             = vector3_add(vector3_scale(horizontal, yaw_cos), vector3_scale(vector3_cross(normalized_up, horizontal), yaw_sin));
        const float new_elevation              = std::clamp(elevation - displacement[1], pole_pad, 3.14159265358979323846f - pole_pad);
        const std::array<float, 3> new_offset   = vector3_scale(vector3_add(vector3_scale(normalized_up, std::cos(new_elevation)), vector3_scale(horizontal, std::sin(new_elevation))), radius);
        const std::array<float, 3> new_position = vector3_add(new_offset, origin);
        if (invert) pose.center = new_position;
        else pose.eye = new_position;
        return true;
    }

    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        if (delta[0] == 0.0f && delta[1] == 0.0f) return false;
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        const SpectraCameraFrame frame = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 3> movement = dolly
            ? vector3_scale(frame.forward, delta[0] * speed)
            : vector3_add(vector3_scale(frame.right, delta[0] * speed), vector3_scale(frame.up, delta[1] * speed));
        pose.eye    = vector3_add(pose.eye, movement);
        pose.center = vector3_add(pose.center, movement);
        return true;
    }

    [[nodiscard]] std::array<float, 16> moving_from_camera_from_pose(const std::array<float, 16>& base_camera_from_world, const SpectraCameraPose& pose) {
        const std::array<float, 16> current_camera_from_world = camera_from_world_matrix_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 16> current_world_from_camera = inverse_matrix_array(current_camera_from_world);
        return multiply_matrix_arrays(base_camera_from_world, current_world_from_camera);
    }

    [[nodiscard]] std::array<float, 16> raster_perspective_matrix(const float fov_degrees, const float aspect) {
        if (!std::isfinite(fov_degrees) || !(fov_degrees > 0.0f) || !(fov_degrees < 180.0f)) throw std::runtime_error("Raster perspective fov must be finite and inside (0, 180)");
        if (!std::isfinite(aspect) || !(aspect > 0.0f)) throw std::runtime_error("Raster perspective aspect ratio must be finite and positive");
        constexpr float radians_per_degree = 0.017453292519943295769f;
        constexpr float near_plane         = 0.01f;
        constexpr float far_plane          = 1000000.0f;
        const float focal                  = 1.0f / std::tan(fov_degrees * radians_per_degree * 0.5f);
        std::array<float, 16> matrix{};
        matrix[0]  = focal / aspect;
        matrix[5]  = -focal;
        matrix[10] = far_plane / (far_plane - near_plane);
        matrix[11] = -(far_plane * near_plane) / (far_plane - near_plane);
        matrix[14] = 1.0f;
        validate_matrix_array(matrix);
        return matrix;
    }

    [[nodiscard]] float raster_camera_fov_degrees(const xayah::SpectraScene& scene) {
        if (!scene.camera.present) throw std::runtime_error("Rasterizer requires an explicit PBRT perspective camera");
        if (scene.camera.name != "perspective") throw std::runtime_error(std::format("Rasterizer v1 requires a PBRT perspective camera, not \"{}\"", scene.camera.name));
        constexpr float pbrt_perspective_default_fov = 90.0f;
        for (const xayah::SpectraPbrtParameter& parameter : scene.camera.parameters) {
            if (parameter.name != "fov") continue;
            if (parameter.floats.size() != 1) throw std::runtime_error("PBRT perspective camera fov must have exactly one float value");
            return parameter.floats.front();
        }
        return pbrt_perspective_default_fov;
    }

    [[nodiscard]] std::array<float, 16> raster_view_projection_matrix(const xayah::SpectraScene& scene, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) {
        if (scene.film_resolution[0] <= 0 || scene.film_resolution[1] <= 0) throw std::runtime_error("Rasterizer requires positive PBRT film resolution metadata");
        const float aspect = static_cast<float>(scene.film_resolution[0]) / static_cast<float>(scene.film_resolution[1]);
        const std::array<float, 16> projection = raster_perspective_matrix(raster_camera_fov_degrees(scene), aspect);
        const std::array<float, 16> current_camera_from_world = multiply_matrix_arrays(inverse_matrix_array(moving_from_camera), camera_from_world);
        return multiply_matrix_arrays(projection, current_camera_from_world);
    }

    [[nodiscard]] ImVec4 imgui_srgb(const float red, const float green, const float blue, const float alpha) {
        return ImVec4{red, green, blue, alpha};
    }

    void load_imgui_fonts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts == nullptr) throw std::runtime_error("ImGui font atlas is unavailable");

        ImFontConfig font_config{};
        font_config.OversampleH   = 3;
        font_config.OversampleV   = 3;
        constexpr float font_size = 15.0f;
        ImFont* default_font      = io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_regular_compressed_data, g_roboto_regular_compressed_size, font_size, &font_config);
        if (default_font == nullptr) throw std::runtime_error("Failed to load Roboto regular font");

        ImFontConfig icon_config{};
        icon_config.MergeMode     = true;
        icon_config.PixelSnapH    = true;
        icon_config.OversampleH   = 3;
        icon_config.OversampleV   = 3;
        constexpr float icon_size = 1.28571429f * font_size;
        icon_config.GlyphOffset.x = icon_size * 0.01f;
        icon_config.GlyphOffset.y = icon_size * 0.2f;
        constexpr std::array<ImWchar, 3> icon_ranges{ICON_MIN_MS, ICON_MAX_MS, 0};
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_materialSymbolsRounded_compressed_data, g_materialSymbolsRounded_compressed_size, icon_size, &icon_config, icon_ranges.data()) == nullptr) throw std::runtime_error("Failed to load Material Symbols icon font");

        ImFontConfig mono_config{};
        mono_config.OversampleH = 3;
        mono_config.OversampleV = 3;
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_mono_compressed_data, g_roboto_mono_compressed_size, font_size, &mono_config) == nullptr) throw std::runtime_error("Failed to load Roboto mono font");
        io.FontDefault = default_font;
    }

    void apply_imgui_style(const bool viewports) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style                  = ImGui::GetStyle();
        style.WindowRounding               = 0.0f;
        style.WindowBorderSize             = 0.0f;
        style.ColorButtonPosition          = ImGuiDir_Right;
        style.FrameRounding                = 2.0f;
        style.FrameBorderSize              = 1.0f;
        style.GrabRounding                 = 4.0f;
        style.IndentSpacing                = 12.0f;
        style.Colors[ImGuiCol_WindowBg]    = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_MenuBarBg]   = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_ScrollbarBg] = imgui_srgb(0.2f, 0.2f, 0.2f, 1.0f);
        style.Colors[ImGuiCol_PopupBg]     = imgui_srgb(0.135f, 0.135f, 0.135f, 1.0f);
        style.Colors[ImGuiCol_Border]      = imgui_srgb(0.4f, 0.4f, 0.4f, 0.5f);
        style.Colors[ImGuiCol_FrameBg]     = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);

        const ImVec4 normal_color = imgui_srgb(0.465f, 0.465f, 0.525f, 1.0f);
        constexpr std::array normal_colors{
            ImGuiCol_Header,
            ImGuiCol_SliderGrab,
            ImGuiCol_Button,
            ImGuiCol_CheckMark,
            ImGuiCol_ResizeGrip,
            ImGuiCol_TextSelectedBg,
            ImGuiCol_Separator,
            ImGuiCol_FrameBgActive,
        };
        for (const ImGuiCol color_id : normal_colors) style.Colors[color_id] = normal_color;

        const ImVec4 active_color = imgui_srgb(0.365f, 0.365f, 0.425f, 1.0f);
        constexpr std::array active_colors{
            ImGuiCol_HeaderActive,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_ButtonActive,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SeparatorActive,
        };
        for (const ImGuiCol color_id : active_colors) style.Colors[color_id] = active_color;

        const ImVec4 hovered_color = imgui_srgb(0.565f, 0.565f, 0.625f, 1.0f);
        constexpr std::array hovered_colors{
            ImGuiCol_HeaderHovered,
            ImGuiCol_ButtonHovered,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_SeparatorHovered,
        };
        for (const ImGuiCol color_id : hovered_colors) style.Colors[color_id] = hovered_color;

        style.Colors[ImGuiCol_TitleBgActive]    = imgui_srgb(0.465f, 0.465f, 0.465f, 1.0f);
        style.Colors[ImGuiCol_TitleBg]          = imgui_srgb(0.125f, 0.125f, 0.125f, 1.0f);
        style.Colors[ImGuiCol_Tab]              = imgui_srgb(0.05f, 0.05f, 0.05f, 0.5f);
        style.Colors[ImGuiCol_TabHovered]       = imgui_srgb(0.465f, 0.495f, 0.525f, 1.0f);
        style.Colors[ImGuiCol_TabActive]        = imgui_srgb(0.282f, 0.290f, 0.302f, 1.0f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = imgui_srgb(0.465f, 0.465f, 0.465f, 0.350f);
        style.Colors[ImGuiCol_ButtonActive]     = static_cast<ImVec4>(ImColor::HSV(0.3F, 0.5F, 0.5F));
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        if (viewports) {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }
    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};
        this->window_title.base = app_name_string;

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> enabled_device_extensions{vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName};
#if defined(_WIN32)
        enabled_device_extensions.push_back(vk::KHRExternalMemoryWin32ExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreWin32ExtensionName);
#else
        enabled_device_extensions.push_back(vk::KHRExternalMemoryFdExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreFdExtensionName);
#endif
        std::vector<const char*> enabled_instance_extensions{};

        {
            std::uint32_t glfw_extension_count = 0;
            const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");
            enabled_instance_extensions = {glfw_extensions, glfw_extensions + glfw_extension_count};
            enabled_instance_extensions.push_back(vk::EXTDebugUtilsExtensionName);

            const std::vector<vk::LayerProperties> available_layers = this->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : enabled_instance_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : enabled_instance_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

            const vk::ApplicationInfo application_info{app_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), engine_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }
        {
            constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
                {},
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                &debug_callback,
            };
            this->context.debug_messenger = this->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
        }
        {
            if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            this->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
            if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
            glfwSetWindowUserPointer(this->surface.window.get(), this);
            glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
        }
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
            this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
        }
        {
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
                if (physical_device.getProperties().apiVersion < VK_API_VERSION_1_4) continue;

                const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
                bool required_extensions_available                              = true;
                for (const char* required_extension : enabled_device_extensions) {
                    if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) required_extensions_available = false;
                }
                if (!required_extensions_available) continue;

                const std::vector<vk::QueueFamilyProperties> queue_families = physical_device.getQueueFamilyProperties();
                for (std::uint32_t queue_family_index = 0; queue_family_index < queue_families.size(); ++queue_family_index) {
                    if (!static_cast<bool>(queue_families[queue_family_index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                    if (!physical_device.getSurfaceSupportKHR(queue_family_index, this->surface.surface)) continue;
                    this->context.physical_device      = physical_device;
                    this->context.graphics_queue_index = queue_family_index;
                    selected                           = true;
                    break;
                }
                if (selected) break;
            }
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
        }
        {
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
            this->context.device         = vk::raii::Device{this->context.physical_device, device_create_info};
            this->context.graphics_queue = vk::raii::Queue{this->context.device, this->context.graphics_queue_index, 0};
        }
        {
            const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->context.graphics_queue_index};
            this->context.command_pool = vk::raii::CommandPool{this->context.device, command_pool_create_info};
        }
        this->create_swapchain();
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
            const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->context.command_pool, vk::CommandBufferLevel::ePrimary, this->sync.frame_count};
            this->sync.command_buffers = vk::raii::CommandBuffers{this->context.device, command_buffer_allocate_info};
            if (this->sync.command_buffers.size() != this->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

            this->sync.image_available_semaphores.reserve(this->sync.frame_count);
            this->sync.in_flight_fences.reserve(this->sync.frame_count);
            for (std::uint32_t frame_index = 0; frame_index < this->sync.frame_count; ++frame_index) {
                this->sync.image_available_semaphores.emplace_back(this->context.device, semaphore_create_info);
                this->sync.in_flight_fences.emplace_back(this->context.device, fence_create_info);
            }
        }
        this->create_imgui();
        this->initialize_pbrt_runtime();

        const auto properties_chain = this->context.physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        const vk::PhysicalDeviceProperties& properties = properties_chain.get<vk::PhysicalDeviceProperties2>().properties;
        const vk::PhysicalDeviceDriverProperties& driver = properties_chain.get<vk::PhysicalDeviceDriverProperties>();
        std::println("Spectra Vulkan device: {} | Vulkan {}.{}.{} | Driver {} ({})", properties.deviceName.data(), vk::apiVersionMajor(properties.apiVersion), vk::apiVersionMinor(properties.apiVersion), vk::apiVersionPatch(properties.apiVersion), driver.driverName.data(), vk::to_string(driver.driverID));
        std::println("Spectra swapchain: {} {}x{} images {} present {}", vk::to_string(this->swapchain.format), this->swapchain.extent.width, this->swapchain.extent.height, this->swapchain.images.size(), vk::to_string(this->swapchain.present_mode));
    } catch (...) {
        this->destroy_imgui();
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }

        this->unload_renderer_sessions_noexcept();
        this->unload_raster_scene_noexcept();
        this->unload_spectra_scene_noexcept();
        this->destroy_imgui();
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
        this->context.command_pool = nullptr;
        this->swapchain.image_views.clear();
        this->swapchain.handle = nullptr;
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->context.graphics_queue  = nullptr;
        this->context.device          = nullptr;
        this->surface.surface         = nullptr;
        this->surface.window          = nullptr;
        this->context.physical_device = nullptr;
        this->context.debug_messenger = nullptr;
        this->context.instance        = nullptr;
        if (this->surface.glfw_initialized) glfwTerminate();
        this->surface.glfw_initialized = false;
    }

    void Spectra::create_imgui() {
        if (this->imgui.initialized) throw std::runtime_error("ImGui is already initialized");
        if (this->surface.window.get() == nullptr) throw std::runtime_error("Cannot initialize ImGui without a GLFW window");
        if (this->swapchain.images.empty()) throw std::runtime_error("Cannot initialize ImGui without swapchain images");

        bool context_created            = false;
        bool glfw_backend_initialized   = false;
        bool vulkan_backend_initialized = false;
        try {
            constexpr std::array descriptor_pool_sizes{
                vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
                vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000},
            };
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()), static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->imgui.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};
            this->imgui.color_format    = this->swapchain.format;
            this->imgui.min_image_count = std::max(2u, this->sync.frame_count);
            this->imgui.image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (this->imgui.image_count < this->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created = true;

            ImGuiIO& io = ImGui::GetIO();
            if (this->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            if (this->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            load_imgui_fonts();
            apply_imgui_style(this->imgui.viewports);

            if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
            glfw_backend_initialized = true;

            auto color_attachment_format = static_cast<VkFormat>(this->imgui.color_format);
            VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
            pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = VK_API_VERSION_1_4;
            init_info.Instance                                     = static_cast<VkInstance>(*this->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->context.device);
            init_info.QueueFamily                                  = this->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->context.graphics_queue);
            init_info.DescriptorPool                               = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
            init_info.MinImageCount                                = this->imgui.min_image_count;
            init_info.ImageCount                                   = this->imgui.image_count;
            init_info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering                          = true;
            init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
            if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
            vulkan_backend_initialized = true;
            this->imgui.initialized    = true;
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->imgui.descriptor_pool = nullptr;
            this->imgui.color_format    = vk::Format::eUndefined;
            this->imgui.min_image_count = 2;
            this->imgui.image_count     = 2;
            this->imgui.initialized     = false;
            throw;
        }
    }

    void Spectra::destroy_imgui() noexcept {
        if (this->vulkan_rasterizer != nullptr) this->vulkan_rasterizer->release_imgui_descriptors();
        if (this->pbrt_interactive != nullptr) this->pbrt_interactive->release_imgui_descriptors();
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool = nullptr;
        this->imgui.color_format    = vk::Format::eUndefined;
        this->imgui.min_image_count = 2;
        this->imgui.image_count     = 2;
        this->imgui.initialized     = false;
        this->ui.dock_layout_initialized = false;
    }

    void Spectra::load_spectra_scene(const std::filesystem::path& scene_path) {
        if (this->spectra_scene != nullptr) throw std::runtime_error("Spectra scene is already loaded");
        std::unique_ptr<SpectraScene> loaded_scene = std::make_unique<SpectraScene>();
        try {
            loaded_scene->load(scene_path);
            this->spectra_scene = std::move(loaded_scene);
        } catch (...) {
            loaded_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_spectra_scene_noexcept() noexcept {
        if (this->spectra_scene != nullptr) {
            this->spectra_scene->unload_noexcept();
            this->spectra_scene.reset();
        }
    }

    void Spectra::load_pbrt_backend_scene(const std::array<int, 2>& resolution) {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot load PBRT backend scene without a loaded Spectra scene");
        if (this->pbrt_backend_scene != nullptr) throw std::runtime_error("PBRT backend scene is already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot load PBRT backend scene with a non-positive resolution");
        std::unique_ptr<SpectraPbrtBackendScene> loaded_backend_scene = std::make_unique<SpectraPbrtBackendScene>();
        try {
            this->reset_pbrt_runtime_options_for_scene();
            loaded_backend_scene->load(*this->spectra_scene, resolution);
            this->pbrt_backend_scene = std::move(loaded_backend_scene);
        } catch (...) {
            this->wait_pbrt_gpu_noexcept();
            loaded_backend_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_pbrt_backend_scene_noexcept() noexcept {
        if (this->pbrt_backend_scene != nullptr) {
            this->wait_pbrt_gpu_noexcept();
            this->pbrt_backend_scene->unload_noexcept();
            this->pbrt_backend_scene.reset();
        }
    }

    void Spectra::load_raster_scene() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot build raster scene without a loaded Spectra scene");
        if (this->raster_scene != nullptr) throw std::runtime_error("Spectra raster scene is already loaded");
        std::unique_ptr<SpectraRasterScene> loaded_raster_scene = std::make_unique<SpectraRasterScene>();
        try {
            loaded_raster_scene->build(*this->spectra_scene);
            this->raster_scene = std::move(loaded_raster_scene);
        } catch (...) {
            loaded_raster_scene->unload_noexcept();
            throw;
        }
    }

    void Spectra::unload_raster_scene_noexcept() noexcept {
        if (this->raster_scene != nullptr) {
            this->raster_scene->unload_noexcept();
            this->raster_scene.reset();
        }
    }

    void Spectra::load_vulkan_rasterizer() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer without a loaded Spectra scene");
        if (this->raster_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer without a built Spectra raster scene");
        if (this->vulkan_rasterizer != nullptr) throw std::runtime_error("Vulkan rasterizer is already loaded");
        this->vulkan_rasterizer = std::make_unique<SpectraVulkanRasterizer>(*this->spectra_scene, *this->raster_scene, this->context.physical_device, this->context.device, this->context.graphics_queue, this->context.command_pool, this->sync.frame_count);
    }

    void Spectra::unload_vulkan_rasterizer_noexcept() noexcept {
        this->vulkan_rasterizer.reset();
    }

    void Spectra::create_renderers_for_resolution(const std::array<int, 2>& resolution) {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot create renderer sessions without a loaded Spectra scene");
        if (this->raster_scene == nullptr) throw std::runtime_error("Cannot create renderer sessions without a built Spectra raster scene");
        if (this->pbrt_backend_scene != nullptr || this->pbrt_interactive != nullptr || this->vulkan_rasterizer != nullptr) throw std::runtime_error("Renderer sessions are already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create renderer sessions with a non-positive resolution");
        try {
            this->load_pbrt_backend_scene(resolution);
            if (this->pbrt_backend_scene == nullptr) throw std::runtime_error("PBRT backend scene was not loaded");
            this->pbrt_interactive = std::make_unique<SpectraPbrtInteractiveSession>(*this->spectra_scene, this->pbrt_backend_scene->basic_scene(), this->context.physical_device, this->context.device, this->sync.frame_count);
            this->spectra_scene->set_runtime_metadata(this->pbrt_interactive->film_resolution(), this->pbrt_interactive->sampler_sample_count(), this->pbrt_interactive->camera_from_world_matrix());
            this->load_vulkan_rasterizer();
            this->render_resolution_sync.active_resolution = resolution;
            this->render_resolution_sync.renderer_created  = true;
        } catch (...) {
            this->unload_renderer_sessions_noexcept();
            throw;
        }
    }

    void Spectra::rebuild_renderers_for_resolution(const std::array<int, 2>& resolution) {
        if (this->render_resolution_sync.rebuilding) throw std::runtime_error("Renderer resolution rebuild is already active");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot rebuild renderer sessions with a non-positive resolution");
        if (this->render_resolution_sync.renderer_created && this->render_resolution_sync.active_resolution == resolution) return;

        const bool preserve_camera = this->camera.initialized;
        const SpectraCameraPose preserved_pose{this->camera.eye, this->camera.center, this->camera.up, this->camera.basis_handedness};
        const float preserved_speed     = this->camera.speed;
        const int preserved_samples     = this->pbrt_interactive == nullptr ? 0 : this->pbrt_interactive->target_sample_count();
        const float preserved_exposure  = this->pbrt_interactive == nullptr ? 1.0f : this->pbrt_interactive->current_exposure();
        this->render_resolution_sync.rebuilding = true;
        try {
            this->context.device.waitIdle();
            this->wait_pbrt_gpu_noexcept();
            this->unload_renderer_sessions_noexcept();
            this->create_renderers_for_resolution(resolution);
            if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT interactive session was not created");
            if (preserved_samples > 0) this->pbrt_interactive->set_target_sample_count(preserved_samples);
            this->pbrt_interactive->set_exposure(preserved_exposure);
            if (preserve_camera) {
                this->camera.camera_from_world          = this->spectra_scene->camera_from_world;
                this->camera.eye                        = preserved_pose.eye;
                this->camera.center                     = preserved_pose.center;
                this->camera.up                         = preserved_pose.up;
                this->camera.basis_handedness           = preserved_pose.basis_handedness;
                this->camera.speed                      = preserved_speed;
                this->camera.fov_degrees                = raster_camera_fov_degrees(*this->spectra_scene);
                this->camera.mouse_position_known       = false;
                this->camera.input_enabled              = false;
                this->camera.moving_from_camera         = moving_from_camera_from_pose(this->camera.camera_from_world, preserved_pose);
                this->camera.pathtracer_accumulation_dirty = false;
            } else
                this->initialize_camera_state();
            this->clear_pathtracer_throughput_statistics();
            this->statistics.last_frame_rendered_sample = false;
            this->render_resolution_sync.rebuilding     = false;
        } catch (...) {
            this->render_resolution_sync.rebuilding = false;
            throw;
        }
    }

    void Spectra::unload_renderer_sessions_noexcept() noexcept {
        this->unload_vulkan_rasterizer_noexcept();
        this->pbrt_interactive.reset();
        this->unload_pbrt_backend_scene_noexcept();
        this->render_resolution_sync.renderer_created  = false;
        this->render_resolution_sync.active_resolution = {0, 0};
    }

    void Spectra::observe_viewport_render_resolution(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid while tracking viewport resolution");
        if (!this->render_resolution_sync.candidate_known || this->render_resolution_sync.candidate_resolution != resolution) {
            this->render_resolution_sync.candidate_known     = true;
            this->render_resolution_sync.candidate_resolution = resolution;
            this->render_resolution_sync.stable_seconds      = 0.0f;
            return;
        }
        this->render_resolution_sync.stable_seconds += io.DeltaTime;
    }

    void Spectra::synchronize_render_resolution() {
        constexpr float resolution_stability_seconds = 0.3f;
        if (this->spectra_scene == nullptr || this->raster_scene == nullptr) return;
        if (!this->render_resolution_sync.candidate_known) return;
        if (this->render_resolution_sync.stable_seconds < resolution_stability_seconds) return;
        if (this->render_resolution_sync.renderer_created && this->render_resolution_sync.active_resolution == this->render_resolution_sync.candidate_resolution) return;
        this->rebuild_renderers_for_resolution(this->render_resolution_sync.candidate_resolution);
    }

    [[nodiscard]] bool Spectra::renderers_ready() const {
        return this->render_resolution_sync.renderer_created && this->pbrt_backend_scene != nullptr && this->pbrt_interactive != nullptr && this->vulkan_rasterizer != nullptr;
    }

    void Spectra::initialize_pbrt_runtime() {
        if (this->pbrt_runtime != nullptr || pbrt::Options != nullptr) throw std::runtime_error("PBRT runtime is already initialized");
        std::unique_ptr<SpectraPbrtRuntimeState> runtime = std::make_unique<SpectraPbrtRuntimeState>();
        runtime->baseline_options.useGPU         = true;
        runtime->baseline_options.wavefront      = false;
        runtime->baseline_options.nThreads       = 30;
        runtime->baseline_options.renderingSpace = pbrt::RenderingCoordinateSystem::CameraWorld;
        pbrt::InitPBRT(runtime->baseline_options);
        runtime->initialized = true;
        this->pbrt_runtime   = std::move(runtime);
    }

    void Spectra::reset_pbrt_runtime_options_for_scene() {
        if (this->pbrt_runtime == nullptr || !this->pbrt_runtime->initialized) throw std::runtime_error("PBRT runtime is not initialized");
        if (pbrt::Options == nullptr) throw std::runtime_error("PBRT global options are unavailable");
        *pbrt::Options = this->pbrt_runtime->baseline_options;
#ifdef PBRT_BUILD_GPU_RENDERER
        if (pbrt::Options->useGPU) pbrt::CopyOptionsToGPU();
#endif
    }

    void Spectra::wait_pbrt_gpu_noexcept() const noexcept {
        try {
            if (pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
        } catch (...) {
        }
    }

    void Spectra::run_interactive_scene(const std::filesystem::path& scene_path) {
        if (this->spectra_scene != nullptr) throw std::runtime_error("Spectra scene is already active");
        if (this->raster_scene != nullptr) throw std::runtime_error("Spectra raster scene is already active");
        if (this->pbrt_backend_scene != nullptr) throw std::runtime_error("PBRT backend scene is already active");
        if (this->pbrt_interactive != nullptr) throw std::runtime_error("PBRT interactive session is already active");
        if (this->vulkan_rasterizer != nullptr) throw std::runtime_error("Vulkan rasterizer is already active");
        try {
            this->load_spectra_scene(scene_path);
            this->load_raster_scene();
            this->render_loop();
            this->context.device.waitIdle();
            this->unload_renderer_sessions_noexcept();
            this->unload_raster_scene_noexcept();
            this->unload_spectra_scene_noexcept();
        } catch (...) {
            try {
                if (*this->context.device) this->context.device.waitIdle();
            } catch (...) {
            }
            this->unload_renderer_sessions_noexcept();
            this->unload_raster_scene_noexcept();
            this->unload_spectra_scene_noexcept();
            throw;
        }
    }

    void Spectra::render_loop() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot enter Spectra render loop without an active Spectra scene");
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
        this->context.device.waitIdle();
    }

    void Spectra::update_window_title(const float delta_seconds) {
        if (this->surface.window == nullptr) throw std::runtime_error("Cannot update window title without a GLFW window");

        ++this->window_title.frame_count;
        this->window_title.refresh_timer += delta_seconds;
        if (this->window_title.refresh_timer <= 1.0f) return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.Framerate <= 0.0f) return;

        std::uint32_t width  = this->swapchain.extent.width;
        std::uint32_t height = this->swapchain.extent.height;
        if (this->render_resolution_sync.renderer_created) {
            width  = static_cast<std::uint32_t>(this->render_resolution_sync.active_resolution[0]);
            height = static_cast<std::uint32_t>(this->render_resolution_sync.active_resolution[1]);
        } else if (this->ui.viewport_known && this->ui.viewport_framebuffer_size[0] > 0 && this->ui.viewport_framebuffer_size[1] > 0) {
            width  = static_cast<std::uint32_t>(this->ui.viewport_framebuffer_size[0]);
            height = static_cast<std::uint32_t>(this->ui.viewport_framebuffer_size[1]);
        }

        const std::string scene_label = this->spectra_scene == nullptr ? "No Scene" : this->spectra_scene->scene_label;
        const std::array<int, 2> sample_range = this->spectra_scene == nullptr ? std::array<int, 2>{0, 0} : this->active_renderer_sample_range();
        const std::string title       = std::format("{} - {} | {} | {}x{} | sample {}/{} | {:.0f} FPS / {:.3f}ms | frame {}", this->window_title.base, scene_label, this->active_renderer_label(), width, height, sample_range[0], sample_range[1], io.Framerate, 1000.0f / io.Framerate, this->window_title.frame_count);
        glfwSetWindowTitle(this->surface.window.get(), title.c_str());
        this->window_title.refresh_timer = 0.0f;
    }

    void Spectra::clear_pathtracer_throughput_statistics() {
        this->statistics.throughput_mspp.clear();
        this->statistics.last_valid_throughput_mspp = 0.0f;
        this->statistics.has_throughput             = false;
    }

    void Spectra::update_frame_statistics(const FrameState& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Renderer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Renderer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        this->statistics.current_frame_id             = this->window_title.frame_count + 1;
        this->statistics.active_frame_index           = frame.frame_index;
        this->statistics.active_swapchain_image_index = frame.image_index;
        this->statistics.last_frame_milliseconds      = frame_milliseconds;
        this->statistics.last_frame_rendered_sample   = rendered_sample;
        this->statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->statistics.throughput_mspp.add(throughput);
            this->statistics.last_valid_throughput_mspp = throughput;
            this->statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] const char* Spectra::active_renderer_label() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                return "PBRT Pathtracer";
            case SpectraRenderMode::VulkanRasterizer:
                return "Vulkan Rasterizer";
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] Spectra::ActiveRendererStatus Spectra::active_renderer_status() const {
        ActiveRendererStatus status{};
        status.label                              = this->active_renderer_label();
        status.sample_range                       = this->active_renderer_sample_range();
        status.pathtracer_accumulation_dirty      = this->camera.pathtracer_accumulation_dirty;
        if (this->render_resolution_sync.rebuilding) {
            status.state = "Rebuilding";
            return status;
        }
        if (!this->render_resolution_sync.renderer_created) {
            status.state = this->render_resolution_sync.candidate_known ? "Pending Resolution" : "Waiting for Viewport";
            return status;
        }
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                status.has_accumulation = true;
                status.uses_external_completion = this->pbrt_interactive != nullptr;
                if (this->pbrt_interactive == nullptr) {
                    status.state = "Unavailable";
                    return status;
                }
                if (status.pathtracer_accumulation_dirty) {
                    status.state = "Camera Dirty";
                    return status;
                }
                status.state = status.sample_range[0] >= status.sample_range[1] ? "Completed" : "Sampling";
                return status;
            case SpectraRenderMode::VulkanRasterizer:
                status.has_accumulation = false;
                status.uses_external_completion = false;
                if (this->vulkan_rasterizer == nullptr) {
                    status.state = "Unavailable";
                    return status;
                }
                status.state = this->vulkan_rasterizer->draw_count == 0 ? "Clear Only" : "Rasterizing";
                return status;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] VkDescriptorSet Spectra::active_viewport_descriptor() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT pathtracer viewport descriptor requested without an active PBRT session");
                return this->pbrt_interactive->active_descriptor();
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Vulkan rasterizer viewport descriptor requested without an active rasterizer session");
                return this->vulkan_rasterizer->active_descriptor();
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] std::array<int, 2> Spectra::active_renderer_sample_range() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) return {0, 0};
                return {this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count()};
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) return {0, 0};
                return {1, 1};
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] float Spectra::active_renderer_initial_move_scale() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT camera move scale requested without an active PBRT session");
                return this->pbrt_interactive->camera_initial_move_scale();
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Vulkan rasterizer camera move scale requested without an active rasterizer session");
                return this->vulkan_rasterizer->camera_initial_move_scale();
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] std::array<float, 6> Spectra::active_renderer_initial_focus_bounds() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT camera focus bounds requested without an active PBRT session");
                return this->pbrt_interactive->camera_initial_focus_bounds();
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Vulkan rasterizer camera focus bounds requested without an active rasterizer session");
                return this->vulkan_rasterizer->camera_initial_focus_bounds();
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] bool Spectra::active_renderer_uses_external_completion_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return true;
            case SpectraRenderMode::VulkanRasterizer:
                return false;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] vk::Semaphore Spectra::active_renderer_complete_semaphore() const {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("PBRT completion semaphore requested without an active PBRT session");
                return this->pbrt_interactive->active_cuda_complete_semaphore();
            case SpectraRenderMode::VulkanRasterizer:
                throw std::runtime_error("Vulkan rasterizer does not use an external completion semaphore");
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    [[nodiscard]] Spectra::ActiveRendererFrameResult Spectra::render_active_renderer_frame(const FrameState& frame) {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer: {
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot render PBRT pathtracer without an active PBRT session");
                const SpectraPbrtInteractiveSession::RenderFrameResult render_result = this->pbrt_interactive->render_frame(frame.frame_index, this->camera.moving_from_camera);
                return {render_result.sample_pixels, render_result.rendered_sample, render_result.reset_accumulation};
            }
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot render Vulkan rasterizer without an active rasterizer session");
                this->vulkan_rasterizer->render_frame(frame.frame_index);
                return {};
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::record_renderer_output(const SpectraRenderMode render_mode, const vk::raii::CommandBuffer& command_buffer) {
        switch (render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot record PBRT pathtracer output without an active PBRT session");
                this->pbrt_interactive->record_copy(command_buffer);
                return;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot record Vulkan rasterizer output without an active rasterizer session");
                this->vulkan_rasterizer->record_draw(command_buffer, this->camera.camera_from_world, this->camera.moving_from_camera);
                return;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::reset_active_renderer_accumulation() {
        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                this->request_pathtracer_accumulation_reset();
                return;
            case SpectraRenderMode::VulkanRasterizer:
                return;
        }
        throw std::runtime_error("Unknown Spectra render mode");
    }

    void Spectra::request_pathtracer_accumulation_reset() {
        if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot reset PBRT accumulation without an active PBRT session");
        this->pbrt_interactive->request_reset_accumulation();
        this->camera.pathtracer_accumulation_dirty = false;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::mark_pathtracer_accumulation_dirty() {
        if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot mark PBRT accumulation dirty without an active PBRT session");
        if (this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer) {
            this->request_pathtracer_accumulation_reset();
            return;
        }
        this->camera.pathtracer_accumulation_dirty = true;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::set_active_render_mode(const SpectraRenderMode render_mode) {
        if (render_mode == this->ui.active_render_mode) return;
        if (!this->renderers_ready()) {
            this->ui.active_render_mode = render_mode;
            return;
        }
        switch (render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                if (this->pbrt_interactive == nullptr) throw std::runtime_error("Cannot switch to PBRT Pathtracer without an active PBRT session");
                if (this->camera.pathtracer_accumulation_dirty) this->request_pathtracer_accumulation_reset();
                break;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) throw std::runtime_error("Cannot switch to Vulkan Rasterizer without an active rasterizer session");
                break;
        }
        this->context.device.waitIdle();
        this->ui.active_render_mode = render_mode;
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::initialize_camera_state() {
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        const float initial_move_scale = this->active_renderer_initial_move_scale();
        if (!std::isfinite(initial_move_scale) || !(initial_move_scale > 0.0f)) throw std::runtime_error("Initial camera move scale must be finite and positive");
        this->camera.camera_from_world = this->spectra_scene->camera_from_world;
        const SpectraCameraPose pose   = camera_pose_from_base_matrix(this->camera.camera_from_world, this->active_renderer_initial_focus_bounds());
        this->camera.initialized       = true;
        this->camera.input_enabled     = false;
        this->camera.speed             = initial_move_scale * 60.0f;
        this->camera.fov_degrees       = raster_camera_fov_degrees(*this->spectra_scene);
        this->camera.basis_handedness  = pose.basis_handedness;
        this->camera.eye               = pose.eye;
        this->camera.center            = pose.center;
        this->camera.up                = pose.up;
        this->camera.mouse_position    = {0.0f, 0.0f};
        this->camera.mouse_position_known = false;
        this->camera.moving_from_camera   = identity_matrix_array();
        this->camera.pathtracer_accumulation_dirty = false;
    }

    void Spectra::set_camera_speed(const float speed) {
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        this->camera.speed = speed;
    }

    void Spectra::reset_camera() {
        if (!this->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        const SpectraCameraPose pose  = camera_pose_from_base_matrix(this->camera.camera_from_world, this->active_renderer_initial_focus_bounds());
        this->camera.eye              = pose.eye;
        this->camera.center           = pose.center;
        this->camera.up               = pose.up;
        this->camera.basis_handedness = pose.basis_handedness;
        this->camera.mouse_position_known = false;
        this->camera.moving_from_camera   = identity_matrix_array();
        this->mark_pathtracer_accumulation_dirty();
    }

    void Spectra::process_camera_input(GLFWwindow* window) {
        if (window == nullptr) throw std::runtime_error("Cannot process camera input without a GLFW window");
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, GLFW_TRUE);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->ui.viewport_known && mouse_position.x >= this->ui.viewport_position[0] && mouse_position.x < this->ui.viewport_position[0] + this->ui.viewport_size[0] && mouse_position.y >= this->ui.viewport_position[1] && mouse_position.y < this->ui.viewport_position[1] + this->ui.viewport_size[1];
        this->camera.input_enabled  = in_viewport_rect && (this->ui.viewport_hovered || this->ui.viewport_focused) && !io.WantTextInput;
        if (!this->camera.input_enabled) {
            this->camera.mouse_position_known = false;
            return;
        }
        if (!this->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            this->reset_camera();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->set_camera_speed(this->camera.speed * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->set_camera_speed(this->camera.speed * 0.5f);

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl;
        const bool alt   = io.KeyAlt;
        SpectraCameraPose pose{this->camera.eye, this->camera.center, this->camera.up, this->camera.basis_handedness};
        bool camera_changed = false;
        if (!alt) {
            if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid");
            float key_motion_factor = io.DeltaTime;
            if (shift) key_motion_factor *= 5.0f;
            if (ctrl) key_motion_factor *= 0.1f;
            if (key_motion_factor > 0.0f) {
                if (ImGui::IsKeyDown(ImGuiKey_W)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) camera_changed = camera_key_motion(pose, {0.0f, key_motion_factor}, this->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) camera_changed = camera_key_motion(pose, {0.0f, -key_motion_factor}, this->camera.speed, false) || camera_changed;
            }
        }

        const std::array<float, 2> viewport_size = this->ui.viewport_size;
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        const std::array<float, 2> current_mouse_position{mouse_position.x, mouse_position.y};
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            this->camera.mouse_position       = current_mouse_position;
            this->camera.mouse_position_known = true;
        }

        const bool left_dragging   = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
        const bool middle_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f);
        const bool right_dragging  = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
        if (left_dragging || middle_dragging || right_dragging) {
            if (!this->camera.mouse_position_known) {
                this->camera.mouse_position       = current_mouse_position;
                this->camera.mouse_position_known = true;
            }
            const std::array<float, 2> mouse_displacement{
                (current_mouse_position[0] - this->camera.mouse_position[0]) / viewport_size[0],
                (current_mouse_position[1] - this->camera.mouse_position[1]) / viewport_size[1],
            };
            if (left_dragging) {
                if ((ctrl && shift) || alt) camera_changed = camera_orbit(pose, {mouse_displacement[0], -mouse_displacement[1]}, true) || camera_changed;
                else if (shift) camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
                else if (ctrl) camera_changed = camera_pan(pose, mouse_displacement, this->camera.fov_degrees, viewport_size) || camera_changed;
                else camera_changed = camera_orbit(pose, mouse_displacement, false) || camera_changed;
            } else if (middle_dragging) {
                camera_changed = camera_pan(pose, mouse_displacement, this->camera.fov_degrees, viewport_size) || camera_changed;
            } else if (right_dragging) {
                camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
            }
            this->camera.mouse_position = current_mouse_position;
        }

        if (io.MouseWheel != 0.0f && !shift) {
            constexpr float wheel_speed = 10.0f;
            const float wheel_value     = io.MouseWheel * wheel_speed;
            const float dolly_delta     = wheel_value * std::abs(wheel_value) / viewport_size[0];
            camera_changed              = camera_dolly(pose, {dolly_delta, 0.0f}) || camera_changed;
        }

        if (camera_changed) {
            this->camera.eye                  = pose.eye;
            this->camera.center               = pose.center;
            this->camera.up                   = pose.up;
            this->camera.basis_handedness     = pose.basis_handedness;
            this->camera.moving_from_camera   = moving_from_camera_from_pose(this->camera.camera_from_world, pose);
            this->mark_pathtracer_accumulation_dirty();
        }
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present = false;
        frame.frame_index            = this->sync.frame_index;
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot update renderer frame without an active Spectra scene");
        frame.render_mode = this->ui.active_render_mode;
        this->synchronize_render_resolution();
        if (this->renderers_ready()) {
            this->process_camera_input(this->surface.window.get());
            const ActiveRendererFrameResult render_result = this->render_active_renderer_frame(frame);
            frame.wait_for_external_completion = this->active_renderer_uses_external_completion_semaphore();
            if (frame.wait_for_external_completion) frame.external_completion_semaphore = this->active_renderer_complete_semaphore();
            this->update_frame_statistics(frame, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        } else {
            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            this->update_frame_statistics(frame, false, false, 0);
        }
        return true;
    }

    void Spectra::record_frame(const FrameState& frame) {
        this->draw_main_menu();
        this->draw_dockspace();
        this->draw_viewport_window();
        this->draw_camera_window();
        this->draw_scene_browser_window();
        this->draw_inspector_window();
        this->draw_settings_window();
        this->draw_environment_window();
        this->draw_tonemapper_window();
        this->draw_statistics_window();

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);
        if (this->renderers_ready()) this->record_renderer_output(frame.render_mode, command_buffer);

        {
            const vk::ImageMemoryBarrier2 color_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->swapchain.image_layouts[frame.image_index],
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                this->swapchain.images[frame.image_index],
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.02f, 0.02f, 0.025f, 1.0f};
        const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            color_clear_value,
        };
        const vk::RenderingInfo clear_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(clear_rendering_info);
        command_buffer.endRendering();

        ImGui::Render();
        const vk::RenderingAttachmentInfo imgui_color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo imgui_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &imgui_color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(imgui_rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, this->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame) {
        if (this->imgui.viewports) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        std::array<vk::SemaphoreSubmitInfo, 2> wait_semaphore_infos{
            vk::SemaphoreSubmitInfo{*this->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands},
            vk::SemaphoreSubmitInfo{},
        };
        std::uint32_t wait_semaphore_count = 1;
        if (frame.wait_for_external_completion) {
            wait_semaphore_infos[1] = vk::SemaphoreSubmitInfo{frame.external_completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer};
            wait_semaphore_count    = 2;
        }
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, wait_semaphore_count, wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        bool frame_presented = true;
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
                frame.recreate_after_present = true;
            else if (present_result == vk::Result::eErrorSurfaceLostKHR) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (present_result != vk::Result::eSuccess)
                throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
            frame_presented              = false;
        } catch (const vk::SystemError& error) {
            if (error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (error.code().value() == static_cast<int>(vk::Result::eSuboptimalKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else
                throw;
        }
        if (frame.recreate_after_present) this->recreate_swapchain();
        if (frame_presented) this->update_window_title(ImGui::GetIO().DeltaTime);

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }
    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities   = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes     = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);
            if (surface_formats.empty()) throw std::runtime_error("Surface has no formats");
            if (present_modes.empty()) throw std::runtime_error("Surface has no present modes");

            if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
                this->swapchain.format      = vk::Format::eB8G8R8A8Unorm;
                this->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
            } else {
                this->swapchain.format      = surface_formats.front().format;
                this->swapchain.color_space = surface_formats.front().colorSpace;

                bool selected = false;
                constexpr std::array preferred_surface_formats{
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                };
                for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_surface_formats) {
                    for (const vk::SurfaceFormatKHR& surface_format : surface_formats) {
                        if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) {
                            this->swapchain.format      = surface_format.format;
                            this->swapchain.color_space = surface_format.colorSpace;
                            selected                    = true;
                            break;
                        }
                    }
                    if (selected) break;
                }
            }

            this->swapchain.present_mode = vk::PresentModeKHR::eFifo;
            for (const vk::PresentModeKHR present_mode : present_modes) {
                if (present_mode == vk::PresentModeKHR::eMailbox) {
                    this->swapchain.present_mode = present_mode;
                    break;
                }
                if (present_mode == vk::PresentModeKHR::eImmediate) this->swapchain.present_mode = present_mode;
            }

            this->swapchain.extent = surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? vk::Extent2D{std::clamp(this->surface.extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width), std::clamp(this->surface.extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)} : surface_capabilities.currentExtent;
            if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

            this->swapchain.image_count = surface_capabilities.minImageCount + 1;
            if (this->swapchain.image_count < 2) this->swapchain.image_count = 2;
            if (surface_capabilities.maxImageCount != 0 && this->swapchain.image_count > surface_capabilities.maxImageCount) this->swapchain.image_count = surface_capabilities.maxImageCount;

            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) throw std::runtime_error("Swapchain must support color attachment usage");
            this->swapchain.usage = vk::ImageUsageFlagBits::eColorAttachment;
        }
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            auto composite_alpha                                  = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            if (!(surface_capabilities.supportedCompositeAlpha & composite_alpha)) {
                if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
                else
                    throw std::runtime_error("Surface has no supported composite alpha mode");
            }

            const vk::SurfaceTransformFlagBitsKHR pre_transform = surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surface_capabilities.currentTransform;
            const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->surface.surface, this->swapchain.image_count, this->swapchain.format, this->swapchain.color_space, this->swapchain.extent, 1, this->swapchain.usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->swapchain.present_mode, VK_TRUE, *old_swapchain};
            this->swapchain.handle = vk::raii::SwapchainKHR{this->context.device, swapchain_create_info};
        }
        {
            this->swapchain.images = this->swapchain.handle.getImages();
            if (this->swapchain.images.empty()) throw std::runtime_error("Swapchain has no images");
            this->swapchain.image_layouts.assign(this->swapchain.images.size(), vk::ImageLayout::eUndefined);
        }
        {
            this->swapchain.image_views.clear();
            this->swapchain.image_views.reserve(this->swapchain.images.size());
            for (const vk::Image image : this->swapchain.images) {
                const vk::ImageViewCreateInfo image_view_create_info{{}, image, vk::ImageViewType::e2D, this->swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                this->swapchain.image_views.emplace_back(this->context.device, image_view_create_info);
            }
            if (this->swapchain.image_views.size() != this->swapchain.images.size()) throw std::runtime_error("Failed to create all swapchain image views");
        }
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            this->sync.render_finished_semaphores.clear();
            this->sync.render_finished_semaphores.reserve(this->swapchain.images.size());
            for (std::uint32_t image_index = 0; image_index < this->swapchain.images.size(); ++image_index) this->sync.render_finished_semaphores.emplace_back(this->context.device, semaphore_create_info);
            this->sync.image_in_flight_frame.assign(this->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    void Spectra::recreate_swapchain() {
        {
            int width  = 0;
            int height = 0;
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
                if (width == 0 || height == 0) glfwWaitEvents();
            }
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }

        this->context.device.waitIdle();
        vk::raii::SwapchainKHR old_swapchain = std::move(this->swapchain.handle);
        this->swapchain.image_views.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_in_flight_frame.clear();
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->create_swapchain(std::move(old_swapchain));

        const std::uint32_t image_count = static_cast<std::uint32_t>(this->swapchain.images.size());
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized during swapchain recreation");
        if (this->imgui.color_format != this->swapchain.format || this->imgui.image_count != image_count) {
            const bool docking   = this->imgui.docking;
            const bool viewports = this->imgui.viewports;
            this->destroy_imgui();
            this->imgui.docking   = docking;
            this->imgui.viewports = viewports;
            this->create_imgui();
            if (this->pbrt_interactive != nullptr) this->pbrt_interactive->create_imgui_descriptors();
            if (this->vulkan_rasterizer != nullptr) this->vulkan_rasterizer->create_imgui_descriptors();
        }
        this->surface.resize_requested = false;
    }
} // namespace xayah
