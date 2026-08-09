module spectra.headless;

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
    void render_scene(RenderRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory) {
        if (request.gbuffer_output_path && request.renderer != PathTracer::descriptor.id) throw std::runtime_error("--gbuffer-output requires the Path Tracer");

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

        DynamicWorld dynamics{runtime, document};
        if (document.content.source.dynamic_setup) dynamics.initialize(request.scene_path, document.content.source);

        GpuScene gpu_scene{runtime, document, dynamics, shader_directory};
        Renderers renderers{runtime, gpu_scene, shader_directory, pathtracer_directory, request.renderer};
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

        renderers.rebuild(document.content.evaluated.view());
        if (request.renderer == PathTracer::descriptor.id) renderers.wait_for_pathtracer();
        DisplayRenderer display{runtime, shader_directory};
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
            scene::SceneChange binding_changes{scene::SceneChange::None};
            if (dynamics.configuration.initialized)
                if (const dynamics::DynamicFrame* dynamic_frame = dynamics.pending_frame()) binding_changes = gpu_scene.apply(*dynamic_frame, frame.command_buffer);

            scene::SceneView current_scene = document.content.evaluated.view();
            current_scene.revision.changes = current_scene.revision.changes | binding_changes;
            renderers.invalidate(current_scene.revision.changes);
            static_cast<void>(renderers.prepare(current_scene, view, frame.command_buffer));
            renderers.record(frame.command_buffer, frame.slot_index);
            const std::optional<RenderProgress> progress = renderers.progress();
            complete                                     = !progress || progress->completed >= progress->target;
            if (progress && (complete || progress->completed >= next_progress_report)) {
                std::println(std::cerr, "Samples: {} / {}", progress->completed, progress->target);
                next_progress_report = progress->completed + std::max(progress->target / 100u, 1u);
            }
            if (complete) {
                display.record(frame.command_buffer, renderers.output(), 0.0f);
                record_display_readback(runtime, frame.command_buffer, display.image, display.layout, display_readback);
                record_linear_readback(runtime, frame.command_buffer, renderers.output(), linear_readback);
            }
            if (dynamics.configuration.initialized) dynamics.consume_frame();
            document.content.evaluated.acknowledge_changes();
            document.content.source.acknowledge_changes();
            final_frame_slot = runtime.frames.submit_frame();
        }
        runtime.frames.wait_frame(final_frame_slot);

        const std::size_t pixel_count = static_cast<std::size_t>(extent.width) * extent.height;
        write_png(request.png_output_path, std::span{static_cast<const std::uint8_t*>(display_readback.mapped), pixel_count * 4u}, extent);
        write_linear_exr(request.linear_output_path, std::span{static_cast<const float*>(linear_readback.mapped), pixel_count * 4u}, extent, film.color_space);
        if (request.gbuffer_output_path) write_gbuffer_exr(*request.gbuffer_output_path, renderers.readback(), film.color_space, film.gbuffer_camera_space);
    }
} // namespace spectra
