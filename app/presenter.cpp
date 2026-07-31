module spectra.app.presenter;

import std;

namespace spectra::app {
    Presenter::Presenter(GpuDevice& gpu, const std::filesystem::path& shader_directory)
        : gpu(&gpu), sampler_descriptor(gpu.allocate_sampler_descriptor()) {
        const std::vector<std::uint32_t> vertex_code = load_spirv(shader_directory / "presenter_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(shader_directory / "presenter_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eVertex,
                vk::ShaderStageFlagBits::eFragment,
                vk::ShaderCodeTypeEXT::eSpirv,
                vertex_code.size() * sizeof(std::uint32_t),
                vertex_code.data(),
                "presenter_vertex",
            },
            vk::ShaderCreateInfoEXT{
                vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap,
                vk::ShaderStageFlagBits::eFragment,
                {},
                vk::ShaderCodeTypeEXT::eSpirv,
                fragment_code.size() * sizeof(std::uint32_t),
                fragment_code.data(),
                "presenter_fragment",
            },
        };
        this->shaders = vk::raii::ShaderEXTs{gpu.device, create_infos};
        gpu.write_sampler(this->sampler_descriptor, vk::SamplerCreateInfo{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
        });
    }

    Presenter::~Presenter() {
        this->gpu->release_sampler_descriptor(this->sampler_descriptor);
    }

    void Presenter::record(
        const vk::raii::CommandBuffer& command_buffer,
        const render::RenderOutput source,
        const vk::Image target_image,
        const vk::ImageView target_view,
        const vk::Extent2D extent,
        const vk::ImageLayout target_layout,
        const vk::ImageLayout final_layout,
        const float exposure) {
        const std::array begin_barriers{
            vk::ImageMemoryBarrier2{
                source.stage,
                source.access,
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                source.layout,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *source.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                target_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eFragmentShader,
                target_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eShaderSampledRead,
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
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(begin_barriers.size()), begin_barriers.data()});

        const vk::RenderingAttachmentInfo color_attachment{
            target_view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 1.0f}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{
            {},
            vk::Rect2D{{0, 0}, extent},
            1,
            0,
            1,
            &color_attachment,
        });

        command_buffer.setViewportWithCount(vk::Viewport{
            0.0f,
            0.0f,
            static_cast<float>(extent.width),
            static_cast<float>(extent.height),
            0.0f,
            1.0f,
        });
        command_buffer.setScissorWithCount(vk::Rect2D{{0, 0}, extent});
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
        constexpr vk::ColorComponentFlags color_components =
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);

        const std::array stages{
            vk::ShaderStageFlagBits::eTaskEXT,
            vk::ShaderStageFlagBits::eMeshEXT,
            vk::ShaderStageFlagBits::eVertex,
            vk::ShaderStageFlagBits::eFragment,
        };
        const std::array shader_handles{
            vk::ShaderEXT{},
            vk::ShaderEXT{},
            *this->shaders[0],
            *this->shaders[1],
        };
        command_buffer.bindShadersEXT(stages, shader_handles);
        this->gpu->bind_descriptor_heaps(command_buffer);
        struct alignas(16) PresenterPushData {
            DescriptorHandle source;
            DescriptorHandle sampler;
            float exposure;
            std::uint32_t reserved;
        };
        const PresenterPushData push_data{
            source.sampled_descriptor,
            this->sampler_descriptor,
            exposure,
            0};
        this->gpu->push_data(command_buffer, std::as_bytes(std::span{&push_data, 1}));
        command_buffer.draw(3, 1, 0, 0);
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eFragmentShader,
                vk::AccessFlagBits2::eShaderSampledRead,
                source.stage,
                source.access,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                source.layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                *source.image.image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
            vk::ImageMemoryBarrier2{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                final_layout == vk::ImageLayout::ePresentSrcKHR
                    ? vk::PipelineStageFlagBits2::eBottomOfPipe
                    : final_layout == vk::ImageLayout::eTransferSrcOptimal
                        ? vk::PipelineStageFlagBits2::eCopy
                        : vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                final_layout == vk::ImageLayout::eTransferSrcOptimal
                    ? vk::AccessFlagBits2::eTransferRead
                    : final_layout == vk::ImageLayout::eColorAttachmentOptimal
                        ? vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eColorAttachmentWrite
                        : vk::AccessFlags2{},
                vk::ImageLayout::eColorAttachmentOptimal,
                final_layout,
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                target_image,
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            },
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(end_barriers.size()), end_barriers.data()});
    }
} // namespace spectra::app
