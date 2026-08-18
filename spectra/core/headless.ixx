export module spectra.headless;

export import spectra.render.types;
export import spectra.render.display;
import std;

namespace spectra::headless {
    export enum class Output : std::uint8_t {
        RendererLinear,
        RendererDisplay,
        ComposedDisplay,
    };

    export struct DisplayLayers {
        bool diagnostics{true};
        bool visualizations{true};
        bool overlays{true};
    };

    export struct Request {
        std::filesystem::path scene_path{};
        std::filesystem::path png_output_path{};
        std::filesystem::path linear_output_path{};
        render::RendererKind renderer{render::RendererKind::PathTracer};
        render::RasterDisplayMode raster_display_mode{render::RasterDisplayMode::Material};
        Output output{Output::RendererDisplay};
        DisplayLayers display_layers{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::optional<std::filesystem::path> telemetry_output_path{};
        std::optional<std::uint64_t> outlined_instance{};
        std::optional<std::uint64_t> simulation_step{};
        std::optional<double> simulation_seconds{};
        render::AxesPlane axes_plane{render::AxesPlane::Yz};
        bool axes{};
    };

    export void run(Request request, const std::filesystem::path& shader_directory, const std::filesystem::path& pathtracer_directory);
} // namespace spectra::headless
