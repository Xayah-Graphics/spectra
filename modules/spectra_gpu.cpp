#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "spectra_gpu.h"

#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <src/base/film.h>
#include <src/base/sampler.h>
#include <src/core/options.h>
#include <src/gpu/memory.h>
#include <src/gpu/util.h>
#include <src/pathtracer.h>
#include <src/runtime.h>
#include <src/scene.h>
#include <src/util/transform.h>
#include <src/util/vecmath.h>
#include <src/wavefront/aggregate.h>
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    void validate_finite_point(const spectra::Point3f& point, const char* message) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::runtime_error(message);
    }

    void validate_finite_vector(const spectra::Vector3f& vector, const char* message) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) throw std::runtime_error(message);
    }

    void validate_transform_matrix(const spectra::Transform& transform, const char* message) {
        const spectra::SquareMatrix<4>& matrix = transform.GetMatrix();
        const spectra::SquareMatrix<4>& inverse = transform.GetInverseMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                if (!std::isfinite(static_cast<float>(matrix[row][column])) || !std::isfinite(static_cast<float>(inverse[row][column]))) throw std::runtime_error(message);
            }
        }
    }

    [[nodiscard]] float finite_length(const spectra::Vector3f& vector, const char* error_message) {
        validate_finite_vector(vector, error_message);
        const float length = spectra::Length(vector);
        if (!std::isfinite(length)) throw std::runtime_error(error_message);
        return length;
    }

    [[nodiscard]] spectra::Vector3f normalized_vector(const spectra::Vector3f& vector, const char* error_message) {
        const float length = finite_length(vector, error_message);
        if (!(length > 1.0e-20f)) throw std::runtime_error(error_message);
        return vector / length;
    }

    [[nodiscard]] spectra::Vector3f camera_effective_up(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up) {
        const spectra::Vector3f view_direction = center - eye;
        if (finite_length(spectra::Cross(view_direction, up), "Camera view/up cross product is invalid") > 1.0e-10f) return up;
        return std::abs(up.y) < 0.9f ? spectra::Vector3f{0.0f, 1.0f, 0.0f} : spectra::Vector3f{1.0f, 0.0f, 0.0f};
    }

    struct SpectraCameraFrame {
        spectra::Vector3f forward{};
        spectra::Vector3f right{};
        spectra::Vector3f up{};
    };

    [[nodiscard]] SpectraCameraFrame camera_frame_from_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up, const float basis_handedness) {
        if (basis_handedness != -1.0f && basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        SpectraCameraFrame frame{};
        frame.forward = normalized_vector(center - eye, "Camera eye and center must not overlap");
        const spectra::Vector3f effective_up = camera_effective_up(eye, center, up);
        const spectra::Vector3f positive_right = normalized_vector(spectra::Cross(effective_up, frame.forward), "Camera right vector is invalid");
        frame.right                         = positive_right * basis_handedness;
        frame.up                            = spectra::Cross(frame.forward, positive_right);
        return frame;
    }

    [[nodiscard]] spectra::Transform camera_from_world_transform_from_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up, const float basis_handedness) {
        const SpectraCameraFrame frame = camera_frame_from_pose(eye, center, up, basis_handedness);
        const spectra::Vector3f eye_vector{eye.x, eye.y, eye.z};
        spectra::Transform transform{spectra::SquareMatrix<4>{
            frame.right.x, frame.right.y, frame.right.z, -spectra::Dot(frame.right, eye_vector),
            frame.up.x, frame.up.y, frame.up.z, -spectra::Dot(frame.up, eye_vector),
            frame.forward.x, frame.forward.y, frame.forward.z, -spectra::Dot(frame.forward, eye_vector),
            0.0f, 0.0f, 0.0f, 1.0f,
        }};
        validate_transform_matrix(transform, "Camera transform contains a non-finite value");
        return transform;
    }

    void validate_bounds(const spectra::Bounds3f& bounds, const char* message) {
        validate_finite_point(bounds.pMin, message);
        validate_finite_point(bounds.pMax, message);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (bounds.pMin[axis] > bounds.pMax[axis]) throw std::runtime_error(message);
        }
    }

    [[nodiscard]] spectra::Point3f camera_focus_center_from_bounds(const spectra::Point3f& eye, const spectra::Vector3f& forward, const spectra::Bounds3f& focus_bounds) {
        validate_bounds(focus_bounds, "Camera focus bounds are invalid");

        const spectra::Point3f bounds_center{
            (focus_bounds.pMin.x + focus_bounds.pMax.x) * 0.5f,
            (focus_bounds.pMin.y + focus_bounds.pMax.y) * 0.5f,
            (focus_bounds.pMin.z + focus_bounds.pMax.z) * 0.5f,
        };
        float focus_distance = spectra::Dot(bounds_center - eye, forward);

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

    [[nodiscard]] xayah::SpectraCameraPose camera_pose_from_base_transform_impl(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds) {
        const spectra::Transform world_from_camera = spectra::Inverse(camera_from_world);
        xayah::SpectraCameraPose pose{};
        pose.eye                          = world_from_camera(spectra::Point3f{0.0f, 0.0f, 0.0f});
        const spectra::Vector3f right        = normalized_vector(world_from_camera(spectra::Vector3f{1.0f, 0.0f, 0.0f}), "Base camera right vector is invalid");
        const spectra::Vector3f forward      = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 0.0f, 1.0f}), "Base camera forward vector is invalid");
        pose.up                           = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 1.0f, 0.0f}), "Base camera up vector is invalid");
        const spectra::Vector3f positive_right = normalized_vector(spectra::Cross(camera_effective_up(pose.eye, pose.eye + forward, pose.up), forward), "Base camera positive right vector is invalid");
        pose.basis_handedness             = spectra::Dot(right, positive_right) < 0.0f ? -1.0f : 1.0f;
        pose.center                        = camera_focus_center_from_bounds(pose.eye, forward, focus_bounds);
        return pose;
    }

    [[nodiscard]] std::array<float, 2> camera_view_dimensions(const spectra::Point3f& eye, const spectra::Point3f& center, const float fov_degrees, const std::array<float, 2>& viewport_size) {
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

    bool camera_pan_impl(xayah::SpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        const SpectraCameraFrame frame       = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const std::array<float, 2> view_size = camera_view_dimensions(pose.eye, pose.center, fov_degrees, viewport_size);
        const spectra::Vector3f offset          = frame.right * (-displacement[0] * view_size[0]) + frame.up * (displacement[1] * view_size[1]);
        pose.eye += offset;
        pose.center += offset;
        return true;
    }

    bool camera_dolly_impl(xayah::SpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        const float larger_displacement = std::abs(displacement[0]) > std::abs(displacement[1]) ? displacement[0] : -displacement[1];
        if (larger_displacement == 0.0f) return false;
        if (larger_displacement >= 0.99f) return false;
        const spectra::Vector3f direction = pose.center - pose.eye;
        if (!(finite_length(direction, "Camera dolly direction is invalid") > 1.0e-6f)) return false;
        pose.eye += direction * larger_displacement;
        return true;
    }

    bool camera_orbit_impl(xayah::SpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        if (displacement[0] == 0.0f && displacement[1] == 0.0f) return false;
        if (pose.basis_handedness != -1.0f && pose.basis_handedness != 1.0f) throw std::runtime_error("Camera basis handedness must be either -1 or 1");
        constexpr float two_pi   = 6.2831853071795864769f;
        constexpr float pole_pad = 1.0e-3f;
        displacement[0] *= -pose.basis_handedness;
        displacement[0] *= two_pi;
        displacement[1] *= two_pi;

        const spectra::Point3f origin   = invert ? pose.eye : pose.center;
        const spectra::Point3f position = invert ? pose.center : pose.eye;
        spectra::Vector3f center_to_eye = position - origin;
        const float radius           = finite_length(center_to_eye, "Camera orbit radius is invalid");
        if (!(radius > 1.0e-6f)) return false;
        center_to_eye /= radius;

        const spectra::Vector3f normalized_up = normalized_vector(pose.up, "Camera up vector is invalid");
        const float cos_elevation          = spectra::Dot(center_to_eye, normalized_up);
        spectra::Vector3f horizontal          = center_to_eye - normalized_up * cos_elevation;
        const float sin_elevation          = finite_length(horizontal, "Camera orbit horizontal vector is invalid");
        const float elevation                    = std::atan2(sin_elevation, cos_elevation);
        if (sin_elevation < 1.0e-6f) {
            const spectra::Vector3f reference = std::abs(normalized_up.x) < 0.9f ? spectra::Vector3f{1.0f, 0.0f, 0.0f} : spectra::Vector3f{0.0f, 0.0f, 1.0f};
            horizontal                     = normalized_vector(reference - normalized_up * spectra::Dot(reference, normalized_up), "Camera orbit horizontal vector is invalid");
        } else {
            horizontal /= sin_elevation;
        }

        const float yaw_cos                    = std::cos(-displacement[0]);
        const float yaw_sin                    = std::sin(-displacement[0]);
        horizontal                             = horizontal * yaw_cos + spectra::Cross(normalized_up, horizontal) * yaw_sin;
        const float new_elevation              = std::clamp(elevation - displacement[1], pole_pad, 3.14159265358979323846f - pole_pad);
        const spectra::Vector3f new_offset        = (normalized_up * std::cos(new_elevation) + horizontal * std::sin(new_elevation)) * radius;
        const spectra::Point3f new_position       = origin + new_offset;
        if (invert) pose.center = new_position;
        else pose.eye = new_position;
        return true;
    }

    bool camera_key_motion_impl(xayah::SpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        if (delta[0] == 0.0f && delta[1] == 0.0f) return false;
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        const SpectraCameraFrame frame = camera_frame_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        const spectra::Vector3f movement = dolly
            ? frame.forward * (delta[0] * speed)
            : frame.right * (delta[0] * speed) + frame.up * (delta[1] * speed);
        pose.eye += movement;
        pose.center += movement;
        return true;
    }

    [[nodiscard]] spectra::Transform moving_from_camera_from_pose_impl(const spectra::Transform& base_camera_from_world, const xayah::SpectraCameraPose& pose) {
        const spectra::Transform current_camera_from_world = camera_from_world_transform_from_pose(pose.eye, pose.center, pose.up, pose.basis_handedness);
        return base_camera_from_world * spectra::Inverse(current_camera_from_world);
    }

    [[nodiscard]] float spectra_camera_fov_degrees_impl(const xayah::SpectraScene& scene) {
        if (!scene.description.camera.present) throw std::runtime_error("Interactive Spectra GPU camera controls require an explicit perspective camera");
        if (scene.description.camera.name != "perspective") throw std::runtime_error(std::format("Interactive Spectra GPU camera controls require a perspective camera, not \"{}\"", scene.description.camera.name));
        constexpr float spectra_gpu_perspective_default_fov = 90.0f;
        for (const spectra::scene::SceneDescriptionParameter& parameter : scene.description.camera.parameters) {
            if (parameter.name != "fov") continue;
            if (parameter.floats.size() != 1) throw std::runtime_error("Spectra GPU perspective camera fov must have exactly one float value");
            return parameter.floats.front();
        }
        return spectra_gpu_perspective_default_fov;
    }

}

namespace xayah {
    [[nodiscard]] float spectra_camera_fov_degrees(const SpectraScene& scene) {
        return spectra_camera_fov_degrees_impl(scene);
    }

    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds) {
        return camera_pose_from_base_transform_impl(camera_from_world, focus_bounds);
    }

    [[nodiscard]] spectra::Transform moving_from_camera_from_pose(const spectra::Transform& base_camera_from_world, const SpectraCameraPose& pose) {
        return moving_from_camera_from_pose_impl(base_camera_from_world, pose);
    }

    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, const float fov_degrees, const std::array<float, 2>& viewport_size) {
        return camera_pan_impl(pose, displacement, fov_degrees, viewport_size);
    }

    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement) {
        return camera_dolly_impl(pose, displacement);
    }

    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, const bool invert) {
        return camera_orbit_impl(pose, displacement, invert);
    }

    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, const float speed, const bool dolly) {
        return camera_key_motion_impl(pose, delta, speed, dolly);
    }

    struct SpectraGpuRuntimeState {
        spectra::SpectraOptions baseline_options{};
        std::unique_ptr<spectra::runtime::Runtime> runtime{};
    };

    SpectraGpuRuntime::SpectraGpuRuntime() : state{std::make_unique<SpectraGpuRuntimeState>()} {
        this->state->baseline_options.nThreads       = 30;
        this->state->baseline_options.renderingSpace = spectra::RenderingCoordinateSystem::CameraWorld;
        this->state->runtime = std::make_unique<spectra::runtime::Runtime>(this->state->baseline_options);
    }

    SpectraGpuRuntime::~SpectraGpuRuntime() noexcept {
        try {
            this->wait_gpu_noexcept();
            if (this->state != nullptr) this->state->runtime.reset();
        } catch (...) {
        }
    }

    void SpectraGpuRuntime::reset_options_for_scene() {
        if (this->state == nullptr || this->state->runtime == nullptr) throw std::runtime_error("Spectra GPU runtime is not initialized");
        this->state->runtime->ResetOptions(this->state->baseline_options);
    }

    void SpectraGpuRuntime::wait_gpu_noexcept() const noexcept {
        if (this->state != nullptr && this->state->runtime != nullptr) this->state->runtime->WaitGpuNoexcept();
    }
}


namespace xayah {
    void SpectraScene::load(const std::filesystem::path& path) {
        if (!this->scene_path.empty()) throw std::runtime_error("Spectra scene is already loaded");
        if (path.empty()) throw std::runtime_error("Spectra scene path is empty");
        if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"Spectra scene does not exist: "} + path.string());

        try {
            this->scene_path = path;
            spectra::scene::SceneDescriptionBuilder builder{&this->description};
            const std::string filename = this->scene_path.string();
            std::vector<std::string> filenames{filename};
            spectra::scene::ParseFiles(&builder, filenames);
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraScene::set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const spectra::Transform& camera_transform) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Spectra GPU film resolution must be positive");
        if (samples_per_pixel <= 0) throw std::runtime_error("Spectra GPU sampler SPP must be positive");
        this->film_resolution      = resolution;
        this->sampler_sample_count = samples_per_pixel;
        this->camera_from_world    = camera_transform;
    }

    void SpectraScene::unload_noexcept() noexcept {
        this->scene_path.clear();
        this->film_resolution      = {0, 0};
        this->camera_from_world    = spectra::Transform{};
        this->sampler_sample_count = 0;
        this->description.Clear();
    }

}

namespace {
    [[nodiscard]] vk::ExternalMemoryHandleTypeFlagBits spectra_external_memory_handle_type() {
#if defined(_WIN32)
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] vk::ExternalSemaphoreHandleTypeFlagBits spectra_external_semaphore_handle_type() {
#if defined(_WIN32)
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
        return vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalMemoryHandleType spectra_cuda_external_memory_handle_type() {
#if defined(_WIN32)
        return cudaExternalMemoryHandleTypeOpaqueWin32;
#else
        return cudaExternalMemoryHandleTypeOpaqueFd;
#endif
    }

    [[nodiscard]] cudaExternalSemaphoreHandleType spectra_cuda_external_semaphore_handle_type() {
#if defined(_WIN32)
        return cudaExternalSemaphoreHandleTypeOpaqueWin32;
#else
        return cudaExternalSemaphoreHandleTypeOpaqueFd;
#endif
    }

}

namespace xayah {
    struct SpectraGpuPathtracerState {
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

        std::unique_ptr<spectra::scene::Scene> scene{};
        std::unique_ptr<spectra::scene::SceneBuilder> builder{};
        std::unique_ptr<spectra::pathtracer::SpectraPathtracer> integrator{};
        spectra::Bounds2i pixel_bounds{};
        spectra::Vector2i resolution{};
        spectra::Transform render_from_camera{};
        spectra::Transform camera_from_render{};
        spectra::Transform camera_from_world{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        float exposure{1.0f};
        float initial_move_scale{1.0f};
        spectra::Bounds3f initial_focus_bounds{};
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
    [[nodiscard]] xayah::SpectraGpuPathtracerState& require_pathtracer_state(std::unique_ptr<xayah::SpectraGpuPathtracerState>& state) {
        if (state == nullptr) throw std::runtime_error("Spectra GPU pathtracer state is null");
        return *state;
    }

    [[nodiscard]] const xayah::SpectraGpuPathtracerState& require_pathtracer_state(const std::unique_ptr<xayah::SpectraGpuPathtracerState>& state) {
        if (state == nullptr) throw std::runtime_error("Spectra GPU pathtracer state is null");
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

    void release_pathtracer_viewport_descriptors_noexcept(xayah::SpectraGpuPathtracerState& pathtracer) noexcept {
        for (xayah::SpectraGpuPathtracerState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                frame.imgui_descriptor = VK_NULL_HANDLE;
            }
        }
    }

    void create_pathtracer_viewport_descriptors(xayah::SpectraGpuPathtracerState& pathtracer) {
        for (xayah::SpectraGpuPathtracerState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra GPU pathtracer viewport descriptor is already allocated");
            frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra GPU pathtracer viewport descriptor");
        }
    }

    void destroy_pathtracer_frame_resources_noexcept(xayah::SpectraGpuPathtracerState& pathtracer) noexcept {
        release_pathtracer_viewport_descriptors_noexcept(pathtracer);
        for (xayah::SpectraGpuPathtracerState::FrameResource& frame : pathtracer.frames) {
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

    void create_pathtracer_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::SpectraGpuPathtracerState::FrameResource& frame, const vk::DeviceSize rgba_bytes) {
        const vk::ExternalMemoryBufferCreateInfo external_buffer_info{spectra_external_memory_handle_type()};
        const vk::BufferCreateInfo buffer_create_info{{}, rgba_bytes, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive, 0, nullptr, &external_buffer_info};
        frame.interop_buffer = vk::raii::Buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = frame.interop_buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::ExportMemoryAllocateInfo export_allocate_info{spectra_external_memory_handle_type()};
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, memory_type, &export_allocate_info};
        frame.interop_memory = vk::raii::DeviceMemory{device, allocate_info};
        frame.interop_buffer.bindMemory(*frame.interop_memory, 0);
        frame.interop_allocation_size = memory_requirements.size;
        frame.interop_buffer_size     = rgba_bytes;

        cudaExternalMemoryHandleDesc memory_handle_desc{};
        memory_handle_desc.type = spectra_cuda_external_memory_handle_type();
        memory_handle_desc.size = static_cast<unsigned long long>(frame.interop_allocation_size);
#if defined(_WIN32)
        const vk::MemoryGetWin32HandleInfoKHR memory_handle_info{*frame.interop_memory, spectra_external_memory_handle_type()};
        HANDLE memory_handle = device.getMemoryWin32HandleKHR(memory_handle_info);
        if (memory_handle == nullptr) throw std::runtime_error("Failed to export Vulkan memory Win32 handle for CUDA");
        memory_handle_desc.handle.win32.handle = memory_handle;
        CUDA_CHECK(cudaImportExternalMemory(&frame.cuda_external_memory, &memory_handle_desc));
        CloseHandle(memory_handle);
#else
        const vk::MemoryGetFdInfoKHR memory_handle_info{*frame.interop_memory, spectra_external_memory_handle_type()};
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
        if (frame.cuda_pixels == nullptr) throw std::runtime_error("CUDA external memory mapped to a null Spectra GPU RGBA pointer");
    }

    void create_pathtracer_cuda_complete_semaphore(const vk::raii::Device& device, xayah::SpectraGpuPathtracerState::FrameResource& frame) {
        const vk::ExportSemaphoreCreateInfo export_semaphore_info{spectra_external_semaphore_handle_type()};
        const vk::SemaphoreCreateInfo semaphore_create_info{{}, &export_semaphore_info};
        frame.cuda_complete_semaphore = vk::raii::Semaphore{device, semaphore_create_info};

        cudaExternalSemaphoreHandleDesc semaphore_handle_desc{};
        semaphore_handle_desc.type = spectra_cuda_external_semaphore_handle_type();
#if defined(_WIN32)
        const vk::SemaphoreGetWin32HandleInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, spectra_external_semaphore_handle_type()};
        HANDLE semaphore_handle = device.getSemaphoreWin32HandleKHR(semaphore_handle_info);
        if (semaphore_handle == nullptr) throw std::runtime_error("Failed to export Vulkan semaphore Win32 handle for CUDA");
        semaphore_handle_desc.handle.win32.handle = semaphore_handle;
        CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
        CloseHandle(semaphore_handle);
#else
        const vk::SemaphoreGetFdInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, spectra_external_semaphore_handle_type()};
        int semaphore_fd = device.getSemaphoreFdKHR(semaphore_handle_info);
        if (semaphore_fd < 0) throw std::runtime_error("Failed to export Vulkan semaphore FD for CUDA");
        semaphore_handle_desc.handle.fd = semaphore_fd;
        CUDA_CHECK(cudaImportExternalSemaphore(&frame.cuda_external_semaphore, &semaphore_handle_desc));
        close(semaphore_fd);
#endif
        if (frame.cuda_external_semaphore == nullptr) throw std::runtime_error("CUDA external semaphore import returned null");
    }

    void create_pathtracer_display_image(xayah::SpectraGpuPathtracerState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::SpectraGpuPathtracerState::FrameResource& frame) {
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

    void create_pathtracer_frame_resources(xayah::SpectraGpuPathtracerState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
        const vk::FormatProperties format_properties = physical_device.getFormatProperties(pathtracer.display_format);
        constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

        const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(pathtracer.resolution.x) * static_cast<vk::DeviceSize>(pathtracer.resolution.y);
        if (rgba_bytes == 0) throw std::runtime_error("Spectra GPU pathtracer interop buffer cannot be zero bytes");
        pathtracer.frames.resize(frame_count);
        for (xayah::SpectraGpuPathtracerState::FrameResource& frame : pathtracer.frames) {
            create_pathtracer_interop_buffer(physical_device, device, frame, rgba_bytes);
            create_pathtracer_cuda_complete_semaphore(device, frame);
            create_pathtracer_display_image(pathtracer, physical_device, device, frame);
        }
    }

    void destroy_pathtracer_resources_noexcept(xayah::SpectraGpuPathtracerState& pathtracer) noexcept {
        try {
            if (pathtracer.device != nullptr) pathtracer.device->waitIdle();
            if (spectra::Options != nullptr) spectra::GPUWait();
        } catch (...) {
        }
        destroy_pathtracer_frame_resources_noexcept(pathtracer);
        pathtracer.integrator.reset();
        pathtracer.builder.reset();
        pathtracer.scene.reset();
    }
}

namespace xayah {

    SpectraGpuPathtracer::SpectraGpuPathtracer(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : state{std::make_unique<SpectraGpuPathtracerState>()} {
        try {
            SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
            if (spectra_scene.scene_path.empty()) throw std::runtime_error("Cannot create Spectra GPU pathtracer without a loaded Spectra scene");
            if (!std::filesystem::exists(spectra_scene.scene_path)) throw std::runtime_error(std::string{"Spectra GPU scene does not exist: "} + spectra_scene.scene_path.string());
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra GPU pathtracer with a non-positive resolution");
            if (frame_count == 0) throw std::runtime_error("Spectra GPU pathtracer requires at least one frame in flight");
            if (spectra::Options == nullptr) throw std::runtime_error("Cannot create Spectra GPU pathtracer before Spectra GPU runtime is initialized");

            pathtracer.physical_device  = &physical_device;
            pathtracer.device           = &device;
            pathtracer.frame_count      = frame_count;
            pathtracer.scene            = std::make_unique<spectra::scene::Scene>();
            pathtracer.builder          = std::make_unique<spectra::scene::SceneBuilder>(pathtracer.scene.get(), spectra::Point2i{resolution[0], resolution[1]});
            std::vector<std::string> filenames{spectra_scene.scene_path.string()};
            spectra::scene::ParseFiles(pathtracer.builder.get(), filenames);

            pathtracer.integrator = std::make_unique<spectra::pathtracer::SpectraPathtracer>(&spectra::CUDATrackedMemoryResource::singleton, *pathtracer.scene);
            pathtracer.integrator->PrefetchGPUAllocations();
            pathtracer.pixel_bounds = pathtracer.integrator->film.PixelBounds();
            pathtracer.resolution   = pathtracer.pixel_bounds.Diagonal();
            if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra GPU film resolution must be positive");
            pathtracer.max_samples = pathtracer.integrator->sampler.SamplesPerPixel();
            if (pathtracer.max_samples <= 0) throw std::runtime_error("Spectra GPU sampler SPP must be positive");
            pathtracer.target_samples = pathtracer.max_samples;
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, spectra::Transform{}, pathtracer.sample_index);
            ++pathtracer.sample_index;
            spectra::GPUWait();

            pathtracer.render_from_camera = pathtracer.integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
            pathtracer.camera_from_render = spectra::Inverse(pathtracer.render_from_camera);
            pathtracer.camera_from_world  = pathtracer.integrator->camera.GetCameraTransform().CameraFromWorld(pathtracer.integrator->camera.SampleTime(0.0f));
            const spectra::Bounds3f scene_bounds = pathtracer.integrator->aggregate->Bounds();
            pathtracer.initial_move_scale     = spectra::Length(scene_bounds.Diagonal()) / 1000.0f;
            if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("Spectra GPU scene bounds must define a positive interactive move scale");
            const spectra::Transform world_from_render = spectra::Inverse(pathtracer.render_from_camera * pathtracer.camera_from_world);
            spectra::Bounds3f world_bounds{};
            bool has_world_bounds = false;
            for (const float x : std::array<float, 2>{scene_bounds.pMin.x, scene_bounds.pMax.x}) {
                for (const float y : std::array<float, 2>{scene_bounds.pMin.y, scene_bounds.pMax.y}) {
                    for (const float z : std::array<float, 2>{scene_bounds.pMin.z, scene_bounds.pMax.z}) {
                        const spectra::Point3f corner_world = world_from_render(spectra::Point3f{x, y, z});
                        validate_finite_point(corner_world, "Spectra GPU scene focus bounds contain a non-finite value");
                        if (!has_world_bounds) world_bounds = spectra::Bounds3f{corner_world};
                        else world_bounds = spectra::Union(world_bounds, corner_world);
                        has_world_bounds = true;
                    }
                }
            }
            if (!has_world_bounds) throw std::runtime_error("Spectra GPU scene focus bounds are unavailable");
            pathtracer.initial_focus_bounds = world_bounds;

            validate_cuda_vulkan_device(physical_device);
            create_pathtracer_frame_resources(pathtracer, physical_device, device, frame_count);
            create_pathtracer_viewport_descriptors(pathtracer);
        } catch (...) {
            if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
            throw;
        }
    }

    SpectraGpuPathtracer::~SpectraGpuPathtracer() noexcept {
        if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
    }

    [[nodiscard]] int SpectraGpuPathtracer::current_sample() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.reset_requested) return 0;
        return pathtracer.sample_index;
    }

    [[nodiscard]] int SpectraGpuPathtracer::sampler_sample_count() const {
        return require_pathtracer_state(this->state).max_samples;
    }

    [[nodiscard]] int SpectraGpuPathtracer::target_sample_count() const {
        return require_pathtracer_state(this->state).target_samples;
    }

    [[nodiscard]] float SpectraGpuPathtracer::current_exposure() const {
        return require_pathtracer_state(this->state).exposure;
    }

    [[nodiscard]] float SpectraGpuPathtracer::camera_initial_move_scale() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("Spectra GPU pathtracer camera initial move scale must be positive");
        return pathtracer.initial_move_scale;
    }

    [[nodiscard]] spectra::Bounds3f SpectraGpuPathtracer::camera_initial_focus_bounds() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        validate_bounds(pathtracer.initial_focus_bounds, "Spectra GPU pathtracer camera initial focus bounds are invalid");
        return pathtracer.initial_focus_bounds;
    }

    [[nodiscard]] std::array<int, 2> SpectraGpuPathtracer::film_resolution() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra GPU film resolution must be positive before metadata is queried");
        return {pathtracer.resolution.x, pathtracer.resolution.y};
    }

    [[nodiscard]] spectra::Transform SpectraGpuPathtracer::camera_from_world_transform() const {
        return require_pathtracer_state(this->state).camera_from_world;
    }

    [[nodiscard]] std::uint64_t SpectraGpuPathtracer::film_pixel_count() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra GPU film resolution must be positive before statistics are queried");
        return static_cast<std::uint64_t>(pathtracer.resolution.x) * static_cast<std::uint64_t>(pathtracer.resolution.y);
    }

    [[nodiscard]] float SpectraGpuPathtracer::completion_ratio() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.target_samples <= 0) throw std::runtime_error("Spectra GPU target sample count must be positive before statistics are queried");
        const int visible_sample = this->current_sample();
        if (visible_sample < 0 || visible_sample > pathtracer.target_samples) throw std::runtime_error("Spectra GPU visible sample count is outside the target sample range");
        return static_cast<float>(visible_sample) / static_cast<float>(pathtracer.target_samples);
    }

    [[nodiscard]] VkDescriptorSet SpectraGpuPathtracer::active_descriptor() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) return VK_NULL_HANDLE;
        return pathtracer.frames.at(pathtracer.active_frame_index).imgui_descriptor;
    }

    [[nodiscard]] vk::Semaphore SpectraGpuPathtracer::active_cuda_complete_semaphore() const {
        const SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) throw std::runtime_error("Spectra GPU completion semaphore requested without frame resources");
        return *pathtracer.frames.at(pathtracer.active_frame_index).cuda_complete_semaphore;
    }

    void SpectraGpuPathtracer::set_target_sample_count(const int target_sample_count) {
        SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (target_sample_count < 1 || target_sample_count > pathtracer.max_samples) throw std::runtime_error("Spectra GPU target sample count is outside the sampler SPP range");
        if (target_sample_count == pathtracer.target_samples) return;
        pathtracer.target_samples = target_sample_count;
        this->request_reset_accumulation();
    }

    void SpectraGpuPathtracer::set_exposure(const float value) {
        if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("Spectra GPU exposure must be in [0.001, 1000]");
        require_pathtracer_state(this->state).exposure = value;
    }

    void SpectraGpuPathtracer::request_reset_accumulation() {
        require_pathtracer_state(this->state).reset_requested = true;
    }

    void SpectraGpuPathtracer::release_viewport_descriptors_noexcept() noexcept {
        if (this->state != nullptr) release_pathtracer_viewport_descriptors_noexcept(*this->state);
    }

    void SpectraGpuPathtracer::create_viewport_descriptors() {
        create_pathtracer_viewport_descriptors(require_pathtracer_state(this->state));
    }

    [[nodiscard]] SpectraGpuPathtracer::RenderFrameResult SpectraGpuPathtracer::render_frame(const std::uint32_t frame_index, const spectra::Transform& moving_from_camera) {
        SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        if (frame_index >= pathtracer.frames.size()) throw std::runtime_error("Spectra GPU pathtracer frame index is out of range");
        pathtracer.active_frame_index = frame_index;
        RenderFrameResult result{};
        const spectra::Transform camera_motion = pathtracer.render_from_camera * moving_from_camera * pathtracer.camera_from_render;
        if (pathtracer.reset_requested) {
            if (pathtracer.physical_device == nullptr || pathtracer.device == nullptr) throw std::runtime_error("Spectra GPU pathtracer Vulkan handles are not available for reset");
            pathtracer.device->waitIdle();
            destroy_pathtracer_frame_resources_noexcept(pathtracer);
            pathtracer.integrator->ResetFilm(pathtracer.pixel_bounds);
            spectra::GPUWait();
            pathtracer.sample_index    = 0;
            pathtracer.reset_requested = false;
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, camera_motion, pathtracer.sample_index);
            ++pathtracer.sample_index;
            spectra::GPUWait();
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
        SpectraGpuPathtracerState::FrameResource& output_frame = pathtracer.frames.at(frame_index);
        pathtracer.integrator->UpdateFramebufferFromFilm(pathtracer.pixel_bounds, pathtracer.exposure, output_frame.cuda_pixels);

        cudaExternalSemaphoreSignalParams signal_params{};
        CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
        return result;
    }

    void SpectraGpuPathtracer::record_copy(const vk::raii::CommandBuffer& command_buffer) {
        SpectraGpuPathtracerState& pathtracer = require_pathtracer_state(this->state);
        SpectraGpuPathtracerState::FrameResource& frame = pathtracer.frames.at(pathtracer.active_frame_index);
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
