module spectra.render.composition.visualization;

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

        struct ResolvedVisualization {
            const dynamics::VisualizationStyle* style{};
            DescriptorHandle primary{};
            DescriptorHandle secondary{};
            DescriptorHandle attribute{};
            std::uint32_t active_count{};
            std::uint32_t secondary_count{};
            std::array<std::uint32_t, 2> image_extent{};
            scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
            math::Transform transform{};
            scene::VisualizationViewKind kind{scene::VisualizationViewKind::Segments};
            scene::PointGlyph point_glyph{scene::PointGlyph::ScreenDisc};
            scene::PointShading point_shading{scene::PointShading::Unlit};
            scene::VisualizationColorSource color_source{scene::VisualizationColorSource::Element};
            scene::VisualizationColorMap color_map{scene::VisualizationColorMap::Viridis};
            math::Float4 screen_rect{0.02f, 0.02f, 0.32f, 0.32f};
            float width{1.0f};
            float scale{1.0f};
            float scalar_minimum{};
            float scalar_maximum{1.0f};
            bool point{};
            bool image{};
        };

        [[nodiscard]] ResolvedVisualization resolve_visualization(const dynamics::GpuVisualization& source) noexcept {
            ResolvedVisualization result{};
            std::visit(
                [&result](const auto& value) {
                    result.style     = &value.style;
                    result.transform = value.style.transform;
                    result.kind      = scene::visualization_view_kind(value.style.view);
                    std::visit(
                        [&result](const auto& data) {
                            if constexpr (requires { data.width; }) result.width = data.width;
                            if constexpr (requires { data.scale; }) result.scale = data.scale;
                            if constexpr (requires { data.scalar_minimum; }) {
                                result.scalar_minimum = data.scalar_minimum;
                                result.scalar_maximum = data.scalar_maximum;
                                result.color_source   = data.color_source;
                                result.color_map      = data.color_map;
                            }
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointVisualization>) {
                                result.point_glyph   = data.glyph;
                                result.point_shading = data.shading;
                            } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::ImageVisualization>) result.screen_rect = data.screen_rect;
                        },
                        value.style.view.data);
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, dynamics::GpuPointVisualization>) {
                        result.primary      = value.points.descriptor;
                        result.active_count = value.count;
                        result.point        = true;
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, dynamics::GpuSegmentVisualization>) {
                        result.primary      = value.segments.descriptor;
                        result.active_count = value.count;
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, dynamics::GpuVectorVisualization>) {
                        result.primary      = value.vectors.descriptor;
                        result.active_count = value.count;
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, dynamics::GpuImageVisualization>) {
                        result.primary           = value.pixels.descriptor;
                        result.active_count      = 1;
                        result.image_extent      = value.image.extent;
                        result.color_space       = value.image.color_space;
                        result.image             = true;
                    } else {
                        result.primary = value.positions.descriptor;
                        if (value.indices) result.secondary = value.indices->descriptor;
                        if (result.color_source == scene::VisualizationColorSource::Element && value.colors) result.attribute = value.colors->descriptor;
                        if (result.color_source == scene::VisualizationColorSource::Element && !value.colors) result.color_source = scene::VisualizationColorSource::Uniform;
                        if (result.color_source == scene::VisualizationColorSource::Scalar && value.scalars) result.attribute = value.scalars->descriptor;
                        if (result.color_source == scene::VisualizationColorSource::Scalar && !value.scalars) result.color_source = scene::VisualizationColorSource::Uniform;
                        result.active_count    = value.vertex_count;
                        result.secondary_count = value.index_count;
                    }
                    if (!result.secondary) result.secondary = result.primary;
                },
                source.data);
            return result;
        }

    } // namespace

    VisualizationRenderer::VisualizationRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {
        const std::vector<std::uint32_t> vertex_code   = load_spirv(this->context.shader_directory / "visualization_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = load_spirv(this->context.shader_directory / "visualization_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data(), "visualization_vertex"},
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data(), "visualization_fragment"},
        };
        this->shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, create_infos};
    }

    VisualizationRenderer::~VisualizationRenderer() {
        this->context.runtime.frames.defer_destruction([shaders = std::move(this->shaders)]() mutable {});
    }

    bool VisualizationRenderer::has_visible(const scene::SceneView scene, const std::span<const dynamics::GpuVisualization> views, const scene::VisualizationCompositionDomain domain) const noexcept {
        if (std::ranges::any_of(views, [domain](const dynamics::GpuVisualization& source) { return std::visit([domain](const auto& value) { return value.style.view.visible && value.style.view.composition_domain == domain; }, source.data); })) return true;
        return domain == scene::VisualizationCompositionDomain::DisplayReferred && std::ranges::any_of(scene.resources.volumes, [](const scene::Volume& volume) { return std::holds_alternative<scene::GridVolume>(volume.data) && volume.diagnostics.mode != scene::VolumeDiagnosticMode::Off; });
    }

    void VisualizationRenderer::record(const vk::raii::CommandBuffer& command_buffer, const ColorCompositionTarget target, DepthBufferView depth, const scene::SceneView scene, const scene::Camera& camera, const std::span<const dynamics::GpuVisualization> views, const scene::VisualizationCompositionDomain domain) {
        std::vector<vk::ImageMemoryBarrier2> barriers{};
        const vk::PipelineStageFlags2 target_stage = target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::PipelineStageFlagBits2::eCopy : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 target_access       = target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::AccessFlagBits2::eTransferWrite : vk::AccessFlagBits2::eColorAttachmentWrite;
        barriers.emplace_back(target_stage, target_access, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, target.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        if (depth.layout != vk::ImageLayout::eShaderReadOnlyOptimal) barriers.emplace_back(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, depth.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *depth.image.image, vk::ImageSubresourceRange{depth.image.aspect, 0, 1, 0, 1});
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, static_cast<std::uint32_t>(barriers.size()), barriers.data()});
        depth.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        const vk::RenderingAttachmentInfo attachment{*target.image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore};
        command_buffer.beginRendering(vk::RenderingInfo{{}, {{0, 0}, target.image.extent}, 1, 0, 1, &attachment});
        command_buffer.setViewportWithCount(vk::Viewport{0.0f, static_cast<float>(target.image.extent.height), static_cast<float>(target.image.extent.width), -static_cast<float>(target.image.extent.height), 0.0f, 1.0f});
        command_buffer.setScissorWithCount(vk::Rect2D{{0, 0}, target.image.extent});
        command_buffer.setCullMode(vk::CullModeFlagBits::eNone);
        command_buffer.setFrontFace(vk::FrontFace::eCounterClockwise);
        command_buffer.setDepthTestEnable(vk::False);
        command_buffer.setDepthWriteEnable(vk::False);
        set_basic_graphics_state(command_buffer);
        command_buffer.setVertexInputEXT({}, {});
        command_buffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);
        command_buffer.setPrimitiveRestartEnable(vk::False);
        constexpr vk::Bool32 blend_enable = vk::True;
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        command_buffer.setColorBlendEquationEXT(0, vk::ColorBlendEquationEXT{vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd});
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        command_buffer.setColorWriteMaskEXT(0, color_components);
        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array handles{vk::ShaderEXT{}, *this->shaders[0], *this->shaders[1]};
        command_buffer.bindShadersEXT(stages, handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);

        const std::array<float, 16>& view_projection = camera.matrices().view_projection;
        std::array<float, 3> camera_depth{};
        std::visit([&camera_depth](const auto& data) { camera_depth = {data.near_plane, data.far_plane, std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData> ? 1.0f : 0.0f}; }, camera.data);
        for (const dynamics::GpuVisualization& source : views) {
            const ResolvedVisualization dataset         = resolve_visualization(source);
            const scene::DynamicVisualizationView& view = dataset.style->view;
            if (!view.visible || view.composition_domain != domain || dataset.active_count == 0) continue;
            const std::array<float, 16>& transform = dataset.transform.matrix;
            std::array<std::uint32_t, 4> detail{0, 0, static_cast<std::uint32_t>(dataset.color_source), static_cast<std::uint32_t>(dataset.color_map)};
            if (dataset.point) {
                detail[0] = static_cast<std::uint32_t>(dataset.point_glyph);
                detail[1] = static_cast<std::uint32_t>(dataset.point_shading);
            }
            if (dataset.image) detail[0] = static_cast<std::uint32_t>(target.color_space);
            std::array<float, 4> screen_rect{dataset.screen_rect.x, dataset.screen_rect.y, dataset.screen_rect.z, dataset.screen_rect.w};
            if (dataset.point) screen_rect = {camera_depth[0], camera_depth[1], camera_depth[2], 0.0f};
            VisualizationPushData push{
                dataset.primary,
                dataset.secondary,
                depth.descriptor,
                dataset.image ? static_cast<std::uint32_t>(dataset.color_space) : dataset.attribute.slot_index,
                dataset.attribute.reserved,
                {static_cast<std::uint32_t>(dataset.kind), dataset.active_count, dataset.secondary_count, static_cast<std::uint32_t>(view.depth_mode)},
                {target.image.extent.width, target.image.extent.height, dataset.image ? dataset.image_extent[0] : 0u, dataset.image ? dataset.image_extent[1] : 0u},
                detail,
                {dataset.width, dataset.scale, dataset.scalar_minimum, dataset.scalar_maximum},
                {view.color.x, view.color.y, view.color.z, view.color.w},
                screen_rect,
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
            if (dataset.kind == scene::VisualizationViewKind::Points)
                command_buffer.draw(dataset.point_glyph == scene::PointGlyph::Cross ? 12u : 6u, dataset.active_count, 0, 0);
            else if (dataset.kind == scene::VisualizationViewKind::Segments)
                command_buffer.draw(6, dataset.active_count, 0, 0);
            else if (dataset.kind == scene::VisualizationViewKind::Vectors)
                command_buffer.draw(18, dataset.active_count, 0, 0);
            else if (dataset.kind == scene::VisualizationViewKind::Image)
                command_buffer.draw(6, 1, 0, 0);
            else
                command_buffer.draw(dataset.secondary_count != 0 ? dataset.secondary_count : dataset.active_count, 1, 0, 0);
        }
        const GpuSceneView gpu_scene = this->context.gpu_scene.view();
        const scene::CameraMatrices camera_matrices = camera.matrices();
        for (const scene::Volume& volume : scene.resources.volumes) {
            if (domain != scene::VisualizationCompositionDomain::DisplayReferred || volume.diagnostics.mode == scene::VolumeDiagnosticMode::Off) continue;
            const auto* grid = std::get_if<scene::GridVolume>(&volume.data);
            if (!grid) continue;
            const scene::VolumeDiagnostics& diagnostics = volume.diagnostics;
            const GpuVolume& gpu_volume = *std::ranges::find(gpu_scene.volumes, volume.id, &GpuVolume::volume_id);
            const math::Float3 extent   = volume.bounds.diagonal();
            const math::Transform grid_to_local{{
                extent.x, 0.0f, 0.0f, volume.bounds.minimum.x,
                0.0f, extent.y, 0.0f, volume.bounds.minimum.y,
                0.0f, 0.0f, extent.z, volume.bounds.minimum.z,
                0.0f, 0.0f, 0.0f, 1.0f,
            }};
            const math::Transform grid_to_world = volume.transform * grid_to_local;
            const GpuVolumeField& field = *std::ranges::find(gpu_volume.fields, diagnostics.field_id, &GpuVolumeField::id);
            DescriptorHandle primary   = field.descriptors.front();
            DescriptorHandle secondary = primary;
            DescriptorHandle tertiary  = primary;
            if (field.kind == scene::VolumeFieldKind::MacFloat3) secondary = field.descriptors[1], tertiary = field.descriptors[2];
            math::Transform vector_to_grid{};
            if (field.vector_space == scene::VolumeVectorSpace::Grid)
                vector_to_grid = math::Transform{{
                    1.0f / static_cast<float>(grid->resolution.x), 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f / static_cast<float>(grid->resolution.y), 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f / static_cast<float>(grid->resolution.z), 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f,
                }};
            else if (field.vector_space == scene::VolumeVectorSpace::Local)
                vector_to_grid = grid_to_local.inverse();
            else
                vector_to_grid = grid_to_world.inverse();
            const bool ray_mode = diagnostics.mode == scene::VolumeDiagnosticMode::RayMarch || diagnostics.mode == scene::VolumeDiagnosticMode::MaximumIntensityProjection || diagnostics.mode == scene::VolumeDiagnosticMode::Isosurface;
            const math::Transform projection = ray_mode ? grid_to_world.inverse() * math::Transform{camera_matrices.inverse_view_projection} : math::Transform{camera_matrices.view_projection} * grid_to_world;
            const std::array<float, 16>& transform = vector_to_grid.matrix;
            const std::array<float, 16>& projected = projection.matrix;
            const VisualizationPushData push{
                primary,
                secondary,
                depth.descriptor,
                tertiary.slot_index,
                tertiary.reserved,
                {7u + std::to_underlying(diagnostics.mode), static_cast<std::uint32_t>(std::to_underlying(field.kind)) | (static_cast<std::uint32_t>(std::to_underlying(field.sampling)) << 8u), std::to_underlying(diagnostics.mapping), std::to_underlying(diagnostics.depth_mode)},
                {target.image.extent.width, target.image.extent.height, grid->resolution.x, grid->resolution.y},
                {grid->resolution.z, diagnostics.axis, diagnostics.sampling, diagnostics.steps},
                {diagnostics.width, diagnostics.scale, diagnostics.minimum, diagnostics.maximum},
                {diagnostics.color.x, diagnostics.color.y, diagnostics.color.z, diagnostics.color.w},
                {diagnostics.slice_position, diagnostics.opacity, diagnostics.threshold, static_cast<float>(std::to_underlying(diagnostics.color_map))},
                {transform[0], transform[1], transform[2], transform[3]},
                {transform[4], transform[5], transform[6], transform[7]},
                {transform[8], transform[9], transform[10], transform[11]},
                {transform[12], transform[13], transform[14], transform[15]},
                {projected[0], projected[1], projected[2], projected[3]},
                {projected[4], projected[5], projected[6], projected[7]},
                {projected[8], projected[9], projected[10], projected[11]},
                {projected[12], projected[13], projected[14], projected[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            const std::uint32_t seed_count = diagnostics.sampling * diagnostics.sampling * diagnostics.sampling;
            if (ray_mode)
                command_buffer.draw(3, 1, 0, 0);
            else if (diagnostics.mode == scene::VolumeDiagnosticMode::Slice || diagnostics.mode == scene::VolumeDiagnosticMode::Lic)
                command_buffer.draw(6, 1, 0, 0);
            else if (diagnostics.mode == scene::VolumeDiagnosticMode::Glyphs)
                command_buffer.draw(18, seed_count, 0, 0);
            else
                command_buffer.draw(6, seed_count * diagnostics.steps, 0, 0);
        }
        command_buffer.endRendering();
        const vk::ImageMemoryBarrier2 target_to_sample{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_sample});
        target.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
} // namespace spectra
