module spectra.headless;

import spectra.simulation.runtime;
import spectra.render;
import spectra.render.display;
import spectra.headless.output;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import spectra.scene.io;
import std;
import vulkan;

namespace spectra::headless {
    void run(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        runtime::VulkanInstance instance{"Spectra Render"};
        runtime::VulkanRuntime runtime{instance};
        scene::Scene authored = scene::load_scene(request.scene_path);

        scene::Film& film    = *std::ranges::find(authored.resources.films, authored.active_film, &scene::Film::id);
        scene::Camera camera = *std::ranges::find(authored.resources.cameras, authored.active_camera, &scene::Camera::id);
        if (request.gbuffer_output_path) film.gbuffer = true;
        const float aspect = static_cast<float>(film.resolution[0]) / static_cast<float>(film.resolution[1]);
        std::visit(
            [aspect](auto& data) {
                const float center_x         = (data.screen_window.minimum.x + data.screen_window.maximum.x) * 0.5f;
                const float half_height      = (data.screen_window.maximum.y - data.screen_window.minimum.y) * 0.5f;
                const float half_width       = half_height * aspect;
                data.screen_window.minimum.x = center_x - half_width;
                data.screen_window.maximum.x = center_x + half_width;
            },
            camera.data);
        scene::Scene evaluated = authored;

        simulation::Runtime simulation{runtime};
        if (authored.simulation) simulation.initialize(request.scene_path, authored, evaluated);

        if (!simulation.initialized() && (request.simulation_step || request.simulation_seconds || request.presentation_frame || request.presentation_seconds)) throw std::runtime_error("Simulation and Presentation targets require an enabled simulation setup");

        render::GpuScene gpu_scene{runtime, shader_directory};
        gpu_scene.initialize(evaluated, simulation.mesh_bindings(), simulation.sphere_set_bindings());

        const auto consume_simulation_frame = [&]() {
            const runtime::FrameContext frame = runtime.frames.begin_frame();
            gpu_scene.retire_frame(frame.slot_index);
            static_cast<void>(gpu_scene.apply(*simulation.acquire_frame(), evaluated.view(), frame.command_buffer, frame.slot_index));
            simulation.record_telemetry(frame.command_buffer, frame.slot_index);
            simulation.consume_frame();
            const std::uint32_t submitted_slot = runtime.frames.submit_frame();
            runtime.frames.wait_frame(submitted_slot);
            gpu_scene.retire_frame(submitted_slot);
            simulation.resolve_telemetry(submitted_slot);
        };
        if (simulation.initialized()) {
            consume_simulation_frame();
            bool final_publication{};
            if (request.presentation_frame || request.presentation_seconds) {
                const simulation::PresentationSequence* sequence = simulation.presentation_sequence();
                if (!sequence) throw std::runtime_error("Presentation targets require a scene with a Presentation Sequence");
                const std::uint64_t previous_frame = simulation.presentation_frame().index;
                if (request.presentation_frame) {
                    if (*request.presentation_frame >= sequence->frame_count) throw std::runtime_error("The requested presentation frame is outside the sequence");
                    simulation.select_presentation_frame(*request.presentation_frame);
                } else {
                    const double end_seconds = sequence->start_seconds + (sequence->frame_count - 1u) * sequence->frame_seconds;
                    if (*request.presentation_seconds < sequence->start_seconds || *request.presentation_seconds > end_seconds) throw std::runtime_error("The requested presentation time is outside the sequence");
                    simulation.select_presentation_time(*request.presentation_seconds);
                }
                final_publication = simulation.presentation_frame().index != previous_frame;
            }
            if (request.simulation_step) {
                if (*request.simulation_step < simulation.timeline().step) throw std::runtime_error("The requested simulation step precedes the scene start step");
                if (*request.simulation_step > simulation.timeline().step) {
                    simulation.evaluate(*request.simulation_step);
                    final_publication = true;
                }
            } else if (request.simulation_seconds) {
                if (*request.simulation_seconds < simulation.timeline().seconds) throw std::runtime_error("The requested simulation time precedes the scene start time");
                if (*request.simulation_seconds > simulation.timeline().seconds) {
                    simulation.evaluate_time(*request.simulation_seconds);
                    final_publication = true;
                }
            }
            simulation.update();
            if (final_publication) consume_simulation_frame();
        }

        render::Engine render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer, request.raster_display_mode};
        render_engine.rebuild(evaluated.view());
        if (request.renderer == render::RendererKind::PathTracer) render_engine.wait_for_pathtracer();
        std::optional<render::Compositor> compositor{};
        if (request.output != Output::RendererLinear) {
            compositor.emplace(runtime, gpu_scene, shader_directory,
                render::DisplayFeatures{
                    .diagnostics    = request.output == Output::ComposedDisplay && request.display_layers.diagnostics,
                    .visualizations = request.output == Output::ComposedDisplay && request.display_layers.visualizations,
                    .neural_fields  = true,
                    .overlays       = request.output == Output::ComposedDisplay && request.display_layers.overlays && (request.axes || request.outlined_instance.has_value()),
                });
        }

        const vk::Extent2D extent{film.resolution[0], film.resolution[1]};
        if (compositor) static_cast<void>(compositor->resize(extent));
        const render::RenderView view{camera, extent, 1};
        runtime::GpuBuffer linear_readback{};
        runtime::GpuBuffer display_readback{};
        std::uint32_t final_frame_slot{};
        std::uint32_t next_progress_report{1};
        bool complete{};
        while (!complete) {
            const runtime::FrameContext frame = runtime.frames.begin_frame();
            gpu_scene.retire_frame(frame.slot_index);
            scene::ResolvedSceneView current_scene = evaluated.view();
            static_cast<void>(render_engine.prepare(current_scene, view, frame.command_buffer));
            render_engine.record(frame.command_buffer, frame.slot_index);
            const std::optional<render::RenderProgress> progress = render_engine.progress();
            complete                                             = !progress || progress->completed >= progress->target;
            if (progress && (complete || progress->completed >= next_progress_report)) {
                std::println(std::cerr, "Samples: {} / {}", progress->completed, progress->target);
                next_progress_report = progress->completed + std::max(progress->target / 100u, 1u);
            }
            if (complete) {
                const render::RenderOutput renderer_output         = render_engine.output();
                const std::optional<render::DepthBufferView> depth = render_engine.depth_buffer();
                if (request.output != Output::RendererLinear) {
                    const render::SceneGuideSettings scene_guides{.all_bounds = true};
                    const render::EntityDiagnostics entity_diagnostics{};
                    std::array<scene::InstanceId, 1> outlined{};
                    std::span<const scene::InstanceId> selected{};
                    if (request.outlined_instance) {
                        outlined[0] = scene::InstanceId{*request.outlined_instance};
                        selected    = outlined;
                    }
                    const render::OverlayRequest overlay_request{
                        .selected_instances = selected,
                        .axes_plane         = request.axes_plane,
                        .axes_visible       = request.axes,
                    };
                    compositor->record(frame.command_buffer, render::DisplayRequest{
                                                                 .renderer_output   = renderer_output,
                                                                 .depth             = depth,
                                                                 .scene             = evaluated.view(),
                                                                 .camera            = camera,
                                                                 .scene_camera_view = camera.id,
                                                                 .visualizations    = simulation.visualizations(),
                                                                 .diagnostics       = request.output == Output::ComposedDisplay && request.display_layers.diagnostics ? std::optional{render::DiagnosticRequest{.scene_guides = scene_guides, .entity = entity_diagnostics}} : std::nullopt,
                                                                 .overlay           = request.output == Output::ComposedDisplay && request.display_layers.overlays && (request.axes || request.outlined_instance) ? std::optional{overlay_request} : std::nullopt,
                                                                 .frame_slot_index  = frame.slot_index,
                                                             });
                    const render::RenderOutput composed_output = compositor->output();
                    record_display_readback(runtime, frame.command_buffer, composed_output.image, composed_output.image_layout, display_readback);
                }
                record_linear_readback(runtime, frame.command_buffer, renderer_output, linear_readback);
            }
            final_frame_slot = runtime.frames.submit_frame();
        }
        runtime.frames.wait_frame(final_frame_slot);

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        if (request.output != Output::RendererLinear) write_png(request.png_output_path, std::span{static_cast<const std::uint8_t*>(display_readback.mapped), pixel_count * 4u}, extent);
        write_linear_exr(request.linear_output_path, std::span{static_cast<const float*>(linear_readback.mapped), pixel_count * 4u}, extent, render_engine.output().color_space);
        if (request.gbuffer_output_path) write_gbuffer_exr(*request.gbuffer_output_path, render_engine.readback(), render_engine.output().color_space, film.gbuffer_camera_space);
        if (request.telemetry_output_path) simulation.write_telemetry(*request.telemetry_output_path);
    }
} // namespace spectra::headless
