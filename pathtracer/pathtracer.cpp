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

#include <backends/imgui_impl_vulkan.h>
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <pathtracer/base/film.cuh>
#include <pathtracer/base/sampler.cuh>
#include <pathtracer/compiled_scene.cuh>
#include <pathtracer/core/cameras.cuh>
#include <pathtracer/core/kernel_config.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/core/textures.cuh>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/integrator.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/transform.cuh>
#include <pathtracer/util/vecmath.cuh>

#include <vulkan/vulkan_raii.hpp>

module spectra.pathtracer;

import spectra.scene;
import std;

namespace {
    constexpr int interactive_default_pixel_samples = 256;

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

    [[nodiscard]] bool scene_entity_has_integer_parameter(const spectra::scene::Scene::Entity& entity, const std::string_view name) {
        for (const spectra::scene::Scene::Parameter& parameter : entity.parameters) {
            if (parameter.type == "integer" && parameter.name == name) return true;
        }
        return false;
    }

} // namespace

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
} // namespace

namespace spectra::pathtracer {
    [[nodiscard]] std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const scene::Scene::ResolvedScene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride);

    struct RenderPipelineSceneResources {
        std::unique_ptr<CompiledPathtracerScene> compiled_scene{};
        std::unique_ptr<WavefrontPathtracer> integrator{};
    };

    struct RenderPipeline {
        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            PathtracerCudaExternalMemory cuda_external_memory{};
            PathtracerCudaExternalSemaphore cuda_external_semaphore{};
            PathtracerCudaMappedBuffer cuda_pixels{};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        RenderPipeline(const scene::Scene::ResolvedScene& scene, const RenderConfig& render_config, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count);
        ~RenderPipeline() noexcept;

        RenderPipeline(const RenderPipeline& other)                = delete;
        RenderPipeline(RenderPipeline&& other) noexcept            = delete;
        RenderPipeline& operator=(const RenderPipeline& other)     = delete;
        RenderPipeline& operator=(RenderPipeline&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const;
        [[nodiscard]] int sampler_sample_count() const;
        [[nodiscard]] int target_sample_count() const;
        [[nodiscard]] float current_exposure() const;
        [[nodiscard]] spectra::Bounds3f camera_initial_focus_bounds() const;
        [[nodiscard]] std::array<int, 2> film_resolution() const;
        [[nodiscard]] spectra::Transform camera_from_world_transform() const;
        [[nodiscard]] float completion_ratio() const;
        [[nodiscard]] VkDescriptorSet active_descriptor() const;
        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;
        void set_target_sample_count(int target_sample_count);
        void set_exposure(float value);
        void request_reset_accumulation();
        void release_viewport_descriptors_noexcept() noexcept;
        void create_viewport_descriptors();
        [[nodiscard]] RenderFrameResult render_frame(std::uint32_t frame_index, const spectra::Transform& moving_from_camera);
        void record_copy(const vk::raii::CommandBuffer& command_buffer);

        std::unique_ptr<PathtracerMemoryScope> scene_memory_scope{};
        std::unique_ptr<CompiledPathtracerScene> compiled_scene{};
        std::unique_ptr<WavefrontPathtracer> integrator{};
        RenderConfig render_config{};
        scene::Scene::Revision scene_revision{};
        spectra::Bounds2i pixel_bounds{};
        spectra::Vector2i resolution{};
        spectra::Transform render_from_camera{};
        spectra::Transform camera_from_render{};
        spectra::Transform camera_from_world{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        float exposure{1.0f};
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
} // namespace spectra::pathtracer

namespace {
    void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) {
        int cuda_device = 0;
        CUDA_CHECK(cudaGetDevice(&cuda_device));
        cudaDeviceProp cuda_properties{};
        CUDA_CHECK(cudaGetDeviceProperties(&cuda_properties, cuda_device));
        const auto vulkan_properties                    = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
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

    void release_pathtracer_viewport_descriptors_noexcept(spectra::pathtracer::RenderPipeline& pathtracer) noexcept {
        for (spectra::pathtracer::RenderPipeline::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(frame.imgui_descriptor);
                frame.imgui_descriptor = VK_NULL_HANDLE;
            }
        }
    }

    void create_pathtracer_viewport_descriptors(spectra::pathtracer::RenderPipeline& pathtracer) {
        for (spectra::pathtracer::RenderPipeline::FrameResource& frame : pathtracer.frames) {
            if (frame.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra pathtracer viewport descriptor is already allocated");
            frame.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*frame.sampler), static_cast<VkImageView>(*frame.image_view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            if (frame.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra pathtracer viewport descriptor");
        }
    }

    void destroy_pathtracer_frame_resources_noexcept(spectra::pathtracer::RenderPipeline& pathtracer) noexcept {
        release_pathtracer_viewport_descriptors_noexcept(pathtracer);
        for (spectra::pathtracer::RenderPipeline::FrameResource& frame : pathtracer.frames) {
            frame.cuda_pixels.ReleaseNoexcept();
            frame.cuda_external_semaphore.ReleaseNoexcept();
            frame.cuda_external_memory.ReleaseNoexcept();
        }
        pathtracer.frames.clear();
        pathtracer.active_frame_index = 0;
    }

    void create_pathtracer_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, spectra::pathtracer::RenderPipeline::FrameResource& frame, const vk::DeviceSize rgba_bytes) {
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
        frame.cuda_external_memory.Import(memory_handle_desc);
        CloseHandle(memory_handle);
#else
        const vk::MemoryGetFdInfoKHR memory_handle_info{*frame.interop_memory, spectra_external_memory_handle_type()};
        int memory_fd = device.getMemoryFdKHR(memory_handle_info);
        if (memory_fd < 0) throw std::runtime_error("Failed to export Vulkan memory FD for CUDA");
        memory_handle_desc.handle.fd = memory_fd;
        frame.cuda_external_memory.Import(memory_handle_desc);
        close(memory_fd);
#endif

        cudaExternalMemoryBufferDesc buffer_desc{};
        buffer_desc.offset = 0;
        buffer_desc.size   = static_cast<unsigned long long>(frame.interop_buffer_size);
        void* mapped_pixels = nullptr;
        CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(&mapped_pixels, frame.cuda_external_memory.get(), &buffer_desc));
        frame.cuda_pixels.Adopt(mapped_pixels);
    }

    void create_pathtracer_cuda_complete_semaphore(const vk::raii::Device& device, spectra::pathtracer::RenderPipeline::FrameResource& frame) {
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
        frame.cuda_external_semaphore.Import(semaphore_handle_desc);
        CloseHandle(semaphore_handle);
#else
        const vk::SemaphoreGetFdInfoKHR semaphore_handle_info{*frame.cuda_complete_semaphore, spectra_external_semaphore_handle_type()};
        int semaphore_fd = device.getSemaphoreFdKHR(semaphore_handle_info);
        if (semaphore_fd < 0) throw std::runtime_error("Failed to export Vulkan semaphore FD for CUDA");
        semaphore_handle_desc.handle.fd = semaphore_fd;
        frame.cuda_external_semaphore.Import(semaphore_handle_desc);
        close(semaphore_fd);
#endif
        if (!frame.cuda_external_semaphore.valid()) throw std::runtime_error("CUDA external semaphore import returned null");
    }

    void create_pathtracer_display_image(spectra::pathtracer::RenderPipeline& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, spectra::pathtracer::RenderPipeline::FrameResource& frame) {
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

    void create_pathtracer_frame_resources(spectra::pathtracer::RenderPipeline& pathtracer, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
        const vk::FormatProperties format_properties       = physical_device.getFormatProperties(pathtracer.display_format);
        constexpr vk::FormatFeatureFlags required_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
        if ((format_properties.optimalTilingFeatures & required_features) != required_features) throw std::runtime_error("Vulkan device does not support sampled transfer destination R32G32B32A32_SFLOAT images");

        const vk::DeviceSize rgba_bytes = static_cast<vk::DeviceSize>(sizeof(float)) * 4u * static_cast<vk::DeviceSize>(pathtracer.resolution.x) * static_cast<vk::DeviceSize>(pathtracer.resolution.y);
        if (rgba_bytes == 0) throw std::runtime_error("Spectra pathtracer interop buffer cannot be zero bytes");
        pathtracer.frames.resize(frame_count);
        for (spectra::pathtracer::RenderPipeline::FrameResource& frame : pathtracer.frames) {
            create_pathtracer_interop_buffer(physical_device, device, frame, rgba_bytes);
            create_pathtracer_cuda_complete_semaphore(device, frame);
            create_pathtracer_display_image(pathtracer, physical_device, device, frame);
        }
    }

    void clear_pathtracer_texture_caches_noexcept() noexcept {
        try {
            spectra::ClearPathtracerTextureCaches();
        } catch (...) {
        }
    }

    void destroy_pathtracer_scene_resources_noexcept(spectra::pathtracer::RenderPipeline& pathtracer) noexcept {
        try {
            if (pathtracer.device != nullptr) pathtracer.device->waitIdle();
            if (pathtracer.integrator != nullptr) spectra::GPUWait();
            if (pathtracer.integrator != nullptr) pathtracer.integrator->ReleaseAggregate();
        } catch (...) {
        }
        pathtracer.integrator.reset();
        pathtracer.compiled_scene.reset();
        clear_pathtracer_texture_caches_noexcept();
        if (pathtracer.scene_memory_scope != nullptr) {
            pathtracer.scene_memory_scope->ReleaseAllNoexcept();
            pathtracer.scene_memory_scope.reset();
        }
        pathtracer.pixel_bounds         = spectra::Bounds2i{};
        pathtracer.resolution           = spectra::Vector2i{};
        pathtracer.render_from_camera   = spectra::Transform{};
        pathtracer.camera_from_render   = spectra::Transform{};
        pathtracer.camera_from_world    = spectra::Transform{};
        pathtracer.initial_focus_bounds = spectra::Bounds3f{};
        pathtracer.sample_index         = 0;
        pathtracer.max_samples          = 0;
        pathtracer.target_samples       = 0;
        pathtracer.reset_requested      = false;
    }

    void destroy_pathtracer_resources_noexcept(spectra::pathtracer::RenderPipeline& pathtracer) noexcept {
        destroy_pathtracer_scene_resources_noexcept(pathtracer);
        destroy_pathtracer_frame_resources_noexcept(pathtracer);
    }

    [[nodiscard]] std::unique_ptr<spectra::pathtracer::PathtracerMemoryScope> create_pathtracer_scene_memory_scope() {
        return std::make_unique<spectra::pathtracer::PathtracerMemoryScope>(spectra::pathtracer::PathtracerMemoryScopeKind::Scene, "pathtracer scene");
    }

    [[nodiscard]] spectra::pathtracer::RenderPipelineSceneResources create_pathtracer_runtime_resources(const spectra::scene::Scene::ResolvedScene& scene, const spectra::pathtracer::RenderConfig& render_config, const std::array<int, 2>& resolution, spectra::pathtracer::PathtracerMemoryScope* scene_memory_scope) {
        if (scene.name.empty()) throw std::runtime_error("Cannot create Spectra pathtracer without a loaded Spectra scene snapshot");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra pathtracer with a non-positive resolution");
        if (scene_memory_scope == nullptr) throw std::runtime_error("Cannot create Spectra pathtracer runtime resources without scene memory");
        std::unique_ptr<spectra::pathtracer::CompiledPathtracerScene> compiled_scene = spectra::pathtracer::CompilePathtracerScene(scene, render_config, scene_memory_scope, spectra::Point2i{resolution[0], resolution[1]});
        std::unique_ptr<spectra::pathtracer::WavefrontPathtracer> integrator         = std::make_unique<spectra::pathtracer::WavefrontPathtracer>(scene_memory_scope, *compiled_scene, render_config);
        return spectra::pathtracer::RenderPipelineSceneResources{
            .compiled_scene = std::move(compiled_scene),
            .integrator     = std::move(integrator),
        };
    }

    void initialize_pathtracer_integrator_state(spectra::pathtracer::RenderPipeline& pathtracer) {
        if (pathtracer.integrator == nullptr) throw std::runtime_error("Cannot initialize Spectra pathtracer state without an integrator");
        pathtracer.integrator->PrefetchGPUAllocations();
        pathtracer.pixel_bounds = pathtracer.integrator->film.PixelBounds();
        pathtracer.resolution   = pathtracer.pixel_bounds.Diagonal();
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive");
        pathtracer.max_samples = pathtracer.integrator->sampler.SamplesPerPixel();
        if (pathtracer.max_samples <= 0) throw std::runtime_error("Spectra pathtracer sampler SPP must be positive");
        pathtracer.target_samples     = pathtracer.max_samples;
        pathtracer.sample_index       = 0;
        pathtracer.reset_requested    = false;
        pathtracer.render_from_camera = pathtracer.integrator->camera.GetCameraTransform().RenderFromCamera().startTransform;
        pathtracer.camera_from_render = spectra::Inverse(pathtracer.render_from_camera);
        pathtracer.camera_from_world  = pathtracer.integrator->camera.GetCameraTransform().CameraFromWorld(pathtracer.integrator->camera.SampleTime(0.0f));

        pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, spectra::Transform{}, pathtracer.sample_index);
        ++pathtracer.sample_index;
        spectra::GPUWait();

        const spectra::Bounds3f scene_bounds = pathtracer.integrator->Bounds();
        const spectra::Transform world_from_render = spectra::Inverse(pathtracer.render_from_camera * pathtracer.camera_from_world);
        spectra::Bounds3f world_bounds{};
        bool has_world_bounds = false;
        for (const float x : std::array<float, 2>{scene_bounds.pMin.x, scene_bounds.pMax.x}) {
            for (const float y : std::array<float, 2>{scene_bounds.pMin.y, scene_bounds.pMax.y}) {
                for (const float z : std::array<float, 2>{scene_bounds.pMin.z, scene_bounds.pMax.z}) {
                    const spectra::Point3f corner_world = world_from_render(spectra::Point3f{x, y, z});
                    validate_finite_point(corner_world, "Spectra pathtracer scene focus bounds contain a non-finite value");
                    if (!has_world_bounds)
                        world_bounds = spectra::Bounds3f{corner_world};
                    else
                        world_bounds = spectra::Union(world_bounds, corner_world);
                    has_world_bounds = true;
                }
            }
        }
        if (!has_world_bounds) throw std::runtime_error("Spectra pathtracer scene focus bounds are unavailable");
        pathtracer.initial_focus_bounds = world_bounds;
    }
} // namespace

namespace spectra::pathtracer {
    RenderPipeline::RenderPipeline(const scene::Scene::ResolvedScene& scene, const RenderConfig& render_config, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count) {
        try {
            RenderPipeline& pathtracer = *this;
            if (scene.name.empty()) throw std::runtime_error("Cannot create Spectra pathtracer without a loaded Spectra scene snapshot");
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra pathtracer with a non-positive resolution");
            if (frame_count == 0) throw std::runtime_error("Spectra pathtracer requires at least one frame in flight");

            pathtracer.physical_device = &physical_device;
            pathtracer.device          = &device;
            pathtracer.frame_count     = frame_count;
            pathtracer.render_config   = render_config;
            pathtracer.scene_revision  = scene.revision;

            pathtracer.scene_memory_scope        = create_pathtracer_scene_memory_scope();
            RenderPipelineSceneResources resources = create_pathtracer_runtime_resources(scene, render_config, resolution, pathtracer.scene_memory_scope.get());
            pathtracer.compiled_scene            = std::move(resources.compiled_scene);
            pathtracer.integrator                = std::move(resources.integrator);
            initialize_pathtracer_integrator_state(pathtracer);

            validate_cuda_vulkan_device(physical_device);
            create_pathtracer_frame_resources(pathtracer, physical_device, device, frame_count);
            create_pathtracer_viewport_descriptors(pathtracer);
        } catch (...) {
            destroy_pathtracer_resources_noexcept(*this);
            throw;
        }
    }

    RenderPipeline::~RenderPipeline() noexcept {
        destroy_pathtracer_resources_noexcept(*this);
    }

    [[nodiscard]] int RenderPipeline::current_sample() const {
        const RenderPipeline& pathtracer = *this;
        if (pathtracer.reset_requested) return 0;
        return pathtracer.sample_index;
    }

    [[nodiscard]] int RenderPipeline::sampler_sample_count() const {
        return this->max_samples;
    }

    [[nodiscard]] int RenderPipeline::target_sample_count() const {
        return this->target_samples;
    }

    [[nodiscard]] float RenderPipeline::current_exposure() const {
        return this->exposure;
    }

    [[nodiscard]] spectra::Bounds3f RenderPipeline::camera_initial_focus_bounds() const {
        const RenderPipeline& pathtracer = *this;
        validate_bounds(pathtracer.initial_focus_bounds, "Spectra pathtracer camera initial focus bounds are invalid");
        return pathtracer.initial_focus_bounds;
    }

    [[nodiscard]] std::array<int, 2> RenderPipeline::film_resolution() const {
        const RenderPipeline& pathtracer = *this;
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive before metadata is queried");
        return {pathtracer.resolution.x, pathtracer.resolution.y};
    }

    [[nodiscard]] spectra::Transform RenderPipeline::camera_from_world_transform() const {
        return this->camera_from_world;
    }

    [[nodiscard]] float RenderPipeline::completion_ratio() const {
        const RenderPipeline& pathtracer = *this;
        if (pathtracer.target_samples <= 0) throw std::runtime_error("Spectra pathtracer target sample count must be positive before statistics are queried");
        const int visible_sample = this->current_sample();
        if (visible_sample < 0 || visible_sample > pathtracer.target_samples) throw std::runtime_error("Spectra pathtracer visible sample count is outside the target sample range");
        return static_cast<float>(visible_sample) / static_cast<float>(pathtracer.target_samples);
    }

    [[nodiscard]] VkDescriptorSet RenderPipeline::active_descriptor() const {
        const RenderPipeline& pathtracer = *this;
        if (pathtracer.frames.empty()) return VK_NULL_HANDLE;
        return pathtracer.frames.at(pathtracer.active_frame_index).imgui_descriptor;
    }

    [[nodiscard]] vk::Semaphore RenderPipeline::active_cuda_complete_semaphore() const {
        const RenderPipeline& pathtracer = *this;
        if (pathtracer.frames.empty()) throw std::runtime_error("Spectra pathtracer completion semaphore requested without frame resources");
        return *pathtracer.frames.at(pathtracer.active_frame_index).cuda_complete_semaphore;
    }

    void RenderPipeline::set_target_sample_count(const int target_sample_count) {
        RenderPipeline& pathtracer = *this;
        if (target_sample_count < 1 || target_sample_count > pathtracer.max_samples) throw std::runtime_error("Spectra pathtracer target sample count is outside the sampler SPP range");
        if (target_sample_count == pathtracer.target_samples) return;
        pathtracer.target_samples = target_sample_count;
        this->request_reset_accumulation();
    }

    void RenderPipeline::set_exposure(const float value) {
        if (!(value >= 0.001f && value <= 1000.0f)) throw std::runtime_error("Spectra pathtracer exposure must be in [0.001, 1000]");
        this->exposure = value;
    }

    void RenderPipeline::request_reset_accumulation() {
        this->reset_requested = true;
    }

    void RenderPipeline::release_viewport_descriptors_noexcept() noexcept {
        release_pathtracer_viewport_descriptors_noexcept(*this);
    }

    void RenderPipeline::create_viewport_descriptors() {
        create_pathtracer_viewport_descriptors(*this);
    }

    [[nodiscard]] RenderPipeline::RenderFrameResult RenderPipeline::render_frame(const std::uint32_t frame_index, const spectra::Transform& moving_from_camera) {
        RenderPipeline& pathtracer = *this;
        if (frame_index >= pathtracer.frames.size()) throw std::runtime_error("Spectra pathtracer frame index is out of range");
        if (pathtracer.resolution.x <= 0 || pathtracer.resolution.y <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive before statistics are queried");
        pathtracer.active_frame_index = frame_index;
        RenderFrameResult result{};
        const std::uint64_t sample_pixels      = static_cast<std::uint64_t>(pathtracer.resolution.x) * static_cast<std::uint64_t>(pathtracer.resolution.y);
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
            result.rendered_sample        = true;
            result.sample_pixels          = sample_pixels * static_cast<std::uint64_t>(pathtracer.sample_index);
            result.reset_accumulation     = true;
        } else if (pathtracer.sample_index < pathtracer.target_samples) {
            pathtracer.integrator->RenderSample(pathtracer.pixel_bounds, camera_motion, pathtracer.sample_index);
            ++pathtracer.sample_index;
            result.rendered_sample = true;
            result.sample_pixels   = sample_pixels;
        }
        FrameResource& output_frame = pathtracer.frames.at(frame_index);
        pathtracer.integrator->UpdateFramebufferFromFilm(pathtracer.pixel_bounds, pathtracer.exposure, static_cast<float*>(output_frame.cuda_pixels.data()));

        cudaExternalSemaphoreSignalParams signal_params{};
        cudaExternalSemaphore_t cuda_external_semaphore = output_frame.cuda_external_semaphore.get();
        CUDA_CHECK(cudaSignalExternalSemaphoresAsync(&cuda_external_semaphore, &signal_params, 1, 0));
        return result;
    }

    void RenderPipeline::record_copy(const vk::raii::CommandBuffer& command_buffer) {
        RenderPipeline& pathtracer                    = *this;
        FrameResource& frame                          = pathtracer.frames.at(pathtracer.active_frame_index);
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
} // namespace spectra::pathtracer


namespace {
    void validate_finite_vector(const spectra::Vector3f& vector, const char* message) {
        if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z)) throw std::runtime_error(message);
    }

    void validate_transform_matrix(const spectra::Transform& transform, const char* message) {
        const spectra::SquareMatrix<4>& matrix  = transform.GetMatrix();
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

    [[nodiscard]] spectra::Vector3f interactive_camera_effective_up(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up) {
        const spectra::Vector3f view_direction = center - eye;
        if (finite_length(spectra::Cross(view_direction, up), "Camera view/up cross product is invalid") > 1.0e-10f) return up;
        return std::abs(up.y) < 0.9f ? spectra::Vector3f{0.0f, 1.0f, 0.0f} : spectra::Vector3f{1.0f, 0.0f, 0.0f};
    }

    struct InteractiveCameraFrame {
        spectra::Vector3f forward{};
        spectra::Vector3f right{};
        spectra::Vector3f up{};
    };

    [[nodiscard]] InteractiveCameraFrame interactive_camera_frame_from_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up) {
        InteractiveCameraFrame frame{};
        frame.forward                          = normalized_vector(center - eye, "Camera eye and center must not overlap");
        const spectra::Vector3f effective_up   = interactive_camera_effective_up(eye, center, up);
        const spectra::Vector3f positive_right = normalized_vector(spectra::Cross(effective_up, frame.forward), "Camera right vector is invalid");
        frame.right                            = positive_right;
        frame.up                               = spectra::Cross(frame.forward, positive_right);
        return frame;
    }

    [[nodiscard]] spectra::Transform camera_from_world_transform_from_interactive_pose(const spectra::Point3f& eye, const spectra::Point3f& center, const spectra::Vector3f& up) {
        const InteractiveCameraFrame frame = interactive_camera_frame_from_pose(eye, center, up);
        const spectra::Vector3f eye_vector{eye.x, eye.y, eye.z};
        spectra::Transform transform{spectra::SquareMatrix<4>{
            frame.right.x,
            frame.right.y,
            frame.right.z,
            -spectra::Dot(frame.right, eye_vector),
            frame.up.x,
            frame.up.y,
            frame.up.z,
            -spectra::Dot(frame.up, eye_vector),
            frame.forward.x,
            frame.forward.y,
            frame.forward.z,
            -spectra::Dot(frame.forward, eye_vector),
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        }};
        validate_transform_matrix(transform, "Camera transform contains a non-finite value");
        return transform;
    }

    [[nodiscard]] spectra::Point3f interactive_camera_focus_center_from_bounds(const spectra::Point3f& eye, const spectra::Vector3f& forward, const spectra::Bounds3f& focus_bounds) {
        validate_bounds(focus_bounds, "Camera focus bounds are invalid");

        const spectra::Point3f bounds_center{
            (focus_bounds.pMin.x + focus_bounds.pMax.x) * 0.5f,
            (focus_bounds.pMin.y + focus_bounds.pMax.y) * 0.5f,
            (focus_bounds.pMin.z + focus_bounds.pMax.z) * 0.5f,
        };
        float focus_distance = spectra::Dot(bounds_center - eye, forward);

        constexpr float parallel_epsilon = 1.0e-7f;
        constexpr float distance_epsilon = 1.0e-5f;
        float ray_min                    = 0.0f;
        float ray_max                    = std::numeric_limits<float>::infinity();
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
        focus_distance          = std::clamp(focus_distance, lower_bound, ray_max);
        if (!(focus_distance > distance_epsilon) || !std::isfinite(focus_distance)) throw std::runtime_error("Camera focus distance is invalid");
        return eye + forward * focus_distance;
    }

} // namespace

namespace spectra::pathtracer {
    struct InteractiveCameraPose {
        spectra::Point3f eye{};
        spectra::Point3f center{};
        spectra::Vector3f up{};
    };

    [[nodiscard]] scene::Vector3 to_scene_vector(const spectra::Point3f& value) {
        return scene::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] scene::Vector3 to_scene_vector(const spectra::Vector3f& value) {
        return scene::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] spectra::Point3f to_pathtracer_point(const scene::Vector3 value) {
        return spectra::Point3f{value.x, value.y, value.z};
    }

    [[nodiscard]] spectra::Vector3f to_pathtracer_vector(const scene::Vector3 value) {
        return spectra::Vector3f{value.x, value.y, value.z};
    }

    [[nodiscard]] scene::Scene::CameraState scene_camera_state_from_interactive_pose(const InteractiveCameraPose& pose, const float fov_degrees) {
        return scene::Scene::CameraState{
            .eye                = to_scene_vector(pose.eye),
            .target             = to_scene_vector(pose.center),
            .up                 = to_scene_vector(pose.up),
            .vertical_fov_degrees = fov_degrees,
        };
    }

    [[nodiscard]] InteractiveCameraPose interactive_camera_pose_from_scene_camera_state(const scene::Scene::CameraState& state) {
        return InteractiveCameraPose{
            .eye    = to_pathtracer_point(state.eye),
            .center = to_pathtracer_point(state.target),
            .up     = to_pathtracer_vector(state.up),
        };
    }

    [[nodiscard]] float interactive_camera_fov_degrees(const scene::Scene::Info& info) {
        if (info.camera != "perspective") throw std::runtime_error(std::format("Interactive Spectra pathtracer camera controls require a perspective camera, not \"{}\"", info.camera));
        if (!(info.camera_fov_degrees > 0.0f && info.camera_fov_degrees < 180.0f)) throw std::runtime_error("Spectra scene camera fov must be inside (0, 180)");
        return info.camera_fov_degrees;
    }

    [[nodiscard]] InteractiveCameraPose interactive_camera_pose_from_base_transform(const spectra::Transform& camera_from_world, const spectra::Bounds3f& focus_bounds) {
        const spectra::Transform world_from_camera = spectra::Inverse(camera_from_world);
        InteractiveCameraPose pose{};
        pose.eye                               = world_from_camera(spectra::Point3f{0.0f, 0.0f, 0.0f});
        const spectra::Vector3f forward        = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 0.0f, 1.0f}), "Base camera forward vector is invalid");
        pose.up                                = normalized_vector(world_from_camera(spectra::Vector3f{0.0f, 1.0f, 0.0f}), "Base camera up vector is invalid");
        pose.center                            = interactive_camera_focus_center_from_bounds(pose.eye, forward, focus_bounds);
        return pose;
    }

    [[nodiscard]] spectra::Transform moving_from_camera_from_interactive_pose(const spectra::Transform& base_camera_from_world, const InteractiveCameraPose& pose) {
        const spectra::Transform current_camera_from_world = camera_from_world_transform_from_interactive_pose(pose.eye, pose.center, pose.up);
        return base_camera_from_world * spectra::Inverse(current_camera_from_world);
    }

} // namespace spectra::pathtracer


namespace spectra::pathtracer {
    [[nodiscard]] PathtracerSceneSupportReport analyze_pathtracer_scene(const scene::Scene::ResolvedScene& document) {
        PathtracerSceneSupportReport report = AnalyzePathtracerSceneSupport(document);
        if (report.target.empty()) report.target = std::string{PathtracerRenderer::name()};
        return report;
    }

    class PathtracerRenderer::Impl {
    public:
        Impl(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace);
        ~Impl() noexcept;

        void attach(PathtracerHostView host);
        void set_scene_workspace(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] PathtracerFrameResult begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        struct PathtracerStatus {
            bool uses_external_completion{false};
            std::string state{};
        };

        struct RollingFloatAverage {
            static constexpr std::size_t sample_count{100};

            std::array<float, sample_count> values{};
            std::size_t count{0};
            std::size_t cursor{0};
            float sum{0.0f};

            void clear();
            void add(float value);
            [[nodiscard]] bool has_value() const;
            [[nodiscard]] float average() const;
        };

        void register_panels(PathtracerHostView& host);
        void detach_noexcept() noexcept;
        void update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count, vk::Extent2D swapchain_extent);
        [[nodiscard]] std::string window_detail() const;
        [[nodiscard]] const scene::Scene::Info& active_scene_info() const;
        [[nodiscard]] const scene::Scene::ResolvedScene& active_scene_snapshot() const;
        [[nodiscard]] std::string active_scene_id() const;
        void load_source_scene();

        void draw_viewport_window();
        void draw_render_tab();
        void draw_camera_tab();
        void draw_scene_tab();
        void draw_tone_mapping_tab();
        void draw_performance_overlay();

        void unload_scene_noexcept() noexcept;
        void create_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution, bool force = false, bool preserve_target_samples = true);
        void rebuild_pathtracer_for_sample_config();
        void request_pathtracer_sample_config_rebuild();
        void unload_pathtracer_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool pathtracer_ready() const;
        [[nodiscard]] std::array<int, 2> pathtracer_sample_range() const;
        void request_pathtracer_accumulation_reset();
        void set_default_pixel_samples(int sample_count);
        void set_override_pixel_samples(std::optional<int> sample_count);
        [[nodiscard]] std::string sample_source_text() const;
        void initialize_camera_state();
        void synchronize_camera_workspace();
        void apply_camera_snapshot(const scene::Scene::CameraSnapshot& snapshot);
        void commit_camera_state(scene::Scene::CameraState state);
        void reset_camera();
        void clear_pathtracer_throughput_statistics();
        void update_frame_statistics(std::uint32_t frame_index, std::uint32_t image_index, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        [[nodiscard]] PathtracerStatus pathtracer_status() const;
        [[nodiscard]] bool process_camera_input();

        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        std::uint32_t frame_count{};
        vk::Extent2D swapchain_extent{};
        bool attached{false};
        bool performance_overlay_visible{true};
        std::shared_ptr<const scene::Scene> source_scene{};
        std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace{};

        struct {
            bool viewport_known{false};
            bool viewport_hovered{false};
            bool viewport_focused{false};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            std::array<int, 2> viewport_framebuffer_size{0, 0};
        } ui;

        std::optional<scene::Scene::ResolvedScene> scene_snapshot{};
        std::optional<scene::Scene::Info> scene_info{};
        RuntimeConfig runtime_config{.thread_count = 30, .cuda_device = 0};
        RenderConfig render_config{.rendering_space = RenderingSpace::CameraWorld, .default_pixel_samples = interactive_default_pixel_samples};
        std::array<int, 2> scene_film_resolution{0, 0};
        spectra::Transform scene_camera_from_world{};
        int scene_sampler_sample_count{0};
        int override_pixel_samples_input{interactive_default_pixel_samples};
        bool sample_config_rebuild_requested{false};
        std::unique_ptr<RenderPipeline> render_pipeline{};
        std::unique_ptr<GpuRuntime> gpu_runtime{};

        struct {
            bool candidate_known{false};
            bool pathtracer_created{false};
            bool rebuilding{false};
            float stable_seconds{0.0f};
            std::array<int, 2> candidate_resolution{0, 0};
            std::array<int, 2> active_resolution{0, 0};
        } render_resolution_sync;

        struct {
            bool initialized{false};
            bool input_enabled{false};
            float fov_degrees{60.0f};
            spectra::Point3f eye{0.0f, 0.0f, 0.0f};
            spectra::Point3f center{0.0f, 0.0f, 1.0f};
            spectra::Vector3f up{0.0f, 1.0f, 0.0f};
            spectra::Transform moving_from_camera{};
            spectra::Transform camera_from_world{};
            scene::Scene::Revision observed_revision{};
        } camera;

        struct {
            RollingFloatAverage frame_milliseconds{};
            RollingFloatAverage throughput_mspp{};
            std::uint64_t current_frame_id{0};
            std::uint32_t active_frame_index{0};
            std::uint32_t active_swapchain_image_index{0};
            float last_frame_milliseconds{0.0f};
            float last_valid_throughput_mspp{0.0f};
            bool has_throughput{false};
            bool last_frame_rendered_sample{false};
        } statistics;
    };

    PathtracerRenderer::PathtracerRenderer(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) : impl(std::make_unique<Impl>(std::move(source_scene), std::move(camera_workspace))) {}

    PathtracerRenderer::~PathtracerRenderer() noexcept = default;

    PathtracerRenderer::PathtracerRenderer(PathtracerRenderer&& other) noexcept = default;

    PathtracerRenderer& PathtracerRenderer::operator=(PathtracerRenderer&& other) noexcept = default;

    void PathtracerRenderer::set_scene_workspace(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) {
        this->impl->set_scene_workspace(std::move(source_scene), std::move(camera_workspace));
    }

    std::string_view PathtracerRenderer::name() {
        return "Spectra Pathtracer";
    }

    void PathtracerRenderer::attach(PathtracerHostView host) {
        this->impl->attach(std::move(host));
    }

    void PathtracerRenderer::detach() noexcept {
        this->impl->detach();
    }

    void PathtracerRenderer::before_imgui_shutdown() noexcept {
        this->impl->before_imgui_shutdown();
    }

    void PathtracerRenderer::after_imgui_created() {
        this->impl->after_imgui_created();
    }

    PathtracerFrameResult PathtracerRenderer::begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame) {
        return this->impl->begin_frame(std::move(host), frame);
    }

    void PathtracerRenderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        this->impl->record_frame(command_buffer);
    }

    void PathtracerRenderer::Impl::RollingFloatAverage::clear() {
        this->values.fill(0.0f);
        this->count  = 0;
        this->cursor = 0;
        this->sum    = 0.0f;
    }

    void PathtracerRenderer::Impl::RollingFloatAverage::add(const float value) {
        if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("Rolling statistic value must be finite and non-negative");
        if (this->count < sample_count) {
            this->values[this->cursor] = value;
            this->sum += value;
            ++this->count;
        } else {
            this->sum -= this->values[this->cursor];
            this->values[this->cursor] = value;
            this->sum += value;
        }
        this->cursor = (this->cursor + 1) % sample_count;
    }

    bool PathtracerRenderer::Impl::RollingFloatAverage::has_value() const {
        return this->count > 0;
    }

    float PathtracerRenderer::Impl::RollingFloatAverage::average() const {
        if (this->count == 0) return 0.0f;
        return this->sum / static_cast<float>(this->count);
    }

    PathtracerRenderer::Impl::Impl(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) : source_scene(std::move(source_scene)), camera_workspace(std::move(camera_workspace)) {
        if (this->source_scene == nullptr) throw std::runtime_error("Spectra pathtracer requires a source scene");
        if (this->camera_workspace == nullptr) throw std::runtime_error("Spectra pathtracer requires a scene camera workspace");
        const scene::Scene::ResolvedScene snapshot = this->source_scene->resolved_scene();
        const PathtracerSceneSupportReport report = analyze_pathtracer_scene(snapshot);
        if (!report.supported) {
            std::string message = std::format("{} cannot translate scene \"{}\"", PathtracerRenderer::name(), snapshot.name);
            if (!report.diagnostics.empty()) message = std::format("{}: {}", message, report.diagnostics.front().message);
            throw std::runtime_error(message);
        }
    }

    PathtracerRenderer::Impl::~Impl() noexcept = default;

    void PathtracerRenderer::Impl::set_scene_workspace(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace) {
        if (source_scene == nullptr) throw std::runtime_error("Spectra pathtracer requires a source scene");
        if (camera_workspace == nullptr) throw std::runtime_error("Spectra pathtracer requires a scene camera workspace");
        if (this->source_scene == source_scene && this->camera_workspace == camera_workspace) return;
        this->unload_pathtracer_noexcept();
        this->unload_scene_noexcept();
        this->source_scene = std::move(source_scene);
        this->camera_workspace = std::move(camera_workspace);
    }

    void PathtracerRenderer::Impl::detach_noexcept() noexcept {
        this->unload_pathtracer_noexcept();
        this->unload_scene_noexcept();
        this->gpu_runtime.reset();
        this->physical_device  = nullptr;
        this->device           = nullptr;
        this->frame_count      = 0;
        this->swapchain_extent = vk::Extent2D{};
        this->attached         = false;
    }

    void PathtracerRenderer::Impl::update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count, const vk::Extent2D swapchain_extent) {
        if (frame_count == 0) throw std::runtime_error("Spectra pathtracer host frame count must be positive");
        if (swapchain_extent.width == 0 || swapchain_extent.height == 0) throw std::runtime_error("Spectra pathtracer host swapchain extent must be positive");
        this->physical_device  = &physical_device;
        this->device           = &device;
        this->frame_count      = frame_count;
        this->swapchain_extent = swapchain_extent;
    }

    const scene::Scene::Info& PathtracerRenderer::Impl::active_scene_info() const {
        if (!this->scene_info.has_value()) throw std::runtime_error("Spectra scene metadata is not loaded");
        return *this->scene_info;
    }

    const scene::Scene::ResolvedScene& PathtracerRenderer::Impl::active_scene_snapshot() const {
        if (!this->scene_snapshot.has_value()) throw std::runtime_error("Spectra scene snapshot is not loaded");
        return *this->scene_snapshot;
    }

    std::string PathtracerRenderer::Impl::active_scene_id() const {
        const scene::Scene::ResolvedScene& scene = this->active_scene_snapshot();
        if (scene.name.empty()) throw std::runtime_error("Spectra pathtracer scene id must not be empty");
        return scene.name;
    }

    void PathtracerRenderer::Impl::load_source_scene() {
        if (this->source_scene == nullptr) throw std::runtime_error("Spectra pathtracer requires a source scene");
        scene::Scene::ResolvedScene source_snapshot = this->source_scene->resolved_scene();
        if (this->scene_snapshot.has_value()) {
            if (this->scene_snapshot->revision == source_snapshot.revision) return;
            this->unload_pathtracer_noexcept();
            this->unload_scene_noexcept();
            source_snapshot = this->source_scene->resolved_scene();
        }

        const PathtracerSceneSupportReport report = analyze_pathtracer_scene(source_snapshot);
        if (!report.supported) {
            std::string message = std::format("{} cannot translate scene \"{}\"", PathtracerRenderer::name(), source_snapshot.name);
            if (!report.diagnostics.empty()) message = std::format("{}: {}", message, report.diagnostics.front().message);
            throw std::runtime_error(message);
        }

        this->scene_snapshot = std::move(source_snapshot);
        this->scene_info     = this->source_scene->info();
        this->scene_film_resolution      = {0, 0};
        this->scene_camera_from_world    = spectra::Transform{};
        this->scene_sampler_sample_count = 0;
    }

    void PathtracerRenderer::Impl::attach(PathtracerHostView host) {
        if (this->attached) throw std::runtime_error("Spectra pathtracer plugin is already attached");
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->attached = true;
        try {
            this->gpu_runtime = std::make_unique<GpuRuntime>(this->runtime_config);
            this->load_source_scene();
            this->register_panels(host);
        } catch (...) {
            this->detach_noexcept();
            throw;
        }
    }
    void PathtracerRenderer::Impl::detach() noexcept {
        this->detach_noexcept();
    }

    void PathtracerRenderer::Impl::before_imgui_shutdown() noexcept {
        if (this->render_pipeline != nullptr) this->render_pipeline->release_viewport_descriptors_noexcept();
    }

    void PathtracerRenderer::Impl::after_imgui_created() {
        if (this->render_pipeline != nullptr) this->render_pipeline->create_viewport_descriptors();
    }

    PathtracerFrameResult PathtracerRenderer::Impl::begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame) {
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        this->load_source_scene();
        if (!this->scene_info.has_value()) throw std::runtime_error("Cannot update Spectra pathtracer frame without an active Spectra scene");
        PathtracerFrameResult result{};
        this->synchronize_render_resolution();
        if (this->sample_config_rebuild_requested && this->pathtracer_ready()) this->rebuild_pathtracer_for_sample_config();
        if (this->pathtracer_ready()) {
            this->synchronize_camera_workspace();
            result.close_requested                                            = this->process_camera_input();
            const RenderPipeline::RenderFrameResult render_result = this->render_pipeline->render_frame(frame.frame_index, this->camera.moving_from_camera);
            result.completion_semaphore                                       = this->render_pipeline->active_cuda_complete_semaphore();
            this->update_frame_statistics(frame.frame_index, frame.image_index, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        } else {
            this->update_frame_statistics(frame.frame_index, frame.image_index, false, false, 0);
        }
        result.window_detail = this->window_detail();
        return result;
    }

    void PathtracerRenderer::Impl::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (this->pathtracer_ready()) this->render_pipeline->record_copy(command_buffer);
    }

    std::string PathtracerRenderer::Impl::window_detail() const {
        std::uint32_t width  = this->swapchain_extent.width;
        std::uint32_t height = this->swapchain_extent.height;
        if (this->render_resolution_sync.pathtracer_created) {
            width  = static_cast<std::uint32_t>(this->render_resolution_sync.active_resolution[0]);
            height = static_cast<std::uint32_t>(this->render_resolution_sync.active_resolution[1]);
        } else if (this->ui.viewport_known && this->ui.viewport_framebuffer_size[0] > 0 && this->ui.viewport_framebuffer_size[1] > 0) {
            width  = static_cast<std::uint32_t>(this->ui.viewport_framebuffer_size[0]);
            height = static_cast<std::uint32_t>(this->ui.viewport_framebuffer_size[1]);
        }
        const std::string scene_title         = !this->scene_info.has_value() ? "No Scene" : std::string(this->active_scene_info().title);
        const std::array<int, 2> sample_range = !this->scene_info.has_value() ? std::array<int, 2>{0, 0} : this->pathtracer_sample_range();
        return std::format("{} | Spectra Pathtracer | {}x{} | sample {}/{}", scene_title, width, height, sample_range[0], sample_range[1]);
    }

    void PathtracerRenderer::Impl::unload_scene_noexcept() noexcept {
        this->scene_snapshot.reset();
        this->scene_info.reset();
        this->scene_film_resolution      = {0, 0};
        this->scene_camera_from_world    = spectra::Transform{};
        this->scene_sampler_sample_count = 0;
        this->camera.initialized         = false;
        this->camera.observed_revision   = scene::Scene::Revision{};
    }

    void PathtracerRenderer::Impl::create_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (!this->scene_info.has_value()) throw std::runtime_error("Cannot create Spectra pathtracer without a loaded Spectra scene");
        if (this->render_pipeline != nullptr) throw std::runtime_error("Spectra pathtracer is already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra pathtracer with a non-positive resolution");
        try {
            if (this->gpu_runtime == nullptr) throw std::runtime_error("Spectra pathtracer runtime is not initialized");
            if (this->physical_device == nullptr || this->device == nullptr) throw std::runtime_error("Spectra pathtracer Vulkan handles are not available");
            this->gpu_runtime->UploadKernelConfig(KernelConfigFrom(this->render_config));
            this->render_pipeline            = std::make_unique<RenderPipeline>(this->active_scene_snapshot(), this->render_config, resolution, *this->physical_device, *this->device, this->frame_count);
            this->scene_film_resolution      = this->render_pipeline->film_resolution();
            this->scene_sampler_sample_count = this->render_pipeline->sampler_sample_count();
            this->scene_camera_from_world    = this->render_pipeline->camera_from_world_transform();
            if (this->scene_film_resolution[0] <= 0 || this->scene_film_resolution[1] <= 0) throw std::runtime_error("Spectra pathtracer film resolution must be positive");
            if (this->scene_sampler_sample_count <= 0) throw std::runtime_error("Spectra pathtracer sampler SPP must be positive");
            this->render_resolution_sync.active_resolution  = resolution;
            this->render_resolution_sync.pathtracer_created = true;
            this->sample_config_rebuild_requested           = false;
        } catch (...) {
            if (this->gpu_runtime != nullptr) this->gpu_runtime->WaitGpuNoexcept();
            this->unload_pathtracer_noexcept();
            throw;
        }
    }

    void PathtracerRenderer::Impl::rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution, const bool force, const bool preserve_target_samples) {
        if (this->render_resolution_sync.rebuilding) throw std::runtime_error("Spectra pathtracer resolution rebuild is already active");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot rebuild Spectra pathtracer with a non-positive resolution");
        if (!force && this->render_resolution_sync.pathtracer_created && this->render_resolution_sync.active_resolution == resolution) return;

        const int preserved_samples             = preserve_target_samples && this->render_pipeline != nullptr ? this->render_pipeline->target_sample_count() : 0;
        const float preserved_exposure          = this->render_pipeline == nullptr ? 1.0f : this->render_pipeline->current_exposure();
        this->render_resolution_sync.rebuilding = true;
        try {
            if (this->device == nullptr) throw std::runtime_error("Spectra pathtracer logical device is not available");
            this->device->waitIdle();
            if (this->gpu_runtime != nullptr) this->gpu_runtime->WaitGpuNoexcept();
            this->unload_pathtracer_noexcept();
            this->create_pathtracer_for_resolution(resolution);
            if (this->render_pipeline == nullptr) throw std::runtime_error("Spectra pathtracer was not created");
            if (preserved_samples > 0) this->render_pipeline->set_target_sample_count(preserved_samples);
            this->render_pipeline->set_exposure(preserved_exposure);
            this->initialize_camera_state();
            this->clear_pathtracer_throughput_statistics();
            this->statistics.last_frame_rendered_sample = false;
            this->render_resolution_sync.rebuilding     = false;
        } catch (...) {
            this->render_resolution_sync.rebuilding = false;
            throw;
        }
    }

    void PathtracerRenderer::Impl::rebuild_pathtracer_for_sample_config() {
        if (!this->pathtracer_ready()) throw std::runtime_error("Cannot rebuild Spectra pathtracer sample configuration before the render pipeline is ready");
        this->rebuild_pathtracer_for_resolution(this->render_pipeline->film_resolution(), true, false);
    }

    void PathtracerRenderer::Impl::request_pathtracer_sample_config_rebuild() {
        this->sample_config_rebuild_requested = true;
    }

    void PathtracerRenderer::Impl::unload_pathtracer_noexcept() noexcept {
        if (this->gpu_runtime != nullptr) this->gpu_runtime->WaitGpuNoexcept();
        this->render_pipeline.reset();
        this->render_resolution_sync.pathtracer_created = false;
        this->render_resolution_sync.active_resolution  = {0, 0};
    }

    void PathtracerRenderer::Impl::observe_viewport_render_resolution(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid while tracking viewport resolution");
        if (!this->render_resolution_sync.candidate_known || this->render_resolution_sync.candidate_resolution != resolution) {
            this->render_resolution_sync.candidate_known      = true;
            this->render_resolution_sync.candidate_resolution = resolution;
            this->render_resolution_sync.stable_seconds       = 0.0f;
            return;
        }
        this->render_resolution_sync.stable_seconds += io.DeltaTime;
    }

    void PathtracerRenderer::Impl::synchronize_render_resolution() {
        constexpr float resolution_stability_seconds = 0.3f;
        if (!this->scene_info.has_value()) return;
        if (!this->render_resolution_sync.candidate_known) return;
        if (this->render_resolution_sync.stable_seconds < resolution_stability_seconds) return;
        if (this->render_resolution_sync.pathtracer_created && this->render_resolution_sync.active_resolution == this->render_resolution_sync.candidate_resolution) return;
        this->rebuild_pathtracer_for_resolution(this->render_resolution_sync.candidate_resolution, false, !this->sample_config_rebuild_requested);
    }

    [[nodiscard]] bool PathtracerRenderer::Impl::pathtracer_ready() const {
        return this->render_resolution_sync.pathtracer_created && this->render_pipeline != nullptr;
    }

    [[nodiscard]] std::array<int, 2> PathtracerRenderer::Impl::pathtracer_sample_range() const {
        if (this->render_pipeline == nullptr) return {0, 0};
        return {this->render_pipeline->current_sample(), this->render_pipeline->target_sample_count()};
    }

    void PathtracerRenderer::Impl::request_pathtracer_accumulation_reset() {
        if (this->render_pipeline == nullptr) throw std::runtime_error("Cannot reset Spectra pathtracer accumulation without an active render pipeline");
        this->render_pipeline->request_reset_accumulation();
        this->clear_pathtracer_throughput_statistics();
    }

    void PathtracerRenderer::Impl::set_default_pixel_samples(const int sample_count) {
        if (sample_count <= 0) throw std::runtime_error("Spectra pathtracer default SPP must be positive");
        if (this->render_config.default_pixel_samples.has_value() && *this->render_config.default_pixel_samples == sample_count) return;
        this->render_config.default_pixel_samples = sample_count;
        if (this->render_config.pixel_samples.has_value()) return;
        if (!this->scene_snapshot.has_value()) throw std::runtime_error("Cannot apply Spectra pathtracer default SPP without an active scene snapshot");
        if (scene_entity_has_integer_parameter(this->scene_snapshot->render_settings.sampler, "pixelsamples")) return;
        this->request_pathtracer_sample_config_rebuild();
    }

    void PathtracerRenderer::Impl::set_override_pixel_samples(const std::optional<int> sample_count) {
        if (sample_count.has_value() && *sample_count <= 0) throw std::runtime_error("Spectra pathtracer override SPP must be positive");
        if (this->render_config.pixel_samples == sample_count) return;
        this->render_config.pixel_samples = sample_count;
        if (sample_count.has_value()) this->override_pixel_samples_input = *sample_count;
        this->request_pathtracer_sample_config_rebuild();
    }

    std::string PathtracerRenderer::Impl::sample_source_text() const {
        if (this->render_config.pixel_samples.has_value()) return "Override";
        if (this->scene_snapshot.has_value() && scene_entity_has_integer_parameter(this->scene_snapshot->render_settings.sampler, "pixelsamples")) return "Scene";
        if (this->render_config.default_pixel_samples.has_value()) return "Spectra Default";
        return "PBRT Default";
    }

    void PathtracerRenderer::Impl::initialize_camera_state() {
        if (!this->scene_info.has_value()) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        if (this->render_pipeline == nullptr) throw std::runtime_error("Spectra pathtracer camera focus bounds requested without an active render pipeline");
        this->camera.camera_from_world = this->scene_camera_from_world;
        const InteractiveCameraPose pose = interactive_camera_pose_from_base_transform(this->camera.camera_from_world, this->render_pipeline->camera_initial_focus_bounds());
        this->camera.input_enabled = false;
        this->camera.fov_degrees = interactive_camera_fov_degrees(this->active_scene_info());
        this->camera_workspace->ensure_camera(this->active_scene_id(), scene_camera_state_from_interactive_pose(pose, this->camera.fov_degrees));
        this->apply_camera_snapshot(this->camera_workspace->snapshot(this->active_scene_id()));
    }

    void PathtracerRenderer::Impl::synchronize_camera_workspace() {
        if (!this->pathtracer_ready()) return;
        if (!this->camera.initialized) this->initialize_camera_state();
        const scene::Scene::CameraSnapshot snapshot = this->camera_workspace->snapshot(this->active_scene_id());
        if (snapshot.revision == this->camera.observed_revision) return;
        this->apply_camera_snapshot(snapshot);
        this->request_pathtracer_accumulation_reset();
    }

    void PathtracerRenderer::Impl::apply_camera_snapshot(const scene::Scene::CameraSnapshot& snapshot) {
        if (!this->scene_info.has_value()) throw std::runtime_error("Cannot apply camera state without an active Spectra scene");
        if (this->render_pipeline == nullptr) throw std::runtime_error("Cannot apply camera state without an active Spectra pathtracer");
        const InteractiveCameraPose pose = interactive_camera_pose_from_scene_camera_state(snapshot.state);
        this->camera.eye = pose.eye;
        this->camera.center = pose.center;
        this->camera.up = pose.up;
        this->camera.fov_degrees = snapshot.state.vertical_fov_degrees;
        this->camera.input_enabled = false;
        this->camera.moving_from_camera = moving_from_camera_from_interactive_pose(this->camera.camera_from_world, pose);
        this->camera.initialized = true;
        this->camera.observed_revision = snapshot.revision;
    }

    void PathtracerRenderer::Impl::commit_camera_state(scene::Scene::CameraState state) {
        const scene::Scene::CameraSnapshot snapshot = this->camera_workspace->commit(this->active_scene_id(), std::move(state));
        this->camera.observed_revision = snapshot.revision;
    }

    void PathtracerRenderer::Impl::reset_camera() {
        if (!this->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        if (!this->pathtracer_ready()) throw std::runtime_error("Cannot reset camera without an active Spectra pathtracer");
        const InteractiveCameraPose pose = interactive_camera_pose_from_base_transform(this->camera.camera_from_world, this->render_pipeline->camera_initial_focus_bounds());
        const scene::Scene::CameraSnapshot snapshot = this->camera_workspace->commit(this->active_scene_id(), scene_camera_state_from_interactive_pose(pose, interactive_camera_fov_degrees(this->active_scene_info())));
        this->apply_camera_snapshot(snapshot);
        this->request_pathtracer_accumulation_reset();
    }

    void PathtracerRenderer::Impl::clear_pathtracer_throughput_statistics() {
        this->statistics.throughput_mspp.clear();
        this->statistics.last_valid_throughput_mspp = 0.0f;
        this->statistics.has_throughput             = false;
    }

    void PathtracerRenderer::Impl::update_frame_statistics(const std::uint32_t frame_index, const std::uint32_t image_index, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Spectra pathtracer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Spectra pathtracer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        ++this->statistics.current_frame_id;
        this->statistics.active_frame_index           = frame_index;
        this->statistics.active_swapchain_image_index = image_index;
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

    [[nodiscard]] PathtracerRenderer::Impl::PathtracerStatus PathtracerRenderer::Impl::pathtracer_status() const {
        PathtracerStatus status{};
        const std::array<int, 2> sample_range = this->pathtracer_sample_range();
        if (this->render_resolution_sync.rebuilding) {
            status.state = "Rebuilding";
            return status;
        }
        if (!this->render_resolution_sync.pathtracer_created) {
            status.state = this->render_resolution_sync.candidate_known ? "Pending Resolution" : "Waiting for Viewport";
            return status;
        }
        status.uses_external_completion = this->render_pipeline != nullptr;
        if (this->render_pipeline == nullptr) {
            status.state = "Unavailable";
            return status;
        }
        if (this->sample_config_rebuild_requested) {
            status.state = "Rebuild Pending";
            return status;
        }
        status.state = sample_range[0] >= sample_range[1] ? "Completed" : "Sampling";
        return status;
    }

    bool PathtracerRenderer::Impl::process_camera_input() {
        ImGuiIO& io                = ImGui::GetIO();
        const bool close_requested = !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->ui.viewport_known && mouse_position.x >= this->ui.viewport_position[0] && mouse_position.x < this->ui.viewport_position[0] + this->ui.viewport_size[0] && mouse_position.y >= this->ui.viewport_position[1] && mouse_position.y < this->ui.viewport_position[1] + this->ui.viewport_size[1];
        this->camera.input_enabled  = in_viewport_rect && (this->ui.viewport_hovered || this->ui.viewport_focused) && !io.WantTextInput;
        if (!this->camera.input_enabled) return close_requested;
        if (!this->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            this->reset_camera();
            return close_requested;
        }

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl;
        const bool alt   = io.KeyAlt;
        scene::Scene::CameraState state{
            .eye                = to_scene_vector(this->camera.eye),
            .target             = to_scene_vector(this->camera.center),
            .up                 = to_scene_vector(this->camera.up),
            .vertical_fov_degrees = this->camera.fov_degrees,
        };
        bool camera_changed = false;

        const std::array<float, 2> viewport_size = this->ui.viewport_size;
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        const scene::Scene::ViewportCameraSize scene_viewport_size{
            .width = viewport_size[0],
            .height = viewport_size[1],
        };
        const bool left_dragging   = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
        const bool middle_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
        const bool right_dragging  = ImGui::IsMouseDragging(ImGuiMouseButton_Right);
        if (left_dragging || middle_dragging || right_dragging) {
            const scene::Scene::ViewportCameraDelta delta{
                .x_pixels = io.MouseDelta.x,
                .y_pixels = io.MouseDelta.y,
            };
            if (delta.x_pixels != 0.0f || delta.y_pixels != 0.0f) {
                if (alt) {
                    if (left_dragging) {
                        state = scene::Scene::orbit_viewport_camera(state, delta);
                        camera_changed = true;
                    } else if (middle_dragging) {
                        state = scene::Scene::pan_viewport_camera(state, delta, scene_viewport_size);
                        camera_changed = true;
                    } else if (right_dragging) {
                        state = scene::Scene::zoom_viewport_camera(state, scene::Scene::viewport_drag_zoom_steps(delta));
                        camera_changed = true;
                    }
                } else if (middle_dragging) {
                    if (shift) {
                        state = scene::Scene::pan_viewport_camera(state, delta, scene_viewport_size);
                        camera_changed = true;
                    } else if (ctrl) {
                        state = scene::Scene::zoom_viewport_camera(state, scene::Scene::viewport_drag_zoom_steps(delta));
                        camera_changed = true;
                    } else {
                        state = scene::Scene::orbit_viewport_camera(state, delta);
                        camera_changed = true;
                    }
                }
            }
        }

        if (io.MouseWheel != 0.0f) {
            state = scene::Scene::zoom_viewport_camera(state, io.MouseWheel);
            camera_changed = true;
        }

        if (camera_changed) {
            const InteractiveCameraPose pose = interactive_camera_pose_from_scene_camera_state(state);
            this->camera.eye                = pose.eye;
            this->camera.center             = pose.center;
            this->camera.up                 = pose.up;
            this->camera.moving_from_camera = moving_from_camera_from_interactive_pose(this->camera.camera_from_world, pose);
            this->commit_camera_state(std::move(state));
            this->request_pathtracer_accumulation_reset();
        }
        return close_requested;
    }
} // namespace spectra::pathtracer


namespace {
    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string resolution_text(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) return "Pending";
        return std::format("{} x {}", resolution[0], resolution[1]);
    }

    [[nodiscard]] std::string positive_int_text(const int value) {
        if (value <= 0) return "Pending";
        return std::format("{}", value);
    }

    [[nodiscard]] ImFont* overlay_value_font() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts == nullptr) throw std::runtime_error("ImGui font atlas is unavailable");
        if (io.Fonts->Fonts.Size < 2) throw std::runtime_error("Spectra performance overlay requires the mono UI font");
        ImFont* font = io.Fonts->Fonts.back();
        if (font == nullptr) throw std::runtime_error("Spectra performance overlay mono font is null");
        return font;
    }

    void draw_overlay_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushFont(overlay_value_font());
        ImGui::TextUnformatted(value);
        ImGui::PopFont();
    }

    void draw_overlay_statistics_row(const char* label, const std::string& value) {
        draw_overlay_statistics_row(label, value.c_str());
    }
} // namespace

namespace spectra::pathtracer {
    void PathtracerRenderer::Impl::draw_viewport_window() {
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
        this->ui.viewport_known            = true;
        this->ui.viewport_position         = {viewport_position.x, viewport_position.y};
        this->ui.viewport_size             = {viewport_size.x, viewport_size.y};
        this->ui.viewport_framebuffer_size = viewport_framebuffer_size;
        this->ui.viewport_hovered          = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
        this->ui.viewport_focused          = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
        this->observe_viewport_render_resolution(viewport_framebuffer_size);
        if (this->pathtracer_ready()) {
            if (this->render_pipeline == nullptr) throw std::runtime_error("Spectra pathtracer viewport descriptor requested without an active render pipeline");
            const VkDescriptorSet descriptor = this->render_pipeline->active_descriptor();
            if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Spectra pathtracer viewport descriptor is null");
            const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
            ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
            ImGui::SetCursorScreenPos(viewport_position);
        } else if (this->scene_info.has_value()) {
            const char* pending_label = this->render_resolution_sync.rebuilding ? "Rebuilding pathtracer" : "Waiting for viewport resolution";
            const ImVec2 text_size    = ImGui::CalcTextSize(pending_label);
            ImGui::SetCursorScreenPos(ImVec2{viewport_position.x + std::max(0.0f, (viewport_size.x - text_size.x) * 0.5f), viewport_position.y + std::max(0.0f, (viewport_size.y - text_size.y) * 0.5f)});
            ImGui::TextDisabled("%s", pending_label);
            ImGui::SetCursorScreenPos(viewport_position);
        }
        ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        this->draw_performance_overlay();
    }

    void PathtracerRenderer::Impl::draw_camera_tab() {
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const PathtracerStatus pathtracer_status = this->pathtracer_status();

            draw_statistics_row("Path Tracer", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->camera.initialized || !this->pathtracer_ready());
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
    }

    void PathtracerRenderer::Impl::draw_scene_tab() {
        if (!this->scene_info.has_value()) {
            ImGui::TextDisabled("No active Spectra scene");
            return;
        }

        const scene::Scene::Info& scene = this->active_scene_info();
        constexpr ImGuiTableFlags table_flags  = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("PathtracerSceneSummary", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Name", std::string(scene.name));
            draw_statistics_row("Title", std::string(scene.title));
            draw_statistics_row("Film Resolution", resolution_text(this->scene_film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->scene_sampler_sample_count));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Pipeline");
        if (ImGui::BeginTable("SpectraScenePipeline", 2, table_flags)) {
            ImGui::TableSetupColumn("Stage", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Implementation", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Camera", std::string(scene.camera));
            draw_statistics_row("Sampler", std::string(scene.sampler));
            draw_statistics_row("Integrator", std::string(scene.integrator));
            draw_statistics_row("Accelerator", std::string(scene.accelerator));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("SpectraSceneResources", 2, table_flags)) {
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Shapes", std::format("{}", scene.shape_count));
            draw_statistics_row("Materials", std::format("{}", scene.material_count));
            draw_statistics_row("Textures", std::format("{}", scene.texture_count));
            draw_statistics_row("Media", std::format("{}", scene.medium_count));
            draw_statistics_row("Lights", std::format("{}", scene.light_count));
            draw_statistics_row("Area Lights", std::format("{}", scene.area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", scene.infinite_light_count));
            draw_statistics_row("Object Definitions", std::format("{}", scene.object_definition_count));
            draw_statistics_row("Object Instances", std::format("{}", scene.object_instance_count));
            ImGui::EndTable();
        }
    }

    void PathtracerRenderer::Impl::draw_render_tab() {
        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        if (!this->pathtracer_ready()) {
            ImGui::TextDisabled("No active render pipeline");
            return;
        }

        if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Render SPP");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(positive_int_text(this->scene_sampler_sample_count).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("SPP Source");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(this->sample_source_text().c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Default SPP");
            ImGui::TableSetColumnIndex(1);
            int default_sample_count = this->render_config.default_pixel_samples.value_or(interactive_default_pixel_samples);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt("##DefaultSamplerSPP", &default_sample_count, 16, 64)) this->set_default_pixel_samples(default_sample_count);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Override SPP");
            ImGui::TableSetColumnIndex(1);
            bool override_enabled = this->render_config.pixel_samples.has_value();
            const bool override_changed = ImGui::Checkbox("##OverrideSamplerSPPEnabled", &override_enabled);
            if (override_changed && override_enabled) this->set_override_pixel_samples(this->override_pixel_samples_input);
            if (override_changed && !override_enabled) this->set_override_pixel_samples(std::nullopt);
            ImGui::SameLine();
            ImGui::BeginDisabled(!override_enabled);
            int override_sample_count = this->override_pixel_samples_input;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt("##OverrideSamplerSPP", &override_sample_count, 16, 64)) this->set_override_pixel_samples(override_sample_count);
            ImGui::EndDisabled();

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Current Sample");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d / %d", this->render_pipeline->current_sample(), this->render_pipeline->target_sample_count());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Max Iterations");
            ImGui::TableSetColumnIndex(1);
            const int previous_target_sample_count = this->render_pipeline->target_sample_count();
            int target_sample_count                = previous_target_sample_count;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->scene_sampler_sample_count)) {
                this->render_pipeline->set_target_sample_count(target_sample_count);
                if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Accumulation");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button(ICON_MS_RESTART_ALT " Reset")) this->request_pathtracer_accumulation_reset();

            ImGui::EndTable();
        }
    }

    void PathtracerRenderer::Impl::draw_tone_mapping_tab() {
        if (!this->pathtracer_ready()) {
            ImGui::TextDisabled("No active render pipeline");
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_NoBordersInBodyUntilResize;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->render_pipeline->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->render_pipeline->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
    }

    void PathtracerRenderer::Impl::draw_performance_overlay() {
        if (!this->performance_overlay_visible || !this->ui.viewport_known) return;

        const ImVec2 overlay_position{this->ui.viewport_position[0] + 12.0f, this->ui.viewport_position[1] + 12.0f};
        ImGui::SetNextWindowPos(overlay_position, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.48f);
        constexpr ImGuiWindowFlags overlay_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{10.0f, 8.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.050f, 0.058f, 0.068f, 0.88f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.250f, 0.310f, 0.360f, 0.62f});
        const bool began = ImGui::Begin("PathtracerPerformanceOverlay", nullptr, overlay_flags);
        if (!began) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            return;
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingFixedFit;
        const PathtracerStatus status         = this->pathtracer_status();
        const std::string scene_title         = this->scene_info.has_value() ? std::string{this->active_scene_info().title} : "No Scene";
        const std::string viewport_resolution = resolution_text(this->ui.viewport_framebuffer_size);
        const std::string render_resolution   = this->render_resolution_sync.pathtracer_created ? resolution_text(this->render_resolution_sync.active_resolution) : "Pending";

        ImGui::TextDisabled("%s", "Performance");
        if (ImGui::BeginTable("SpectraPathtracerPerformanceOverlayStats", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 76.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 148.0f);
            draw_overlay_statistics_row("Scene", scene_title);
            draw_overlay_statistics_row("State", status.state);
            if (this->render_pipeline != nullptr) {
                draw_overlay_statistics_row("Sample", std::format("{} / {}", this->render_pipeline->current_sample(), this->render_pipeline->target_sample_count()));
                draw_overlay_statistics_row("Progress", std::format("{:.1f}%", this->render_pipeline->completion_ratio() * 100.0f));
            } else {
                draw_overlay_statistics_row("Sample", "No Pipeline");
                draw_overlay_statistics_row("Progress", "Pending");
            }
            draw_overlay_statistics_row("Viewport", viewport_resolution);
            draw_overlay_statistics_row("Render", render_resolution);
            draw_overlay_statistics_row("Frame", std::format("{:.3f} ms", this->statistics.last_frame_milliseconds));
            if (this->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_overlay_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_overlay_statistics_row("FPS Avg", "Collecting");
            }
            if (this->statistics.throughput_mspp.has_value())
                draw_overlay_statistics_row("Throughput", std::format("{:.2f} MSPP/s", this->statistics.throughput_mspp.average()));
            else
                draw_overlay_statistics_row("Throughput", "Collecting");
            ImGui::EndTable();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void PathtracerRenderer::Impl::register_panels(PathtracerHostView& host) {
        host.register_panel(PathtracerPanel{
            .id                  = "pathtracer.viewport",
            .title               = "Viewport",
            .dock_slot           = PathtracerDockSlot::Center,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground,
            .closable            = false,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_sidebar_tab(PathtracerSidebarTab{
            .id             = "pathtracer.render",
            .title          = "Render",
            .icon           = ICON_MS_SETTINGS,
            .shortcut_label = "F3",
            .shortcut_key   = ImGuiKey_F3,
            .draw           = [this] { this->draw_render_tab(); },
        });
        host.register_sidebar_tab(PathtracerSidebarTab{
            .id             = "pathtracer.camera",
            .title          = "Camera",
            .icon           = ICON_MS_PHOTO_CAMERA,
            .shortcut_label = "F4",
            .shortcut_key   = ImGuiKey_F4,
            .draw           = [this] { this->draw_camera_tab(); },
        });
        host.register_sidebar_tab(PathtracerSidebarTab{
            .id             = "pathtracer.scene",
            .title          = "Scene Info",
            .icon           = ICON_MS_ACCOUNT_TREE,
            .shortcut_label = "F5",
            .shortcut_key   = ImGuiKey_F5,
            .draw           = [this] { this->draw_scene_tab(); },
        });
        host.register_sidebar_tab(PathtracerSidebarTab{
            .id             = "pathtracer.tone",
            .title          = "Tone",
            .icon           = ICON_MS_TONALITY,
            .shortcut_label = "F6",
            .shortcut_key   = ImGuiKey_F6,
            .draw           = [this] { this->draw_tone_mapping_tab(); },
        });
        host.register_toolbar_action(PathtracerToolbarAction{
            .id             = "pathtracer.performance_overlay",
            .title          = "Performance Overlay",
            .icon           = ICON_MS_ANALYTICS,
            .shortcut_label = "F9",
            .shortcut_key   = ImGuiKey_F9,
            .enabled        = [] { return true; },
            .active         = [this] { return this->performance_overlay_visible; },
            .trigger        = [this] { this->performance_overlay_visible = !this->performance_overlay_visible; },
        });
    }
} // namespace spectra::pathtracer
