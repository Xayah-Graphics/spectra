module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <driver_types.h>
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <cstring>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/transform.h>
#include <pbrt/util/vecmath.h>
#include <vulkan/vulkan_raii.hpp>
#include "spectra_raster_spirv.h"
#include "spectra_pbrt_fwd.h"
module spectra;
import std;
#include "spectra_internal.h"

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
