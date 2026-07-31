#include <Windows.h>
#include <shellapi.h>

import spectra.application;
import std;

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        int argument_count{};
        auto argument_deleter = [](wchar_t** arguments) { LocalFree(arguments); };
        std::unique_ptr<wchar_t*, decltype(argument_deleter)> arguments{
            CommandLineToArgvW(GetCommandLineW(), &argument_count),
            argument_deleter,
        };
        if (!arguments) throw std::runtime_error("Windows failed to parse the Spectra command line");
        std::filesystem::path scene_path{SPECTRA_DEFAULT_SCENE};
        std::optional<std::filesystem::path> plugin_path{};
        bool scene_path_explicit{};
        bool start_pathtracer{};
        std::optional<std::uint64_t> maximum_frame_count{};
        for (int index = 1; index < argument_count; ++index) {
            const std::wstring_view argument{arguments.get()[index]};
            if (argument == L"--scene") {
                if (++index == argument_count) throw std::runtime_error("--scene requires a .spectra path");
                scene_path = arguments.get()[index];
                scene_path_explicit = true;
            } else if (argument == L"--plugin") {
                if (++index == argument_count) throw std::runtime_error("--plugin requires a Plugin API 5 library path");
                plugin_path = arguments.get()[index];
            } else if (argument == L"--renderer") {
                if (++index == argument_count) throw std::runtime_error("--renderer requires rasterizer or pathtracer");
                const std::wstring_view renderer{arguments.get()[index]};
                if (renderer == L"pathtracer") start_pathtracer = true;
                else if (renderer != L"rasterizer") throw std::runtime_error("--renderer requires rasterizer or pathtracer");
            } else if (argument == L"--frames") {
                if (++index == argument_count) throw std::runtime_error("--frames requires a frame count");
                maximum_frame_count = std::stoull(arguments.get()[index]);
            } else {
                throw std::runtime_error("Unknown Spectra command-line argument");
            }
        }

        if (scene_path_explicit && plugin_path.has_value()) throw std::runtime_error("--scene and --plugin select mutually exclusive Scene providers");
        spectra::run_application(
            scene_path,
            plugin_path,
            SPECTRA_SHADER_DIRECTORY,
            maximum_frame_count,
            start_pathtracer);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Spectra", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
