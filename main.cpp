#if defined(_WIN32)
#include <Windows.h>
#endif

import spectra.command_line;
import spectra.headless;
#if defined(SPECTRA_HAS_EDITOR)
import spectra.editor;
#endif
import std;

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
} // namespace

int main(const int argument_count, char** raw_arguments) {
    try {
        const spectra::CommandLine command_line = spectra::parse_command_line({raw_arguments, static_cast<std::size_t>(argument_count)});
        if (command_line.help_requested) {
            std::print("{}", spectra::command_line_help(raw_arguments[0]));
            return 0;
        }
        if (command_line.version_requested) {
            std::println("Spectra {}", SPECTRA_VERSION);
            return 0;
        }

        const std::filesystem::path application_directory = executable_directory();
        const std::filesystem::path resource_directory    = (application_directory / SPECTRA_RESOURCE_DIRECTORY).lexically_normal();
        const std::filesystem::path shader_directory      = resource_directory / "shaders";
        const std::filesystem::path pathtracer_directory  = resource_directory / "pathtracer";

#if defined(SPECTRA_HAS_EDITOR)
        if (command_line.gui) {
            const std::optional<std::filesystem::path> initial_scene = command_line.scene_path.empty() ? std::nullopt : std::optional{command_line.scene_path};
            spectra::editor::run({.scene_path = initial_scene, .renderer = command_line.renderer, .raster_display_mode = command_line.raster_display_mode}, shader_directory, pathtracer_directory);
            return 0;
        }
#endif

        const spectra::render::RendererKind renderer                  = command_line.renderer.value_or(spectra::render::RendererKind::PathTracer);
        const spectra::render::RendererDescriptor renderer_descriptor = spectra::render::renderer_descriptor(renderer);
        std::filesystem::path output_base                             = command_line.output_base;
        if (output_base.empty()) {
            const std::string output_name = renderer == spectra::render::RendererKind::Rasterizer && command_line.raster_display_mode == spectra::render::RasterDisplayMode::Wireframe ? std::format("{}-wireframe", renderer_descriptor.id) : std::string{renderer_descriptor.id};
            output_base                   = std::filesystem::current_path() / "renders" / command_line.scene_path.stem() / output_name;
        }
        std::filesystem::path png_output_path = output_base;
        png_output_path += ".png";
        std::filesystem::path linear_output_path = output_base;
        linear_output_path += ".exr";
        spectra::headless::run(
            {
                .scene_path            = command_line.scene_path,
                .png_output_path       = png_output_path,
                .linear_output_path    = linear_output_path,
                .renderer              = renderer,
                .raster_display_mode   = command_line.raster_display_mode,
                .output                = command_line.render_output,
                .display_layers        = command_line.display_layers,
                .gbuffer_output_path   = command_line.gbuffer_output_path,
                .telemetry_output_path = command_line.telemetry_output_path,
                .outlined_instance     = command_line.outlined_instance,
                .simulation_step       = command_line.simulation_step,
                .simulation_seconds    = command_line.simulation_seconds,
                .presentation_frame    = command_line.presentation_frame,
                .presentation_seconds  = command_line.presentation_seconds,
                .axes_plane            = command_line.axes_plane,
                .axes                  = command_line.axes,
            },
            shader_directory, pathtracer_directory);
        if (command_line.render_output != spectra::headless::Output::RendererLinear) std::println("{}", png_output_path.string());
        std::println("{}", linear_output_path.string());
        if (command_line.gbuffer_output_path) std::println("{}", command_line.gbuffer_output_path->string());
        if (command_line.telemetry_output_path) std::println("{}", command_line.telemetry_output_path->string());
        return 0;
    } catch (const std::exception& error) {
        std::println(std::cerr, "Spectra failed: {}", error.what());
        return 1;
    }
}
