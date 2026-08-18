module;

#include "../shaders/shader_semantics.h"
#include <spectra/sdk/neural_field_layout.h>

module spectra.render.display.diagnostics;

import spectra.render.shader_abi;
import std;
import vulkan;

namespace spectra::render {
    namespace {
        [[nodiscard]] constexpr std::array<float, 3> shader_value(const math::Float3 value) noexcept {
            return {value.x, value.y, value.z};
        }
        [[nodiscard]] constexpr std::array<float, 4> shader_value(const math::Float4 value) noexcept {
            return {value.x, value.y, value.z, value.w};
        }

        [[nodiscard]] constexpr std::uint32_t shader_depth_mode(const scene::VisualizationDepthMode mode) noexcept {
            switch (mode) {
            case scene::VisualizationDepthMode::Tested: return shader_semantics::depth_tested;
            case scene::VisualizationDepthMode::XRay: return shader_semantics::depth_xray;
            case scene::VisualizationDepthMode::Overlay: return shader_semantics::depth_overlay;
            }
            std::unreachable();
        }

        [[nodiscard]] constexpr std::uint32_t shader_vector_space(const scene::VolumeVectorSpace space) noexcept {
            switch (space) {
            case scene::VolumeVectorSpace::Grid: return shader_semantics::vector_space_grid;
            case scene::VolumeVectorSpace::Local: return shader_semantics::vector_space_local;
            case scene::VolumeVectorSpace::World: return shader_semantics::vector_space_world;
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

        [[nodiscard]] math::Float4 diagnostic_color(const scene::EntityReference entity, const DiagnosticSelection& selection, const math::Float4 base) noexcept {
            if (selection.active == entity) return {1.0f, 0.55f, 0.08f, 1.0f};
            if (std::ranges::contains(selection.selected, entity)) return {0.10f, 0.58f, 1.0f, 1.0f};
            if (selection.hovered == entity) return {0.45f, 1.0f, 0.28f, 1.0f};
            return base;
        }

        [[nodiscard]] math::Transform light_transform(const scene::Light& light) noexcept {
            return std::visit(
                [](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) return data.environment.transform;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>) return math::Transform{};
                    else return data.transform;
                },
                light.data);
        }
    } // namespace

    DiagnosticsPass::DiagnosticsPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {
        const std::vector<std::uint32_t> vertex_code     = runtime::load_spirv(this->context.shader_directory / "scene_diagnostic_vertex.spv");
        const std::vector<std::uint32_t> fragment_code   = runtime::load_spirv(this->context.shader_directory / "scene_diagnostic_fragment.spv");
        const std::vector<std::uint32_t> compaction_code = runtime::load_spirv(this->context.shader_directory / "scene_diagnostic_occupancy_compaction.spv");
        const std::array draw_create_infos{
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data(), "scene_diagnostic_vertex"},
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data(), "scene_diagnostic_fragment"},
        };
        this->renderer.draw_shaders                = vk::raii::ShaderEXTs{this->context.runtime.device.logical, draw_create_infos};
        this->renderer.occupancy_compaction_shader = vk::raii::ShaderEXT{this->context.runtime.device.logical, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, compaction_code.size() * sizeof(std::uint32_t), compaction_code.data(), "compact_scene_diagnostic_occupancy"}};
        for (SceneDiagnosticFrameResources& frame : this->renderer.frame_resources) {
            frame.line_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            frame.box_descriptor  = this->context.runtime.frames.allocate_resource_descriptor();
        }
    }

    DiagnosticsPass::~DiagnosticsPass() {
        this->context.runtime.frames.defer_destruction([frames = std::move(this->renderer.frame_resources), draw_shaders = std::move(this->renderer.draw_shaders), occupancy_compaction_shader = std::move(this->renderer.occupancy_compaction_shader), pick_image = std::move(this->renderer.pick_image)]() mutable {});
    }

    void DiagnosticsPass::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, ColorTarget target, DepthBufferView depth, const scene::ResolvedSceneView scene_view, const scene::Camera& camera, const std::optional<scene::CameraId> scene_camera_view, const DiagnosticRequest& request) {
        SceneDiagnosticFrameResources& frame_resources = this->renderer.frame_resources[frame_slot_index];
        std::vector<DiagnosticLine> lines{};
        std::vector<DiagnosticBox> boxes{};
        const SceneGuideSettings hidden_scene_guides{};
        EntityDiagnostics hidden_entity_diagnostics{};
        hidden_entity_diagnostics.bounds               = false;
        hidden_entity_diagnostics.camera_frustum       = false;
        hidden_entity_diagnostics.camera_gt_overlay    = false;
        hidden_entity_diagnostics.light_guide          = false;
        const SceneGuideSettings& scene_guides         = request.visible ? request.scene_guides : hidden_scene_guides;
        const EntityDiagnostics& selection_diagnostics = request.visible ? request.entity : hidden_entity_diagnostics;
        const DiagnosticSelection& selection           = request.selection;
        const scene::NeuralField* occupancy_field{};
        const GpuNeuralField* occupancy_gpu_field{};
        const auto field = request.visible ? std::ranges::find_if(scene_view.resources.neural_fields, [](const scene::NeuralField& candidate) { return candidate.visible && candidate.diagnostics.occupancy_grid; }) : scene_view.resources.neural_fields.end();
        if (field != scene_view.resources.neural_fields.end()) {
            const GpuSceneView gpu_scene = this->context.gpu_scene.view();
            const auto gpu_field         = std::ranges::find(gpu_scene.neural_fields, field->id, &GpuNeuralField::neural_field_id);
            if (gpu_field != gpu_scene.neural_fields.end() && gpu_field->revision != 0) {
                occupancy_field     = &*field;
                occupancy_gpu_field = &*gpu_field;
            }
        }
        std::vector<scene::EntityReference>& pick_entities = this->renderer.pick_entities[frame_slot_index];
        pick_entities.clear();
        const auto pick_index = [&pick_entities](const scene::EntityReference entity) {
            const auto found = std::ranges::find(pick_entities, entity);
            if (found != pick_entities.end()) return static_cast<std::uint32_t>(found - pick_entities.begin()) + 1u;
            pick_entities.push_back(entity);
            return static_cast<std::uint32_t>(pick_entities.size());
        };
        const auto add_line   = [&](const math::Float3 first, const math::Float3 second, const math::Float4 color, const std::optional<scene::EntityReference> entity = std::nullopt, const float width = 1.5f, const scene::VisualizationDepthMode depth_mode = scene::VisualizationDepthMode::Tested) { lines.emplace_back(shader_value(first), width, shader_value(second), shader_depth_mode(depth_mode), shader_value(color), entity ? pick_index(*entity) : 0u); };
        const auto add_circle = [&](const math::Float3 center, const math::Float3 first_axis, const math::Float3 second_axis, const float radius, const math::Float4 color, const scene::EntityReference entity) {
            constexpr std::uint32_t segments = 32;
            for (std::uint32_t index = 0; index != segments; ++index) {
                const float first_angle  = static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
                const float second_angle = static_cast<float>(index + 1u) * 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
                add_line(center + (first_axis * std::cos(first_angle) + second_axis * std::sin(first_angle)) * radius, center + (first_axis * std::cos(second_angle) + second_axis * std::sin(second_angle)) * radius, color, entity);
            }
        };
        const auto selected_instance = [&selection](const scene::InstanceId id) {
            return std::ranges::any_of(selection.selected, [id](const scene::EntityReference entity) {
                if (const scene::InstanceId* instance = std::get_if<scene::InstanceId>(&entity.data)) return *instance == id;
                if (const scene::EntityReference::AreaEmitter* emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity.data)) return emitter->instance == id;
                return false;
            });
        };
        std::vector<bool> instance_has_geometry(scene_view.resources.instances.size());
        for (const GpuScenePrimitive& primitive : this->context.gpu_scene.view().primitives) {
            const std::uint32_t count = primitive.kind == GpuScenePrimitiveKind::Geometry ? this->context.gpu_scene.view().geometries[primitive.resource_index].vertex_count : this->context.gpu_scene.view().sphere_sets[primitive.resource_index].sphere_count;
            if (count != 0) instance_has_geometry[primitive.scene_instance_index] = true;
        }

        math::Bounds3 diagnostic_bounds = scene_view.bounds();
        diagnostic_bounds.include(this->context.gpu_scene.view().resolved_scene_bounds);
        const float bounds_radius     = diagnostic_bounds.radius();
        const float scene_radius      = std::max(bounds_radius, 1.0f);
        const float camera_guide_size = bounds_radius * 0.1f;
        if (scene_guides.all_bounds || selection_diagnostics.bounds)
            for (std::uint32_t index = 0; index != scene_view.resources.instances.size(); ++index) {
                const scene::Instance& instance = scene_view.resources.instances[index];
                const bool selected             = selected_instance(instance.id);
                if (!instance.visible || !instance_has_geometry[index] || (!scene_guides.all_bounds && !(selection_diagnostics.bounds && selected))) continue;
                const scene::EntityReference entity{instance.id};
                boxes.push_back({math::Transform{}.matrix, {0.0f, 0.0f, 0.0f, selected ? selection_diagnostics.line_width : 1.5f}, {}, shader_value(diagnostic_color(entity, selection, {0.24f, 0.76f, 1.0f, 0.82f})), {index, 1u, pick_index(entity), shader_depth_mode(selected ? selection_diagnostics.depth_mode : scene::VisualizationDepthMode::Tested)}});
            }

        for (const scene::Volume& volume : scene_view.resources.volumes) {
            if (!volume.visible) continue;
            const scene::EntityReference entity{volume.id};
            const bool selected = std::ranges::contains(selection.selected, entity);
            if (scene_guides.all_bounds || (selection_diagnostics.bounds && selected))
                boxes.push_back({
                    volume.transform.matrix,
                    {volume.domain.minimum.x, volume.domain.minimum.y, volume.domain.minimum.z, selected ? selection_diagnostics.line_width : 1.5f},
                    {volume.domain.maximum.x, volume.domain.maximum.y, volume.domain.maximum.z, 0.0f},
                    shader_value(diagnostic_color(entity, selection, {0.64f, 0.32f, 0.92f, 0.72f})),
                    {0, 0, pick_index(entity), shader_depth_mode(selected ? selection_diagnostics.depth_mode : scene::VisualizationDepthMode::Tested)},
                });
        }

        for (const scene::ParticleSet& particles : scene_view.resources.particle_sets) {
            if (!particles.visible) continue;
            const scene::EntityReference entity{particles.id};
            const bool selected = std::ranges::contains(selection.selected, entity);
            if (scene_guides.all_bounds || (selection_diagnostics.bounds && selected))
                boxes.push_back({
                    particles.transform.matrix,
                    {particles.domain.minimum.x, particles.domain.minimum.y, particles.domain.minimum.z, selected ? selection_diagnostics.line_width : 1.5f},
                    {particles.domain.maximum.x, particles.domain.maximum.y, particles.domain.maximum.z, 0.0f},
                    shader_value(diagnostic_color(entity, selection, {0.12f, 0.66f, 1.0f, 0.76f})),
                    {0, 0, pick_index(entity), shader_depth_mode(selected ? selection_diagnostics.depth_mode : scene::VisualizationDepthMode::Tested)},
                });
        }

        for (const scene::NeuralField& field : scene_view.resources.neural_fields) {
            if (!field.visible) continue;
            const scene::EntityReference entity{field.id};
            const bool selected = std::ranges::contains(selection.selected, entity);
            if (scene_guides.all_bounds || (selection_diagnostics.bounds && selected))
                boxes.push_back({
                    field.transform.matrix,
                    {scene::NeuralField::local_bounds.minimum.x, scene::NeuralField::local_bounds.minimum.y, scene::NeuralField::local_bounds.minimum.z, selected ? selection_diagnostics.line_width : 1.5f},
                    {scene::NeuralField::local_bounds.maximum.x, scene::NeuralField::local_bounds.maximum.y, scene::NeuralField::local_bounds.maximum.z, 0.0f},
                    shader_value(diagnostic_color(entity, selection, {0.20f, 0.86f, 0.72f, 0.76f})),
                    {0, 0, pick_index(entity), shader_depth_mode(selected ? selection_diagnostics.depth_mode : scene::VisualizationDepthMode::Tested)},
                });
        }

        for (const scene::Camera& scene_camera : scene_view.resources.cameras) {
            const bool selected_camera = selection.active && selection.active->data == scene::EntityReference{scene_camera.id}.data;
            if (!scene_guides.cameras && !(selected_camera && selection_diagnostics.camera_frustum)) continue;
            if (scene_camera_view && scene_camera.id == *scene_camera_view) continue;
            const scene::EntityReference entity{scene_camera.id};
            const math::Float4 color                       = diagnostic_color(entity, selection, scene_camera.id == scene_view.camera.id ? math::Float4{1.0f, 0.66f, 0.12f, 0.95f} : math::Float4{0.20f, 0.80f, 0.95f, 0.82f});
            const scene::CameraFrame frame                 = scene_camera.frame();
            const float line_width                         = selected_camera ? selection_diagnostics.line_width : 1.5f;
            const scene::VisualizationDepthMode depth_mode = selected_camera ? selection_diagnostics.depth_mode : scene::VisualizationDepthMode::Tested;
            std::array<math::Float3, 4> guide_corners{};
            std::visit(
                [&](const auto& data) {
                    constexpr std::array signs{math::Float2{-1.0f, -1.0f}, math::Float2{1.0f, -1.0f}, math::Float2{1.0f, 1.0f}, math::Float2{-1.0f, 1.0f}};
                    const float window_width  = data.screen_window.maximum.x - data.screen_window.minimum.x;
                    const float window_height = data.screen_window.maximum.y - data.screen_window.minimum.y;
                    const float guide_scale   = camera_guide_size / std::max(window_width, window_height);
                    float guide_distance      = camera_guide_size;
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) guide_distance = guide_scale / std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                    for (std::size_t index = 0; index != signs.size(); ++index) {
                        const math::Float2 window{
                            signs[index].x < 0.0f ? data.screen_window.minimum.x : data.screen_window.maximum.x,
                            signs[index].y < 0.0f ? data.screen_window.minimum.y : data.screen_window.maximum.y,
                        };
                        guide_corners[index] = frame.position + frame.forward * guide_distance + (frame.right * window.x + frame.up * window.y) * guide_scale;
                    }
                    if (selected_camera && selection_diagnostics.camera_focal_plane) {
                        std::array<math::Float3, 4> focal{};
                        for (std::size_t index = 0; index != signs.size(); ++index) {
                            const math::Float2 window{signs[index].x < 0.0f ? data.screen_window.minimum.x : data.screen_window.maximum.x, signs[index].y < 0.0f ? data.screen_window.minimum.y : data.screen_window.maximum.y};
                            float scale{1.0f};
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) scale = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) * data.focal_distance;
                            focal[index] = frame.position + frame.forward * data.focal_distance + (frame.right * window.x + frame.up * window.y) * scale;
                        }
                        for (std::size_t index = 0; index != focal.size(); ++index) add_line(focal[index], focal[(index + 1) % focal.size()], {0.96f, 0.34f, 0.72f, 0.72f}, entity);
                    }
                    if (selected_camera && selection_diagnostics.camera_lens && data.lens_radius > 0.0f) add_circle(frame.position, frame.right, frame.up, data.lens_radius, {0.96f, 0.34f, 0.72f, 0.82f}, entity);
                },
                scene_camera.data);
            for (std::size_t index = 0; index != guide_corners.size(); ++index) {
                add_line(guide_corners[index], guide_corners[(index + 1) % guide_corners.size()], color, entity, line_width, depth_mode);
                add_line(frame.position, guide_corners[index], color, entity, line_width, depth_mode);
            }
        }

        for (const scene::Light& light : scene_view.resources.lights) {
            if (std::holds_alternative<scene::DiffuseAreaLight>(light.data)) continue;
            const bool selected_light = selection.active && selection.active->data == scene::EntityReference{light.id}.data;
            if (!scene_guides.lights && !(selected_light && selection_diagnostics.light_guide)) continue;
            const scene::EntityReference entity{light.id};
            const math::Float4 color        = diagnostic_color(entity, selection, {1.0f, 0.82f, 0.25f, 0.88f});
            const math::Transform transform = light_transform(light);
            const math::Float3 position{transform.matrix[3], transform.matrix[7], transform.matrix[11]};
            const math::Float3 right   = transform.transform_vector({1.0f, 0.0f, 0.0f}).normalized();
            const math::Float3 up      = transform.transform_vector({0.0f, 1.0f, 0.0f}).normalized();
            const math::Float3 forward = -transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
            const float icon           = scene_radius * 0.035f;
            std::visit(
                [&](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight>) {
                        add_circle(position, right, up, icon, color, entity);
                        add_circle(position, right, forward, icon, color, entity);
                        add_circle(position, up, forward, icon, color, entity);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) {
                        const float length = scene_radius * 0.18f;
                        for (const float angle : {data.cone_angle - data.cone_delta, data.cone_angle}) {
                            const float radius        = std::tan(angle * std::numbers::pi_v<float> / 180.0f) * length;
                            const math::Float3 center = position + forward * length;
                            add_circle(center, right, up, radius, angle == data.cone_angle ? color : math::Float4{0.98f, 0.48f, 0.20f, 0.78f}, entity);
                            add_line(position, center + right * radius, color, entity);
                            add_line(position, center - right * radius, color, entity);
                            add_line(position, center + up * radius, color, entity);
                            add_line(position, center - up * radius, color, entity);
                        }
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DistantLight>) {
                        add_circle(position, right, up, icon, color, entity);
                        const math::Float3 end = position + forward * icon * 3.5f;
                        add_line(position, end, color, entity, 1.875f);
                        add_line(end, end - forward * icon + right * icon * 0.5f, color, entity);
                        add_line(end, end - forward * icon - right * icon * 0.5f, color, entity);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InfiniteLight>) {
                        add_circle(position, right, up, icon * 1.5f, color, entity);
                        add_circle(position, right, forward, icon * 1.5f, color, entity);
                        add_line(position, position + forward * icon * 3.0f, color, entity, 1.875f);
                    } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) {
                        add_circle(position, right, up, icon * 1.5f, color, entity);
                        add_circle(position, right, forward, icon * 1.5f, color, entity);
                        add_line(position, position + forward * icon * 3.0f, color, entity, 1.875f);
                        for (const std::array<math::Float3, 4>& portal : data.portals) {
                            for (std::size_t index = 0; index != portal.size(); ++index) add_line(portal[index], portal[(index + 1) % portal.size()], {0.28f, 0.95f, 0.78f, 0.92f}, entity, 2.1f);
                            const math::Float3 portal_center = (portal[0] + portal[1] + portal[2] + portal[3]) / 4.0f;
                            const math::Float3 normal        = (portal[1] - portal[0]).cross(portal[3] - portal[0]).normalized();
                            add_line(portal_center, portal_center + normal * scene_radius * 0.08f, {0.28f, 0.95f, 0.78f, 0.92f}, entity);
                        }
                    }
                },
                light.data);
        }

        this->ensure_buffers(frame_resources, lines.size(), boxes.size());
        if (!lines.empty()) std::memcpy(frame_resources.line_buffer.mapped, lines.data(), lines.size() * sizeof(DiagnosticLine));
        if (!boxes.empty()) std::memcpy(frame_resources.box_buffer.mapped, boxes.data(), boxes.size() * sizeof(DiagnosticBox));
        if (occupancy_gpu_field) {
            this->ensure_occupancy_buffers(frame_resources);
            command_buffer.fillBuffer(*frame_resources.occupancy_draw_buffer.buffer, 0, frame_resources.occupancy_draw_buffer.size, 0u);
            const vk::MemoryBarrier2 reset_dependency{vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &reset_dependency});
            const DiagnosticPushData compaction_push{occupancy_gpu_field->occupancy.descriptor, frame_resources.occupied_cell_descriptor, frame_resources.occupancy_draw_descriptor};
            this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&compaction_push, 1}));
            command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.occupancy_compaction_shader);
            command_buffer.dispatch(256, 1, 1);
            const vk::MemoryBarrier2 compaction_dependency{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eDrawIndirect | vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eIndirectCommandRead | vk::AccessFlagBits2::eShaderStorageRead};
            command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &compaction_dependency});
        }
        this->resize_pick_image(target.image.extent);
        const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead};
        std::vector<vk::ImageMemoryBarrier2> image_barriers{};
        image_barriers.emplace_back(vk::ImageMemoryBarrier2{target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput, target.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, target.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        if (depth.layout != vk::ImageLayout::eShaderReadOnlyOptimal) image_barriers.emplace_back(vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, depth.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *depth.image.image, {depth.image.aspect, 0, 1, 0, 1}});
        image_barriers.emplace_back(vk::ImageMemoryBarrier2{this->renderer.pick_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eCopy, this->renderer.pick_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, this->renderer.pick_layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.pick_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, 0, nullptr, static_cast<std::uint32_t>(image_barriers.size()), image_barriers.data()});
        depth.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        const std::array color_attachments{
            vk::RenderingAttachmentInfo{*target.image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore},
            vk::RenderingAttachmentInfo{*this->renderer.pick_image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearValue{vk::ClearColorValue{std::array{0u, 0u, 0u, 0u}}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{{}, {{0, 0}, target.image.extent}, 1, 0, static_cast<std::uint32_t>(color_attachments.size()), color_attachments.data()});
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
        const std::array<vk::Bool32, 2> blend_enable{vk::True, vk::False};
        command_buffer.setColorBlendEnableEXT(0, blend_enable);
        const std::array blend_equations{
            vk::ColorBlendEquationEXT{vk::BlendFactor::eSrcAlpha, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd, vk::BlendFactor::eOne, vk::BlendFactor::eOneMinusSrcAlpha, vk::BlendOp::eAdd},
            vk::ColorBlendEquationEXT{},
        };
        command_buffer.setColorBlendEquationEXT(0, blend_equations);
        constexpr vk::ColorComponentFlags color_components = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const std::array color_masks{color_components, color_components};
        command_buffer.setColorWriteMaskEXT(0, color_masks);
        const std::array stages{vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array handles{vk::ShaderEXT{}, *this->renderer.draw_shaders[0], *this->renderer.draw_shaders[1]};
        command_buffer.bindShadersEXT(stages, handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        const std::array<float, 16>& view_projection = camera.matrices().view_projection;
        const auto push_and_draw                     = [&](const std::uint32_t kind, const runtime::DescriptorHandle primary, const runtime::DescriptorHandle secondary, const std::uint32_t count, const std::uint32_t pick, const scene::VisualizationDepthMode depth_mode, const math::Float4 color, const math::Transform transform, const std::uint32_t vertex_count, const std::uint32_t gpu_transform_index = std::numeric_limits<std::uint32_t>::max()) {
            const std::array<float, 16>& matrix = transform.matrix;
            const DiagnosticPushData push{
                primary,
                secondary,
                gpu_transform_index == std::numeric_limits<std::uint32_t>::max() ? this->context.gpu_scene.view().instance_bounds : this->context.gpu_scene.view().primitive_transforms,
                depth.descriptor,
                                    {kind, count, pick, shader_depth_mode(depth_mode)},
                                    {target.image.extent.width, target.image.extent.height, selection_diagnostics.attribute_sampling, gpu_transform_index},
                                    {selection_diagnostics.line_width, selection_diagnostics.point_size, selection_diagnostics.vector_scale, static_cast<float>(selection_diagnostics.attribute_sampling)},
                                    {color.x, color.y, color.z, color.w},
                                    {matrix[0], matrix[1], matrix[2], matrix[3]},
                                    {matrix[4], matrix[5], matrix[6], matrix[7]},
                                    {matrix[8], matrix[9], matrix[10], matrix[11]},
                                    {matrix[12], matrix[13], matrix[14], matrix[15]},
                                    {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                                    {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                                    {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                                    {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.draw(vertex_count, kind == shader_semantics::diagnostic_lines || kind == shader_semantics::diagnostic_boxes || kind == shader_semantics::diagnostic_sphere_wireframe ? count : 1u, 0, 0);
        };
        if (occupancy_gpu_field) {
            const std::array<float, 16>& matrix = occupancy_field->transform.matrix;
            const DiagnosticPushData push{
                frame_resources.occupied_cell_descriptor,
                frame_resources.occupied_cell_descriptor,
                frame_resources.occupied_cell_descriptor,
                depth.descriptor,
                {shader_semantics::diagnostic_occupancy_cells, 0u, 0u, shader_semantics::depth_xray},
                {target.image.extent.width, target.image.extent.height, 1u, std::numeric_limits<std::uint32_t>::max()},
                {0.5f, 0.0f, 0.0f, 0.0f},
                {0.08f, 0.82f, 0.96f, 1.0f},
                {matrix[0], matrix[1], matrix[2], matrix[3]},
                {matrix[4], matrix[5], matrix[6], matrix[7]},
                {matrix[8], matrix[9], matrix[10], matrix[11]},
                {matrix[12], matrix[13], matrix[14], matrix[15]},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            constexpr std::array<vk::Bool32, 1> disabled_blending{vk::False};
            command_buffer.setColorBlendEnableEXT(0, disabled_blending);
            command_buffer.drawIndirect(*frame_resources.occupancy_draw_buffer.buffer, 0, 1, sizeof(vk::DrawIndirectCommand));
            constexpr std::array<vk::Bool32, 1> enabled_blending{vk::True};
            command_buffer.setColorBlendEnableEXT(0, enabled_blending);
        }
        if (!lines.empty()) push_and_draw(shader_semantics::diagnostic_lines, frame_resources.line_descriptor, frame_resources.line_descriptor, static_cast<std::uint32_t>(lines.size()), 0, selection_diagnostics.depth_mode, {}, {}, 6);
        if (!boxes.empty()) push_and_draw(shader_semantics::diagnostic_boxes, frame_resources.box_descriptor, this->context.gpu_scene.view().instance_bounds, static_cast<std::uint32_t>(boxes.size()), 0, selection_diagnostics.depth_mode, {}, {}, 72);

        for (const scene::ParticleSet& particles : scene_view.resources.particle_sets) {
            if (!particles.visible) continue;
            const GpuParticleSet& gpu_particles = *std::ranges::find(this->context.gpu_scene.view().particle_sets, particles.id, &GpuParticleSet::particle_set_id);
            if (gpu_particles.count == 0u) continue;
            const scene::EntityReference entity{particles.id};
            push_and_draw(shader_semantics::diagnostic_particle_points, gpu_particles.positions, gpu_particles.positions, gpu_particles.count, pick_index(entity), scene::VisualizationDepthMode::Tested, {0.0f, 0.0f, 0.0f, 0.0f}, particles.transform, gpu_particles.count * 6u);
            if (!request.visible || !selection.active || *selection.active != entity || particles.diagnostics.vector_field.empty()) continue;
            const GpuParticleField& field                 = *std::ranges::find(gpu_particles.fields, particles.diagnostics.vector_field, &GpuParticleField::id);
            const std::uint32_t sampling                  = particles.diagnostics.sampling;
            const std::uint32_t arrow_count               = (gpu_particles.count + sampling - 1u) / sampling;
            const std::array<float, 16>& matrix           = particles.transform.matrix;
            const scene::ParticleDiagnostics& diagnostics = particles.diagnostics;
            const DiagnosticPushData push{
                gpu_particles.positions,
                field.descriptor,
                this->context.gpu_scene.view().instance_bounds,
                depth.descriptor,
                {shader_semantics::diagnostic_particle_vectors, shader_vector_space(field.vector_space), pick_index(entity), shader_depth_mode(selection_diagnostics.depth_mode)},
                {target.image.extent.width, target.image.extent.height, sampling, std::numeric_limits<std::uint32_t>::max()},
                {diagnostics.width, 0.0f, diagnostics.scale, diagnostics.minimum},
                {0.0f, 0.0f, static_cast<float>(shader_color_map(diagnostics.color_map)), diagnostics.maximum},
                {matrix[0], matrix[1], matrix[2], matrix[3]},
                {matrix[4], matrix[5], matrix[6], matrix[7]},
                {matrix[8], matrix[9], matrix[10], matrix[11]},
                {matrix[12], matrix[13], matrix[14], matrix[15]},
                {view_projection[0], view_projection[1], view_projection[2], view_projection[3]},
                {view_projection[4], view_projection[5], view_projection[6], view_projection[7]},
                {view_projection[8], view_projection[9], view_projection[10], view_projection[11]},
                {view_projection[12], view_projection[13], view_projection[14], view_projection[15]},
            };
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.draw(arrow_count * 18u, 1u, 0u, 0u);
        }

        const bool inspect_instance                = selection.active && std::holds_alternative<scene::InstanceId>(selection.active->data);
        const bool inspect_area                    = selection.active && std::holds_alternative<scene::EntityReference::AreaEmitter>(selection.active->data);
        const scene::InstanceId inspected_instance = inspect_instance ? std::get<scene::InstanceId>(selection.active->data) : inspect_area ? std::get<scene::EntityReference::AreaEmitter>(selection.active->data).instance : scene::InstanceId{};
        const std::uint32_t inspected_primitive    = inspect_area ? std::get<scene::EntityReference::AreaEmitter>(selection.active->data).primitive_index : 0u;
        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
            const scene::Instance& instance = scene_view.resources.instances[gpu_primitive.scene_instance_index];
            if ((!inspect_instance && !inspect_area) || instance.id != inspected_instance) continue;
            const scene::Prototype& prototype = *std::ranges::find(scene_view.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
            const scene::EntityReference instance_entity{instance.id};
            const math::Transform transform = instance.transform * primitive.transform;
            if (gpu_primitive.kind == GpuScenePrimitiveKind::Geometry) {
                const GpuGeometry& geometry       = this->context.gpu_scene.view().geometries[gpu_primitive.resource_index];
                const std::uint32_t instance_pick = pick_index(instance_entity);
                if (inspect_instance && selection_diagnostics.wireframe) push_and_draw(shader_semantics::diagnostic_triangle_edges, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, instance_pick, selection_diagnostics.depth_mode, diagnostic_color(instance_entity, selection, {0.18f, 0.76f, 1.0f, 0.56f}), transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
                if (inspect_instance && selection_diagnostics.vertices) push_and_draw(shader_semantics::diagnostic_mesh_vertices, geometry.positions_descriptor, geometry.positions_descriptor, geometry.vertex_count, instance_pick, selection_diagnostics.depth_mode, diagnostic_color(instance_entity, selection, {0.94f, 0.94f, 0.98f, 0.90f}), transform, geometry.vertex_count * 6u, gpu_primitive.scene_primitive_index);
                if (inspect_instance && selection_diagnostics.normals && (geometry.attribute_mask & gpu_geometry_attribute_normal) != 0) push_and_draw(shader_semantics::diagnostic_mesh_normals, geometry.positions_descriptor, geometry.normals_descriptor, geometry.vertex_count, instance_pick, selection_diagnostics.depth_mode, {0.24f, 0.86f, 0.48f, 0.84f}, transform, ((geometry.vertex_count + selection_diagnostics.attribute_sampling - 1u) / selection_diagnostics.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                if (inspect_instance && selection_diagnostics.tangents && (geometry.attribute_mask & gpu_geometry_attribute_tangent) != 0) push_and_draw(shader_semantics::diagnostic_mesh_tangents, geometry.positions_descriptor, geometry.tangents_descriptor, geometry.vertex_count, instance_pick, selection_diagnostics.depth_mode, {0.94f, 0.38f, 0.74f, 0.84f}, transform, ((geometry.vertex_count + selection_diagnostics.attribute_sampling - 1u) / selection_diagnostics.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                if (selection_diagnostics.area_emitter && primitive.area_light.value != 0 && (!inspect_area || inspected_primitive == gpu_primitive.prototype_primitive_index)) {
                    const scene::EntityReference area{scene::EntityReference::AreaEmitter{primitive.area_light, instance.id, gpu_primitive.prototype_primitive_index}};
                    push_and_draw(shader_semantics::diagnostic_triangle_edges, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, pick_index(area), selection_diagnostics.depth_mode, diagnostic_color(area, selection, {1.0f, 0.48f, 0.10f, 0.94f}), transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
                    if ((geometry.attribute_mask & gpu_geometry_attribute_normal) != 0) push_and_draw(shader_semantics::diagnostic_mesh_normals, geometry.positions_descriptor, geometry.normals_descriptor, geometry.vertex_count, pick_index(area), selection_diagnostics.depth_mode, {1.0f, 0.48f, 0.10f, 0.72f}, transform, ((geometry.vertex_count + selection_diagnostics.attribute_sampling - 1u) / selection_diagnostics.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                }
                if (inspect_instance && selection_diagnostics.medium_boundary && (primitive.media.inside.value != 0 || primitive.media.outside.value != 0)) push_and_draw(shader_semantics::diagnostic_triangle_edges, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, instance_pick, selection_diagnostics.depth_mode, {0.18f, 0.92f, 0.86f, 0.72f}, transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
            } else {
                const GpuSphereSet& spheres       = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                const std::uint32_t instance_pick = pick_index(instance_entity);
                if (inspect_instance && selection_diagnostics.wireframe) push_and_draw(shader_semantics::diagnostic_sphere_wireframe, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, selection_diagnostics.depth_mode, diagnostic_color(instance_entity, selection, {0.18f, 0.76f, 1.0f, 0.56f}), transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
                if (inspect_instance && selection_diagnostics.vertices) push_and_draw(shader_semantics::diagnostic_sphere_centers, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, selection_diagnostics.depth_mode, diagnostic_color(instance_entity, selection, {0.94f, 0.94f, 0.98f, 0.90f}), transform, spheres.sphere_count * 6u, gpu_primitive.scene_primitive_index);
                if (selection_diagnostics.area_emitter && primitive.area_light.value != 0 && (!inspect_area || inspected_primitive == gpu_primitive.prototype_primitive_index)) {
                    const scene::EntityReference area{scene::EntityReference::AreaEmitter{primitive.area_light, instance.id, gpu_primitive.prototype_primitive_index}};
                    push_and_draw(shader_semantics::diagnostic_sphere_wireframe, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, pick_index(area), selection_diagnostics.depth_mode, diagnostic_color(area, selection, {1.0f, 0.48f, 0.10f, 0.94f}), transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
                }
                if (inspect_instance && selection_diagnostics.medium_boundary && (primitive.media.inside.value != 0 || primitive.media.outside.value != 0)) push_and_draw(shader_semantics::diagnostic_sphere_wireframe, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, selection_diagnostics.depth_mode, {0.18f, 0.92f, 0.86f, 0.72f}, transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
            }
        }
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *target.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.pick_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(end_barriers.size()), end_barriers.data()});
        target.layout              = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->renderer.pick_layout = vk::ImageLayout::eTransferSrcOptimal;
    }

    const runtime::GpuImage& DiagnosticsPass::pick_image() const noexcept {
        return this->renderer.pick_image;
    }

    std::optional<scene::EntityReference> DiagnosticsPass::pick_entity(const std::uint32_t frame_slot_index, const std::uint32_t pick_index) const noexcept {
        if (pick_index == 0 || pick_index > this->renderer.pick_entities[frame_slot_index].size()) return std::nullopt;
        return this->renderer.pick_entities[frame_slot_index][pick_index - 1u];
    }

    void DiagnosticsPass::ensure_buffers(SceneDiagnosticFrameResources& frame, const std::size_t line_count, const std::size_t box_count) {
        const auto ensure = [this](runtime::GpuBuffer& buffer, std::size_t& capacity, runtime::DescriptorLease& descriptor, const std::size_t count, const std::size_t stride) {
            if (capacity >= std::max<std::size_t>(count, 1)) return;
            const std::size_t next_capacity          = std::bit_ceil(std::max<std::size_t>(count, 1));
            runtime::GpuBuffer replacement           = this->context.runtime.resources.create_buffer(next_capacity * stride, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            runtime::DescriptorLease next_descriptor = this->context.runtime.frames.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(next_descriptor, vk::DescriptorType::eStorageBuffer, replacement);
            if (*buffer.buffer) this->context.runtime.frames.defer_destruction([previous = std::move(buffer)]() mutable {});
            buffer     = std::move(replacement);
            descriptor = std::move(next_descriptor);
            capacity   = next_capacity;
        };
        ensure(frame.line_buffer, frame.line_capacity, frame.line_descriptor, line_count, sizeof(DiagnosticLine));
        ensure(frame.box_buffer, frame.box_capacity, frame.box_descriptor, box_count, sizeof(DiagnosticBox));
    }

    void DiagnosticsPass::ensure_occupancy_buffers(SceneDiagnosticFrameResources& frame) {
        if (*frame.occupied_cell_buffer.buffer) return;
        constexpr vk::DeviceSize cell_count = static_cast<vk::DeviceSize>(sdk::neural_field_layout::occupancy_resolution) * sdk::neural_field_layout::occupancy_resolution * sdk::neural_field_layout::occupancy_resolution;
        frame.occupied_cell_buffer          = this->context.runtime.resources.create_buffer(cell_count * sizeof(std::uint32_t), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        frame.occupancy_draw_buffer         = this->context.runtime.resources.create_buffer(sizeof(vk::DrawIndirectCommand), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eIndirectBuffer | vk::BufferUsageFlagBits::eTransferDst, vk::MemoryPropertyFlagBits::eDeviceLocal, false);
        frame.occupied_cell_descriptor      = this->context.runtime.frames.allocate_resource_descriptor();
        frame.occupancy_draw_descriptor     = this->context.runtime.frames.allocate_resource_descriptor();
        this->context.runtime.resources.write_buffer_descriptor(frame.occupied_cell_descriptor, vk::DescriptorType::eStorageBuffer, frame.occupied_cell_buffer);
        this->context.runtime.resources.write_buffer_descriptor(frame.occupancy_draw_descriptor, vk::DescriptorType::eStorageBuffer, frame.occupancy_draw_buffer);
    }

    void DiagnosticsPass::resize_pick_image(const vk::Extent2D extent) {
        if (*this->renderer.pick_image.image && this->renderer.pick_image.extent == extent) return;
        runtime::GpuImage next = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
        if (*this->renderer.pick_image.image) this->context.runtime.frames.defer_destruction([previous = std::move(this->renderer.pick_image)]() mutable {});
        this->renderer.pick_image  = std::move(next);
        this->renderer.pick_layout = vk::ImageLayout::eUndefined;
    }

} // namespace spectra::render
