#include <cstdio>

#if defined(_WIN32)
#include <Windows.h>
#endif

import spectra;
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
} // namespace

int main(int argument_count, char** raw_arguments) {
    try {
        std::vector<const char*> arguments{};
        arguments.reserve(static_cast<std::size_t>(argument_count));
        for (int index = 0; index != argument_count; ++index) arguments.push_back(raw_arguments[index]);

        std::filesystem::path scene_path{};
        std::optional<std::string> renderer{};
        std::filesystem::path output_base{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
#if defined(SPECTRA_HAS_EDITOR)
        bool gui{};
#endif

        xayah::util::Command command{"Spectra scene visualization and physically based rendering."};
        command | xayah::util::positional({.name = "scene", .description = "Scene file to open or render."}, scene_path) | xayah::util::option({.long_name = "renderer", .value_name = "RENDERER", .description = "Select rasterizer or pathtracer.", .show_default = false}, renderer) | xayah::util::option({.long_name = "output", .value_name = "BASE", .description = "Override the default output/renders/<scene>/<renderer> basename.", .show_default = false}, output_base) | xayah::util::option({.long_name = "gbuffer-output", .value_name = "IMAGE", .description = "Optional GBuffer EXR output.", .show_default = false}, gbuffer_output_path);
#if defined(SPECTRA_HAS_EDITOR)
        command | xayah::util::option({.long_name = "gui", .description = "Open the visualization UI."}, gui);
#endif

        const std::expected<xayah::util::ParseResult, std::string> parsed = command.parse(arguments);
        if (!parsed) throw std::runtime_error(parsed.error());
        if (parsed->help_requested) {
            std::print("{}", command.help(arguments, {}));
            return 0;
        }

        const std::filesystem::path application_directory = executable_directory();
        const std::filesystem::path resource_directory    = application_directory / "resources";
        const std::filesystem::path output_directory      = application_directory / "output";
        const std::filesystem::path shader_directory      = resource_directory / "shaders";
        const std::filesystem::path pathtracer_directory  = resource_directory / "pathtracer";

#if defined(SPECTRA_HAS_EDITOR)
        if (gui) {
            const std::optional<std::filesystem::path> initial_scene = scene_path.empty() ? std::nullopt : std::optional{scene_path};
            spectra::run_editor({.scene_path = initial_scene, .renderer = std::move(renderer)}, shader_directory, pathtracer_directory, output_directory);
            return 0;
        }
#endif

        const std::string selected_renderer = renderer.value_or("rasterizer");
        if (output_base.empty()) {
            output_base = output_directory / "renders" / scene_path.stem() / selected_renderer;
            std::filesystem::create_directories(output_base.parent_path());
        }
        std::filesystem::path png_output_path = output_base;
        png_output_path += ".png";
        std::filesystem::path linear_output_path = output_base;
        linear_output_path += ".exr";
        spectra::render_scene(
            {
                .scene_path          = scene_path,
                .png_output_path     = png_output_path,
                .linear_output_path  = linear_output_path,
                .renderer            = selected_renderer,
                .gbuffer_output_path = gbuffer_output_path,
            },
            shader_directory, pathtracer_directory);
        std::println("{}", png_output_path.string());
        std::println("{}", linear_output_path.string());
        if (gbuffer_output_path) std::println("{}", gbuffer_output_path->string());
        return 0;
    } catch (const std::exception& error) {
        std::println(stderr, "Spectra failed: {}", error.what());
        return 1;
    }
}
