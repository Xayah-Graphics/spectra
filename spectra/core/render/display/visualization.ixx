export module spectra.render.display.visualization;

import spectra.render.display.types;
import spectra.simulation.frame;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

export namespace spectra::render {
    struct VisualizationPass {
        VisualizationPass(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory);
        ~VisualizationPass();

        VisualizationPass(const VisualizationPass&)            = delete;
        VisualizationPass(VisualizationPass&&)                 = delete;
        VisualizationPass& operator=(const VisualizationPass&) = delete;
        VisualizationPass& operator=(VisualizationPass&&)      = delete;

        [[nodiscard]] bool has_visible(scene::ResolvedSceneView scene, std::span<const simulation::GpuVisualization> views, scene::VisualizationCompositionDomain domain) const noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, ColorTarget target, DepthBufferView depth, scene::ResolvedSceneView scene, const scene::Camera& camera, std::span<const simulation::GpuVisualization> views, scene::VisualizationCompositionDomain domain, const CameraReferenceRequest* camera_reference);

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
    };
} // namespace spectra::render
