export module spectra.render.display;

import spectra.runtime;
import spectra.render.contract;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct DisplayPass {
        DisplayPass(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept;
        ~DisplayPass();

        DisplayPass(const DisplayPass&)            = delete;
        DisplayPass(DisplayPass&&)                 = delete;
        DisplayPass& operator=(const DisplayPass&) = delete;
        DisplayPass& operator=(DisplayPass&&)      = delete;

        void initialize();
        [[nodiscard]] bool resize(vk::Extent2D extent);
        void prepare_sampling(const vk::raii::CommandBuffer& command_buffer);
        void prepare_linear_composition(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output);
        [[nodiscard]] ColorCompositionTarget linear_target() noexcept;
        [[nodiscard]] RenderOutput linear_output(RenderOutput renderer_output) const noexcept;
        [[nodiscard]] ColorCompositionTarget target() noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, float exposure);

        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
        DescriptorLease sampler_descriptor{};
        DescriptorLease linear_sampled_descriptor{};
        GpuImage linear_image{};
        vk::ImageLayout linear_layout{vk::ImageLayout::eUndefined};
        scene::SpectrumColorSpace linear_color_space{scene::SpectrumColorSpace::Srgb};
        GpuImage image{};
        vk::ImageLayout layout{vk::ImageLayout::eUndefined};
    };
} // namespace spectra
