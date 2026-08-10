export module spectra.render.display;

import spectra.runtime;
import spectra.render.contract;
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
