export module spectra.editor;

export import spectra.render.types;
import std;

namespace spectra::editor {
    export struct Request {
        std::optional<std::filesystem::path> scene_path{};
        std::optional<render::RendererKind> renderer{};
        render::RasterDisplayMode raster_display_mode{render::RasterDisplayMode::Material};
    };

    export void run(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);
} // namespace spectra::editor
