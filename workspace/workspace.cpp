module spectra.workspace;

import spectra.workspace.overlay;
import spectra.workspace.picker;
import std;

namespace spectra::workspace {
    struct WorkspaceInteraction {
        std::unique_ptr<Picker> picker{};
        std::unique_ptr<OverlayRenderer> overlay_renderer{};
    };

    ViewportCamera::ViewportCamera(const scene::CameraResource& camera, const scene::Bounds3 bounds) noexcept
        : camera(camera), focus(bounds.center()) {}

    void ViewportCamera::orbit(const float x_pixels, const float y_pixels) noexcept {
        constexpr float radians_per_pixel = 0.006f;
        scene::Float3 offset              = this->camera.frame().position - this->focus;
        const float distance              = offset.length();
        const scene::Float3 up            = this->navigation_up.normalized();
        scene::Float3 horizontal          = (offset - up * offset.dot(up)).normalized();
        const float yaw                   = -x_pixels * radians_per_pixel;
        horizontal                        = horizontal * std::cos(yaw) + up.cross(horizontal) * std::sin(yaw);
        const float pitch                 = std::clamp(
            std::asin(std::clamp(offset.normalized().dot(up), -1.0f, 1.0f)) + y_pixels * radians_per_pixel,
            -std::numbers::pi_v<float> * 0.49f,
            std::numbers::pi_v<float> * 0.49f);
        const scene::Float3 direction = horizontal.normalized() * std::cos(pitch) + up * std::sin(pitch);
        this->camera.transform        = scene::Transform::look_at(this->focus + direction * distance, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::pan(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
        const scene::CameraFrame frame = this->camera.frame();
        const float distance           = (frame.position - this->focus).length();
        const float world_per_pixel    = std::visit(
            [distance, viewport_height](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PerspectiveCameraData>)
                    return 2.0f * distance * std::tan(data.vertical_fov * std::numbers::pi_v<float> / 360.0f) / viewport_height;
                else
                    return (data.screen.maximum.y - data.screen.minimum.y) / viewport_height;
            },
            this->camera.data);
        const scene::Float3 movement = frame.right * (-x_pixels * world_per_pixel) + frame.up * (y_pixels * world_per_pixel);
        this->focus                   = this->focus + movement;
        this->camera.transform       = scene::Transform::look_at(frame.position + movement, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::zoom(const float steps) noexcept {
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->camera.data)) {
            const float scale = std::pow(0.88f, steps);
            const scene::Float2 center{
                (orthographic->screen.minimum.x + orthographic->screen.maximum.x) * 0.5f,
                (orthographic->screen.minimum.y + orthographic->screen.maximum.y) * 0.5f,
            };
            orthographic->screen.minimum = {
                center.x + (orthographic->screen.minimum.x - center.x) * scale,
                center.y + (orthographic->screen.minimum.y - center.y) * scale,
            };
            orthographic->screen.maximum = {
                center.x + (orthographic->screen.maximum.x - center.x) * scale,
                center.y + (orthographic->screen.maximum.y - center.y) * scale,
            };
            ++this->revision;
            return;
        }
        const scene::CameraFrame frame = this->camera.frame();
        const scene::Float3 offset     = frame.position - this->focus;
        const float distance           = std::clamp(offset.length() * std::pow(0.88f, steps), 0.01f, 1000000.0f);
        this->camera.transform         = scene::Transform::look_at(this->focus + offset.normalized() * distance, this->focus, this->navigation_up);
        ++this->revision;
    }

    void ViewportCamera::frame(const scene::Bounds3 bounds, const float aspect) noexcept {
        const scene::CameraFrame camera_frame = this->camera.frame();
        const scene::Float3 target            = bounds.center();
        const scene::Float3 direction         = (camera_frame.position - this->focus).normalized();
        if (scene::OrthographicCameraData* orthographic = std::get_if<scene::OrthographicCameraData>(&this->camera.data)) {
            const float half_height = bounds.radius() * 1.1f * std::max(1.0f, 1.0f / aspect);
            const float half_width  = half_height * aspect;
            const float distance    = std::max((camera_frame.position - this->focus).length(), bounds.radius() * 3.0f);
            orthographic->screen    = {{-half_width, -half_height}, {half_width, half_height}};
            orthographic->far_plane = std::max(orthographic->far_plane, distance + bounds.radius() * 4.0f);
            this->focus             = target;
            this->camera.transform  = scene::Transform::look_at(target + direction * distance, target, this->navigation_up);
            ++this->revision;
            return;
        }
        scene::PerspectiveCameraData& perspective = std::get<scene::PerspectiveCameraData>(this->camera.data);
        const float horizontal_fov                = 2.0f * std::atan(std::tan(perspective.vertical_fov * std::numbers::pi_v<float> / 360.0f) * aspect);
        const float limiting_fov                  = std::min(perspective.vertical_fov * std::numbers::pi_v<float> / 180.0f, horizontal_fov);
        const float distance                      = bounds.radius() / std::sin(limiting_fov * 0.5f) * 1.1f;
        this->focus                               = target;
        this->camera.transform                    = scene::Transform::look_at(target + direction * distance, target, this->navigation_up);
        perspective.far_plane                     = std::max(perspective.far_plane, distance + bounds.radius() * 4.0f);
        ++this->revision;
    }

    void ViewportCamera::align(const scene::Float3 direction, const scene::Bounds3 bounds, const float aspect) noexcept {
        this->focus         = bounds.center();
        this->navigation_up = std::abs(direction.y) > 0.9f ? scene::Float3{0.0f, 0.0f, -1.0f} : scene::Float3{0.0f, 1.0f, 0.0f};
        this->camera.transform = scene::Transform::look_at(this->focus + direction.normalized() * bounds.radius() * 3.0f, this->focus, this->navigation_up);
        this->frame(bounds, aspect);
    }

    Workspace::Workspace(GpuDevice& gpu, const std::filesystem::path& shader_directory, const std::filesystem::path& scene_path, const std::optional<std::filesystem::path>& plugin_path, const std::uint32_t frames_in_flight) : gpu(&gpu), shader_directory(shader_directory), frame_count(frames_in_flight), scene(this->scene_storage) {
        if (plugin_path)
            this->open_plugin(*plugin_path);
        else
            this->open_scene(scene_path);
    }

    Workspace::~Workspace() = default;

    void Workspace::destroy_renderers() noexcept {
        this->interaction.reset();
        this->pathtracer_renderer.reset();
        this->rasterizer_renderer.reset();
        this->raster_scene.reset();
        this->gpu_assets.reset();
    }

    void Workspace::rebuild_renderers() {
        this->gpu_assets            = std::make_unique<render::GpuAssetCache>(*this->gpu, this->scene_storage.view());
        this->raster_scene          = std::make_unique<rasterizer::RasterScene>(*this->gpu, *this->gpu_assets, this->scene_storage.view());
        this->rasterizer_renderer   = std::make_unique<rasterizer::Rasterizer>(*this->gpu, *this->raster_scene, this->shader_directory);
        this->pathtracer_renderer   = std::make_unique<pathtracer::PathTracer>(*this->gpu, *this->gpu_assets, this->scene_storage.view(), this->shader_directory, this->frame_count);
        this->interaction           = std::make_unique<WorkspaceInteraction>();
        this->interaction->picker   = std::make_unique<Picker>(*this->gpu, *this->raster_scene, this->shader_directory, this->frame_count);
        this->interaction->overlay_renderer = std::make_unique<OverlayRenderer>(*this->gpu, *this->raster_scene, this->shader_directory);
        this->synchronized_revision = this->scene_storage.revision().value;
        this->pending_pathtracer_changes = scene::SceneChange::None;
        this->scene_storage.acknowledge_changes();
        this->viewport_camera = ViewportCamera{this->scene_storage.camera(), this->scene_storage.view().bounds()};
        const scene::Film& film = this->scene_storage.film();
        this->viewport_aspect = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        this->axes_plane = AxesPlane::Xz;
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
        this->selection = {};
        this->undo_history.clear();
        this->redo_history.clear();
        this->transform_edit.reset();
        this->current_edit_serial = 0;
        this->saved_edit_serial   = 0;
        this->next_edit_serial    = 1;
        this->pathtracer_paused   = false;
    }

    void Workspace::open_scene(const std::filesystem::path& path) {
        this->gpu->wait_idle();
        this->destroy_renderers();
        this->scene_plugin.reset();
        this->scene_storage = scene::load_scene(path);
        this->source_path   = path;
        this->provider      = SceneProvider::File;
        this->camera_source = CameraSource::Viewport;
        this->rebuild_renderers();
    }

    void Workspace::open_plugin(const std::filesystem::path& path) {
        this->gpu->wait_idle();
        this->destroy_renderers();
        this->scene_plugin.reset();
        this->scene_storage       = {};
        this->scene_storage.name  = path.stem().string();
        this->scene_plugin        = std::make_unique<plugin::PluginHost>(path, this->scene_storage);
        this->source_path         = path;
        this->provider            = SceneProvider::Plugin;
        this->camera_source       = CameraSource::Viewport;
        this->rebuild_renderers();
    }

    void Workspace::save() {
        scene::save_scene(this->scene_storage, this->source_path, this->source_path);
        this->saved_edit_serial = this->current_edit_serial;
    }

    void Workspace::save_as(const std::filesystem::path& path) {
        scene::save_scene(this->scene_storage, path, this->source_path);
        this->source_path       = path;
        this->saved_edit_serial = this->current_edit_serial;
    }

    void Workspace::begin_frame(const std::uint32_t frame_index) {
        const PickResult result = this->interaction->picker->consume(frame_index);
        if (!result.available) return;
        std::optional<scene::InstanceId> instance{};
        if (result.instance_index) instance = this->gpu_assets->source_instances[*result.instance_index];
        if (!result.select) {
            this->selection.hovered = instance;
            return;
        }
        if (!instance) {
            if (!result.additive) this->clear_selection();
            return;
        }
        this->select_instance(*instance, result.additive);
    }

    void Workspace::update(const double seconds) {
        if (this->scene_plugin && this->scene_plugin->controls().running) this->scene_plugin->advance(seconds);
    }

    void Workspace::prepare(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        const scene::SceneRevision revision = this->scene_storage.revision();
        const bool scene_changed = revision.value != this->synchronized_revision;
        if (scene_changed) {
            this->pending_pathtracer_changes = this->pending_pathtracer_changes | revision.changes;
            this->gpu_assets->synchronize(this->scene_storage.view(), command_buffer);
            this->raster_scene->synchronize(this->scene_storage.view(), command_buffer);
            this->synchronized_revision = revision.value;
            this->prune_selection();
        }
        if (this->mode == RenderMode::PathTracer && this->pending_pathtracer_changes != scene::SceneChange::None) {
            scene::SceneView path_scene = this->scene_storage.view();
            path_scene.revision.changes = this->pending_pathtracer_changes;
            this->pathtracer_renderer->synchronize(path_scene, command_buffer);
            if ((this->pending_pathtracer_changes & (scene::SceneChange::Geometry | scene::SceneChange::Transform | scene::SceneChange::Texture | scene::SceneChange::Material | scene::SceneChange::Light | scene::SceneChange::Medium | scene::SceneChange::Volume | scene::SceneChange::Camera | scene::SceneChange::Film | scene::SceneChange::Sampler | scene::SceneChange::Transport)) != scene::SceneChange::None)
                this->pathtracer_renderer->reset_accumulation();
            this->pending_pathtracer_changes = scene::SceneChange::None;
        }
        const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed = aspect != this->viewport_aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->viewport_aspect = aspect;
        const bool camera_changed =
            aspect_changed ||
            this->camera_source != this->synchronized_camera_source ||
            (this->camera_source == CameraSource::Viewport && (scene_camera_changed || this->viewport_camera.revision != this->synchronized_viewport_camera_revision)) ||
            (this->camera_source == CameraSource::Scene && this->scene_storage.camera().revision != this->synchronized_scene_camera_revision);
        if (camera_changed) this->camera_changed();
        if (scene_changed) this->scene_storage.acknowledge_changes();
        if (this->mode == RenderMode::Rasterizer)
            this->rasterizer_renderer->prepare(extent);
        else
            this->pathtracer_renderer->prepare(extent);
    }

    void Workspace::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        if (this->mode == RenderMode::Rasterizer)
            this->rasterizer_renderer->record(command_buffer);
        else if (!this->pathtracer_paused && this->pathtracer_renderer->accumulated_samples() < this->scene_storage.sampler().samples_per_pixel)
            this->pathtracer_renderer->record(command_buffer, frame_index);
        this->interaction->picker->record(command_buffer, frame_index);
    }

    void Workspace::record_overlays(const vk::raii::CommandBuffer& command_buffer, const vk::Image target_image, const vk::ImageView target_view, const vk::Extent2D extent, const bool show_axes) {
        std::vector<std::uint32_t> selected_indices{};
        std::vector<std::uint32_t> active_indices{};
        std::vector<std::uint32_t> hovered_indices{};
        const auto collect = [this](const scene::InstanceId instance, std::vector<std::uint32_t>& destination) {
            for (std::uint32_t gpu_instance = 0; gpu_instance < this->gpu_assets->source_instances.size(); ++gpu_instance)
                if (this->gpu_assets->source_instances[gpu_instance] == instance) destination.push_back(gpu_instance);
        };
        for (const scene::InstanceId instance : this->selection.selected_instances) collect(instance, selected_indices);
        if (this->selection.active) collect(*this->selection.active, active_indices);
        if (this->selection.hovered) collect(*this->selection.hovered, hovered_indices);
        const vk::Rect2D render_region{{0, 0}, extent};
        this->interaction->overlay_renderer->record(command_buffer, target_image, target_view, extent, render_region, selected_indices, active_indices, hovered_indices, std::to_underlying(this->camera_source == CameraSource::Scene ? AxesPlane::Xz : this->axes_plane), show_axes, this->overlays_visible);
    }

    render::RenderOutput Workspace::output() const noexcept {
        if (this->mode == RenderMode::Rasterizer) return this->rasterizer_renderer->output();
        return this->pathtracer_renderer->output();
    }

    std::string_view Workspace::provider_name() const noexcept {
        if (this->scene_plugin) return this->scene_plugin->name;
        return this->scene_storage.name;
    }

    bool Workspace::dirty() const noexcept {
        return this->current_edit_serial != this->saved_edit_serial || (this->transform_edit && this->transform_edit->before != this->transform_edit->after);
    }

    void Workspace::reset_accumulation() noexcept {
        this->pathtracer_renderer->reset_accumulation();
    }

    std::uint32_t Workspace::accumulated_path_samples() const noexcept {
        return this->pathtracer_renderer->accumulated_samples();
    }

    render::RenderReadback Workspace::readback() {
        return this->pathtracer_renderer->readback();
    }

    PlaybackControls Workspace::playback_controls() const {
        const plugin::Controls controls = this->scene_plugin->controls();
        return {
            controls.running,
            controls.can_start,
            controls.can_stop,
            controls.can_advance};
    }

    TimelineState Workspace::timeline() const {
        const plugin::Timeline timeline = this->scene_plugin->timeline();
        return {
            timeline.seconds};
    }

    void Workspace::start_playback() {
        this->scene_plugin->start();
    }

    void Workspace::stop_playback() {
        this->scene_plugin->stop();
    }

    void Workspace::advance_playback(const double seconds) {
        this->scene_plugin->advance(seconds);
    }

    const scene::CameraResource& Workspace::active_camera() const noexcept {
        return this->display_camera;
    }

    void Workspace::camera_changed() noexcept {
        this->display_camera =
            this->camera_source == CameraSource::Scene
                ? this->scene_storage.camera()
                : this->viewport_camera.camera;
        std::visit(
            [this](auto& data) {
                const float center_x = (data.screen.minimum.x + data.screen.maximum.x) * 0.5f;
                const float half_height = (data.screen.maximum.y - data.screen.minimum.y) * 0.5f;
                const float half_width = half_height * this->viewport_aspect;
                data.screen.minimum.x = center_x - half_width;
                data.screen.maximum.x = center_x + half_width;
            },
            this->display_camera.data);
        this->raster_scene->camera = this->display_camera;
        this->pathtracer_renderer->change_camera(this->display_camera);
        this->synchronized_camera_source = this->camera_source;
        this->synchronized_viewport_camera_revision = this->viewport_camera.revision;
        this->synchronized_scene_camera_revision = this->scene_storage.camera().revision;
    }

    void Workspace::orbit_viewport_camera(const float x_pixels, const float y_pixels) noexcept {
        this->viewport_camera.orbit(x_pixels, y_pixels);
        this->axes_plane      = AxesPlane::Xz;
        this->camera_source   = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::pan_viewport_camera(const float x_pixels, const float y_pixels, const float viewport_height) noexcept {
        this->viewport_camera.pan(x_pixels, y_pixels, viewport_height);
        this->camera_source   = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::zoom_viewport_camera(const float steps) noexcept {
        this->viewport_camera.zoom(steps);
        this->camera_source   = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::frame_scene(const float aspect) noexcept {
        this->viewport_camera.frame(this->scene_storage.view().bounds(), aspect);
        this->camera_source   = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::frame_selection(const float aspect) noexcept {
        if (const std::optional<scene::Bounds3> bounds = this->scene_storage.view().bounds(this->selection.selected_instances))
            this->viewport_camera.frame(*bounds, aspect);
        else
            this->viewport_camera.frame(this->scene_storage.view().bounds(), aspect);
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::view_axis(const scene::Float3 direction, const float aspect) noexcept {
        const std::optional<scene::Bounds3> selected = this->scene_storage.view().bounds(this->selection.selected_instances);
        this->viewport_camera.align(direction, selected.value_or(this->scene_storage.view().bounds()), aspect);
        if (std::abs(direction.x) > 0.5f) this->axes_plane = AxesPlane::Yz;
        else if (std::abs(direction.z) > 0.5f) this->axes_plane = AxesPlane::Xy;
        else this->axes_plane = AxesPlane::Xz;
        this->camera_source = CameraSource::Viewport;
        this->camera_changed();
    }

    void Workspace::select_instance(const scene::InstanceId instance, const bool additive) {
        const std::vector<scene::InstanceId>::iterator found = std::ranges::find(this->selection.selected_instances, instance);
        if (!additive) {
            this->selection.selected_instances.assign(1, instance);
            this->selection.active = instance;
            return;
        }
        if (found == this->selection.selected_instances.end()) {
            this->selection.selected_instances.push_back(instance);
            this->selection.active = instance;
            return;
        }
        this->selection.selected_instances.erase(found);
        if (this->selection.selected_instances.empty())
            this->selection.active.reset();
        else
            this->selection.active = this->selection.selected_instances.back();
    }

    void Workspace::clear_selection() noexcept {
        this->selection.selected_instances.clear();
        this->selection.active.reset();
        this->selection.hovered.reset();
    }

    void Workspace::clear_hover() noexcept {
        this->selection.hovered.reset();
    }

    void Workspace::request_pick(const float x, const float y, const bool select, const bool additive) noexcept {
        this->interaction->picker->request(PickRequest{x, y, select, additive});
    }

    void Workspace::begin_transform_edit(const scene::InstanceId instance) {
        if (this->scene_plugin || this->transform_edit) return;
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->scene_storage.resources.instances, instance, &scene::Instance::id);
        this->transform_edit = TransformEdit{
            .instance = instance,
            .before = found->transform,
            .after = found->transform,
        };
    }

    void Workspace::update_transform_edit(scene::Transform transform) {
        if (!this->transform_edit) return;
        this->transform_edit->after = transform;
        scene::SceneWriter{this->scene_storage}.update_transform(this->transform_edit->instance, std::move(transform));
    }

    void Workspace::finish_transform_edit() {
        if (!this->transform_edit) return;
        if (this->transform_edit->before != this->transform_edit->after) this->push_edit(*this->transform_edit);
        this->transform_edit.reset();
    }

    bool Workspace::can_undo() const noexcept {
        return !this->undo_history.empty();
    }

    bool Workspace::can_redo() const noexcept {
        return !this->redo_history.empty();
    }

    void Workspace::push_edit(TransformEdit edit) {
        edit.before_serial        = this->current_edit_serial;
        edit.after_serial         = this->next_edit_serial++;
        this->current_edit_serial = edit.after_serial;
        this->undo_history.push_back(std::move(edit));
        this->redo_history.clear();
    }

    void Workspace::apply_edit(const TransformEdit& edit, const bool before) {
        scene::SceneWriter{this->scene_storage}.update_transform(edit.instance, before ? edit.before : edit.after);
    }

    void Workspace::undo() {
        if (this->undo_history.empty()) return;
        TransformEdit edit = std::move(this->undo_history.back());
        this->undo_history.pop_back();
        this->apply_edit(edit, true);
        this->current_edit_serial = edit.before_serial;
        this->redo_history.push_back(std::move(edit));
    }

    void Workspace::redo() {
        if (this->redo_history.empty()) return;
        TransformEdit edit = std::move(this->redo_history.back());
        this->redo_history.pop_back();
        this->apply_edit(edit, false);
        this->current_edit_serial = edit.after_serial;
        this->undo_history.push_back(std::move(edit));
    }

    std::optional<std::uint32_t> Workspace::instance_index(const scene::InstanceId id) const noexcept {
        const std::vector<scene::Instance>::const_iterator found = std::ranges::find(this->scene_storage.resources.instances, id, &scene::Instance::id);
        if (found == this->scene_storage.resources.instances.end()) return std::nullopt;
        return static_cast<std::uint32_t>(found - this->scene_storage.resources.instances.begin());
    }

    void Workspace::prune_selection() noexcept {
        std::erase_if(this->selection.selected_instances, [this](const scene::InstanceId id) { return !this->instance_index(id); });
        if (this->selection.hovered && !this->instance_index(*this->selection.hovered)) this->selection.hovered.reset();
        if (this->selection.active && !this->instance_index(*this->selection.active)) this->selection.active.reset();
    }
} // namespace spectra::workspace
