module spectra.editor;

import spectra.runtime.shaders;

import :visualization.renderer;

import std;
import vulkan;

namespace spectra {
    namespace {
        struct alignas(16) VisualizationPushData {
            DescriptorHandle primary{};
            DescriptorHandle secondary{};
            DescriptorHandle depth{};
            std::uint32_t color_space{};
            std::uint32_t reserved{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<std::uint32_t, 4> dimensions{};
            std::array<std::uint32_t, 4> detail{};
            std::array<float, 4> parameters{};
            std::array<float, 4> color{};
            std::array<float, 4> screen_rect{};
            std::array<float, 4> transform_row_0{};
            std::array<float, 4> transform_row_1{};
            std::array<float, 4> transform_row_2{};
            std::array<float, 4> transform_row_3{};
            std::array<float, 4> view_projection_row_0{};
            std::array<float, 4> view_projection_row_1{};
            std::array<float, 4> view_projection_row_2{};
            std::array<float, 4> view_projection_row_3{};
        };

        static_assert(sizeof(VisualizationPushData) == 256);

        [[nodiscard]] const dynamics::GpuDatasetBufferView* visualization_buffer(const dynamics::GpuVisualizationDatasetView& view, const dynamics::DatasetBufferKind kind, const std::uint32_t channel_index = 0) noexcept {
            const auto found = std::ranges::find_if(view.buffers, [kind, channel_index](const dynamics::GpuDatasetBufferView& buffer) { return buffer.kind == kind && buffer.channel_index == channel_index; });
            return found == view.buffers.end() ? nullptr : std::to_address(found);
        }

    } // namespace

    VisualizationRenderer::VisualizationRenderer(VulkanRuntime& runtime, std::filesystem::path shader_directory) : context{runtime, std::move(shader_directory)} {
        const std::vector<std::uint32_t> vertex_code   = load_spirv(this->context.shader_directory / "visualization_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "visualization_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data(), "visualization_vertex"},
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data(), "visualization_fragment"},
        };
        this->shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
    }

    void VisualizationRenderer::record(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, DepthBufferView depth, const scene::Camera& camera, const std::span<const dynamics::GpuVisualizationDatasetView> views) {
        if (views.empty()) return;
        std::vector<vk::ImageMemoryBarrier2> barriers{};
        barriers.emplace_back(display.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput, display.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, display.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *display.image.image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        if (depth.layout != vk::ImageLayout::eShaderReadOnlyOptimal) barriers.emplace_back(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eShaderStorageRead, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, depth.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *depth.image.image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(barriers.size()), barriers.data()});
        depth.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        const vk::RenderingAttachmentInfo target{*display.image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore};
        command_buffer.beginRendering(vk::RenderingInfo{{}, {{0, 0}, display.image.extent}, 1, 0, 1, &target});
        command_buffer.setViewportWithCount(vk::Viewport{0.0f, static_cast<float>(display.image.extent.height), static_cast<float>(display.image.extent.width), -static_cast<float>(display.image.extent.height), 0.0f, 1.0f});
        command_buffer.setScissorWithCount(vk::Rect2D{{0, 0}, display.image.extent});
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::False);
        command_buffer.setDepthWriteEnable(vk::False);
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
        command_buffer.setColorBlendEquationEXT(0, vk::ColorBlendEquationEXT{vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd});
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);
        const std::array stages{vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array handles{vk::ShaderEXT{}, vk::ShaderEXT{}, *this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);

        const std::array<float, 16>& view_projection = camera.matrices().view_projection;
        for (const dynamics::GpuVisualizationDatasetView& dataset : views) {
            if (!dataset.view.visible || dataset.active_count == 0) continue;
            const dynamics::GpuDatasetBufferView* primary{};
            const dynamics::GpuDatasetBufferView* secondary{};
            if (dataset.kind == dynamics::DatasetKind::Mesh) {
                primary   = visualization_buffer(dataset, dynamics::DatasetBufferKind::MeshPosition);
                secondary = visualization_buffer(dataset, dynamics::DatasetBufferKind::MeshIndex);
            } else if (dataset.kind == dynamics::DatasetKind::PointSet)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::Point);
            else if (dataset.kind == dynamics::DatasetKind::SegmentSet)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::Segment);
            else if (dataset.kind == dynamics::DatasetKind::CurveSet)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::Curve);
            else if (dataset.kind == dynamics::DatasetKind::VectorSet)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::Vector);
            else if (dataset.kind == dynamics::DatasetKind::TransformSet)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::Transform);
            else if (dataset.kind == dynamics::DatasetKind::Image)
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::ImagePixel);
            else if (dataset.kind == dynamics::DatasetKind::CameraObservationSet) {
                primary   = visualization_buffer(dataset, dynamics::DatasetBufferKind::CameraObservation);
                secondary = visualization_buffer(dataset, dynamics::DatasetBufferKind::ImagePixel);
            } else {
                const auto channel = std::ranges::find(dataset.field_channels, dataset.view.channel_id, &dynamics::FieldChannelDescriptor::id);
                const std::uint32_t channel_index = static_cast<std::uint32_t>(channel - dataset.field_channels.begin());
                primary = visualization_buffer(dataset, dynamics::DatasetBufferKind::FieldChannel, channel_index);
            }
            const std::array<float, 16>& transform = dataset.transform.matrix;
            const bool image_dataset = dataset.kind == dynamics::DatasetKind::Image || dataset.kind == dynamics::DatasetKind::CameraObservationSet;
            VisualizationPushData push{
                primary->descriptor,
                secondary ? secondary->descriptor : primary->descriptor,
                depth.descriptor,
                static_cast<std::uint32_t>(dataset.color_space),
                0,
                {static_cast<std::uint32_t>(dataset.view.kind), static_cast<std::uint32_t>(dataset.active_count), static_cast<std::uint32_t>(dataset.secondary_count), static_cast<std::uint32_t>(dataset.view.depth_mode)},
                {display.image.extent.width, display.image.extent.height, image_dataset ? dataset.image_extent[0] : dataset.resolution.x, image_dataset ? dataset.image_extent[1] : dataset.resolution.y},
                dataset.kind == dynamics::DatasetKind::PointSet
                    ? std::array{
                          static_cast<std::uint32_t>(dataset.view.point_glyph),
                          static_cast<std::uint32_t>(dataset.view.point_shading),
                          static_cast<std::uint32_t>(dataset.view.color_source),
                          static_cast<std::uint32_t>(dataset.view.color_map),
                      }
                    : std::array{dataset.resolution.z, dataset.view.sampling, dataset.view.slice_axis, static_cast<std::uint32_t>(dataset.image_format)},
                dataset.kind == dynamics::DatasetKind::PointSet
                    ? std::array{dataset.view.width, dataset.view.scale, dataset.view.scalar_minimum, dataset.view.scalar_maximum}
                    : std::array{dataset.view.width, dataset.view.scale, dataset.view.slice_position, dataset.kind == dynamics::DatasetKind::Field ? static_cast<float>(dataset.field_channels[primary->channel_index].kind) : 0.0f},
                {dataset.view.color.x, dataset.view.color.y, dataset.view.color.z, dataset.view.color.w},
                {dataset.view.screen_rect.x, dataset.view.screen_rect.y, dataset.view.screen_rect.z, dataset.view.screen_rect.w},
                {transform[0], transform[1], transform[2], transform[3]},
                {transform[4], transform[5], transform[6], transform[7]},
                {transform[8], transform[9], transform[10], transform[11]},
                {transform[12], transform[13], transform[14], transform[15]},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            if (dataset.view.kind == scene::VisualizationViewKind::Points)
                command_buffer.draw(dataset.view.point_glyph == scene::PointGlyph::Cross ? 12u : 6u, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::Segments)
                command_buffer.draw(6, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::Curves)
                command_buffer.draw(96, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::Vectors)
                command_buffer.draw(18, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::FieldSlice || dataset.view.kind == scene::VisualizationViewKind::Image)
                command_buffer.draw(6, 1, 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::FieldVectors)
                command_buffer.draw(18, dataset.view.sampling * dataset.view.sampling * dataset.view.sampling, 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::CameraObservations)
                command_buffer.draw(54, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else if (dataset.view.kind == scene::VisualizationViewKind::Frames)
                command_buffer.draw(18, static_cast<std::uint32_t>(dataset.active_count), 0, 0);
            else
                command_buffer.draw(static_cast<std::uint32_t>(dataset.secondary_count), 1, 0, 0);
        }
        command_buffer.endRendering();
        const vk::ImageMemoryBarrier2 target_to_sample{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *display.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_sample});
        display.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
} // namespace spectra
