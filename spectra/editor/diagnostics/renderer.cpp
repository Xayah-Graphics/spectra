module spectra.diagnostics.renderer;

import spectra.runtime.shaders;

import std;
import vulkan;

namespace spectra {
    namespace {
        struct alignas(16) DiagnosticLine {
            math::Float3 first{};
            float width{};
            math::Float3 second{};
            std::uint32_t depth_mode{};
            math::Float4 color{};
            std::uint32_t pick_index{};
            std::array<std::uint32_t, 3> reserved{};
        };
        static_assert(sizeof(DiagnosticLine) == 64);

        struct alignas(16) DiagnosticBox {
            std::array<float, 16> transform{};
            math::Float4 minimum{};
            math::Float4 maximum{};
            math::Float4 color{};
            std::array<std::uint32_t, 4> metadata{};
        };
        static_assert(sizeof(DiagnosticBox) == 128);

        struct alignas(16) DiagnosticPushData {
            DescriptorHandle primary{};
            DescriptorHandle secondary{};
            DescriptorHandle tertiary{};
            DescriptorHandle depth{};
            std::array<std::uint32_t, 4> metadata{};
            std::array<std::uint32_t, 4> detail{};
            std::array<float, 4> parameters{};
            std::array<float, 4> color{};
            std::array<float, 4> transform_row_0{};
            std::array<float, 4> transform_row_1{};
            std::array<float, 4> transform_row_2{};
            std::array<float, 4> transform_row_3{};
            std::array<float, 4> view_projection_row_0{};
            std::array<float, 4> view_projection_row_1{};
            std::array<float, 4> view_projection_row_2{};
            std::array<float, 4> view_projection_row_3{};
        };
        static_assert(sizeof(DiagnosticPushData) == 224);

        [[nodiscard]] math::Float4 diagnostic_color(const SceneEntityReference entity, const SelectionState& selection, const math::Float4 base) noexcept {
            if (selection.active == entity) return {1.0f, 0.55f, 0.08f, 1.0f};
            if (std::ranges::contains(selection.selected, entity)) return {0.10f, 0.58f, 1.0f, 1.0f};
            if (selection.hovered == entity) return {0.45f, 1.0f, 0.28f, 1.0f};
            return base;
        }

        [[nodiscard]] math::Transform light_transform(const scene::Light& light) noexcept {
            return std::visit(
                [](const auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                        return data.environment.transform;
                    else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                        return math::Transform{};
                    else
                        return data.transform;
                },
                light.data);
        }
    } // namespace

    SceneDiagnosticRenderer::SceneDiagnosticRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory) : context{runtime, gpu_scene, std::move(shader_directory)} {}

    SceneDiagnosticRenderer::~SceneDiagnosticRenderer() {
        if (!this->renderer.initialized) return;
        this->context.runtime.frames.defer_destruction([
            frames = std::move(this->renderer.frame_resources),
            draw_shaders = std::move(this->renderer.draw_shaders),
            clear_shader = std::move(this->renderer.clear_bounds_shader),
            accumulate_shader = std::move(this->renderer.accumulate_bounds_shader),
            pick_image = std::move(this->renderer.pick_image)
        ]() mutable {});
    }

    void SceneDiagnosticRenderer::initialize() {
        const std::vector<std::uint32_t> vertex_code     = load_spirv(this->context.shader_directory / "scene_diagnostic_vertex.spv");
        const std::vector<std::uint32_t> fragment_code   = load_spirv(this->context.shader_directory / "scene_diagnostic_fragment.spv");
        const std::vector<std::uint32_t> clear_code      = load_spirv(this->context.shader_directory / "scene_diagnostic_bounds_clear.spv");
        const std::vector<std::uint32_t> accumulate_code = load_spirv(this->context.shader_directory / "scene_diagnostic_bounds_accumulate.spv");
        const std::array draw_create_infos{
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment, vk::ShaderCodeTypeEXT::eSpirv, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data(), "scene_diagnostic_vertex"},
            vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eLinkStage | vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eFragment, {}, vk::ShaderCodeTypeEXT::eSpirv, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data(), "scene_diagnostic_fragment"},
        };
        this->renderer.draw_shaders = vk::raii::ShaderEXTs{this->context.runtime.graphics.device, draw_create_infos};
        this->renderer.clear_bounds_shader = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, clear_code.size() * sizeof(std::uint32_t), clear_code.data(), "scene_diagnostic_bounds_clear"}};
        this->renderer.accumulate_bounds_shader = vk::raii::ShaderEXT{this->context.runtime.graphics.device, vk::ShaderCreateInfoEXT{vk::ShaderCreateFlagBitsEXT::eDescriptorHeap, vk::ShaderStageFlagBits::eCompute, {}, vk::ShaderCodeTypeEXT::eSpirv, accumulate_code.size() * sizeof(std::uint32_t), accumulate_code.data(), "scene_diagnostic_bounds_accumulate"}};
        for (SceneDiagnosticFrameResources& frame : this->renderer.frame_resources) {
            frame.line_descriptor   = this->context.runtime.resources.allocate_resource_descriptor();
            frame.box_descriptor    = this->context.runtime.resources.allocate_resource_descriptor();
            frame.bounds_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
        }
        this->renderer.initialized       = true;
    }

    void SceneDiagnosticRenderer::ensure_buffers(SceneDiagnosticFrameResources& frame, const std::size_t line_count, const std::size_t box_count, const std::size_t bounds_count) {
        const auto ensure = [this](GpuBuffer& buffer, std::size_t& capacity, DescriptorLease& descriptor, const std::size_t count, const std::size_t stride) {
            if (capacity >= std::max<std::size_t>(count, 1)) return;
            const std::size_t next_capacity = std::bit_ceil(std::max<std::size_t>(count, 1));
            GpuBuffer replacement = this->context.runtime.resources.create_buffer(next_capacity * stride, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
            DescriptorLease next_descriptor = this->context.runtime.resources.allocate_resource_descriptor();
            this->context.runtime.resources.write_buffer_descriptor(next_descriptor, vk::DescriptorType::eStorageBuffer, replacement);
            if (*buffer.buffer) this->context.runtime.frames.defer_destruction([previous = std::move(buffer)]() mutable {});
            buffer     = std::move(replacement);
            descriptor = std::move(next_descriptor);
            capacity   = next_capacity;
        };
        ensure(frame.line_buffer, frame.line_capacity, frame.line_descriptor, line_count, sizeof(DiagnosticLine));
        ensure(frame.box_buffer, frame.box_capacity, frame.box_descriptor, box_count, sizeof(DiagnosticBox));
        ensure(frame.bounds_buffer, frame.bounds_capacity, frame.bounds_descriptor, bounds_count, 32);
    }

    void SceneDiagnosticRenderer::resize_pick_image(const vk::Extent2D extent) {
        if (*this->renderer.pick_image.image && this->renderer.pick_image.extent == extent) return;
        GpuImage next = this->context.runtime.resources.create_image_2d(extent, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc);
        if (*this->renderer.pick_image.image) this->context.runtime.frames.defer_destruction([previous = std::move(this->renderer.pick_image)]() mutable {});
        this->renderer.pick_image = std::move(next);
        this->renderer.pick_layout = vk::ImageLayout::eUndefined;
    }

    void SceneDiagnosticRenderer::record_bounds(const vk::raii::CommandBuffer& command_buffer, SceneDiagnosticFrameResources& frame, const scene::SceneView source_scene, const std::size_t instance_count) {
        if (instance_count == 0) return;
        const DiagnosticPushData clear_push{{}, {}, frame.bounds_descriptor, {}, {static_cast<std::uint32_t>(instance_count), 0, 0, 0}};
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.clear_bounds_shader);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&clear_push, 1}));
        command_buffer.dispatch((static_cast<std::uint32_t>(instance_count) + 63u) / 64u, 1, 1);
        const vk::MemoryBarrier2 clear_barrier{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &clear_barrier});
        command_buffer.bindShadersEXT(vk::ShaderStageFlagBits::eCompute, *this->renderer.accumulate_bounds_shader);
        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
            const scene::Instance& instance   = source_scene.resources.instances[gpu_primitive.scene_instance_index];
            if (!instance.visible) continue;
            const scene::Prototype& prototype = *std::ranges::find(source_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
            const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
            const std::array<float, 16>& transform = primitive.transform.matrix;
            DiagnosticPushData push{
                {},
                {},
                frame.bounds_descriptor,
                {},
                {0, gpu_primitive.scene_instance_index, static_cast<std::uint32_t>(gpu_primitive.kind), 0},
                {},
                {},
                {},
                {transform[0], transform[1], transform[2], transform[3]},
                {transform[4], transform[5], transform[6], transform[7]},
                {transform[8], transform[9], transform[10], transform[11]},
            };
            if (gpu_primitive.kind == GpuScenePrimitiveKind::Geometry) {
                const GpuGeometry& geometry = this->context.gpu_scene.view().geometries[gpu_primitive.resource_index];
                push.primary                = geometry.positions_descriptor;
                push.secondary              = geometry.positions_descriptor;
                push.metadata[0]            = geometry.vertex_count;
            } else {
                const GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                push.primary                = spheres.positions_descriptor;
                push.secondary              = spheres.radii_descriptor;
                push.metadata[0]            = spheres.sphere_count;
            }
            if (push.metadata[0] == 0) continue;
            this->context.runtime.resources.push_data(command_buffer, std::as_bytes(std::span{&push, 1}));
            command_buffer.dispatch((push.metadata[0] + 255u) / 256u, 1, 1);
        }
        const vk::MemoryBarrier2 ready{vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead};
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &ready});
    }

    void SceneDiagnosticRenderer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index, DisplayPass& display, DepthBufferView depth, const scene::SceneView source_scene, const scene::Camera& camera, const SceneDiagnosticSettings& settings, const SelectionState& selection) {
        SceneDiagnosticFrameResources& frame_resources = this->renderer.frame_resources[frame_slot_index];
        std::vector<DiagnosticLine> lines{};
        std::vector<DiagnosticBox> boxes{};
        std::vector<SceneEntityReference>& pick_entities = this->renderer.pick_entities[frame_slot_index];
        pick_entities.clear();
        const auto pick_index = [&pick_entities](const SceneEntityReference entity) {
            const auto found = std::ranges::find(pick_entities, entity);
            if (found != pick_entities.end()) return static_cast<std::uint32_t>(found - pick_entities.begin()) + 1u;
            pick_entities.push_back(entity);
            return static_cast<std::uint32_t>(pick_entities.size());
        };
        const auto add_line = [&](const math::Float3 first, const math::Float3 second, const math::Float4 color, const std::optional<SceneEntityReference> entity = std::nullopt, const float width = 0.0f) {
            lines.emplace_back(first, width == 0.0f ? settings.line_width : width, second, std::to_underlying(settings.depth_mode), color, entity ? pick_index(*entity) : 0u);
        };
        const auto add_circle = [&](const math::Float3 center, const math::Float3 first_axis, const math::Float3 second_axis, const float radius, const math::Float4 color, const SceneEntityReference entity) {
            constexpr std::uint32_t segments = 32;
            for (std::uint32_t index = 0; index != segments; ++index) {
                const float first_angle  = static_cast<float>(index) * 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
                const float second_angle = static_cast<float>(index + 1u) * 2.0f * std::numbers::pi_v<float> / static_cast<float>(segments);
                add_line(center + (first_axis * std::cos(first_angle) + second_axis * std::sin(first_angle)) * radius, center + (first_axis * std::cos(second_angle) + second_axis * std::sin(second_angle)) * radius, color, entity);
            }
        };
        const auto selected_instance = [&selection](const scene::InstanceId id) {
            return std::ranges::any_of(selection.selected, [id](const SceneEntityReference entity) { return (entity.kind == SceneEntityKind::Instance && entity.id == id.value) || (entity.kind == SceneEntityKind::AreaEmitter && entity.owner == id.value); });
        };
        std::vector<bool> instance_has_geometry(source_scene.resources.instances.size());
        for (const GpuScenePrimitive& primitive : this->context.gpu_scene.view().primitives) {
            const std::uint32_t count = primitive.kind == GpuScenePrimitiveKind::Geometry ? this->context.gpu_scene.view().geometries[primitive.resource_index].vertex_count : this->context.gpu_scene.view().sphere_sets[primitive.resource_index].sphere_count;
            if (count != 0) instance_has_geometry[primitive.scene_instance_index] = true;
        }

        math::Bounds3 diagnostic_bounds = source_scene.bounds();
        diagnostic_bounds.include(this->context.gpu_scene.view().resolved_dynamic_bounds);
        const float scene_radius = std::max(diagnostic_bounds.radius(), 1.0f);
        if (settings.all_bounds || settings.selected_bounds)
            for (std::uint32_t index = 0; index != source_scene.resources.instances.size(); ++index) {
                const scene::Instance& instance = source_scene.resources.instances[index];
                if (!instance.visible || !instance_has_geometry[index] || (!settings.all_bounds && !selected_instance(instance.id))) continue;
                const SceneEntityReference entity{SceneEntityKind::Instance, instance.id.value};
                boxes.push_back({instance.transform.matrix, {}, {}, diagnostic_color(entity, selection, {0.24f, 0.76f, 1.0f, 0.82f}), {index, 1u, pick_index(entity), std::to_underlying(settings.depth_mode)}});
            }

        if (settings.volume_bounds)
            for (const scene::Volume& volume : source_scene.resources.volumes) {
                const SceneEntityReference entity{SceneEntityKind::Volume, volume.id.value};
                boxes.push_back({
                    volume.transform.matrix,
                    {volume.bounds.minimum.x, volume.bounds.minimum.y, volume.bounds.minimum.z, 0.0f},
                    {volume.bounds.maximum.x, volume.bounds.maximum.y, volume.bounds.maximum.z, 0.0f},
                    diagnostic_color(entity, selection, {0.64f, 0.32f, 0.92f, 0.72f}),
                    {0, 0, pick_index(entity), std::to_underlying(settings.depth_mode)},
                });
            }

        if (settings.volume_grid)
            for (const scene::Volume& volume : source_scene.resources.volumes) {
                const math::Float3 minimum = volume.bounds.minimum;
                const math::Float3 size    = volume.bounds.maximum - volume.bounds.minimum;
                const std::uint32_t count  = settings.volume_grid_sampling;
                for (std::uint32_t first = 0; first <= count; ++first)
                    for (std::uint32_t second = 0; second <= count; ++second) {
                        const float a = static_cast<float>(first) / static_cast<float>(count);
                        const float b = static_cast<float>(second) / static_cast<float>(count);
                        add_line(volume.transform.transform_point(minimum + math::Float3{0.0f, size.y * a, size.z * b}), volume.transform.transform_point(minimum + math::Float3{size.x, size.y * a, size.z * b}), {0.56f, 0.27f, 0.86f, 0.28f});
                        add_line(volume.transform.transform_point(minimum + math::Float3{size.x * a, 0.0f, size.z * b}), volume.transform.transform_point(minimum + math::Float3{size.x * a, size.y, size.z * b}), {0.56f, 0.27f, 0.86f, 0.28f});
                        add_line(volume.transform.transform_point(minimum + math::Float3{size.x * a, size.y * b, 0.0f}), volume.transform.transform_point(minimum + math::Float3{size.x * a, size.y * b, size.z}), {0.56f, 0.27f, 0.86f, 0.28f});
                    }
            }

        if (settings.cameras)
            for (const scene::Camera& scene_camera : source_scene.resources.cameras) {
                if (scene_camera.id == source_scene.camera.id) continue;
                const SceneEntityReference entity{SceneEntityKind::Camera, scene_camera.id.value};
                const math::Float4 color = diagnostic_color(entity, selection, scene_camera.id == source_scene.camera.id ? math::Float4{1.0f, 0.66f, 0.12f, 0.95f} : math::Float4{0.20f, 0.80f, 0.95f, 0.82f});
                const scene::CameraFrame frame = scene_camera.frame();
                std::array<math::Float3, 4> near_corners{};
                std::array<math::Float3, 4> far_corners{};
                std::visit(
                    [&](const auto& data) {
                        constexpr std::array signs{math::Float2{-1.0f, -1.0f}, math::Float2{1.0f, -1.0f}, math::Float2{1.0f, 1.0f}, math::Float2{-1.0f, 1.0f}};
                        for (std::size_t index = 0; index != signs.size(); ++index) {
                            const math::Float2 window{
                                signs[index].x < 0.0f ? data.screen_window.minimum.x : data.screen_window.maximum.x,
                                signs[index].y < 0.0f ? data.screen_window.minimum.y : data.screen_window.maximum.y,
                            };
                            if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) {
                                const float tangent = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f);
                                near_corners[index] = frame.position + frame.forward * data.near_plane + (frame.right * window.x + frame.up * window.y) * tangent * data.near_plane;
                                far_corners[index]  = frame.position + frame.forward * data.far_plane + (frame.right * window.x + frame.up * window.y) * tangent * data.far_plane;
                            } else {
                                near_corners[index] = frame.position + frame.forward * data.near_plane + frame.right * window.x + frame.up * window.y;
                                far_corners[index]  = frame.position + frame.forward * data.far_plane + frame.right * window.x + frame.up * window.y;
                            }
                        }
                        if (settings.camera_focal_plane) {
                            std::array<math::Float3, 4> focal{};
                            for (std::size_t index = 0; index != signs.size(); ++index) {
                                const math::Float2 window{signs[index].x < 0.0f ? data.screen_window.minimum.x : data.screen_window.maximum.x, signs[index].y < 0.0f ? data.screen_window.minimum.y : data.screen_window.maximum.y};
                                float scale{1.0f};
                                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) scale = std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) * data.focal_distance;
                                focal[index] = frame.position + frame.forward * data.focal_distance + (frame.right * window.x + frame.up * window.y) * scale;
                            }
                            for (std::size_t index = 0; index != focal.size(); ++index) add_line(focal[index], focal[(index + 1) % focal.size()], {0.96f, 0.34f, 0.72f, 0.72f}, entity);
                        }
                        if (settings.camera_lens && data.lens_radius > 0.0f) add_circle(frame.position, frame.right, frame.up, data.lens_radius, {0.96f, 0.34f, 0.72f, 0.82f}, entity);
                    },
                    scene_camera.data);
                for (std::size_t index = 0; index != near_corners.size(); ++index) {
                    add_line(near_corners[index], near_corners[(index + 1) % near_corners.size()], color, entity);
                    add_line(far_corners[index], far_corners[(index + 1) % far_corners.size()], color, entity);
                    add_line(near_corners[index], far_corners[index], color, entity);
                }
            }

        if (settings.lights)
            for (const scene::Light& light : source_scene.resources.lights) {
                if (std::holds_alternative<scene::DiffuseAreaLight>(light.data)) continue;
                const SceneEntityReference entity{SceneEntityKind::Light, light.id.value};
                const math::Float4 color = diagnostic_color(entity, selection, {1.0f, 0.82f, 0.25f, 0.88f});
                const math::Transform transform = light_transform(light);
                const math::Float3 position{transform.matrix[3], transform.matrix[7], transform.matrix[11]};
                const math::Float3 right = transform.transform_vector({1.0f, 0.0f, 0.0f}).normalized();
                const math::Float3 up = transform.transform_vector({0.0f, 1.0f, 0.0f}).normalized();
                const math::Float3 forward = -transform.transform_vector({0.0f, 0.0f, 1.0f}).normalized();
                const float icon = scene_radius * 0.035f;
                std::visit(
                    [&](const auto& data) {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight>) {
                            add_circle(position, right, up, icon, color, entity);
                            add_circle(position, right, forward, icon, color, entity);
                            add_circle(position, up, forward, icon, color, entity);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) {
                            const float length = scene_radius * 0.18f;
                            for (const float angle : {data.cone_angle - data.cone_delta, data.cone_angle}) {
                                const float radius = std::tan(angle * std::numbers::pi_v<float> / 180.0f) * length;
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
                            add_line(position, end, color, entity, settings.line_width * 1.25f);
                            add_line(end, end - forward * icon + right * icon * 0.5f, color, entity);
                            add_line(end, end - forward * icon - right * icon * 0.5f, color, entity);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::InfiniteLight>) {
                            add_circle(position, right, up, icon * 1.5f, color, entity);
                            add_circle(position, right, forward, icon * 1.5f, color, entity);
                            add_line(position, position + forward * icon * 3.0f, color, entity, settings.line_width * 1.25f);
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) {
                            add_circle(position, right, up, icon * 1.5f, color, entity);
                            add_circle(position, right, forward, icon * 1.5f, color, entity);
                            add_line(position, position + forward * icon * 3.0f, color, entity, settings.line_width * 1.25f);
                            for (const std::array<math::Float3, 4>& portal : data.portals) {
                                for (std::size_t index = 0; index != portal.size(); ++index) add_line(portal[index], portal[(index + 1) % portal.size()], {0.28f, 0.95f, 0.78f, 0.92f}, entity, settings.line_width * 1.4f);
                                const math::Float3 portal_center = (portal[0] + portal[1] + portal[2] + portal[3]) / 4.0f;
                                const math::Float3 normal = (portal[1] - portal[0]).cross(portal[3] - portal[0]).normalized();
                                add_line(portal_center, portal_center + normal * scene_radius * 0.08f, {0.28f, 0.95f, 0.78f, 0.92f}, entity);
                            }
                        }
                    },
                    light.data);
            }

        if (settings.pivots || settings.orientation)
            for (const SceneEntityReference entity : selection.selected) {
                math::Transform transform{};
                bool spatial{true};
                if (entity.kind == SceneEntityKind::Instance) transform = std::ranges::find(source_scene.resources.instances, scene::InstanceId{entity.id}, &scene::Instance::id)->transform;
                else if (entity.kind == SceneEntityKind::Camera) transform = std::ranges::find(source_scene.resources.cameras, scene::CameraId{entity.id}, &scene::Camera::id)->transform;
                else if (entity.kind == SceneEntityKind::Light) transform = light_transform(*std::ranges::find(source_scene.resources.lights, scene::LightId{entity.id}, &scene::Light::id));
                else if (entity.kind == SceneEntityKind::AreaEmitter) transform = std::ranges::find(source_scene.resources.instances, scene::InstanceId{entity.owner}, &scene::Instance::id)->transform;
                else if (entity.kind == SceneEntityKind::Volume) transform = std::ranges::find(source_scene.resources.volumes, scene::VolumeId{entity.id}, &scene::Volume::id)->transform;
                else spatial = false;
                if (!spatial) continue;
                const math::Float3 origin{transform.matrix[3], transform.matrix[7], transform.matrix[11]};
                const float size = scene_radius * 0.07f;
                if (settings.pivots) {
                    const float half = size * 0.16f;
                    add_line(origin - math::Float3{half, 0.0f, 0.0f}, origin + math::Float3{half, 0.0f, 0.0f}, {0.95f, 0.95f, 0.98f, 0.88f}, entity, settings.line_width * 1.2f);
                    add_line(origin - math::Float3{0.0f, half, 0.0f}, origin + math::Float3{0.0f, half, 0.0f}, {0.95f, 0.95f, 0.98f, 0.88f}, entity, settings.line_width * 1.2f);
                    add_line(origin - math::Float3{0.0f, 0.0f, half}, origin + math::Float3{0.0f, 0.0f, half}, {0.95f, 0.95f, 0.98f, 0.88f}, entity, settings.line_width * 1.2f);
                }
                if (settings.orientation) {
                    add_line(origin, origin + transform.transform_vector({size, 0.0f, 0.0f}), {0.95f, 0.22f, 0.18f, 0.95f}, entity, settings.line_width * 1.2f);
                    add_line(origin, origin + transform.transform_vector({0.0f, size, 0.0f}), {0.20f, 0.90f, 0.28f, 0.95f}, entity, settings.line_width * 1.2f);
                    add_line(origin, origin + transform.transform_vector({0.0f, 0.0f, size}), {0.18f, 0.48f, 1.0f, 0.95f}, entity, settings.line_width * 1.2f);
                }
            }

        this->ensure_buffers(frame_resources, lines.size(), boxes.size(), source_scene.resources.instances.size());
        if (!lines.empty()) std::memcpy(frame_resources.line_buffer.mapped, lines.data(), lines.size() * sizeof(DiagnosticLine));
        if (!boxes.empty()) std::memcpy(frame_resources.box_buffer.mapped, boxes.data(), boxes.size() * sizeof(DiagnosticBox));
        this->resize_pick_image(display.image.extent);
        if (settings.all_bounds || settings.selected_bounds) this->record_bounds(command_buffer, frame_resources, source_scene, source_scene.resources.instances.size());

        const vk::MemoryBarrier2 host_barrier{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderStorageRead};
        std::vector<vk::ImageMemoryBarrier2> image_barriers{};
        image_barriers.emplace_back(vk::ImageMemoryBarrier2{display.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::PipelineStageFlagBits2::eFragmentShader : vk::PipelineStageFlagBits2::eColorAttachmentOutput, display.layout == vk::ImageLayout::eShaderReadOnlyOptimal ? vk::AccessFlagBits2::eShaderSampledRead : vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, display.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *display.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        if (depth.layout != vk::ImageLayout::eShaderReadOnlyOptimal) image_barriers.emplace_back(vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eShaderStorageRead, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, depth.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *depth.image.image, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}});
        image_barriers.emplace_back(vk::ImageMemoryBarrier2{this->renderer.pick_layout == vk::ImageLayout::eUndefined ? vk::PipelineStageFlagBits2::eNone : vk::PipelineStageFlagBits2::eCopy, this->renderer.pick_layout == vk::ImageLayout::eUndefined ? vk::AccessFlags2{} : vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, this->renderer.pick_layout, vk::ImageLayout::eColorAttachmentOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.pick_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 1, &host_barrier, 0, nullptr, static_cast<std::uint32_t>(image_barriers.size()), image_barriers.data()});
        depth.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        const std::array color_attachments{
            vk::RenderingAttachmentInfo{*display.image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eLoad, vk::AttachmentStoreOp::eStore},
            vk::RenderingAttachmentInfo{*this->renderer.pick_image.view, vk::ImageLayout::eColorAttachmentOptimal, vk::ResolveModeFlagBits::eNone, {}, vk::ImageLayout::eUndefined, vk::AttachmentLoadOp::eClear, vk::AttachmentStoreOp::eStore, vk::ClearValue{vk::ClearColorValue{std::array{0u, 0u, 0u, 0u}}}},
        };
        command_buffer.beginRendering(vk::RenderingInfo{{}, {{0, 0}, display.image.extent}, 1, 0, static_cast<std::uint32_t>(color_attachments.size()), color_attachments.data()});
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
        const std::array stages{vk::ShaderStageFlagBits::eTaskEXT, vk::ShaderStageFlagBits::eMeshEXT, vk::ShaderStageFlagBits::eVertex, vk::ShaderStageFlagBits::eFragment};
        const std::array handles{vk::ShaderEXT{}, vk::ShaderEXT{}, *this->renderer.draw_shaders[0], *this->renderer.draw_shaders[1]};
        command_buffer.bindShadersEXT(stages, handles);
        this->context.runtime.resources.bind_descriptor_heaps(command_buffer);
        const std::array<float, 16>& view_projection = camera.matrices().view_projection;
        const auto push_and_draw = [&](const std::uint32_t kind, const DescriptorHandle primary, const DescriptorHandle secondary, const std::uint32_t count, const std::uint32_t pick, const scene::VisualizationDepthMode depth_mode, const math::Float4 color, const math::Transform transform, const std::uint32_t vertex_count, const std::uint32_t gpu_transform_index = std::numeric_limits<std::uint32_t>::max()) {
            const std::array<float, 16>& matrix = transform.matrix;
            const DiagnosticPushData push{
                primary,
                secondary,
                gpu_transform_index == std::numeric_limits<std::uint32_t>::max() ? frame_resources.bounds_descriptor : this->context.gpu_scene.view().primitive_transforms,
                depth.descriptor,
                {kind, count, pick, std::to_underlying(depth_mode)},
                {display.image.extent.width, display.image.extent.height, settings.attribute_sampling, gpu_transform_index},
                {settings.line_width, settings.point_size, settings.normal_scale, static_cast<float>(settings.attribute_sampling)},
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
            command_buffer.draw(vertex_count, kind <= 1 || kind == 7 || kind == 8 ? count : 1u, 0, 0);
        };
        if (!lines.empty()) push_and_draw(0, frame_resources.line_descriptor, frame_resources.line_descriptor, static_cast<std::uint32_t>(lines.size()), 0, settings.depth_mode, {}, {}, 6);
        if (!boxes.empty()) push_and_draw(1, frame_resources.box_descriptor, frame_resources.bounds_descriptor, static_cast<std::uint32_t>(boxes.size()), 0, settings.depth_mode, {}, {}, 72);
        if (settings.all_bounds)
            for (const GpuSceneView::DynamicBoundsView bounds : this->context.gpu_scene.view().dynamic_bounds)
                if (bounds.count != 0) push_and_draw(7, bounds.descriptor, bounds.descriptor, bounds.count, 0, settings.depth_mode, {0.30f, 0.92f, 0.62f, 0.82f}, {}, 72);

        for (const GpuScenePrimitive& gpu_primitive : this->context.gpu_scene.view().primitives) {
                const scene::Instance& instance   = source_scene.resources.instances[gpu_primitive.scene_instance_index];
                const scene::Prototype& prototype = *std::ranges::find(source_scene.resources.prototypes, instance.prototype, &scene::Prototype::id);
                const scene::Primitive& primitive = prototype.primitives[gpu_primitive.prototype_primitive_index];
                const SceneEntityReference instance_entity{SceneEntityKind::Instance, instance.id.value};
                const math::Transform transform = instance.transform * primitive.transform;
                if (gpu_primitive.kind == GpuScenePrimitiveKind::Geometry) {
                    const GpuGeometry& geometry       = this->context.gpu_scene.view().geometries[gpu_primitive.resource_index];
                    const std::uint32_t instance_pick = pick_index(instance_entity);
                    if (settings.wireframe) push_and_draw(2, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, instance_pick, settings.depth_mode, diagnostic_color(instance_entity, selection, {0.18f, 0.76f, 1.0f, 0.56f}), transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.vertices) push_and_draw(3, geometry.positions_descriptor, geometry.positions_descriptor, geometry.vertex_count, instance_pick, settings.depth_mode, diagnostic_color(instance_entity, selection, {0.94f, 0.94f, 0.98f, 0.90f}), transform, geometry.vertex_count * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.normals && (geometry.attribute_mask & 1u) != 0) push_and_draw(4, geometry.positions_descriptor, geometry.normals_descriptor, geometry.vertex_count, instance_pick, settings.depth_mode, {0.24f, 0.86f, 0.48f, 0.84f}, transform, ((geometry.vertex_count + settings.attribute_sampling - 1u) / settings.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.tangents && (geometry.attribute_mask & 2u) != 0) push_and_draw(5, geometry.positions_descriptor, geometry.tangents_descriptor, geometry.vertex_count, instance_pick, settings.depth_mode, {0.94f, 0.38f, 0.74f, 0.84f}, transform, ((geometry.vertex_count + settings.attribute_sampling - 1u) / settings.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.area_emitters && primitive.area_light.value != 0) {
                        const SceneEntityReference area{SceneEntityKind::AreaEmitter, primitive.area_light.value, instance.id.value, gpu_primitive.prototype_primitive_index};
                        push_and_draw(2, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, pick_index(area), settings.depth_mode, diagnostic_color(area, selection, {1.0f, 0.48f, 0.10f, 0.94f}), transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
                        if ((geometry.attribute_mask & 1u) != 0) push_and_draw(4, geometry.positions_descriptor, geometry.normals_descriptor, geometry.vertex_count, pick_index(area), settings.depth_mode, {1.0f, 0.48f, 0.10f, 0.72f}, transform, ((geometry.vertex_count + settings.attribute_sampling - 1u) / settings.attribute_sampling) * 6u, gpu_primitive.scene_primitive_index);
                    }
                    if (settings.medium_boundaries && (primitive.media.inside.value != 0 || primitive.media.outside.value != 0)) push_and_draw(2, geometry.positions_descriptor, geometry.indices_descriptor, geometry.index_count / 3u, instance_pick, scene::VisualizationDepthMode::XRay, {0.18f, 0.92f, 0.86f, 0.72f}, transform, geometry.index_count * 6u, gpu_primitive.scene_primitive_index);
                } else {
                    const GpuSphereSet& spheres = this->context.gpu_scene.view().sphere_sets[gpu_primitive.resource_index];
                    const std::uint32_t instance_pick = pick_index(instance_entity);
                    if (settings.wireframe) push_and_draw(8, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, settings.depth_mode, diagnostic_color(instance_entity, selection, {0.18f, 0.76f, 1.0f, 0.56f}), transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.vertices) push_and_draw(6, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, settings.depth_mode, diagnostic_color(instance_entity, selection, {0.94f, 0.94f, 0.98f, 0.90f}), transform, spheres.sphere_count * 6u, gpu_primitive.scene_primitive_index);
                    if (settings.area_emitters && primitive.area_light.value != 0) {
                        const SceneEntityReference area{SceneEntityKind::AreaEmitter, primitive.area_light.value, instance.id.value, gpu_primitive.prototype_primitive_index};
                        push_and_draw(8, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, pick_index(area), settings.depth_mode, diagnostic_color(area, selection, {1.0f, 0.48f, 0.10f, 0.94f}), transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
                    }
                    if (settings.medium_boundaries && (primitive.media.inside.value != 0 || primitive.media.outside.value != 0)) push_and_draw(8, spheres.positions_descriptor, spheres.radii_descriptor, spheres.sphere_count, instance_pick, scene::VisualizationDepthMode::XRay, {0.18f, 0.92f, 0.86f, 0.72f}, transform, 32u * 3u * 6u, gpu_primitive.scene_primitive_index);
                }
        }
        command_buffer.endRendering();

        const std::array end_barriers{
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *display.image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
            vk::ImageMemoryBarrier2{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eCopy, vk::AccessFlagBits2::eTransferRead, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eTransferSrcOptimal, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, *this->renderer.pick_image.image, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}},
        };
        command_buffer.pipelineBarrier2(vk::DependencyInfo{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(end_barriers.size()), end_barriers.data()});
        display.layout             = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->renderer.pick_layout = vk::ImageLayout::eTransferSrcOptimal;
    }

    const GpuImage& SceneDiagnosticRenderer::pick_image() const noexcept {
        return this->renderer.pick_image;
    }

    std::optional<SceneEntityReference> SceneDiagnosticRenderer::pick_entity(const std::uint32_t frame_slot_index, const std::uint32_t pick_index) const noexcept {
        if (pick_index == 0 || pick_index > this->renderer.pick_entities[frame_slot_index].size()) return std::nullopt;
        return this->renderer.pick_entities[frame_slot_index][pick_index - 1u];
    }
} // namespace spectra
