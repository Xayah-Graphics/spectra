#include <Windows.h>

#include <shellapi.h>

import spectra;
import std;
import xayah.util.xcli;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int argument_count{};
        auto argument_deleter = [](wchar_t** arguments) { LocalFree(arguments); };
        std::unique_ptr<wchar_t*, decltype(argument_deleter)> arguments{
            CommandLineToArgvW(GetCommandLineW(), &argument_count),
            argument_deleter,
        };
        if (!arguments) throw std::runtime_error("Windows failed to parse the Spectra command line");

        std::optional<std::filesystem::path> scene_path{};
        std::vector<std::filesystem::path> session_scene_roots{};
        std::optional<std::string> initial_renderer{};
        std::uint64_t maximum_frame_count{};

        xayah::util::Command command{"Spectra scene visualization and physically based rendering."};
        command
            | xayah::util::option(
                {
                    .long_name    = "scene",
                    .value_name   = "FILE",
                    .description  = "open a .spectra scene directly",
                    .show_default = false,
                },
                scene_path, {.requirement = xayah::util::PathRequirement::existing_file})
            | xayah::util::option(
                {
                    .long_name    = "scene-root",
                    .value_name   = "DIRECTORY",
                    .description  = "add a scene-library root for this session; may be repeated",
                    .show_default = false,
                },
                session_scene_roots, {.requirement = xayah::util::PathRequirement::existing_directory})
            | xayah::util::option(
                {
                    .long_name    = "renderer",
                    .value_name   = "NAME",
                    .description  = "start with rasterizer or pathtracer",
                    .show_default = false,
                },
                initial_renderer)
            | xayah::util::option(
                {
                    .long_name    = "frames",
                    .value_name   = "COUNT",
                    .description  = "exit after COUNT successfully presented frames",
                    .show_default = false,
                },
                maximum_frame_count)
            | xayah::util::validator("renderer",
                [&initial_renderer]() -> std::expected<void, std::string> {
                    if (initial_renderer && *initial_renderer != "rasterizer" && *initial_renderer != "pathtracer") return std::unexpected{"--renderer requires rasterizer or pathtracer"};
                    return {};
                })
            | xayah::util::validator("direct scene",
                [&command, &scene_path, &initial_renderer]() -> std::expected<void, std::string> {
                    if (!scene_path && (initial_renderer || command.option_provided("frames"))) return std::unexpected{"--renderer and --frames require --scene"};
                    return {};
                })
            | xayah::util::example("--scene scenes/cornell-box.spectra --renderer pathtracer --frames 5") | xayah::util::example("--scene-root ../research-scenes");

        const std::span<wchar_t* const> argument_span{arguments.get(), static_cast<std::size_t>(argument_count)};
        const std::expected<xayah::util::ParseResult, std::string> parse_result = command.parse(argument_span);
        if (!parse_result) throw std::runtime_error{parse_result.error()};
        if (parse_result->help_requested) {
            const std::string help = command.help(argument_span, {});
            MessageBoxA(nullptr, help.c_str(), "Spectra", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        const std::expected<void, std::string> validation = command.validate();
        if (!validation) throw std::runtime_error{validation.error()};

        std::optional<std::uint64_t> frame_limit{};
        if (command.option_provided("frames")) frame_limit = maximum_frame_count;

        spectra::Spectra application{std::move(scene_path), SPECTRA_SCENE_LIBRARY, std::move(session_scene_roots), SPECTRA_SHADER_DIRECTORY, std::move(initial_renderer)};
        application.run(frame_limit);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Spectra", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
