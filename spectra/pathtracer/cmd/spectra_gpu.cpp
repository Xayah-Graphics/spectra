#include <spectra/pathtracer/util/float.h>

#include <spectra/pathtracer/core/options.h>
#include <spectra/pathtracer/gpu/memory.h>
#include <spectra/pathtracer/util/args.h>
#include <spectra/pathtracer/util/error.h>
#include <spectra/pathtracer/integrator.h>
#include <spectra/scene.h>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace
{
    void usage(const std::string& message)
    {
        if (!message.empty()) std::fprintf(stderr, "spectra_gpu: %s\n\n", message.c_str());
        std::fprintf(stderr, "usage: spectra_gpu <scene.pbrt> [--outfile <file.exr>] [--spp <n>] [--seed <n>] [--gpu-device <index>] [--quiet]\n");
    }

    [[nodiscard]] std::string require_value(const std::vector<std::string>& arguments, std::size_t& index, const std::string& option)
    {
        ++index;
        if (index >= arguments.size())
        {
            usage(option + " requires a value");
            std::exit(1);
        }
        return arguments[index];
    }

    [[nodiscard]] int parse_int(const std::string& text, const std::string& option)
    {
        std::size_t consumed = 0;
        int value = 0;
        try
        {
            value = std::stoi(text, &consumed);
        }
        catch (const std::exception&)
        {
            usage(option + " requires an integer value");
            std::exit(1);
        }
        if (consumed != text.size())
        {
            usage(option + " requires an integer value");
            std::exit(1);
        }
        return value;
    }
}

int main(int argc, char* argv[])
{
    static_cast<void>(argc);

    std::vector<std::string> arguments = spectra::GetCommandLineArguments(argv);
    if (arguments.empty())
    {
        usage("missing scene filename");
        return 1;
    }

    spectra::SpectraOptions options;
    options.renderingSpace = spectra::RenderingCoordinateSystem::CameraWorld;

    std::vector<std::string> filenames;

    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const std::string& argument = arguments[index];
        if (argument == "--outfile")
        {
            options.imageFile = require_value(arguments, index, argument);
        }
        else if (argument == "--spp")
        {
            options.pixelSamples = parse_int(require_value(arguments, index, argument), argument);
        }
        else if (argument == "--seed")
        {
            options.seed = parse_int(require_value(arguments, index, argument), argument);
        }
        else if (argument == "--gpu-device")
        {
            options.gpuDevice = parse_int(require_value(arguments, index, argument), argument);
        }
        else if (argument == "--quiet")
        {
            options.quiet = true;
        }
        else if (argument == "--help" || argument == "-h")
        {
            usage("");
            return 0;
        }
        else if (!argument.empty() && argument[0] == '-')
        {
            usage("unknown argument \"" + argument + "\"");
            return 1;
        }
        else
        {
            filenames.push_back(argument);
        }
    }

    if (filenames.empty())
    {
        usage("missing scene filename");
        return 1;
    }
    if (filenames.size() != 1)
    {
        usage("spectra_gpu accepts exactly one scene filename");
        return 1;
    }
    if (options.pixelSamples && *options.pixelSamples <= 0)
    {
        usage("--spp must be positive");
        return 1;
    }
    if (options.gpuDevice && *options.gpuDevice < 0)
    {
        usage("--gpu-device must be non-negative");
        return 1;
    }

    spectra::pathtracer::GpuRuntime runtime(options);

    spectra::scene::Scene scene;
    spectra::scene::SceneBuilder builder(&scene);
    spectra::scene::ParseFiles(&builder, filenames);

    std::unique_ptr<spectra::pathtracer::WavefrontPathtracer> pathtracer = std::make_unique<spectra::pathtracer::WavefrontPathtracer>(&spectra::CUDATrackedMemoryResource::singleton, scene);
    struct AggregateGuard
    {
        spectra::pathtracer::WavefrontPathtracer* pathtracer;

        ~AggregateGuard() noexcept
        {
            if (pathtracer != nullptr) pathtracer->ReleaseAggregate();
        }
    };
    AggregateGuard aggregate_guard{pathtracer.get()};

    spectra::Float seconds = pathtracer->Render();

    spectra::ImageMetadata metadata;
    pathtracer->camera.InitMetadata(&metadata);
    metadata.renderTimeSeconds = seconds;
    metadata.samplesPerPixel = pathtracer->sampler.SamplesPerPixel();
    pathtracer->film.WriteImage(metadata);
    return 0;
}
