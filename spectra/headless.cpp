module spectra.headless;

import spectra.dynamics.runtime;
import spectra.render;
import spectra.render.capture;
import spectra.render.display;
import spectra.render.scene;
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
        if (request.renderer == pathtracer_descriptor.id && raster_display_mode != RasterDisplayMode::Material) throw std::runtime_error("--raster-mode only applies to the Rasterizer");

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
        if (document.content.source.dynamic_setup) dynamics.initialize(request.scene_path, document.content.source);

        GpuScene gpu_scene{runtime, shader_directory};
        RenderEngine render_engine{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer, raster_display_mode};
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

        render_engine.rebuild(document.content.evaluated.view());
        if (request.renderer == pathtracer_descriptor.id) render_engine.wait_for_pathtracer();
        DisplayPass display{runtime, shader_directory};
        display.initialize();

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
                display.record(frame.command_buffer, render_engine.output(), 0.0f);
                record_display_readback(runtime, frame.command_buffer, display.image, display.layout, display_readback);
                record_linear_readback(runtime, frame.command_buffer, render_engine.output(), linear_readback);
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

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        write_png(request.png_output_path, std::span{static_cast<const std::uint8_t*>(display_readback.mapped), pixel_count * 4u}, extent);
        write_linear_exr(request.linear_output_path, std::span{static_cast<const float*>(linear_readback.mapped), pixel_count * 4u}, extent, film.color_space);
        if (request.gbuffer_output_path) write_gbuffer_exr(*request.gbuffer_output_path, render_engine.readback(), film.color_space, film.gbuffer_camera_space);
    }
} // namespace spectra
