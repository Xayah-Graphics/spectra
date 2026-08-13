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
        std::vector<const char*> arguments{};
        arguments.reserve(static_cast<std::size_t>(argument_count));
        for (int index = 0; index != argument_count; ++index) arguments.push_back(raw_arguments[index]);

        std::filesystem::path scene_path{};
        std::optional<std::string> renderer{};
        std::optional<std::string> raster_display_mode{};
        std::optional<std::string> output_layer{};
        std::optional<std::string> composition{};
        bool axes{};
        std::optional<std::string> axes_plane_text{};
        std::optional<std::string> outlined_instance_text{};
        std::optional<std::string> simulation_step_text{};
        std::optional<std::string> simulation_seconds_text{};
        std::filesystem::path output_base{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::optional<std::filesystem::path> telemetry_output_path{};
#if defined(SPECTRA_HAS_EDITOR)
        bool gui{};
#endif

        xayah::util::Command command{"Spectra scene visualization and physically based rendering."};
        command | xayah::util::positional({.name = "scene", .description = "Scene file to open or render.", .required = false}, scene_path) | xayah::util::option({.long_name = "renderer", .value_name = "RENDERER", .description = "Select rasterizer or pathtracer.", .show_default = false}, renderer) | xayah::util::option({.long_name = "raster-mode", .value_name = "MODE", .description = "Select material or wireframe Raster display.", .show_default = false}, raster_display_mode) | xayah::util::option({.long_name = "output-layer", .value_name = "LAYER", .description = "Select renderer-linear, renderer-display, or composed-display."}, output_layer) | xayah::util::option({.long_name = "composition", .value_name = "CONTENT", .description = "Select all, diagnostics, visualizations, overlays, or none for composed output."}, composition) | xayah::util::option({.long_name = "axes", .description = "Include the world axes overlay in composed output."}, axes)
            | xayah::util::option({.long_name = "axes-plane", .value_name = "PLANE", .description = "Select the axes grid plane."}, axes_plane_text) | xayah::util::option({.long_name = "outline-instance", .value_name = "ID", .description = "Outline one Scene Instance ID in composed output.", .show_default = false}, outlined_instance_text) | xayah::util::option({.long_name = "simulation-step", .value_name = "STEP", .description = "Evaluate a dynamic scene at an exact simulation step.", .show_default = false}, simulation_step_text) | xayah::util::option({.long_name = "simulation-time", .value_name = "SECONDS", .description = "Evaluate the fixed simulation step at or immediately before this time.", .show_default = false}, simulation_seconds_text) | xayah::util::option({.long_name = "output", .value_name = "BASE", .description = "Override the default renders/<scene>/<renderer> basename.", .show_default = false}, output_base)
            | xayah::util::option({.long_name = "gbuffer-output", .value_name = "IMAGE", .description = "Optional GBuffer EXR output.", .show_default = false}, gbuffer_output_path) | xayah::util::option({.long_name = "telemetry-output", .value_name = "CSV", .description = "Optional current Telemetry CSV output.", .show_default = false}, telemetry_output_path);
#if defined(SPECTRA_HAS_EDITOR)
        command | xayah::util::option({.long_name = "gui", .description = "Open the visualization UI."}, gui);
#endif

        const std::expected<xayah::util::ParseResult, std::string> parsed = command.parse(arguments);
        if (!parsed) throw std::runtime_error(parsed.error());
        if (parsed->help_requested) {
            std::print("{}", command.help(arguments, {}));
            return 0;
        }

        const std::optional<std::uint64_t> simulation_step   = numeric_option<std::uint64_t>(simulation_step_text, "simulation step");
        const std::optional<double> simulation_seconds       = numeric_option<double>(simulation_seconds_text, "simulation time");
        const std::optional<std::uint32_t> axes_plane        = numeric_option<std::uint32_t>(axes_plane_text, "axes plane");
        const std::optional<std::uint64_t> outlined_instance = numeric_option<std::uint64_t>(outlined_instance_text, "outlined instance");

        const std::filesystem::path application_directory = executable_directory();
        const std::filesystem::path resource_directory    = (application_directory / SPECTRA_RESOURCE_DIRECTORY).lexically_normal();
        const std::filesystem::path shader_directory      = resource_directory / "shaders";
        const std::filesystem::path pathtracer_directory  = resource_directory / "pathtracer";

#if defined(SPECTRA_HAS_EDITOR)
        if (gui) {
            if (output_layer || composition || axes || axes_plane || outlined_instance || simulation_step || simulation_seconds || !output_base.empty() || gbuffer_output_path || telemetry_output_path) throw std::runtime_error("Headless-only options cannot be used with --gui");
            const std::optional<std::filesystem::path> initial_scene = scene_path.empty() ? std::nullopt : std::optional{scene_path};
            spectra::run_editor({.scene_path = initial_scene, .renderer = std::move(renderer), .raster_display_mode = raster_display_mode.value_or("material")}, shader_directory, pathtracer_directory);
            return 0;
        }
#endif

        if (scene_path.empty()) throw std::runtime_error("Headless rendering requires a scene path");

        const std::string selected_renderer            = renderer.value_or("pathtracer");
        const std::string selected_raster_display_mode = raster_display_mode.value_or("material");
        if (output_base.empty()) {
            const std::string output_name = selected_renderer == "rasterizer" && selected_raster_display_mode != "material" ? std::format("{}-{}", selected_renderer, selected_raster_display_mode) : selected_renderer;
            output_base                   = std::filesystem::current_path() / "renders" / scene_path.stem() / output_name;
        }
        std::filesystem::path png_output_path = output_base;
        png_output_path += ".png";
        std::filesystem::path linear_output_path = output_base;
        linear_output_path += ".exr";
        spectra::render_scene(
            {
                .scene_path            = scene_path,
                .png_output_path       = png_output_path,
                .linear_output_path    = linear_output_path,
                .renderer              = selected_renderer,
                .raster_display_mode   = selected_raster_display_mode,
                .output_layer          = output_layer.value_or("renderer-display"),
                .composition           = composition.value_or("all"),
                .gbuffer_output_path   = gbuffer_output_path,
                .telemetry_output_path = telemetry_output_path,
                .outlined_instance     = outlined_instance,
                .simulation_step       = simulation_step,
                .simulation_seconds    = simulation_seconds,
                .axes_plane            = axes_plane.value_or(2u),
                .axes                  = axes,
            },
            shader_directory, pathtracer_directory);
        if (output_layer.value_or("renderer-display") != "renderer-linear") std::println("{}", png_output_path.string());
        std::println("{}", linear_output_path.string());
        if (gbuffer_output_path) std::println("{}", gbuffer_output_path->string());
        if (telemetry_output_path) std::println("{}", telemetry_output_path->string());
        return 0;
    } catch (const std::exception& error) {
        std::println(std::cerr, "Spectra failed: {}", error.what());
        return 1;
    }
}
