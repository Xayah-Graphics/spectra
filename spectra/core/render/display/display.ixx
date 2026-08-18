export module spectra.render.display;

export import spectra.render.display.types;
import spectra.render.display.diagnostics;
import spectra.render.display.neural_field;
import spectra.render.display.overlay;
import spectra.render.display.visualization;
import spectra.render.types;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra::render {
    export struct Compositor {
        Compositor(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, DisplayFeatures features = {});
        ~Compositor();

        Compositor(const Compositor&)            = delete;
        Compositor(Compositor&&)                 = delete;
        Compositor& operator=(const Compositor&) = delete;
        Compositor& operator=(Compositor&&)      = delete;

        [[nodiscard]] bool resize(vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, const DisplayRequest& request);
        void prepare_sampling(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] RenderOutput output() const noexcept;
        [[nodiscard]] const runtime::GpuImage* diagnostic_pick_image() const noexcept;
        [[nodiscard]] std::optional<scene::EntityReference> pick_entity(std::uint32_t frame_slot_index, std::uint32_t pick_index) const noexcept;

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        std::optional<DiagnosticsPass> diagnostics{};
        std::optional<NeuralFieldPass> neural_fields{};
        std::optional<VisualizationPass> visualizations{};
        std::optional<OverlayPass> overlays{};
        vk::raii::ShaderEXTs shaders{nullptr};
        runtime::DescriptorLease sampler_descriptor{};
        runtime::DescriptorLease linear_sampled_descriptor{};
        runtime::DescriptorLease linear_storage_descriptor{};
        runtime::DescriptorLease sampled_descriptor{};
        runtime::GpuImage linear_image{};
        vk::ImageLayout linear_layout{vk::ImageLayout::eUndefined};
        scene::SpectrumColorSpace linear_color_space{scene::SpectrumColorSpace::Srgb};
        runtime::GpuImage image{};
        vk::ImageLayout layout{vk::ImageLayout::eUndefined};

        [[nodiscard]] ColorTarget target() noexcept;
        void prepare_linear_composition(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output);
        [[nodiscard]] ColorTarget linear_target() noexcept;
        [[nodiscard]] RenderOutput linear_output(RenderOutput renderer_output) const noexcept;
        void record_display(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, float exposure);
    };
} // namespace spectra::render
