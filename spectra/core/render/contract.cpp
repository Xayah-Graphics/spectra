module spectra.render.contract;

import std;

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
        if (identifier == "material-wireframe") return RasterDisplayMode::MaterialWireframe;
        throw std::runtime_error(std::format("Unknown Raster display mode: {}", identifier));
    }
} // namespace spectra
