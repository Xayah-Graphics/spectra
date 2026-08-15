export module spectra.render.composition.visualization;

import spectra.dynamics.gpu;
import spectra.render.contract;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct CameraReferenceVisualization {
        const dynamics::CameraReferenceImage* reference{};
        const scene::Camera* camera{};
        math::Float4 overlay_rect{};
        bool overlay{};
        bool plane{};
    };

    export struct VisualizationRenderer {
        VisualizationRenderer(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~VisualizationRenderer();

        VisualizationRenderer(const VisualizationRenderer&)            = delete;
        VisualizationRenderer(VisualizationRenderer&&)                 = delete;
        VisualizationRenderer& operator=(const VisualizationRenderer&) = delete;
        VisualizationRenderer& operator=(VisualizationRenderer&&)      = delete;

        [[nodiscard]] bool has_visible(scene::SceneView scene, std::span<const dynamics::GpuVisualization> views, scene::VisualizationCompositionDomain domain) const noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, ColorCompositionTarget target, DepthBufferView depth, scene::SceneView scene, const scene::Camera& camera, std::span<const dynamics::GpuVisualization> views, scene::VisualizationCompositionDomain domain, const CameraReferenceVisualization* camera_reference);

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
    };
} // namespace spectra
