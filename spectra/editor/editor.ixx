export module spectra.editor;

import std;

namespace spectra {
    export struct EditorRequest {
        std::optional<std::filesystem::path> scene_path{};
        std::optional<std::string> renderer{};
        std::string raster_display_mode{"material"};
    };

    export void run_editor(EditorRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory, const std::filesystem::path& output_directory);
} // namespace spectra
