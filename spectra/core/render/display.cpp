module spectra.render.display;

import spectra.runtime.shaders;
import std;
import vulkan;

namespace spectra {
    DisplayPass::DisplayPass(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept : context{runtime, std::move(shader_directory)} {}

    DisplayPass::~DisplayPass() {
        this->context.runtime.frames.defer_destruction([shaders = std::move(this->shaders), linear = std::move(this->linear_image), display = std::move(this->image)]() mutable {});
    }

    void DisplayPass::initialize() {
        this->sampler_descriptor                       = this->context.runtime.resources.allocate_sampler_descriptor();
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

    bool DisplayPass::resize(const vk::Extent2D extent) {
        if (*this->image.image && extent == this->image.extent) return false;
        GpuImage next_linear_image = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32G32B32A32Sfloat, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
        GpuImage next_image = this->context.runtime.resources.create_image_2d(extent, vk::Format::eB8G8R8A8Srgb, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc);
        DescriptorLease next_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        this->context.runtime.resources.write_sampled_image_descriptor(next_descriptor, next_linear_image, vk::ImageLayout::eShaderReadOnlyOptimal);
        if (*this->image.image) {
            this->context.runtime.frames.defer_destruction([linear = std::move(this->linear_image), display = std::move(this->image)]() mutable {});
        }
        this->linear_image              = std::move(next_linear_image);
        this->image                     = std::move(next_image);
        this->linear_sampled_descriptor = std::move(next_descriptor);
        this->linear_layout = vk::ImageLayout::eUndefined;
        this->layout = vk::ImageLayout::eUndefined;
        return true;
    }

    void DisplayPass::prepare_linear_composition(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output) {
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

    ColorCompositionTarget DisplayPass::linear_target() noexcept {
        return {this->linear_image, this->linear_layout, this->linear_color_space};
    }

    RenderOutput DisplayPass::linear_output(const RenderOutput renderer_output) const noexcept {
        const vk::PipelineStageFlags2 stage = this->linear_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 access = this->linear_layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite;
        return {this->linear_image, this->linear_sampled_descriptor, this->linear_layout, stage, access, renderer_output.color_space, renderer_output.exposure};
    }

    ColorCompositionTarget DisplayPass::target() noexcept {
        return {this->image, this->layout, scene::SpectrumColorSpace::Srgb};
    }

    void DisplayPass::record(const vk::raii::CommandBuffer& command_buffer, const RenderOutput render_output, const float exposure) {
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
        constexpr vk::Bool32 blend_enable = vk::False;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array shader_handles{vk::ShaderEXT{}, vk::ShaderEXT{}, *this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        struct alignas(16) DisplayPushData {
            DescriptorHandle source;
            DescriptorHandle sampler;
            float exposure;
            std::uint32_t color_space;
        };
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
