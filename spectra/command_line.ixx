export module spectra.command_line;

import spectra.headless;
import std;

export namespace spectra {
    struct CommandLine final {
        bool help_requested{};
        bool version_requested{};
#if defined(SPECTRA_HAS_EDITOR)
        bool gui{};
#endif
        bool axes{};
        std::filesystem::path scene_path{};
        std::filesystem::path output_base{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::optional<std::filesystem::path> telemetry_output_path{};
        std::optional<render::RendererKind> renderer{};
        render::RasterDisplayMode raster_display_mode{render::RasterDisplayMode::Material};
        headless::Output render_output{headless::Output::RendererDisplay};
        headless::DisplayLayers display_layers{};
        render::AxesPlane axes_plane{render::AxesPlane::Yz};
        std::optional<std::uint64_t> outlined_instance{};
        std::optional<std::uint64_t> simulation_step{};
        std::optional<double> simulation_seconds{};
        std::optional<std::uint64_t> presentation_frame{};
        std::optional<double> presentation_seconds{};
    };

    [[nodiscard]] CommandLine parse_command_line(std::span<char* const> arguments);
    [[nodiscard]] std::string command_line_help(std::string_view executable);
} // namespace spectra
