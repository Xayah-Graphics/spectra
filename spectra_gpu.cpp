#include <charconv>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <exception>
#include <optional>
#include <spectra/pathtracer/core/kernel_config.cuh>
#include <spectra/pathtracer/core/render_config.cuh>
#include <spectra/pathtracer/gpu/memory.cuh>
#include <spectra/pathtracer/integrator.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef SPECTRA_IS_WINDOWS
// clang-format off
#include <Windows.h>
#include <shellapi.h>
// clang-format on
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/string.cuh>
#endif

import spectra.scene;

namespace {
    class UsageError : public std::runtime_error {
    public:
        explicit UsageError(std::string message) : std::runtime_error(std::move(message)) {}
    };

    void print_usage(std::string_view message) {
        if (!message.empty()) std::fprintf(stderr, "spectra_gpu: %.*s\n\n", static_cast<int>(message.size()), message.data());
        std::fprintf(stderr, "usage: spectra_gpu <scene-name> [--outfile <file.exr>] [--spp <n>] [--seed <n>] [--gpu-device <index>] [--quiet]\n");
    }

    [[nodiscard]] const std::string& require_value(const std::vector<std::string>& arguments, std::size_t& index, std::string_view option) {
        ++index;
        if (index >= arguments.size()) throw UsageError(std::string(option) + " requires a value");
        return arguments[index];
    }

    [[nodiscard]] int parse_int(std::string_view text, std::string_view option) {
        int value                           = 0;
        const char* begin                   = text.data();
        const char* end                     = begin + text.size();
        const std::from_chars_result result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end) throw UsageError(std::string(option) + " requires an integer value");
        return value;
    }

    [[nodiscard]] std::vector<std::string> command_line_arguments(int argc, char* argv[]) {
#ifdef SPECTRA_IS_WINDOWS
        static_cast<void>(argc);
        static_cast<void>(argv);
        int windows_argc = 0;
        LPWSTR* argvw    = CommandLineToArgvW(GetCommandLineW(), &windows_argc);
        CHECK(argvw != nullptr);
        std::vector<std::string> arguments;
        if (windows_argc > 1) arguments.reserve(static_cast<std::size_t>(windows_argc - 1));
        for (int index = 1; index < windows_argc; ++index) arguments.push_back(spectra::UTF8FromWString(argvw[index]));
        CHECK(LocalFree(argvw) == nullptr);
#else
        std::vector<std::string> arguments;
        if (argc > 1) arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
#endif
        return arguments;
    }
} // namespace

int main(int argc, char* argv[]) {
    try {
        const std::vector<std::string> arguments = command_line_arguments(argc, argv);
        if (arguments.empty()) throw UsageError("missing scene name");

        spectra::pathtracer::RuntimeConfig runtime_config{};
        spectra::pathtracer::RenderConfig render_config{};
        render_config.rendering_space = spectra::pathtracer::RenderingSpace::CameraWorld;

        std::optional<std::string> scene_name;

        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const std::string& argument = arguments[index];
            if (argument == "--outfile") {
                render_config.output_file = require_value(arguments, index, argument);
            } else if (argument == "--spp") {
                render_config.pixel_samples = parse_int(require_value(arguments, index, argument), argument);
            } else if (argument == "--seed") {
                render_config.seed = parse_int(require_value(arguments, index, argument), argument);
            } else if (argument == "--gpu-device") {
                runtime_config.cuda_device = parse_int(require_value(arguments, index, argument), argument);
            } else if (argument == "--quiet") {
                render_config.quiet = true;
            } else if (argument == "--help" || argument == "-h") {
                print_usage({});
                return 0;
            } else if (!argument.empty() && argument[0] == '-') {
                throw UsageError("unknown argument \"" + argument + "\"");
            } else {
                if (scene_name.has_value()) throw UsageError("spectra_gpu accepts exactly one scene name");
                scene_name = argument;
            }
        }

        if (!scene_name.has_value()) throw UsageError("missing scene name");
        if (render_config.pixel_samples.has_value() && *render_config.pixel_samples <= 0) throw UsageError("--spp must be positive");
        if (runtime_config.cuda_device.has_value() && *runtime_config.cuda_device < 0) throw UsageError("--gpu-device must be non-negative");

        spectra::pathtracer::GpuRuntime runtime(runtime_config);
        runtime.UploadKernelConfig(spectra::pathtracer::KernelConfigFrom(render_config));

        spectra::scene::Scene scene = spectra::scene::BuildScene(*scene_name);

        spectra::pathtracer::WavefrontPathtracer pathtracer(&spectra::CUDATrackedMemoryResource::singleton, scene, render_config);

        spectra::Float seconds = pathtracer.Render();

        spectra::ImageMetadata metadata;
        pathtracer.camera.InitMetadata(&metadata);
        metadata.renderTimeSeconds = seconds;
        metadata.samplesPerPixel   = pathtracer.sampler.SamplesPerPixel();
        pathtracer.film.WriteImage(metadata);
        return 0;
    } catch (const UsageError& error) {
        print_usage(error.what());
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
