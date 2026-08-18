export module spectra.headless.output;

import spectra.render.types;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra::headless {
    export void record_linear_readback(runtime::VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, render::RenderOutput render_output, runtime::GpuBuffer& readback_buffer);
    export void record_display_readback(runtime::VulkanRuntime& runtime, const vk::raii::CommandBuffer& command_buffer, const runtime::GpuImage& image, vk::ImageLayout image_layout, runtime::GpuBuffer& readback_buffer);
    export void write_png(const std::filesystem::path& path, std::span<const std::uint8_t> bgra, vk::Extent2D extent);
    export void write_linear_exr(const std::filesystem::path& path, std::span<const float> rgba, vk::Extent2D extent, scene::SpectrumColorSpace color_space);
    export void write_gbuffer_exr(const std::filesystem::path& path, const render::RenderGBufferReadback& readback, scene::SpectrumColorSpace color_space, bool camera_space);
} // namespace spectra::headless
