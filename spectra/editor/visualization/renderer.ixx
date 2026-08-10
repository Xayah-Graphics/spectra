export module spectra.editor:visualization.renderer;

import spectra.dynamics;
import spectra.render.contract;
import spectra.render.display;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct VisualizationRenderer {
        VisualizationRenderer(VulkanRuntime& runtime, std::filesystem::path shader_directory);

        VisualizationRenderer(const VisualizationRenderer&)            = delete;
        VisualizationRenderer(VisualizationRenderer&&)                 = delete;
        VisualizationRenderer& operator=(const VisualizationRenderer&) = delete;
        VisualizationRenderer& operator=(VisualizationRenderer&&)      = delete;

        void record(const vk::raii::CommandBuffer& command_buffer, DisplayPass& display, DepthBufferView depth, const scene::Camera& camera, std::span<const dynamics::GpuVisualizationDatasetView> views);

        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
    };
} // namespace spectra
