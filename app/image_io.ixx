export module spectra.app.image_io;

import spectra.render.output;
import spectra.scene;
import std;

namespace spectra::app {
    export void write_linear_exr(const std::filesystem::path& path, std::span<const float> rgba, std::uint32_t width, std::uint32_t height, scene::SpectrumColorSpace color_space);
    export void write_png(const std::filesystem::path& path, std::span<const std::uint8_t> rgba, std::uint32_t width, std::uint32_t height);
    export void write_render_readback_exr(const std::filesystem::path& path, const render::RenderReadback& readback, scene::SpectrumColorSpace color_space, bool camera_space);
} // namespace spectra::app
