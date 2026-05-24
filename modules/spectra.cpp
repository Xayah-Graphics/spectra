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
#include <cuda_runtime_api.h>
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
#include <cstring>
#include <pbrt/base/film.h>
#include <pbrt/base/sampler.h>
#include <pbrt/gpu/memory.h>
#include <pbrt/gpu/util.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/mesh.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>
#include <pbrt/wavefront/integrator.h>
#include <vulkan/vulkan_raii.hpp>
#include "spectra_raster_spirv.h"

module spectra;
import std;

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
        {
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
            this->pbrt_interactive = std::make_unique<SpectraPbrtInteractiveSession>(*this->spectra_scene, *this->pbrt_backend_scene, this->context.physical_device, this->context.device, this->sync.frame_count);
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

        std::exception_ptr failure{};
        try {
            {
                std::unique_ptr<SpectraScene> loaded_scene = std::make_unique<SpectraScene>();
                try {
                    loaded_scene->load(scene_path);
                    this->spectra_scene = std::move(loaded_scene);
                } catch (...) {
                    loaded_scene->unload_noexcept();
                    throw;
                }
            }
            {
                std::unique_ptr<SpectraRasterScene> loaded_raster_scene = std::make_unique<SpectraRasterScene>();
                try {
                    loaded_raster_scene->build(*this->spectra_scene);
                    this->raster_scene = std::move(loaded_raster_scene);
                } catch (...) {
                    loaded_raster_scene->unload_noexcept();
                    throw;
                }
            }
            this->render_loop();
        } catch (...) {
            failure = std::current_exception();
        }

        try {
            this->context.device.waitIdle();
        } catch (...) {
            if (failure == nullptr) failure = std::current_exception();
        }
        this->unload_renderer_sessions_noexcept();
        this->unload_raster_scene_noexcept();
        this->unload_spectra_scene_noexcept();
        if (failure != nullptr) std::rethrow_exception(failure);
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

    [[nodiscard]] xayah::SpectraPbrtDirective make_directive(const xayah::SpectraPbrtDirectiveKind kind, const pbrt::FileLoc& location) {
        xayah::SpectraPbrtDirective directive{};
        directive.kind      = kind;
        directive.location  = copy_file_location(location);
        directive.transform = xayah::identity_matrix_array();
        return directive;
    }

    [[nodiscard]] std::array<float, 16> copy_transform_matrix(const pbrt::Float transform[16]) {
        std::array<float, 16> copied_transform{};
        for (std::size_t index = 0; index < copied_transform.size(); ++index) copied_transform[index] = static_cast<float>(transform[index]);
        return copied_transform;
    }

    [[nodiscard]] pbrt::Transform pbrt_transform_from_parser_matrix(const pbrt::Float transform[16]) {
        return pbrt::Transpose(pbrt::Transform{pbrt::SquareMatrix<4>{pstd::MakeSpan(transform, 16)}});
    }

    [[nodiscard]] std::array<pbrt::Transform, pbrt::MaxTransforms> inverse_transform_set(const std::array<pbrt::Transform, pbrt::MaxTransforms>& transform_set) {
        std::array<pbrt::Transform, pbrt::MaxTransforms> inverse_set{};
        for (std::size_t index = 0; index < transform_set.size(); ++index) inverse_set[index] = pbrt::Inverse(transform_set[index]);
        return inverse_set;
    }

    [[nodiscard]] std::string lowercase_copy(const std::string& text) {
        std::string lower_text{};
        lower_text.reserve(text.size());
        for (const char character : text) lower_text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        return lower_text;
    }

    [[nodiscard]] bool contains_token_case_insensitive(const std::string& text, const std::string& token) {
        return lowercase_copy(text).find(lowercase_copy(token)) != std::string::npos;
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

    [[nodiscard]] const xayah::SpectraPbrtParameter* find_parameter(const std::vector<xayah::SpectraPbrtParameter>& parameters, const std::string& name) {
        for (const xayah::SpectraPbrtParameter& parameter : parameters) {
            if (parameter.name == name) return &parameter;
        }
        return nullptr;
    }

    void add_raster_diagnostic(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraRasterDiagnosticKind kind, const std::string& source_type, const std::string& source_name, const std::string& message, const xayah::SpectraPbrtFileLocation& location) {
        xayah::SpectraRasterDiagnostic diagnostic{};
        diagnostic.kind        = kind;
        diagnostic.source_type = source_type;
        diagnostic.source_name = source_name;
        diagnostic.message     = message;
        diagnostic.location    = location;
        raster_scene.diagnostics.push_back(std::move(diagnostic));
    }

    [[nodiscard]] bool material_parameter_references_texture(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.name == "type") return false;
        return parameter.type == "texture" || !parameter.strings.empty();
    }

    [[nodiscard]] std::array<float, 3> vector_subtract(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    }

    [[nodiscard]] std::array<float, 3> vector_cross(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0],
        };
    }

    [[nodiscard]] float vector_length(const std::array<float, 3>& value) {
        return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    }

    [[nodiscard]] std::array<float, 3> vector_normalize(const std::array<float, 3>& value) {
        const float length = vector_length(value);
        if (length == 0.0f) return {0.0f, 0.0f, 0.0f};
        return {value[0] / length, value[1] / length, value[2] / length};
    }

    void vector_add_in_place(std::array<float, 3>& target, const std::array<float, 3>& value) {
        target[0] += value[0];
        target[1] += value[1];
        target[2] += value[2];
    }

    [[nodiscard]] std::optional<std::array<float, 3>> read_constant_rgb_parameter(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneMaterial& material, const std::string& material_label, const std::string& parameter_name) {
        const xayah::SpectraPbrtParameter* parameter = find_parameter(material.parameters, parameter_name);
        if (parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material requires constant rgb parameter \"{}\"", material.type, parameter_name), material.location);
            return std::nullopt;
        }
        if (material_parameter_references_texture(*parameter)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        if ((parameter->type != "rgb" && parameter->type != "color") || parameter->floats.size() != 3 || !parameter->ints.empty() || !parameter->bools.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" must be a 3-component constant rgb value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        return std::array<float, 3>{parameter->floats[0], parameter->floats[1], parameter->floats[2]};
    }

    [[nodiscard]] std::optional<float> read_constant_float_parameter(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneMaterial& material, const std::string& material_label, const std::string& parameter_name) {
        const xayah::SpectraPbrtParameter* parameter = find_parameter(material.parameters, parameter_name);
        if (parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material requires constant float parameter \"{}\"", material.type, parameter_name), material.location);
            return std::nullopt;
        }
        if (material_parameter_references_texture(*parameter)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        if (parameter->type != "float" || parameter->floats.size() != 1 || !parameter->ints.empty() || !parameter->bools.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" must be a single constant float value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        return parameter->floats[0];
    }

    [[nodiscard]] std::string raster_material_label(const xayah::SpectraSceneMaterial& material, const std::size_t material_index) {
        if (material.named) return material.name;
        return std::format("<inline:{}>", material_index);
    }

    [[nodiscard]] std::optional<std::size_t> resolve_shape_material_index(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape) {
        if (!shape.material_name.empty()) {
            for (std::size_t index = 0; index < scene.materials.size(); ++index) {
                if (scene.materials[index].named && scene.materials[index].name == shape.material_name) return index;
            }
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, std::format("Named material \"{}\" was not recorded in SpectraScene", shape.material_name), shape.location);
            return std::nullopt;
        }
        if (shape.material_index < 0) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, "Shape has no explicit PBRT material; raster v1 does not synthesize a default material", shape.location);
            return std::nullopt;
        }
        const std::size_t material_index = static_cast<std::size_t>(shape.material_index);
        if (material_index >= scene.materials.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, std::format("Shape references material index {} but SpectraScene has {} materials", material_index, scene.materials.size()), shape.location);
            return std::nullopt;
        }
        return material_index;
    }

    [[nodiscard]] std::optional<std::size_t> build_raster_material(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t source_material_index, std::map<std::size_t, std::size_t>& material_indices) {
        const auto found_material = material_indices.find(source_material_index);
        if (found_material != material_indices.end()) return found_material->second;
        if (source_material_index >= scene.materials.size()) throw std::runtime_error("Raster material source index is out of range");

        const xayah::SpectraSceneMaterial& material = scene.materials[source_material_index];
        const std::string material_label = raster_material_label(material, source_material_index);
        if (material.type != "diffuse" && material.type != "coateddiffuse") {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("Raster v1 only supports diffuse and coateddiffuse materials, not \"{}\"", material.type), material.location);
            return std::nullopt;
        }
        for (const xayah::SpectraPbrtParameter& parameter : material.parameters) {
            if (parameter.name != "type" && parameter.name != "reflectance" && !(material.type == "coateddiffuse" && parameter.name == "roughness")) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" is not in the raster v1 whitelist", material.type, parameter.name), parameter.location);
                return std::nullopt;
            }
            if (material_parameter_references_texture(parameter)) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter.name), parameter.location);
                return std::nullopt;
            }
        }

        const std::optional<std::array<float, 3>> base_color = read_constant_rgb_parameter(raster_scene, material, material_label, "reflectance");
        if (!base_color.has_value()) return std::nullopt;

        xayah::SpectraRasterMaterial raster_material{};
        raster_material.name                  = material_label;
        raster_material.source_type           = material.type;
        raster_material.base_color            = base_color.value();
        raster_material.source_material_index = source_material_index;
        if (material.type == "coateddiffuse") {
            const std::optional<float> roughness = read_constant_float_parameter(raster_scene, material, material_label, "roughness");
            if (!roughness.has_value()) return std::nullopt;
            raster_material.roughness = roughness.value();
        }

        raster_scene.materials.push_back(std::move(raster_material));
        const std::size_t raster_material_index = raster_scene.materials.size() - 1;
        material_indices[source_material_index] = raster_material_index;
        return raster_material_index;
    }

    [[nodiscard]] std::vector<std::array<float, 3>> read_point3_array_parameter(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.floats.size() % 3 != 0u) throw std::runtime_error(std::format("Parameter {} has {} float values, not a multiple of 3", parameter.name, parameter.floats.size()));
        std::vector<std::array<float, 3>> values{};
        values.reserve(parameter.floats.size() / 3);
        for (std::size_t index = 0; index < parameter.floats.size(); index += 3) values.push_back({parameter.floats[index], parameter.floats[index + 1], parameter.floats[index + 2]});
        return values;
    }

    [[nodiscard]] std::vector<std::array<float, 2>> read_point2_array_parameter(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.floats.size() % 2 != 0u) throw std::runtime_error(std::format("Parameter {} has {} float values, not a multiple of 2", parameter.name, parameter.floats.size()));
        std::vector<std::array<float, 2>> values{};
        values.reserve(parameter.floats.size() / 2);
        for (std::size_t index = 0; index < parameter.floats.size(); index += 2) values.push_back({parameter.floats[index], parameter.floats[index + 1]});
        return values;
    }

    [[nodiscard]] std::vector<std::array<float, 3>> compute_triangle_vertex_normals(const std::vector<std::array<float, 3>>& positions, const std::vector<std::uint32_t>& indices) {
        std::vector<std::array<float, 3>> normals(positions.size(), {0.0f, 0.0f, 0.0f});
        for (std::size_t index = 0; index < indices.size(); index += 3) {
            const std::uint32_t vertex0 = indices[index];
            const std::uint32_t vertex1 = indices[index + 1];
            const std::uint32_t vertex2 = indices[index + 2];
            const std::array<float, 3> edge10 = vector_subtract(positions[vertex1], positions[vertex0]);
            const std::array<float, 3> edge20 = vector_subtract(positions[vertex2], positions[vertex0]);
            const std::array<float, 3> face_normal = vector_normalize(vector_cross(edge10, edge20));
            vector_add_in_place(normals[vertex0], face_normal);
            vector_add_in_place(normals[vertex1], face_normal);
            vector_add_in_place(normals[vertex2], face_normal);
        }
        for (std::array<float, 3>& normal : normals) normal = vector_normalize(normal);
        return normals;
    }

    [[nodiscard]] std::optional<std::size_t> append_raster_mesh(xayah::SpectraRasterScene& raster_scene, const std::size_t source_shape_index, const std::string& source_type, const std::filesystem::path& source_path, const xayah::SpectraPbrtFileLocation& location, const std::vector<std::array<float, 3>>& positions, const std::vector<std::array<float, 3>>& normals, const std::vector<std::array<float, 2>>& uvs, const std::vector<std::uint32_t>& local_indices) {
        if (positions.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh has no vertex positions", location);
            return std::nullopt;
        }
        if (local_indices.empty() || local_indices.size() % 3 != 0u) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh index count must be a non-empty multiple of 3", location);
            return std::nullopt;
        }
        if (!normals.empty() && normals.size() != positions.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh normal count does not match vertex count", location);
            return std::nullopt;
        }
        if (!uvs.empty() && uvs.size() != positions.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh uv count does not match vertex count", location);
            return std::nullopt;
        }
        for (const std::uint32_t index : local_indices) {
            if (static_cast<std::size_t>(index) >= positions.size()) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, std::format("Mesh index {} is out of range for {} vertices", index, positions.size()), location);
                return std::nullopt;
            }
        }
        if (raster_scene.vertices.size() + positions.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster scene vertex count exceeds uint32 index range");

        const std::size_t first_vertex = raster_scene.vertices.size();
        const std::size_t first_index = raster_scene.indices.size();
        const std::vector<std::array<float, 3>> computed_normals = normals.empty() ? compute_triangle_vertex_normals(positions, local_indices) : normals;

        raster_scene.vertices.reserve(raster_scene.vertices.size() + positions.size());
        for (std::size_t index = 0; index < positions.size(); ++index) {
            xayah::SpectraRasterVertex vertex{};
            vertex.position = positions[index];
            vertex.normal   = computed_normals[index];
            if (!uvs.empty()) vertex.uv = uvs[index];
            raster_scene.vertices.push_back(vertex);
        }

        raster_scene.indices.reserve(raster_scene.indices.size() + local_indices.size());
        for (const std::uint32_t index : local_indices) raster_scene.indices.push_back(static_cast<std::uint32_t>(first_vertex) + index);

        xayah::SpectraRasterGeometry geometry{};
        geometry.source_shape_index = source_shape_index;
        geometry.source_type        = source_type;
        geometry.source_path        = source_path;
        geometry.first_vertex       = first_vertex;
        geometry.vertex_count       = positions.size();
        geometry.first_index        = first_index;
        geometry.index_count        = local_indices.size();
        raster_scene.geometries.push_back(std::move(geometry));
        return raster_scene.geometries.size() - 1;
    }

    [[nodiscard]] std::optional<std::size_t> append_trianglemesh_geometry(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape, const std::size_t shape_index) {
        const xayah::SpectraPbrtParameter* position_parameter = find_parameter(shape.parameters, "P");
        if (position_parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh requires point3 P positions", shape.location);
            return std::nullopt;
        }

        std::vector<std::array<float, 3>> positions{};
        try {
            positions = read_point3_array_parameter(*position_parameter);
        } catch (const std::exception& error) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), position_parameter->location);
            return std::nullopt;
        }

        std::vector<std::uint32_t> local_indices{};
        const xayah::SpectraPbrtParameter* index_parameter = find_parameter(shape.parameters, "indices");
        if (index_parameter == nullptr) {
            if (positions.size() != 3) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh without indices must contain exactly 3 positions", shape.location);
                return std::nullopt;
            }
            local_indices = {0u, 1u, 2u};
        } else {
            if (index_parameter->ints.empty() || index_parameter->ints.size() % 3 != 0u) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh indices must be a non-empty multiple of 3", index_parameter->location);
                return std::nullopt;
            }
            local_indices.reserve(index_parameter->ints.size());
            for (const int index : index_parameter->ints) {
                if (index < 0) {
                    add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, std::format("trianglemesh index {} is negative", index), index_parameter->location);
                    return std::nullopt;
                }
                local_indices.push_back(static_cast<std::uint32_t>(index));
            }
        }

        std::vector<std::array<float, 3>> normals{};
        const xayah::SpectraPbrtParameter* normal_parameter = find_parameter(shape.parameters, "N");
        if (normal_parameter != nullptr) {
            try {
                normals = read_point3_array_parameter(*normal_parameter);
            } catch (const std::exception& error) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), normal_parameter->location);
                return std::nullopt;
            }
        }

        std::vector<std::array<float, 2>> uvs{};
        const xayah::SpectraPbrtParameter* uv_parameter = find_parameter(shape.parameters, "uv");
        if (uv_parameter != nullptr) {
            try {
                uvs = read_point2_array_parameter(*uv_parameter);
            } catch (const std::exception& error) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), uv_parameter->location);
                return std::nullopt;
            }
        }

        return append_raster_mesh(raster_scene, shape_index, shape.type, {}, shape.location, positions, normals, uvs, local_indices);
    }

    [[nodiscard]] std::filesystem::path resolve_shape_relative_path(const xayah::SpectraScene& scene, const xayah::SpectraSceneShape& shape, const std::string& filename) {
        std::filesystem::path resolved_path{filename};
        if (resolved_path.is_absolute()) return resolved_path;
        std::filesystem::path base_path{};
        if (!shape.location.filename.empty()) base_path = std::filesystem::path{shape.location.filename}.parent_path();
        if (base_path.empty()) base_path = scene.scene_path.parent_path();
        return base_path / resolved_path;
    }

    [[nodiscard]] std::optional<std::size_t> append_plymesh_geometry(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape, const std::size_t shape_index) {
        const xayah::SpectraPbrtParameter* filename_parameter = find_parameter(shape.parameters, "filename");
        if (filename_parameter == nullptr || filename_parameter->strings.size() != 1) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh requires exactly one string filename parameter", shape.location);
            return std::nullopt;
        }

        const std::filesystem::path ply_path = resolve_shape_relative_path(scene, shape, filename_parameter->strings.front());
        if (!std::filesystem::exists(ply_path)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingPlyFile, "shape", shape.type, std::format("PLY file does not exist: {}", ply_path.string()), filename_parameter->location);
            return std::nullopt;
        }

        pbrt::TriQuadMesh mesh = pbrt::TriQuadMesh::ReadPLY(ply_path.string());
        mesh.ConvertToOnlyTriangles();
        if (mesh.n.empty()) mesh.ComputeNormals();
        if (mesh.p.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh produced no vertices", shape.location);
            return std::nullopt;
        }
        if (mesh.triIndices.empty() || mesh.triIndices.size() % 3 != 0u) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh produced no triangle indices", shape.location);
            return std::nullopt;
        }
        if (!mesh.n.empty() && mesh.n.size() != mesh.p.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh normal count does not match vertex count", shape.location);
            return std::nullopt;
        }
        if (!mesh.uv.empty() && mesh.uv.size() != mesh.p.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh uv count does not match vertex count", shape.location);
            return std::nullopt;
        }

        std::vector<std::array<float, 3>> positions{};
        positions.reserve(mesh.p.size());
        for (const pbrt::Point3f& point : mesh.p) positions.push_back({static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)});

        std::vector<std::array<float, 3>> normals{};
        normals.reserve(mesh.n.size());
        for (const pbrt::Normal3f& normal : mesh.n) normals.push_back({static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z)});

        std::vector<std::array<float, 2>> uvs{};
        uvs.reserve(mesh.uv.size());
        for (const pbrt::Point2f& uv : mesh.uv) uvs.push_back({static_cast<float>(uv.x), static_cast<float>(uv.y)});

        std::vector<std::uint32_t> local_indices{};
        local_indices.reserve(mesh.triIndices.size());
        for (const int index : mesh.triIndices) {
            if (index < 0) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, std::format("plymesh index {} is negative", index), shape.location);
                return std::nullopt;
            }
            local_indices.push_back(static_cast<std::uint32_t>(index));
        }

        return append_raster_mesh(raster_scene, shape_index, shape.type, ply_path, shape.location, positions, normals, uvs, local_indices);
    }

    [[nodiscard]] bool shape_is_supported_for_raster(const xayah::SpectraSceneShape& shape, xayah::SpectraRasterScene& raster_scene) {
        if (shape.animated_transform) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform, "shape", shape.type, "Raster v1 does not support animated shape transforms", shape.location);
            return false;
        }
        if (!shape.inside_medium.empty() || !shape.outside_medium.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMediumBinding, "shape", shape.type, "Raster v1 does not support PBRT medium-bound shapes", shape.location);
            return false;
        }
        if (!shape.area_light_type.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedAreaLight, "shape", shape.type, "Raster v1 does not render PBRT area-light shapes", shape.location);
            return false;
        }
        if (shape.type != "trianglemesh" && shape.type != "plymesh") {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedShape, "shape", shape.type, std::format("Raster v1 only supports trianglemesh and plymesh, not \"{}\"", shape.type), shape.location);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> build_raster_geometry(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t shape_index, std::map<std::size_t, std::size_t>& geometry_indices) {
        const auto found_geometry = geometry_indices.find(shape_index);
        if (found_geometry != geometry_indices.end()) return found_geometry->second;
        if (shape_index >= scene.shapes.size()) throw std::runtime_error("Raster shape index is out of range");

        const xayah::SpectraSceneShape& shape = scene.shapes[shape_index];
        std::optional<std::size_t> geometry_index{};
        if (shape.type == "trianglemesh") geometry_index = append_trianglemesh_geometry(raster_scene, shape, shape_index);
        else if (shape.type == "plymesh") geometry_index = append_plymesh_geometry(scene, raster_scene, shape, shape_index);
        else throw std::runtime_error("Raster geometry builder received an unsupported shape type");
        if (geometry_index.has_value()) geometry_indices[shape_index] = geometry_index.value();
        return geometry_index;
    }

    void append_raster_draw(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t shape_index, const std::array<float, 16>& transform, const std::size_t instance_index, std::map<std::size_t, std::size_t>& material_indices, std::map<std::size_t, std::size_t>& geometry_indices) {
        if (shape_index >= scene.shapes.size()) throw std::runtime_error("Raster draw shape index is out of range");
        const xayah::SpectraSceneShape& shape = scene.shapes[shape_index];
        if (!shape_is_supported_for_raster(shape, raster_scene)) return;

        const std::optional<std::size_t> source_material_index = resolve_shape_material_index(scene, raster_scene, shape);
        if (!source_material_index.has_value()) return;
        const std::optional<std::size_t> raster_material_index = build_raster_material(scene, raster_scene, source_material_index.value(), material_indices);
        if (!raster_material_index.has_value()) return;
        const std::optional<std::size_t> raster_geometry_index = build_raster_geometry(scene, raster_scene, shape_index, geometry_indices);
        if (!raster_geometry_index.has_value()) return;

        xayah::SpectraRasterDraw draw{};
        draw.geometry_index        = raster_geometry_index.value();
        draw.material_index        = raster_material_index.value();
        draw.source_shape_index    = shape_index;
        draw.source_instance_index = instance_index;
        draw.transform             = transform;
        draw.reverse_orientation   = shape.reverse_orientation;
        raster_scene.draws.push_back(std::move(draw));
    }

    [[nodiscard]] const xayah::SpectraSceneObjectDefinition* find_object_definition(const xayah::SpectraScene& scene, const std::string& name) {
        for (const xayah::SpectraSceneObjectDefinition& object_definition : scene.object_definitions) {
            if (object_definition.name == name) return &object_definition;
        }
        return nullptr;
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
        std::array<pbrt::Transform, pbrt::MaxTransforms> ctm{};
        std::uint32_t active_transform_bits{all_transform_bits};
        pbrt::Float transform_start_time{0.0f};
        pbrt::Float transform_end_time{1.0f};
    };

    struct SpectraPbrtSceneBuilder final : pbrt::ParserTarget {
        xayah::SpectraScene* spectra_scene{nullptr};
        xayah::SpectraSceneBuildChunk chunk{};
        SpectraPbrtBuilderGraphicsState graphics_state{};
        std::vector<SpectraPbrtBuilderGraphicsState> pushed_graphics_states{};
        std::map<std::string, std::array<pbrt::Transform, pbrt::MaxTransforms>> named_coordinate_systems{};
        std::vector<std::unique_ptr<pbrt::ParserTarget>> imported_builders{};
        std::string active_object_definition_name{};
        std::size_t material_index_base{0};
        bool root_builder{true};
        bool world_begun{false};

        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene) : spectra_scene{&scene} {}
        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene, const SpectraPbrtBuilderGraphicsState& parent_graphics_state, const std::vector<SpectraPbrtBuilderGraphicsState>& parent_pushed_graphics_states, const std::map<std::string, std::array<pbrt::Transform, pbrt::MaxTransforms>>& parent_named_coordinate_systems, const std::string& parent_active_object_definition_name, const std::size_t parent_material_index_base, const bool parent_world_begun) : spectra_scene{&scene}, graphics_state{parent_graphics_state}, pushed_graphics_states{parent_pushed_graphics_states}, named_coordinate_systems{parent_named_coordinate_systems}, active_object_definition_name{parent_active_object_definition_name}, material_index_base{parent_material_index_base}, root_builder{false}, world_begun{parent_world_begun} {}

        ~SpectraPbrtSceneBuilder() override = default;

        SpectraPbrtSceneBuilder(const SpectraPbrtSceneBuilder& other)                = delete;
        SpectraPbrtSceneBuilder(SpectraPbrtSceneBuilder&& other) noexcept            = delete;
        SpectraPbrtSceneBuilder& operator=(const SpectraPbrtSceneBuilder& other)     = delete;
        SpectraPbrtSceneBuilder& operator=(SpectraPbrtSceneBuilder&& other) noexcept = delete;

        void record_directive(xayah::SpectraPbrtDirective directive) {
            this->chunk.pbrt_directives.push_back(std::move(directive));
        }

        void merge_chunk(xayah::SpectraSceneBuildChunk& imported_chunk) {
            append_vector(this->chunk.pbrt_directives, imported_chunk.pbrt_directives);
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
            append_vector(this->chunk.unsupported_features, imported_chunk.unsupported_features);
        }

        [[nodiscard]] bool current_transform_is_animated() const {
            for (std::size_t index = 0; index + 1 < this->graphics_state.ctm.size(); ++index) {
                if (this->graphics_state.ctm[index] != this->graphics_state.ctm[index + 1]) return true;
            }
            return false;
        }

        [[nodiscard]] std::array<float, 16> current_transform_matrix() const {
            return xayah::matrix_array_from_transform(this->graphics_state.ctm[0]);
        }

        void mark_unsupported(const xayah::SpectraSceneUnsupportedFeatureKind kind, const std::string& source_type, const std::string& source_name, const std::string& message, const pbrt::FileLoc& location) {
            xayah::SpectraSceneUnsupportedFeature feature{};
            feature.kind        = kind;
            feature.source_type = source_type;
            feature.source_name = source_name;
            feature.message     = message;
            feature.location    = copy_file_location(location);
            this->chunk.unsupported_features.push_back(std::move(feature));
        }

        void mark_medium_features(const std::string& name, const std::vector<xayah::SpectraPbrtParameter>& parameters, const pbrt::FileLoc& location) {
            this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "medium", name, "PBRT participating media are not rasterizable in Spectra rasterizer v1", location);
            const std::string medium_type = first_string_parameter_value(parameters, "type");
            bool references_vdb = contains_token_case_insensitive(name, "vdb") || contains_token_case_insensitive(medium_type, "vdb") || contains_token_case_insensitive(medium_type, "nanovdb") || contains_token_case_insensitive(medium_type, "grid");
            for (const xayah::SpectraPbrtParameter& parameter : parameters) {
                for (const std::string& value : parameter.strings) {
                    references_vdb = references_vdb || contains_token_case_insensitive(value, "vdb") || contains_token_case_insensitive(value, "nanovdb") || contains_token_case_insensitive(value, "grid");
                }
            }
            if (references_vdb) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::VdbMedium, "medium", name, "VDB/NanoVDB media require a later explicit raster policy", location);
        }

        [[nodiscard]] xayah::SpectraSceneRenderSetting make_render_setting(const std::string& type, const std::string& name, const std::vector<xayah::SpectraPbrtParameter>& parameters, const pbrt::FileLoc& location, const bool include_transform) const {
            xayah::SpectraSceneRenderSetting setting{};
            setting.present    = true;
            setting.type       = type;
            setting.name       = name;
            setting.location   = copy_file_location(location);
            setting.transform  = include_transform ? this->current_transform_matrix() : xayah::identity_matrix_array();
            setting.parameters = parameters;
            return setting;
        }

        void apply_transform_to_active(const pbrt::Transform& transform) {
            for (std::size_t index = 0; index < this->graphics_state.ctm.size(); ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = this->graphics_state.ctm[index] * transform;
            }
        }

        void replace_active_transform(const pbrt::Transform& transform) {
            for (std::size_t index = 0; index < this->graphics_state.ctm.size(); ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = transform;
            }
        }

        void Scale(const pbrt::Float sx, const pbrt::Float sy, const pbrt::Float sz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Scale, loc);
            directive.vector                       = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), 0.0f};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Scale(sx, sy, sz));
        }

        void Shape(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Shape, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));

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
            shape.transform              = this->current_transform_matrix();
            shape.parameters             = copied_parameters;
            this->chunk.shapes.push_back(std::move(shape));

            if (animated_transform) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform, "shape", name, "Animated shape transforms are not rasterizable in Spectra rasterizer v1", loc);
            if (!this->graphics_state.current_inside_medium.empty() || !this->graphics_state.current_outside_medium.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "shape", name, "Shape references a PBRT medium interface", loc);
            if (!this->graphics_state.area_light_type.empty()) {
                xayah::SpectraSceneLight light{};
                light.type           = this->graphics_state.area_light_type;
                light.area           = true;
                light.outside_medium = this->graphics_state.current_outside_medium;
                light.location       = this->graphics_state.area_light_location;
                light.transform      = this->current_transform_matrix();
                light.parameters     = this->graphics_state.area_light_parameters;
                this->chunk.lights.push_back(std::move(light));
                if (!this->active_object_definition_name.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AreaLightInObjectDefinition, "shape", name, "Area lights inside PBRT object definitions need a later explicit instance policy", loc);
            }
        }

        void Option(const std::string& name, const std::string& value, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Option, loc);
            directive.name                         = name;
            directive.value                        = value;
            this->record_directive(std::move(directive));
        }

        void Identity(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::Identity, loc));
            this->replace_active_transform(pbrt::Transform{});
        }

        void Translate(const pbrt::Float dx, const pbrt::Float dy, const pbrt::Float dz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Translate, loc);
            directive.vector                       = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz), 0.0f};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Translate(pbrt::Vector3f{dx, dy, dz}));
        }

        void Rotate(const pbrt::Float angle, const pbrt::Float ax, const pbrt::Float ay, const pbrt::Float az, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Rotate, loc);
            directive.vector                       = {static_cast<float>(angle), static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(az)};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Rotate(angle, pbrt::Vector3f{ax, ay, az}));
        }

        void LookAt(const pbrt::Float ex, const pbrt::Float ey, const pbrt::Float ez, const pbrt::Float lx, const pbrt::Float ly, const pbrt::Float lz, const pbrt::Float ux, const pbrt::Float uy, const pbrt::Float uz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::LookAt, loc);
            directive.look_at                      = {static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez), static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz)};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::LookAt(pbrt::Point3f{ex, ey, ez}, pbrt::Point3f{lx, ly, lz}, pbrt::Vector3f{ux, uy, uz}));
        }

        void ConcatTransform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ConcatTransform, loc);
            directive.transform                    = copy_transform_matrix(transform);
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt_transform_from_parser_matrix(transform));
        }

        void Transform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Transform, loc);
            directive.transform                    = copy_transform_matrix(transform);
            this->record_directive(std::move(directive));
            this->replace_active_transform(pbrt_transform_from_parser_matrix(transform));
        }

        void CoordinateSystem(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::CoordinateSystem, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->named_coordinate_systems[name]   = this->graphics_state.ctm;
        }

        void CoordSysTransform(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::CoordSysTransform, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            const auto found = this->named_coordinate_systems.find(name);
            if (found != this->named_coordinate_systems.end()) this->graphics_state.ctm = found->second;
        }

        void ActiveTransformAll(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformAll, loc));
            this->graphics_state.active_transform_bits = all_transform_bits;
        }

        void ActiveTransformEndTime(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformEndTime, loc));
            this->graphics_state.active_transform_bits = end_transform_bit;
        }

        void ActiveTransformStartTime(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformStartTime, loc));
            this->graphics_state.active_transform_bits = start_transform_bit;
        }

        void TransformTimes(const pbrt::Float start, const pbrt::Float end, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::TransformTimes, loc);
            directive.times                        = {static_cast<float>(start), static_cast<float>(end)};
            this->record_directive(std::move(directive));
            this->graphics_state.transform_start_time = start;
            this->graphics_state.transform_end_time   = end;
        }

        void ColorSpace(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ColorSpace, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
        }

        void PixelFilter(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::PixelFilter, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.pixel_filter = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Film, loc);
            directive.type                         = type;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.film = this->make_render_setting(type, {}, copied_parameters, loc, false);
        }

        void Accelerator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Accelerator, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.accelerator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Integrator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Integrator, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.integrator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Camera(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Camera, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.camera = this->make_render_setting(name, name, copied_parameters, loc, true);
            this->named_coordinate_systems["camera"] = inverse_transform_set(this->graphics_state.ctm);
        }

        void MakeNamedMedium(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MakeNamedMedium, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneMedium medium{};
            medium.name       = name;
            medium.type       = first_string_parameter_value(copied_parameters, "type");
            medium.location   = copy_file_location(loc);
            medium.transform  = this->current_transform_matrix();
            medium.parameters = copied_parameters;
            this->chunk.mediums.push_back(std::move(medium));
            this->mark_medium_features(name, copied_parameters, loc);
        }

        void MediumInterface(const std::string& inside_name, const std::string& outside_name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MediumInterface, loc);
            directive.name                         = inside_name;
            directive.value                        = outside_name;
            this->record_directive(std::move(directive));
            this->graphics_state.current_inside_medium  = inside_name;
            this->graphics_state.current_outside_medium = outside_name;
            xayah::SpectraSceneMediumBinding binding{};
            binding.inside   = inside_name;
            binding.outside  = outside_name;
            binding.location = copy_file_location(loc);
            this->chunk.medium_bindings.push_back(std::move(binding));
            if (!inside_name.empty() || !outside_name.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "medium-interface", inside_name + "/" + outside_name, "PBRT medium interfaces are not rasterizable in Spectra rasterizer v1", loc);
        }

        void Sampler(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Sampler, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.sampler = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void WorldBegin(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::WorldBegin, loc));
            this->graphics_state = {};
            this->pushed_graphics_states.clear();
            this->active_object_definition_name.clear();
            this->named_coordinate_systems["world"] = this->graphics_state.ctm;
            this->world_begun = true;
        }

        void AttributeBegin(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::AttributeBegin, loc));
            this->pushed_graphics_states.push_back(this->graphics_state);
        }

        void AttributeEnd(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::AttributeEnd, loc));
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
        }

        void Attribute(const std::string& target, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Attribute, loc);
            directive.target                       = target;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParserAttribute, "attribute", target, "PBRT Attribute directives need explicit rasterizer support before use", loc);
        }

        void Texture(const std::string& name, const std::string& type, const std::string& texture_name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Texture, loc);
            directive.name                         = name;
            directive.type                         = type;
            directive.value                        = texture_name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneTexture texture{};
            texture.name           = name;
            texture.value_type     = type == "float" ? xayah::SpectraSceneTextureValueType::Float : type == "spectrum" ? xayah::SpectraSceneTextureValueType::Spectrum : xayah::SpectraSceneTextureValueType::Unknown;
            texture.implementation = texture_name;
            texture.location       = copy_file_location(loc);
            texture.transform      = this->current_transform_matrix();
            texture.parameters     = copied_parameters;
            this->chunk.textures.push_back(std::move(texture));
            if (texture_name != "imagemap" && texture_name != "constant") this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ProceduralTexture, "texture", name, "Procedural PBRT textures require a later explicit raster policy", loc);
        }

        void Material(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Material, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
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
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MakeNamedMaterial, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneMaterial material{};
            material.name       = name;
            material.type       = first_string_parameter_value(copied_parameters, "type");
            material.named      = true;
            material.location   = copy_file_location(loc);
            material.parameters = copied_parameters;
            this->chunk.materials.push_back(std::move(material));
        }

        void NamedMaterial(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::NamedMaterial, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->graphics_state.current_material_name  = name;
            this->graphics_state.current_material_index = -1;
        }

        void LightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::LightSource, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneLight light{};
            light.type           = name;
            light.area           = false;
            light.outside_medium = this->graphics_state.current_outside_medium;
            light.location       = copy_file_location(loc);
            light.transform      = this->current_transform_matrix();
            light.parameters     = copied_parameters;
            this->chunk.lights.push_back(std::move(light));
            if (!this->graphics_state.current_outside_medium.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "light", name, "Light references a PBRT outside medium", loc);
        }

        void AreaLightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::AreaLightSource, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->graphics_state.area_light_type       = name;
            this->graphics_state.area_light_location   = copy_file_location(loc);
            this->graphics_state.area_light_parameters = copied_parameters;
        }

        void ReverseOrientation(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ReverseOrientation, loc));
            this->graphics_state.reverse_orientation = !this->graphics_state.reverse_orientation;
        }

        void ObjectBegin(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ObjectBegin, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->pushed_graphics_states.push_back(this->graphics_state);
            this->active_object_definition_name = name;
            find_or_create_object_definition(this->chunk.object_definitions, name, copy_file_location(loc));
        }

        void ObjectEnd(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ObjectEnd, loc));
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
            this->active_object_definition_name.clear();
        }

        void ObjectInstance(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ObjectInstance, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            const bool animated_transform = this->current_transform_is_animated();
            xayah::SpectraSceneObjectInstance object_instance{};
            object_instance.name               = name;
            object_instance.animated_transform = animated_transform;
            object_instance.location           = copy_file_location(loc);
            object_instance.transform          = this->current_transform_matrix();
            this->chunk.object_instances.push_back(std::move(object_instance));
            if (animated_transform) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform, "object-instance", name, "Animated object instance transforms are not rasterizable in Spectra rasterizer v1", loc);
        }

        void EndOfFiles() override {
            pbrt::FileLoc location{};
            location.filename = this->spectra_scene == nullptr ? std::string_view{} : std::string_view{this->spectra_scene->scene_path_text};
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::EndOfFiles, location));
            if (!this->pushed_graphics_states.empty()) throw std::runtime_error("Missing AttributeEnd before EndOfFiles in Spectra scene parser");
            if (this->root_builder) {
                if (this->spectra_scene == nullptr) throw std::runtime_error("Spectra scene builder has no target scene at EndOfFiles");
                this->spectra_scene->append_build_chunk(std::move(this->chunk));
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
    struct SpectraPbrtBackendSceneState {
        std::unique_ptr<pbrt::BasicScene> scene{};
        std::unique_ptr<pbrt::BasicSceneBuilder> builder{};
    };

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
            if (this->pbrt_directives.empty()) throw std::runtime_error("Spectra scene parser recorded no PBRT directives");
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraScene::append_build_chunk(SpectraSceneBuildChunk chunk) {
        std::lock_guard<std::mutex> lock{this->scene_mutex};
        append_vector(this->pbrt_directives, chunk.pbrt_directives);
        merge_setting(this->pixel_filter, chunk.pixel_filter);
        merge_setting(this->film, chunk.film);
        merge_setting(this->sampler, chunk.sampler);
        merge_setting(this->accelerator, chunk.accelerator);
        merge_setting(this->integrator, chunk.integrator);
        merge_setting(this->camera, chunk.camera);
        append_vector(this->textures, chunk.textures);
        append_vector(this->materials, chunk.materials);
        append_vector(this->mediums, chunk.mediums);
        append_vector(this->medium_bindings, chunk.medium_bindings);
        append_vector(this->lights, chunk.lights);
        append_vector(this->object_definitions, chunk.object_definitions);
        for (SpectraSceneShape& shape : chunk.shapes) {
            const std::string object_definition_name = shape.object_definition_name;
            const SpectraPbrtFileLocation shape_location = shape.location;
            const std::size_t shape_index = this->shapes.size();
            this->shapes.push_back(std::move(shape));
            if (!object_definition_name.empty()) {
                const std::size_t object_definition_index = find_or_create_object_definition(this->object_definitions, object_definition_name, shape_location);
                this->object_definitions[object_definition_index].shape_indices.push_back(shape_index);
            }
        }
        chunk.shapes.clear();
        append_vector(this->object_instances, chunk.object_instances);
        append_vector(this->unsupported_features, chunk.unsupported_features);
    }

    void SpectraScene::set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const std::array<float, 16>& camera_transform) {
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
        this->camera_from_world    = identity_matrix_array();
        this->sampler_sample_count = 0;
        try {
            std::lock_guard<std::mutex> lock{this->scene_mutex};
            this->pbrt_directives.clear();
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
            this->unsupported_features.clear();
        } catch (...) {
        }
    }

    void SpectraRasterScene::build(const SpectraScene& scene) {
        if (!this->scene_path.empty() || !this->vertices.empty() || !this->indices.empty() || !this->materials.empty() || !this->geometries.empty() || !this->draws.empty() || !this->diagnostics.empty()) throw std::runtime_error("Spectra raster scene is already built");
        if (scene.scene_path.empty()) throw std::runtime_error("Cannot build SpectraRasterScene without a loaded SpectraScene");
        if (scene.pbrt_directives.empty()) throw std::runtime_error("Cannot build SpectraRasterScene before SpectraScene parsing is complete");

        try {
            this->scene_path  = scene.scene_path;
            this->scene_label = scene.scene_label;
            std::map<std::size_t, std::size_t> material_indices{};
            std::map<std::size_t, std::size_t> geometry_indices{};

            for (std::size_t shape_index = 0; shape_index < scene.shapes.size(); ++shape_index) {
                const SpectraSceneShape& shape = scene.shapes[shape_index];
                if (!shape.object_definition_name.empty()) continue;
                append_raster_draw(scene, *this, shape_index, shape.transform, std::numeric_limits<std::size_t>::max(), material_indices, geometry_indices);
            }

            for (std::size_t instance_index = 0; instance_index < scene.object_instances.size(); ++instance_index) {
                const SpectraSceneObjectInstance& object_instance = scene.object_instances[instance_index];
                if (object_instance.animated_transform) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform, "object-instance", object_instance.name, "Raster v1 does not support animated object instance transforms", object_instance.location);
                    continue;
                }
                const SpectraSceneObjectDefinition* object_definition = find_object_definition(scene, object_instance.name);
                if (object_definition == nullptr) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedObjectInstance, "object-instance", object_instance.name, "ObjectInstance references an object definition that was not recorded", object_instance.location);
                    continue;
                }
                if (object_definition->shape_indices.empty()) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedObjectInstance, "object-instance", object_instance.name, "ObjectInstance references an empty object definition", object_instance.location);
                    continue;
                }
                for (const std::size_t shape_index : object_definition->shape_indices) {
                    if (shape_index >= scene.shapes.size()) throw std::runtime_error("Object definition shape index is out of range");
                    const SpectraSceneShape& shape = scene.shapes[shape_index];
                    const std::array<float, 16> transform = multiply_matrix_arrays(object_instance.transform, shape.transform);
                    append_raster_draw(scene, *this, shape_index, transform, instance_index, material_indices, geometry_indices);
                }
            }
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraRasterScene::unload_noexcept() noexcept {
        this->scene_path.clear();
        this->scene_label = "No Scene";
        this->vertices.clear();
        this->indices.clear();
        this->materials.clear();
        this->geometries.clear();
        this->draws.clear();
        this->diagnostics.clear();
    }

    SpectraPbrtBackendScene::SpectraPbrtBackendScene() : state{std::make_unique<SpectraPbrtBackendSceneState>()} {}

    SpectraPbrtBackendScene::~SpectraPbrtBackendScene() noexcept {
        this->unload_noexcept();
    }

    void SpectraPbrtBackendScene::load(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution) {
        if (this->state == nullptr) throw std::runtime_error("PBRT backend scene state is null");
        if (this->state->scene != nullptr) throw std::runtime_error("PBRT backend scene is already loaded");
        if (spectra_scene.scene_path.empty()) throw std::runtime_error("Cannot build PBRT backend scene without a loaded Spectra scene");
        if (spectra_scene.pbrt_directives.empty()) throw std::runtime_error("Cannot build PBRT backend scene before Spectra scene parsing is complete");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot build PBRT backend scene with a non-positive resolution");
        if (pbrt::Options == nullptr) throw std::runtime_error("Cannot build PBRT backend scene before PBRT runtime is initialized");
        try {
            this->state->scene   = std::make_unique<pbrt::BasicScene>();
            this->state->builder = std::make_unique<SpectraResolutionOverrideSceneBuilder>(this->state->scene.get(), resolution);
            std::vector<std::string> filenames{spectra_scene.scene_path_text};
            pbrt::ParseFiles(this->state->builder.get(), filenames);
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraPbrtBackendScene::unload_noexcept() noexcept {
        if (this->state == nullptr) return;
        this->state->builder.reset();
        this->state->scene.reset();
    }

    [[nodiscard]] void* SpectraPbrtBackendScene::native_basic_scene() {
        if (this->state == nullptr) throw std::runtime_error("PBRT backend scene state is null");
        if (this->state->scene == nullptr) throw std::runtime_error("PBRT backend scene is not loaded");
        return this->state->scene.get();
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
    struct SpectraPbrtInteractiveState {
        std::unique_ptr<pbrt::WavefrontPathIntegrator> integrator{};
        pbrt::Bounds2i pixel_bounds{};
        pbrt::Vector2i resolution{};
        pbrt::Transform render_from_camera{};
        pbrt::Transform camera_from_render{};
        pbrt::Transform camera_from_world{};
    };

    SpectraPbrtInteractiveSession::SpectraPbrtInteractiveSession(const SpectraScene& spectra_scene, SpectraPbrtBackendScene& backend_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : scene_path {spectra_scene.scene_path} {
            try {
                this->pbrt_state = std::make_unique<SpectraPbrtInteractiveState>();
                pbrt::BasicScene* native_backend_scene = static_cast<pbrt::BasicScene*>(backend_scene.native_basic_scene());
                if (native_backend_scene == nullptr) throw std::runtime_error("PBRT backend scene native pointer is null");
                if (this->scene_path.empty()) throw std::runtime_error("PBRT scene path is empty");
                if (!std::filesystem::exists(this->scene_path)) throw std::runtime_error(std::string{"PBRT scene does not exist: "} + this->scene_path.string());
                if (spectra_scene.pbrt_directives.empty()) throw std::runtime_error("Spectra scene has no PBRT parser directives");
                if (frame_count == 0) throw std::runtime_error("PBRT interactive requires at least one frame in flight");
                this->physical_device = &physical_device;
                this->device          = &device;
                this->frame_count     = frame_count;

                this->pbrt_state->integrator = std::make_unique<pbrt::WavefrontPathIntegrator>(&pbrt::CUDATrackedMemoryResource::singleton, *native_backend_scene);
#ifdef PBRT_BUILD_GPU_RENDERER
                if (pbrt::Options != nullptr && pbrt::Options->useGPU) this->pbrt_state->integrator->PrefetchGPUAllocations();
#endif
                this->pbrt_state->pixel_bounds = this->pbrt_state->integrator->film.PixelBounds();
                this->pbrt_state->resolution   = this->pbrt_state->pixel_bounds.Diagonal();
                if (this->pbrt_state->resolution.x <= 0 || this->pbrt_state->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive");
                this->max_samples = this->pbrt_state->integrator->sampler.SamplesPerPixel();
                if (this->max_samples <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
                this->target_samples = this->max_samples;
                this->pbrt_state->integrator->RenderSample(this->pbrt_state->pixel_bounds, pbrt::Transform{}, this->sample_index);
                ++this->sample_index;
                pbrt::GPUWait();

                this->pbrt_state->render_from_camera = this->pbrt_state->integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
                this->pbrt_state->camera_from_render = pbrt::Inverse(this->pbrt_state->render_from_camera);
                this->pbrt_state->camera_from_world  = this->pbrt_state->integrator->camera.GetCameraTransform().CameraFromWorld(this->pbrt_state->integrator->camera.SampleTime(0.0f));
                const pbrt::Bounds3f scene_bounds = this->pbrt_state->integrator->aggregate->Bounds();
                this->initial_move_scale          = pbrt::Length(scene_bounds.Diagonal()) / 1000.0f;
                if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT scene bounds must define a positive interactive move scale");
                const pbrt::Transform world_from_render = pbrt::Inverse(this->pbrt_state->render_from_camera * this->pbrt_state->camera_from_world);
                std::array<float, 3> world_bounds_min{};
                std::array<float, 3> world_bounds_max{};
                bool has_world_bounds = false;
                for (const float x : std::array<float, 2>{scene_bounds.pMin.x, scene_bounds.pMax.x}) {
                    for (const float y : std::array<float, 2>{scene_bounds.pMin.y, scene_bounds.pMax.y}) {
                        for (const float z : std::array<float, 2>{scene_bounds.pMin.z, scene_bounds.pMax.z}) {
                            const pbrt::Point3f corner_world = world_from_render(pbrt::Point3f{x, y, z});
                            const std::array<float, 3> point{corner_world.x, corner_world.y, corner_world.z};
                            for (const float value : point) {
                                if (!std::isfinite(value)) throw std::runtime_error("PBRT scene focus bounds contain a non-finite value");
                            }
                            if (!has_world_bounds) {
                                world_bounds_min = point;
                                world_bounds_max = point;
                                has_world_bounds = true;
                            } else {
                                for (std::size_t axis = 0; axis < 3; ++axis) {
                                    world_bounds_min[axis] = std::min(world_bounds_min[axis], point[axis]);
                                    world_bounds_max[axis] = std::max(world_bounds_max[axis], point[axis]);
                                }
                            }
                        }
                    }
                }
                if (!has_world_bounds) throw std::runtime_error("PBRT scene focus bounds are unavailable");
                this->initial_focus_bounds = {world_bounds_min[0], world_bounds_min[1], world_bounds_min[2], world_bounds_max[0], world_bounds_max[1], world_bounds_max[2]};

                this->validate_cuda_vulkan_device(physical_device);
                this->create_frame_resources(physical_device, device, frame_count);
                this->create_imgui_descriptors();
            } catch (...) {
                this->destroy_resources_noexcept();
                throw;
            }
        }


    SpectraPbrtInteractiveSession::~SpectraPbrtInteractiveSession() noexcept {
            this->destroy_resources_noexcept();
        }


    void SpectraPbrtInteractiveSession::destroy_resources_noexcept() noexcept {
            try {
                if (this->device != nullptr) this->device->waitIdle();
                if (pbrt::Options != nullptr && pbrt::Options->useGPU) pbrt::GPUWait();
            } catch (...) {
            }
            this->destroy_frame_resources_noexcept();
            this->pbrt_state->integrator.reset();
        }


    [[nodiscard]] int SpectraPbrtInteractiveSession::current_sample() const {
            if (this->reset_requested) return 0;
            return this->sample_index;
        }


    [[nodiscard]] int SpectraPbrtInteractiveSession::sampler_sample_count() const {
            return this->max_samples;
        }


    [[nodiscard]] int SpectraPbrtInteractiveSession::target_sample_count() const {
            return this->target_samples;
        }


    [[nodiscard]] float SpectraPbrtInteractiveSession::current_exposure() const {
            return static_cast<float>(this->exposure);
        }


    [[nodiscard]] float SpectraPbrtInteractiveSession::camera_initial_move_scale() const {
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("PBRT interactive camera initial move scale must be positive");
            return static_cast<float>(this->initial_move_scale);
        }


    [[nodiscard]] std::array<float, 6> SpectraPbrtInteractiveSession::camera_initial_focus_bounds() const {
            for (const float value : this->initial_focus_bounds) {
                if (!std::isfinite(value)) throw std::runtime_error("PBRT interactive camera initial focus bounds contain a non-finite value");
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (this->initial_focus_bounds[axis] > this->initial_focus_bounds[axis + 3]) throw std::runtime_error("PBRT interactive camera initial focus bounds are invalid");
            }
            return this->initial_focus_bounds;
        }


    [[nodiscard]] std::array<int, 2> SpectraPbrtInteractiveSession::film_resolution() const {
            if (this->pbrt_state->resolution.x <= 0 || this->pbrt_state->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before metadata is queried");
            return {this->pbrt_state->resolution.x, this->pbrt_state->resolution.y};
        }


    [[nodiscard]] std::array<float, 16> SpectraPbrtInteractiveSession::camera_from_world_matrix() const {
            return matrix_array_from_transform(this->pbrt_state->camera_from_world);
        }


    [[nodiscard]] std::uint64_t SpectraPbrtInteractiveSession::film_pixel_count() const {
            if (this->pbrt_state->resolution.x <= 0 || this->pbrt_state->resolution.y <= 0) throw std::runtime_error("PBRT film resolution must be positive before statistics are queried");
            return static_cast<std::uint64_t>(this->pbrt_state->resolution.x) * static_cast<std::uint64_t>(this->pbrt_state->resolution.y);
        }


    [[nodiscard]] float SpectraPbrtInteractiveSession::completion_ratio() const {
            if (this->target_samples <= 0) throw std::runtime_error("PBRT target sample count must be positive before statistics are queried");
            const int visible_sample = this->current_sample();
            if (visible_sample < 0 || visible_sample > this->target_samples) throw std::runtime_error("PBRT visible sample count is outside the target sample range");
            return static_cast<float>(visible_sample) / static_cast<float>(this->target_samples);
        }


    [[nodiscard]] VkDescriptorSet SpectraPbrtInteractiveSession::active_descriptor() const {
            if (this->frames.empty()) return VK_NULL_HANDLE;
            return this->frames.at(this->active_frame_index).imgui_descriptor;
        }


    [[nodiscard]] vk::Semaphore SpectraPbrtInteractiveSession::active_cuda_complete_semaphore() const {
            if (this->frames.empty()) throw std::runtime_error("PBRT completion semaphore requested without frame resources");
            return *this->frames.at(this->active_frame_index).cuda_complete_semaphore;
        }


    void SpectraPbrtInteractiveSession::set_target_sample_count(const int target_sample_count) {
            if (target_sample_count < 1 || target_sample_count > this->max_samples) throw std::runtime_error("PBRT target sample count is outside the sampler SPP range");
            if (target_sample_count == this->target_samples) return;
            this->target_samples = target_sample_count;
            this->request_reset_accumulation();
        }


    void SpectraPbrtInteractiveSession::set_exposure(const float value) {
            if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("PBRT exposure must be in [0.001, 1000]");
            this->exposure = value;
        }


    void SpectraPbrtInteractiveSession::request_reset_accumulation() {
            this->reset_requested = true;
        }


    void SpectraPbrtInteractiveSession::release_imgui_descriptors() noexcept {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                    frame.imgui_descriptor = VK_NULL_HANDLE;
                }
            }
        }


    void SpectraPbrtInteractiveSession::create_imgui_descriptors() {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("PBRT interactive ImGui descriptor is already allocated");
                frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate ImGui descriptor for PBRT interactive image");
            }
        }


    void SpectraPbrtInteractiveSession::destroy_frame_resources_noexcept() noexcept {
            this->release_imgui_descriptors();
            for (FrameResource& frame : this->frames) {
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
            this->frames.clear();
            this->active_frame_index = 0;
        }


    [[nodiscard]] SpectraPbrtInteractiveSession::RenderFrameResult SpectraPbrtInteractiveSession::render_frame(const std::uint32_t frame_index, const std::array<float, 16>& moving_from_camera_matrix) {
            if (frame_index >= this->frames.size()) throw std::runtime_error("PBRT interactive frame index is out of range");
            this->active_frame_index = frame_index;
            RenderFrameResult result{};
            const pbrt::Transform moving_from_camera = transform_from_matrix_array(moving_from_camera_matrix);
            const pbrt::Transform camera_motion = this->pbrt_state->render_from_camera * moving_from_camera * this->pbrt_state->camera_from_render;
            if (this->reset_requested) {
                if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("PBRT interactive Vulkan handles are not available for reset");
                this->device->waitIdle();
                this->destroy_frame_resources_noexcept();
                this->pbrt_state->integrator->ResetFilm(this->pbrt_state->pixel_bounds);
                pbrt::GPUWait();
                this->sample_index    = 0;
                this->reset_requested = false;
                this->pbrt_state->integrator->RenderSample(this->pbrt_state->pixel_bounds, camera_motion, this->sample_index);
                ++this->sample_index;
                pbrt::GPUWait();
                this->create_frame_resources(*this->physical_device, *this->device, this->frame_count);
                this->create_imgui_descriptors();
                this->active_frame_index = frame_index;
                result.rendered_sample    = true;
                result.sample_pixels      = this->film_pixel_count() * static_cast<std::uint64_t>(this->sample_index);
                result.reset_accumulation = true;
            } else if (this->sample_index < this->target_samples) {
                this->pbrt_state->integrator->RenderSample(this->pbrt_state->pixel_bounds, camera_motion, this->sample_index);
                ++this->sample_index;
                result.rendered_sample = true;
                result.sample_pixels   = this->film_pixel_count();
            }
            FrameResource& output_frame = this->frames.at(frame_index);
            this->pbrt_state->integrator->UpdateFramebufferFromFilm(this->pbrt_state->pixel_bounds, this->exposure, output_frame.cuda_pixels);

            cudaExternalSemaphoreSignalParams signal_params{};
            CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
            return result;
        }


    void SpectraPbrtInteractiveSession::record_copy(const vk::raii::CommandBuffer& command_buffer) {
            FrameResource& frame = this->frames.at(this->active_frame_index);
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
                {static_cast<std::uint32_t>(this->pbrt_state->resolution.x), static_cast<std::uint32_t>(this->pbrt_state->resolution.y), 1},
            };
            command_buffer.copyBufferToImage(*frame.interop_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, copy_region);

            transition_image_layout(command_buffer, *frame.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame.image_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }


    void SpectraPbrtInteractiveSession::validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const {
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


    void SpectraPbrtInteractiveSession::create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
            const vk::FormatProperties format_properties = physical_device.getFormatProperties(this->display_format);
            constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
            if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

            const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(this->pbrt_state->resolution.x) * static_cast<vk::DeviceSize>(this->pbrt_state->resolution.y);
            if (rgba_bytes == 0) throw std::runtime_error("PBRT interactive interop buffer cannot be zero bytes");
            this->frames.resize(frame_count);
            for (FrameResource& frame : this->frames) {
                this->create_interop_buffer(physical_device, device, frame, rgba_bytes);
                this->create_cuda_complete_semaphore(device, frame);
                this->create_display_image(physical_device, device, frame, this->display_format);
            }
        }


    void SpectraPbrtInteractiveSession::create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, SpectraPbrtInteractiveSession::FrameResource& frame, const vk::DeviceSize rgba_bytes) {
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


    void SpectraPbrtInteractiveSession::create_cuda_complete_semaphore(const vk::raii::Device& device, SpectraPbrtInteractiveSession::FrameResource& frame) {
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


    void SpectraPbrtInteractiveSession::create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, SpectraPbrtInteractiveSession::FrameResource& frame, const vk::Format display_format) {
            const vk::ImageCreateInfo image_create_info{
                {},
                vk::ImageType::e2D,
                display_format,
                vk::Extent3D{static_cast<std::uint32_t>(this->pbrt_state->resolution.x), static_cast<std::uint32_t>(this->pbrt_state->resolution.y), 1},
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
                display_format,
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

} // namespace xayah

namespace xayah {
    SpectraVulkanRasterizer::SpectraVulkanRasterizer(const SpectraScene& scene, const SpectraRasterScene& raster_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::Queue& graphics_queue, const vk::raii::CommandPool& command_pool, const std::uint32_t frame_count) : scene {&scene}, raster_scene{&raster_scene}, physical_device{&physical_device}, device{&device}, graphics_queue{&graphics_queue}, command_pool{&command_pool} {
            try {
                if (frame_count == 0) throw std::runtime_error("Vulkan rasterizer requires at least one frame in flight");
                if (scene.film_resolution[0] <= 0 || scene.film_resolution[1] <= 0) throw std::runtime_error("Vulkan rasterizer requires positive PBRT film resolution metadata");
                this->extent = vk::Extent2D{static_cast<std::uint32_t>(scene.film_resolution[0]), static_cast<std::uint32_t>(scene.film_resolution[1])};
                this->validate_formats();
                this->create_scene_buffers();
                this->create_frame_resources(frame_count);
                this->create_descriptors();
                this->create_pipeline();
                this->create_imgui_descriptors();
            } catch (...) {
                this->destroy_resources_noexcept();
                throw;
            }
        }


    SpectraVulkanRasterizer::~SpectraVulkanRasterizer() noexcept {
            this->destroy_resources_noexcept();
        }


    [[nodiscard]] VkDescriptorSet SpectraVulkanRasterizer::active_descriptor() const {
            if (this->frames.empty()) return VK_NULL_HANDLE;
            return this->frames.at(this->active_frame_index).imgui_descriptor;
        }


    [[nodiscard]] float SpectraVulkanRasterizer::camera_initial_move_scale() const {
            if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("Vulkan rasterizer camera initial move scale must be positive");
            return this->initial_move_scale;
        }


    [[nodiscard]] std::array<float, 6> SpectraVulkanRasterizer::camera_initial_focus_bounds() const {
            if (!this->has_initial_focus_bounds) throw std::runtime_error("Vulkan rasterizer camera initial focus bounds are unavailable");
            for (const float value : this->initial_focus_bounds) {
                if (!std::isfinite(value)) throw std::runtime_error("Vulkan rasterizer camera initial focus bounds contain a non-finite value");
            }
            for (std::size_t axis = 0; axis < 3; ++axis) {
                if (this->initial_focus_bounds[axis] > this->initial_focus_bounds[axis + 3]) throw std::runtime_error("Vulkan rasterizer camera initial focus bounds are invalid");
            }
            return this->initial_focus_bounds;
        }


    void SpectraVulkanRasterizer::render_frame(const std::uint32_t frame_index) {
            if (frame_index >= this->frames.size()) throw std::runtime_error("Vulkan rasterizer frame index is out of range");
            this->active_frame_index = frame_index;
        }


    void SpectraVulkanRasterizer::record_draw(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) {
            if (this->scene == nullptr || this->raster_scene == nullptr) throw std::runtime_error("Vulkan rasterizer cannot record without scene data");
            FrameResource& frame = this->frames.at(this->active_frame_index);
            const vk::PipelineStageFlags2 color_src_stage = frame.color_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader;
            const vk::AccessFlags2 color_src_access       = frame.color_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead;
            transition_image_layout(command_buffer, *frame.color_image, frame.color_layout, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor, color_src_stage, color_src_access, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
            frame.color_layout = vk::ImageLayout::eColorAttachmentOptimal;
            if (frame.depth_layout == vk::ImageLayout::eUndefined) {
                transition_image_layout(command_buffer, *frame.depth_image, frame.depth_layout, vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::ImageAspectFlagBits::eDepth, vk::PipelineStageFlagBits2::eNone, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
                frame.depth_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
            }

            constexpr std::array<float, 4> clear_color{0.025f, 0.025f, 0.028f, 1.0f};
            const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
            const vk::RenderingAttachmentInfo color_attachment{
                *frame.color_image_view,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                {},
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                color_clear_value,
            };
            const vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0}};
            const vk::RenderingAttachmentInfo depth_attachment{
                *frame.depth_image_view,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ResolveModeFlagBits::eNone,
                {},
                vk::ImageLayout::eUndefined,
                vk::AttachmentLoadOp::eClear,
                vk::AttachmentStoreOp::eStore,
                depth_clear_value,
            };
            const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->extent}, 1, 0, 1, &color_attachment, &depth_attachment, nullptr};
            command_buffer.beginRendering(rendering_info);
            if (this->draw_count > 0) this->record_geometry(command_buffer, camera_from_world, moving_from_camera);
            command_buffer.endRendering();

            transition_image_layout(command_buffer, *frame.color_image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame.color_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }


    void SpectraVulkanRasterizer::release_imgui_descriptors() noexcept {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                    ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                    frame.imgui_descriptor = VK_NULL_HANDLE;
                }
            }
        }


    void SpectraVulkanRasterizer::create_imgui_descriptors() {
            for (FrameResource& frame : this->frames) {
                if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Vulkan rasterizer ImGui descriptor is already allocated");
                frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.color_sampler), static_cast<VkImageView>(*frame.color_image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate ImGui descriptor for Vulkan rasterizer image");
            }
        }


    void SpectraVulkanRasterizer::destroy_resources_noexcept() noexcept {
            try {
                if (this->device != nullptr) this->device->waitIdle();
            } catch (...) {
            }
            this->release_imgui_descriptors();
            this->frames.clear();
            this->pipeline = nullptr;
            this->fragment_shader = nullptr;
            this->vertex_shader = nullptr;
            this->pipeline_layout = nullptr;
            this->descriptor_sets.clear();
            this->descriptor_pool = nullptr;
            this->descriptor_set_layout = nullptr;
            this->material_buffer = {};
            this->draw_buffer = {};
            this->index_buffer = {};
            this->vertex_buffer = {};
            this->active_frame_index = 0;
        }


    [[nodiscard]] std::size_t SpectraVulkanRasterizer::vertex_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->vertices.size();
        }


    [[nodiscard]] std::size_t SpectraVulkanRasterizer::index_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->indices.size();
        }


    [[nodiscard]] std::size_t SpectraVulkanRasterizer::material_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->materials.size();
        }


    [[nodiscard]] std::size_t SpectraVulkanRasterizer::diagnostic_count() const {
            if (this->raster_scene == nullptr) return 0;
            return this->raster_scene->diagnostics.size();
        }


    void SpectraVulkanRasterizer::validate_formats() const {
            const vk::FormatProperties color_properties = this->physical_device->getFormatProperties(this->color_format);
            constexpr vk::FormatFeatureFlags color_required = vk::FormatFeatureFlagBits::eColorAttachment | vk::FormatFeatureFlagBits::eSampledImage;
            if ((color_properties.optimalTilingFeatures & color_required) != color_required) throw std::runtime_error("Vulkan device does not support sampled color attachment R8G8B8A8_UNORM images");
            const vk::FormatProperties depth_properties = this->physical_device->getFormatProperties(this->depth_format);
            if ((depth_properties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment) != vk::FormatFeatureFlagBits::eDepthStencilAttachment) throw std::runtime_error("Vulkan device does not support D32_SFLOAT depth attachment images");
        }


    [[nodiscard]] SpectraVulkanRasterizer::BufferResource SpectraVulkanRasterizer::create_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags memory_properties) const {
            if (size == 0) throw std::runtime_error("Vulkan rasterizer cannot create a zero-sized buffer");
            BufferResource resource{};
            const vk::BufferCreateInfo buffer_create_info{{}, size, usage, vk::SharingMode::eExclusive};
            resource.buffer = vk::raii::Buffer{*this->device, buffer_create_info};
            const vk::MemoryRequirements memory_requirements = resource.buffer.getMemoryRequirements();
            const std::uint32_t memory_type = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, memory_properties);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            resource.memory = vk::raii::DeviceMemory{*this->device, allocate_info};
            resource.buffer.bindMemory(*resource.memory, 0);
            resource.size = size;
            return resource;
        }


    void SpectraVulkanRasterizer::submit_upload(const vk::raii::Buffer& staging_buffer, const vk::raii::Buffer& destination_buffer, const vk::DeviceSize size) const {
            const vk::CommandBufferAllocateInfo allocate_info{**this->command_pool, vk::CommandBufferLevel::ePrimary, 1};
            vk::raii::CommandBuffers command_buffers{*this->device, allocate_info};
            const vk::raii::CommandBuffer& command_buffer = command_buffers.front();
            constexpr vk::CommandBufferBeginInfo begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
            command_buffer.begin(begin_info);
            const vk::BufferCopy copy_region{0, 0, size};
            command_buffer.copyBuffer(*staging_buffer, *destination_buffer, copy_region);
            const vk::BufferMemoryBarrier2 buffer_barrier{
                vk::PipelineStageFlagBits2::eTransfer,
                vk::AccessFlagBits2::eTransferWrite,
                vk::PipelineStageFlagBits2::eVertexInput | vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eVertexAttributeRead | vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eShaderStorageRead,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                *destination_buffer,
                0,
                size,
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_barrier, 0, nullptr};
            command_buffer.pipelineBarrier2(dependency_info);
            command_buffer.end();

            const vk::CommandBufferSubmitInfo command_buffer_submit_info{*command_buffer};
            const vk::SubmitInfo2 submit_info{{}, 0, nullptr, 1, &command_buffer_submit_info, 0, nullptr};
            this->graphics_queue->submit2(submit_info, nullptr);
            this->graphics_queue->waitIdle();
        }


    template <typename T>
    [[nodiscard]] SpectraVulkanRasterizer::BufferResource SpectraVulkanRasterizer::upload_vector_buffer(const std::vector<T>& values, const vk::BufferUsageFlags usage) const {
            if (values.empty()) throw std::runtime_error("Vulkan rasterizer cannot upload an empty typed buffer");
            const vk::DeviceSize byte_size = static_cast<vk::DeviceSize>(sizeof(T)) * static_cast<vk::DeviceSize>(values.size());
            BufferResource staging = this->create_buffer(byte_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
            void* mapped = staging.memory.mapMemory(0, byte_size);
            std::memcpy(mapped, values.data(), static_cast<std::size_t>(byte_size));
            staging.memory.unmapMemory();
            BufferResource destination = this->create_buffer(byte_size, usage | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal);
            this->submit_upload(staging.buffer, destination.buffer, byte_size);
            return destination;
        }


    [[nodiscard]] std::vector<SpectraVulkanRasterizer::SpectraRasterMaterialGpu> SpectraVulkanRasterizer::build_gpu_materials() const {
            std::vector<SpectraRasterMaterialGpu> materials{};
            materials.reserve(std::max<std::size_t>(this->raster_scene->materials.size(), 1));
            for (const SpectraRasterMaterial& material : this->raster_scene->materials) {
                SpectraRasterMaterialGpu gpu_material{};
                gpu_material.base_color_roughness = {material.base_color[0], material.base_color[1], material.base_color[2], material.roughness};
                materials.push_back(gpu_material);
            }
            if (materials.empty()) materials.push_back(SpectraRasterMaterialGpu{{1.0f, 1.0f, 1.0f, 1.0f}});
            return materials;
        }


    [[nodiscard]] std::vector<SpectraVulkanRasterizer::SpectraRasterDrawGpu> SpectraVulkanRasterizer::build_gpu_draws() {
            std::vector<SpectraRasterDrawGpu> draws{};
            draws.reserve(std::max<std::size_t>(this->raster_scene->draws.size(), 1));
            bool has_bounds = false;
            std::array<float, 3> bounds_min{};
            std::array<float, 3> bounds_max{};
            this->draw_count = this->raster_scene->draws.size();
            this->triangle_count = 0;
            for (const SpectraRasterDraw& draw : this->raster_scene->draws) {
                if (draw.geometry_index >= this->raster_scene->geometries.size()) throw std::runtime_error("Raster draw references a geometry index outside SpectraRasterScene");
                if (draw.material_index >= this->raster_scene->materials.size()) throw std::runtime_error("Raster draw references a material index outside SpectraRasterScene");
                const SpectraRasterGeometry& geometry = this->raster_scene->geometries[draw.geometry_index];
                if (geometry.first_index + geometry.index_count > this->raster_scene->indices.size()) throw std::runtime_error("Raster geometry index range is outside SpectraRasterScene");
                if (geometry.first_vertex + geometry.vertex_count > this->raster_scene->vertices.size()) throw std::runtime_error("Raster geometry vertex range is outside SpectraRasterScene");

                SpectraRasterDrawGpu gpu_draw{};
                gpu_draw.object_from_local = draw.transform;
                gpu_draw.normal_from_local = normal_from_local_matrix_array(draw.transform);
                if (draw.reverse_orientation) {
                    for (const std::size_t offset : std::array<std::size_t, 9>{0, 1, 2, 4, 5, 6, 8, 9, 10}) gpu_draw.normal_from_local[offset] = -gpu_draw.normal_from_local[offset];
                }
                gpu_draw.material_index = static_cast<std::uint32_t>(draw.material_index);
                draws.push_back(gpu_draw);
                this->triangle_count += geometry.index_count / 3u;

                for (std::size_t vertex_index = geometry.first_vertex; vertex_index < geometry.first_vertex + geometry.vertex_count; ++vertex_index) {
                    const std::array<float, 3> point = transform_point_array(draw.transform, this->raster_scene->vertices[vertex_index].position);
                    if (!has_bounds) {
                        bounds_min = point;
                        bounds_max = point;
                        has_bounds = true;
                    } else {
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            bounds_min[axis] = std::min(bounds_min[axis], point[axis]);
                            bounds_max[axis] = std::max(bounds_max[axis], point[axis]);
                        }
                    }
                }
            }
            if (draws.empty()) draws.push_back(SpectraRasterDrawGpu{identity_matrix_array(), identity_matrix_array(), 0, {}});
            if (has_bounds) {
                const float dx = bounds_max[0] - bounds_min[0];
                const float dy = bounds_max[1] - bounds_min[1];
                const float dz = bounds_max[2] - bounds_min[2];
                this->initial_move_scale = std::sqrt(dx * dx + dy * dy + dz * dz) / 1000.0f;
                if (!(this->initial_move_scale > 0.0f)) throw std::runtime_error("Raster scene bounds must define a positive interactive move scale");
                this->initial_focus_bounds     = {bounds_min[0], bounds_min[1], bounds_min[2], bounds_max[0], bounds_max[1], bounds_max[2]};
                this->has_initial_focus_bounds = true;
            }
            return draws;
        }


    void SpectraVulkanRasterizer::create_scene_buffers() {
            if (this->raster_scene == nullptr) throw std::runtime_error("Cannot create Vulkan rasterizer buffers without SpectraRasterScene");
            const std::vector<SpectraRasterMaterialGpu> gpu_materials = this->build_gpu_materials();
            const std::vector<SpectraRasterDrawGpu> gpu_draws = this->build_gpu_draws();
            this->material_buffer = this->upload_vector_buffer(gpu_materials, vk::BufferUsageFlagBits::eStorageBuffer);
            this->draw_buffer = this->upload_vector_buffer(gpu_draws, vk::BufferUsageFlagBits::eStorageBuffer);
            if (!this->raster_scene->vertices.empty()) this->vertex_buffer = this->upload_vector_buffer(this->raster_scene->vertices, vk::BufferUsageFlagBits::eVertexBuffer);
            if (!this->raster_scene->indices.empty()) this->index_buffer = this->upload_vector_buffer(this->raster_scene->indices, vk::BufferUsageFlagBits::eIndexBuffer);
        }


    void SpectraVulkanRasterizer::create_image_resource(SpectraVulkanRasterizer::FrameResource& frame, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect, vk::raii::DeviceMemory& memory, vk::raii::Image& image, vk::raii::ImageView& image_view) const {
            const vk::ImageCreateInfo image_create_info{
                {},
                vk::ImageType::e2D,
                format,
                vk::Extent3D{this->extent.width, this->extent.height, 1},
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                usage,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined,
            };
            image = vk::raii::Image{*this->device, image_create_info};
            const vk::MemoryRequirements memory_requirements = image.getMemoryRequirements();
            const std::uint32_t memory_type = find_memory_type_index(*this->physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
            const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type};
            memory = vk::raii::DeviceMemory{*this->device, allocate_info};
            image.bindMemory(*memory, 0);

            const vk::ImageViewCreateInfo image_view_create_info{{}, *image, vk::ImageViewType::e2D, format, {}, {aspect, 0, 1, 0, 1}};
            image_view = vk::raii::ImageView{*this->device, image_view_create_info};
        }


    void SpectraVulkanRasterizer::create_frame_resources(const std::uint32_t frame_count) {
            this->frames.resize(frame_count);
            for (FrameResource& frame : this->frames) {
                this->create_image_resource(frame, this->color_format, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor, frame.color_memory, frame.color_image, frame.color_image_view);
                this->create_image_resource(frame, this->depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth, frame.depth_memory, frame.depth_image, frame.depth_image_view);
                const vk::SamplerCreateInfo sampler_create_info{
                    {},
                    vk::Filter::eLinear,
                    vk::Filter::eLinear,
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
                frame.color_sampler = vk::raii::Sampler{*this->device, sampler_create_info};
            }
        }


    void SpectraVulkanRasterizer::create_descriptors() {
            const std::array descriptor_bindings{
                vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
                vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eFragment},
            };
            const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_bindings.size()), descriptor_bindings.data()};
            this->descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->device, descriptor_set_layout_create_info};

            const std::array descriptor_pool_sizes{
                vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 2},
            };
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->descriptor_pool = vk::raii::DescriptorPool{*this->device, descriptor_pool_create_info};
            const vk::DescriptorSetLayout descriptor_set_layout_handle = *this->descriptor_set_layout;
            const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->descriptor_pool, 1, &descriptor_set_layout_handle};
            this->descriptor_sets = vk::raii::DescriptorSets{*this->device, descriptor_set_allocate_info};
            if (this->descriptor_sets.size() != 1) throw std::runtime_error("Vulkan rasterizer failed to allocate descriptor set");

            const vk::DescriptorBufferInfo draw_buffer_info{*this->draw_buffer.buffer, 0, this->draw_buffer.size};
            const vk::DescriptorBufferInfo material_buffer_info{*this->material_buffer.buffer, 0, this->material_buffer.size};
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*this->descriptor_sets.front(), 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &draw_buffer_info},
                vk::WriteDescriptorSet{*this->descriptor_sets.front(), 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &material_buffer_info},
            };
            this->device->updateDescriptorSets(descriptor_writes, {});
        }


    void SpectraVulkanRasterizer::create_pipeline() {
            const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, xayah::generated::spectra_raster_vertex_spirv_size, xayah::generated::spectra_raster_vertex_spirv.data()};
            const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, xayah::generated::spectra_raster_fragment_spirv_size, xayah::generated::spectra_raster_fragment_spirv.data()};
            this->vertex_shader = vk::raii::ShaderModule{*this->device, vertex_shader_create_info};
            this->fragment_shader = vk::raii::ShaderModule{*this->device, fragment_shader_create_info};

            const vk::DescriptorSetLayout descriptor_set_layout_handle = *this->descriptor_set_layout;
            const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, static_cast<std::uint32_t>(sizeof(SpectraRasterPushConstants))};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_set_layout_handle, 1, &push_constant_range};
            this->pipeline_layout = vk::raii::PipelineLayout{*this->device, pipeline_layout_create_info};

            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *this->vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *this->fragment_shader, "main"},
            };
            const vk::VertexInputBindingDescription vertex_binding{0, static_cast<std::uint32_t>(sizeof(SpectraRasterVertex)), vk::VertexInputRate::eVertex};
            const std::array vertex_attributes{
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, 12},
            };
            const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
            const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
            const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1, nullptr, 1, nullptr};
            const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
            const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLess, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
            const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
            const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{0, 1, &this->color_format, this->depth_format, vk::Format::eUndefined};

            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            pipeline_create_info.setStages(shader_stages);
            pipeline_create_info.setPVertexInputState(&vertex_input_state);
            pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            pipeline_create_info.setPViewportState(&viewport_state);
            pipeline_create_info.setPRasterizationState(&rasterization_state);
            pipeline_create_info.setPMultisampleState(&multisample_state);
            pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
            pipeline_create_info.setPColorBlendState(&color_blend_state);
            pipeline_create_info.setPDynamicState(&dynamic_state);
            pipeline_create_info.setLayout(*this->pipeline_layout);
            this->pipeline = vk::raii::Pipeline{*this->device, nullptr, pipeline_create_info};
        }


    void SpectraVulkanRasterizer::record_geometry(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) const {
            if (!*this->pipeline || !*this->pipeline_layout || this->descriptor_sets.empty()) throw std::runtime_error("Vulkan rasterizer pipeline is not ready");
            if (!*this->vertex_buffer.buffer || !*this->index_buffer.buffer) throw std::runtime_error("Vulkan rasterizer draw list is non-empty but vertex/index buffers are missing");
            const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->extent.width), static_cast<float>(this->extent.height), 0.0f, 1.0f};
            const vk::Rect2D scissor{{0, 0}, this->extent};
            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->pipeline);
            command_buffer.setViewport(0, viewport);
            command_buffer.setScissor(0, scissor);
            const vk::DescriptorSet descriptor_set = *this->descriptor_sets.front();
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->pipeline_layout, 0, descriptor_set, {});
            const std::array vertex_buffers{*this->vertex_buffer.buffer};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            command_buffer.bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            command_buffer.bindIndexBuffer(*this->index_buffer.buffer, 0, vk::IndexType::eUint32);

            SpectraRasterPushConstants push_constants{};
            push_constants.view_projection = raster_view_projection_matrix(*this->scene, camera_from_world, moving_from_camera);
            for (std::size_t draw_index = 0; draw_index < this->raster_scene->draws.size(); ++draw_index) {
                const SpectraRasterDraw& draw = this->raster_scene->draws[draw_index];
                if (draw.geometry_index >= this->raster_scene->geometries.size()) throw std::runtime_error("Raster draw references a geometry index outside SpectraRasterScene while recording");
                const SpectraRasterGeometry& geometry = this->raster_scene->geometries[draw.geometry_index];
                if (draw_index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster draw index exceeds uint32 push-constant range");
                if (geometry.first_index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) || geometry.index_count > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster geometry index range exceeds Vulkan uint32 draw range");
                push_constants.draw_index = static_cast<std::uint32_t>(draw_index);
                command_buffer.pushConstants(*this->pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, static_cast<std::uint32_t>(sizeof(push_constants)), &push_constants);
                command_buffer.drawIndexed(static_cast<std::uint32_t>(geometry.index_count), 1, static_cast<std::uint32_t>(geometry.first_index), 0, 0);
            }
        }

} // namespace xayah

namespace {
    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string pbrt_parameter_count_text(const std::vector<xayah::SpectraPbrtParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const xayah::SpectraSceneRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    [[nodiscard]] std::string resolution_text(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) return "Pending";
        return std::format("{} x {}", resolution[0], resolution[1]);
    }

    [[nodiscard]] std::string positive_int_text(const int value) {
        if (value <= 0) return "Pending";
        return std::format("{}", value);
    }

    [[nodiscard]] const char* scene_texture_value_type_label(const xayah::SpectraSceneTextureValueType value_type) {
        switch (value_type) {
            case xayah::SpectraSceneTextureValueType::Unknown: return "Unknown";
            case xayah::SpectraSceneTextureValueType::Float: return "Float";
            case xayah::SpectraSceneTextureValueType::Spectrum: return "Spectrum";
        }
        throw std::runtime_error("Unknown Spectra scene texture value type");
    }

    void draw_scene_render_setting_row(const char* label, const xayah::SpectraSceneRenderSetting& setting) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const std::string setting_text = scene_render_setting_text(setting);
        if (setting.present) ImGui::TextWrapped("%s", setting_text.c_str());
        else ImGui::TextDisabled("%s", setting_text.c_str());
        ImGui::TableSetColumnIndex(2);
        if (setting.present) ImGui::TextWrapped("%s", scene_file_location_text(setting.location).c_str());
        else ImGui::TextDisabled("None");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(pbrt_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] const char* scene_unsupported_kind_label(const xayah::SpectraSceneUnsupportedFeatureKind kind) {
        switch (kind) {
            case xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform:
                return "Animated Transform";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium:
                return "Participating Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::VdbMedium:
                return "VDB Medium";
            case xayah::SpectraSceneUnsupportedFeatureKind::ProceduralTexture:
                return "Procedural Texture";
            case xayah::SpectraSceneUnsupportedFeatureKind::AreaLightInObjectDefinition:
                return "Area Light Instance Policy";
            case xayah::SpectraSceneUnsupportedFeatureKind::ParserAttribute:
                return "Parser Attribute";
        }
        throw std::runtime_error("Unknown Spectra scene unsupported feature kind");
    }

    [[nodiscard]] const char* raster_diagnostic_kind_label(const xayah::SpectraRasterDiagnosticKind kind) {
        switch (kind) {
            case xayah::SpectraRasterDiagnosticKind::UnsupportedShape: return "Unsupported Shape";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial: return "Unsupported Material";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedTexture: return "Unsupported Texture";
            case xayah::SpectraRasterDiagnosticKind::MissingMaterial: return "Missing Material";
            case xayah::SpectraRasterDiagnosticKind::InvalidMesh: return "Invalid Mesh";
            case xayah::SpectraRasterDiagnosticKind::MissingPlyFile: return "Missing PLY File";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform: return "Unsupported Animated Transform";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedObjectInstance: return "Unsupported Object Instance";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedAreaLight: return "Unsupported Area Light";
            case xayah::SpectraRasterDiagnosticKind::UnsupportedMediumBinding: return "Unsupported Medium Binding";
        }
        throw std::runtime_error("Unknown Spectra raster diagnostic kind");
    }

    [[nodiscard]] std::string scene_file_location_text(const xayah::SpectraPbrtFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

    [[nodiscard]] std::string scene_unsupported_source_text(const xayah::SpectraSceneUnsupportedFeature& feature) {
        if (feature.source_name.empty()) return feature.source_type;
        return std::format("{} {}", feature.source_type, feature.source_name);
    }

    [[nodiscard]] std::string raster_diagnostic_source_text(const xayah::SpectraRasterDiagnostic& diagnostic) {
        if (diagnostic.source_name.empty()) return diagnostic.source_type;
        return std::format("{} {}", diagnostic.source_type, diagnostic.source_name);
    }

} // namespace

namespace xayah {
    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) this->ui.camera_visible = !this->ui.camera_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) this->ui.scene_browser_visible = !this->ui.scene_browser_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) this->ui.settings_visible = !this->ui.settings_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) this->ui.inspector_visible = !this->ui.inspector_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) this->ui.environment_visible = !this->ui.environment_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) this->ui.tonemapper_visible = !this->ui.tonemapper_visible;
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(ICON_MS_PHOTO_CAMERA " Camera", "F1", &this->ui.camera_visible);
            ImGui::MenuItem(ICON_MS_ACCOUNT_TREE " Scene Browser", "F2", &this->ui.scene_browser_visible);
            ImGui::MenuItem(ICON_MS_SETTINGS " Settings", "F3", &this->ui.settings_visible);
            ImGui::MenuItem(ICON_MS_LIST_ALT " Inspector", "F4", &this->ui.inspector_visible);
            ImGui::MenuItem(ICON_MS_PUBLIC " Environment", "F5", &this->ui.environment_visible);
            ImGui::MenuItem(ICON_MS_TONALITY " Tonemapper", "F6", &this->ui.tonemapper_visible);
            ImGui::Separator();
            ImGui::MenuItem(ICON_MS_ANALYTICS " Statistics", nullptr, &this->ui.statistics_visible);
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }


    void Spectra::draw_menu_toolbar() {
        struct ToggleButton {
            const char* icon;
            const char* shortcut;
            bool* visible;
            const char* tooltip;
        };

        const std::array<ToggleButton, 6> toggles{{
            {ICON_MS_PHOTO_CAMERA, "F1", &this->ui.camera_visible, "Camera"},
            {ICON_MS_ACCOUNT_TREE, "F2", &this->ui.scene_browser_visible, "Scene Browser"},
            {ICON_MS_SETTINGS, "F3", &this->ui.settings_visible, "Settings"},
            {ICON_MS_LIST_ALT, "F4", &this->ui.inspector_visible, "Inspector"},
            {ICON_MS_PUBLIC, "F5", &this->ui.environment_visible, "Environment"},
            {ICON_MS_TONALITY, "F6", &this->ui.tonemapper_visible, "Tonemapper"},
        }};

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toggles.size()) * button_size + static_cast<float>(toggles.size() + 1) * 2.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 180.0f) return;

        ImGui::SameLine(window_width * 0.5f - total_width * 0.5f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (const ToggleButton& toggle : toggles) {
            ImGui::PushStyleColor(ImGuiCol_Button, *toggle.visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
            if (ImGui::Button(toggle.icon, ImVec2{button_size, button_size})) *toggle.visible = !*toggle.visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && toggle.shortcut != nullptr) ImGui::SetTooltip("Toggle %s Window (%s)", toggle.tooltip, toggle.shortcut);
            if (ImGui::IsItemHovered() && toggle.shortcut == nullptr) ImGui::SetTooltip("Toggle %s Window", toggle.tooltip);
            ImGui::SameLine(0.0f, 2.0f);
        }
    }


    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (main_viewport->WorkSize.x <= 640.0f || main_viewport->WorkSize.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        const ImVec4 dockspace_window_background     = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{dockspace_window_background.x, dockspace_window_background.y, dockspace_window_background.z, 0.0f});
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, dockspace_flags);
        ImGui::PopStyleColor();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->ui.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID left_id   = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.25f, nullptr, &center_id);
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.25f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.35f, nullptr, &center_id);
        if (left_id == 0 || right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGuiID left_bottom_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Down, 0.35f, nullptr, &left_id);
        ImGuiID inspector_id   = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.35f, nullptr, &right_id);
        if (left_bottom_id == 0 || inspector_id == 0 || left_id == 0 || right_id == 0) throw std::runtime_error("Failed to build Spectra side panels");

        ImGui::DockBuilderDockWindow("Viewport", center_id);
        ImGui::DockBuilderDockWindow("Camera", left_id);
        ImGui::DockBuilderDockWindow("Settings", left_id);
        ImGui::DockBuilderDockWindow("Tonemapper", left_bottom_id);
        ImGui::DockBuilderDockWindow("Environment", left_bottom_id);
        ImGui::DockBuilderDockWindow("Scene Browser", right_id);
        ImGui::DockBuilderDockWindow("Inspector", inspector_id);
        ImGui::DockBuilderDockWindow("Statistics", bottom_id);
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->ui.dock_layout_initialized = true;
    }


    void Spectra::draw_viewport_window() {
        constexpr ImGuiWindowFlags viewport_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        if (ImGui::Begin("Viewport", nullptr, viewport_window_flags)) {
            const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
            const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
            if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
            const ImGuiIO& io = ImGui::GetIO();
            if (!std::isfinite(io.DisplayFramebufferScale.x) || !std::isfinite(io.DisplayFramebufferScale.y) || !(io.DisplayFramebufferScale.x > 0.0f) || !(io.DisplayFramebufferScale.y > 0.0f)) throw std::runtime_error("ImGui framebuffer scale must be finite and positive");
            const std::array<int, 2> viewport_framebuffer_size{
                static_cast<int>(std::round(viewport_size.x * io.DisplayFramebufferScale.x)),
                static_cast<int>(std::round(viewport_size.y * io.DisplayFramebufferScale.y)),
            };
            if (viewport_framebuffer_size[0] <= 0 || viewport_framebuffer_size[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
            this->ui.viewport_known    = true;
            this->ui.viewport_position = {viewport_position.x, viewport_position.y};
            this->ui.viewport_size     = {viewport_size.x, viewport_size.y};
            this->ui.viewport_framebuffer_size = viewport_framebuffer_size;
            this->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
            this->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
            this->observe_viewport_render_resolution(viewport_framebuffer_size);
            if (this->renderers_ready()) {
                const VkDescriptorSet descriptor = this->active_viewport_descriptor();
                if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Active renderer viewport descriptor is null");
                const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
                ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
                ImGui::SetCursorScreenPos(viewport_position);
            } else if (this->spectra_scene != nullptr) {
                const char* pending_label = this->render_resolution_sync.rebuilding ? "Rebuilding renderer" : "Waiting for viewport resolution";
                const ImVec2 text_size = ImGui::CalcTextSize(pending_label);
                ImGui::SetCursorScreenPos(ImVec2{viewport_position.x + std::max(0.0f, (viewport_size.x - text_size.x) * 0.5f), viewport_position.y + std::max(0.0f, (viewport_size.y - text_size.y) * 0.5f)});
                ImGui::TextDisabled("%s", pending_label);
                ImGui::SetCursorScreenPos(viewport_position);
            }
            ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        } else {
            this->ui.viewport_known   = false;
            this->ui.viewport_hovered = false;
            this->ui.viewport_focused = false;
            this->ui.viewport_framebuffer_size = {0, 0};
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }


    void Spectra::draw_camera_window() {
        if (!this->ui.camera_visible) return;
        if (!ImGui::Begin("Camera", &this->ui.camera_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const ActiveRendererStatus renderer_status = this->active_renderer_status();

            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("PBRT Dirty", renderer_status.pathtracer_accumulation_dirty ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Camera Speed");
            ImGui::TableSetColumnIndex(1);
            float speed = this->camera.speed;
            const float drag_speed = std::max(std::abs(speed) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraSpeed", &speed, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_speed(speed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement speed in world units per second. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->camera.initialized || this->spectra_scene == nullptr);
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_scene_browser_window() {
        if (!this->ui.scene_browser_visible) return;
        if (!ImGui::Begin("Scene Browser", &this->ui.scene_browser_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Asset Info");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", this->spectra_scene->scene_label);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", this->spectra_scene->scene_path_text.c_str());
            draw_statistics_row("Active Renderer", this->active_renderer_label());
            draw_statistics_row("Film Resolution", resolution_text(this->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->spectra_scene->sampler_sample_count));
            draw_statistics_row("Directives", std::format("{}", this->spectra_scene->pbrt_directives.size()));
            draw_statistics_row("Shapes", std::format("{}", this->spectra_scene->shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->spectra_scene->materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->spectra_scene->textures.size()));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->spectra_scene->object_definitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->spectra_scene->object_instances.size()));
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            ImGui::EndTable();
        }

        if (!ImGui::BeginTabBar("SpectraSceneBrowserTabs")) {
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTabItem("Render Settings")) {
            if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
                ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                draw_scene_render_setting_row("Pixel Filter", this->spectra_scene->pixel_filter);
                draw_scene_render_setting_row("Film", this->spectra_scene->film);
                draw_scene_render_setting_row("Sampler", this->spectra_scene->sampler);
                draw_scene_render_setting_row("Accelerator", this->spectra_scene->accelerator);
                draw_scene_render_setting_row("Integrator", this->spectra_scene->integrator);
                draw_scene_render_setting_row("Camera", this->spectra_scene->camera);
                ImGui::EndTable();
            }
            if (this->raster_scene != nullptr) {
                ImGui::SeparatorText("Raster Scene");
                if (ImGui::BeginTable("SpectraRasterSceneSummary", 2, summary_table_flags)) {
                    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    draw_statistics_row("Vertices", std::format("{}", this->raster_scene->vertices.size()));
                    draw_statistics_row("Indices", std::format("{}", this->raster_scene->indices.size()));
                    draw_statistics_row("Triangles", std::format("{}", this->raster_scene->indices.size() / 3u));
                    draw_statistics_row("Geometries", std::format("{}", this->raster_scene->geometries.size()));
                    draw_statistics_row("Draws", std::format("{}", this->raster_scene->draws.size()));
                    draw_statistics_row("Materials", std::format("{}", this->raster_scene->materials.size()));
                    draw_statistics_row("Diagnostics", std::format("{}", this->raster_scene->diagnostics.size()));
                    ImGui::EndTable();
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shapes")) {
            if (this->spectra_scene->shapes.empty()) {
                ImGui::TextDisabled("No PBRT shapes recorded");
            } else if (ImGui::BeginTable("SpectraSceneShapes", 7, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Area Light", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const SpectraSceneShape& shape : this->spectra_scene->shapes) {
                    const std::string material_text = !shape.material_name.empty() ? shape.material_name : shape.material_index >= 0 ? std::format("#{}", shape.material_index) : "None";
                    const std::string media_text    = shape.inside_medium.empty() && shape.outside_medium.empty() ? "None" : std::format("{} / {}", optional_scene_text(shape.inside_medium), optional_scene_text(shape.outside_medium));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", shape.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", material_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", media_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.object_definition_name).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.area_light_type).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextWrapped("%s", scene_file_location_text(shape.location).c_str());
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(pbrt_parameter_count_text(shape.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials")) {
            if (this->spectra_scene->materials.empty()) {
                ImGui::TextDisabled("No PBRT materials recorded");
            } else if (ImGui::BeginTable("SpectraSceneMaterials", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const SpectraSceneMaterial& material : this->spectra_scene->materials) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(material.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(material.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(material.named ? "Named" : "Inline");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(material.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(pbrt_parameter_count_text(material.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures")) {
            if (this->spectra_scene->textures.empty()) {
                ImGui::TextDisabled("No PBRT textures recorded");
            } else if (ImGui::BeginTable("SpectraSceneTextures", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Implementation", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const SpectraSceneTexture& texture : this->spectra_scene->textures) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(scene_texture_value_type_label(texture.value_type));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.implementation).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(texture.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(pbrt_parameter_count_text(texture.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Media")) {
            if (this->spectra_scene->mediums.empty()) {
                ImGui::TextDisabled("No PBRT media recorded");
            } else if (ImGui::BeginTable("SpectraSceneMedia", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const SpectraSceneMedium& medium : this->spectra_scene->mediums) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(pbrt_parameter_count_text(medium.parameters).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Medium Interfaces");
            if (this->spectra_scene->medium_bindings.empty()) {
                ImGui::TextDisabled("No PBRT medium interfaces recorded");
            } else if (ImGui::BeginTable("SpectraSceneMediumInterfaces", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const SpectraSceneMediumBinding& binding : this->spectra_scene->medium_bindings) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights")) {
            if (this->spectra_scene->lights.empty()) {
                ImGui::TextDisabled("No PBRT lights recorded");
            } else if (ImGui::BeginTable("SpectraSceneLights", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const SpectraSceneLight& light : this->spectra_scene->lights) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", light.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(light.area ? "Area" : "Light");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(light.outside_medium).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(pbrt_parameter_count_text(light.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects")) {
            ImGui::SeparatorText("Definitions");
            if (this->spectra_scene->object_definitions.empty()) {
                ImGui::TextDisabled("No PBRT object definitions recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectDefinitions", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shapes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const SpectraSceneObjectDefinition& object_definition : this->spectra_scene->object_definitions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_definition.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu", object_definition.shape_indices.size());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_definition.location).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Instances");
            if (this->spectra_scene->object_instances.empty()) {
                ImGui::TextDisabled("No PBRT object instances recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectInstances", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Animated", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const SpectraSceneObjectInstance& object_instance : this->spectra_scene->object_instances) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_instance.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(object_instance.animated_transform ? "Yes" : "No");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_instance.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Diagnostics")) {
            ImGui::SeparatorText("PBRT Diagnostics");
            if (this->spectra_scene->unsupported_features.empty()) {
                ImGui::TextDisabled("No unsupported PBRT features recorded");
            } else if (ImGui::BeginTable("SpectraSceneDiagnostics", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const SpectraSceneUnsupportedFeature& feature : this->spectra_scene->unsupported_features) {
                    const std::string source_text   = scene_unsupported_source_text(feature);
                    const std::string location_text = scene_file_location_text(feature.location);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(scene_unsupported_kind_label(feature.kind));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", source_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", location_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", feature.message.c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Raster Diagnostics");
            if (this->raster_scene == nullptr) {
                ImGui::TextDisabled("No active Spectra raster scene");
            } else if (this->raster_scene->diagnostics.empty()) {
                ImGui::TextDisabled("No raster diagnostics recorded");
            } else if (ImGui::BeginTable("SpectraRasterSceneDiagnostics", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 160.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Reason", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const SpectraRasterDiagnostic& diagnostic : this->raster_scene->diagnostics) {
                    const std::string source_text   = raster_diagnostic_source_text(diagnostic);
                    const std::string location_text = scene_file_location_text(diagnostic.location);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(raster_diagnostic_kind_label(diagnostic.kind));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", source_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", location_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", diagnostic.message.c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::End();
    }


    void Spectra::draw_inspector_window() {
        if (!this->ui.inspector_visible) return;
        if (!ImGui::Begin("Inspector", &this->ui.inspector_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const ActiveRendererStatus renderer_status = this->active_renderer_status();
        const std::string viewport_resolution       = this->ui.viewport_known ? resolution_text(this->ui.viewport_framebuffer_size) : "Unknown";

        ImGui::SeparatorText("Renderer");
        if (ImGui::BeginTable("SpectraInspectorRenderer", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("PBRT Dirty", renderer_status.pathtracer_accumulation_dirty ? "Yes" : "No");
            draw_statistics_row("External Completion", renderer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraInspectorScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", this->spectra_scene->scene_label);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", this->spectra_scene->scene_path_text.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->spectra_scene->sampler_sample_count));
            draw_statistics_row("Viewport", viewport_resolution);
            draw_statistics_row("Swapchain", std::format("{} x {}", this->swapchain.extent.width, this->swapchain.extent.height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("SpectraInspectorResources", 2, table_flags)) {
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Directives", std::format("{}", this->spectra_scene->pbrt_directives.size()));
            draw_statistics_row("Shapes", std::format("{}", this->spectra_scene->shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->spectra_scene->materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->spectra_scene->textures.size()));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->spectra_scene->object_definitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->spectra_scene->object_instances.size()));
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            ImGui::EndTable();
        }

        if (this->pbrt_interactive != nullptr) {
            ImGui::SeparatorText("Path Tracer");
            if (ImGui::BeginTable("SpectraInspectorPathTracer", 2, table_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Sample", std::format("{} / {}", this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count()));
                draw_statistics_row("Completion", std::format("{:.1f}%", this->pbrt_interactive->completion_ratio() * 100.0f));
                draw_statistics_row("Exposure", std::format("{:.3f}", this->pbrt_interactive->current_exposure()));
                ImGui::EndTable();
            }
        }

        if (this->raster_scene != nullptr) {
            ImGui::SeparatorText("Raster Scene");
            if (ImGui::BeginTable("SpectraInspectorRasterScene", 2, table_flags)) {
                ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Vertices", std::format("{}", this->raster_scene->vertices.size()));
                draw_statistics_row("Indices", std::format("{}", this->raster_scene->indices.size()));
                draw_statistics_row("Triangles", std::format("{}", this->raster_scene->indices.size() / 3u));
                draw_statistics_row("Geometries", std::format("{}", this->raster_scene->geometries.size()));
                draw_statistics_row("Draws", std::format("{}", this->raster_scene->draws.size()));
                draw_statistics_row("Materials", std::format("{}", this->raster_scene->materials.size()));
                draw_statistics_row("Diagnostics", std::format("{}", this->raster_scene->diagnostics.size()));
                ImGui::EndTable();
            }
        }

        ImGui::End();
    }


    void Spectra::draw_settings_window() {
        if (!this->ui.settings_visible) return;
        if (!ImGui::Begin("Settings", &this->ui.settings_visible)) {
            ImGui::End();
            return;
        }
        ActiveRendererStatus renderer_status = this->active_renderer_status();
        if (ImGui::BeginTable("SpectraRendererSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Active Renderer");
            ImGui::TableSetColumnIndex(1);
            const char* renderer_items[]{"PBRT Pathtracer", "Vulkan Rasterizer"};
            int active_renderer_item = static_cast<int>(this->ui.active_render_mode);
            if (active_renderer_item < 0 || active_renderer_item > 1) throw std::runtime_error("Unknown Spectra render mode item");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##ActiveRenderer", &active_renderer_item, renderer_items, static_cast<int>(std::size(renderer_items)))) {
                if (active_renderer_item == 0) this->set_active_render_mode(SpectraRenderMode::PbrtPathtracer);
                else if (active_renderer_item == 1) this->set_active_render_mode(SpectraRenderMode::VulkanRasterizer);
                else throw std::runtime_error("Unknown Spectra render mode item selected");
                renderer_status = this->active_renderer_status();
            }
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("External Completion", renderer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Path Tracer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->pbrt_interactive == nullptr) {
                ImGui::TextDisabled("No active PBRT interactive session");
            } else if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::PbrtPathtracer ? renderer_status.state : "Inactive");
                draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("PBRT Sampler SPP");
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(positive_int_text(this->spectra_scene->sampler_sample_count).c_str());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Current Sample");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%d / %d", this->pbrt_interactive->current_sample(), this->pbrt_interactive->target_sample_count());

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Max Iterations");
                ImGui::TableSetColumnIndex(1);
                const int previous_target_sample_count = this->pbrt_interactive->target_sample_count();
                int target_sample_count                = previous_target_sample_count;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->spectra_scene->sampler_sample_count)) {
                    this->pbrt_interactive->set_target_sample_count(target_sample_count);
                    if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted("Accumulation");
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Reset Accumulation")) {
                    this->pbrt_interactive->request_reset_accumulation();
                    this->clear_pathtracer_throughput_statistics();
                }

                ImGui::EndTable();
            }
        }
        if (ImGui::CollapsingHeader("Rasterizer", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (this->vulkan_rasterizer == nullptr || this->raster_scene == nullptr) {
                ImGui::TextDisabled("No active Vulkan rasterizer session");
            } else if (ImGui::BeginTable("SpectraRasterizerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("State", this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer ? renderer_status.state : "Inactive");
                draw_statistics_row("Output", resolution_text(this->spectra_scene->film_resolution));
                draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }


    void Spectra::draw_environment_window() {
        if (!this->ui.environment_visible) return;
        if (!ImGui::Begin("Environment", &this->ui.environment_visible)) {
            ImGui::End();
            return;
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const SpectraSceneLight& light : this->spectra_scene->lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->spectra_scene->lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->spectra_scene->mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->spectra_scene->medium_bindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->spectra_scene->lights.empty()) {
            ImGui::TextDisabled("No PBRT lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneLight& light : this->spectra_scene->lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outside_medium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(pbrt_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->spectra_scene->mediums.empty()) {
            ImGui::TextDisabled("No PBRT media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMedium& medium : this->spectra_scene->mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(pbrt_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->spectra_scene->medium_bindings.empty()) {
            ImGui::TextDisabled("No PBRT medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const SpectraSceneMediumBinding& binding : this->spectra_scene->medium_bindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }


    void Spectra::draw_tonemapper_window() {
        if (!this->ui.tonemapper_visible) return;
        if (!ImGui::Begin("Tonemapper", &this->ui.tonemapper_visible)) {
            ImGui::End();
            return;
        }
        if (this->ui.active_render_mode == SpectraRenderMode::VulkanRasterizer) {
            ImGui::TextDisabled("Tonemapper applies to PBRT Pathtracer only");
            ImGui::End();
            return;
        }
        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->pbrt_interactive->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->pbrt_interactive->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_statistics_window() {
        if (!this->ui.statistics_visible) return;
        if (!ImGui::Begin("Statistics", &this->ui.statistics_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->ui.viewport_known ? resolution_text(this->ui.viewport_framebuffer_size) : "Unknown";
        const ActiveRendererStatus renderer_status = this->active_renderer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Active Renderer", renderer_status.label);
            draw_statistics_row("Renderer State", renderer_status.state);
            draw_statistics_row("Accumulation", renderer_status.has_accumulation ? "Yes" : "No");
            draw_statistics_row("Scene", this->spectra_scene == nullptr ? "No Scene" : this->spectra_scene->scene_label);
            draw_statistics_row("Frame ID", std::format("{}", this->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->sync.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->swapchain.extent.width, this->swapchain.extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->statistics.last_frame_milliseconds));
            if (this->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active PBRT scene");
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraSceneStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Unsupported Features", std::format("{}", this->spectra_scene->unsupported_features.size()));
            draw_statistics_row("Raster Diagnostics", this->raster_scene == nullptr ? "No raster scene" : std::format("{}", this->raster_scene->diagnostics.size()));
            ImGui::EndTable();
        }

        switch (this->ui.active_render_mode) {
            case SpectraRenderMode::PbrtPathtracer:
                break;
            case SpectraRenderMode::VulkanRasterizer:
                if (this->vulkan_rasterizer == nullptr) {
                    ImGui::TextDisabled("No active Vulkan rasterizer session");
                    ImGui::End();
                    return;
                }
                ImGui::SeparatorText("Rasterizer");
                if (ImGui::BeginTable("SpectraRasterizerStatistics", 2, table_flags)) {
                    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
                    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                    draw_statistics_row("State", renderer_status.state);
                    draw_statistics_row("Output Resolution", resolution_text(this->spectra_scene->film_resolution));
                    draw_statistics_row("Vertices", std::format("{}", this->vulkan_rasterizer->vertex_count()));
                    draw_statistics_row("Indices", std::format("{}", this->vulkan_rasterizer->index_count()));
                    draw_statistics_row("Triangles", std::format("{}", this->vulkan_rasterizer->triangle_count));
                    draw_statistics_row("Draws", std::format("{}", this->vulkan_rasterizer->draw_count));
                    draw_statistics_row("Materials", std::format("{}", this->vulkan_rasterizer->material_count()));
                    draw_statistics_row("Diagnostics", std::format("{}", this->vulkan_rasterizer->diagnostic_count()));
                    ImGui::EndTable();
                }
                ImGui::End();
                return;
        }

        if (this->pbrt_interactive == nullptr) {
            ImGui::TextDisabled("No active PBRT interactive session");
            ImGui::End();
            return;
        }

        const std::array<int, 2> film_resolution = this->spectra_scene->film_resolution;
        const int current_sample                 = this->pbrt_interactive->current_sample();
        const int target_sample                  = this->pbrt_interactive->target_sample_count();
        const float completion_ratio             = this->pbrt_interactive->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", this->camera.pathtracer_accumulation_dirty ? "Camera Dirty" : sampling_state);
            draw_statistics_row("Camera Dirty", this->camera.pathtracer_accumulation_dirty ? "Yes" : "No");
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", resolution_text(film_resolution));
            if (this->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->statistics.throughput_mspp.average(), this->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->statistics.last_frame_rendered_sample ? "Rendered sample" : "No PBRT sample");
            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace xayah
