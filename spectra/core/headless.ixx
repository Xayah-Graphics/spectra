export module spectra.headless;

import std;

namespace spectra {
    export struct RenderRequest {
        std::filesystem::path scene_path{};
        std::filesystem::path png_output_path{};
        std::filesystem::path linear_output_path{};
        std::string renderer{"rasterizer"};
        std::optional<std::filesystem::path> gbuffer_output_path{};
    };

    export void render_scene(RenderRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);
} // namespace spectra
