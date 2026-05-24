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
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <pbrt/base/film.h>
#include <pbrt/base/sampler.h>
#include <pbrt/gpu/memory.h>
#include <pbrt/gpu/util.h>
#include <pbrt/options.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>
#include <pbrt/wavefront/integrator.h>
#include <vulkan/vulkan_raii.hpp>
module spectra;
import std;
import :runtime;

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

    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform) {
        std::array<float, 16> values{};
        const pbrt::SquareMatrix<4>& matrix = transform.GetMatrix();
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) values[row * 4 + column] = static_cast<float>(matrix[row][column]);
        }
        return values;
    }

    [[nodiscard]] pbrt::Transform transform_from_matrix_array(const std::array<float, 16>& values) {
        pbrt::Float matrix[4][4]{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                const float value = values[row * 4 + column];
                if (!std::isfinite(value)) throw std::runtime_error("Matrix contains a non-finite value");
                matrix[row][column] = static_cast<pbrt::Float>(value);
            }
        }
        return pbrt::Transform{matrix};
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

    struct SpectraPbrtSessionDriver {
        static void render_one_sample(SpectraPbrtInteractiveSession& session, const pbrt::Transform& camera_motion) {
            if (session.sample_index >= session.target_samples) throw std::runtime_error("PBRT sample index is already at the target sample count");
            session.pbrt_state->integrator->RenderSample(session.pbrt_state->pixel_bounds, camera_motion, session.sample_index);
            ++session.sample_index;
        }

        static void rerender_after_reset(SpectraPbrtInteractiveSession& session, const std::uint32_t frame_index, const pbrt::Transform& camera_motion) {
            if (session.physical_device == nullptr || session.device == nullptr) throw std::runtime_error("PBRT interactive Vulkan handles are not available for reset");
            session.device->waitIdle();
            session.destroy_frame_resources_noexcept();
            session.pbrt_state->integrator->ResetFilm(session.pbrt_state->pixel_bounds);
            pbrt::GPUWait();
            session.sample_index     = 0;
            session.reset_requested  = false;
            render_one_sample(session, camera_motion);
            pbrt::GPUWait();
            session.create_frame_resources(*session.physical_device, *session.device, session.frame_count);
            session.create_imgui_descriptors();
            session.active_frame_index = frame_index;
        }
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
                SpectraPbrtSessionDriver::render_one_sample(*this, pbrt::Transform{});
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
                SpectraPbrtSessionDriver::rerender_after_reset(*this, frame_index, camera_motion);
                result.rendered_sample    = true;
                result.sample_pixels      = this->film_pixel_count() * static_cast<std::uint64_t>(this->sample_index);
                result.reset_accumulation = true;
            } else if (this->sample_index < this->target_samples) {
                SpectraPbrtSessionDriver::render_one_sample(*this, camera_motion);
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
