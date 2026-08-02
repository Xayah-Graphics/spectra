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
        std::optional<std::filesystem::path> scene_path{};
        std::vector<std::filesystem::path> scene_roots{};
        bool start_pathtracer{};
        bool renderer_selected{};
        std::optional<std::uint64_t> maximum_frame_count{};
        for (int index = 1; index < argument_count; ++index) {
            const std::wstring_view argument{arguments.get()[index]};
            if (argument == L"--scene") {
                if (++index == argument_count) throw std::runtime_error("--scene requires a .spectra path");
                scene_path = arguments.get()[index];
            } else if (argument == L"--scene-root") {
                if (++index == argument_count) throw std::runtime_error("--scene-root requires a directory path");
                scene_roots.emplace_back(arguments.get()[index]);
            } else if (argument == L"--renderer") {
                if (++index == argument_count) throw std::runtime_error("--renderer requires rasterizer or pathtracer");
                const std::wstring_view renderer{arguments.get()[index]};
                renderer_selected = true;
                if (renderer == L"pathtracer")
                    start_pathtracer = true;
                else if (renderer != L"rasterizer")
                    throw std::runtime_error("--renderer requires rasterizer or pathtracer");
            } else if (argument == L"--frames") {
                if (++index == argument_count) throw std::runtime_error("--frames requires a frame count");
                maximum_frame_count = std::stoull(arguments.get()[index]);
            } else {
                throw std::runtime_error("Unknown Spectra command-line argument");
            }
        }

        if (!scene_path && (renderer_selected || maximum_frame_count)) throw std::runtime_error("--renderer and --frames require --scene");

        spectra::run_application(std::move(scene_path), SPECTRA_SCENE_LIBRARY, std::move(scene_roots), SPECTRA_SHADER_DIRECTORY, maximum_frame_count, start_pathtracer);
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Spectra", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
