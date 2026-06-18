#include <pathtracer/compiled_scene.cuh>
#include <pathtracer/core/kernel_config.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/integrator.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/float.cuh>

import std;
import spectra.pathtracer.renderer;
import spectra.scene;
import xayah.util.xcli;

int main(const int argc, const char* const* const argv) {
    try {
        const std::span<const char* const> arguments{argv, static_cast<std::size_t>(argc)};
        std::string scene_name{};
        std::string output_file{};
        std::int32_t pixel_samples{};
        std::int32_t seed{};
        std::int32_t gpu_device{};
        bool quiet{};
        xayah::util::Command command =
            xayah::util::Command{"Render a Spectra PBRT scene with the CUDA/OptiX pathtracer."}
            | xayah::util::positional({.name = "scene-name", .description = "Spectra scene id or scene-root-relative PBRT path", .show_default = false, .required = true}, scene_name)
            | xayah::util::option({.long_name = "outfile", .value_name = "file.exr", .description = "override the scene output filename", .show_default = false}, output_file)
            | xayah::util::option({.long_name = "spp", .value_name = "n", .description = "override sampler pixel samples", .default_text = "scene"}, pixel_samples, {.minimum = 1.0})
            | xayah::util::option({.long_name = "seed", .value_name = "n", .description = "sampler random seed"}, seed)
            | xayah::util::option({.long_name = "gpu-device", .value_name = "index", .description = "CUDA device index", .default_text = "runtime default"}, gpu_device, {.minimum = 0.0})
            | xayah::util::option({.long_name = "quiet", .description = "suppress render progress output", .show_default = false}, quiet)
            | xayah::util::example("default --spp 64 --outfile output.exr")
            | xayah::util::example("pbrt-book/book.pbrt --quiet --seed 1 --spp 1");
        const std::string usage = command.help(arguments);

        const auto cli_result = command.parse(arguments);
        if (!cli_result) {
            std::cerr << "error: " << cli_result.error() << '\n' << usage << '\n';
            return 2;
        }
        if (cli_result->help_requested) {
            std::println("{}", usage);
            return 0;
        }

        const auto cli_validation = command.validate();
        if (!cli_validation) {
            std::cerr << "error: " << cli_validation.error() << '\n';
            return 2;
        }

        spectra::pathtracer::RuntimeConfig runtime_config{};
        spectra::pathtracer::RenderConfig render_config{};
        render_config.rendering_space = spectra::pathtracer::RenderingSpace::CameraWorld;
        render_config.output_file = std::move(output_file);
        render_config.seed = static_cast<int>(seed);
        render_config.quiet = quiet;
        if (command.option_provided("spp")) render_config.pixel_samples = static_cast<int>(pixel_samples);
        if (command.option_provided("gpu-device")) runtime_config.cuda_device = static_cast<int>(gpu_device);

        spectra::pathtracer::GpuRuntime runtime(runtime_config);
        runtime.UploadKernelConfig(spectra::pathtracer::KernelConfigFrom(render_config));

        spectra::scene::Scene scene = spectra::scene::Scene::parse_pbrt(scene_name);
        const spectra::scene::Scene::ResolvedScene scene_snapshot = scene.resolved_scene();
        spectra::pathtracer::PathtracerMemoryScope scene_memory_scope(spectra::pathtracer::PathtracerMemoryScopeKind::Scene, "spectra_pathtracer_cli scene");
        std::unique_ptr<spectra::pathtracer::CompiledScene> compiled_scene = spectra::pathtracer::CompileScene(scene_snapshot, render_config, &scene_memory_scope);
        spectra::pathtracer::WavefrontIntegrator integrator(&scene_memory_scope, *compiled_scene, render_config);

        spectra::Float seconds = integrator.Render();

        spectra::ImageMetadata metadata;
        integrator.camera.InitMetadata(&metadata);
        metadata.renderTimeSeconds = seconds;
        metadata.samplesPerPixel   = integrator.sampler.SamplesPerPixel();
        integrator.film.WriteImage(metadata);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
