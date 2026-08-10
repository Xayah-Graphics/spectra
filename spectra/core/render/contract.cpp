module spectra.render.contract;

import std;

namespace spectra {
    RasterDisplayMode parse_raster_display_mode(const std::string_view identifier) {
        if (identifier == "material") return RasterDisplayMode::Material;
        if (identifier == "wireframe") return RasterDisplayMode::Wireframe;
        if (identifier == "material-wireframe") return RasterDisplayMode::MaterialWireframe;
        throw std::runtime_error(std::format("Unknown Raster display mode: {}", identifier));
    }
} // namespace spectra
