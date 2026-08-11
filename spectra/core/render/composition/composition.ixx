export module spectra.render.composition;

import spectra.dynamics.gpu;
import spectra.render.contract;
import spectra.render.composition.diagnostics;
import spectra.render.composition.visualization;
import spectra.render.display;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct SceneDiagnosticsComposition {
        SceneDiagnosticRenderer& renderer;
        const SceneDiagnosticSettings& settings;
        const SelectionState& selection;
    };

    export struct RenderCompositionRequest {
        RenderOutput renderer_output;
        std::optional<DepthBufferView> depth{};
        scene::SceneView scene;
        scene::Camera camera{};
        std::optional<scene::CameraId> scene_camera_view{};
        std::span<const dynamics::GpuVisualization> visualizations{};
        VisualizationRenderer* visualization{};
        std::optional<SceneDiagnosticsComposition> diagnostics{};
        std::uint32_t frame_slot_index{};
        float exposure{};
        bool compose_visualizations{};
    };

    export void record_render_composition(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, const RenderCompositionRequest& request);
} // namespace spectra
