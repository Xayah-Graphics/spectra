module spectra.editor.viewport.interaction;

import std;

namespace spectra {
    ViewportInteraction::ViewportInteraction(SceneDocument& document, DynamicsRuntime& dynamics, GpuScene& gpu_scene) noexcept : context{document, dynamics, gpu_scene} {}

    void ViewportInteraction::initialize_from_scene() {
        this->view.camera          = this->context.document.content.source.camera();
        const scene::CameraFrame camera_frame = this->view.camera.frame();
        const math::Float3 bounds_center       = this->navigation_bounds().center();
        this->view.focus                       = camera_frame.position + camera_frame.forward * (bounds_center - camera_frame.position).dot(camera_frame.forward);
        this->view.navigation_up   = {0.0f, 1.0f, 0.0f};
        this->view.camera_revision = 1;
        const scene::Film& film    = this->context.document.content.source.film();
        this->view.aspect          = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        this->view.axes_plane      = AxesPlane::Xz;
        this->view.source          = CameraSource::Viewport;
        this->camera_changed();
        this->view.selection = {};
    }

    void ViewportInteraction::camera_changed() noexcept {
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

    void ViewportInteraction::orbit_viewport_camera(const float x_pixels, const float y_pixels) noexcept {
        constexpr float radians_per_pixel = 0.006f;
        math::Float3 offset               = this->view.camera.frame().position - this->view.focus;
        const float distance              = offset.length();
        const math::Float3 up             = this->view.navigation_up.normalized();
        math::Float3 horizontal           = (offset - up * offset.dot(up)).normalized();
        const float yaw                   = -x_pixels * radians_per_pixel;
        horizontal                        = horizontal * std::cos(yaw) + up.cross(horizontal) * std::sin(yaw);
        const float pitch                 = std::clamp(std::asin(std::clamp(offset.normalized().dot(up), -1.0f, 1.0f)) + y_pixels * radians_per_pixel, -std::numbers::pi_v<float> * 0.49f, std::numbers::pi_v<float> * 0.49f);
        const math::Float3 direction      = horizontal.normalized() * std::cos(pitch) + up * std::sin(pitch);
        this->view.camera.transform       = math::Transform::look_at(this->view.focus + direction * distance, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.axes_plane = AxesPlane::Xz;
        this->view.source     = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::pan_viewport_camera(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
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
        this->view.focus            = this->view.focus + movement;
        this->view.camera.transform = math::Transform::look_at(frame.position + movement, this->view.focus, this->view.navigation_up);
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::zoom_viewport_camera(const float steps) noexcept {
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
            const float distance           = std::clamp(offset.length() * std::pow(0.88f, steps), 0.01f, 1000000.0f);
            this->view.camera.transform    = math::Transform::look_at(this->view.focus + offset.normalized() * distance, this->view.focus, this->view.navigation_up);
        }
        ++this->view.camera_revision;
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::frame_viewport_camera(const math::Bounds3 bounds, const float aspect) noexcept {
        const scene::CameraFrame camera_frame = this->view.camera.frame();
        const math::Float3 target             = bounds.center();
        const math::Float3 direction          = (camera_frame.position - this->view.focus).normalized();
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

    void ViewportInteraction::frame_scene(const float aspect) noexcept {
        this->frame_viewport_camera(this->navigation_bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::frame_selection(const float aspect) noexcept {
        math::Bounds3 selected = math::Bounds3::empty();
        bool found{};
        for (const SceneEntityReference entity : this->view.selection.selected)
            if (const std::optional<math::Bounds3> bounds = this->entity_bounds(entity)) {
                selected.include(*bounds);
                found = true;
            }
        if (found)
            this->frame_viewport_camera(selected, aspect);
        else
            this->frame_viewport_camera(this->navigation_bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::view_axis(const math::Float3 direction, const float aspect) noexcept {
        math::Bounds3 selected = math::Bounds3::empty();
        bool found{};
        for (const SceneEntityReference entity : this->view.selection.selected)
            if (const std::optional<math::Bounds3> entity_bound = this->entity_bounds(entity)) {
                selected.include(*entity_bound);
                found = true;
            }
        const math::Bounds3 bounds                  = found ? selected : this->navigation_bounds();
        this->view.focus                            = bounds.center();
        this->view.navigation_up                    = std::abs(direction.y) > 0.9f ? math::Float3{0.0f, 0.0f, -1.0f} : math::Float3{0.0f, 1.0f, 0.0f};
        this->view.camera.transform                 = math::Transform::look_at(this->view.focus + direction.normalized() * bounds.radius() * 3.0f, this->view.focus, this->view.navigation_up);
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

    void ViewportInteraction::select(const SceneEntityReference entity, const bool additive) {
        const std::vector<SceneEntityReference>::iterator found = std::ranges::find(this->view.selection.selected, entity);
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
        if (this->view.selection.selected.empty())
            this->view.selection.active.reset();
        else
            this->view.selection.active = this->view.selection.selected.back();
    }

    void ViewportInteraction::clear_selection() noexcept {
        this->view.selection.selected.clear();
        this->view.selection.active.reset();
        this->view.selection.hovered.reset();
    }

    void ViewportInteraction::clear_hover() noexcept {
        this->view.selection.hovered.reset();
    }

    bool ViewportInteraction::entity_exists(const SceneEntityReference entity) const noexcept {
        const scene::SceneResources& resources = this->context.document.content.source.resources;
        if (entity.kind == SceneEntityKind::Instance) return std::ranges::contains(resources.instances, scene::InstanceId{entity.id}, &scene::Instance::id);
        if (entity.kind == SceneEntityKind::Camera) return std::ranges::contains(resources.cameras, scene::CameraId{entity.id}, &scene::Camera::id);
        if (entity.kind == SceneEntityKind::Light) return std::ranges::contains(resources.lights, scene::LightId{entity.id}, &scene::Light::id);
        if (entity.kind == SceneEntityKind::Volume) return std::ranges::contains(resources.volumes, scene::VolumeId{entity.id}, &scene::Volume::id);
        if (entity.kind != SceneEntityKind::AreaEmitter) return false;
        const auto instance = std::ranges::find(resources.instances, scene::InstanceId{entity.owner}, &scene::Instance::id);
        if (instance == resources.instances.end()) return false;
        const scene::Prototype& prototype = *std::ranges::find(resources.prototypes, instance->prototype, &scene::Prototype::id);
        return entity.subindex < prototype.primitives.size() && prototype.primitives[entity.subindex].area_light == scene::LightId{entity.id};
    }

    math::Bounds3 ViewportInteraction::navigation_bounds() const noexcept {
        const scene::Scene& source = this->context.document.content.evaluated;
        const std::span<const math::Bounds3> gpu_bounds = this->context.gpu_scene.view().resolved_instance_bounds;
        math::Bounds3 bounds = math::Bounds3::empty();
        for (std::size_t index = 0; index != source.resources.instances.size(); ++index) {
            const scene::Instance& instance = source.resources.instances[index];
            if (!instance.visible) continue;
            const scene::Prototype& prototype = *std::ranges::find(source.resources.prototypes, instance.prototype, &scene::Prototype::id);
            if (std::ranges::all_of(prototype.primitives, [](const scene::Primitive& primitive) { return primitive.area_light.value != 0; })) continue;
            bounds.include(gpu_bounds[index]);
        }
        for (const scene::Volume& volume : source.resources.volumes) bounds.include(volume.bounds.transformed(volume.transform));
        return bounds;
    }

    math::Bounds3 ViewportInteraction::effective_scene_bounds() const noexcept {
        math::Bounds3 bounds = this->context.document.content.evaluated.view().bounds();
        bounds.include(this->context.gpu_scene.view().resolved_scene_bounds);
        return bounds;
    }

    std::optional<math::Bounds3> ViewportInteraction::entity_bounds(const SceneEntityReference entity) const noexcept {
        const scene::Scene& source = this->context.document.content.evaluated;
        if (entity.kind == SceneEntityKind::Instance || entity.kind == SceneEntityKind::AreaEmitter) {
            const scene::InstanceId instance_id{entity.kind == SceneEntityKind::Instance ? entity.id : entity.owner};
            const auto instance = std::ranges::find(source.resources.instances, instance_id, &scene::Instance::id);
            const std::size_t index = static_cast<std::size_t>(instance - source.resources.instances.begin());
            const std::span<const math::Bounds3> gpu_bounds = this->context.gpu_scene.view().resolved_instance_bounds;
            if (index < gpu_bounds.size() && gpu_bounds[index].valid()) return gpu_bounds[index];
            return source.view().bounds(std::array{instance_id});
        }
        const math::Bounds3 scene_bounds = this->effective_scene_bounds();
        const float extent               = std::max(scene_bounds.radius() * 0.05f, 0.05f);
        if (entity.kind == SceneEntityKind::Volume) {
            const scene::Volume& volume = *std::ranges::find(source.resources.volumes, scene::VolumeId{entity.id}, &scene::Volume::id);
            return volume.bounds.transformed(volume.transform);
        }
        if (entity.kind == SceneEntityKind::Camera) {
            const scene::Camera& camera = *std::ranges::find(source.resources.cameras, scene::CameraId{entity.id}, &scene::Camera::id);
            const math::Float3 position = camera.frame().position;
            return math::Bounds3{position - math::Float3{extent, extent, extent}, position + math::Float3{extent, extent, extent}};
        }
        if (entity.kind != SceneEntityKind::Light) return std::nullopt;
        const scene::Light& light = *std::ranges::find(source.resources.lights, scene::LightId{entity.id}, &scene::Light::id);
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
                } else
                    return scene_bounds;
            },
            light.data);
    }

    void ViewportInteraction::prune_selection() noexcept {
        std::erase_if(this->view.selection.selected, [this](const SceneEntityReference entity) { return !this->entity_exists(entity); });
        if (this->view.selection.hovered && !this->entity_exists(*this->view.selection.hovered)) this->view.selection.hovered.reset();
        if (this->view.selection.active && !this->entity_exists(*this->view.selection.active)) this->view.selection.active.reset();
    }

} // namespace spectra
