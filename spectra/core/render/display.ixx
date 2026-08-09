export module spectra.display;

import spectra.runtime;
import spectra.render;
import std;
import vulkan;

namespace spectra {
    export struct DisplayRenderer {
        DisplayRenderer(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept;
        ~DisplayRenderer();

        DisplayRenderer(const DisplayRenderer&)            = delete;
        DisplayRenderer(DisplayRenderer&&)                 = delete;
        DisplayRenderer& operator=(const DisplayRenderer&) = delete;
        DisplayRenderer& operator=(DisplayRenderer&&)      = delete;

        void initialize();
        [[nodiscard]] bool resize(vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, float exposure);

        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        vk::raii::ShaderEXTs shaders{nullptr};
        DescriptorHandle sampler_descriptor{};
        GpuImage image{};
        vk::ImageLayout layout{vk::ImageLayout::eUndefined};
    };
} // namespace spectra
