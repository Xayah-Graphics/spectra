module spectra.render.composition.overlay;

import spectra.render.contract;
import std;
import vulkan;

namespace spectra {
    namespace {
        struct alignas(16) MaskPushData {
            DescriptorHandle positions;
            DescriptorHandle indices;
            DescriptorHandle radii;
            DescriptorHandle transforms;
            std::uint32_t scene_primitive_index;
            std::uint32_t element_count;
            std::uint32_t draw_kind;
            std::uint32_t reserved_0;
            std::array<float, 4> color;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
        };
        static_assert(sizeof(MaskPushData) == 128);

        struct alignas(16) AxesPushData {
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
            std::array<float, 4> inverse_view_projection_row_0;
            std::array<float, 4> inverse_view_projection_row_1;
            std::array<float, 4> inverse_view_projection_row_2;
            std::array<float, 4> inverse_view_projection_row_3;
            std::array<std::uint32_t, 4> metadata;
        };
        static_assert(sizeof(AxesPushData) == 144);


    } // namespace

    ViewportOverlay::ViewportOverlay(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {}

    ViewportOverlay::~ViewportOverlay() {
        this->destroy();
    }

    void ViewportOverlay::initialize() {
        this->initialize_overlay();
    }

    void ViewportOverlay::destroy() noexcept {
        this->context.runtime.frames.defer_destruction([
            mask_shaders = std::move(this->overlay.mask_shaders),
            axes_shaders = std::move(this->overlay.axes_shaders),
            outline_shaders = std::move(this->overlay.outline_shaders),
            mask = std::move(this->overlay.mask),
            depth = std::move(this->overlay.depth)
        ]() mutable {});
        this->overlay.mask_descriptor = {};
        this->overlay.sampler_descriptor = {};
        this->overlay.mask_layout  = vk::ImageLayout::eUndefined;
        this->overlay.depth_layout = vk::ImageLayout::eUndefined;
    }

    void ViewportOverlay::record(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, const scene::Camera& camera, const ViewportOverlayState& state) {
        std::vector<std::uint32_t> selected_indices{};
        std::vector<std::uint32_t> active_indices{};
        std::vector<std::uint32_t> hovered_indices{};
        const auto collect = [this](const scene::InstanceId instance, std::vector<std::uint32_t>& destination) {
            for (std::uint32_t gpu_instance = 0; gpu_instance < this->context.gpu_scene.view().primitive_instance_ids.size(); ++gpu_instance) {
                if (this->context.gpu_scene.view().primitive_instance_ids[gpu_instance] == instance) destination.push_back(gpu_instance);
            }
        };
        for (const scene::InstanceId instance : state.selected_instances) collect(instance, selected_indices);
        if (state.active_instance) collect(*state.active_instance, active_indices);
        if (state.hovered_instance) collect(*state.hovered_instance, hovered_indices);
        const vk::Extent2D extent = display.image.extent;
        this->record_impl(command_buffer, *display.image.image, *display.image.view, display.layout, extent, vk::Rect2D{{0, 0}, extent}, camera, selected_indices, active_indices, hovered_indices, state.axes_plane, state.axes_visible, state.outline_visible);
        display.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }


    void ViewportOverlay::configure_mask_render_state(const vk::raii::CommandBuffer& command_buffer, const vk::Rect2D render_region, const vk::CompareOp depth_compare, const bool depth_write) {
        command_buffer.setViewportWithCount(vk::Viewport{
            static_cast<float>(render_region.offset.x),
            static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height),
            static_cast<float>(render_region.extent.width),
            -static_cast<float>(render_region.extent.height),
            0.0f,
            1.0f,
        });
        command_buffer.setScissorWithCount(render_region);
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::True);
        command_buffer.setDepthWriteEnable(depth_write);
        command_buffer.setDepthCompareOp(depth_compare);
        set_basic_graphics_state(command_buffer);
        constexpr vk::Bool32 blend_enable = vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);
    }

    void ViewportOverlay::initialize_overlay() {
        this->overlay.mask_descriptor                       = this->context.runtime.frames.allocate_resource_descriptor();
        this->overlay.sampler_descriptor                    = this->context.runtime.frames.allocate_sampler_descriptor();
        const std::vector<std::uint32_t> mask_mesh_code     = load_spirv(this->context.shader_directory / "overlay_mesh.spv");
        const std::vector<std::uint32_t> mask_fragment_code = load_spirv(this->context.shader_directory / "overlay_mask.spv");
        const std::array mask_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap | vk::ShaderCreateFlagBitsEXT::eNoTaskShader,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                mask_mesh_code.size() * sizeof(std::uint32_t),
                mask_mesh_code.data(),
                "overlay_mesh",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                mask_fragment_code.size() * sizeof(std::uint32_t),
                mask_fragment_code.data(),
                "overlay_mask",
            },
        };
        this->overlay.mask_shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, mask_create_infos};

        const std::vector<std::uint32_t> axes_vertex_code   = load_spirv(this->context.shader_directory / "overlay_axes_vertex.spv");
        const std::vector<std::uint32_t> axes_fragment_code = load_spirv(this->context.shader_directory / "overlay_axes_fragment.spv");
        const std::array axes_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                axes_vertex_code.size() * sizeof(std::uint32_t),
                axes_vertex_code.data(),
                "overlay_axes_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                axes_fragment_code.size() * sizeof(std::uint32_t),
                axes_fragment_code.data(),
                "overlay_axes_fragment",
            },
        };
        this->overlay.axes_shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, axes_create_infos};

        const std::vector<std::uint32_t> outline_vertex_code   = load_spirv(this->context.shader_directory / "overlay_vertex.spv");
        const std::vector<std::uint32_t> outline_fragment_code = load_spirv(this->context.shader_directory / "overlay_outline.spv");
        const std::array outline_create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                outline_vertex_code.size() * sizeof(std::uint32_t),
                outline_vertex_code.data(),
                "overlay_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                outline_fragment_code.size() * sizeof(std::uint32_t),
                outline_fragment_code.data(),
                "overlay_outline",
            },
        };
        this->overlay.outline_shaders                        = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, outline_create_infos};
        this->context.runtime.resources.write_sampler_descriptor(this->overlay.sampler_descriptor, vk::SamplerCreateInfo{
                                                                                                       {},
                                                                                                       vk::Filter::eNearest,
                                                                                                       vk::Filter::eNearest,
                                                                                                       vk::SamplerMipmapMode::eNearest,
                                                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                                                       vk::SamplerAddressMode::eClampToEdge,
                                                                                                   });
    }

    void ViewportOverlay::create_overlay_images(const vk::Extent2D extent) {
        GpuImage next_mask = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
        GpuImage next_depth = this->context.runtime.resources.create_image_2d(extent, vk::Format::eD32Sfloat, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
        if (*this->overlay.mask.image) {
            DescriptorLease next_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_sampled_image_descriptor(next_descriptor, next_mask, vk::ImageLayout::eShaderReadOnlyOptimal);
            this->overlay.mask_descriptor = std::move(next_descriptor);
            this->context.runtime.frames.defer_destruction([mask = std::move(this->overlay.mask), depth = std::move(this->overlay.depth)]() mutable {});
        } else
            this->context.runtime.resources.write_sampled_image_descriptor(this->overlay.mask_descriptor, next_mask, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->overlay.mask            = std::move(next_mask);
        this->overlay.depth           = std::move(next_depth);
        this->overlay.mask_layout  = vk::ImageLayout::eUndefined;
        this->overlay.depth_layout = vk::ImageLayout::eUndefined;
    }

    void ViewportOverlay::record_impl(const vk::raii::CommandBuffer& command_buffer, const vk::Image target_image, const vk::ImageView target_view, const vk::ImageLayout target_layout, const vk::Extent2D extent, const vk::Rect2D render_region, const scene::Camera& camera, const std::span<const std::uint32_t> selected_instances, const std::span<const std::uint32_t> active_instances, const std::span<const std::uint32_t> hovered_instances, const std::uint32_t axes_plane, const bool axes_visible, const bool outline_visible) {
        const bool outline_required = outline_visible && (!selected_instances.empty() || !active_instances.empty() || !hovered_instances.empty());
        if (!axes_visible && !outline_required) {
            if (target_layout == vk::ImageLayout::eShaderReadOnlyOptimal) return;
            const vk::ImageMemoryBarrier2 to_sample{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                target_layout,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_sample});
            return;
        }
        if (!*this->overlay.mask.image || this->overlay.mask.extent != extent) this->create_overlay_images(extent);

        const std::array mask_barriers{
            vk::ImageMemoryBarrier2{
                this->overlay.mask_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                this->overlay.mask_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->overlay.mask_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->overlay.mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->overlay.depth_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlags2{} : vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->overlay.depth_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->overlay.depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->overlay.depth.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(mask_barriers.size()), mask_barriers.data()});
        const vk::RenderingAttachmentInfo color_attachment{
            *this->overlay.mask.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->overlay.depth.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearDepthStencilValue{1.0f, 0}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &color_attachment,
            &depth_attachment,
        });
        const std::array mask_stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array mask_handles{*this->overlay.mask_shaders[0], vk::ShaderEXT{}, *this->overlay.mask_shaders[1]};
        command_buffer.bindShadersEXT(mask_stages, mask_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        const scene::CameraMatrices matrices = camera.matrices();
        const auto draw_primitive            = [&](const std::uint32_t scene_primitive_index, const std::array<float, 4> color) {
            const GpuScenePrimitive& gpu_primitive = this->context.gpu_scene.view().primitives[scene_primitive_index];
            const GpuGeometry* mesh       = gpu_primitive.kind == GpuScenePrimitiveKind::Geometry ? &this->context.gpu_scene.view().geometries[gpu_primitive.resource_index] : nullptr;
            const GpuSphereSet* spheres   = gpu_primitive.kind == GpuScenePrimitiveKind::SphereSet ? &this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index] : nullptr;
            const MaskPushData push_data{
                mesh ? mesh->positions_descriptor : spheres->positions_descriptor,
                mesh ? mesh->indices_descriptor : spheres->positions_descriptor,
                spheres ? spheres->radii_descriptor : mesh->positions_descriptor,
                this->context.gpu_scene.view().primitive_transforms,
                scene_primitive_index,
                mesh ? mesh->index_count / 3u : spheres->sphere_count,
                static_cast<std::uint32_t>(gpu_primitive.kind),
                0,
                color,
                {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]},
                {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]},
                {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]},
                {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        };
        this->configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eLess, true);
        for (std::uint32_t scene_primitive_index = 0; scene_primitive_index < this->context.gpu_scene.view().primitives.size(); ++scene_primitive_index) draw_primitive(scene_primitive_index, {});
        if (outline_required) {
            this->configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eEqual, false);
            for (const std::uint32_t scene_primitive_index : selected_instances) draw_primitive(scene_primitive_index, {0.10f, 0.58f, 1.0f, 1.0f});
            for (const std::uint32_t scene_primitive_index : active_instances) draw_primitive(scene_primitive_index, {1.0f, 0.55f, 0.08f, 1.0f});
            for (const std::uint32_t scene_primitive_index : hovered_instances) draw_primitive(scene_primitive_index, {0.45f, 1.0f, 0.28f, 1.0f});
        }
        command_buffer.endRendering();
        this->overlay.mask_layout  = vk::ImageLayout::eColorAttachmentOptimal;
        this->overlay.depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;

        const std::array outline_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->overlay.mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                target_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                target_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                target_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(outline_barriers.size()), outline_barriers.data()});
        this->overlay.mask_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        const vk::RenderingAttachmentInfo target_attachment{
            target_view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
        };
        const vk::RenderingAttachmentInfo overlay_depth_attachment{
            *this->overlay.depth.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eDontCare,
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &target_attachment,
            &overlay_depth_attachment,
        });
        command_buffer.setScissorWithCount(render_region);
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        set_basic_graphics_state(command_buffer);
        command_buffer.setVertexInputEXT({}, {});
        command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
        command_buffer.setPrimitiveRestartEnable(vk::False);
        constexpr vk::Bool32 blend_enable = vk::True;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        command_buffer.setColorBlendEquationEXT(0, vk::ColorBlendEquationEXT{
                                                       vk::BlendFactor::eSrcAlpha,
                                                       vk::BlendFactor::eOneMinusSrcAlpha,
                                                       vk::BlendOp::eAdd,
                                                       vk::BlendFactor::eOne,
                                                       vk::BlendFactor::eOneMinusSrcAlpha,
                                                       vk::BlendOp::eAdd,
                                                   });
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);
        if (axes_visible) {
            command_buffer.setViewportWithCount(vk::Viewport{
                static_cast<float>(render_region.offset.x),
                static_cast<float>(render_region.offset.y) + static_cast<float>(render_region.extent.height),
                static_cast<float>(render_region.extent.width),
                -static_cast<float>(render_region.extent.height),
                0.0f,
                1.0f,
            });
            command_buffer.setDepthTestEnable(vk::True);
            command_buffer.setDepthWriteEnable(vk::False);
            command_buffer.setDepthCompareOp(vk::CompareOp::eLess);
            const std::array axes_stages{
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array axes_handles{
                vk::ShaderEXT{},
                *this->overlay.axes_shaders[0],
                *this->overlay.axes_shaders[1],
            };
            command_buffer.bindShadersEXT(axes_stages, axes_handles);
            const AxesPushData axes_push{
                {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]},
                {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]},
                {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]},
                {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]},
                {matrices.inverse_view_projection[0], matrices.inverse_view_projection[1], matrices.inverse_view_projection[2], matrices.inverse_view_projection[3]},
                {matrices.inverse_view_projection[4], matrices.inverse_view_projection[5], matrices.inverse_view_projection[6], matrices.inverse_view_projection[7]},
                {matrices.inverse_view_projection[8], matrices.inverse_view_projection[9], matrices.inverse_view_projection[10], matrices.inverse_view_projection[11]},
                {matrices.inverse_view_projection[12], matrices.inverse_view_projection[13], matrices.inverse_view_projection[14], matrices.inverse_view_projection[15]},
                {axes_plane, render_region.extent.width, render_region.extent.height, 0},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&axes_push, 1}));
            command_buffer.draw(3, 1, 0, 0);
        }
        if (outline_required) {
            command_buffer.setViewportWithCount(vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
            command_buffer.setDepthTestEnable(vk::False);
            command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
            command_buffer.setPrimitiveRestartEnable(vk::False);
            const std::array outline_stages{
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array outline_handles{
                vk::ShaderEXT{},
                *this->overlay.outline_shaders[0],
                *this->overlay.outline_shaders[1],
            };
            command_buffer.bindShadersEXT(outline_stages, outline_handles);
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            struct alignas(16) OutlinePushData {
                DescriptorHandle mask;
                DescriptorHandle sampler;
                std::array<float, 2> inverse_extent;
                std::array<std::uint32_t, 2> reserved;
            };
            const OutlinePushData outline_push{
                this->overlay.mask_descriptor,
                this->overlay.sampler_descriptor,
                {1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height)},
                {},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&outline_push, 1}));
            command_buffer.draw(3, 1, 0, 0);
        }
        command_buffer.endRendering();
        const vk::ImageMemoryBarrier2 target_to_sample{
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderSampledRead,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            target_image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_sample});
    }
} // namespace spectra
