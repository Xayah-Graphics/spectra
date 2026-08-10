export module spectra.render.capture;

import spectra.render.contract;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export void record_linear_readback(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, RenderOutput render_output, GpuBuffer& readback_buffer);
    export void record_display_readback(VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const GpuImage& image, vk::ImageLayout image_layout, GpuBuffer& readback_buffer);
    export [[nodiscard]] RenderGBufferReadback materialize_gbuffer_readback(const RenderGBufferSnapshot& snapshot);
    export void write_png(const std::filesystem::path& path, std::span<const std::uint8_t> bgra, vk::Extent2D extent);
    export void write_linear_exr(const std::filesystem::path& path, std::span<const float> rgba, vk::Extent2D extent, scene::SpectrumColorSpace color_space);
    export void write_gbuffer_exr(const std::filesystem::path& path, const RenderGBufferReadback& readback, scene::SpectrumColorSpace color_space, bool camera_space);
} // namespace spectra
