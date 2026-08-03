module spectra.editor;

import :interaction;

import std;

namespace spectra {
    EditorInteraction::EditorInteraction(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, GpuScene& gpu_scene, Renderers& renderers) noexcept : context{runtime, document, dynamics, gpu_scene, renderers} {}

    void EditorInteraction::initialize_from_scene() {
        this->view.camera          = this->context.document.content.source.camera();
        this->view.focus           = this->context.document.content.source.view().bounds().center();
        this->view.navigation_up   = {0.0f, 1.0f, 0.0f};
        this->view.camera_revision = 1;
        const scene::Film& film    = this->context.document.content.source.film();
        this->view.aspect          = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        this->view.axes_plane      = AxesPlane::Xz;
        this->view.source          = CameraSource::Viewport;
        this->camera_changed();
        this->view.selection = {};
        this->editing.undo_history.clear();
        this->editing.redo_history.clear();
        this->editing.transform_edit.reset();
        this->editing.current_edit_serial = 0;
        this->editing.saved_edit_serial   = 0;
        this->editing.next_edit_serial    = 1;
    }

    void EditorInteraction::destroy_scene_rendering() noexcept {
        this->context.renderers.destroy();
        this->context.gpu_scene.destroy();
    }

    void EditorInteraction::rebuild_scene_rendering(scene::Scene& source_scene) {
        std::vector<GpuGeometryBinding> geometry_bindings{};
        geometry_bindings.reserve(this->context.dynamics.outputs.mesh_bindings.size());
        for (const dynamics::MeshOutputBinding& binding : this->context.dynamics.outputs.mesh_bindings)
            geometry_bindings.push_back(GpuGeometryBinding{
                binding.geometry_id,
                binding.update_mode == dynamics::MeshUpdateMode::Deformable ? GpuMeshUpdateMode::Deformable : GpuMeshUpdateMode::TopologyChanging,
                binding.vertex_capacity,
                binding.index_capacity,
            });
        this->context.gpu_scene.initialize(source_scene, geometry_bindings, this->context.dynamics.outputs.particle_capacities, this->context.dynamics.outputs.hidden_instances);
        this->context.renderers.rebuild(this->context.document.content.evaluated.view());
    }

    void EditorInteraction::camera_changed() noexcept {
        this->view.render_camera = this->view.source == CameraSource::Scene ? this->context.document.content.source.camera() : this->view.camera;
        std::visit(
            [this](auto& data) {
                const float center_x         = (data.screen_window.minimum.x + data.screen_window.maximum.x) * 0.5f;
                const float half_height      = (data.screen_window.maximum.y - data.screen_window.minimum.y) * 0.5f;
                const float half_width       = half_height * this->view.aspect;
                data.screen_window.minimum.x = center_x - half_width;
                data.screen_window.maximum.x = center_x + half_width;
            },
            this->view.render_camera.data);
        ++this->view.render_camera_revision;
        this->view.synchronized_source                = this->view.source;
        this->view.synchronized_camera_revision       = this->view.camera_revision;
        this->view.synchronized_scene_camera_revision = this->context.document.content.source.camera().revision;
    }

    void EditorInteraction::orbit_viewport_camera(const float x_pixels, const float y_pixels) noexcept {
        constexpr float radians_per_pixel = 0.006f;
        math::Float3 offset              = this->view.camera.frame().position - this->view.focus;
        const float distance              = offset.length();
        const math::Float3 up            = this->view.navigation_up.normalized();
        math::Float3 horizontal          = (offset - up * offset.dot(up)).normalized();
        const float yaw                   = -x_pixels * radians_per_pixel;
        horizontal                        = horizontal * std::cos(yaw) + up.cross(horizontal) * std::sin(yaw);
        const float pitch                 = std::clamp(std::asin(std::clamp(offset.normalized().dot(up), -1.0f, 1.0f)) + y_pixels * radians_per_pixel, -std::numbers::pi_v<float> * 0.49f, std::numbers::pi_v<float> * 0.49f);
        const math::Float3 direction     = horizontal.normalized() * std::cos(pitch) + up * std::sin(pitch);
        this->view.camera.transform       = math::Transform::look_at(this->view.focus + direction * distance, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.axes_plane = AxesPlane::Xz;
        this->view.source     = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::pan_viewport_camera(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
        const scene::CameraFrame frame = this->view.camera.frame();
        const float distance           = (frame.position - this->view.focus).length();
        const float world_per_pixel    = std::visit(
            [distance, viewport_height](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>)
                    return 2.0f * distance * std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) / viewport_height;
                else
                    return (data.screen_window.maximum.y - data.screen_window.minimum.y) / viewport_height;
            },
            this->view.camera.data);
        const math::Float3 movement = frame.right * (-x_pixels * world_per_pixel) + frame.up * (y_pixels * world_per_pixel);
        this->view.focus             = this->view.focus + movement;
        this->view.camera.transform  = math::Transform::look_at(frame.position + movement, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::zoom_viewport_camera(const float steps) noexcept {
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->view.camera.data)) {
            const float scale = std::pow(0.88f, steps);
            const math::Float2 center{
                (orthographic->screen_window.minimum.x + orthographic->screen_window.maximum.x) * 0.5f,
                (orthographic->screen_window.minimum.y + orthographic->screen_window.maximum.y) * 0.5f,
            };
            orthographic->screen_window.minimum = {
                center.x + (orthographic->screen_window.minimum.x - center.x) * scale,
                center.y + (orthographic->screen_window.minimum.y - center.y) * scale,
            };
            orthographic->screen_window.maximum = {
                center.x + (orthographic->screen_window.maximum.x - center.x) * scale,
                center.y + (orthographic->screen_window.maximum.y - center.y) * scale,
            };
        } else {
            const scene::CameraFrame frame = this->view.camera.frame();
            const math::Float3 offset     = frame.position - this->view.focus;
            const float distance           = std::clamp(offset.length() * std::pow(0.88f, steps), 0.01f, 1000000.0f);
            this->view.camera.transform    = math::Transform::look_at(this->view.focus + offset.normalized() * distance, this->view.focus, this->view.navigation_up);
        }
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::frame_viewport_camera(const math::Bounds3 bounds, const float aspect) noexcept {
        const scene::CameraFrame camera_frame = this->view.camera.frame();
        const math::Float3 target            = bounds.center();
        const math::Float3 direction         = (camera_frame.position - this->view.focus).normalized();
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->view.camera.data)) {
            const float half_height     = bounds.radius() * 1.1f * std::max(1.0f, 1.0f / aspect);
            const float half_width      = half_height * aspect;
            const float distance        = std::max((camera_frame.position - this->view.focus).length(), bounds.radius() * 3.0f);
            orthographic->screen_window = {{-half_width, -half_height}, {half_width, half_height}};
            orthographic->far_plane     = std::max(orthographic->far_plane, distance + bounds.radius() * 4.0f);
            this->view.focus            = target;
            this->view.camera.transform = math::Transform::look_at(target + direction * distance, target, this->view.navigation_up);
            ++this->view.camera_revision;
            return;
        }
        scene::PerspectiveCameraData& perspective = std::get<scene::PerspectiveCameraData>(this->view.camera.data);
        const float horizontal_fov                = 2.0f * std::atan(std::tan(perspective.vertical_fov * std::numbers::pi_v<float> / 360.0f) * aspect);
        const float limiting_fov                  = std::min(perspective.vertical_fov * std::numbers::pi_v<float> / 180.0f, horizontal_fov);
        const float distance                      = bounds.radius() / std::sin(limiting_fov * 0.5f) * 1.1f;
        this->view.focus                          = target;
        this->view.camera.transform               = math::Transform::look_at(target + direction * distance, target, this->view.navigation_up);
        perspective.far_plane                     = std::max(perspective.far_plane, distance + bounds.radius() * 4.0f);
        ++this->view.camera_revision;
    }

    void EditorInteraction::frame_scene(const float aspect) noexcept {
        this->frame_viewport_camera(this->context.document.content.evaluated.view().bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::frame_selection(const float aspect) noexcept {
        if (const std::optional<math::Bounds3> bounds = this->context.document.content.evaluated.view().bounds(this->view.selection.selected_instances))
            this->frame_viewport_camera(*bounds, aspect);
        else
            this->frame_viewport_camera(this->context.document.content.evaluated.view().bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::view_axis(const math::Float3 direction, const float aspect) noexcept {
        const std::optional<math::Bounds3> selected = this->context.document.content.evaluated.view().bounds(this->view.selection.selected_instances);
        const math::Bounds3 bounds                  = selected.value_or(this->context.document.content.evaluated.view().bounds());
        this->view.focus                             = bounds.center();
        this->view.navigation_up                     = std::abs(direction.y) > 0.9f ? math::Float3{0.0f, 0.0f, -1.0f} : math::Float3{0.0f, 1.0f, 0.0f};
        this->view.camera.transform                  = math::Transform::look_at(this->view.focus + direction.normalized() * bounds.radius() * 3.0f, this->view.focus, this->view.navigation_up);
        this->frame_viewport_camera(bounds, aspect);
        if (std::abs(direction.x) > 0.5f)
            this->view.axes_plane = AxesPlane::Yz;
        else if (std::abs(direction.z) > 0.5f)
            this->view.axes_plane = AxesPlane::Xy;
        else
            this->view.axes_plane = AxesPlane::Xz;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void EditorInteraction::select_instance(const scene::InstanceId instance_id, const bool additive) {
        const std::vector<scene::InstanceId>::iterator found = std::ranges::find(this->view.selection.selected_instances, instance_id);
        if (!additive) {
            this->view.selection.selected_instances.assign(1, instance_id);
            this->view.selection.active_instance = instance_id;
            return;
        }
        if (found == this->view.selection.selected_instances.end()) {
            this->view.selection.selected_instances.push_back(instance_id);
            this->view.selection.active_instance = instance_id;
            return;
        }
        this->view.selection.selected_instances.erase(found);
        if (this->view.selection.selected_instances.empty())
            this->view.selection.active_instance.reset();
        else
            this->view.selection.active_instance = this->view.selection.selected_instances.back();
    }

    void EditorInteraction::clear_selection() noexcept {
        this->view.selection.selected_instances.clear();
        this->view.selection.active_instance.reset();
        this->view.selection.hovered_instance.reset();
        this->view.selection.selected_debug_object.reset();
        this->view.selection.hovered_debug_object.reset();
    }

    void EditorInteraction::clear_hover() noexcept {
        this->view.selection.hovered_instance.reset();
        this->view.selection.hovered_debug_object.reset();
    }

    std::pair<std::optional<std::uint64_t>, bool> EditorInteraction::debug_object_at(const float normalized_x, const float normalized_y) const noexcept {
        if (!this->context.dynamics.configuration.initialized) return {};
        const std::array<float, 16>& matrix = this->view.render_camera.matrices().view_projection;
        const auto project                  = [&matrix](const math::Float3 point) -> std::optional<math::Float2> {
            const float clip_x = matrix[0] * point.x + matrix[1] * point.y + matrix[2] * point.z + matrix[3];
            const float clip_y = matrix[4] * point.x + matrix[5] * point.y + matrix[6] * point.z + matrix[7];
            const float clip_w = matrix[12] * point.x + matrix[13] * point.y + matrix[14] * point.z + matrix[15];
            if (clip_w <= 0.0f) return std::nullopt;
            return math::Float2{
                clip_x / clip_w * 0.5f + 0.5f,
                0.5f - clip_y / clip_w * 0.5f,
            };
        };
        const math::Float2 cursor{normalized_x * this->view.aspect, normalized_y};
        float closest_distance_squared = 0.000144f;
        std::optional<std::uint64_t> debug_object_id{};
        bool debug_xray{};
        const auto test_segment = [&](const math::Float3 first, const math::Float3 second, const dynamics::DebugPrimitive& primitive) {
            const std::optional<math::Float2> projected_first  = project(first);
            const std::optional<math::Float2> projected_second = project(second);
            if (!projected_first || !projected_second) return;
            const math::Float2 a{projected_first->x * this->view.aspect, projected_first->y};
            const math::Float2 b{projected_second->x * this->view.aspect, projected_second->y};
            const math::Float2 edge{b.x - a.x, b.y - a.y};
            const math::Float2 offset{cursor.x - a.x, cursor.y - a.y};
            const float length_squared   = edge.x * edge.x + edge.y * edge.y;
            const float t                = length_squared > 0.0f ? std::clamp((offset.x * edge.x + offset.y * edge.y) / length_squared, 0.0f, 1.0f) : 0.0f;
            const float dx               = cursor.x - (a.x + edge.x * t);
            const float dy               = cursor.y - (a.y + edge.y * t);
            const float distance_squared = dx * dx + dy * dy;
            if (distance_squared >= closest_distance_squared || primitive.pick_id == 0) return;
            closest_distance_squared = distance_squared;
            debug_object_id          = primitive.pick_id;
            debug_xray               = primitive.depth_mode == dynamics::DebugDepthMode::XRay;
        };
        for (const dynamics::DebugPrimitive& primitive : this->context.dynamics.publication.debug_primitives) {
            if (primitive.kind == dynamics::DebugPrimitiveKind::Point) {
                test_segment(primitive.first_position, primitive.first_position, primitive);
                continue;
            }
            if (primitive.kind == dynamics::DebugPrimitiveKind::AxisAlignedBox) {
                const math::Float3 minimum = primitive.first_position;
                const math::Float3 maximum = primitive.second_position;
                const std::array corners{
                    math::Float3{minimum.x, minimum.y, minimum.z},
                    math::Float3{maximum.x, minimum.y, minimum.z},
                    math::Float3{minimum.x, maximum.y, minimum.z},
                    math::Float3{maximum.x, maximum.y, minimum.z},
                    math::Float3{minimum.x, minimum.y, maximum.z},
                    math::Float3{maximum.x, minimum.y, maximum.z},
                    math::Float3{minimum.x, maximum.y, maximum.z},
                    math::Float3{maximum.x, maximum.y, maximum.z},
                };
                constexpr std::array<std::array<std::uint32_t, 2>, 12> edges{{
                    {0, 1},
                    {2, 3},
                    {4, 5},
                    {6, 7},
                    {0, 2},
                    {1, 3},
                    {4, 6},
                    {5, 7},
                    {0, 4},
                    {1, 5},
                    {2, 6},
                    {3, 7},
                }};
                for (const std::array<std::uint32_t, 2> edge : edges) test_segment(corners[edge[0]], corners[edge[1]], primitive);
                continue;
            }
            test_segment(primitive.first_position, primitive.second_position, primitive);
        }
        return {debug_object_id, debug_xray};
    }

    void EditorInteraction::begin_transform_edit(const scene::InstanceId instance_id) {
        if (this->editing.transform_edit || (this->context.dynamics.configuration.initialized && this->context.dynamics.controls(instance_id))) return;
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->context.document.content.source.resources.instances, instance_id, &scene::Instance::id);
        this->editing.transform_edit                             = TransformEdit{
                                        .instance_id      = instance_id,
                                        .before_transform = found->transform,
                                        .after_transform  = found->transform,
        };
    }

    void EditorInteraction::update_transform_edit(math::Transform transform) {
        if (!this->editing.transform_edit) return;
        this->editing.transform_edit->after_transform = transform;
        this->context.document.update_transform(this->context.document.content.source, this->editing.transform_edit->instance_id, transform);
        this->context.document.update_transform(this->context.document.content.evaluated, this->editing.transform_edit->instance_id, std::move(transform));
    }

    void EditorInteraction::finish_transform_edit() {
        if (!this->editing.transform_edit) return;
        if (this->editing.transform_edit->before_transform != this->editing.transform_edit->after_transform) this->push_edit(*this->editing.transform_edit);
        this->editing.transform_edit.reset();
    }

    bool EditorInteraction::can_undo() const noexcept {
        return !this->editing.undo_history.empty();
    }

    bool EditorInteraction::can_redo() const noexcept {
        return !this->editing.redo_history.empty();
    }

    bool EditorInteraction::scene_modified() const noexcept {
        return this->editing.current_edit_serial != this->editing.saved_edit_serial || (this->editing.transform_edit && this->editing.transform_edit->before_transform != this->editing.transform_edit->after_transform);
    }

    void EditorInteraction::push_edit(std::variant<TransformEdit, DynamicSetupEdit> edit) {
        std::visit(
            [this](auto& value) {
                value.before_serial               = this->editing.current_edit_serial;
                value.after_serial                = this->editing.next_edit_serial++;
                this->editing.current_edit_serial = value.after_serial;
            },
            edit);
        this->editing.undo_history.push_back(std::move(edit));
        this->editing.redo_history.clear();
    }

    void EditorInteraction::apply_edit(const std::variant<TransformEdit, DynamicSetupEdit>& edit, const bool apply_before) {
        std::visit(
            [this, apply_before](const auto& value) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(value)>, TransformEdit>) {
                    const math::Transform transform = apply_before ? value.before_transform : value.after_transform;
                    this->context.document.update_transform(this->context.document.content.source, value.instance_id, transform);
                    this->context.document.update_transform(this->context.document.content.evaluated, value.instance_id, transform);
                } else {
                    const std::optional<dynamics::SimulationTimeline> timeline = value.preserve_simulation && this->context.dynamics.configuration.initialized ? std::optional{this->context.dynamics.timeline()} : std::nullopt;
                    const bool running                                         = value.preserve_simulation && this->context.dynamics.clock.playing;
                    scene::Scene next_scene                                    = this->context.document.content.source;
                    this->context.document.update_dynamic_setup(next_scene, apply_before ? value.before_setup : value.after_setup);
                    this->context.runtime.graphics.device.waitIdle();
                    this->destroy_scene_rendering();
                    this->context.dynamics.destroy();
                    this->context.document.content.source = std::move(next_scene);
                    if (this->context.document.content.source.dynamic_setup) this->context.dynamics.initialize(this->context.document.content.path, this->context.document.content.source);
                    if (this->context.dynamics.configuration.initialized && timeline) this->context.dynamics.seek(timeline->simulation_step);
                    this->context.dynamics.clock.playing = this->context.dynamics.configuration.initialized && running;
                    this->rebuild_scene_rendering(this->context.document.content.source);
                    this->camera_changed();
                }
            },
            edit);
    }

    void EditorInteraction::undo() {
        if (this->editing.undo_history.empty()) return;
        std::variant<TransformEdit, DynamicSetupEdit> edit = std::move(this->editing.undo_history.back());
        this->editing.undo_history.pop_back();
        this->apply_edit(edit, true);
        this->editing.current_edit_serial = std::visit([](const auto& value) { return value.before_serial; }, edit);
        this->editing.redo_history.push_back(std::move(edit));
    }

    void EditorInteraction::redo() {
        if (this->editing.redo_history.empty()) return;
        std::variant<TransformEdit, DynamicSetupEdit> edit = std::move(this->editing.redo_history.back());
        this->editing.redo_history.pop_back();
        this->apply_edit(edit, false);
        this->editing.current_edit_serial = std::visit([](const auto& value) { return value.after_serial; }, edit);
        this->editing.undo_history.push_back(std::move(edit));
    }

    std::optional<std::uint32_t> EditorInteraction::instance_index(const scene::InstanceId instance_id) const noexcept {
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->context.document.content.source.resources.instances, instance_id, &scene::Instance::id);
        if (found == this->context.document.content.source.resources.instances.end()) return std::nullopt;
        return static_cast<std::uint32_t>(found - this->context.document.content.source.resources.instances.begin());
    }

    void EditorInteraction::prune_selection() noexcept {
        std::erase_if(this->view.selection.selected_instances, [this](const scene::InstanceId id) { return !this->instance_index(id); });
        if (this->view.selection.hovered_instance && !this->instance_index(*this->view.selection.hovered_instance)) this->view.selection.hovered_instance.reset();
        if (this->view.selection.active_instance && !this->instance_index(*this->view.selection.active_instance)) this->view.selection.active_instance.reset();
    }

    void EditorInteraction::set_dynamic_system_parameters(const std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters, const bool reset) {
        scene::DynamicSetup setup              = *this->context.document.content.source.dynamic_setup;
        const std::optional before             = this->context.document.content.source.dynamic_setup;
        setup.systems[system_index].parameters = std::move(parameters);
        this->context.document.update_dynamic_setup(this->context.document.content.source, setup);
        this->push_edit(DynamicSetupEdit{.before_setup = before, .after_setup = setup});
        if (!setup.systems[system_index].enabled) return;
        this->context.dynamics.configuration.setup.systems[system_index].parameters = setup.systems[system_index].parameters;
        std::vector<dynamics::ParameterDescriptor>& status_parameters               = this->context.dynamics.systems.statuses[system_index].parameters;
        for (dynamics::ParameterDescriptor& parameter : status_parameters) {
            const auto setting = std::ranges::find(setup.systems[system_index].parameters, parameter.id, &scene::DynamicParameterSetting::parameter_id);
            if (setting != setup.systems[system_index].parameters.end()) parameter.value = setting->value;
        }
        this->context.dynamics.apply_parameter_changes(system_index, reset);
    }

    void EditorInteraction::set_dynamic_clock(scene::DynamicClock clock) {
        scene::DynamicSetup setup = *this->context.document.content.source.dynamic_setup;
        setup.clock               = std::move(clock);
        this->commit_dynamic_setup(std::move(setup));
    }

    void EditorInteraction::set_dynamic_seed(const std::uint64_t seed) {
        scene::DynamicSetup setup = *this->context.document.content.source.dynamic_setup;
        setup.seed                = seed;
        this->commit_dynamic_setup(std::move(setup));
    }

    void EditorInteraction::commit_dynamic_setup(std::optional<scene::DynamicSetup> setup, const bool preserve_simulation) {
        if (setup == this->context.document.content.source.dynamic_setup) return;
        const std::optional<dynamics::SimulationTimeline> timeline = preserve_simulation && this->context.dynamics.configuration.initialized ? std::optional{this->context.dynamics.timeline()} : std::nullopt;
        const bool running                                         = preserve_simulation && this->context.dynamics.running();
        DynamicSetupEdit edit{.before_setup = this->context.document.content.source.dynamic_setup, .after_setup = setup, .preserve_simulation = preserve_simulation};
        scene::Scene next_scene = this->context.document.content.source;
        this->context.document.update_dynamic_setup(next_scene, std::move(setup));
        this->context.runtime.graphics.device.waitIdle();
        this->destroy_scene_rendering();
        this->context.dynamics.destroy();
        this->context.document.content.source = std::move(next_scene);
        if (this->context.document.content.source.dynamic_setup) this->context.dynamics.initialize(this->context.document.content.path, this->context.document.content.source);
        if (timeline && this->context.dynamics.configuration.initialized) this->context.dynamics.seek(timeline->simulation_step);
        if (running && this->context.dynamics.configuration.initialized) this->context.dynamics.start();
        this->rebuild_scene_rendering(this->context.document.content.source);
        this->camera_changed();
        this->push_edit(std::move(edit));
    }

    void EditorInteraction::set_dynamic_system_enabled(const std::size_t system_index, const bool enabled) {
        scene::DynamicSetup setup           = *this->context.document.content.source.dynamic_setup;
        setup.systems[system_index].enabled = enabled;
        this->commit_dynamic_setup(std::move(setup));
    }

    void EditorInteraction::set_dynamic_system_visible(const std::size_t system_index, const bool visible) {
        scene::DynamicSetup setup           = *this->context.document.content.source.dynamic_setup;
        setup.systems[system_index].visible = visible;
        this->commit_dynamic_setup(std::move(setup), true);
    }

    void EditorInteraction::set_dynamic_system_provider(const std::size_t system_index, const dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings) {
        scene::DynamicSetup setup                   = *this->context.document.content.source.dynamic_setup;
        scene::DynamicSystem& destination           = setup.systems[system_index];
        const dynamics::ProviderDescriptor& current = this->context.dynamics.provider_descriptor(destination.provider_id);
        if (provider.interface_id != current.interface_id || provider.interface_version != current.interface_version) throw std::runtime_error("A comparison System can only replace its Provider with the same interface contract");
        destination.provider_id = provider.id;
        destination.name        = provider.name;
        destination.bindings    = std::move(bindings);
        destination.parameters.clear();
        for (const dynamics::ParameterDescriptor& parameter : provider.parameters) destination.parameters.emplace_back(parameter.id, parameter.value);
        this->commit_dynamic_setup(std::move(setup));
    }

    void EditorInteraction::add_dynamic_system(const dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings) {
        scene::DynamicSetup setup = this->context.document.content.source.dynamic_setup.value_or(scene::DynamicSetup{});
        std::string system_id     = provider.id;
        for (std::uint32_t suffix = 2; std::ranges::any_of(setup.systems, [&system_id](const scene::DynamicSystem& system) { return system.id.value == system_id; }); ++suffix) system_id = std::format("{}_{}", provider.id, suffix);
        scene::DynamicSystem system{.id = {system_id}, .name = provider.name, .provider_id = provider.id, .enabled = false, .visible = true, .bindings = std::move(bindings)};
        for (const dynamics::ParameterDescriptor& parameter : provider.parameters) system.parameters.emplace_back(parameter.id, parameter.value);
        setup.systems.emplace_back(std::move(system));
        this->commit_dynamic_setup(std::move(setup));
    }

    void EditorInteraction::remove_dynamic_system(const std::size_t system_index) {
        scene::DynamicSetup setup = *this->context.document.content.source.dynamic_setup;
        setup.systems.erase(setup.systems.begin() + system_index);
        this->commit_dynamic_setup(setup.systems.empty() ? std::optional<scene::DynamicSetup>{} : std::optional{std::move(setup)});
    }

    void EditorInteraction::set_dynamic_port_binding(const std::size_t system_index, std::string port_id, scene::DynamicPortBinding binding) {
        scene::DynamicSetup setup                        = *this->context.document.content.source.dynamic_setup;
        std::vector<scene::DynamicPortBinding>& bindings = setup.systems[system_index].bindings;
        std::erase_if(bindings, [&port_id](const scene::DynamicPortBinding& current) { return current.port_id == port_id; });
        binding.port_id = std::move(port_id);
        bindings.emplace_back(std::move(binding));
        this->commit_dynamic_setup(std::move(setup));
    }
} // namespace spectra
