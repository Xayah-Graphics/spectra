#include <Windows.h>

#include <shellapi.h>

import spectra.application;
import spectra.render;
import spectra.scene;
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
        std::optional<spectra::SequenceExport> sequence_export{};
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
            } else if (argument == L"--export-sequence") {
                if (++index == argument_count) throw std::runtime_error("--export-sequence requires an output directory");
                sequence_export.emplace();
                sequence_export->directory = arguments.get()[index];
            } else if (argument == L"--export-range") {
                if (!sequence_export) throw std::runtime_error("--export-range requires --export-sequence first");
                if (++index == argument_count) throw std::runtime_error("--export-range requires first and last frame");
                sequence_export->first_frame = std::stoull(arguments.get()[index]);
                if (++index == argument_count) throw std::runtime_error("--export-range requires first and last frame");
                sequence_export->last_frame = std::stoull(arguments.get()[index]);
                if (sequence_export->last_frame < sequence_export->first_frame) throw std::runtime_error("--export-range last frame must not precede first frame");
            } else if (argument == L"--export-format") {
                if (!sequence_export) throw std::runtime_error("--export-format requires --export-sequence first");
                if (++index == argument_count) throw std::runtime_error("--export-format requires png or exr");
                const std::wstring_view format{arguments.get()[index]};
                if (format == L"png")
                    sequence_export->format = spectra::render::ImageFileFormat::Png;
                else if (format == L"exr")
                    sequence_export->format = spectra::render::ImageFileFormat::Exr;
                else
                    throw std::runtime_error("--export-format requires png or exr");
            } else {
                throw std::runtime_error("Unknown Spectra command-line argument");
            }
        }

        if (!scene_path && (renderer_selected || maximum_frame_count || sequence_export)) throw std::runtime_error("--renderer, --frames, and animation export require --scene");

        spectra::run_application(std::move(scene_path), SPECTRA_SCENE_LIBRARY, std::move(scene_roots), SPECTRA_SHADER_DIRECTORY, maximum_frame_count, start_pathtracer, std::move(sequence_export));
    } catch (const std::exception& error) {
        MessageBoxA(nullptr, error.what(), "Spectra", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}
