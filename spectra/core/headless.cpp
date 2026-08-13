module spectra.headless;

import spectra.dynamics.runtime;
import spectra.render.composition.diagnostics;
import spectra.render.composition.overlay;
import spectra.render.composition.visualization;
import spectra.render;
import spectra.render.composition;
import spectra.render.capture;
import spectra.render.display;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import spectra.scene.document;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra {
    void render_scene(RenderRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        if (request.gbuffer_output_path && request.renderer != pathtracer_descriptor.id) throw std::runtime_error("--gbuffer-output requires the Path Tracer");
        const RasterDisplayMode raster_display_mode = parse_raster_display_mode(request.raster_display_mode);
        const RenderOutputLayer output_layer        = parse_render_output_layer(request.output_layer);
        const bool compose_diagnostics              = request.composition == "all" || request.composition == "diagnostics";
        const bool compose_visualizations           = request.composition == "all" || request.composition == "visualizations";
        const bool compose_overlays                 = request.composition == "all" || request.composition == "overlays";
        if (!compose_diagnostics && !compose_visualizations && !compose_overlays && request.composition != "none") throw std::runtime_error(std::format("Unknown composed output content: {}", request.composition));
        if ((request.axes || request.outlined_instance) && (output_layer != RenderOutputLayer::ComposedDisplay || !compose_overlays)) throw std::runtime_error("Axes and Instance outlines require overlays in composed-display output");
        if (request.renderer == pathtracer_descriptor.id && raster_display_mode != RasterDisplayMode::Material) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
        if (request.simulation_step && request.simulation_seconds) throw std::runtime_error("Select either --simulation-step or --simulation-time");
        if (request.simulation_seconds && *request.simulation_seconds < 0.0) throw std::runtime_error("--simulation-time must be nonnegative");

        VulkanInstance instance{"Spectra Render"};
        VulkanRuntime runtime{instance};
        SceneDocument document{};
        document.content.source = scene::load_scene(request.scene_path);
        document.content.path   = request.scene_path;
        document.content.loaded = true;

        scene::Film& film    = *std::ranges::find(document.content.source.resources.films, document.content.source.active_film, &scene::Film::id);
        scene::Camera camera = *std::ranges::find(document.content.source.resources.cameras, document.content.source.active_camera, &scene::Camera::id);
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
        document.content.evaluated = document.content.source;

        DynamicsRuntime dynamics{runtime};
        if (document.content.source.dynamic_setup) dynamics.initialize(request.scene_path, document.content.source);

        GpuScene gpu_scene{runtime, shader_directory};
        gpu_scene.initialize(document.content.source, dynamics.mesh_bindings(), dynamics.sphere_set_bindings());

        const auto consume_dynamic_snapshot = [&]() {
            const FrameContext frame = runtime.frames.begin_frame();
            gpu_scene.retire_frame(frame.slot_index);
            static_cast<void>(gpu_scene.apply(*dynamics.acquire_snapshot(), document.content.evaluated.view(), frame.command_buffer));
            dynamics.record_telemetry(frame.command_buffer, frame.slot_index);
            dynamics.consume_snapshot();
            const std::uint32_t submitted_slot = runtime.frames.submit_frame();
            runtime.frames.wait_frame(submitted_slot);
            gpu_scene.retire_frame(submitted_slot);
            dynamics.resolve_telemetry(submitted_slot);
        };
        if (dynamics.initialized()) {
            consume_dynamic_snapshot();
            if (request.simulation_step) {
                if (*request.simulation_step < dynamics.timeline().step) throw std::runtime_error("The requested simulation step precedes the scene start step");
                dynamics.evaluate(*request.simulation_step);
                consume_dynamic_snapshot();
            } else if (request.simulation_seconds) {
                if (*request.simulation_seconds < dynamics.timeline().seconds) throw std::runtime_error("The requested simulation time precedes the scene start time");
                dynamics.evaluate_time(*request.simulation_seconds);
                consume_dynamic_snapshot();
            }
        } else if (request.simulation_step || request.simulation_seconds)
            throw std::runtime_error("Dynamic time targets require a scene with an enabled Dynamic Setup");

        RenderEngine render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer, raster_display_mode};
        render_engine.rebuild(document.content.evaluated.view());
        if (request.renderer == pathtracer_descriptor.id) render_engine.wait_for_pathtracer();
        std::unique_ptr<DisplayPass> display{};
        if (output_layer != RenderOutputLayer::RendererLinear) {
            display = std::make_unique<DisplayPass>(runtime, shader_directory);
            display->initialize();
        }
        std::unique_ptr<VisualizationRenderer> visualization{};
        if (output_layer == RenderOutputLayer::ComposedDisplay && compose_visualizations) visualization = std::make_unique<VisualizationRenderer>(runtime, shader_directory);
        std::unique_ptr<SceneDiagnosticRenderer> diagnostics{};
        if (output_layer == RenderOutputLayer::ComposedDisplay && compose_diagnostics) {
            diagnostics = std::make_unique<SceneDiagnosticRenderer>(runtime, gpu_scene, shader_directory);
            diagnostics->initialize();
        }
        std::unique_ptr<ViewportOverlay> overlay{};
        if (compose_overlays && (request.axes || request.outlined_instance)) {
            overlay = std::make_unique<ViewportOverlay>(runtime, gpu_scene, shader_directory);
            overlay->initialize();
        }

        const vk::Extent2D extent{film.resolution[0], film.resolution[1]};
        if (display) static_cast<void>(display->resize(extent));
        const RenderView view{camera, extent, 1};
        GpuBuffer linear_readback{};
        GpuBuffer display_readback{};
        std::uint32_t final_frame_slot{};
        std::uint32_t next_progress_report{1};
        bool complete{};
        while (!complete) {
            const FrameContext frame       = runtime.frames.begin_frame();
            gpu_scene.retire_frame(frame.slot_index);
            scene::SceneView current_scene = document.content.evaluated.view();
            static_cast<void>(render_engine.prepare(current_scene, view, frame.command_buffer));
            render_engine.record(frame.command_buffer, frame.slot_index);
            const std::optional<RenderProgress> progress = render_engine.progress();
            complete                                     = !progress || progress->completed >= progress->target;
            if (progress && (complete || progress->completed >= next_progress_report)) {
                std::println(std::cerr, "Samples: {} / {}", progress->completed, progress->target);
                next_progress_report = progress->completed + std::max(progress->target / 100u, 1u);
            }
            if (complete) {
                const RenderOutput renderer_output         = render_engine.output();
                const std::optional<DepthBufferView> depth = render_engine.depth_buffer();
                if (output_layer != RenderOutputLayer::RendererLinear) {
                    if (output_layer == RenderOutputLayer::RendererDisplay)
                        display->record(frame.command_buffer, renderer_output, 0.0f);
                    else {
                        const SceneDiagnosticSettings diagnostic_settings{.selected_bounds = false, .all_bounds = true};
                        const SelectionState selection{};
                        record_render_composition(frame.command_buffer, *display,
                            RenderCompositionRequest{
                                .renderer_output        = renderer_output,
                                .depth                  = depth,
                                .scene                  = document.content.evaluated.view(),
                                .camera                 = camera,
                                .scene_camera_view      = camera.id,
                                .visualizations         = dynamics.visualizations(),
                                .visualization          = visualization.get(),
                                .diagnostics            = diagnostics ? std::optional{SceneDiagnosticsComposition{*diagnostics, diagnostic_settings, selection}} : std::nullopt,
                                .frame_slot_index       = frame.slot_index,
                                .compose_visualizations = compose_visualizations,
                            });
                    }
                    if (overlay) {
                        std::array<scene::InstanceId, 1> outlined{};
                        std::span<const scene::InstanceId> selected{};
                        if (request.outlined_instance) {
                            outlined[0] = scene::InstanceId{*request.outlined_instance};
                            selected    = outlined;
                        }
                        overlay->record(frame.command_buffer, *display, camera, ViewportOverlayState{.selected_instances = selected, .axes_plane = request.axes_plane, .axes_visible = request.axes});
                    }
                    record_display_readback(runtime, frame.command_buffer, display->image, display->layout, display_readback);
                }
                record_linear_readback(runtime, frame.command_buffer, renderer_output, linear_readback);
            }
            final_frame_slot = runtime.frames.submit_frame();
        }
        runtime.frames.wait_frame(final_frame_slot);

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        if (output_layer != RenderOutputLayer::RendererLinear) write_png(request.png_output_path, std::span{static_cast<const std::uint8_t*>(display_readback.mapped), pixel_count * 4u}, extent);
        write_linear_exr(request.linear_output_path, std::span{static_cast<const float*>(linear_readback.mapped), pixel_count * 4u}, extent, render_engine.output().color_space);
        if (request.gbuffer_output_path) write_gbuffer_exr(*request.gbuffer_output_path, render_engine.readback(), render_engine.output().color_space, film.gbuffer_camera_space);
        if (request.telemetry_output_path) dynamics.write_telemetry(*request.telemetry_output_path);
    }
} // namespace spectra
