module spectra.editor;

import spectra.editor.document;
import spectra.editor.platform.dialogs;
import spectra.editor.platform.presentation;
import spectra.editor.platform.window;
import spectra.editor.ui;
import spectra.editor.ui.imgui;
import spectra.editor.viewport;
import spectra.editor.viewport.picker;
import spectra.simulation.runtime;
import spectra.render;
import spectra.render.display;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import spectra.scene.io;
import std;
import vulkan;

namespace spectra::editor {
    struct Application {
        Application(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);

        Application(const Application&)            = delete;
        Application(Application&&)                 = delete;
        Application& operator=(const Application&) = delete;
        Application& operator=(Application&&)      = delete;

        void run();

        WindowPlatform platform;
        Dialogs dialogs;
        runtime::VulkanInstance instance;
        VulkanSurface surface;
        runtime::VulkanRuntime runtime;
        VulkanPresentation presentation;
        Document document;
        simulation::Runtime simulation;
        render::GpuScene gpu_scene;
        render::Engine render_engine;
        Viewport viewport;
        render::Compositor compositor;
        Picker picker;
        ImGuiBackend imgui;
        Ui ui;

        std::uint64_t synchronized_scene_revision{};

    private:
        void open_scene(const std::filesystem::path& path);
        void handle_dropped_scene_paths();
        void handle_actions(const Actions& actions);
        void begin_frame(std::uint32_t frame_slot_index);
        [[nodiscard]] bool confirm_scene_replacement();
        void replace_scene(const std::filesystem::path& path);
        void reload_scene();
        void destroy_rendering() noexcept;
        void rebuild_rendering();
        [[nodiscard]] bool prepare_rendering(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent, std::uint32_t frame_slot_index);
        void record_scene_frame(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent, std::uint32_t frame_slot_index, bool show_axes);
    };

    Application::Application(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) : platform{"Spectra", {1920, 1080}}, dialogs{platform}, instance{"Spectra", presentation_instance_extensions}, surface{platform, instance}, runtime{instance, *surface.surface}, presentation{platform, surface, runtime.device, runtime.frames}, simulation{runtime}, gpu_scene{runtime, shader_directory}, render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer, request.raster_display_mode}, viewport{document, simulation}, compositor{runtime, gpu_scene, shader_directory}, picker{runtime, gpu_scene, shader_directory}, imgui{platform, runtime, shader_directory}, ui{document, simulation, render_engine, viewport, picker, imgui} {
        if (request.scene_path) this->open_scene(*request.scene_path);
    }

    void Application::run() {
        while (true) {
            this->platform.poll_events();
            if (this->platform.take_close_request()) {
                try {
                    if (this->confirm_scene_replacement()) break;
                } catch (const std::exception& error) {
                    this->ui.notify(error.what(), true);
                }
            }
            this->handle_dropped_scene_paths();

            const std::optional<PresentedFrameContext> frame = this->presentation.begin_frame();
            if (!frame) {
                this->platform.wait_events();
                continue;
            }

            const bool viewport_ready = this->document.loaded;
            if (viewport_ready) {
                this->begin_frame(frame->frame.slot_index);
                if (this->compositor.resize(frame->presentation_target.extent)) this->imgui.bind_viewport(this->compositor.output());
            }

            this->imgui.begin_frame();
            const Actions actions              = this->ui.draw();
            this->platform.window_drag_regions = actions.window_drag_regions;
            this->handle_actions(actions);
            this->imgui.end_frame();

            if (this->document.loaded) {
                if (this->simulation.initialized()) try {
                        this->simulation.advance();
                    } catch (const std::exception& error) {
                        this->ui.notify(error.what(), true);
                    }
                if (!viewport_ready) {
                    static_cast<void>(this->compositor.resize(frame->presentation_target.extent));
                    this->imgui.bind_viewport(this->compositor.output());
                }
                const vk::Extent2D viewport_extent = this->compositor.output().image.extent;
                if (this->prepare_rendering(frame->frame.command_buffer, viewport_extent, frame->frame.slot_index)) this->record_scene_frame(frame->frame.command_buffer, viewport_extent, frame->frame.slot_index, actions.show_axes);
                this->compositor.prepare_sampling(frame->frame.command_buffer);
            }

            this->imgui.record(frame->frame.command_buffer, frame->frame.slot_index, frame->presentation_target.image, frame->presentation_target.view, frame->presentation_target.extent, frame->presentation_target.image_layout, vk::ImageLayout::ePresentSrcKHR);
            this->presentation.present_frame();
        }
    }

    void Application::open_scene(const std::filesystem::path& path) {
        scene::Scene next_scene    = scene::load_scene(path);
        Document previous_document = std::move(this->document);
        const bool previous_loaded = previous_document.loaded;
        const auto previous_view   = this->viewport.view;
        this->document             = {};
        this->destroy_rendering();
        this->simulation.destroy();
        try {
            this->document.authored  = std::move(next_scene);
            this->document.evaluated = this->document.authored;
            this->document.path      = path;
            if (this->document.authored.simulation) this->simulation.initialize(path, this->document.authored, this->document.evaluated);
            this->rebuild_rendering();
            this->viewport.initialize_from_scene();
            this->viewport.settings.entity_diagnostics.clear();
            this->document.loaded   = true;
            this->document.modified = false;
        } catch (...) {
            const std::exception_ptr open_error = std::current_exception();
            this->destroy_rendering();
            this->simulation.destroy();
            this->document                    = std::move(previous_document);
            this->document.loaded             = false;
            this->synchronized_scene_revision = 0;
            if (previous_loaded) {
                try {
                    if (this->document.authored.simulation) this->simulation.initialize(this->document.path, this->document.authored, this->document.evaluated);
                    this->rebuild_rendering();
                    this->viewport.initialize_from_scene();
                    this->viewport.view   = previous_view;
                    this->document.loaded = true;
                } catch (const std::exception& restore_error) {
                    this->destroy_rendering();
                    this->simulation.destroy();
                    std::string open_message{"unknown error"};
                    try {
                        std::rethrow_exception(open_error);
                    } catch (const std::exception& open_exception) {
                        open_message = open_exception.what();
                    }
                    throw std::runtime_error(std::format("Opening '{}' failed: {}; restoring the previous scene also failed: {}", path.string(), open_message, restore_error.what()));
                }
            }
            std::rethrow_exception(open_error);
        }
    }

    void Application::handle_dropped_scene_paths() {
        const std::vector<std::filesystem::path> paths = this->platform.take_dropped_paths();
        if (paths.empty()) return;
        try {
            if (paths.size() != 1u) throw std::runtime_error("Drop exactly one .spectra scene");
            this->replace_scene(paths.front());
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void Application::handle_actions(const Actions& actions) {
        if (actions.exit_application) this->platform.request_close();
        try {
            if (actions.open_scene_file) {
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_file()) this->replace_scene(*path);
            }
            if (actions.reload_scene) this->reload_scene();
            if (actions.save_scene) {
                this->document.save();
                this->ui.notify("Scene saved");
            }
            if (actions.save_scene_as) {
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_save_path(this->document.path)) {
                    this->document.save_as(*path);
                    this->ui.notify("Scene saved");
                }
            }
            if (actions.rebuild_simulation_rendering) {
                this->destroy_rendering();
                this->rebuild_rendering();
            }
            if (actions.renderer) this->render_engine.activate(*actions.renderer, this->document.evaluated.view());
            if (actions.raster_display_mode) this->render_engine.set_raster_display_mode(*actions.raster_display_mode);
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void Application::begin_frame(const std::uint32_t frame_slot_index) {
        this->gpu_scene.retire_frame(frame_slot_index);
        const render::GpuSceneView gpu_view = this->gpu_scene.view();
        this->viewport.synchronize_bounds(gpu_view.resolved_scene_bounds, gpu_view.resolved_instance_bounds);
        if (this->simulation.initialized()) this->simulation.resolve_telemetry(frame_slot_index);
        const Picker::PickResult pick = this->picker.take_pick_result(frame_slot_index);
        if (!pick.ready) return;
        std::optional<scene::EntityReference> entity{};
        if (pick.diagnostic_pick_index) entity = this->compositor.pick_entity(frame_slot_index, *pick.diagnostic_pick_index);
        if (!entity && pick.neural_field) entity = scene::EntityReference{*pick.neural_field};
        if (!entity && pick.entity) {
            if (pick.entity->kind == render::GpuAccelerationEntityKind::Primitive) {
                const render::GpuScenePrimitive& primitive = this->gpu_scene.view().primitives[pick.entity->resource_index];
                entity                                     = scene::EntityReference{this->document.evaluated.resources.instances[primitive.scene_instance_index].id};
            } else entity = scene::EntityReference{this->document.evaluated.resources.volumes[pick.entity->resource_index].id};
        }
        if (!pick.select) {
            this->viewport.view.selection.hovered = entity;
            return;
        }
        if (!entity) {
            if (!pick.additive) this->viewport.clear_selection();
            return;
        }
        this->viewport.select(*entity, pick.additive);
    }

    bool Application::confirm_scene_replacement() {
        if (!this->document.loaded || !this->document.modified) return true;
        const SceneReplacementDecision decision = this->dialogs.confirm_scene_replacement();
        if (decision == SceneReplacementDecision::Cancel) return false;
        if (decision == SceneReplacementDecision::Save) this->document.save();
        return true;
    }

    void Application::replace_scene(const std::filesystem::path& path) {
        if (path.extension() != ".spectra") throw std::runtime_error("Spectra accepts only .spectra scenes");
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(path);
    }

    void Application::reload_scene() {
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(this->document.path);
        this->ui.notify("Scene reloaded");
    }

    void Application::destroy_rendering() noexcept {
        this->picker.destroy_scene();
        this->render_engine.destroy();
        this->gpu_scene.destroy();
    }

    void Application::rebuild_rendering() {
        this->gpu_scene.initialize(this->document.evaluated, this->simulation.mesh_bindings(), this->simulation.sphere_set_bindings());
        const render::GpuSceneView gpu_view = this->gpu_scene.view();
        this->viewport.synchronize_bounds(gpu_view.resolved_scene_bounds, gpu_view.resolved_instance_bounds);
        this->render_engine.rebuild(this->document.evaluated.view());
        this->picker.initialize(this->document.evaluated.view());
        this->document.authored.acknowledge_changes();
        this->document.evaluated.acknowledge_changes();
        this->synchronized_scene_revision = this->document.evaluated.revision().number;
    }

    bool Application::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent, const std::uint32_t frame_slot_index) {
        render::GpuSceneUpdate gpu_update{};
        bool gpu_scene_synchronized{};
        if (const simulation::SimulationFrame* frame = this->simulation.acquire_frame()) {
            gpu_update             = this->gpu_scene.apply(*frame, this->document.evaluated.view(), command_buffer, frame_slot_index);
            gpu_scene_synchronized = true;
        }

        const scene::SceneRevision revision = this->document.evaluated.revision();
        const bool scene_changed            = revision.number != this->synchronized_scene_revision;
        if (scene_changed && !gpu_scene_synchronized) gpu_update = this->gpu_scene.synchronize(this->document.evaluated.view(), command_buffer, frame_slot_index);
        const scene::SceneChange scene_changes = (scene_changed ? revision.changes : scene::SceneChange::None) | gpu_update.scene_changes;
        if (scene_changes != scene::SceneChange::None || gpu_update.gpu_changes != render::GpuSceneChange::None) {
            scene::ResolvedSceneView synchronized_scene = this->document.evaluated.view();
            synchronized_scene.revision.changes         = scene_changes;
            this->picker.synchronize(synchronized_scene, gpu_update, command_buffer);
            this->render_engine.invalidate(scene_changes, gpu_update);
        }
        if (scene_changed) {
            this->synchronized_scene_revision = revision.number;
            this->viewport.prune_selection();
        }

        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->viewport.view.aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->viewport.view.aspect      = aspect;
        scene::ResourceRevision scene_camera_revision{};
        if (this->viewport.view.source == CameraSource::Scene) scene_camera_revision = std::ranges::find(this->document.evaluated.resources.cameras, this->viewport.view.scene_camera, &scene::Camera::id)->revision;
        const bool camera_changed = aspect_changed || this->viewport.view.source != this->viewport.view.synchronized_source || (this->viewport.view.source == CameraSource::Viewport && (scene_camera_changed || this->viewport.view.camera_revision != this->viewport.view.synchronized_camera_revision)) || (this->viewport.view.source == CameraSource::Scene && scene_camera_revision != this->viewport.view.synchronized_scene_camera_revision);
        if (camera_changed) this->viewport.camera_changed();

        if (scene_changed) this->document.evaluated.acknowledge_changes();
        if (this->document.authored.revision().changes != scene::SceneChange::None) this->document.authored.acknowledge_changes();
        const bool renderer_ready = this->render_engine.prepare(this->document.evaluated.view(), render::RenderView{this->viewport.view.render_camera, extent, this->viewport.view.render_camera_revision}, command_buffer);
        if (this->simulation.initialized()) this->simulation.record_telemetry(command_buffer, frame_slot_index);
        this->simulation.consume_frame();
        return renderer_ready;
    }

    void Application::record_scene_frame(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent, const std::uint32_t frame_slot_index, const bool show_axes) {
        this->render_engine.record(command_buffer, frame_slot_index);
        const render::RenderOutput output                  = this->render_engine.output();
        const std::optional<render::DepthBufferView> depth = this->render_engine.depth_buffer();
        render::EntityDiagnostics default_diagnostics{};
        const render::EntityDiagnostics& entity_diagnostics = this->viewport.view.selection.active ? this->viewport.settings.entity_diagnostics.try_emplace(*this->viewport.view.selection.active).first->second : default_diagnostics;
        std::optional<render::CameraReferenceRequest> camera_reference{};
        if (this->viewport.view.selection.active && std::holds_alternative<scene::CameraId>(this->viewport.view.selection.active->data)) {
            const scene::CameraId camera_id = std::get<scene::CameraId>(this->viewport.view.selection.active->data);
            if (const simulation::CameraReferenceImage* reference = this->simulation.camera_reference(camera_id)) {
                const scene::Camera& camera  = *std::ranges::find(this->document.evaluated.resources.cameras, camera_id, &scene::Camera::id);
                const bool viewing_reference = this->viewport.view.source == CameraSource::Scene && this->viewport.view.scene_camera == camera_id;
                const math::Float4 gate      = viewing_reference ? this->viewport.view.camera_gate.value_or(math::Float4{0.0f, 0.0f, 1.0f, 1.0f}) : math::Float4{0.0f, 0.0f, 1.0f, 1.0f};
                const float image_aspect     = static_cast<float>(reference->extent[0]) / static_cast<float>(reference->extent[1]);
                const float maximum_width    = gate.z * 0.28f;
                const float maximum_height   = gate.w * 0.28f;
                const float overlay_width    = std::min(maximum_width, maximum_height * static_cast<float>(extent.height) * image_aspect / static_cast<float>(extent.width));
                const float overlay_height   = overlay_width * static_cast<float>(extent.width) / (image_aspect * static_cast<float>(extent.height));
                const math::Float4 overlay_rect{
                    gate.x + 16.0f / static_cast<float>(extent.width),
                    gate.y + gate.w - overlay_height - 16.0f / static_cast<float>(extent.height),
                    overlay_width,
                    overlay_height,
                };
                camera_reference = render::CameraReferenceRequest{reference, &camera, overlay_rect, entity_diagnostics.camera_gt_overlay, entity_diagnostics.camera_gt_plane};
            }
        }

        std::vector<scene::InstanceId> selected_instances{};
        for (const scene::EntityReference entity : this->viewport.view.selection.selected) {
            if (const scene::InstanceId* instance = std::get_if<scene::InstanceId>(&entity.data)) selected_instances.emplace_back(*instance);
            else if (const scene::EntityReference::AreaEmitter* emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity.data)) selected_instances.emplace_back(emitter->instance);
        }
        const auto instance_id = [](const std::optional<scene::EntityReference>& entity) -> std::optional<scene::InstanceId> {
            if (!entity) return std::nullopt;
            if (const scene::InstanceId* instance = std::get_if<scene::InstanceId>(&entity->data)) return *instance;
            if (const scene::EntityReference::AreaEmitter* emitter = std::get_if<scene::EntityReference::AreaEmitter>(&entity->data)) return emitter->instance;
            return std::nullopt;
        };
        const render::OverlayRequest overlay_request{
            .selected_instances = selected_instances,
            .active_instance    = instance_id(this->viewport.view.selection.active),
            .hovered_instance   = instance_id(this->viewport.view.selection.hovered),
            .axes_plane         = this->viewport.view.source == CameraSource::Scene ? render::AxesPlane::Xz : this->viewport.view.axes_plane,
            .axes_visible       = show_axes,
            .outline_visible    = this->viewport.settings.guides_visible && this->viewport.settings.selection_outline,
        };
        this->compositor.record(command_buffer, render::DisplayRequest{
                                                    .renderer_output   = output,
                                                    .depth             = depth,
                                                    .scene             = this->document.evaluated.view(),
                                                    .camera            = this->viewport.view.render_camera,
                                                    .scene_camera_view = this->viewport.view.source == CameraSource::Scene ? std::optional{this->viewport.view.scene_camera} : std::nullopt,
                                                    .visualizations    = this->simulation.visualizations(),
                                                    .diagnostics =
                                                        render::DiagnosticRequest{
                                                            .scene_guides = this->viewport.settings.scene_guides,
                                                            .entity       = entity_diagnostics,
                                                            .selection    = {this->viewport.view.selection.selected, this->viewport.view.selection.active, this->viewport.view.selection.hovered},
                                                            .visible      = this->viewport.settings.guides_visible,
                                                        },
                                                    .camera_reference = camera_reference,
                                                    .overlay          = overlay_request,
                                                    .frame_slot_index = frame_slot_index,
                                                    .exposure         = this->viewport.settings.exposure,
                                                });
        this->picker.record(command_buffer, frame_slot_index, this->document.evaluated.view(), this->viewport.view.render_camera, *depth, this->compositor.diagnostic_pick_image());
    }

    void run(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        Application application{std::move(request), shader_directory, pathtracer_directory};
        application.run();
    }
} // namespace spectra::editor
