module spectra.editor;

import :output.capture;
import :output.frozen_scene;
import :platform.dialogs;
import :platform.presentation;
import :platform.window;
import :ui;
import :ui.imgui;
import :viewport.interaction;
import :viewport.overlay;
import :viewport.picker;
import spectra.display;
import spectra.render;
import spectra.runtime;
import spectra.scene;
import spectra.scene.document;
import spectra.scene.dynamics;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra {
    struct EditorApplication {
        EditorApplication(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory, const std::filesystem::path& output_directory);

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
        DynamicWorld dynamics;
        GpuScene gpu_scene;
        Renderers renderers;
        ViewportInteraction viewport;
        DisplayRenderer display;
        ViewportOverlay overlay;
        ViewportPicker picker;
        FrameCapture capture;
        FrozenSceneExporter frozen_export;
        ImGuiBackend imgui;
        EditorUi ui;

        struct {
            std::uint64_t synchronized_scene_revision{};
        } rendering;

        struct {
            std::chrono::steady_clock::time_point previous_simulation_sample{};
            bool simulation_sample_valid{};
        } timing;

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

    EditorApplication::EditorApplication(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory, const std::filesystem::path& output_directory) : platform{"Spectra", {1920, 1080}}, dialogs{platform}, instance{"Spectra", presentation_instance_extensions}, surface{platform, instance}, runtime{instance, *surface.surface}, presentation{platform, surface, runtime.graphics, runtime.frames}, dynamics{runtime, document}, gpu_scene{runtime, document, dynamics, shader_directory}, renderers{runtime, gpu_scene, shader_directory, pathtracer_directory, std::move(request.renderer)}, viewport{document, dynamics}, display{runtime, shader_directory}, overlay{runtime, gpu_scene, shader_directory}, picker{runtime, gpu_scene, shader_directory}, capture{runtime, renderers, display, output_directory}, frozen_export{gpu_scene}, imgui{platform, runtime, display, shader_directory}, ui{document, dynamics, renderers, viewport, picker, frozen_export, imgui} {
        this->display.initialize();
        this->imgui.initialize();
        if (request.scene_path) this->open_scene(*request.scene_path);
    }

    void EditorApplication::destroy_rendering() noexcept {
        this->picker.destroy_scene();
        this->overlay.destroy_scene();
        this->renderers.destroy();
        this->gpu_scene.destroy();
    }

    void EditorApplication::rebuild_rendering(scene::Scene& source_scene) {
        std::vector<GpuGeometryBinding> geometry_bindings{};
        geometry_bindings.reserve(this->dynamics.outputs.mesh_bindings.size());
        for (const dynamics::MeshOutputBinding& binding : this->dynamics.outputs.mesh_bindings)
            geometry_bindings.push_back(GpuGeometryBinding{
                binding.geometry_id,
                binding.update_mode == dynamics::MeshUpdateMode::Deformable ? GpuMeshUpdateMode::Deformable : GpuMeshUpdateMode::TopologyChanging,
                binding.vertex_capacity,
                binding.index_capacity,
            });
        this->gpu_scene.initialize(source_scene, geometry_bindings, this->dynamics.outputs.particle_capacities, this->dynamics.outputs.hidden_instances);
        this->renderers.rebuild(this->document.content.evaluated.view());
        this->overlay.initialize(this->document.content.evaluated.view());
        this->picker.initialize(this->document.content.evaluated.view());
    }

    void EditorApplication::open_scene(const std::filesystem::path& path) {
        scene::Scene next_scene = scene::load_scene(path);
        this->runtime.graphics.device.waitIdle();
        this->destroy_rendering();
        this->dynamics.destroy();
        this->document.content.source    = std::move(next_scene);
        this->document.content.evaluated = this->document.content.source;
        this->document.content.path      = path;
        if (this->document.content.source.dynamic_setup) this->dynamics.initialize(path, this->document.content.source);
        this->rebuild_rendering(this->document.content.source);
        this->viewport.initialize_from_scene();
        this->document.content.loaded               = true;
        this->document.content.modified             = false;
        this->rendering.synchronized_scene_revision = 0;
    }

    void EditorApplication::begin_frame(const std::uint32_t frame_slot_index) {
        if (std::optional<std::expected<std::filesystem::path, std::string>> result = this->capture.begin_frame(frame_slot_index)) {
            if (*result)
                this->ui.notify(std::format("Written  {}", (*result)->filename().string()));
            else
                this->ui.notify(result->error(), true);
        }
        if (std::optional<std::expected<std::filesystem::path, std::string>> result = this->frozen_export.begin_frame(frame_slot_index)) {
            if (*result)
                this->ui.notify(std::format("Written  {}", (*result)->filename().string()));
            else
                this->ui.notify(result->error(), true);
        }

        const ViewportPicker::PickResult pick = this->picker.take_pick_result(frame_slot_index);
        if (!pick.ready) return;
        std::optional<scene::InstanceId> instance{};
        if (pick.acceleration_instance_index) instance = this->gpu_scene.resources.acceleration_instance_ids[*pick.acceleration_instance_index];
        const bool debug_hit = pick.debug_object_id && (pick.debug_xray || !instance);
        if (!pick.select) {
            this->viewport.view.selection.hovered_instance = debug_hit ? std::nullopt : instance;
            return;
        }
        if (debug_hit) {
            if (!pick.additive) this->viewport.clear_selection();
            return;
        }
        if (!instance) {
            if (!pick.additive) this->viewport.clear_selection();
            return;
        }
        this->viewport.select_instance(*instance, pick.additive);
    }

    bool EditorApplication::confirm_scene_replacement() {
        if (!this->document.content.loaded || !this->document.content.modified) return true;
        this->timing.simulation_sample_valid    = false;
        const SceneReplacementDecision decision = this->dialogs.confirm_scene_replacement();
        if (decision == SceneReplacementDecision::Cancel) return false;
        if (decision == SceneReplacementDecision::Save) this->document.save();
        return true;
    }

    void EditorApplication::replace_scene(const std::filesystem::path& path) {
        this->timing.simulation_sample_valid = false;
        if (path.extension() != ".spectra") throw std::runtime_error("Spectra accepts only .spectra scenes");
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(path);
    }

    void EditorApplication::reload_scene() {
        this->timing.simulation_sample_valid = false;
        if (!this->confirm_scene_replacement()) return;
        this->open_scene(this->document.content.path);
        this->ui.notify("Scene reloaded");
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
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_file()) this->replace_scene(*path);
            }
            if (actions.reload_scene) this->reload_scene();
            if (actions.save_scene) {
                this->timing.simulation_sample_valid = false;
                this->document.save();
                this->ui.notify("Scene saved");
            }
            if (actions.save_scene_as) {
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_save_path(this->document.content.path)) {
                    this->document.save_as(*path);
                    this->ui.notify("Scene saved");
                }
            }
            if (actions.export_frozen_scene) {
                this->timing.simulation_sample_valid = false;
                if (const std::optional<std::filesystem::path> path = this->dialogs.choose_scene_save_path(this->document.content.path, true)) {
                    this->frozen_export.request(*path);
                    this->ui.notify("Capturing Frozen Scene");
                }
            }
            if (actions.renderer) {
                this->renderers.activate(*actions.renderer, this->document.content.evaluated.view());
            }
            if (actions.capture_format) this->capture.request(*actions.capture_format, this->document.content.source.film(), this->document.content.path);
        } catch (const std::exception& error) {
            this->ui.notify(error.what(), true);
        }
    }

    bool EditorApplication::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        scene::SceneChange binding_changes{scene::SceneChange::None};
        bool gpu_scene_synchronized{};
        if (this->dynamics.configuration.initialized)
            if (const dynamics::DynamicFrame* frame = this->dynamics.pending_frame()) {
                binding_changes        = this->gpu_scene.apply(*frame, command_buffer);
                gpu_scene_synchronized = true;
            }

        const scene::SceneRevision revision = this->document.content.evaluated.revision();
        const bool scene_changed            = revision.number != this->rendering.synchronized_scene_revision;
        if (scene_changed) {
            if (!gpu_scene_synchronized) binding_changes = this->gpu_scene.synchronize(command_buffer);
            scene::SceneView synchronized_scene = this->document.content.evaluated.view();
            synchronized_scene.revision.changes = synchronized_scene.revision.changes | binding_changes;
            this->overlay.synchronize(synchronized_scene, command_buffer);
            this->picker.synchronize(synchronized_scene, command_buffer);
            this->renderers.invalidate(synchronized_scene.revision.changes);
            this->rendering.synchronized_scene_revision = revision.number;
            this->viewport.prune_selection();
        }

        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->viewport.view.aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->viewport.view.aspect      = aspect;
        const bool camera_changed       = aspect_changed || this->viewport.view.source != this->viewport.view.synchronized_source || (this->viewport.view.source == CameraSource::Viewport && (scene_camera_changed || this->viewport.view.camera_revision != this->viewport.view.synchronized_camera_revision)) || (this->viewport.view.source == CameraSource::Scene && this->document.content.source.camera().revision != this->viewport.view.synchronized_scene_camera_revision);
        if (camera_changed) this->viewport.camera_changed();

        this->frozen_export.record_snapshot(command_buffer, this->runtime.frames.frame.current_slot_index, this->viewport.view.render_camera, extent, this->ui.controls.exposure, this->document.content.path);

        if (scene_changed) this->document.content.evaluated.acknowledge_changes();
        if (this->document.content.source.revision().changes != scene::SceneChange::None) this->document.content.source.acknowledge_changes();
        const bool renderer_ready = this->renderers.prepare(this->document.content.evaluated.view(), RenderView{this->viewport.view.render_camera, extent, this->viewport.view.render_camera_revision}, command_buffer);
        if (this->dynamics.configuration.initialized) this->dynamics.consume_frame();
        return renderer_ready;
    }

    void EditorApplication::record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, const bool show_axes) {
        const bool visualizations = this->renderers.active_renders_visualizations();
        this->overlay.record(command_buffer, this->display, this->viewport.view.render_camera,
            ViewportOverlayState{
                .selected_instances     = this->viewport.view.selection.selected_instances,
                .active_instance        = this->viewport.view.selection.active_instance,
                .hovered_instance       = this->viewport.view.selection.hovered_instance,
                .axes_plane             = std::to_underlying(this->viewport.view.source == CameraSource::Scene ? AxesPlane::Xz : this->viewport.view.axes_plane),
                .axes_visible           = show_axes,
                .outline_visible        = this->viewport.view.overlays_visible,
                .raster_visualizations  = visualizations,
                .debug_primitives       = visualizations && this->dynamics.configuration.initialized ? std::span<const dynamics::DebugPrimitive>{this->dynamics.publication.debug_primitives} : std::span<const dynamics::DebugPrimitive>{},
                .volume_velocity_fields = visualizations ? std::span<const GpuVolumeVelocityField>{this->gpu_scene.resources.volume_velocity_fields} : std::span<const GpuVolumeVelocityField>{},
            });
    }

    void EditorApplication::run() {
        while (true) {
            this->platform.poll_events();
            if (this->platform.take_close_request()) break;
            this->handle_dropped_scene_paths();

            const std::optional<PresentedFrameContext> frame = this->presentation.begin_frame();
            if (!frame) {
                this->platform.wait_events();
                this->timing.simulation_sample_valid = false;
                continue;
            }

            const std::chrono::steady_clock::time_point current_clock_sample = std::chrono::steady_clock::now();
            const bool simulation_clock_active                               = this->document.content.loaded;
            if (this->document.content.loaded) {
                this->begin_frame(frame->frame.slot_index);
                if (simulation_clock_active && this->timing.simulation_sample_valid && this->dynamics.configuration.initialized) this->dynamics.advance(current_clock_sample - this->timing.previous_simulation_sample);
                this->imgui.resize_viewport(frame->presentation_target.extent);
            }
            this->timing.previous_simulation_sample = current_clock_sample;
            this->timing.simulation_sample_valid    = simulation_clock_active;

            this->imgui.begin_frame();
            const EditorActions actions        = this->ui.draw_editor_ui();
            this->platform.window_drag_regions = actions.window_drag_regions;
            this->handle_actions(actions);
            if (!this->timing.simulation_sample_valid && this->document.content.loaded) {
                this->timing.previous_simulation_sample = std::chrono::steady_clock::now();
                this->timing.simulation_sample_valid    = true;
            }
            this->imgui.end_frame();

            if (this->document.content.loaded) {
                this->imgui.resize_viewport(frame->presentation_target.extent);
                const vk::Extent2D viewport_extent = this->display.image.extent;
                if (this->prepare_rendering(frame->frame.command_buffer, viewport_extent)) {
                    this->renderers.record(frame->frame.command_buffer, frame->frame.slot_index);
                    const RenderOutput output = this->renderers.output();
                    this->picker.record(frame->frame.command_buffer, frame->frame.slot_index, this->viewport.view.render_camera);
                    this->display.record(frame->frame.command_buffer, output, this->ui.controls.exposure);
                    this->record_editor_overlays(frame->frame.command_buffer, actions.show_axes);
                    this->capture.record(frame->frame.command_buffer, frame->frame.slot_index, output);
                }
            }

            this->imgui.record(frame->frame.command_buffer, frame->frame.slot_index, frame->presentation_target.image, frame->presentation_target.view, frame->presentation_target.extent, frame->presentation_target.image_layout, vk::ImageLayout::ePresentSrcKHR);
            this->presentation.present_frame();
        }
    }

    void run_editor(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory, const std::filesystem::path& output_directory) {
        EditorApplication application{std::move(request), shader_directory, pathtracer_directory, output_directory};
        application.run();
    }
} // namespace spectra
