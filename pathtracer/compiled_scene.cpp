module;

#include <cstddef>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <array>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <pathtracer/base/material.cuh>
#include <pathtracer/base/shape.cuh>
#include <pathtracer/compiled_scene.cuh>
#include <pathtracer/core/cameras.cuh>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/core/film.cuh>
#include <pathtracer/core/filters.cuh>
#include <pathtracer/core/lights.cuh>
#include <pathtracer/core/materials.cuh>
#include <pathtracer/core/media.cuh>
#include <pathtracer/core/paramdict.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/core/samplers.cuh>
#include <pathtracer/core/textures.cuh>
#include <pathtracer/device_scene.cuh>
#include <pathtracer/gpu/volume.cuh>
#include <pathtracer/util/color.cuh>
#include <pathtracer/util/colorspace.cuh>
#include <pathtracer/util/file.h>
#include <pathtracer/util/image.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/mesh.cuh>
#include <pathtracer/util/parallel.cuh>
#include <pathtracer/util/spectrum.cuh>
#include <pathtracer/util/transform.cuh>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

module spectra.pathtracer.renderer;

import spectra.scene;
import std;

namespace spectra::pathtracer {
    namespace {
        [[nodiscard]] SquareMatrix<4> ToMatrix(const std::array<float, 16>& matrix) {
            return SquareMatrix<4>{
                matrix[0],
                matrix[1],
                matrix[2],
                matrix[3],
                matrix[4],
                matrix[5],
                matrix[6],
                matrix[7],
                matrix[8],
                matrix[9],
                matrix[10],
                matrix[11],
                matrix[12],
                matrix[13],
                matrix[14],
                matrix[15],
            };
        }

        [[nodiscard]] Transform ToTransform(const scene::SceneTransform& transform) {
            return Transform(ToMatrix(transform.matrix), ToMatrix(transform.inverse));
        }

        [[nodiscard]] const RGBColorSpace* ToColorSpace(scene::Scene::ColorSpace color_space) {
            switch (color_space) {
            case scene::Scene::ColorSpace::sRGB: return RGBColorSpace::SRGB();
            case scene::Scene::ColorSpace::DCI_P3: return RGBColorSpace::DCI_P3();
            case scene::Scene::ColorSpace::Rec2020: return RGBColorSpace::Rec2020();
            case scene::Scene::ColorSpace::ACES2065_1: return RGBColorSpace::ACES2065_1();
            }
            throw std::runtime_error("Unknown Spectra scene color space.");
        }

        [[nodiscard]] FileLoc ToFileLoc(const scene::Scene::SourceLocation& source, const std::string& default_value) {
            FileLoc location(source.filename.empty() ? std::string_view(default_value) : std::string_view(source.filename));
            location.line   = source.line;
            location.column = source.column;
            return location;
        }

        [[nodiscard]] std::string SourceString(const scene::Scene::SourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::string FormatGpuSupportReport(const SceneSupportReport& report) {
            std::string message = "Scene is not supported by the current GPU pathtracer:";
            for (const scene::Scene::Diagnostic& diagnostic : report.diagnostics) message += std::format("\n  {}: {}", SourceString(diagnostic.source), diagnostic.message);
            return message;
        }

        [[nodiscard]] bool ContainsName(const std::set<std::string>& values, const std::string& value) {
            return values.find(value) != values.end();
        }

        [[nodiscard]] std::string OneStringParameter(const std::vector<scene::Scene::Parameter>& parameters, const std::string& name, std::string default_value) {
            for (const scene::Scene::Parameter& parameter : parameters) {
                if (parameter.name != name) continue;
                if (const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values); values != nullptr && !values->empty()) return values->front();
            }
            return default_value;
        }

        void AddDiagnostic(SceneSupportReport* report, scene::Scene::SourceLocation source, std::string message) {
            report->supported = false;
            report->diagnostics.push_back(scene::Scene::Diagnostic{
                .source  = std::move(source),
                .message = std::move(message),
            });
        }

        void ValidateTransform(SceneSupportReport* report, const scene::SceneTransformSet& transform, const scene::Scene::SourceLocation& source, const std::string_view owner) {
            if (transform.animated) AddDiagnostic(report, source, std::format("{} uses animated transforms, which are represented by the scene document but are not supported by the current GPU pathtracer", owner));
        }

        void ValidateEntityType(SceneSupportReport* report, const scene::Scene::Entity& entity, const std::set<std::string>& supported, const std::string_view kind) {
            if (!ContainsName(supported, entity.type)) AddDiagnostic(report, entity.source, std::format("GPU pathtracer does not support {} type \"{}\"", kind, entity.type));
        }

        void AppendParameterValues(ParsedParameter& parsedParameter, const scene::Scene::Parameter& parameter) {
            if (const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values)) {
                for (float value : *values) parsedParameter.AddFloat(value);
                return;
            }
            if (const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values)) {
                for (int value : *values) parsedParameter.AddInt(value);
                return;
            }
            if (const std::vector<std::uint8_t>* values = std::get_if<std::vector<std::uint8_t>>(&parameter.values)) {
                for (std::uint8_t value : *values) parsedParameter.AddBool(value != 0);
                return;
            }
            const std::vector<std::string>& values = std::get<std::vector<std::string>>(parameter.values);
            for (const std::string& value : values) parsedParameter.AddString(value);
        }

        [[nodiscard]] bool IsImageTexture(const std::string& type) {
            return type == "imagemap" || type == "ptex";
        }

        class SceneCompiler {
        public:
            SceneCompiler(const scene::Scene::ResolvedScene& sourceScene, CompiledScene& compiledScene, const RenderConfig& config, std::optional<Point2i> resolutionOverride, cudaStream_t renderStream) : source(sourceScene), compiled(compiledScene), renderConfig(config), filmResolutionOverride(resolutionOverride), renderStream(renderStream) {}

            void Compile() {
                const SceneSupportReport supportReport = AnalyzeSceneSupport(this->source);
                if (!supportReport.supported) throw std::runtime_error(FormatGpuSupportReport(supportReport));

                this->RegisterResourceNames();
                this->SetRenderSettings(this->source.render_settings);
                for (const scene::Scene::Material& material : this->source.materials) this->AddMaterial(material);
                for (const scene::Scene::Texture& texture : this->source.textures) this->AddTexture(texture);
                for (const scene::Scene::Medium& medium : this->source.media) this->AddMedium(medium);

                this->compiled.media   = this->CreateMedia();
                for (const std::pair<const std::string, Medium>& medium : this->compiled.media) this->compiled.haveEmissiveMedia |= medium.second.IsEmissive();
                this->CreateDeviceVolumes();
                this->compiled.sampler = this->CreateSampler();
                this->compiled.camera  = this->CreateCamera();

                for (const scene::Scene::Light& light : this->source.lights) this->AddLight(light);
                for (const scene::Scene::Shape& shape : this->source.shapes) this->AddShape(shape);
                for (const scene::Scene::ObjectDefinition& definition : this->source.object_definitions) this->AddObjectDefinition(definition);
                for (const scene::Scene::ObjectInstance& instance : this->source.object_instances) this->AddObjectInstance(instance);

                this->compiled.textures = this->CreateTextures();
                this->CreateMaterials();

                Allocator alloc               = this->compiled.threadAllocators.Get();
                this->compiled.infiniteLights = alloc.new_object<pstd::vector<Light>>(alloc);
                for (Light light : this->CreateLights()) {
                    if (light.Is<UniformInfiniteLight>() || light.Is<ImageInfiniteLight>() || light.Is<PortalImageInfiniteLight>()) this->compiled.infiniteLights->push_back(light);
                    this->compiled.allLights.push_back(light);
                }
            }

        private:
            [[nodiscard]] ParameterDictionary MakeParameterDictionary(const std::vector<scene::Scene::Parameter>& parameters, scene::Scene::ColorSpace color_space) const {
                InlinedVector<std::shared_ptr<ParsedParameter>, 8> parsedParameters{};
                for (const scene::Scene::Parameter& parameter : parameters) {
                    if (parameter.type.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty type.", this->source.source));
                    if (parameter.name.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty name.", this->source.source));
                    auto parsedParameter         = std::make_shared<ParsedParameter>(ToFileLoc(parameter.source, this->source.source));
                    parsedParameter->type        = parameter.type;
                    parsedParameter->name        = parameter.name;
                    parsedParameter->mayBeUnused = parameter.may_be_unused;
                    parsedParameter->colorSpace  = ToColorSpace(parameter.color_space);
                    AppendParameterValues(*parsedParameter, parameter);
                    parsedParameters.push_back(std::move(parsedParameter));
                }

                return ParameterDictionary(std::move(parsedParameters), ToColorSpace(color_space), &this->compiled.spectrumFileCache);
            }

            [[nodiscard]] Entity MakeEntity(const scene::Scene::Entity& entity) const {
                if (entity.type.empty()) throw std::runtime_error(std::format("{} scene entity has an empty type.", this->source.source));
                return Entity{
                    .name       = entity.type,
                    .loc        = ToFileLoc(entity.source, this->source.source),
                    .parameters = this->MakeParameterDictionary(entity.parameters, entity.color_space),
                };
            }

            void OverrideIntegerParameter(scene::Scene::Entity* entity, std::string name, int value) const {
                for (scene::Scene::Parameter& parameter : entity->parameters) {
                    if (parameter.type != "integer" || parameter.name != name) continue;
                    parameter.values = std::vector<int>{value};
                    return;
                }
                entity->parameters.push_back(scene::Scene::Parameter{
                    .type       = "integer",
                    .name       = std::move(name),
                    .values     = std::vector<int>{value},
                    .color_space = entity->color_space,
                    .source     = entity->source,
                });
            }

            [[nodiscard]] bool HasIntegerParameter(const scene::Scene::Entity& entity, const std::string_view name) const {
                for (const scene::Scene::Parameter& parameter : entity.parameters) {
                    if (parameter.type == "integer" && parameter.name == name) return true;
                }
                return false;
            }

            void ApplySamplerDefaults(scene::Scene::Entity* sampler) const {
                if (sampler == nullptr) throw std::runtime_error("Spectra pathtracer sampler defaults require a sampler entity.");
                if (!this->renderConfig.default_pixel_samples.has_value()) return;
                if (this->renderConfig.pixel_samples.has_value()) return;
                if (this->HasIntegerParameter(*sampler, "pixelsamples")) return;
                if (*this->renderConfig.default_pixel_samples <= 0) throw std::runtime_error("Spectra pathtracer default sampler SPP must be positive.");
                this->OverrideIntegerParameter(sampler, "pixelsamples", *this->renderConfig.default_pixel_samples);
            }

            void RequireRenderSettings() const {
                if (!this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings must be configured before adding world content.", this->source.source));
            }

            void RequireMaterial(const std::string& material_name) const {
                if (material_name.empty()) throw std::runtime_error(std::format("{} scene shape must specify a material.", this->source.source));
                if (this->material_names.find(material_name) == this->material_names.end()) throw std::runtime_error(std::format("{} scene references unknown material \"{}\".", this->source.source, material_name));
            }

            void RequireUniqueName(const std::set<std::string>& names, std::string_view kind, const std::string& name) const {
                if (name.empty()) throw std::runtime_error(std::format("{} scene {} name must not be empty.", this->source.source, kind));
                if (names.find(name) != names.end()) throw std::runtime_error(std::format("{} scene {} \"{}\" is already defined.", this->source.source, kind, name));
            }

            void ConsumeMatchingTypeParameter(const Entity& entity, std::string_view kind) const {
                const std::string parameterType = entity.parameters.GetOneString("type", "");
                if (!parameterType.empty() && parameterType != entity.name) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s type parameter \"%s\" does not match entity type \"%s\".", kind, parameterType, entity.name));
            }

            void RequireMatchingTypeParameter(const Entity& entity, std::string_view kind) const {
                const std::string parameterType = entity.parameters.GetOneString("type", "");
                if (parameterType.empty()) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s requires \"string type\".", kind));
                if (parameterType != entity.name) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s type parameter \"%s\" does not match entity type \"%s\".", kind, parameterType, entity.name));
            }

            void RegisterResourceNames() {
                for (const scene::Scene::Material& material : this->source.materials) {
                    this->RequireUniqueName(this->material_names, "material", material.name);
                    this->material_names.insert(material.name);
                }
                for (const scene::Scene::Medium& medium : this->source.media) {
                    this->RequireUniqueName(this->mediumNames, "medium", medium.name);
                    this->mediumNames.insert(medium.name);
                }
                for (const scene::Scene::VolumeGrid& volume : this->source.volumes) {
                    bool has_external_gpu_channel = false;
                    for (const scene::Scene::VolumeChannel& channel : volume.channels)
                        if (channel.source_kind == scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer) has_external_gpu_channel = true;
                    if (!has_external_gpu_channel) continue;
                    std::string medium_name = std::format("{}.__medium", volume.name);
                    this->RequireUniqueName(this->mediumNames, "volume medium", medium_name);
                    this->deviceVolumeMediumNames.insert(medium_name);
                    this->mediumNames.insert(std::move(medium_name));
                }
                for (const scene::Scene::ObjectDefinition& definition : this->source.object_definitions) {
                    this->RequireUniqueName(this->objectDefinitionNames, "object definition", definition.name);
                    this->objectDefinitionNames.insert(definition.name);
                }
            }

            [[nodiscard]] Transform RenderFromWorldTransform() const {
                this->RequireRenderSettings();
                return this->cameraEntity.cameraTransform.RenderFromWorld();
            }

            [[nodiscard]] Transform scene_transform(const scene::SceneTransformSet& transform, const scene::Scene::SourceLocation& source) const {
                if (transform.animated) throw std::runtime_error(std::format("{}: animated transform reached GPU scene compiler after validation.", SourceString(source)));
                return ToTransform(transform.start);
            }

            [[nodiscard]] Transform RenderFromObjectTransform(const scene::SceneTransformSet& transform, const scene::Scene::SourceLocation& source) const {
                return this->RenderFromWorldTransform() * this->scene_transform(transform, source);
            }

            [[nodiscard]] ShapeEntity MakeShapeEntity(const scene::Scene::Shape& shape) const {
                const std::string material_name = shape.material_name;
                this->RequireMaterial(material_name);
                Transform renderFromObject = this->RenderFromObjectTransform(shape.transform, shape.entity.source);
                Allocator allocator        = this->compiled.threadAllocators.Get();
                Entity base = this->MakeEntity(shape.entity);
                ShapeEntity entity{
                    .name               = std::move(base.name),
                    .loc                = base.loc,
                    .parameters         = std::move(base.parameters),
                    .renderFromObject   = allocator.new_object<Transform>(renderFromObject),
                    .objectFromRender   = allocator.new_object<Transform>(Inverse(renderFromObject)),
                    .reverseOrientation = shape.reverse_orientation,
                    .material_name       = material_name,
                    .insideMedium       = shape.medium_interface.inside,
                    .outsideMedium      = shape.medium_interface.outside,
                };
                if (!shape.medium_interface.inside.empty() || !shape.medium_interface.outside.empty()) this->compiled.haveMedia = true;
                if (shape.area_light.has_value()) entity.areaLight = this->MakeEntity(shape.area_light->entity);
                return entity;
            }

            [[nodiscard]] Medium FindMedium(const std::string& name, const FileLoc* loc) const {
                if (name.empty()) return nullptr;
                std::map<std::string, Medium>::const_iterator iter = this->compiled.media.find(name);
                if (iter == this->compiled.media.end()) throw std::runtime_error(diagnostics::Format(loc, "%s: medium not defined", name));
                return iter->second;
            }

            void SetRenderSettings(const scene::Scene::RenderSettings& settings) {
                if (this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings are already configured.", this->source.source));

                Entity filterEntity = this->MakeEntity(settings.filter);
                scene::Scene::Entity film            = settings.film;
                scene::Scene::Entity sampler         = settings.sampler;
                this->ApplySamplerDefaults(&sampler);
                if (this->filmResolutionOverride.has_value()) {
                    if (this->filmResolutionOverride->x <= 0 || this->filmResolutionOverride->y <= 0) throw std::runtime_error("Spectra interactive film resolution must be positive.");
                    this->OverrideIntegerParameter(&film, "xresolution", this->filmResolutionOverride->x);
                    this->OverrideIntegerParameter(&film, "yresolution", this->filmResolutionOverride->y);
                }
                Entity filmEntity   = this->MakeEntity(film);
                this->samplerEntity = this->MakeEntity(sampler);
                Entity integratorEntity = this->MakeEntity(settings.integrator);
                this->compiled.integrator = IntegratorSettings{
                    .lightSampler = integratorEntity.parameters.GetOneString("lightsampler", "bvh"),
                    .maxDepth = integratorEntity.parameters.GetOneInt("maxdepth", 5),
                    .regularize = integratorEntity.parameters.GetOneBool("regularize", false),
                };
                Entity cameraEntity = this->MakeEntity(settings.camera);
                const Transform worldFromCamera    = this->scene_transform(settings.camera_transform, settings.camera.source);
                this->cameraEntity                 = CameraEntity{
                    .name            = std::move(cameraEntity.name),
                    .loc             = cameraEntity.loc,
                    .parameters      = std::move(cameraEntity.parameters),
                    .cameraTransform = CameraTransform(AnimatedTransform(worldFromCamera, settings.camera_transform.start_time, worldFromCamera, settings.camera_transform.end_time), this->renderConfig.rendering_space),
                    .medium          = settings.camera_medium,
                };

                this->renderSettingsReady = true;

                Allocator alloc       = this->compiled.threadAllocators.Get();
                this->compiled.filter = Filter::Create(filterEntity.name, filterEntity.parameters, &filterEntity.loc, alloc, this->compiled.deviceArena, this->renderStream);

                Float exposureTime = this->cameraEntity.parameters.GetOneFloat("shutterclose", 1.0f) - this->cameraEntity.parameters.GetOneFloat("shutteropen", 0.0f);
                if (exposureTime <= 0.0f) throw std::runtime_error(diagnostics::Format(&this->cameraEntity.loc, "The specified camera shutter times imply that the shutter does not open. A black image will result."));

                this->compiled.film = Film::Create(filmEntity.name, filmEntity.parameters, exposureTime, this->cameraEntity.cameraTransform, this->compiled.filter, this->renderConfig, &filmEntity.loc, alloc, Allocator(&this->compiled.filmMemoryScope), this->renderStream, &this->compiled.outputFilename);
                const PixelSensor* sensor = this->compiled.film.GetPixelSensor();
                constexpr std::size_t sensor_sample_count = Lambda_max - Lambda_min + 1;
                std::array<std::vector<Float>, 3> sensor_values{
                    std::vector<Float>(sensor_sample_count),
                    std::vector<Float>(sensor_sample_count),
                    std::vector<Float>(sensor_sample_count),
                };
                for (int channel = 0; channel < 3; ++channel)
                    for (int wavelength = Lambda_min; wavelength <= Lambda_max; ++wavelength) sensor_values[channel][wavelength - Lambda_min] = sensor->Response(channel, wavelength);
                DevicePixelSensor device_sensor{
                    .red = this->compiled.deviceArena.StoreArray(sensor_values[0].data(), sensor_values[0].size(), this->renderStream),
                    .green = this->compiled.deviceArena.StoreArray(sensor_values[1].data(), sensor_values[1].size(), this->renderStream),
                    .blue = this->compiled.deviceArena.StoreArray(sensor_values[2].data(), sensor_values[2].size(), this->renderStream),
                    .imagingRatio = sensor->ImagingRatio(),
                };
                this->compiled.film.DispatchHost([&](auto* concrete) { concrete->SetDevicePixelSensor(device_sensor); });
            }

            [[nodiscard]] Sampler CreateSampler() const {
                Allocator alloc = this->compiled.threadAllocators.Get();
                Point2i res     = this->compiled.film.FullResolution();
                return Sampler::Create(this->samplerEntity.name, this->samplerEntity.parameters, res, this->renderConfig, &this->samplerEntity.loc, alloc, this->compiled.deviceArena, this->renderStream);
            }

            [[nodiscard]] Camera CreateCamera() const {
                Allocator alloc     = this->compiled.threadAllocators.Get();
                Medium camera_medium = this->FindMedium(this->cameraEntity.medium, &this->cameraEntity.loc);
                return Camera::Create(this->cameraEntity.name, this->cameraEntity.parameters, camera_medium, this->cameraEntity.cameraTransform, this->compiled.film, &this->cameraEntity.loc, alloc, this->compiled.deviceArena, this->renderStream);
            }

            void AddMaterial(const scene::Scene::Material& material) {
                Entity entity = this->MakeEntity(material.entity);
                this->ConsumeMatchingTypeParameter(entity, "material");
                std::lock_guard<std::mutex> lock(this->materialMutex);
                this->StartLoadingNormalMaps(entity.parameters);
                this->materials.push_back(std::make_pair(material.name, std::move(entity)));
            }

            void AddTexture(const scene::Scene::Texture& texture) {
                this->RequireRenderSettings();
                if (texture.kind == "float")
                    this->RequireUniqueName(this->floatTextureNames, "float texture", texture.name);
                else
                    this->RequireUniqueName(this->spectrumTextureNames, "spectrum texture", texture.name);

                Entity base = this->MakeEntity(texture.entity);
                TransformedEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(texture.transform, texture.entity.source),
                };

                std::lock_guard<std::mutex> lock(this->textureMutex);
                if (texture.kind == "float") {
                    this->floatTextureNames.insert(texture.name);
                    if (!IsImageTexture(entity.name)) {
                        this->serialFloatTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                } else {
                    this->spectrumTextureNames.insert(texture.name);
                    if (!IsImageTexture(entity.name)) {
                        this->serialSpectrumTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                }

                std::string filename = entity.parameters.GetOneString("filename", "");
                if (filename.empty()) throw std::runtime_error(diagnostics::Format(&entity.loc, "\"string filename\" not provided for image texture."));
                if (!FileExists(filename)) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s: file not found.", filename));

                if (this->loadingTextureFilenames.find(filename) != this->loadingTextureFilenames.end()) {
                    if (texture.kind == "float")
                        this->serialFloatTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                    else
                        this->serialSpectrumTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                    return;
                }

                this->loadingTextureFilenames.insert(filename);
                if (texture.kind == "float") {
                    auto create = [entity, this]() {
                        Allocator alloc             = this->compiled.threadAllocators.Get();
                        Transform renderFromTexture = entity.renderFromObject;
                        TextureParameterDictionary textureParameters(&entity.parameters, nullptr);
                        return FloatTexture::Create(entity.name, renderFromTexture, textureParameters, &entity.loc, this->compiled.textureCache, alloc);
                    };
                    this->floatTextureJobs[texture.name] = RunAsync(create);
                } else {
                    this->asyncSpectrumTextures.push_back(std::make_pair(texture.name, entity));
                    auto create = [entity, this]() {
                        Allocator alloc             = this->compiled.threadAllocators.Get();
                        Transform renderFromTexture = entity.renderFromObject;
                        TextureParameterDictionary textureParameters(&entity.parameters, nullptr);
                        return SpectrumTexture::Create(entity.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &entity.loc, this->compiled.textureCache, alloc);
                    };
                    this->spectrumTextureJobs[texture.name] = RunAsync(create);
                }
            }

            void AddMedium(const scene::Scene::Medium& medium) {
                this->RequireRenderSettings();

                Entity base = this->MakeEntity(medium.entity);
                this->RequireMatchingTypeParameter(base, "medium");
                TransformedEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(medium.transform, medium.entity.source),
                };

                auto create = [entity, this]() { return Medium::Create(entity.name, entity.parameters, entity.renderFromObject, &entity.loc, this->compiled.threadAllocators.Get()); };

                std::lock_guard<std::mutex> lock(this->mediaMutex);
                this->mediumJobs[medium.name] = RunAsync(create);
            }

            void AddLight(const scene::Scene::Light& light) {
                this->RequireRenderSettings();

                Entity base = this->MakeEntity(light.entity);
                LightEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(light.transform, light.entity.source),
                    .medium           = light.medium,
                };

                Medium lightMedium = this->FindMedium(entity.medium, &entity.loc);
                auto create        = [this, entity, lightMedium]() { return Light::Create(entity.name, entity.parameters, entity.renderFromObject, this->compiled.camera.GetCameraTransform(), lightMedium, &entity.loc, this->compiled.lightSpectrumCache, this->compiled.threadAllocators.Get()); };

                std::lock_guard<std::mutex> lock(this->lightMutex);
                this->lightJobs.push_back(RunAsync(create));
            }

            void AddShape(const scene::Scene::Shape& shape) {
                this->RequireRenderSettings();
                this->compiled.shapes.push_back(this->MakeShapeEntity(shape));
            }

            void AddObjectDefinition(const scene::Scene::ObjectDefinition& definition) {
                this->RequireRenderSettings();
                InstanceDefinitionEntity entity{
                    .name = definition.name,
                    .loc  = ToFileLoc(definition.source, this->source.source),
                };
                entity.shapes.reserve(definition.shapes.size());
                for (const scene::Scene::Shape& shape : definition.shapes) {
                    ShapeEntity shapeEntity = this->MakeShapeEntity(shape);
                    if (shapeEntity.areaLight.has_value()) throw std::runtime_error(std::format("{} scene object definition \"{}\" contains an area light shape; instanced area lights are not supported.", this->source.source, definition.name));
                    entity.shapes.push_back(std::move(shapeEntity));
                }

                this->compiled.instanceDefinitions[definition.name] = std::move(entity);
            }

            void AddObjectInstance(const scene::Scene::ObjectInstance& instance) {
                this->RequireRenderSettings();
                if (this->objectDefinitionNames.find(instance.definition_name) == this->objectDefinitionNames.end()) throw std::runtime_error(std::format("{} scene references unknown object definition \"{}\".", this->source.source, instance.definition_name));

                Transform worldFromRender = Inverse(this->RenderFromWorldTransform());
                this->compiled.instances.push_back({
                    .name               = instance.definition_name,
                    .loc                = ToFileLoc(instance.source, this->source.source),
                    .renderFromInstance = this->RenderFromObjectTransform(instance.transform, instance.source) * worldFromRender,
                });
            }

            [[nodiscard]] const scene::Scene::PreviewMaterial& FindPreviewMaterial(const std::string& name) const {
                for (const scene::Scene::PreviewMaterial& material : this->source.preview_materials)
                    if (material.name == name) return material;
                throw std::runtime_error(std::format("{} volume references unknown preview material \"{}\"", this->source.source, name));
            }

            [[nodiscard]] const scene::Scene::VolumeChannel& FindVolumeChannel(const scene::Scene::VolumeGrid& volume, const scene::Scene::VolumeChannelBinding& binding) const {
                for (const scene::Scene::VolumeChannel& channel : volume.channels)
                    if (channel.name == binding.channel_name) return channel;
                throw std::runtime_error(std::format("{} volume \"{}\" references unknown channel \"{}\"", this->source.source, volume.name, binding.channel_name));
            }

            [[nodiscard]] static std::uint64_t SpreadMortonBits(std::uint32_t value) {
                std::uint64_t result{};
                for (std::uint32_t bit = 0u; bit < 21u; ++bit) result |= static_cast<std::uint64_t>((value >> bit) & 1u) << (3u * bit);
                return result;
            }

            [[nodiscard]] DeviceVolumeChannelBuildInput MakeVolumeChannelInput(const scene::Scene::VolumeGrid& volume, const scene::Scene::VolumeChannelBinding& binding) const {
                const scene::Scene::VolumeChannel& channel = this->FindVolumeChannel(volume, binding);
                std::uint32_t component_count = scene::volume_channel_component_count(channel.format);
                std::size_t source_value_count = channel.source_kind == scene::Scene::VolumeChannelSourceKind::Values ? channel.values.size() : static_cast<std::size_t>(channel.source_byte_size / sizeof(float));
                if (channel.index_encoding == scene::Scene::VolumeChannelIndexEncoding::Morton3D) {
                    std::uint64_t maximum_index = SpreadMortonBits(volume.dimensions[0] - 1u) | (SpreadMortonBits(volume.dimensions[1] - 1u) << 1u) | (SpreadMortonBits(volume.dimensions[2] - 1u) << 2u);
                    if ((maximum_index + 1u) * component_count > source_value_count) throw std::runtime_error(std::format("{} volume \"{}\" channel \"{}\" Morton source does not cover its dimensions", this->source.source, volume.name, channel.name));
                }
                return DeviceVolumeChannelBuildInput{
                    .host_values = channel.source_kind == scene::Scene::VolumeChannelSourceKind::Values ? channel.values.data() : nullptr,
                    .device_values = channel.source_kind == scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer ? reinterpret_cast<const float*>(channel.external_device_pointer) : nullptr,
                    .source_value_count = source_value_count,
                    .component_count = component_count,
                    .first_component = binding.component,
                    .scale = binding.scale,
                    .bias = binding.bias,
                    .morton_encoded = channel.index_encoding == scene::Scene::VolumeChannelIndexEncoding::Morton3D,
                    .ready_event = channel.source_kind == scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer ? reinterpret_cast<cudaEvent_t>(channel.external_ready_event) : nullptr,
                };
            }

            void CreateDeviceVolumes() {
                if (this->deviceVolumeMediumNames.empty()) return;
                DeviceSceneBuilder device_builder(this->compiled.deviceArena, this->renderStream);
                const RGBColorSpace* color_space = device_builder.CompileRGBColorSpace(RGBColorSpace::SRGB());
                for (const scene::Scene::VolumeGrid& volume : this->source.volumes) {
                    if (!this->deviceVolumeMediumNames.contains(std::format("{}.__medium", volume.name))) continue;
                    const scene::Scene::PreviewMaterial& material = this->FindPreviewMaterial(volume.material_name);
                    Vector3f extent{
                        static_cast<Float>(volume.dimensions[0]) * volume.voxel_size.x,
                        static_cast<Float>(volume.dimensions[1]) * volume.voxel_size.y,
                        static_cast<Float>(volume.dimensions[2]) * volume.voxel_size.z,
                    };
                    Transform render_from_medium = this->RenderFromWorldTransform() * Translate(Vector3f{volume.origin.x, volume.origin.y, volume.origin.z}) * Scale(extent.x, extent.y, extent.z);
                    DeviceVolumeMediumBuildInput input{
                        .dimensions = Point3i{static_cast<int>(volume.dimensions[0]), static_cast<int>(volume.dimensions[1]), static_cast<int>(volume.dimensions[2])},
                        .bounds = Bounds3f{Point3f{0.f, 0.f, 0.f}, Point3f{1.f, 1.f, 1.f}},
                        .render_from_medium = render_from_medium,
                        .color_space = color_space,
                        .density = this->MakeVolumeChannelInput(volume, material.volume.density),
                        .has_color = material.volume.color.enabled,
                        .has_emission = material.volume.emission.enabled,
                    };
                    if (input.has_color) input.color = this->MakeVolumeChannelInput(volume, material.volume.color);
                    if (input.has_emission) input.emission = this->MakeVolumeChannelInput(volume, material.volume.emission);
                    std::unique_ptr<DeviceVolumeMediumStorage> storage = std::make_unique<DeviceVolumeMediumStorage>(input, this->renderStream);
                    this->compiled.media[std::format("{}.__medium", volume.name)] = storage->medium();
                    this->compiled.haveMedia = true;
                    this->compiled.haveEmissiveMedia |= input.has_emission;
                    this->compiled.deviceVolumeMedia.push_back(std::move(storage));
                }
            }

            [[nodiscard]] std::map<std::string, Medium> CreateMedia() {
                std::map<std::string, Medium> mediaMap;
                std::unique_lock<std::mutex> lock(this->mediaMutex);
                for (const auto& mediumJob : this->mediumJobs) {
                    while (mediaMap.find(mediumJob.first) == mediaMap.end()) {
                        pstd::optional<Medium> medium = mediumJob.second->TryGetResult(lock);
                        if (medium) mediaMap[mediumJob.first] = *medium;
                    }
                }
                this->mediumJobs.clear();
                return mediaMap;
            }

            void StartLoadingNormalMaps(const ParameterDictionary& parameters) {
                std::string filename = parameters.GetOneString("normalmap", "");
                if (filename.empty()) return;
                if (this->normalMapJobs.find(filename) != this->normalMapJobs.end()) return;

                auto create = [filename, this]() {
                    Allocator alloc          = this->compiled.threadAllocators.Get();
                    ImageAndMetadata immeta  = Image::Read(filename, Allocator(), ColorEncoding::Linear);
                    Image& image             = immeta.image;
                    ImageChannelDesc rgbDesc = image.GetChannelDesc({"R", "G", "B"});
                    if (!rgbDesc) throw std::runtime_error(diagnostics::Format("%s: normal map image must contain R, G, and B channels", filename));
                    Image* normalMap = alloc.new_object<Image>(alloc);
                    *normalMap       = image.SelectChannels(rgbDesc);
                    return normalMap;
                };
                this->normalMapJobs[filename] = RunAsync(create);
            }

            void CreateMaterials() {
                std::lock_guard<std::mutex> lock(this->materialMutex);
                for (const auto& job : this->normalMapJobs) {
                    SPECTRA_CHECK(this->normalMaps.find(job.first) == this->normalMaps.end());
                    this->normalMaps[job.first] = job.second->GetResult();
                }
                this->normalMapJobs.clear();

                for (const std::pair<std::string, Entity>& material : this->materials) {
                    const std::string& name             = material.first;
                    const Entity& entity = material.second;
                    Allocator alloc                     = this->compiled.threadAllocators.Get();
                    std::string normalMapName           = entity.parameters.GetOneString("normalmap", "");
                    Image* normalMap                    = nullptr;
                    if (!normalMapName.empty()) {
                        SPECTRA_CHECK(this->normalMaps.find(normalMapName) != this->normalMaps.end());
                        normalMap = this->normalMaps[normalMapName];
                    }

                    TextureParameterDictionary textureParameters(&entity.parameters, &this->compiled.textures);
                    Material createdMaterial       = Material::Create(entity.name, textureParameters, normalMap, this->compiled.materials, this->compiled.measuredBxDFData, &entity.loc, alloc);
                    this->compiled.materials[name] = createdMaterial;
                }
            }

            [[nodiscard]] NamedTextures CreateTextures() {
                NamedTextures textures;

                this->textureMutex.lock();
                for (const auto& texture : this->floatTextureJobs) textures.floatTextures[texture.first] = texture.second->GetResult();
                this->floatTextureJobs.clear();
                for (const auto& texture : this->spectrumTextureJobs) textures.albedoSpectrumTextures[texture.first] = texture.second->GetResult();
                this->spectrumTextureJobs.clear();
                this->textureMutex.unlock();

                Allocator alloc = this->compiled.threadAllocators.Get();
                for (const std::pair<std::string, TransformedEntity>& texture : this->asyncSpectrumTextures) {
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, nullptr);
                    SpectrumTexture unboundedTexture                   = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, this->compiled.textureCache, alloc);
                    SpectrumTexture illuminantTexture                  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, this->compiled.textureCache, alloc);
                    textures.unboundedSpectrumTextures[texture.first]  = unboundedTexture;
                    textures.illuminantSpectrumTextures[texture.first] = illuminantTexture;
                }

                for (const std::pair<std::string, TransformedEntity>& texture : this->serialFloatTextures) {
                    Allocator alloc             = this->compiled.threadAllocators.Get();
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
                    textures.floatTextures[texture.first] = FloatTexture::Create(texture.second.name, renderFromTexture, textureParameters, &texture.second.loc, this->compiled.textureCache, alloc);
                }

                for (const std::pair<std::string, TransformedEntity>& texture : this->serialSpectrumTextures) {
                    Allocator alloc             = this->compiled.threadAllocators.Get();
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
                    textures.albedoSpectrumTextures[texture.first]     = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &texture.second.loc, this->compiled.textureCache, alloc);
                    textures.unboundedSpectrumTextures[texture.first]  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, this->compiled.textureCache, alloc);
                    textures.illuminantSpectrumTextures[texture.first] = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, this->compiled.textureCache, alloc);
                }

                return textures;
            }

            [[nodiscard]] std::vector<Light> CreateLights() {
                Allocator alloc = this->compiled.threadAllocators.Get();

                auto getAlphaTexture = [&](const ParameterDictionary& parameters, const FileLoc* loc) -> FloatTexture {
                    std::string alphaTextureName = parameters.GetTexture("alpha");
                    if (!alphaTextureName.empty()) {
                        std::map<std::string, FloatTexture>::const_iterator iter = this->compiled.textures.floatTextures.find(alphaTextureName);
                        if (iter == this->compiled.textures.floatTextures.end()) throw std::runtime_error(diagnostics::Format(loc, "%s: couldn't find float texture for \"alpha\" parameter.", alphaTextureName));
                        if (!BasicTextureEvaluator().CanEvaluate({iter->second}, {})) return nullptr;
                        return iter->second;
                    }

                    Float alpha = parameters.GetOneFloat("alpha", 1.0f);
                    if (alpha < 1.0f) return alloc.new_object<FloatConstantTexture>(alpha);
                    return nullptr;
                };

                std::vector<Light> lights;
                for (std::size_t index = 0; index < this->compiled.shapes.size(); ++index) {
                    const ShapeEntity& shape = this->compiled.shapes[index];
                    if (!shape.areaLight.has_value()) continue;

                    std::map<std::string, Material>::const_iterator materialIter = this->compiled.materials.find(shape.material_name);
                    if (materialIter == this->compiled.materials.end()) throw std::runtime_error(diagnostics::Format(&shape.loc, "%s: no named material defined.", shape.material_name));

                    if (!materialIter->second) throw std::runtime_error(diagnostics::Format(&shape.loc, "Area light shape \"%s\" cannot use an interface material.", shape.name));

                    pstd::vector<Shape> shapeObjects = Shape::Create(shape.name, shape.renderFromObject, shape.objectFromRender, shape.reverseOrientation, shape.parameters, this->compiled.textures.floatTextures, this->renderConfig, &shape.loc, this->compiled.meshBufferCache, alloc);
                    FloatTexture alphaTexture        = getAlphaTexture(shape.parameters, &shape.loc);
                    MediumInterface medium_interface(this->FindMedium(shape.insideMedium, &shape.loc), this->FindMedium(shape.outsideMedium, &shape.loc));
                    pstd::vector<Light>* shapeLights = alloc.new_object<pstd::vector<Light>>(alloc);
                    for (Shape shapeObject : shapeObjects) {
                        Light area_light = Light::CreateArea(shape.areaLight->name, shape.areaLight->parameters, *shape.renderFromObject, medium_interface, shapeObject, alphaTexture, &shape.areaLight->loc, this->compiled.lightSpectrumCache, alloc);
                        if (area_light) {
                            lights.push_back(area_light);
                            shapeLights->push_back(area_light);
                        }
                    }
                    this->compiled.shapeIndexToAreaLights[static_cast<int>(index)] = shapeLights;
                }

                std::lock_guard<std::mutex> lock(this->lightMutex);
                for (const std::unique_ptr<AsyncJob<Light>>& job : this->lightJobs) lights.push_back(job->GetResult());
                return lights;
            }

            const scene::Scene::ResolvedScene& source;
            CompiledScene& compiled;
            RenderConfig renderConfig;
            std::optional<Point2i> filmResolutionOverride{};
            cudaStream_t renderStream{};
            bool renderSettingsReady{false};
            Entity samplerEntity{};
            CameraEntity cameraEntity{};
            std::mutex mediaMutex;
            std::map<std::string, std::unique_ptr<AsyncJob<Medium>>> mediumJobs{};
            std::mutex materialMutex;
            std::map<std::string, std::unique_ptr<AsyncJob<Image*>>> normalMapJobs{};
            std::map<std::string, Image*> normalMaps{};
            std::vector<std::pair<std::string, Entity>> materials{};
            std::mutex lightMutex;
            std::vector<std::unique_ptr<AsyncJob<Light>>> lightJobs{};
            std::mutex textureMutex;
            std::vector<std::pair<std::string, TransformedEntity>> serialFloatTextures{};
            std::vector<std::pair<std::string, TransformedEntity>> serialSpectrumTextures{};
            std::vector<std::pair<std::string, TransformedEntity>> asyncSpectrumTextures{};
            std::set<std::string> loadingTextureFilenames{};
            std::map<std::string, std::unique_ptr<AsyncJob<FloatTexture>>> floatTextureJobs{};
            std::map<std::string, std::unique_ptr<AsyncJob<SpectrumTexture>>> spectrumTextureJobs{};
            std::set<std::string> material_names{};
            std::set<std::string> mediumNames{};
            std::set<std::string> deviceVolumeMediumNames{};
            std::set<std::string> floatTextureNames{};
            std::set<std::string> spectrumTextureNames{};
            std::set<std::string> objectDefinitionNames{};
        };
    } // namespace

    SceneSupportReport AnalyzeSceneSupport(const scene::Scene::ResolvedScene& scene) {
        static const std::set<std::string> supportedFilters{"box", "gaussian", "mitchell", "sinc", "triangle"};
        static const std::set<std::string> supportedFilms{"rgb", "gbuffer", "spectral"};
        static const std::set<std::string> supportedCameras{"perspective", "orthographic", "realistic", "spherical"};
        static const std::set<std::string> supportedSamplers{"zsobol", "paddedsobol", "halton", "sobol", "pmj02bn", "independent", "stratified"};
        static const std::set<std::string> supportedIntegrators{"path", "volpath"};
        static const std::set<std::string> supportedAccelerators{"bvh"};
        static const std::set<std::string> supportedMaterials{"interface", "diffuse", "coateddiffuse", "coatedconductor", "diffusetransmission", "dielectric", "thindielectric", "hair", "conductor", "measured", "subsurface", "mix"};
        static const std::set<std::string> supportedTextures{"constant", "scale", "mix", "directionmix", "bilerp", "imagemap", "checkerboard", "dots", "fbm", "wrinkled", "windy", "marble", "ptex"};
        static const std::set<std::string> supportedMedia{"homogeneous", "uniformgrid", "rgbgrid", "cloud", "nanovdb"};
        static const std::set<std::string> supportedLights{"point", "spot", "goniometric", "projection", "distant", "infinite"};
        static const std::set<std::string> supportedAreaLights{"diffuse"};
        static const std::set<std::string> supportedShapes{"sphere", "cylinder", "disk", "bilinearmesh", "curve", "trianglemesh", "plymesh", "loopsubdiv"};
        static const std::set<std::string> supportedLightSamplers{"uniform", "power", "bvh", "exhaustive"};

        SceneSupportReport report{};
        ValidateEntityType(&report, scene.render_settings.filter, supportedFilters, "pixel filter");
        ValidateEntityType(&report, scene.render_settings.film, supportedFilms, "film");
        ValidateEntityType(&report, scene.render_settings.camera, supportedCameras, "camera");
        ValidateEntityType(&report, scene.render_settings.sampler, supportedSamplers, "sampler");
        ValidateEntityType(&report, scene.render_settings.integrator, supportedIntegrators, "integrator");
        ValidateEntityType(&report, scene.render_settings.accelerator, supportedAccelerators, "accelerator");
        ValidateTransform(&report, scene.render_settings.camera_transform, scene.render_settings.camera.source, "camera");

        for (const scene::Scene::Option& option : scene.render_settings.options) AddDiagnostic(&report, option.source, std::format("PBRT Option \"{}\" is represented by the scene document but is not wired into the current pathtracer runtime", option.name));

        const std::string lightSampler = OneStringParameter(scene.render_settings.integrator.parameters, "lightsampler", "");
        if (!lightSampler.empty() && !ContainsName(supportedLightSamplers, lightSampler)) AddDiagnostic(&report, scene.render_settings.integrator.source, std::format("GPU pathtracer does not support light sampler \"{}\"", lightSampler));

        std::set<std::string> materials{};
        for (const scene::Scene::Material& material : scene.materials) {
            materials.insert(material.name);
            ValidateEntityType(&report, material.entity, supportedMaterials, "material");
        }

        std::set<std::string> media{};
        for (const scene::Scene::Medium& medium : scene.media) {
            media.insert(medium.name);
            ValidateEntityType(&report, medium.entity, supportedMedia, "medium");
            ValidateTransform(&report, medium.transform, medium.entity.source, std::format("medium \"{}\"", medium.name));
        }
        for (const scene::Scene::VolumeGrid& volume : scene.volumes) media.insert(std::format("{}.__medium", volume.name));

        for (const scene::Scene::Texture& texture : scene.textures) {
            if (texture.kind != "float" && texture.kind != "spectrum") AddDiagnostic(&report, texture.entity.source, std::format("GPU pathtracer does not support texture value kind \"{}\"", texture.kind));
            ValidateEntityType(&report, texture.entity, supportedTextures, "texture");
            if (texture.kind == "float" && texture.entity.type == "marble") AddDiagnostic(&report, texture.entity.source, "\"marble\" is only a spectrum texture in the GPU pathtracer");
            if (texture.kind == "spectrum" && (texture.entity.type == "fbm" || texture.entity.type == "wrinkled" || texture.entity.type == "windy")) AddDiagnostic(&report, texture.entity.source, std::format("\"{}\" is only a float texture in the GPU pathtracer", texture.entity.type));
            ValidateTransform(&report, texture.transform, texture.entity.source, std::format("texture \"{}\"", texture.name));
        }

        const auto validateShape = [&report, &materials, &media](const scene::Scene::Shape& shape, const std::string_view owner) {
            ValidateEntityType(&report, shape.entity, supportedShapes, "shape");
            ValidateTransform(&report, shape.transform, shape.entity.source, owner);
            if (shape.material_name.empty() || !ContainsName(materials, shape.material_name)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown material \"{}\"", owner, shape.material_name));
            if (!shape.medium_interface.inside.empty() && !ContainsName(media, shape.medium_interface.inside)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown inside medium \"{}\"", owner, shape.medium_interface.inside));
            if (!shape.medium_interface.outside.empty() && !ContainsName(media, shape.medium_interface.outside)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown outside medium \"{}\"", owner, shape.medium_interface.outside));
            if (shape.area_light.has_value()) ValidateEntityType(&report, shape.area_light->entity, supportedAreaLights, "area light");
        };

        for (const scene::Scene::Light& light : scene.lights) {
            ValidateEntityType(&report, light.entity, supportedLights, "light");
            ValidateTransform(&report, light.transform, light.entity.source, std::format("light \"{}\"", light.name));
            if (!light.medium.empty() && !ContainsName(media, light.medium)) AddDiagnostic(&report, light.entity.source, std::format("light \"{}\" references unknown medium \"{}\"", light.name, light.medium));
        }

        for (const scene::Scene::Shape& shape : scene.shapes) validateShape(shape, std::format("shape \"{}\"", shape.name));

        std::set<std::string> object_definitions{};
        for (const scene::Scene::ObjectDefinition& definition : scene.object_definitions) {
            object_definitions.insert(definition.name);
            for (const scene::Scene::Shape& shape : definition.shapes) {
                validateShape(shape, std::format("object definition \"{}\" shape", definition.name));
                if (shape.area_light.has_value()) AddDiagnostic(&report, shape.entity.source, std::format("object definition \"{}\" contains an area light shape; instanced area lights are not supported by the current GPU pathtracer", definition.name));
            }
        }

        for (const scene::Scene::ObjectInstance& instance : scene.object_instances) {
            if (!ContainsName(object_definitions, instance.definition_name)) AddDiagnostic(&report, instance.source, std::format("object instance references unknown definition \"{}\"", instance.definition_name));
            ValidateTransform(&report, instance.transform, instance.source, std::format("object instance \"{}\"", instance.name));
        }

        return report;
    }

    CompiledScene::~CompiledScene() noexcept = default;

    std::unique_ptr<CompiledScene> CompileScene(const scene::Scene::ResolvedScene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride, cudaStream_t renderStream) {
        std::unique_ptr<CompiledScene> compiled = std::make_unique<CompiledScene>(memoryResource);
        SceneCompiler compiler(scene, *compiled, config, filmResolutionOverride, renderStream);
        compiler.Compile();
        compiled->deviceArena.FinishUploads(renderStream);
        return compiled;
    }

} // namespace spectra::pathtracer
