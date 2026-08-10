export module spectra.visualization;

import spectra.dynamics;
import spectra.render.contract;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct VisualizationRenderer {
        VisualizationRenderer(VulkanRuntime& runtime, std::filesystem::path shader_directory);
        ~VisualizationRenderer();

        VisualizationRenderer(const VisualizationRenderer&)            = delete;
        VisualizationRenderer(VisualizationRenderer&&)                 = delete;
        VisualizationRenderer& operator=(const VisualizationRenderer&) = delete;
        VisualizationRenderer& operator=(VisualizationRenderer&&)      = delete;

        [[nodiscard]] bool has_visible(std::span<const dynamics::GpuVisualization> views, scene::VisualizationCompositionDomain domain) const noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, ColorCompositionTarget target, DepthBufferView depth, const scene::Camera& camera, std::span<const dynamics::GpuVisualization> views, scene::VisualizationCompositionDomain domain);

        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
    };
} // namespace spectra
