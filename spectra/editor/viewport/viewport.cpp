module spectra.editor.viewport;

import std;

namespace spectra::editor {
    Viewport::Viewport(Document& document, simulation::Runtime& simulation) noexcept : context{document, simulation} {}

    void Viewport::initialize_from_scene() {
        this->view.camera                     = this->context.document.authored.camera();
        const scene::CameraFrame camera_frame = this->view.camera.frame();
        const math::Float3 bounds_center      = this->navigation_bounds().center();
        this->view.focus                      = camera_frame.position + camera_frame.forward * (bounds_center - camera_frame.position).dot(camera_frame.forward);
        this->view.navigation_up              = {0.0f, 1.0f, 0.0f};
        this->view.camera_revision            = 1;
        const scene::Film& film               = this->context.document.authored.film();
        this->view.aspect                     = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        this->view.axes_plane                 = render::AxesPlane::Xz;
        this->view.source                     = CameraSource::Viewport;
        this->view.scene_camera               = this->context.document.authored.active_camera;
        this->camera_changed();
        this->view.selection = {};
    }

    void Viewport::synchronize_bounds(const math::Bounds3 scene_bounds, const std::span<const math::Bounds3> instance_bounds) {
        this->bounds.scene = scene_bounds;
        this->bounds.instances.assign(instance_bounds.begin(), instance_bounds.end());
    }

    void Viewport::camera_changed() noexcept {
        const scene::Camera& scene_camera = this->view.source == CameraSource::Scene ? *std::ranges::find(this->context.document.evaluated.resources.cameras, this->view.scene_camera, &scene::Camera::id) : this->view.camera;
        this->view.render_camera          = scene_camera;
        this->view.camera_gate.reset();
        const simulation::CameraReferenceImage* reference = this->view.source == CameraSource::Scene ? this->context.simulation.camera_reference(this->view.scene_camera) : nullptr;
        std::visit(
            [this, reference](auto& data) {
                const float center_x    = (data.screen_window.minimum.x + data.screen_window.maximum.x) * 0.5f;
                const float center_y    = (data.screen_window.minimum.y + data.screen_window.maximum.y) * 0.5f;
                const float half_width  = (data.screen_window.maximum.x - data.screen_window.minimum.x) * 0.5f;
                const float half_height = (data.screen_window.maximum.y - data.screen_window.minimum.y) * 0.5f;
                if (!reference) {
                    const float adjusted_half_width = half_height * this->view.aspect;
                    data.screen_window.minimum.x    = center_x - adjusted_half_width;
                    data.screen_window.maximum.x    = center_x + adjusted_half_width;
                    return;
                }
                const float image_aspect = static_cast<float>(reference->extent[0]) / static_cast<float>(reference->extent[1]);
                if (this->view.aspect >= image_aspect) {
                    const float scale            = this->view.aspect / image_aspect;
                    data.screen_window.minimum.x = center_x - half_width * scale;
                    data.screen_window.maximum.x = center_x + half_width * scale;
                    const float gate_width       = image_aspect / this->view.aspect;
                    this->view.camera_gate       = math::Float4{(1.0f - gate_width) * 0.5f, 0.0f, gate_width, 1.0f};
                } else {
                    const float scale            = image_aspect / this->view.aspect;
                    data.screen_window.minimum.y = center_y - half_height * scale;
                    data.screen_window.maximum.y = center_y + half_height * scale;
                    const float gate_height      = this->view.aspect / image_aspect;
                    this->view.camera_gate       = math::Float4{0.0f, (1.0f - gate_height) * 0.5f, 1.0f, gate_height};
                }
            },
            this->view.render_camera.data);
        ++this->view.render_camera_revision;
        this->view.synchronized_source                = this->view.source;
        this->view.synchronized_camera_revision       = this->view.camera_revision;
        this->view.synchronized_scene_camera_revision = scene_camera.revision;
    }

    void Viewport::view_camera(const scene::CameraId camera_id) noexcept {
        this->view.scene_camera = camera_id;
        this->view.source       = CameraSource::Scene;
        this->camera_changed();
    }

    void Viewport::toggle_scene_camera() noexcept {
        if (this->view.source == CameraSource::Scene) this->view.source = CameraSource::Viewport;
        else {
            this->view.scene_camera = this->context.document.authored.active_camera;
            this->view.source       = CameraSource::Scene;
        }
        this->camera_changed();
    }

    void Viewport::orbit_viewport_camera(const float x_pixels, const float y_pixels) {
        constexpr float radians_per_pixel = 0.006f;
        math::Float3 offset               = this->view.camera.frame().position - this->view.focus;
        const float distance              = offset.length();
        if (distance == 0.0f) return;
        const math::Float3 up        = this->view.navigation_up.normalized();
        math::Float3 horizontal      = (offset - up * offset.dot(up)).normalized();
        const float yaw              = -x_pixels * radians_per_pixel;
        horizontal                   = horizontal * std::cos(yaw) + up.cross(horizontal) * std::sin(yaw);
        const float pitch            = std::clamp(std::asin(std::clamp(offset.normalized().dot(up), -1.0f, 1.0f)) + y_pixels * radians_per_pixel, -std::numbers::pi_v<float> * 0.49f, std::numbers::pi_v<float> * 0.49f);
        const math::Float3 direction = horizontal.normalized() * std::cos(pitch) + up * std::sin(pitch);
        this->view.camera.transform  = math::Transform::look_at(this->view.focus + direction * distance, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.axes_plane = render::AxesPlane::Xz;
        this->view.source     = CameraSource::Viewport;
        this->camera_changed();
    }

    void Viewport::pan_viewport_camera(const float x_pixels, const float y_pixels, const float viewport_height) {
        const scene::CameraFrame frame = this->view.camera.frame();
        const float distance           = (frame.position - this->view.focus).length();
        if (distance == 0.0f) return;
        const float world_per_pixel = std::visit(
            [distance, viewport_height](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>) return 2.0f * distance * std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) / viewport_height;
                else return (data.screen_window.maximum.y - data.screen_window.minimum.y) / viewport_height;
            },
            this->view.camera.data);
        const math::Float3 movement = frame.right * (-x_pixels * world_per_pixel) + frame.up * (y_pixels * world_per_pixel);
        this->view.focus            = this->view.focus + movement;
        this->view.camera.transform = math::Transform::look_at(frame.position + movement, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Viewport::zoom_viewport_camera(const float steps) {
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
            const math::Float3 offset      = frame.position - this->view.focus;
            if (offset == math::Float3{}) return;
            const float distance        = std::clamp(offset.length() * std::pow(0.88f, steps), 0.01f, 1000000.0f);
            this->view.camera.transform = math::Transform::look_at(this->view.focus + offset.normalized() * distance, this->view.focus, this->view.navigation_up);
        }
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Viewport::frame_selection(const float aspect) {
        math::Bounds3 selected = math::Bounds3::empty();
        bool found{};
        for (const scene::EntityReference entity : this->view.selection.selected)
            if (const std::optional<math::Bounds3> bounds = this->entity_bounds(entity)) {
                selected.include(*bounds);
                found = true;
            }
        if (found) this->frame_viewport_camera(selected, aspect);
        else this->frame_viewport_camera(this->navigation_bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Viewport::view_axis(const math::Float3 direction, const float aspect) {
        math::Bounds3 selected = math::Bounds3::empty();
        bool found{};
        for (const scene::EntityReference entity : this->view.selection.selected)
            if (const std::optional<math::Bounds3> entity_bound = this->entity_bounds(entity)) {
                selected.include(*entity_bound);
                found = true;
            }
        const math::Bounds3 bounds  = found ? selected : this->navigation_bounds();
        this->view.focus            = bounds.center();
        this->view.navigation_up    = std::abs(direction.y) > 0.9f ? math::Float3{0.0f, 0.0f, -1.0f} : math::Float3{0.0f, 1.0f, 0.0f};
        this->view.camera.transform = math::Transform::look_at(this->view.focus + direction.normalized() * bounds.radius() * 3.0f, this->view.focus, this->view.navigation_up);
        this->frame_viewport_camera(bounds, aspect);
        if (std::abs(direction.x) > 0.5f) this->view.axes_plane = render::AxesPlane::Yz;
        else if (std::abs(direction.z) > 0.5f) this->view.axes_plane = render::AxesPlane::Xy;
        else this->view.axes_plane = render::AxesPlane::Xz;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Viewport::select(const scene::EntityReference entity, const bool additive) {
        const std::vector<scene::EntityReference>::iterator found = std::ranges::find(this->view.selection.selected, entity);
        if (!additive) {
            this->view.selection.selected.assign(1, entity);
            this->view.selection.active = entity;
            return;
        }
        if (found == this->view.selection.selected.end()) {
            this->view.selection.selected.push_back(entity);
            this->view.selection.active = entity;
            return;
        }
        this->view.selection.selected.erase(found);
        if (this->view.selection.selected.empty()) this->view.selection.active.reset();
        else this->view.selection.active = this->view.selection.selected.back();
    }

    void Viewport::clear_selection() noexcept {
        this->view.selection.selected.clear();
        this->view.selection.active.reset();
        this->view.selection.hovered.reset();
    }

    void Viewport::clear_hover() noexcept {
        this->view.selection.hovered.reset();
    }

    void Viewport::prune_selection() noexcept {
        std::erase_if(this->view.selection.selected, [this](const scene::EntityReference entity) { return !this->entity_exists(entity); });
        if (this->view.selection.hovered && !this->entity_exists(*this->view.selection.hovered)) this->view.selection.hovered.reset();
        if (this->view.selection.active && !this->entity_exists(*this->view.selection.active)) this->view.selection.active.reset();
    }

    void Viewport::frame_viewport_camera(const math::Bounds3 bounds, const float aspect) {
        const scene::CameraFrame camera_frame = this->view.camera.frame();
        const math::Float3 target             = bounds.center();
        const math::Float3 offset             = camera_frame.position - this->view.focus;
        const math::Float3 direction          = offset == math::Float3{} ? -camera_frame.forward : offset.normalized();
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

    bool Viewport::entity_exists(const scene::EntityReference entity) const noexcept {
        const scene::SceneResources& resources = this->context.document.evaluated.resources;
        if (const scene::InstanceId* id = std::get_if<scene::InstanceId>(&entity.data)) return std::ranges::contains(resources.instances, *id, &scene::Instance::id);
        if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) return std::ranges::contains(resources.cameras, *id, &scene::Camera::id);
        if (const scene::LightId* id = std::get_if<scene::LightId>(&entity.data)) return std::ranges::contains(resources.lights, *id, &scene::Light::id);
        if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) return std::ranges::contains(resources.particle_sets, *id, &scene::ParticleSet::id);
        if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) return std::ranges::contains(resources.volumes, *id, &scene::Volume::id);
        if (const scene::NeuralFieldId* id = std::get_if<scene::NeuralFieldId>(&entity.data)) return std::ranges::contains(resources.neural_fields, *id, &scene::NeuralField::id);
        const scene::EntityReference::AreaEmitter* emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity.data);
        if (!emitter) return false;
        const auto instance = std::ranges::find(resources.instances, emitter->instance, &scene::Instance::id);
        if (instance == resources.instances.end()) return false;
        const scene::Prototype& prototype = *std::ranges::find(resources.prototypes, instance->prototype, &scene::Prototype::id);
        return emitter->primitive_index < prototype.primitives.size() && prototype.primitives[emitter->primitive_index].area_light == emitter->light;
    }

    math::Bounds3 Viewport::navigation_bounds() const noexcept {
        const scene::Scene& source                      = this->context.document.evaluated;
        const std::span<const math::Bounds3> gpu_bounds = this->bounds.instances;
        math::Bounds3 bounds                            = math::Bounds3::empty();
        for (std::size_t index = 0; index != source.resources.instances.size(); ++index) {
            const scene::Instance& instance = source.resources.instances[index];
            if (!instance.visible) continue;
            if (index < gpu_bounds.size()) bounds.include(gpu_bounds[index]);
            else if (const std::optional<math::Bounds3> instance_bounds = source.view().bounds(std::array{instance.id})) bounds.include(*instance_bounds);
        }
        for (const scene::Volume& volume : source.resources.volumes)
            if (volume.visible) bounds.include(volume.domain.transformed(volume.transform));
        for (const scene::ParticleSet& particles : source.resources.particle_sets)
            if (particles.visible) bounds.include(scene::particle_set_bounds(particles));
        for (const scene::NeuralField& field : source.resources.neural_fields)
            if (field.visible) bounds.include(scene::NeuralField::local_bounds.transformed(field.transform));
        return bounds;
    }

    math::Bounds3 Viewport::effective_scene_bounds() const noexcept {
        math::Bounds3 bounds = this->context.document.evaluated.view().bounds();
        bounds.include(this->bounds.scene);
        return bounds;
    }

    std::optional<math::Bounds3> Viewport::entity_bounds(const scene::EntityReference entity) const noexcept {
        const scene::Scene& source                                  = this->context.document.evaluated;
        const scene::InstanceId* selected_instance                  = std::get_if<scene::InstanceId>(&entity.data);
        const scene::EntityReference::AreaEmitter* selected_emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity.data);
        if (selected_instance || selected_emitter) {
            const scene::InstanceId instance_id             = selected_instance ? *selected_instance : selected_emitter->instance;
            const auto instance                             = std::ranges::find(source.resources.instances, instance_id, &scene::Instance::id);
            const std::size_t index                         = static_cast<std::size_t>(instance - source.resources.instances.begin());
            const std::span<const math::Bounds3> gpu_bounds = this->bounds.instances;
            if (index < gpu_bounds.size() && gpu_bounds[index].valid()) return gpu_bounds[index];
            return source.view().bounds(std::array{instance_id});
        }
        const math::Bounds3 scene_bounds = this->effective_scene_bounds();
        const float extent               = std::max(scene_bounds.radius() * 0.05f, 0.05f);
        if (const scene::VolumeId* id = std::get_if<scene::VolumeId>(&entity.data)) {
            const scene::Volume& volume = *std::ranges::find(source.resources.volumes, *id, &scene::Volume::id);
            return volume.domain.transformed(volume.transform);
        }
        if (const scene::ParticleSetId* id = std::get_if<scene::ParticleSetId>(&entity.data)) return scene::particle_set_bounds(*std::ranges::find(source.resources.particle_sets, *id, &scene::ParticleSet::id));
        if (const scene::NeuralFieldId* id = std::get_if<scene::NeuralFieldId>(&entity.data)) {
            const scene::NeuralField& field = *std::ranges::find(source.resources.neural_fields, *id, &scene::NeuralField::id);
            return scene::NeuralField::local_bounds.transformed(field.transform);
        }
        if (const scene::CameraId* id = std::get_if<scene::CameraId>(&entity.data)) {
            const scene::Camera& camera = *std::ranges::find(source.resources.cameras, *id, &scene::Camera::id);
            const math::Float3 position = camera.frame().position;
            return math::Bounds3{position - math::Float3{extent, extent, extent}, position + math::Float3{extent, extent, extent}};
        }
        const scene::LightId* light_id = std::get_if<scene::LightId>(&entity.data);
        if (!light_id) return std::nullopt;
        const scene::Light& light = *std::ranges::find(source.resources.lights, *light_id, &scene::Light::id);
        return std::visit(
            [&](const auto& data) -> std::optional<math::Bounds3> {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PointLight> || std::same_as<std::remove_cvref_t<decltype(data)>, scene::SpotLight>) {
                    const math::Float3 position{data.transform.matrix[3], data.transform.matrix[7], data.transform.matrix[11]};
                    return math::Bounds3{position - math::Float3{extent, extent, extent}, position + math::Float3{extent, extent, extent}};
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) {
                    math::Bounds3 portals = math::Bounds3::empty();
                    for (const std::array<math::Float3, 4>& portal : data.portals)
                        for (const math::Float3 point : portal) portals.include(point);
                    return data.portals.empty() ? std::optional{scene_bounds} : std::optional{portals};
                } else return scene_bounds;
            },
            light.data);
    }

} // namespace spectra::editor
