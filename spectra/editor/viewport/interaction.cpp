module spectra.editor;

import :viewport.interaction;

import std;

namespace spectra {
    ViewportInteraction::ViewportInteraction(SceneDocument& document, DynamicWorld& dynamics) noexcept : context{document, dynamics} {}

    void ViewportInteraction::initialize_from_scene() {
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
        this->frame_viewport_camera(this->context.document.content.evaluated.view().bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::frame_selection(const float aspect) noexcept {
        if (const std::optional<math::Bounds3> bounds = this->context.document.content.evaluated.view().bounds(this->view.selection.selected_instances))
            this->frame_viewport_camera(*bounds, aspect);
        else
            this->frame_viewport_camera(this->context.document.content.evaluated.view().bounds(), aspect);
        this->view.source = CameraSource::Viewport;
        this->camera_changed();
    }

    void ViewportInteraction::view_axis(const math::Float3 direction, const float aspect) noexcept {
        const std::optional<math::Bounds3> selected = this->context.document.content.evaluated.view().bounds(this->view.selection.selected_instances);
        const math::Bounds3 bounds                  = selected.value_or(this->context.document.content.evaluated.view().bounds());
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

    void ViewportInteraction::select_instance(const scene::InstanceId instance_id, const bool additive) {
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

    void ViewportInteraction::clear_selection() noexcept {
        this->view.selection.selected_instances.clear();
        this->view.selection.active_instance.reset();
        this->view.selection.hovered_instance.reset();
    }

    void ViewportInteraction::clear_hover() noexcept {
        this->view.selection.hovered_instance.reset();
    }

    std::pair<std::optional<std::uint64_t>, bool> ViewportInteraction::debug_object_at(const float normalized_x, const float normalized_y) const noexcept {
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

    std::optional<std::uint32_t> ViewportInteraction::instance_index(const scene::InstanceId instance_id) const noexcept {
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->context.document.content.source.resources.instances, instance_id, &scene::Instance::id);
        if (found == this->context.document.content.source.resources.instances.end()) return std::nullopt;
        return static_cast<std::uint32_t>(found - this->context.document.content.source.resources.instances.begin());
    }

    void ViewportInteraction::prune_selection() noexcept {
        std::erase_if(this->view.selection.selected_instances, [this](const scene::InstanceId id) { return !this->instance_index(id); });
        if (this->view.selection.hovered_instance && !this->instance_index(*this->view.selection.hovered_instance)) this->view.selection.hovered_instance.reset();
        if (this->view.selection.active_instance && !this->instance_index(*this->view.selection.active_instance)) this->view.selection.active_instance.reset();
    }

} // namespace spectra
