module;

#include "../shaders/shader_semantics.h"

module spectra.render.display.visualization;

import spectra.render.shader_abi;
import std;
import vulkan;

namespace spectra::render {
    namespace {
        [[nodiscard]] constexpr std::uint32_t shader_depth_mode(const scene::VisualizationDepthMode mode) noexcept {
            switch (mode) {
            case scene::VisualizationDepthMode::Tested: return shader_semantics::depth_tested;
            case scene::VisualizationDepthMode::XRay: return shader_semantics::depth_xray;
            case scene::VisualizationDepthMode::Overlay: return shader_semantics::depth_overlay;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_color_source(const scene::VisualizationColorSource source) noexcept {
            switch (source) {
            case scene::VisualizationColorSource::Element: return shader_semantics::color_source_element;
            case scene::VisualizationColorSource::Uniform: return shader_semantics::color_source_uniform;
            case scene::VisualizationColorSource::Scalar: return shader_semantics::color_source_scalar;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_color_map(const scene::VisualizationColorMap map) noexcept {
            switch (map) {
            case scene::VisualizationColorMap::Viridis: return shader_semantics::color_map_viridis;
            case scene::VisualizationColorMap::Turbo: return shader_semantics::color_map_turbo;
            case scene::VisualizationColorMap::CoolWarm: return shader_semantics::color_map_cool_warm;
            case scene::VisualizationColorMap::Grayscale: return shader_semantics::color_map_grayscale;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_field_kind(const scene::FieldKind kind) noexcept {
            switch (kind) {
            case scene::FieldKind::Float: return shader_semantics::field_float;
            case scene::FieldKind::Float3: return shader_semantics::field_float3;
            case scene::FieldKind::UInt32: return shader_semantics::field_uint32;
            case scene::FieldKind::MacFloat3: return shader_semantics::field_mac_float3;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_field_sampling(const scene::VolumeFieldSampling sampling) noexcept {
            switch (sampling) {
            case scene::VolumeFieldSampling::Cell: return shader_semantics::field_sampling_cell;
            case scene::VolumeFieldSampling::Vertex: return shader_semantics::field_sampling_vertex;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_field_mapping(const scene::FieldMapping mapping) noexcept {
            switch (mapping) {
            case scene::FieldMapping::Value: return shader_semantics::field_mapping_value;
            case scene::FieldMapping::Magnitude: return shader_semantics::field_mapping_magnitude;
            case scene::FieldMapping::X: return shader_semantics::field_mapping_x;
            case scene::FieldMapping::Y: return shader_semantics::field_mapping_y;
            case scene::FieldMapping::Z: return shader_semantics::field_mapping_z;
            case scene::FieldMapping::Divergence: return shader_semantics::field_mapping_divergence;
            case scene::FieldMapping::CurlMagnitude: return shader_semantics::field_mapping_curl_magnitude;
            case scene::FieldMapping::QCriterion: return shader_semantics::field_mapping_q_criterion;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_particle_display(const scene::ParticleDisplayMode display) noexcept {
            switch (display) {
            case scene::ParticleDisplayMode::Points: return shader_semantics::particle_points;
            case scene::ParticleDisplayMode::Discs: return shader_semantics::particle_discs;
            case scene::ParticleDisplayMode::Spheres: return shader_semantics::particle_spheres;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_volume_mode(const scene::VolumeDiagnosticMode mode) noexcept {
            switch (mode) {
            case scene::VolumeDiagnosticMode::Slice: return shader_semantics::visualization_volume_slice;
            case scene::VolumeDiagnosticMode::Cells: return shader_semantics::visualization_volume_cells;
            case scene::VolumeDiagnosticMode::RayMarch: return shader_semantics::visualization_volume_ray_march;
            case scene::VolumeDiagnosticMode::MaximumIntensityProjection: return shader_semantics::visualization_volume_mip;
            case scene::VolumeDiagnosticMode::Isosurface: return shader_semantics::visualization_volume_isosurface;
            case scene::VolumeDiagnosticMode::Glyphs: return shader_semantics::visualization_volume_glyphs;
            case scene::VolumeDiagnosticMode::Streamlines: return shader_semantics::visualization_volume_streamlines;
            case scene::VolumeDiagnosticMode::Lic: return shader_semantics::visualization_volume_lic;
            case scene::VolumeDiagnosticMode::Off: break;
            }
            std::unreachable();
        }
    } // namespace

    VisualizationPass::VisualizationPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {
        const std::vector<std::uint32_t> vertex_code   = runtime::load_spirv(this->context.shader_directory / "visualization_vertex.spv");
        const std::vector<std::uint32_t> fragment_code = runtime::load_spirv(this->context.shader_directory / "visualization_fragment.spv");
        const std::array create_infos{
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data(), "visualization_vertex"},
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data(), "visualization_fragment"},
        };
        this->shaders = vk::raii::ShaderEXTs{this->context.runtime.device.logical, create_infos};
    }

    VisualizationPass::~VisualizationPass() {
        this->context.runtime.frames.defer_destruction([shaders = std::move(this->shaders)]() mutable {});
    }

    bool VisualizationPass::has_visible(const scene::ResolvedSceneView scene, const std::span<const simulation::GpuVisualization> views, const scene::VisualizationCompositionDomain domain) const noexcept {
        if (std::ranges::any_of(views, [domain](const simulation::GpuVisualization& source) { return std::visit([domain](const auto& value) { return value.style.view.visible && value.style.view.composition_domain == domain; }, source.data); })) return true;
        if (domain == scene::VisualizationCompositionDomain::SceneLinear && std::ranges::any_of(scene.resources.particle_sets, [](const scene::ParticleSet& particles) { return particles.visible; })) return true;
        return domain == scene::VisualizationCompositionDomain::DisplayReferred && std::ranges::any_of(scene.resources.volumes, [](const scene::Volume& volume) { return volume.visible && std::holds_alternative<scene::GridVolume>(volume.data) && volume.diagnostics.mode != scene::VolumeDiagnosticMode::Off; });
    }

    void VisualizationPass::record(const vk::raii::CommandBuffer& command_buffer, const ColorTarget target, DepthBufferView depth, const scene::ResolvedSceneView scene, const scene::Camera& camera, const std::span<const simulation::GpuVisualization> views, const scene::VisualizationCompositionDomain domain, const CameraReferenceRequest* camera_reference) {
        std::vector<vk::ImageMemoryBarrier2> barriers{};
        const vk::PipelineStageFlags2 target_stage = target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::PipelineStageFlagBits2::eCopy : target.layout == vk::ImageLayout::eGeneral ? vk::PipelineStageFlagBits2::eComputeShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        const vk::AccessFlags2 target_access       = target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : target.layout == vk::ImageLayout::eTransferDstOptimal ? vk::AccessFlagBits2::eTransferWrite : target.layout == vk::ImageLayout::eGeneral ? vk::AccessFlagBits2::eShaderStorageWrite : vk::AccessFlagBits2::eColorAttachmentWrite;
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
        runtime::record_default_graphics_state(command_buffer);
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
        for (const simulation::GpuVisualization& source : views) {
            std::visit(
                [&](const auto& output) {
                    const scene::SimulationVisualization& view = output.style.view;
                    const std::array<float, 16>& transform     = output.style.transform.matrix;
                    runtime::DescriptorHandle primary{};
                    runtime::DescriptorHandle secondary{};
                    runtime::DescriptorHandle attribute{};
                    std::uint32_t active_count{};
                    std::uint32_t secondary_count{};
                    std::uint32_t kind{};
                    std::array<std::uint32_t, 2> image_extent{};
                    std::uint32_t color_space{};
                    std::uint32_t color_source = shader_semantics::color_source_element;
                    std::uint32_t color_map    = shader_semantics::color_map_viridis;
                    math::Float4 screen_rect{0.02f, 0.02f, 0.32f, 0.32f};
                    float width{1.0f};
                    float scale{1.0f};
                    float scalar_minimum{};
                    float scalar_maximum{1.0f};
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuSegmentVisualization>) {
                        const scene::SegmentVisualization& style = std::get<scene::SegmentVisualization>(view.data);
                        primary                                  = output.segments.descriptor;
                        active_count                             = output.count;
                        kind                                     = shader_semantics::visualization_segments;
                        width                                    = style.width;
                        scalar_minimum                           = style.scalar_minimum;
                        scalar_maximum                           = style.scalar_maximum;
                        color_source                             = shader_color_source(style.color_source);
                        color_map                                = shader_color_map(style.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuVectorVisualization>) {
                        const scene::VectorVisualization& style = std::get<scene::VectorVisualization>(view.data);
                        primary                                 = output.vectors.descriptor;
                        active_count                            = output.count;
                        kind                                    = shader_semantics::visualization_vectors;
                        width                                   = style.width;
                        scale                                   = style.scale;
                        scalar_minimum                          = style.scalar_minimum;
                        scalar_maximum                          = style.scalar_maximum;
                        color_source                            = shader_color_source(style.color_source);
                        color_map                               = shader_color_map(style.color_map);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuImageVisualization>) {
                        primary      = output.pixels.descriptor;
                        active_count = 1u;
                        kind         = shader_semantics::visualization_image;
                        image_extent = output.image.extent;
                        color_space  = std::to_underlying(output.image.color_space);
                        screen_rect  = std::get<scene::ImageVisualization>(view.data).screen_rect;
                    } else {
                        const scene::SurfaceVisualization& style = std::get<scene::SurfaceVisualization>(view.data);
                        primary                                  = output.positions.descriptor;
                        secondary                                = output.indices ? output.indices->descriptor : primary;
                        active_count                             = output.vertex_count;
                        secondary_count                          = output.index_count;
                        kind                                     = shader_semantics::visualization_surface;
                        scalar_minimum                           = style.scalar_minimum;
                        scalar_maximum                           = style.scalar_maximum;
                        color_source                             = shader_color_source(style.color_source);
                        color_map                                = shader_color_map(style.color_map);
                        if (style.color_source == scene::VisualizationColorSource::Element && output.colors) attribute = output.colors->descriptor;
                        else if (style.color_source == scene::VisualizationColorSource::Scalar && output.scalars) attribute = output.scalars->descriptor;
                        else color_source = shader_semantics::color_source_uniform;
                    }
                    if (!view.visible || view.composition_domain != domain || active_count == 0u) return;
                    if (!secondary) secondary = primary;
                    const VisualizationPushData push{
                        primary,
                        secondary,
                        depth.descriptor,
                        std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuImageVisualization> ? color_space : attribute.slot_index,
                        attribute.reserved,
                        {kind, active_count, secondary_count, shader_depth_mode(view.depth_mode)},
                        {target.image.extent.width, target.image.extent.height, image_extent[0], image_extent[1]},
                        {std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuImageVisualization> ? std::to_underlying(target.color_space) : 0u, 0u, color_source, color_map},
                        {width, scale, scalar_minimum, scalar_maximum},
                        {view.color.x, view.color.y, view.color.z, view.color.w},
                        {screen_rect.x, screen_rect.y, screen_rect.z, screen_rect.w},
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
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuSegmentVisualization>) command_buffer.draw(6u, active_count, 0u, 0u);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuVectorVisualization>) command_buffer.draw(18u, active_count, 0u, 0u);
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(output)>, simulation::GpuImageVisualization>) command_buffer.draw(6u, 1u, 0u, 0u);
                    else command_buffer.draw(secondary_count != 0u ? secondary_count : active_count, 1u, 0u, 0u);
                },
                source.data);
        }
        const GpuSceneView gpu_scene                = this->context.gpu_scene.view();
        const scene::CameraMatrices camera_matrices = camera.matrices();
        if (domain == scene::VisualizationCompositionDomain::SceneLinear) {
            for (const scene::ParticleSet& particles : scene.resources.particle_sets) {
                if (!particles.visible) continue;
                const GpuParticleSet& gpu_particles = *std::ranges::find(gpu_scene.particle_sets, particles.id, &GpuParticleSet::particle_set_id);
                if (gpu_particles.count == 0u) continue;
                const scene::ParticleVisualization& visualization = particles.visualization;
                runtime::DescriptorHandle attribute               = gpu_particles.positions;
                std::uint32_t field_kind                          = std::numeric_limits<std::uint32_t>::max();
                if (!visualization.field_id.empty()) {
                    const GpuParticleField& field = *std::ranges::find(gpu_particles.fields, visualization.field_id, &GpuParticleField::id);
                    attribute                     = field.descriptor;
                    field_kind                    = shader_field_kind(field.kind);
                }
                const std::array<float, 16>& transform = particles.transform.matrix;
                const VisualizationPushData push{
                    gpu_particles.positions,
                    attribute,
                    depth.descriptor,
                    0u,
                    0u,
                    {shader_semantics::visualization_particles, field_kind, gpu_particles.count, shader_depth_mode(visualization.depth_mode)},
                    {target.image.extent.width, target.image.extent.height, 0u, 0u},
                    {shader_particle_display(visualization.display), shader_field_mapping(visualization.mapping), shader_color_map(visualization.color_map), 0u},
                    {visualization.display == scene::ParticleDisplayMode::Points ? visualization.point_size : particles.radius * visualization.radius_scale, 0.0f, visualization.minimum, visualization.maximum},
                    {visualization.color.x, visualization.color.y, visualization.color.z, visualization.color.w},
                    {camera_depth[0], camera_depth[1], camera_depth[2], 0.0f},
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
                command_buffer.draw(6u, gpu_particles.count, 0u, 0u);
            }
        }
        for (const scene::Volume& volume : scene.resources.volumes) {
            if (domain != scene::VisualizationCompositionDomain::DisplayReferred || !volume.visible || volume.diagnostics.mode == scene::VolumeDiagnosticMode::Off) continue;
            const auto* grid = std::get_if<scene::GridVolume>(&volume.data);
            if (!grid) continue;
            const scene::VolumeDiagnostics& diagnostics = volume.diagnostics;
            const GpuVolume& gpu_volume                 = *std::ranges::find(gpu_scene.volumes, volume.id, &GpuVolume::volume_id);
            const math::Float3 extent                   = volume.domain.diagonal();
            const math::Transform grid_to_local{{
                extent.x,
                0.0f,
                0.0f,
                volume.domain.minimum.x,
                0.0f,
                extent.y,
                0.0f,
                volume.domain.minimum.y,
                0.0f,
                0.0f,
                extent.z,
                volume.domain.minimum.z,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            }};
            const math::Transform grid_to_world = volume.transform * grid_to_local;
            const GpuVolumeField& field         = *std::ranges::find(gpu_volume.fields, diagnostics.field_id, &GpuVolumeField::id);
            runtime::DescriptorHandle primary   = field.descriptors.front();
            runtime::DescriptorHandle secondary = primary;
            runtime::DescriptorHandle tertiary  = primary;
            if (field.kind == scene::FieldKind::MacFloat3) secondary = field.descriptors[1], tertiary = field.descriptors[2];
            math::Transform vector_to_grid{};
            if (field.vector_space == scene::VolumeVectorSpace::Grid)
                vector_to_grid = math::Transform{{
                    1.0f / static_cast<float>(grid->resolution.x),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f / static_cast<float>(grid->resolution.y),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f / static_cast<float>(grid->resolution.z),
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                }};
            else if (field.vector_space == scene::VolumeVectorSpace::Local) vector_to_grid = grid_to_local.inverse();
            else vector_to_grid = grid_to_world.inverse();
            const bool ray_mode                    = diagnostics.mode == scene::VolumeDiagnosticMode::RayMarch || diagnostics.mode == scene::VolumeDiagnosticMode::MaximumIntensityProjection || diagnostics.mode == scene::VolumeDiagnosticMode::Isosurface;
            const math::Transform projection       = ray_mode ? grid_to_world.inverse() * math::Transform{camera_matrices.inverse_view_projection} : math::Transform{camera_matrices.view_projection} * grid_to_world;
            const std::array<float, 16>& transform = vector_to_grid.matrix;
            const std::array<float, 16>& projected = projection.matrix;
            const VisualizationPushData push{
                primary,
                secondary,
                depth.descriptor,
                field.kind == scene::FieldKind::UInt32 ? diagnostics.category_mask : tertiary.slot_index,
                field.kind == scene::FieldKind::UInt32 ? 0u : tertiary.reserved,
                {shader_volume_mode(diagnostics.mode), shader_field_kind(field.kind) | (shader_field_sampling(field.sampling) << 8u), shader_field_mapping(diagnostics.mapping), shader_depth_mode(diagnostics.depth_mode)},
                {target.image.extent.width, target.image.extent.height, grid->resolution.x, grid->resolution.y},
                {grid->resolution.z, diagnostics.axis, diagnostics.sampling, diagnostics.steps},
                {diagnostics.width, diagnostics.scale, diagnostics.minimum, diagnostics.maximum},
                {diagnostics.color.x, diagnostics.color.y, diagnostics.color.z, diagnostics.color.w},
                {diagnostics.slice_position, diagnostics.opacity, diagnostics.threshold, static_cast<float>(shader_color_map(diagnostics.color_map))},
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
            if (ray_mode) command_buffer.draw(3, 1, 0, 0);
            else if (diagnostics.mode == scene::VolumeDiagnosticMode::Cells) command_buffer.draw(72u, grid->resolution.x * grid->resolution.y * grid->resolution.z, 0u, 0u);
            else if (diagnostics.mode == scene::VolumeDiagnosticMode::Slice || diagnostics.mode == scene::VolumeDiagnosticMode::Lic) command_buffer.draw(6, 1, 0, 0);
            else if (diagnostics.mode == scene::VolumeDiagnosticMode::Glyphs) command_buffer.draw(18, seed_count, 0, 0);
            else command_buffer.draw(6, seed_count * diagnostics.steps, 0, 0);
        }
        if (domain == scene::VisualizationCompositionDomain::DisplayReferred && camera_reference) {
            const simulation::CameraReferenceImage& reference    = *camera_reference->reference;
            const scene::PerspectiveCameraData& reference_camera = std::get<scene::PerspectiveCameraData>(camera_reference->camera->data);
            const std::array<float, 16>& transform               = camera_reference->camera->transform.matrix;
            const std::array<float, 16>& projected               = camera.matrices().view_projection;
            const float tangent                                  = std::tan(reference_camera.vertical_fov * std::numbers::pi_v<float> / 360.0f);
            const auto draw_reference                            = [&](const std::uint32_t kind, const math::Float4 parameters, const math::Float4 screen_rect, const std::uint32_t depth_mode) {
                const VisualizationPushData push{
                    reference.pixels.descriptor,
                    reference.pixels.descriptor,
                    depth.descriptor,
                    0u,
                    0u,
                                               {kind, 1u, 0u, depth_mode},
                                               {target.image.extent.width, target.image.extent.height, reference.extent[0], reference.extent[1]},
                                               {reference.layer, 0u, 0u, 0u},
                                               {parameters.x, parameters.y, parameters.z, parameters.w},
                                               {1.0f, 1.0f, 1.0f, 1.0f},
                                               {screen_rect.x, screen_rect.y, screen_rect.z, screen_rect.w},
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
                command_buffer.draw(6, 1, 0, 0);
            };
            if (camera_reference->plane) draw_reference(shader_semantics::visualization_reference_plane, {tangent, reference_camera.focal_distance, 0.0f, 0.0f}, {reference_camera.screen_window.minimum.x, reference_camera.screen_window.minimum.y, reference_camera.screen_window.maximum.x, reference_camera.screen_window.maximum.y}, shader_semantics::depth_tested);
            if (camera_reference->overlay) draw_reference(shader_semantics::visualization_reference_overlay, {}, camera_reference->overlay_rect, shader_semantics::depth_overlay);
        }
        command_buffer.endRendering();
        const vk::ImageMemoryBarrier2 target_to_sample{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, {}, {}, {}, {}, 1, &target_to_sample});
        target.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    }
} // namespace spectra::render
