#include <charconv>
#include <cstdio>
#include <cuda_runtime_api.h>
#include <exception>
#include <memory>
#include <optional>
#include <pathtracer/compiled_scene.cuh>
#include <pathtracer/core/kernel_config.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/integrator.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/float.cuh>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

import spectra.pathtracer;
import spectra.scene;
import spectra.scene.pbrt;

namespace spectra::app {
    [[nodiscard]] pathtracer::ColorSpace ToPathtracerColorSpace(const scene::ColorSpace colorSpace) {
        switch (colorSpace) {
        case scene::ColorSpace::sRGB: return pathtracer::ColorSpace::sRGB;
        case scene::ColorSpace::DCI_P3: return pathtracer::ColorSpace::DCI_P3;
        case scene::ColorSpace::Rec2020: return pathtracer::ColorSpace::Rec2020;
        case scene::ColorSpace::ACES2065_1: return pathtracer::ColorSpace::ACES2065_1;
        }
        throw std::runtime_error("Unknown scene color space while adapting scene to pathtracer");
    }

    [[nodiscard]] pathtracer::SceneSourceLocation ToPathtracerSourceLocation(const scene::SceneSourceLocation& source) {
        return pathtracer::SceneSourceLocation{
            .filename = source.filename,
            .line     = source.line,
            .column   = source.column,
        };
    }

    [[nodiscard]] pathtracer::SceneRevision ToPathtracerRevision(const scene::SceneRevision revision) {
        return pathtracer::SceneRevision{.value = revision.value};
    }

    [[nodiscard]] pathtracer::SceneTransform ToPathtracerTransform(const math::Transform& transform) {
        return pathtracer::SceneTransform{
            .matrix  = transform.matrix,
            .inverse = transform.inverse,
        };
    }

    [[nodiscard]] pathtracer::SceneTransformSet ToPathtracerTransformSet(const scene::SceneTransformSet& transform) {
        return pathtracer::SceneTransformSet{
            .start     = ToPathtracerTransform(transform.start),
            .end       = ToPathtracerTransform(transform.end),
            .startTime = transform.startTime,
            .endTime   = transform.endTime,
            .animated  = transform.animated,
        };
    }

    [[nodiscard]] pathtracer::SceneParameter ToPathtracerParameter(const scene::SceneParameter& parameter) {
        return pathtracer::SceneParameter{
            .type        = parameter.type,
            .name        = parameter.name,
            .values      = parameter.values,
            .mayBeUnused = parameter.mayBeUnused,
            .colorSpace  = ToPathtracerColorSpace(parameter.colorSpace),
            .source      = ToPathtracerSourceLocation(parameter.source),
        };
    }

    [[nodiscard]] std::vector<pathtracer::SceneParameter> ToPathtracerParameters(const std::vector<scene::SceneParameter>& parameters) {
        std::vector<pathtracer::SceneParameter> result;
        result.reserve(parameters.size());
        for (const scene::SceneParameter& parameter : parameters) result.push_back(ToPathtracerParameter(parameter));
        return result;
    }

    [[nodiscard]] pathtracer::SceneEntity ToPathtracerEntity(const scene::SceneEntity& entity) {
        return pathtracer::SceneEntity{
            .type       = entity.type,
            .parameters = ToPathtracerParameters(entity.parameters),
            .colorSpace = ToPathtracerColorSpace(entity.colorSpace),
            .source     = ToPathtracerSourceLocation(entity.source),
        };
    }

    [[nodiscard]] pathtracer::SceneOption ToPathtracerOption(const scene::SceneOption& option) {
        return pathtracer::SceneOption{
            .name   = option.name,
            .value  = option.value,
            .source = ToPathtracerSourceLocation(option.source),
        };
    }

    [[nodiscard]] pathtracer::SceneMediumInterface ToPathtracerMediumInterface(const scene::SceneMediumInterface& mediumInterface) {
        return pathtracer::SceneMediumInterface{
            .inside  = mediumInterface.inside,
            .outside = mediumInterface.outside,
        };
    }

    [[nodiscard]] pathtracer::SceneRenderSettings ToPathtracerRenderSettings(const scene::SceneRenderSettings& settings) {
        std::vector<pathtracer::SceneOption> options;
        options.reserve(settings.options.size());
        for (const scene::SceneOption& option : settings.options) options.push_back(ToPathtracerOption(option));

        return pathtracer::SceneRenderSettings{
            .filter          = ToPathtracerEntity(settings.filter),
            .film            = ToPathtracerEntity(settings.film),
            .camera          = ToPathtracerEntity(settings.camera),
            .sampler         = ToPathtracerEntity(settings.sampler),
            .integrator      = ToPathtracerEntity(settings.integrator),
            .accelerator     = ToPathtracerEntity(settings.accelerator),
            .cameraTransform = ToPathtracerTransformSet(settings.cameraTransform),
            .cameraMedium    = settings.cameraMedium,
            .options         = std::move(options),
        };
    }

    [[nodiscard]] pathtracer::SceneMaterial ToPathtracerMaterial(const scene::SceneMaterial& material) {
        return pathtracer::SceneMaterial{
            .name   = material.name,
            .entity = ToPathtracerEntity(material.entity),
        };
    }

    [[nodiscard]] pathtracer::SceneTexture ToPathtracerTexture(const scene::SceneTexture& texture) {
        return pathtracer::SceneTexture{
            .name      = texture.name,
            .kind      = texture.kind,
            .entity    = ToPathtracerEntity(texture.entity),
            .transform = ToPathtracerTransformSet(texture.transform),
        };
    }

    [[nodiscard]] pathtracer::SceneMedium ToPathtracerMedium(const scene::SceneMedium& medium) {
        return pathtracer::SceneMedium{
            .name      = medium.name,
            .entity    = ToPathtracerEntity(medium.entity),
            .transform = ToPathtracerTransformSet(medium.transform),
        };
    }

    [[nodiscard]] pathtracer::SceneLight ToPathtracerLight(const scene::SceneLight& light) {
        return pathtracer::SceneLight{
            .name      = light.name,
            .entity    = ToPathtracerEntity(light.entity),
            .transform = ToPathtracerTransformSet(light.transform),
            .medium    = light.medium,
        };
    }

    [[nodiscard]] std::optional<pathtracer::SceneAreaLight> ToPathtracerAreaLight(const std::optional<scene::SceneAreaLight>& areaLight) {
        if (!areaLight.has_value()) return std::nullopt;
        return pathtracer::SceneAreaLight{.entity = ToPathtracerEntity(areaLight->entity)};
    }

    [[nodiscard]] pathtracer::SceneShape ToPathtracerShape(const scene::SceneShape& shape) {
        return pathtracer::SceneShape{
            .name               = shape.name,
            .entity             = ToPathtracerEntity(shape.entity),
            .transform          = ToPathtracerTransformSet(shape.transform),
            .reverseOrientation = shape.reverseOrientation,
            .materialName       = shape.materialName,
            .areaLight          = ToPathtracerAreaLight(shape.areaLight),
            .mediumInterface    = ToPathtracerMediumInterface(shape.mediumInterface),
        };
    }

    [[nodiscard]] pathtracer::SceneObjectDefinition ToPathtracerObjectDefinition(const scene::SceneObjectDefinition& definition) {
        std::vector<pathtracer::SceneShape> shapes;
        shapes.reserve(definition.shapes.size());
        for (const scene::SceneShape& shape : definition.shapes) shapes.push_back(ToPathtracerShape(shape));

        return pathtracer::SceneObjectDefinition{
            .name   = definition.name,
            .shapes = std::move(shapes),
            .source = ToPathtracerSourceLocation(definition.source),
        };
    }

    [[nodiscard]] pathtracer::SceneObjectInstance ToPathtracerObjectInstance(const scene::SceneObjectInstance& instance) {
        return pathtracer::SceneObjectInstance{
            .name           = instance.name,
            .definitionName = instance.definitionName,
            .transform      = ToPathtracerTransformSet(instance.transform),
            .source         = ToPathtracerSourceLocation(instance.source),
        };
    }

    [[nodiscard]] pathtracer::SceneSnapshot ToPathtracerScene(const scene::SceneSnapshot& source) {
        pathtracer::SceneSnapshot result{
            .revision       = ToPathtracerRevision(source.revision),
            .name           = source.name,
            .title          = source.title,
            .source         = source.source,
            .renderSettings = ToPathtracerRenderSettings(source.renderSettings),
        };

        result.materials.reserve(source.materials.size());
        for (const scene::SceneMaterial& material : source.materials) result.materials.push_back(ToPathtracerMaterial(material));

        result.textures.reserve(source.textures.size());
        for (const scene::SceneTexture& texture : source.textures) result.textures.push_back(ToPathtracerTexture(texture));

        result.media.reserve(source.media.size());
        for (const scene::SceneMedium& medium : source.media) result.media.push_back(ToPathtracerMedium(medium));

        result.lights.reserve(source.lights.size());
        for (const scene::SceneLight& light : source.lights) result.lights.push_back(ToPathtracerLight(light));

        result.shapes.reserve(source.shapes.size());
        for (const scene::SceneShape& shape : source.shapes) result.shapes.push_back(ToPathtracerShape(shape));

        result.objectDefinitions.reserve(source.objectDefinitions.size());
        for (const scene::SceneObjectDefinition& definition : source.objectDefinitions) result.objectDefinitions.push_back(ToPathtracerObjectDefinition(definition));

        result.objectInstances.reserve(source.objectInstances.size());
        for (const scene::SceneObjectInstance& instance : source.objectInstances) result.objectInstances.push_back(ToPathtracerObjectInstance(instance));

        return result;
    }
} // namespace spectra::app

namespace {
    class UsageError : public std::runtime_error {
    public:
        explicit UsageError(std::string message) : std::runtime_error(std::move(message)) {}
    };

    void print_usage(std::string_view message) {
        if (!message.empty()) std::fprintf(stderr, "spectra_cli: %.*s\n\n", static_cast<int>(message.size()), message.data());
        std::fprintf(stderr, "usage: spectra_cli <scene-name> [--outfile <file.exr>] [--spp <n>] [--seed <n>] [--gpu-device <index>] [--quiet]\n");
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
        std::vector<std::string> arguments;
        if (argc > 1) arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
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
                if (scene_name.has_value()) throw UsageError("spectra_cli accepts exactly one scene name");
                scene_name = argument;
            }
        }

        if (!scene_name.has_value()) throw UsageError("missing scene name");
        if (render_config.pixel_samples.has_value() && *render_config.pixel_samples <= 0) throw UsageError("--spp must be positive");
        if (runtime_config.cuda_device.has_value() && *runtime_config.cuda_device < 0) throw UsageError("--gpu-device must be non-negative");

        spectra::pathtracer::GpuRuntime runtime(runtime_config);
        runtime.UploadKernelConfig(spectra::pathtracer::KernelConfigFrom(render_config));

        spectra::scene::SceneWorkspace scene_workspace                      = spectra::scene::BuildPbrtScene(*scene_name);
        std::shared_ptr<const spectra::scene::SceneSnapshot> scene_snapshot = scene_workspace.snapshot();
        spectra::pathtracer::SceneSnapshot pathtracer_scene                = spectra::app::ToPathtracerScene(*scene_snapshot);
        spectra::pathtracer::PathtracerMemoryScope scene_memory_scope(spectra::pathtracer::PathtracerMemoryScopeKind::Scene, "spectra_cli scene");
        std::unique_ptr<spectra::pathtracer::CompiledPathtracerScene> compiled_scene = spectra::pathtracer::CompilePathtracerScene(pathtracer_scene, render_config, &scene_memory_scope);
        spectra::pathtracer::WavefrontPathtracer pathtracer(&scene_memory_scope, *compiled_scene, render_config);

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
