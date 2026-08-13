module spectra.editor;

import spectra.editor.platform.dialogs;
import spectra.editor.platform.presentation;
import spectra.editor.platform.window;
import spectra.editor.ui;
import spectra.editor.ui.imgui;
import spectra.editor.viewport.interaction;
import spectra.editor.viewport.picker;
import spectra.dynamics.runtime;
import spectra.render;
import spectra.render.composition;
import spectra.render.composition.diagnostics;
import spectra.render.composition.overlay;
import spectra.render.composition.visualization;
import spectra.render.display;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import spectra.scene.document;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra {
    struct EditorApplication {
        EditorApplication(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);

        EditorApplication(const EditorApplication&)            = delete;
        EditorApplication(EditorApplication&&)                 = delete;
        EditorApplication& operator=(const EditorApplication&) = delete;
        EditorApplication& operator=(EditorApplication&&)      = delete;

        void run();

        WindowPlatform platform;
        EditorDialogs dialogs;
        VulkanInstance instance;
        VulkanSurface surface;
        VulkanRuntime runtime;
        VulkanPresentation presentation;
        SceneDocument document;
        SceneDiagnosticSettings diagnostic_settings{};
        DynamicsRuntime dynamics;
        GpuScene gpu_scene;
        SceneDiagnosticRenderer diagnostics;
        RenderEngine render_engine;
        ViewportInteraction viewport;
        DisplayPass display;
        VisualizationRenderer visualization;
        ViewportOverlay overlay;
        ViewportPicker picker;
        ImGuiBackend imgui;
        EditorUi ui;

        struct {
            std::uint64_t synchronized_scene_revision{};
        } rendering;

    private:
        void open_scene(const std::filesystem::path& path);
        void handle_dropped_scene_paths();
        void handle_actions(const EditorActions& actions);
        void begin_frame(std::uint32_t frame_slot_index);
        [[nodiscard]] bool confirm_scene_replacement();
        void replace_scene(const std::filesystem::path& path);
        void reload_scene();
        void destroy_rendering() noexcept;
        void rebuild_rendering(scene::Scene& source_scene);
        [[nodiscard]] bool prepare_rendering(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
        void record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, bool show_axes);
    };

    EditorApplication::EditorApplication(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory)
        : platform{"Spectra", {1920, 1080}}, dialogs{platform}, instance{"Spectra", presentation_instance_extensions}, surface{platform, instance}, runtime{instance, *surface.surface}, presentation{platform, surface, runtime.graphics, runtime.frames}, dynamics{runtime}, gpu_scene{runtime, shader_directory}, diagnostics{runtime, gpu_scene, shader_directory}, render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, std::move(request.renderer), parse_raster_display_mode(request.raster_display_mode)}, viewport{document, dynamics, gpu_scene}, display{runtime, shader_directory}, visualization{runtime, shader_directory}, overlay{runtime, gpu_scene, shader_directory}, picker{runtime, gpu_scene, shader_directory}, imgui{platform, runtime, display, shader_directory}, ui{document, diagnostic_settings, dynamics, render_engine, viewport, picker, imgui} {
        this->display.initialize();
        this->diagnostics.initialize();
        this->imgui.initialize();
        if (request.scene_path) this->open_scene(*request.scene_path);
    }

    void EditorApplication::run() {
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

            if (this->document.content.loaded) {
                this->begin_frame(frame->frame.slot_index);
                this->imgui.resize_viewport(frame->presentation_target.extent);
            }

            this->imgui.begin_frame();
            const EditorActions actions        = this->ui.draw_editor_ui();
            this->platform.window_drag_regions = actions.window_drag_regions;
            this->handle_actions(actions);
            this->imgui.end_frame();

            if (this->document.content.loaded) {
                if (this->dynamics.initialized())
                    try {
                        this->dynamics.advance();
                    } catch (const std::exception& error) {
                        this->ui.notify(error.what(), true);
                    }
                this->imgui.resize_viewport(frame->presentation_target.extent);
                const vk::Extent2D viewport_extent = this->display.image.extent;
                if (this->prepare_rendering(frame->frame.command_buffer, viewport_extent)) {
                    this->render_engine.record(frame->frame.command_buffer, frame->frame.slot_index);
                    const RenderOutput output                  = this->render_engine.output();
                    const std::optional<DepthBufferView> depth = this->render_engine.depth_buffer();
                    record_render_composition(frame->frame.command_buffer, this->display,
                        RenderCompositionRequest{
                            .renderer_output        = output,
                            .depth                  = depth,
                            .scene                  = this->document.content.evaluated.view(),
                            .camera                 = this->viewport.view.render_camera,
                            .scene_camera_view      = this->viewport.view.source == CameraSource::Scene ? std::optional{this->viewport.view.render_camera.id} : std::nullopt,
                            .visualizations         = this->dynamics.visualizations(),
                            .visualization          = &this->visualization,
                            .diagnostics            = this->diagnostic_settings.enabled ? std::optional{SceneDiagnosticsComposition{this->diagnostics, this->diagnostic_settings, this->viewport.view.selection}} : std::nullopt,
                            .frame_slot_index       = frame->frame.slot_index,
                            .exposure               = this->ui.controls.exposure,
                            .compose_visualizations = true,
                        });
                    this->picker.record(frame->frame.command_buffer, frame->frame.slot_index, this->viewport.view.render_camera, *depth, this->diagnostic_settings.enabled ? &this->diagnostics.pick_image() : nullptr);
                    this->record_editor_overlays(frame->frame.command_buffer, actions.show_axes);
                }
            }

            this->imgui.record(frame->frame.command_buffer, frame->frame.slot_index, frame->presentation_target.image, frame->presentation_target.view, frame->presentation_target.extent, frame->presentation_target.image_layout, vk::ImageLayout::ePresentSrcKHR);
            this->presentation.present_frame();
        }
    }

    void EditorApplication::open_scene(const std::filesystem::path& path) {
        scene::Scene next_scene             = scene::load_scene(path);
        const bool previous_loaded          = this->document.content.loaded;
        const bool previous_modified        = this->document.content.modified;
        scene::Scene previous_source        = std::move(this->document.content.source);
        scene::Scene previous_evaluated     = std::move(this->document.content.evaluated);
        std::filesystem::path previous_path = std::move(this->document.content.path);
        this->document.content.loaded       = false;
        this->destroy_rendering();
        this->dynamics.destroy();
        try {
            this->document.content.source    = std::move(next_scene);
            this->document.content.evaluated = this->document.content.source;
            this->document.content.path      = path;
            if (this->document.content.source.dynamic_setup) this->dynamics.initialize(path, this->document.content.source);
            this->rebuild_rendering(this->document.content.source);
            this->viewport.initialize_from_scene();
            this->document.content.loaded   = true;
            this->document.content.modified = false;
        } catch (...) {
            const std::exception_ptr open_error = std::current_exception();
            this->destroy_rendering();
            this->dynamics.destroy();
            this->document.content.source               = std::move(previous_source);
            this->document.content.evaluated            = std::move(previous_evaluated);
            this->document.content.path                 = std::move(previous_path);
            this->document.content.modified             = previous_modified;
            this->document.content.loaded               = false;
            this->rendering.synchronized_scene_revision = 0;
            if (previous_loaded) {
                try {
                    if (this->document.content.source.dynamic_setup) this->dynamics.initialize(this->document.content.path, this->document.content.source);
                    this->rebuild_rendering(this->document.content.source);
                    this->viewport.initialize_from_scene();
                    this->document.content.loaded = true;
                } catch (const std::exception& restore_error) {
                    this->destroy_rendering();
                    this->dynamics.destroy();
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

    void EditorApplication::handle_dropped_scene_paths() {
        const std::vector<std::filesystem::path> paths = this->platform.take_dropped_paths();
        if (paths.empty()) return;
        try {
            if (paths.size() != 1u) throw std::runtime_error("Drop exactly one .spectra scene");
            this->replace_scene(paths.front());
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void EditorApplication::handle_actions(const EditorActions& actions) {
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
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_save_path(this->document.content.path)) {
                    this->document.save_as(*path);
                    this->ui.notify("Scene saved");
                }
            }
            if (actions.renderer) this->render_engine.activate(*actions.renderer, this->document.content.evaluated.view());
            if (actions.raster_display_mode) this->render_engine.set_raster_display_mode(*actions.raster_display_mode);
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    void EditorApplication::begin_frame(const std::uint32_t frame_slot_index) {
        this->gpu_scene.retire_frame(frame_slot_index);
        if (this->dynamics.initialized()) this->dynamics.resolve_telemetry(frame_slot_index);
        const ViewportPicker::PickResult pick = this->picker.take_pick_result(frame_slot_index);
        if (!pick.ready) return;
        std::optional<SceneEntityReference> entity{};
        if (pick.diagnostic_pick_index) entity = this->diagnostics.pick_entity(frame_slot_index, *pick.diagnostic_pick_index);
        if (!entity && pick.instance_id) entity = SceneEntityReference{SceneEntityKind::Instance, pick.instance_id->value};
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

    bool EditorApplication::confirm_scene_replacement() {
        if (!this->document.content.loaded || !this->document.content.modified) return true;
        const SceneReplacementDecision decision = this->dialogs.confirm_scene_replacement();
        if (decision == SceneReplacementDecision::Cancel) return false;
        if (decision == SceneReplacementDecision::Save) this->document.save();
        return true;
    }

    void EditorApplication::replace_scene(const std::filesystem::path& path) {
        if (path.extension() != ".spectra") throw std::runtime_error("Spectra accepts only .spectra scenes");
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(path);
    }

    void EditorApplication::reload_scene() {
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(this->document.content.path);
        this->ui.notify("Scene reloaded");
    }

    void EditorApplication::destroy_rendering() noexcept {
        this->picker.destroy_scene();
        this->overlay.destroy();
        this->render_engine.destroy();
        this->gpu_scene.destroy();
    }

    void EditorApplication::rebuild_rendering(scene::Scene& source_scene) {
        this->gpu_scene.initialize(source_scene, this->dynamics.mesh_bindings(), this->dynamics.sphere_set_bindings());
        this->render_engine.rebuild(this->document.content.evaluated.view());
        this->overlay.initialize();
        this->picker.initialize(this->document.content.evaluated.view());
        this->document.content.source.acknowledge_changes();
        this->document.content.evaluated.acknowledge_changes();
        this->rendering.synchronized_scene_revision = this->document.content.evaluated.revision().number;
    }

    bool EditorApplication::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        GpuSceneUpdate gpu_update{};
        bool gpu_scene_synchronized{};
        if (const dynamics::DynamicSnapshot* snapshot = this->dynamics.acquire_snapshot()) {
            gpu_update             = this->gpu_scene.apply(*snapshot, this->document.content.evaluated.view(), command_buffer);
            gpu_scene_synchronized = true;
        }

        const scene::SceneRevision revision = this->document.content.evaluated.revision();
        const bool scene_changed            = revision.number != this->rendering.synchronized_scene_revision;
        if (scene_changed && !gpu_scene_synchronized) gpu_update = this->gpu_scene.synchronize(this->document.content.evaluated.view(), command_buffer);
        const scene::SceneChange scene_changes = (scene_changed ? revision.changes : scene::SceneChange::None) | gpu_update.scene_changes;
        if (scene_changes != scene::SceneChange::None || gpu_update.gpu_changes != GpuSceneChange::None) {
            scene::SceneView synchronized_scene = this->document.content.evaluated.view();
            synchronized_scene.revision.changes = scene_changes;
            this->picker.synchronize(synchronized_scene, gpu_update, command_buffer);
            this->render_engine.invalidate(scene_changes, gpu_update);
        }
        if (scene_changed) {
            this->rendering.synchronized_scene_revision = revision.number;
            this->viewport.prune_selection();
        }

        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->viewport.view.aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->viewport.view.aspect      = aspect;
        const bool camera_changed       = aspect_changed || this->viewport.view.source != this->viewport.view.synchronized_source || (this->viewport.view.source == CameraSource::Viewport && (scene_camera_changed || this->viewport.view.camera_revision != this->viewport.view.synchronized_camera_revision)) || (this->viewport.view.source == CameraSource::Scene && this->document.content.source.camera().revision != this->viewport.view.synchronized_scene_camera_revision);
        if (camera_changed) this->viewport.camera_changed();

        if (scene_changed) this->document.content.evaluated.acknowledge_changes();
        if (this->document.content.source.revision().changes != scene::SceneChange::None) this->document.content.source.acknowledge_changes();
        const bool renderer_ready = this->render_engine.prepare(this->document.content.evaluated.view(), RenderView{this->viewport.view.render_camera, extent, this->viewport.view.render_camera_revision}, command_buffer);
        if (this->dynamics.initialized()) {
            this->dynamics.record_telemetry(command_buffer, this->runtime.frames.frame.current_slot_index);
        }
        this->dynamics.consume_snapshot();
        return renderer_ready;
    }

    void EditorApplication::record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, const bool show_axes) {
        std::vector<scene::InstanceId> selected_instances{};
        for (const SceneEntityReference entity : this->viewport.view.selection.selected) {
            if (entity.kind == SceneEntityKind::Instance)
                selected_instances.emplace_back(entity.id);
            else if (entity.kind == SceneEntityKind::AreaEmitter)
                selected_instances.emplace_back(entity.owner);
        }
        const auto instance_id = [](const std::optional<SceneEntityReference> entity) -> std::optional<scene::InstanceId> {
            if (!entity) return std::nullopt;
            if (entity->kind == SceneEntityKind::Instance) return scene::InstanceId{entity->id};
            if (entity->kind == SceneEntityKind::AreaEmitter) return scene::InstanceId{entity->owner};
            return std::nullopt;
        };
        this->overlay.record(command_buffer, this->display, this->viewport.view.render_camera,
            ViewportOverlayState{
                .selected_instances = selected_instances,
                .active_instance    = instance_id(this->viewport.view.selection.active),
                .hovered_instance   = instance_id(this->viewport.view.selection.hovered),
                .axes_plane         = std::to_underlying(this->viewport.view.source == CameraSource::Scene ? AxesPlane::Xz : this->viewport.view.axes_plane),
                .axes_visible       = show_axes,
                .outline_visible    = this->viewport.view.overlays_visible,
            });
    }

    void run_editor(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        EditorApplication application{std::move(request), shader_directory, pathtracer_directory};
        application.run();
    }
} // namespace spectra
