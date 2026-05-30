#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "backend.h"

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
#include <vulkan/vulkan_raii.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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

    void validate_bounds(const spectra::Bounds3f& bounds, const char* message) {
        validate_finite_point(bounds.pMin, message);
        validate_finite_point(bounds.pMax, message);
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if (bounds.pMin[axis] > bounds.pMax[axis]) throw std::runtime_error(message);
        }
    }
}

namespace xayah::spectra_pathtracer {

    struct RuntimeSessionState {
        spectra::SpectraOptions baseline_options{};
        std::unique_ptr<spectra::runtime::Runtime> runtime{};
    };

    RuntimeSession::RuntimeSession() : state{std::make_unique<RuntimeSessionState>()} {
        this->state->baseline_options.nThreads       = 30;
        this->state->baseline_options.renderingSpace = spectra::RenderingCoordinateSystem::CameraWorld;
        this->state->runtime = std::make_unique<spectra::runtime::Runtime>(this->state->baseline_options);
    }

    RuntimeSession::~RuntimeSession() noexcept {
        try {
            this->wait_gpu_noexcept();
            if (this->state != nullptr) this->state->runtime.reset();
        } catch (...) {
        }
    }

    void RuntimeSession::reset_options_for_scene() {
        if (this->state == nullptr || this->state->runtime == nullptr) throw std::runtime_error("Spectra pathtracer runtime is not initialized");
        this->state->runtime->ResetOptions(this->state->baseline_options);
    }

    void RuntimeSession::wait_gpu_noexcept() const noexcept {
        if (this->state != nullptr && this->state->runtime != nullptr) this->state->runtime->WaitGpuNoexcept();
    }
}


namespace xayah::spectra_pathtracer {
    void SceneSession::load(const std::filesystem::path& path) {
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

    void SceneSession::set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const spectra::Transform& camera_transform) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive");
        if (samples_per_pixel <= 0) throw std::runtime_error("Spectra pathtracer sampler SPP must be positive");
        this->film_resolution      = resolution;
        this->sampler_sample_count = samples_per_pixel;
        this->camera_from_world    = camera_transform;
    }

    void SceneSession::unload_noexcept() noexcept {
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

namespace xayah::spectra_pathtracer {
    struct PathtracerSessionState {
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
    [[nodiscard]] xayah::spectra_pathtracer::PathtracerSessionState& require_pathtracer_state(std::unique_ptr<xayah::spectra_pathtracer::PathtracerSessionState>& state) {
        if (state == nullptr) throw std::runtime_error("Spectra pathtracer state is null");
        return *state;
    }

    [[nodiscard]] const xayah::spectra_pathtracer::PathtracerSessionState& require_pathtracer_state(const std::unique_ptr<xayah::spectra_pathtracer::PathtracerSessionState>& state) {
        if (state == nullptr) throw std::runtime_error("Spectra pathtracer state is null");
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

    void release_pathtracer_viewport_descriptors_noexcept(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer) noexcept {
        for (xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                frame.imgui_descriptor = VK_NULL_HANDLE;
            }
        }
    }

    void create_pathtracer_viewport_descriptors(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer) {
        for (xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra pathtracer viewport descriptor is already allocated");
            frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra pathtracer viewport descriptor");
        }
    }

    void destroy_pathtracer_frame_resources_noexcept(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer) noexcept {
        release_pathtracer_viewport_descriptors_noexcept(pathtracer);
        for (xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame : pathtracer.frames) {
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

    void create_pathtracer_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame, const vk::DeviceSize rgba_bytes) {
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
        if (frame.cuda_pixels == nullptr) throw std::runtime_error("CUDA external memory mapped to a null Spectra pathtracer RGBA pointer");
    }

    void create_pathtracer_cuda_complete_semaphore(const vk::raii::Device& device, xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame) {
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

    void create_pathtracer_display_image(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame) {
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

    void create_pathtracer_frame_resources(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
        const vk::FormatProperties format_properties = physical_device.getFormatProperties(pathtracer.display_format);
        constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

        const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(pathtracer.resolution.x) * static_cast<vk::DeviceSize>(pathtracer.resolution.y);
        if (rgba_bytes == 0) throw std::runtime_error("Spectra pathtracer interop buffer cannot be zero bytes");
        pathtracer.frames.resize(frame_count);
        for (xayah::spectra_pathtracer::PathtracerSessionState::FrameResource& frame : pathtracer.frames) {
            create_pathtracer_interop_buffer(physical_device, device, frame, rgba_bytes);
            create_pathtracer_cuda_complete_semaphore(device, frame);
            create_pathtracer_display_image(pathtracer, physical_device, device, frame);
        }
    }

    void destroy_pathtracer_resources_noexcept(xayah::spectra_pathtracer::PathtracerSessionState& pathtracer) noexcept {
        try {
            if (pathtracer.device != nullptr) pathtracer.device->waitIdle();
            if (spectra::Options != nullptr) spectra::GPUWait();
            if (pathtracer.integrator != nullptr) pathtracer.integrator->ReleaseAggregate();
        } catch (...) {
        }
        destroy_pathtracer_frame_resources_noexcept(pathtracer);
        pathtracer.integrator.reset();
        pathtracer.builder.reset();
        pathtracer.scene.reset();
    }
}

namespace xayah::spectra_pathtracer {

    PathtracerSession::PathtracerSession(const SceneSession& spectra_scene, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) : state{std::make_unique<PathtracerSessionState>()} {
        try {
            PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
            if (spectra_scene.scene_path.empty()) throw std::runtime_error("Cannot create Spectra pathtracer without a loaded Spectra scene");
            if (!std::filesystem::exists(spectra_scene.scene_path)) throw std::runtime_error(std::string{"Spectra pathtracer scene does not exist: "} + spectra_scene.scene_path.string());
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra pathtracer with a non-positive resolution");
            if (frame_count == 0) throw std::runtime_error("Spectra pathtracer requires at least one frame in flight");
            if (spectra::Options == nullptr) throw std::runtime_error("Cannot create Spectra pathtracer before Spectra pathtracer runtime is initialized");

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
            if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive");
            pathtracer.max_samples = pathtracer.integrator->sampler.SamplesPerPixel();
            if (pathtracer.max_samples <= 0) throw std::runtime_error("Spectra pathtracer sampler SPP must be positive");
            pathtracer.target_samples = pathtracer.max_samples;
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, spectra::Transform{}, pathtracer.sample_index);
            ++pathtracer.sample_index;
            spectra::GPUWait();

            pathtracer.render_from_camera = pathtracer.integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
            pathtracer.camera_from_render = spectra::Inverse(pathtracer.render_from_camera);
            pathtracer.camera_from_world  = pathtracer.integrator->camera.GetCameraTransform().CameraFromWorld(pathtracer.integrator->camera.SampleTime(0.0f));
            const spectra::Bounds3f scene_bounds = pathtracer.integrator->Bounds();
            pathtracer.initial_move_scale     = spectra::Length(scene_bounds.Diagonal()) / 1000.0f;
            if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("Spectra pathtracer scene bounds must define a positive interactive move scale");
            const spectra::Transform world_from_render = spectra::Inverse(pathtracer.render_from_camera * pathtracer.camera_from_world);
            spectra::Bounds3f world_bounds{};
            bool has_world_bounds = false;
            for (const float x : std::array<float, 2>{scene_bounds.pMin.x, scene_bounds.pMax.x}) {
                for (const float y : std::array<float, 2>{scene_bounds.pMin.y, scene_bounds.pMax.y}) {
                    for (const float z : std::array<float, 2>{scene_bounds.pMin.z, scene_bounds.pMax.z}) {
                        const spectra::Point3f corner_world = world_from_render(spectra::Point3f{x, y, z});
                        validate_finite_point(corner_world, "Spectra pathtracer scene focus bounds contain a non-finite value");
                        if (!has_world_bounds) world_bounds = spectra::Bounds3f{corner_world};
                        else world_bounds = spectra::Union(world_bounds, corner_world);
                        has_world_bounds = true;
                    }
                }
            }
            if (!has_world_bounds) throw std::runtime_error("Spectra pathtracer scene focus bounds are unavailable");
            pathtracer.initial_focus_bounds = world_bounds;

            validate_cuda_vulkan_device(physical_device);
            create_pathtracer_frame_resources(pathtracer, physical_device, device, frame_count);
            create_pathtracer_viewport_descriptors(pathtracer);
        } catch (...) {
            if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
            throw;
        }
    }

    PathtracerSession::~PathtracerSession() noexcept {
        if (this->state != nullptr) destroy_pathtracer_resources_noexcept(*this->state);
    }

    [[nodiscard]] int PathtracerSession::current_sample() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.reset_requested) return 0;
        return pathtracer.sample_index;
    }

    [[nodiscard]] int PathtracerSession::sampler_sample_count() const {
        return require_pathtracer_state(this->state).max_samples;
    }

    [[nodiscard]] int PathtracerSession::target_sample_count() const {
        return require_pathtracer_state(this->state).target_samples;
    }

    [[nodiscard]] float PathtracerSession::current_exposure() const {
        return require_pathtracer_state(this->state).exposure;
    }

    [[nodiscard]] float PathtracerSession::camera_initial_move_scale() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (!(pathtracer.initial_move_scale > 0.0f)) throw std::runtime_error("Spectra pathtracer camera initial move scale must be positive");
        return pathtracer.initial_move_scale;
    }

    [[nodiscard]] spectra::Bounds3f PathtracerSession::camera_initial_focus_bounds() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        validate_bounds(pathtracer.initial_focus_bounds, "Spectra pathtracer camera initial focus bounds are invalid");
        return pathtracer.initial_focus_bounds;
    }

    [[nodiscard]] std::array<int, 2> PathtracerSession::film_resolution() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive before metadata is queried");
        return {pathtracer.resolution.x, pathtracer.resolution.y};
    }

    [[nodiscard]] spectra::Transform PathtracerSession::camera_from_world_transform() const {
        return require_pathtracer_state(this->state).camera_from_world;
    }

    [[nodiscard]] std::uint64_t PathtracerSession::film_pixel_count() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive before statistics are queried");
        return static_cast<std::uint64_t>(pathtracer.resolution.x) * static_cast<std::uint64_t>(pathtracer.resolution.y);
    }

    [[nodiscard]] float PathtracerSession::completion_ratio() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.target_samples <= 0) throw std::runtime_error("Spectra pathtracer target sample count must be positive before statistics are queried");
        const int visible_sample = this->current_sample();
        if (visible_sample < 0 || visible_sample > pathtracer.target_samples) throw std::runtime_error("Spectra pathtracer visible sample count is outside the target sample range");
        return static_cast<float>(visible_sample) / static_cast<float>(pathtracer.target_samples);
    }

    [[nodiscard]] VkDescriptorSet PathtracerSession::active_descriptor() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) return VK_NULL_HANDLE;
        return pathtracer.frames.at(pathtracer.active_frame_index).imgui_descriptor;
    }

    [[nodiscard]] vk::Semaphore PathtracerSession::active_cuda_complete_semaphore() const {
        const PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (pathtracer.frames.empty()) throw std::runtime_error("Spectra pathtracer completion semaphore requested without frame resources");
        return *pathtracer.frames.at(pathtracer.active_frame_index).cuda_complete_semaphore;
    }

    void PathtracerSession::set_target_sample_count(const int target_sample_count) {
        PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (target_sample_count < 1 || target_sample_count > pathtracer.max_samples) throw std::runtime_error("Spectra pathtracer target sample count is outside the sampler SPP range");
        if (target_sample_count == pathtracer.target_samples) return;
        pathtracer.target_samples = target_sample_count;
        this->request_reset_accumulation();
    }

    void PathtracerSession::set_exposure(const float value) {
        if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("Spectra pathtracer exposure must be in [0.001, 1000]");
        require_pathtracer_state(this->state).exposure = value;
    }

    void PathtracerSession::request_reset_accumulation() {
        require_pathtracer_state(this->state).reset_requested = true;
    }

    void PathtracerSession::release_viewport_descriptors_noexcept() noexcept {
        if (this->state != nullptr) release_pathtracer_viewport_descriptors_noexcept(*this->state);
    }

    void PathtracerSession::create_viewport_descriptors() {
        create_pathtracer_viewport_descriptors(require_pathtracer_state(this->state));
    }

    [[nodiscard]] PathtracerSession::RenderFrameResult PathtracerSession::render_frame(const std::uint32_t frame_index, const spectra::Transform& moving_from_camera) {
        PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        if (frame_index >= pathtracer.frames.size()) throw std::runtime_error("Spectra pathtracer frame index is out of range");
        pathtracer.active_frame_index = frame_index;
        RenderFrameResult result{};
        const spectra::Transform camera_motion = pathtracer.render_from_camera * moving_from_camera * pathtracer.camera_from_render;
        if (pathtracer.reset_requested) {
            if (pathtracer.physical_device == nullptr || pathtracer.device == nullptr) throw std::runtime_error("Spectra pathtracer Vulkan handles are not available for reset");
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
        PathtracerSessionState::FrameResource& output_frame = pathtracer.frames.at(frame_index);
        pathtracer.integrator->UpdateFramebufferFromFilm(pathtracer.pixel_bounds, pathtracer.exposure, output_frame.cuda_pixels);

        cudaExternalSemaphoreSignalParams signal_params{};
        CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&output_frame.cuda_external_semaphore, &signal_params, 1, 0));
        return result;
    }

    void PathtracerSession::record_copy(const vk::raii::CommandBuffer& command_buffer) {
        PathtracerSessionState& pathtracer = require_pathtracer_state(this->state);
        PathtracerSessionState::FrameResource& frame = pathtracer.frames.at(pathtracer.active_frame_index);
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


} // namespace xayah::spectra_pathtracer
