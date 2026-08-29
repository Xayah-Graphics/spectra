#if defined(_WIN32)
#include <Windows.h>
#endif

import spectra.headless;
#if defined(SPECTRA_HAS_EDITOR)
import spectra.editor;
#endif
import std;
import xayah.util.xcli;

namespace {
    std::filesystem::path executable_directory() {
#if defined(_WIN32)
        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0 || length == path.size()) throw std::runtime_error("Failed to locate the Spectra executable");
        return std::filesystem::path{std::wstring_view{path.data(), length}}.parent_path();
#else
        return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
    }

    [[nodiscard]] spectra::render::RendererKind parse_renderer(const std::string_view identifier) {
        if (identifier == spectra::render::rasterizer_descriptor.id) return spectra::render::RendererKind::Rasterizer;
        if (identifier == spectra::render::pathtracer_descriptor.id) return spectra::render::RendererKind::PathTracer;
        throw std::runtime_error(std::format("Unknown renderer: {}", identifier));
    }

    [[nodiscard]] spectra::render::RasterDisplayMode parse_raster_display_mode(const std::string_view identifier) {
        if (identifier == "material") return spectra::render::RasterDisplayMode::Material;
        if (identifier == "wireframe") return spectra::render::RasterDisplayMode::Wireframe;
        throw std::runtime_error(std::format("Unknown Raster display mode: {}", identifier));
    }

    [[nodiscard]] spectra::headless::Output parse_headless_output(const std::string_view identifier) {
        if (identifier == "renderer-linear") return spectra::headless::Output::RendererLinear;
        if (identifier == "renderer-display") return spectra::headless::Output::RendererDisplay;
        if (identifier == "composed-display") return spectra::headless::Output::ComposedDisplay;
        throw std::runtime_error(std::format("Unknown headless output: {}", identifier));
    }

    [[nodiscard]] spectra::render::AxesPlane parse_axes_plane(const std::string_view identifier) {
        if (identifier == "xz") return spectra::render::AxesPlane::Xz;
        if (identifier == "xy") return spectra::render::AxesPlane::Xy;
        if (identifier == "yz") return spectra::render::AxesPlane::Yz;
        throw std::runtime_error(std::format("Unknown axes plane: {}", identifier));
    }

    template <class Number>
    [[nodiscard]] std::optional<Number> numeric_option(const std::optional<std::string>& text, const std::string_view name) {
        if (!text) return std::nullopt;
        Number value{};
        const std::from_chars_result result = std::from_chars(text->data(), text->data() + text->size(), value);
        if (result.ec != std::errc{} || result.ptr != text->data() + text->size()) throw std::runtime_error(std::format("Invalid {} value '{}'", name, *text));
        if constexpr (std::floating_point<Number>)
            if (!std::isfinite(value)) throw std::runtime_error(std::format("Invalid {} value '{}'", name, *text));
        return value;
    }
} // namespace

int main(int argument_count, char** raw_arguments) {
    try {
        const std::vector<const char*> arguments{raw_arguments, raw_arguments + argument_count};

        std::filesystem::path scene_path{};
        bool version_requested{};
        std::optional<std::string> renderer{};
        std::optional<std::string> raster_display_mode{};
        std::optional<std::string> output{};
        std::optional<std::string> display_layers{};
        bool axes{};
        std::optional<std::string> axes_plane_text{};
        std::optional<std::string> outlined_instance_text{};
        std::optional<std::string> simulation_step_text{};
        std::optional<std::string> simulation_seconds_text{};
        std::optional<std::string> presentation_frame_text{};
        std::optional<std::string> presentation_seconds_text{};
        std::filesystem::path output_base{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::optional<std::filesystem::path> telemetry_output_path{};
#if defined(SPECTRA_HAS_EDITOR)
        bool gui{};
#endif

        xayah::util::Command command{"Spectra scene visualization and physically based rendering."};
        command | xayah::util::positional({.name = "scene", .description = "Scene file to open or render.", .required = false}, scene_path) | xayah::util::option({.long_name = "version", .description = "Print the Spectra version."}, version_requested) | xayah::util::option({.long_name = "renderer", .value_name = "RENDERER", .description = "Select rasterizer or pathtracer.", .show_default = false}, renderer) | xayah::util::option({.long_name = "raster-mode", .value_name = "MODE", .description = "Select material or wireframe Raster display.", .show_default = false}, raster_display_mode) | xayah::util::option({.long_name = "render-output", .value_name = "OUTPUT", .description = "Select renderer-linear, renderer-display, or composed-display."}, output) | xayah::util::option({.long_name = "display-layers", .value_name = "LAYERS", .description = "Select all, diagnostics, visualizations, overlays, or none for composed output."}, display_layers)
            | xayah::util::option({.long_name = "axes", .description = "Include the world axes overlay in composed output."}, axes) | xayah::util::option({.long_name = "axes-plane", .value_name = "PLANE", .description = "Select the xz, xy, or yz axes grid plane."}, axes_plane_text) | xayah::util::option({.long_name = "outline-instance", .value_name = "ID", .description = "Outline one Scene Instance ID in composed output.", .show_default = false}, outlined_instance_text) | xayah::util::option({.long_name = "simulation-step", .value_name = "STEP", .description = "Evaluate a simulation scene at an exact step.", .show_default = false}, simulation_step_text) | xayah::util::option({.long_name = "simulation-time", .value_name = "SECONDS", .description = "Evaluate the fixed simulation step at or immediately before this time.", .show_default = false}, simulation_seconds_text) | xayah::util::option({.long_name = "presentation-frame", .value_name = "FRAME", .description = "Select a Presentation Sequence frame.", .show_default = false}, presentation_frame_text) | xayah::util::option({.long_name = "presentation-time", .value_name = "SECONDS", .description = "Select the Presentation Sequence frame at or immediately before this time.", .show_default = false}, presentation_seconds_text)
            | xayah::util::option({.long_name = "output", .value_name = "BASE", .description = "Override the default renders/<scene>/<renderer> basename.", .show_default = false}, output_base) | xayah::util::option({.long_name = "gbuffer-output", .value_name = "IMAGE", .description = "Optional GBuffer EXR output.", .show_default = false}, gbuffer_output_path) | xayah::util::option({.long_name = "telemetry-output", .value_name = "CSV", .description = "Optional current Telemetry CSV output.", .show_default = false}, telemetry_output_path);
#if defined(SPECTRA_HAS_EDITOR)
        command | xayah::util::option({.long_name = "gui", .description = "Open the visualization UI."}, gui);
#endif

        const std::expected<xayah::util::ParseResult, std::string> parsed = command.parse(arguments);
        if (!parsed) throw std::runtime_error(parsed.error());
        if (parsed->help_requested) {
            std::print("{}", command.help(arguments, {}));
            return 0;
        }
        if (version_requested) {
            std::println("Spectra {}", SPECTRA_VERSION);
            return 0;
        }

        const std::optional<std::uint64_t> simulation_step                    = numeric_option<std::uint64_t>(simulation_step_text, "simulation step");
        const std::optional<double> simulation_seconds                        = numeric_option<double>(simulation_seconds_text, "simulation time");
        const std::optional<std::uint64_t> presentation_frame                 = numeric_option<std::uint64_t>(presentation_frame_text, "presentation frame");
        const std::optional<double> presentation_seconds                      = numeric_option<double>(presentation_seconds_text, "presentation time");
        const std::optional<std::uint64_t> outlined_instance                  = numeric_option<std::uint64_t>(outlined_instance_text, "outlined instance");
        const std::optional<spectra::render::RendererKind> requested_renderer = renderer ? std::optional{parse_renderer(*renderer)} : std::nullopt;
        const spectra::render::RasterDisplayMode selected_raster_display_mode = parse_raster_display_mode(raster_display_mode.value_or("material"));
        const spectra::render::AxesPlane selected_axes_plane                  = parse_axes_plane(axes_plane_text.value_or("yz"));

        const std::filesystem::path application_directory = executable_directory();
        const std::filesystem::path resource_directory    = (application_directory / SPECTRA_RESOURCE_DIRECTORY).lexically_normal();
        const std::filesystem::path shader_directory      = resource_directory / "shaders";
        const std::filesystem::path pathtracer_directory  = resource_directory / "pathtracer";

#if defined(SPECTRA_HAS_EDITOR)
        if (gui) {
            if (output || display_layers || axes || axes_plane_text || outlined_instance || simulation_step || simulation_seconds || presentation_frame || presentation_seconds || !output_base.empty() || gbuffer_output_path || telemetry_output_path) throw std::runtime_error("Headless-only options cannot be used with --gui");
            if (requested_renderer == spectra::render::RendererKind::PathTracer && raster_display_mode) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
            const std::optional<std::filesystem::path> initial_scene = scene_path.empty() ? std::nullopt : std::optional{scene_path};
            spectra::editor::run({.scene_path = initial_scene, .renderer = requested_renderer, .raster_display_mode = selected_raster_display_mode}, shader_directory, pathtracer_directory);
            return 0;
        }
#endif

        if (scene_path.empty()) throw std::runtime_error("Headless rendering requires a scene path");

        const spectra::render::RendererKind selected_renderer = requested_renderer.value_or(spectra::render::RendererKind::PathTracer);
        const spectra::headless::Output selected_output       = parse_headless_output(output.value_or("renderer-display"));
        spectra::headless::DisplayLayers selected_display_layers{};
        if (display_layers && *display_layers != "all") {
            selected_display_layers = {false, false, false};
            if (*display_layers == "diagnostics") selected_display_layers.diagnostics = true;
            else if (*display_layers == "visualizations") selected_display_layers.visualizations = true;
            else if (*display_layers == "overlays") selected_display_layers.overlays = true;
            else if (*display_layers != "none") throw std::runtime_error(std::format("Unknown display layers: {}", *display_layers));
        }
        if (selected_renderer == spectra::render::RendererKind::PathTracer && raster_display_mode) throw std::runtime_error("--raster-mode only applies to the Rasterizer");
        if (gbuffer_output_path && selected_renderer != spectra::render::RendererKind::PathTracer) throw std::runtime_error("--gbuffer-output requires the Path Tracer");
        if ((axes || outlined_instance) && (selected_output != spectra::headless::Output::ComposedDisplay || !selected_display_layers.overlays)) throw std::runtime_error("Axes and Instance outlines require overlays in composed-display output");
        if (simulation_step && simulation_seconds) throw std::runtime_error("Select either --simulation-step or --simulation-time");
        if (presentation_frame && presentation_seconds) throw std::runtime_error("Select either --presentation-frame or --presentation-time");
        if (simulation_seconds && *simulation_seconds < 0.0) throw std::runtime_error("--simulation-time must be nonnegative");

        const spectra::render::RendererDescriptor selected_renderer_descriptor = spectra::render::renderer_descriptor(selected_renderer);
        if (output_base.empty()) {
            const std::string output_name = selected_renderer == spectra::render::RendererKind::Rasterizer && selected_raster_display_mode == spectra::render::RasterDisplayMode::Wireframe ? std::format("{}-wireframe", selected_renderer_descriptor.id) : std::string{selected_renderer_descriptor.id};
            output_base                   = std::filesystem::current_path() / "renders" / scene_path.stem() / output_name;
        }
        std::filesystem::path png_output_path = output_base;
        png_output_path += ".png";
        std::filesystem::path linear_output_path = output_base;
        linear_output_path += ".exr";
        spectra::headless::run(
            {
                .scene_path            = scene_path,
                .png_output_path       = png_output_path,
                .linear_output_path    = linear_output_path,
                .renderer              = selected_renderer,
                .raster_display_mode   = selected_raster_display_mode,
                .output                = selected_output,
                .display_layers        = selected_display_layers,
                .gbuffer_output_path   = gbuffer_output_path,
                .telemetry_output_path = telemetry_output_path,
                .outlined_instance     = outlined_instance,
                .simulation_step       = simulation_step,
                .simulation_seconds    = simulation_seconds,
                .presentation_frame    = presentation_frame,
                .presentation_seconds  = presentation_seconds,
                .axes_plane            = selected_axes_plane,
                .axes                  = axes,
            },
            shader_directory, pathtracer_directory);
        if (selected_output != spectra::headless::Output::RendererLinear) std::println("{}", png_output_path.string());
        std::println("{}", linear_output_path.string());
        if (gbuffer_output_path) std::println("{}", gbuffer_output_path->string());
        if (telemetry_output_path) std::println("{}", telemetry_output_path->string());
        return 0;
    } catch (const std::exception& error) {
        std::println(std::cerr, "Spectra failed: {}", error.what());
        return 1;
    }
}
