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
import spectra.render.composition.neural_field;
import spectra.render.composition.overlay;
import spectra.render.composition.visualization;
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
        EditorViewportSettings viewport_settings{};
        DynamicsRuntime dynamics;
        GpuScene gpu_scene;
        SceneDiagnosticRenderer diagnostics;
        RenderEngine render_engine;
        ViewportInteraction viewport;
        RenderCompositor compositor;
        NeuralFieldRenderer neural_field;
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
        void rebuild_rendering();
        [[nodiscard]] bool prepare_rendering(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent, std::uint32_t frame_slot_index);
        void record_scene_frame(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent, std::uint32_t frame_slot_index, bool show_axes);
    };

    EditorApplication::EditorApplication(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory)
        : platform{"Spectra", {1920, 1080}}, dialogs{platform}, instance{"Spectra", presentation_instance_extensions}, surface{platform, instance}, runtime{instance, *surface.surface}, presentation{platform, surface, runtime.graphics, runtime.frames}, dynamics{runtime}, gpu_scene{runtime, shader_directory}, diagnostics{runtime, gpu_scene, shader_directory}, render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, std::move(request.renderer), parse_raster_display_mode(request.raster_display_mode)}, viewport{document, dynamics, gpu_scene}, compositor{runtime, shader_directory}, neural_field{runtime, gpu_scene, shader_directory}, visualization{runtime, gpu_scene, shader_directory}, overlay{runtime, gpu_scene, shader_directory}, picker{runtime, gpu_scene, shader_directory}, imgui{platform, runtime, compositor, shader_directory}, ui{document, viewport_settings, dynamics, render_engine, viewport, picker, imgui} {
        this->compositor.initialize();
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
                const vk::Extent2D viewport_extent = this->compositor.target().image.extent;
                if (this->prepare_rendering(frame->frame.command_buffer, viewport_extent, frame->frame.slot_index)) this->record_scene_frame(frame->frame.command_buffer, viewport_extent, frame->frame.slot_index, actions.show_axes);
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
            this->viewport_settings.entity_diagnostics.clear();
            if (this->document.content.source.dynamic_setup) this->dynamics.initialize(path, this->document.content.source, this->document.content.evaluated);
            this->rebuild_rendering();
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
                    if (this->document.content.source.dynamic_setup) this->dynamics.initialize(this->document.content.path, this->document.content.source, this->document.content.evaluated);
                    this->rebuild_rendering();
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
            if (actions.rebuild_dynamic_rendering) {
                this->destroy_rendering();
                this->rebuild_rendering();
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
        if (!entity && pick.neural_field) entity = SceneEntityReference{*pick.neural_field};
        if (!entity && pick.entity) {
            if (pick.entity->kind == GpuAccelerationEntityKind::Primitive) {
                const GpuScenePrimitive& primitive = this->gpu_scene.view().primitives[pick.entity->resource_index];
                entity = SceneEntityReference{this->document.content.evaluated.resources.instances[primitive.scene_instance_index].id};
            } else
                entity = SceneEntityReference{this->document.content.evaluated.resources.volumes[pick.entity->resource_index].id};
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

    void EditorApplication::rebuild_rendering() {
        this->gpu_scene.initialize(this->document.content.evaluated, this->dynamics.mesh_bindings(), this->dynamics.sphere_set_bindings());
        this->render_engine.rebuild(this->document.content.evaluated.view());
        this->overlay.initialize();
        this->picker.initialize(this->document.content.evaluated.view());
        this->document.content.source.acknowledge_changes();
        this->document.content.evaluated.acknowledge_changes();
        this->rendering.synchronized_scene_revision = this->document.content.evaluated.revision().number;
    }

    bool EditorApplication::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent, const std::uint32_t frame_slot_index) {
        GpuSceneUpdate gpu_update{};
        bool gpu_scene_synchronized{};
        if (const dynamics::DynamicSnapshot* snapshot = this->dynamics.acquire_snapshot()) {
            gpu_update             = this->gpu_scene.apply(*snapshot, this->document.content.evaluated.view(), command_buffer, frame_slot_index);
            gpu_scene_synchronized = true;
        }

        const scene::SceneRevision revision = this->document.content.evaluated.revision();
        const bool scene_changed            = revision.number != this->rendering.synchronized_scene_revision;
        if (scene_changed && !gpu_scene_synchronized) gpu_update = this->gpu_scene.synchronize(this->document.content.evaluated.view(), command_buffer, frame_slot_index);
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
        scene::ResourceRevision scene_camera_revision{};
        if (this->viewport.view.source == CameraSource::Scene) scene_camera_revision = std::ranges::find(this->document.content.evaluated.resources.cameras, this->viewport.view.scene_camera, &scene::Camera::id)->revision;
        const bool camera_changed       = aspect_changed || this->viewport.view.source != this->viewport.view.synchronized_source || (this->viewport.view.source == CameraSource::Viewport && (scene_camera_changed || this->viewport.view.camera_revision != this->viewport.view.synchronized_camera_revision)) || (this->viewport.view.source == CameraSource::Scene && scene_camera_revision != this->viewport.view.synchronized_scene_camera_revision);
        if (camera_changed) this->viewport.camera_changed();

        if (scene_changed) this->document.content.evaluated.acknowledge_changes();
        if (this->document.content.source.revision().changes != scene::SceneChange::None) this->document.content.source.acknowledge_changes();
        const bool renderer_ready = this->render_engine.prepare(this->document.content.evaluated.view(), RenderView{this->viewport.view.render_camera, extent, this->viewport.view.render_camera_revision}, command_buffer);
        if (this->dynamics.initialized()) this->dynamics.record_telemetry(command_buffer, frame_slot_index);
        this->dynamics.consume_snapshot();
        return renderer_ready;
    }

    void EditorApplication::record_scene_frame(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent, const std::uint32_t frame_slot_index, const bool show_axes) {
        this->render_engine.record(command_buffer, frame_slot_index);
        const RenderOutput output                  = this->render_engine.output();
        const std::optional<DepthBufferView> depth = this->render_engine.depth_buffer();
        EntityDiagnostics default_diagnostics{};
        const EntityDiagnostics& entity_diagnostics = this->viewport.view.selection.active ? this->viewport_settings.entity_diagnostics.try_emplace(*this->viewport.view.selection.active).first->second : default_diagnostics;
        std::optional<CameraReferenceVisualization> camera_reference{};
        if (this->viewport.view.selection.active && std::holds_alternative<scene::CameraId>(this->viewport.view.selection.active->data)) {
            const scene::CameraId camera_id = std::get<scene::CameraId>(this->viewport.view.selection.active->data);
            if (const dynamics::CameraReferenceImage* reference = this->dynamics.camera_reference(camera_id)) {
                const scene::Camera& camera          = *std::ranges::find(this->document.content.evaluated.resources.cameras, camera_id, &scene::Camera::id);
                const bool viewing_reference        = this->viewport.view.source == CameraSource::Scene && this->viewport.view.scene_camera == camera_id;
                const math::Float4 gate              = viewing_reference ? this->viewport.view.camera_gate.value_or(math::Float4{0.0f, 0.0f, 1.0f, 1.0f}) : math::Float4{0.0f, 0.0f, 1.0f, 1.0f};
                const float image_aspect             = static_cast<float>(reference->extent[0]) / static_cast<float>(reference->extent[1]);
                const float maximum_width            = gate.z * 0.28f;
                const float maximum_height           = gate.w * 0.28f;
                const float overlay_width            = std::min(maximum_width, maximum_height * static_cast<float>(extent.height) * image_aspect / static_cast<float>(extent.width));
                const float overlay_height           = overlay_width * static_cast<float>(extent.width) / (image_aspect * static_cast<float>(extent.height));
                const math::Float4 overlay_rect{
                    gate.x + 16.0f / static_cast<float>(extent.width),
                    gate.y + gate.w - overlay_height - 16.0f / static_cast<float>(extent.height),
                    overlay_width,
                    overlay_height,
                };
                camera_reference = CameraReferenceVisualization{reference, &camera, overlay_rect, entity_diagnostics.camera_gt_overlay, entity_diagnostics.camera_gt_plane};
            }
        }

        std::vector<scene::InstanceId> selected_instances{};
        for (const SceneEntityReference entity : this->viewport.view.selection.selected) {
            if (const scene::InstanceId* instance = std::get_if<scene::InstanceId>(&entity.data)) selected_instances.emplace_back(*instance);
            else if (const SceneEntityReference::AreaEmitter* emitter = std::get_if<SceneEntityReference::AreaEmitter>(&entity.data)) selected_instances.emplace_back(emitter->instance);
        }
        const auto instance_id = [](const std::optional<SceneEntityReference>& entity) -> std::optional<scene::InstanceId> {
            if (!entity) return std::nullopt;
            if (const scene::InstanceId* instance = std::get_if<scene::InstanceId>(&entity->data)) return *instance;
            if (const SceneEntityReference::AreaEmitter* emitter = std::get_if<SceneEntityReference::AreaEmitter>(&entity->data)) return emitter->instance;
            return std::nullopt;
        };
        const ViewportOverlayState overlay_state{
            .selected_instances = selected_instances,
            .active_instance    = instance_id(this->viewport.view.selection.active),
            .hovered_instance   = instance_id(this->viewport.view.selection.hovered),
            .axes_plane         = std::to_underlying(this->viewport.view.source == CameraSource::Scene ? AxesPlane::Xz : this->viewport.view.axes_plane),
            .axes_visible       = show_axes,
            .outline_visible    = this->viewport_settings.guides_visible && this->viewport_settings.selection_outline,
        };
        this->compositor.record(command_buffer,
            RenderCompositionRequest{
                .renderer_output        = output,
                .depth                  = depth,
                .scene                  = this->document.content.evaluated.view(),
                .camera                 = this->viewport.view.render_camera,
                .scene_camera_view      = this->viewport.view.source == CameraSource::Scene ? std::optional{this->viewport.view.scene_camera} : std::nullopt,
                .visualizations         = this->dynamics.visualizations(),
                .visualization          = &this->visualization,
                .neural_field           = &this->neural_field,
                .diagnostics            = SceneDiagnosticsComposition{this->diagnostics, this->viewport_settings.scene_guides, entity_diagnostics, this->viewport.view.selection, this->viewport_settings.guides_visible},
                .camera_reference       = camera_reference,
                .overlay                = ViewportOverlayComposition{this->overlay, overlay_state},
                .frame_slot_index       = frame_slot_index,
                .exposure               = this->viewport_settings.exposure,
                .compose_visualizations = true,
            });
        this->picker.record(command_buffer, frame_slot_index, this->document.content.evaluated.view(), this->viewport.view.render_camera, *depth, &this->diagnostics.pick_image());
    }

    void run_editor(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        EditorApplication application{std::move(request), shader_directory, pathtracer_directory};
        application.run();
    }
} // namespace spectra
