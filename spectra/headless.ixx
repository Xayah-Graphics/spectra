export module spectra.headless;

import std;

namespace spectra {
    export struct RenderRequest {
        std::filesystem::path scene_path{};
        std::filesystem::path png_output_path{};
        std::filesystem::path linear_output_path{};
        std::string renderer{"rasterizer"};
        std::string raster_display_mode{"material"};
        std::string output_layer{"renderer-display"};
        std::string composition{"all"};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::optional<std::filesystem::path> telemetry_output_path{};
        std::optional<std::uint64_t> outlined_instance{};
        std::uint64_t simulation_step{std::numeric_limits<std::uint64_t>::max()};
        double simulation_seconds{-1.0};
        std::uint64_t presentation_frame{std::numeric_limits<std::uint64_t>::max()};
        double presentation_seconds{-1.0};
        std::uint32_t axes_plane{2};
        bool axes{};
    };

    export void render_scene(RenderRequest request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);
} // namespace spectra
