module spectra.render.contract;

import std;
import vulkan;

namespace spectra {
    RenderOutputLayer parse_render_output_layer(const std::string_view identifier) {
        if (identifier == "renderer-linear") return RenderOutputLayer::RendererLinear;
        if (identifier == "renderer-display") return RenderOutputLayer::RendererDisplay;
        if (identifier == "composed-display") return RenderOutputLayer::ComposedDisplay;
        throw std::runtime_error(std::format("Unknown render output layer: {}", identifier));
    }

    RasterDisplayMode parse_raster_display_mode(const std::string_view identifier) {
        if (identifier == "material") return RasterDisplayMode::Material;
        if (identifier == "wireframe") return RasterDisplayMode::Wireframe;
        throw std::runtime_error(std::format("Unknown Raster display mode: {}", identifier));
    }

    void set_basic_graphics_state(const vk::raii::CommandBuffer& command_buffer) {
        command_buffer.setRasterizerDiscardEnable(vk::False);
        command_buffer.setPolygonModeEXT(vk::PolygonMode::eFill);
        command_buffer.setRasterizationSamplesEXT(vk::SampleCountFlagBits::e1);
        command_buffer.setAlphaToCoverageEnableEXT(vk::False);
        command_buffer.setDepthBiasEnable(vk::False);
        command_buffer.setStencilTestEnable(vk::False);
        constexpr vk::SampleMask sample_mask = 1;
        command_buffer.setSampleMaskEXT(vk::SampleCountFlagBits::e1, sample_mask);
    }
} // namespace spectra
