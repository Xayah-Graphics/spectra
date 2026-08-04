#include <Windows.h>

#include <cstdio>
#include <shellapi.h>

import spectra;
import std;
import vulkan;
import xayah.util.xcli;

namespace {
    void write_console(const std::string_view text, const DWORD stream) noexcept {
        const HANDLE handle = GetStdHandle(stream);
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return;
        DWORD written{};
        WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    }
} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    bool headless_render{};
    try {
        int argument_count{};
        auto argument_deleter = [](wchar_t** arguments) { LocalFree(arguments); };
        std::unique_ptr<wchar_t*, decltype(argument_deleter)> arguments{
            CommandLineToArgvW(GetCommandLineW(), &argument_count),
            argument_deleter,
        };
        if (!arguments) throw std::runtime_error("Windows failed to parse the Spectra command line");

        const std::span<wchar_t* const> argument_span{arguments.get(), static_cast<std::size_t>(argument_count)};
        headless_render = std::ranges::any_of(argument_span.subspan(1), [](const wchar_t* argument) {
            const std::wstring_view value{argument};
            return value == L"--output" || value.starts_with(L"--output=");
        });
        if (headless_render) {
            AttachConsole(ATTACH_PARENT_PROCESS);
            FILE* console_output{};
            FILE* console_error{};
            freopen_s(&console_output, "CONOUT$", "w", stdout);
            freopen_s(&console_error, "CONOUT$", "w", stderr);
        }

        std::optional<std::filesystem::path> scene_path{};
        std::vector<std::filesystem::path> session_scene_roots{};
        std::optional<std::string> initial_renderer{};
        std::uint64_t maximum_frame_count{};
        std::optional<std::filesystem::path> output_path{};
        std::optional<std::filesystem::path> gbuffer_output_path{};
        std::uint32_t samples_per_pixel{};
        std::uint32_t seed{};
        std::uint64_t simulation_step{};
        std::optional<std::string> resolution{};

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
            | xayah::util::option(
                {
                    .long_name    = "output",
                    .value_name   = "FILE.exr",
                    .description  = "render without a window and write a scene-linear EXR",
                    .show_default = false,
                },
                output_path)
            | xayah::util::option(
                {
                    .long_name    = "gbuffer-output",
                    .value_name   = "FILE.exr",
                    .description  = "also write the Renderer GBuffer as a multichannel EXR",
                    .show_default = false,
                },
                gbuffer_output_path)
            | xayah::util::option(
                {
                    .long_name    = "spp",
                    .value_name   = "COUNT",
                    .description  = "override Path Tracer samples per pixel for this render",
                    .show_default = false,
                },
                samples_per_pixel, {.minimum = 1})
            | xayah::util::option(
                {
                    .long_name    = "seed",
                    .value_name   = "VALUE",
                    .description  = "override the Scene sampler seed for this render",
                    .show_default = false,
                },
                seed)
            | xayah::util::option(
                {
                    .long_name    = "resolution",
                    .value_name   = "WIDTHxHEIGHT",
                    .description  = "override the Scene Film resolution for this render",
                    .show_default = false,
                },
                resolution)
            | xayah::util::option(
                {
                    .long_name    = "simulation-step",
                    .value_name   = "STEP",
                    .description  = "render a dynamic Scene at an exact simulation step",
                    .show_default = false,
                },
                simulation_step)
            | xayah::util::validator("renderer",
                [&initial_renderer]() -> std::expected<void, std::string> {
                    if (initial_renderer && *initial_renderer != "rasterizer" && *initial_renderer != "pathtracer") return std::unexpected{"--renderer requires rasterizer or pathtracer"};
                    return {};
                })
            | xayah::util::validator("direct scene",
                [&command, &scene_path, &initial_renderer, &output_path, &gbuffer_output_path, &resolution, &session_scene_roots]() -> std::expected<void, std::string> {
                    if (!scene_path && (initial_renderer || command.option_provided("frames"))) return std::unexpected{"--renderer and --frames require --scene"};
                    if (output_path && (!scene_path || !initial_renderer)) return std::unexpected{"--output requires --scene and --renderer"};
                    if (output_path && command.option_provided("frames")) return std::unexpected{"--output and --frames are mutually exclusive"};
                    if (output_path && !session_scene_roots.empty()) return std::unexpected{"--scene-root is unavailable during a direct render"};
                    if (!output_path && (gbuffer_output_path || command.option_provided("spp") || command.option_provided("seed") || resolution || command.option_provided("simulation-step"))) return std::unexpected{"--gbuffer-output, --spp, --seed, --resolution, and --simulation-step require --output"};
                    if (gbuffer_output_path && *initial_renderer != "pathtracer") return std::unexpected{"--gbuffer-output requires --renderer pathtracer"};
                    if (command.option_provided("spp") && *initial_renderer != "pathtracer") return std::unexpected{"--spp requires --renderer pathtracer"};
                    return {};
                })
            | xayah::util::validator("resolution",
                [&resolution]() -> std::expected<void, std::string> {
                    if (!resolution) return {};
                    const std::size_t separator = resolution->find_first_of("xX");
                    if (separator == std::string::npos || separator == 0 || separator + 1 == resolution->size()) return std::unexpected{"--resolution requires WIDTHxHEIGHT"};
                    std::uint32_t width{};
                    std::uint32_t height{};
                    const std::from_chars_result width_result  = std::from_chars(resolution->data(), resolution->data() + separator, width);
                    const std::from_chars_result height_result = std::from_chars(resolution->data() + separator + 1, resolution->data() + resolution->size(), height);
                    if (width_result.ec != std::errc{} || width_result.ptr != resolution->data() + separator || height_result.ec != std::errc{} || height_result.ptr != resolution->data() + resolution->size() || width == 0 || height == 0) return std::unexpected{"--resolution requires positive WIDTHxHEIGHT values"};
                    return {};
                })
            | xayah::util::example("--scene scenes/cornell-box.spectra --renderer pathtracer --output cornell-box.exr") | xayah::util::example("--scene scenes/cornell-box.spectra --renderer pathtracer --frames 5") | xayah::util::example("--scene-root ../research-scenes");

        const std::expected<xayah::util::ParseResult, std::string> parse_result = command.parse(argument_span);
        if (!parse_result) throw std::runtime_error{parse_result.error()};
        if (parse_result->help_requested) {
            const std::string help = command.help(argument_span, {});
            if (headless_render)
                write_console(help, STD_OUTPUT_HANDLE);
            else
                MessageBoxA(nullptr, help.c_str(), "Spectra", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        const std::expected<void, std::string> validation = command.validate();
        if (!validation) throw std::runtime_error{validation.error()};

        std::optional<std::uint64_t> frame_limit{};
        if (command.option_provided("frames")) frame_limit = maximum_frame_count;

        if (headless_render) {
            std::optional<vk::Extent2D> render_extent{};
            if (resolution) {
                const std::size_t separator = resolution->find_first_of("xX");
                std::uint32_t width{};
                std::uint32_t height{};
                std::from_chars(resolution->data(), resolution->data() + separator, width);
                std::from_chars(resolution->data() + separator + 1, resolution->data() + resolution->size(), height);
                render_extent = vk::Extent2D{width, height};
            }
            spectra::render_scene(
                {
                    .scene_path = *scene_path,
                    .output_path = *output_path,
                    .renderer = *initial_renderer,
                    .resolution = render_extent,
                    .samples_per_pixel = command.option_provided("spp") ? std::optional<std::uint32_t>{samples_per_pixel} : std::nullopt,
                    .seed = command.option_provided("seed") ? std::optional<std::uint32_t>{seed} : std::nullopt,
                    .simulation_step = command.option_provided("simulation-step") ? std::optional<std::uint64_t>{simulation_step} : std::nullopt,
                    .gbuffer_output_path = gbuffer_output_path,
                },
                SPECTRA_SHADER_DIRECTORY);
            write_console(std::format("Written {}\n", output_path->string()), STD_OUTPUT_HANDLE);
            return 0;
        }

        spectra::Spectra application{std::move(scene_path), SPECTRA_SCENE_LIBRARY, std::move(session_scene_roots), SPECTRA_SHADER_DIRECTORY, std::move(initial_renderer)};
        application.run(frame_limit);
    } catch (const std::exception& error) {
        if (headless_render)
            write_console(std::format("Spectra render failed: {}\n", error.what()), STD_ERROR_HANDLE);
        else
            MessageBoxA(nullptr, error.what(), "Spectra", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
