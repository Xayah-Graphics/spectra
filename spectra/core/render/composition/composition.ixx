export module spectra.render.composition;

import spectra.dynamics.gpu;
import spectra.render.contract;
import spectra.render.composition.diagnostics;
import spectra.render.composition.neural_field;
import spectra.render.composition.overlay;
import spectra.render.composition.visualization;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct SceneDiagnosticsComposition {
        SceneDiagnosticRenderer& renderer;
        const SceneGuideSettings& scene_guides;
        const EntityDiagnostics& entity_diagnostics;
        const SelectionState& selection;
        bool visible{true};
    };

    export struct ViewportOverlayComposition {
        ViewportOverlay& renderer;
        const ViewportOverlayState& state;
    };

    export struct RenderCompositionRequest {
        RenderOutput renderer_output;
        std::optional<DepthBufferView> depth{};
        scene::SceneView scene;
        scene::Camera camera{};
        std::optional<scene::CameraId> scene_camera_view{};
        std::span<const dynamics::GpuVisualization> visualizations{};
        VisualizationRenderer* visualization{};
        NeuralFieldRenderer* neural_field{};
        std::optional<SceneDiagnosticsComposition> diagnostics{};
        std::optional<CameraReferenceVisualization> camera_reference{};
        std::optional<ViewportOverlayComposition> overlay{};
        std::uint32_t frame_slot_index{};
        float exposure{};
        bool compose_visualizations{};
    };

    export struct RenderCompositor {
        RenderCompositor(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept;
        ~RenderCompositor();

        RenderCompositor(const RenderCompositor&)            = delete;
        RenderCompositor(RenderCompositor&&)                 = delete;
        RenderCompositor& operator=(const RenderCompositor&) = delete;
        RenderCompositor& operator=(RenderCompositor&&)      = delete;

        void initialize();
        [[nodiscard]] bool resize(vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, const RenderCompositionRequest& request);
        void prepare_sampling(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] ColorCompositionTarget target() noexcept;

    private:
        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
        DescriptorLease sampler_descriptor{};
        DescriptorLease linear_sampled_descriptor{};
        DescriptorLease linear_storage_descriptor{};
        GpuImage linear_image{};
        vk::ImageLayout linear_layout{vk::ImageLayout::eUndefined};
        scene::SpectrumColorSpace linear_color_space{scene::SpectrumColorSpace::Srgb};
        GpuImage image{};
        vk::ImageLayout layout{vk::ImageLayout::eUndefined};

        void prepare_linear_composition(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output);
        [[nodiscard]] ColorCompositionTarget linear_target() noexcept;
        [[nodiscard]] RenderOutput linear_output(RenderOutput renderer_output) const noexcept;
        void record_display(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, float exposure);
    };
} // namespace spectra
