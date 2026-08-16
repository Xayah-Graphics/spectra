module spectra.render.composition;

import std;

namespace spectra {
    void record_render_composition(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, const RenderCompositionRequest& request) {
        const bool scene_visualizations   = request.visualization && request.compose_visualizations && request.visualization->has_visible(request.scene, request.visualizations, scene::VisualizationCompositionDomain::SceneLinear);
        const bool display_visualizations = request.visualization && request.compose_visualizations && request.visualization->has_visible(request.scene, request.visualizations, scene::VisualizationCompositionDomain::DisplayReferred);
        const bool camera_reference       = request.visualization && request.camera_reference && (request.camera_reference->overlay || request.camera_reference->plane);
        const bool neural_field           = request.neural_field && request.neural_field->has_visible(request.scene);
        if (!request.depth && (neural_field || scene_visualizations || display_visualizations || request.diagnostics || camera_reference)) throw std::runtime_error("Render composition requires Renderer depth");

        std::optional<RenderOutput> linear_composition{};
        if (neural_field || scene_visualizations) {
            display.prepare_linear_composition(command_buffer, request.renderer_output);
            if (neural_field) request.neural_field->record(command_buffer, display.linear_target(), *request.depth, request.scene, request.camera);
            if (scene_visualizations) request.visualization->record(command_buffer, display.linear_target(), *request.depth, request.scene, request.camera, request.visualizations, scene::VisualizationCompositionDomain::SceneLinear, nullptr);
            linear_composition.emplace(display.linear_output(request.renderer_output));
        }
        display.record(command_buffer, linear_composition ? *linear_composition : request.renderer_output, request.exposure);
        if (request.diagnostics) request.diagnostics->renderer.record(command_buffer, request.frame_slot_index, display.target(), *request.depth, request.scene, request.camera, request.scene_camera_view, request.diagnostics->scene_guides, request.diagnostics->entity_diagnostics, request.diagnostics->selection, request.diagnostics->visible);
        if (display_visualizations || camera_reference) request.visualization->record(command_buffer, display.target(), *request.depth, request.scene, request.camera, request.visualizations, scene::VisualizationCompositionDomain::DisplayReferred, request.camera_reference ? &*request.camera_reference : nullptr);
    }
} // namespace spectra
