module spectra.headless;

import spectra.dynamics.runtime;
import spectra.dynamics.frozen;
import spectra.diagnostics;
import spectra.diagnostics.renderer;
import spectra.render;
import spectra.render.capture;
import spectra.render.display;
import spectra.render.scene;
import spectra.runtime;
import spectra.scene;
import spectra.scene.document;
import spectra.scene.format;
import spectra.visualization;
import spectra.overlay;
import std;
import vulkan;

namespace spectra {
    void render_scene(RenderRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        if (request.gbuffer_output_path && request.renderer != pathtracer_descriptor.id) throw std::runtime_error("--gbuffer-output requires the Path Tracer");
        const RasterDisplayMode raster_display_mode = parse_raster_display_mode(request.raster_display_mode);
        const RenderOutputLayer output_layer = parse_render_output_layer(request.output_layer);
        const bool compose_diagnostics = request.composition == "all" || request.composition == "diagnostics";
        const bool compose_visualizations = request.composition == "all" || request.composition == "visualizations";
        const bool compose_overlays = request.composition == "all" || request.composition == "overlays";
        if (!compose_diagnostics && !compose_visualizations && !compose_overlays && request.composition != "none") throw std::runtime_error(std::format("Unknown composed output content: {}", request.composition));
        if ((request.axes || request.outlined_instance) && (output_layer != RenderOutputLayer::ComposedDisplay || !compose_overlays)) throw std::runtime_error("Axes and Instance outlines require overlays in composed-display output");
        if (request.renderer == pathtracer_descriptor.id && raster_display_mode != RasterDisplayMode::Material) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
        if (request.simulation_step != std::numeric_limits<std::uint64_t>::max() && request.simulation_seconds >= 0.0) throw std::runtime_error("Select either --simulation-step or --simulation-time");

        VulkanInstance instance{"Spectra Render"};
        VulkanRuntime runtime{instance};
        SceneDocument document{};
        document.content.source = scene::load_scene(request.scene_path);
        document.content.path   = request.scene_path;
        document.content.loaded = true;

        scene::Film& film     = *std::ranges::find(document.content.source.resources.films, document.content.source.active_film, &scene::Film::id);
        scene::Camera& camera = *std::ranges::find(document.content.source.resources.cameras, document.content.source.active_camera, &scene::Camera::id);
        if (request.gbuffer_output_path) film.gbuffer = true;
        document.content.source.mark_all_changed();
        document.content.evaluated = document.content.source;

        DynamicsRuntime dynamics{runtime, document};
        if (document.content.source.dynamic_setup || document.content.source.frozen_dynamic_frame) dynamics.initialize(request.scene_path, document.content.source);

        GpuScene gpu_scene{runtime, shader_directory};
        std::vector<GpuGeometryBinding> geometry_bindings{};
        geometry_bindings.reserve(dynamics.mesh_bindings().size());
        for (const dynamics::MeshOutputBinding& binding : dynamics.mesh_bindings())
            geometry_bindings.push_back(GpuGeometryBinding{
                binding.geometry_id,
                binding.update_mode == dynamics::MeshUpdateMode::Deformable ? GpuMeshUpdateMode::Deformable : GpuMeshUpdateMode::TopologyChanging,
                binding.vertex_capacity,
                binding.index_capacity,
            });
        std::vector<std::pair<scene::SphereSetId, std::uint32_t>> sphere_capacities{};
        sphere_capacities.reserve(dynamics.sphere_set_bindings().size());
        for (const dynamics::SphereSetOutputBinding& binding : dynamics.sphere_set_bindings()) sphere_capacities.emplace_back(binding.sphere_set_id, binding.capacity);
        gpu_scene.initialize(document.content.source, geometry_bindings, sphere_capacities);

        const auto consume_dynamic_frame = [&]() {
            if (!dynamics.pending_frame()) return;
            const FrameContext frame = runtime.frames.begin_frame();
            static_cast<void>(gpu_scene.apply(*dynamics.pending_frame(), document.content.evaluated.view(), frame.command_buffer));
            dynamics.record_telemetry(frame.command_buffer, frame.slot_index);
            dynamics.consume_frame();
            static_cast<void>(runtime.frames.submit_frame());
        };
        consume_dynamic_frame();
        if (dynamics.initialized()) {
            if (request.simulation_step != std::numeric_limits<std::uint64_t>::max()) {
                if (request.simulation_step < dynamics.timeline().step) throw std::runtime_error("The requested simulation step precedes the scene start step");
                dynamics.evaluate(request.simulation_step);
                consume_dynamic_frame();
            } else if (request.simulation_seconds >= 0.0) {
                if (request.simulation_seconds < dynamics.timeline().seconds) throw std::runtime_error("The requested simulation time precedes the scene start time");
                dynamics.evaluate_time(request.simulation_seconds);
                consume_dynamic_frame();
            }
            const std::uint64_t target_presentation_frame = request.presentation_frame == std::numeric_limits<std::uint64_t>::max() ? (request.presentation_seconds >= 0.0 ? 1u : 0u) : request.presentation_frame;
            if (target_presentation_frame < dynamics.presentation_timeline().frame) throw std::runtime_error("The requested presentation frame precedes the current frame");
            const std::uint64_t presentation_ticks = target_presentation_frame - dynamics.presentation_timeline().frame;
            const double target_presentation_seconds = request.presentation_seconds >= 0.0 ? request.presentation_seconds : dynamics.presentation_timeline().seconds;
            if (target_presentation_seconds < dynamics.presentation_timeline().seconds) throw std::runtime_error("The requested presentation time precedes the current time");
            for (std::uint64_t tick = 0; tick != presentation_ticks; ++tick) {
                const double remaining_seconds = target_presentation_seconds - dynamics.presentation_timeline().seconds;
                dynamics.advance(std::chrono::duration<double>{remaining_seconds / static_cast<double>(presentation_ticks - tick)});
                consume_dynamic_frame();
            }
        } else if (request.simulation_step != std::numeric_limits<std::uint64_t>::max() || request.simulation_seconds >= 0.0 || request.presentation_frame != std::numeric_limits<std::uint64_t>::max() || request.presentation_seconds >= 0.0)
            throw std::runtime_error("Dynamic time targets require a scene with an enabled Dynamic Setup");

        RenderEngine render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer, raster_display_mode};
        render_engine.rebuild(document.content.evaluated.view());
        if (request.renderer == pathtracer_descriptor.id) render_engine.wait_for_pathtracer();
        DisplayPass display{runtime, shader_directory};
        display.initialize();
        VisualizationRenderer visualization{runtime, shader_directory};
        SceneDiagnosticRenderer diagnostics{runtime, gpu_scene, shader_directory};
        diagnostics.initialize();
        std::unique_ptr<ViewportOverlay> overlay{};
        if (compose_overlays && (request.axes || request.outlined_instance)) {
            overlay = std::make_unique<ViewportOverlay>(runtime, gpu_scene, shader_directory);
            overlay->initialize();
        }

        const vk::Extent2D extent{film.resolution[0], film.resolution[1]};
        static_cast<void>(display.resize(extent));
        const RenderView view{camera, extent, 1};
        GpuBuffer linear_readback{};
        GpuBuffer display_readback{};
        std::uint32_t final_frame_slot{};
        std::uint32_t next_progress_report{1};
        bool complete{};
        while (!complete) {
            const FrameContext frame = runtime.frames.begin_frame();
            GpuSceneUpdate gpu_update{};
            if (dynamics.initialized())
                if (const dynamics::DynamicFrame* dynamic_frame = dynamics.pending_frame()) gpu_update = gpu_scene.apply(*dynamic_frame, document.content.evaluated.view(), frame.command_buffer);

            scene::SceneView current_scene = document.content.evaluated.view();
            current_scene.revision.changes = current_scene.revision.changes | gpu_update.scene_changes;
            render_engine.invalidate(current_scene.revision.changes, gpu_update);
            static_cast<void>(render_engine.prepare(current_scene, view, frame.command_buffer));
            render_engine.record(frame.command_buffer, frame.slot_index);
            const std::optional<RenderProgress> progress = render_engine.progress();
            complete                                     = !progress || progress->completed >= progress->target;
            if (progress && (complete || progress->completed >= next_progress_report)) {
                std::println(std::cerr, "Samples: {} / {}", progress->completed, progress->target);
                next_progress_report = progress->completed + std::max(progress->target / 100u, 1u);
            }
            if (complete) {
                const RenderOutput renderer_output = render_engine.output();
                std::optional<RenderOutput> renderer_composition{};
                const std::optional<DepthBufferView> depth = render_engine.depth_buffer();
                if (output_layer == RenderOutputLayer::ComposedDisplay && compose_visualizations && !depth && visualization.has_visible(dynamics.visualizations(), scene::VisualizationCompositionDomain::SceneLinear)) throw std::runtime_error("Scene-linear Visualization composition requires Renderer depth");
                if (output_layer == RenderOutputLayer::ComposedDisplay && compose_visualizations && depth && visualization.has_visible(dynamics.visualizations(), scene::VisualizationCompositionDomain::SceneLinear)) {
                    display.prepare_linear_composition(frame.command_buffer, renderer_output);
                    visualization.record(frame.command_buffer, display.linear_target(), *depth, camera, dynamics.visualizations(), scene::VisualizationCompositionDomain::SceneLinear);
                    renderer_composition.emplace(display.linear_output(renderer_output));
                }
                if (output_layer != RenderOutputLayer::RendererLinear) {
                    display.record(frame.command_buffer, renderer_composition ? *renderer_composition : renderer_output, 0.0f);
                    if (output_layer == RenderOutputLayer::ComposedDisplay && !depth && (compose_diagnostics || (compose_visualizations && visualization.has_visible(dynamics.visualizations(), scene::VisualizationCompositionDomain::DisplayReferred)))) throw std::runtime_error("Composed Diagnostics and Visualization require Renderer depth");
                    if (output_layer == RenderOutputLayer::ComposedDisplay && depth) {
                        if (compose_diagnostics) diagnostics.record(frame.command_buffer, frame.slot_index, display, *depth, document.content.evaluated.view(), camera, SceneDiagnosticSettings{}, SelectionState{});
                        if (compose_visualizations) visualization.record(frame.command_buffer, display.target(), *depth, camera, dynamics.visualizations(), scene::VisualizationCompositionDomain::DisplayReferred);
                    }
                    if (output_layer == RenderOutputLayer::ComposedDisplay && overlay) {
                        std::array<scene::InstanceId, 1> outlined{};
                        std::span<const scene::InstanceId> selected{};
                        if (request.outlined_instance) {
                            outlined[0] = scene::InstanceId{*request.outlined_instance};
                            selected = outlined;
                        }
                        overlay->record(frame.command_buffer, display, camera, ViewportOverlayState{.selected_instances = selected, .axes_plane = request.axes_plane, .axes_visible = request.axes});
                    }
                    record_display_readback(runtime, frame.command_buffer, display.image, display.layout, display_readback);
                }
                record_linear_readback(runtime, frame.command_buffer, renderer_output, linear_readback);
            }
            if (dynamics.initialized()) {
                dynamics.record_telemetry(frame.command_buffer, frame.slot_index);
                dynamics.consume_frame();
            }
            document.content.evaluated.acknowledge_changes();
            document.content.source.acknowledge_changes();
            final_frame_slot = runtime.frames.submit_frame();
        }
        runtime.frames.wait_frame(final_frame_slot);
        if (dynamics.initialized()) for (std::uint32_t slot = 0; slot != VulkanFrames::frames_in_flight; ++slot) dynamics.resolve_telemetry(slot);

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        if (output_layer != RenderOutputLayer::RendererLinear) write_png(request.png_output_path, std::span{static_cast<const std::uint8_t*>(display_readback.mapped), pixel_count * 4u}, extent);
        write_linear_exr(request.linear_output_path, std::span{static_cast<const float*>(linear_readback.mapped), pixel_count * 4u}, extent, render_engine.output().color_space);
        if (request.gbuffer_output_path) write_gbuffer_exr(*request.gbuffer_output_path, render_engine.readback(), render_engine.output().color_space, film.gbuffer_camera_space);
        if (request.telemetry_output_path) dynamics::write_telemetry(*request.telemetry_output_path, dynamics.telemetry_frame());
    }
} // namespace spectra
