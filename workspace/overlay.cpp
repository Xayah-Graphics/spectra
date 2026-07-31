module spectra.workspace.overlay;

import std;

namespace spectra::workspace {
    namespace {
        struct alignas(16) MaskPushData {
            DescriptorHandle positions;
            DescriptorHandle indices;
            DescriptorHandle radii;
            DescriptorHandle primitives;
            DescriptorHandle transforms;
            std::uint32_t instance_index;
            std::uint32_t element_count;
            std::uint32_t draw_kind;
            std::uint32_t reserved_0;
            std::array<std::uint32_t, 2> reserved_1;
            std::array<float, 4> color;
            std::array<float, 4> view_projection_row_0;
            std::array<float, 4> view_projection_row_1;
            std::array<float, 4> view_projection_row_2;
            std::array<float, 4> view_projection_row_3;
        };
        static_assert(sizeof(MaskPushData) == 144);

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

        void configure_mask_render_state(
            const vk::raii::CommandBuffer& command_buffer,
            const vk::Rect2D render_region,
            const vk::CompareOp depth_compare,
            const bool depth_write) {
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
            command_buffer.setRasterizerDiscardEnable(vk::False);
            command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
            command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
            command_buffer.setAlphaToCoverageEnableEXT(vk::False);
            command_buffer.setDepthBiasEnable(vk::False);
            command_buffer.setStencilTestEnable(vk::False);
            constexpr vk::SampleMask sample_mask = 1;
            command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
            constexpr vk::Bool32 blend_enable = vk::False;
            command_buffer.setColorBlendEnableEXT(0, blend_enable);
            constexpr vk::ColorComponentFlags color_components =
                vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            command_buffer.setColorWriteMaskEXT(0, color_components);
        }
    }

    OverlayRenderer::OverlayRenderer(
        GpuDevice& gpu,
        const rasterizer::RasterScene& scene,
        const std::filesystem::path& shader_directory)
        : gpu(&gpu),
          scene(&scene),
          mask_descriptor(gpu.allocate_resource_descriptor()),
          sampler_descriptor(gpu.allocate_sampler_descriptor()) {
        const std::vector<std::uint32_t> mask_mesh_code = load_spirv(shader_directory / "overlay_mesh.spv");
        const std::vector<std::uint32_t> mask_fragment_code = load_spirv(shader_directory / "overlay_mask.spv");
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
        this->mask_shaders = vk::raii::ShaderEXTs{gpu.device, mask_create_infos};

        const std::vector<std::uint32_t> axes_vertex_code = load_spirv(shader_directory / "overlay_axes_vertex.spv");
        const std::vector<std::uint32_t> axes_fragment_code = load_spirv(shader_directory / "overlay_axes_fragment.spv");
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
        this->axes_shaders = vk::raii::ShaderEXTs{gpu.device, axes_create_infos};

        const std::vector<std::uint32_t> outline_vertex_code = load_spirv(shader_directory / "overlay_vertex.spv");
        const std::vector<std::uint32_t> outline_fragment_code = load_spirv(shader_directory / "overlay_outline.spv");
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
        this->outline_shaders = vk::raii::ShaderEXTs{gpu.device, outline_create_infos};
        gpu.write_sampler(this->sampler_descriptor, vk::SamplerCreateInfo{
            {},
            vk::Filter::eNearest,
            vk::Filter::eNearest,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
        });
    }

    OverlayRenderer::~OverlayRenderer() {
        this->gpu->release_resource_descriptor(this->mask_descriptor);
        this->gpu->release_sampler_descriptor(this->sampler_descriptor);
    }

    void OverlayRenderer::create_images(const vk::Extent2D extent) {
        this->mask = this->gpu->create_image_2d(
            extent,
            vk::Format::eR8G8B8A8Unorm,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled);
        this->depth = this->gpu->create_image_2d(
            extent,
            vk::Format::eD32Sfloat,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::ImageAspectFlagBits::eDepth);
        this->gpu->write_sampled_image(this->mask_descriptor, this->mask, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->mask_layout = vk::ImageLayout::eUndefined;
        this->depth_layout = vk::ImageLayout::eUndefined;
    }

    void OverlayRenderer::record(
        const vk::raii::CommandBuffer& command_buffer,
        const vk::Image target_image,
        const vk::ImageView target_view,
        const vk::Extent2D extent,
        const vk::Rect2D render_region,
        const std::span<const std::uint32_t> selected_instances,
        const std::span<const std::uint32_t> active_instances,
        const std::span<const std::uint32_t> hovered_instances,
        const std::uint32_t axes_plane,
        const bool axes_visible,
        const bool outline_visible) {
        const bool outline_required =
            outline_visible &&
            (!selected_instances.empty() ||
             !active_instances.empty() ||
             !hovered_instances.empty());
        if (!axes_visible && !outline_required) {
            const vk::ImageMemoryBarrier2 to_sample{
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
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_sample});
            return;
        }
        if (!*this->mask.image || this->mask.extent != extent) this->create_images(extent);

        const std::array mask_barriers{
            vk::ImageMemoryBarrier2{
                this->mask_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                this->mask_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->mask_layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlags2{} : vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                this->depth_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                this->depth_layout,
                vk::ImageLayout::eDepthAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->depth.image,
                {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(mask_barriers.size()), mask_barriers.data()});
        const vk::RenderingAttachmentInfo color_attachment{
            *this->mask.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}},
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->depth.view,
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
        const std::array mask_stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eFragment};
        const std::array mask_handles{*this->mask_shaders[0], *this->mask_shaders[1]};
        command_buffer.bindShadersEXT(mask_stages, mask_handles);
        this->gpu->bind_descriptor_heaps(command_buffer);
        const scene::CameraMatrices matrices = this->scene->camera.matrices();
        const auto draw_instance = [&](const std::uint32_t instance_index, const std::array<float, 4> color) {
            const render::GpuDraw& draw = this->scene->assets.draws[instance_index];
            const render::GpuGeometry* mesh =
                draw.kind ==
                        render::GpuDrawKind::Geometry
                    ? &this->scene
                           ->assets.geometries[
                               draw.resource_index]
                    : nullptr;
            const render::GpuParticleSet* particles =
                draw.kind ==
                        render::GpuDrawKind::ParticleSet
                    ? &this->scene
                           ->assets.particle_sets[
                               draw.resource_index]
                    : nullptr;
            const MaskPushData push_data{
                mesh
                    ? mesh->positions_descriptor
                    : particles
                          ->positions_descriptor,
                mesh
                    ? mesh->indices_descriptor
                    : particles
                          ->positions_descriptor,
                particles
                    ? particles
                          ->radii_descriptor
                    : mesh
                          ->positions_descriptor,
                this->scene->primitives_descriptor,
                this->scene->transforms_descriptor,
                instance_index,
                mesh
                    ? mesh->index_count / 3u
                    : particles
                          ->particle_count,
                static_cast<std::uint32_t>(
                    draw.kind),
                0,
                {},
                color,
                {matrices.view_projection[0], matrices.view_projection[1], matrices.view_projection[2], matrices.view_projection[3]},
                {matrices.view_projection[4], matrices.view_projection[5], matrices.view_projection[6], matrices.view_projection[7]},
                {matrices.view_projection[8], matrices.view_projection[9], matrices.view_projection[10], matrices.view_projection[11]},
                {matrices.view_projection[12], matrices.view_projection[13], matrices.view_projection[14], matrices.view_projection[15]},
            };
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
            command_buffer.drawMeshTasksEXT((push_data.element_count + 31u) / 32u, 1, 1);
        };
        configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eLess, true);
        for (std::uint32_t index = 0; index < this->scene->assets.draws.size(); ++index) draw_instance(index, {});
        if (outline_required) {
            configure_mask_render_state(command_buffer, render_region, vk::CompareOp::eEqual, false);
            for (const std::uint32_t index : selected_instances) draw_instance(index, {0.10f, 0.58f, 1.0f, 1.0f});
            for (const std::uint32_t index : active_instances)
                draw_instance(index, {1.0f, 0.55f, 0.08f, 1.0f});
            for (const std::uint32_t index : hovered_instances)
                draw_instance(index, {0.45f, 1.0f, 0.28f, 1.0f});
        }
        command_buffer.endRendering();
        this->mask_layout = vk::ImageLayout::eColorAttachmentOptimal;
        this->depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;

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
                *this->mask.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(outline_barriers.size()), outline_barriers.data()});
        this->mask_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
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
            *this->depth.view,
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
        command_buffer.setRasterizerDiscardEnable(vk::False);
        command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
        command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
        command_buffer.setAlphaToCoverageEnableEXT(vk::False);
        command_buffer.setDepthBiasEnable(vk::False);
        command_buffer.setStencilTestEnable(vk::False);
        command_buffer.setVertexInputEXT({}, {});
        command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
        command_buffer.setPrimitiveRestartEnable(vk::False);
        constexpr vk::SampleMask sample_mask = 1;
        command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
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
        constexpr vk::ColorComponentFlags color_components =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
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
                vk::ShaderStageFlagBits::eTaskEXT,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array axes_handles{
                vk::ShaderEXT{},
                vk::ShaderEXT{},
                *this->axes_shaders[0],
                *this->axes_shaders[1],
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
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&axes_push, 1}));
            command_buffer.draw(3, 1, 0, 0);
        }
        if (outline_required) {
            command_buffer.setViewportWithCount(vk::Viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f});
            command_buffer.setDepthTestEnable(vk::False);
            command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
            command_buffer.setPrimitiveRestartEnable(vk::False);
            const std::array outline_stages{
                vk::ShaderStageFlagBits::eTaskEXT,
                vk::ShaderStageFlagBits::eMeshEXT,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
            };
            const std::array outline_handles{
                vk::ShaderEXT{},
                vk::ShaderEXT{},
                *this->outline_shaders[0],
                *this->outline_shaders[1],
            };
            command_buffer.bindShadersEXT(outline_stages, outline_handles);
            this->gpu->bind_descriptor_heaps(command_buffer);
            struct alignas(16) OutlinePushData {
                DescriptorHandle mask;
                DescriptorHandle sampler;
                std::array<float, 2> inverse_extent;
                std::array<std::uint32_t, 2> reserved;
            };
            const OutlinePushData outline_push{
                this->mask_descriptor,
                this->sampler_descriptor,
                {1.0f / static_cast<float>(extent.width), 1.0f / static_cast<float>(extent.height)},
                {},
            };
            this->gpu->push_data(command_buffer, std::as_bytes(std::span{&outline_push, 1}));
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
}
