module;

#include <imgui.h>
#include <imgui_impl_glfw.h>

module spectra;

import std;
import vulkan;

namespace spectra {
    Spectra::Spectra(std::optional<std::filesystem::path> scene_path, const std::filesystem::path& scene_library_path, std::vector<std::filesystem::path> session_scene_roots, const std::filesystem::path& shader_directory, std::optional<std::string> initial_renderer) : platform("Spectra", {1920, 1080}), runtime(platform, "Spectra"), presentation(platform, runtime.graphics, runtime.frames), dynamics(runtime, document), gpu_scene(runtime, document, dynamics, shader_directory), renderers(runtime, gpu_scene, shader_directory, std::move(initial_renderer)), editor(platform, runtime, document, dynamics, gpu_scene, renderers, shader_directory, scene_library_path, std::move(session_scene_roots)) {
        this->editor.initialize(std::move(scene_path));
    }

    void Spectra::prepare_rendering(const vk::raii::CommandBuffer& command_buffer, const vk::Extent2D extent) {
        scene::SceneChange binding_changes{scene::SceneChange::None};
        bool gpu_scene_synchronized{};
        if (this->dynamics.configuration.initialized)
            if (const dynamics::DynamicFrame* frame = this->dynamics.pending_frame()) {
                binding_changes        = this->gpu_scene.apply(*frame, command_buffer);
                gpu_scene_synchronized = true;
            }

        const scene::SceneRevision revision = this->document.content.evaluated.revision();
        const bool scene_changed            = revision.number != this->editor.rendering.synchronized_scene_revision;
        if (scene_changed) {
            if (!gpu_scene_synchronized) binding_changes = this->gpu_scene.synchronize(command_buffer);
            scene::SceneView synchronized_scene = this->document.content.evaluated.view();
            synchronized_scene.revision.changes = synchronized_scene.revision.changes | binding_changes;
            this->editor.viewport.synchronize(synchronized_scene, command_buffer);
            this->renderers.invalidate(synchronized_scene.revision.changes);
            this->editor.rendering.synchronized_scene_revision = revision.number;
            this->editor.interaction.prune_selection();
        }

        const float aspect              = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        const bool aspect_changed       = aspect != this->editor.interaction.view.aspect;
        const bool scene_camera_changed = scene_changed && (revision.changes & scene::SceneChange::Camera) != scene::SceneChange::None;
        this->editor.interaction.view.aspect = aspect;
        const bool camera_changed = aspect_changed || this->editor.interaction.view.source != this->editor.interaction.view.synchronized_source || (this->editor.interaction.view.source == CameraSource::Viewport && (scene_camera_changed || this->editor.interaction.view.camera_revision != this->editor.interaction.view.synchronized_camera_revision)) || (this->editor.interaction.view.source == CameraSource::Scene && this->document.content.source.camera().revision != this->editor.interaction.view.synchronized_scene_camera_revision);
        if (camera_changed) this->editor.interaction.camera_changed();

        this->editor.output.record_frozen_scene_snapshot(command_buffer, this->runtime.frames.frame.current_slot_index, this->editor.interaction.view.render_camera, extent, this->document.content.path);

        if (scene_changed) this->document.content.evaluated.acknowledge_changes();
        if (this->document.content.source.revision().changes != scene::SceneChange::None) this->document.content.source.acknowledge_changes();
        this->editor.viewport.set_camera(this->editor.interaction.view.render_camera);
        this->renderers.prepare(this->document.content.evaluated.view(), RenderView{this->editor.interaction.view.render_camera, extent, this->editor.interaction.view.render_camera_revision}, command_buffer);
        if (this->dynamics.configuration.initialized) this->dynamics.consume_frame();
    }

    void Spectra::record_rendering(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        this->renderers.record(command_buffer, frame_slot_index);
        this->editor.viewport.record_picker(command_buffer, frame_slot_index);
    }

    void Spectra::record_editor_overlays(const vk::raii::CommandBuffer& command_buffer, const bool show_axes) {
        this->editor.viewport.record_overlay(command_buffer, show_axes, this->editor.interaction);
    }

    RenderOutput Spectra::current_render_output() const noexcept {
        return this->renderers.output();
    }

    void Spectra::run(const std::optional<std::uint64_t> maximum_frame_count) {
        while (true) {
            if (maximum_frame_count && this->editor.timing.presented_frames >= *maximum_frame_count) break;
            this->platform.poll_events();
            if (this->platform.take_close_request()) break;
            this->editor.handle_dropped_scene_paths();

            const std::optional<PresentedFrameContext> frame = this->presentation.begin_frame();
            if (!frame) {
                this->platform.wait_events();
                this->editor.timing.simulation_sample_valid = false;
                continue;
            }

            const std::chrono::steady_clock::time_point current_clock_sample = std::chrono::steady_clock::now();
            const bool simulation_clock_active                               = this->document.content.loaded && !this->editor.library.visible;
            if (this->document.content.loaded) {
                this->editor.begin_frame(frame->frame.slot_index);
                if (simulation_clock_active && this->editor.timing.simulation_sample_valid && this->dynamics.configuration.initialized) this->dynamics.advance(current_clock_sample - this->editor.timing.previous_simulation_sample);
                this->editor.viewport.resize(frame->presentation_target.extent);
            }
            this->editor.timing.previous_simulation_sample = current_clock_sample;
            this->editor.timing.simulation_sample_valid    = simulation_clock_active;

            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            const EditorActions actions = this->editor.library.visible ? this->editor.ui.draw_scene_library(this->editor.library.scenes, this->editor.library.problems, this->document.content.loaded) : this->editor.ui.draw_editor_ui();
            this->platform.window_drag_regions = actions.window_drag_regions;
            this->editor.handle_actions(actions);
            if (!this->editor.timing.simulation_sample_valid && this->document.content.loaded && !this->editor.library.visible) {
                this->editor.timing.previous_simulation_sample = std::chrono::steady_clock::now();
                this->editor.timing.simulation_sample_valid    = true;
            }
            ImGui::Render();

            if (this->document.content.loaded) {
                this->editor.viewport.resize(frame->presentation_target.extent);
                const vk::Extent2D viewport_extent = this->editor.viewport.target.image.extent;
                this->prepare_rendering(frame->frame.command_buffer, viewport_extent);
                this->record_rendering(frame->frame.command_buffer, frame->frame.slot_index);
                const RenderOutput output = this->current_render_output();
                this->editor.output.record_presenter(frame->frame.command_buffer, output);
                this->record_editor_overlays(frame->frame.command_buffer, actions.show_axes);
                this->editor.output.record_capture(frame->frame.command_buffer, frame->frame.slot_index, output);
            }

            this->editor.ui.record_imgui(*ImGui::GetDrawData(), frame->frame.command_buffer, frame->frame.slot_index, frame->presentation_target.image, frame->presentation_target.view, frame->presentation_target.extent, frame->presentation_target.image_layout, vk::ImageLayout::ePresentSrcKHR);
            if (this->presentation.present_frame()) ++this->editor.timing.presented_frames;
        }
    }

    void render_scene(const RenderRequest& request, const std::filesystem::path& shader_directory) {
        if (request.output_path.extension() != ".exr") throw std::runtime_error("--output requires an .exr file");
        if (request.gbuffer_output_path && request.gbuffer_output_path->extension() != ".exr") throw std::runtime_error("--gbuffer-output requires an .exr file");
        if (request.gbuffer_output_path && request.renderer != PathTracer::descriptor.id) throw std::runtime_error("--gbuffer-output requires the Path Tracer");
        if (request.gbuffer_output_path && std::filesystem::absolute(request.output_path).lexically_normal() == std::filesystem::absolute(*request.gbuffer_output_path).lexically_normal()) throw std::runtime_error("--output and --gbuffer-output require different files");
        if (std::filesystem::exists(request.output_path)) throw std::runtime_error(std::format("Output file already exists: {}", request.output_path.string()));
        if (request.gbuffer_output_path && std::filesystem::exists(*request.gbuffer_output_path)) throw std::runtime_error(std::format("GBuffer output file already exists: {}", request.gbuffer_output_path->string()));
        if (!request.output_path.parent_path().empty() && !std::filesystem::is_directory(request.output_path.parent_path())) throw std::runtime_error(std::format("Output directory does not exist: {}", request.output_path.parent_path().string()));
        if (request.gbuffer_output_path && !request.gbuffer_output_path->parent_path().empty() && !std::filesystem::is_directory(request.gbuffer_output_path->parent_path())) throw std::runtime_error(std::format("GBuffer output directory does not exist: {}", request.gbuffer_output_path->parent_path().string()));

        VulkanRuntime runtime{"Spectra Render"};
        SceneDocument document{};
        document.content.source = scene::load_scene(request.scene_path);
        document.content.path   = request.scene_path;
        document.content.loaded = true;

        scene::Film& film       = *std::ranges::find(document.content.source.resources.films, document.content.source.active_film, &scene::Film::id);
        scene::Sampler& sampler = *std::ranges::find(document.content.source.resources.samplers, document.content.source.active_sampler, &scene::Sampler::id);
        scene::Camera& camera   = *std::ranges::find(document.content.source.resources.cameras, document.content.source.active_camera, &scene::Camera::id);
        if (request.resolution) {
            film.resolution    = {request.resolution->width, request.resolution->height};
            film.pixel_minimum = {};
            film.pixel_maximum = film.resolution;
            const float aspect = static_cast<float>(request.resolution->width) / static_cast<float>(request.resolution->height);
            std::visit(
                [aspect](auto& data) {
                    const float center_x         = (data.screen_window.minimum.x + data.screen_window.maximum.x) * 0.5f;
                    const float half_height      = (data.screen_window.maximum.y - data.screen_window.minimum.y) * 0.5f;
                    const float half_width       = half_height * aspect;
                    data.screen_window.minimum.x = center_x - half_width;
                    data.screen_window.maximum.x = center_x + half_width;
                },
                camera.data);
        }
        if (request.samples_per_pixel) sampler.samples_per_pixel = *request.samples_per_pixel;
        if (request.seed) sampler.seed = *request.seed;
        if (request.gbuffer_output_path) film.gbuffer = true;
        document.content.source.mark_all_changed();
        document.content.evaluated = document.content.source;

        DynamicWorld dynamics{runtime, document};
        if (document.content.source.dynamic_setup) {
            dynamics.initialize(request.scene_path, document.content.source);
            dynamics.seek(request.simulation_step.value_or(document.content.source.dynamic_setup->clock.start_step));
        } else if (request.simulation_step)
            throw std::runtime_error("--simulation-step requires a dynamic Scene");

        GpuScene gpu_scene{runtime, document, dynamics, shader_directory};
        std::vector<GpuGeometryBinding> geometry_bindings{};
        geometry_bindings.reserve(dynamics.outputs.mesh_bindings.size());
        for (const dynamics::MeshOutputBinding& binding : dynamics.outputs.mesh_bindings)
            geometry_bindings.push_back(GpuGeometryBinding{
                binding.geometry_id,
                binding.update_mode == dynamics::MeshUpdateMode::Deformable ? GpuMeshUpdateMode::Deformable : GpuMeshUpdateMode::TopologyChanging,
                binding.vertex_capacity,
                binding.index_capacity,
            });
        gpu_scene.initialize(document.content.source, geometry_bindings, dynamics.outputs.particle_capacities, dynamics.outputs.hidden_instances);

        Renderers renderers{runtime, gpu_scene, shader_directory, request.renderer};
        renderers.rebuild(document.content.evaluated.view());
        const vk::Extent2D extent{film.resolution[0], film.resolution[1]};
        const RenderView view{camera, extent, 1};
        GpuBuffer readback_buffer{};
        std::uint32_t final_frame_slot{};
        std::uint32_t next_progress_report{1};
        bool complete{};
        while (!complete) {
            const FrameContext frame = runtime.frames.begin_frame();
            scene::SceneChange binding_changes{scene::SceneChange::None};
            if (dynamics.configuration.initialized)
                if (const dynamics::DynamicFrame* dynamic_frame = dynamics.pending_frame()) binding_changes = gpu_scene.apply(*dynamic_frame, frame.command_buffer);

            scene::SceneView current_scene = document.content.evaluated.view();
            current_scene.revision.changes = current_scene.revision.changes | binding_changes;
            renderers.invalidate(current_scene.revision.changes);
            renderers.prepare(current_scene, view, frame.command_buffer);
            renderers.record(frame.command_buffer, frame.slot_index);
            const std::optional<RenderProgress> progress = renderers.progress();
            complete                                     = !progress || progress->completed >= progress->target;
            if (progress && (complete || progress->completed >= next_progress_report)) {
                std::println("Samples: {} / {}", progress->completed, progress->target);
                next_progress_report = progress->completed + std::max(progress->target / 100u, 1u);
            }
            if (complete) record_linear_readback(runtime, frame.command_buffer, renderers.output(), readback_buffer);
            if (dynamics.configuration.initialized) dynamics.consume_frame();
            document.content.evaluated.acknowledge_changes();
            document.content.source.acknowledge_changes();
            final_frame_slot = runtime.frames.submit_frame();
        }
        runtime.frames.wait_frame(final_frame_slot);

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        write_linear_exr(request.output_path, std::span{static_cast<const float*>(readback_buffer.mapped), pixel_count * 4u}, extent, film.color_space);
        if (request.gbuffer_output_path) {
            if (!renderers.gbuffer_available()) throw std::runtime_error("--gbuffer-output requires a Renderer with a GBuffer");
            write_gbuffer_exr(*request.gbuffer_output_path, renderers.readback(), film.color_space, film.gbuffer_camera_space);
        }
    }
} // namespace spectra
