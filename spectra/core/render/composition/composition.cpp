module spectra.render.composition;

import spectra.render.runtime_abi;
import std;
import vulkan;

namespace spectra {
    RenderCompositor::RenderCompositor(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept : context{runtime, std::move(shader_directory)} {}

    RenderCompositor::~RenderCompositor() {
        this->context.runtime.frames.defer_destruction([shaders = std::move(this->shaders), linear = std::move(this->linear_image), display = std::move(this->image)]() mutable {});
    }

    void RenderCompositor::initialize() {
        this->sampler_descriptor                       = this->context.runtime.frames.allocate_sampler_descriptor();
        const std::vector<std::uint32_t> vertex_code   = load_spirv(this->context.shader_directory / "display_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "display_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                vertex_code.size() * sizeof(std::uint32_t),
                vertex_code.data(),
                "display_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "display_fragment",
            },
        };
        this->shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
        this->context.runtime.resources.write_sampler_descriptor(this->sampler_descriptor, vk::SamplerCreateInfo{
                                                                                               {},
                                                                                               vk::Filter::eLinear,
                                                                                               vk::Filter::eLinear,
                                                                                               vk::SamplerMipmapMode::eNearest,
                                                                                               vk::SamplerAddressMode::eClampToEdge,
                                                                                               vk::SamplerAddressMode::eClampToEdge,
                                                                                               vk::SamplerAddressMode::eClampToEdge,
                                                                                           });
    }

    bool RenderCompositor::resize(const vk::Extent2D extent) {
        if (*this->image.image && extent == this->image.extent) return false;
        GpuImage next_linear_image = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferDst);
        GpuImage next_image = this->context.runtime.resources.create_image_2d(extent, vk::Format::eB8G8R8A8Srgb, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
        DescriptorLease next_descriptor         = this->context.runtime.frames.allocate_resource_descriptor();
        DescriptorLease next_storage_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_sampled_image_descriptor(next_descriptor, next_linear_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        this->context.runtime.resources.write_storage_image_descriptor(next_storage_descriptor, next_linear_image, vk::ImageLayout::eGeneral);
        if (*this->image.image) this->context.runtime.frames.defer_destruction([linear = std::move(this->linear_image), display = std::move(this->image)]() mutable {});
        this->linear_image              = std::move(next_linear_image);
        this->image                     = std::move(next_image);
        this->linear_sampled_descriptor = std::move(next_descriptor);
        this->linear_storage_descriptor = std::move(next_storage_descriptor);
        this->linear_layout             = vk::ImageLayout::eUndefined;
        this->layout                    = vk::ImageLayout::eUndefined;
        return true;
    }

    void RenderCompositor::record(const vk::raii::CommandBuffer& command_buffer, const RenderCompositionRequest& request) {
        const bool scene_visualizations   = request.visualization && request.compose_visualizations && request.visualization->has_visible(request.scene, request.visualizations, scene::VisualizationCompositionDomain::SceneLinear);
        const bool display_visualizations = request.visualization && request.compose_visualizations && request.visualization->has_visible(request.scene, request.visualizations, scene::VisualizationCompositionDomain::DisplayReferred);
        const bool camera_reference       = request.visualization && request.camera_reference && (request.camera_reference->overlay || request.camera_reference->plane);
        const bool neural_field           = request.neural_field && request.neural_field->has_visible(request.scene);
        if (!request.depth && (neural_field || scene_visualizations || display_visualizations || request.diagnostics || camera_reference)) throw std::runtime_error("Render composition requires Renderer depth");

        std::optional<RenderOutput> linear_composition{};
        if (neural_field || scene_visualizations) {
            this->prepare_linear_composition(command_buffer, request.renderer_output);
            if (neural_field) request.neural_field->record(command_buffer, this->linear_target(), *request.depth, request.scene, request.camera);
            if (scene_visualizations) request.visualization->record(command_buffer, this->linear_target(), *request.depth, request.scene, request.camera, request.visualizations, scene::VisualizationCompositionDomain::SceneLinear, nullptr);
            linear_composition.emplace(this->linear_output(request.renderer_output));
        }
        this->record_display(command_buffer, linear_composition ? *linear_composition : request.renderer_output, request.exposure);
        if (request.diagnostics) request.diagnostics->renderer.record(command_buffer, request.frame_slot_index, this->target(), *request.depth, request.scene, request.camera, request.scene_camera_view, request.diagnostics->scene_guides, request.diagnostics->entity_diagnostics, request.diagnostics->selection, request.diagnostics->visible);
        if (display_visualizations || camera_reference) request.visualization->record(command_buffer, this->target(), *request.depth, request.scene, request.camera, request.visualizations, scene::VisualizationCompositionDomain::DisplayReferred, request.camera_reference ? &*request.camera_reference : nullptr);
        if (request.overlay) request.overlay->renderer.record(command_buffer, this->target(), request.camera, request.overlay->state);
    }

    void RenderCompositor::prepare_sampling(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->image.image || this->layout == vk::ImageLayout::eShaderReadOnlyOptimal) return;
        if (this->layout == vk::ImageLayout::eUndefined) {
            const vk::ImageMemoryBarrier2 to_clear{
                vk::PipelineStageFlagBits2::eNone,
                {},
                vk::PipelineStageFlagBits2::eClear,
                vk::AccessFlagBits2::eTransferWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eTransferDstOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_clear});
            command_buffer.clearColorImage(*this->image.image, vk::ImageLayout::eTransferDstOptimal, vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}}, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
            this->layout = vk::ImageLayout::eTransferDstOptimal;
        }
        const vk::ImageMemoryBarrier2 to_sampling{
            this->layout == vk::ImageLayout::eTransferDstOptimal ? vk::PipelineStageFlagBits2::eClear : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            this->layout == vk::ImageLayout::eTransferDstOptimal ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderSampledRead,
            this->layout,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            *this->image.image,
            {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &to_sampling});
        this->layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }

    ColorCompositionTarget RenderCompositor::target() noexcept {
        return {this->image, this->layout, scene::SpectrumColorSpace::Srgb};
    }

    void RenderCompositor::prepare_linear_composition(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output) {
        const vk::PipelineStageFlags2 destination_stage = this->linear_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eTopOfPipe : vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 destination_access = this->linear_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead | vk::AccessFlagBits2::eColorAttachmentWrite;
        const std::array barriers{
            vk::ImageMemoryBarrier2{render_output.source_stage, render_output.source_access, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, render_output.image_layout, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *render_output.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{destination_stage, destination_access, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, this->linear_layout, vk::ImageLayout::eTransferDstOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->linear_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(barriers.size()), barriers.data()});
        command_buffer.copyImage(*render_output.image.image, vk::ImageLayout::eTransferSrcOptimal, *this->linear_image.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageCopy{{vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {}, {vk::ImageAspectFlagBits::eColor, 0, 0, 1}, {}, {this->linear_image.extent.width, this->linear_image.extent.height, 1}});
        const vk::ImageMemoryBarrier2 restore{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, render_output.source_stage, render_output.source_access, vk::ImageLayout::eTransferSrcOptimal, render_output.image_layout, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *render_output.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &restore});
        this->linear_layout      = vk::ImageLayout::eTransferDstOptimal;
        this->linear_color_space = render_output.color_space;
    }

    ColorCompositionTarget RenderCompositor::linear_target() noexcept {
        return {this->linear_image, this->linear_layout, this->linear_color_space, this->linear_storage_descriptor};
    }

    RenderOutput RenderCompositor::linear_output(const RenderOutput renderer_output) const noexcept {
        const vk::PipelineStageFlags2 stage = this->linear_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : this->linear_layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 access = this->linear_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : this->linear_layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite : vk::AccessFlagBits2::eColorAttachmentWrite;
        return {this->linear_image, this->linear_sampled_descriptor, this->linear_layout, stage, access, renderer_output.color_space, renderer_output.exposure};
    }

    void RenderCompositor::record_display(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output, const float exposure) {
        const std::array begin_barriers{
            vk::ImageMemoryBarrier2{
                render_output.source_stage,
                render_output.source_access,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                render_output.image_layout,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *render_output.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                this->layout == vk::ImageLayout::eUndefined               ? vk::PipelineStageFlagBits2::eNone
                : this->layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader
                                                                          : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                this->layout == vk::ImageLayout::eUndefined               ? vk::AccessFlags2{}
                : this->layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead
                                                                          : vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->layout,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(begin_barriers.size()), begin_barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            *this->image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{{}, vk::Rect2D{{0, 0}, this->image.extent}, 1, 0, 1, &color_attachment});
        command_buffer.setViewportWithCount(vk::Viewport{0.0f, 0.0f, static_cast<float>(this->image.extent.width), static_cast<float>(this->image.extent.height), 0.0f, 1.0f});
        command_buffer.setScissorWithCount(vk::Rect2D{{0, 0}, this->image.extent});
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::False);
        set_basic_graphics_state(command_buffer);
        command_buffer.setVertexInputEXT({}, {});
        command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
        command_buffer.setPrimitiveRestartEnable(vk::False);
        constexpr vk::Bool32 blend_enable = vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{vk::ShaderEXT{}, *this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        const DisplayPushData push_data{render_output.sampled_descriptor, this->sampler_descriptor, render_output.exposure + exposure, std::to_underlying(render_output.color_space)};
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                render_output.source_stage,
                render_output.source_access,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                render_output.image_layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *render_output.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *this->image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(end_barriers.size()), end_barriers.data()});
        this->layout = vk::ImageLayout::eColorAttachmentOptimal;
    }
} // namespace spectra
