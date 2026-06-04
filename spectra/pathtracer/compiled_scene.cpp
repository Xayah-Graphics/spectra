#include <cstddef>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <array>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <spectra/pathtracer/base/material.cuh>
#include <spectra/pathtracer/base/shape.cuh>
#include <spectra/pathtracer/compiled_scene.cuh>
#include <spectra/pathtracer/core/cameras.cuh>
#include <spectra/pathtracer/core/diagnostics.cuh>
#include <spectra/pathtracer/core/film.cuh>
#include <spectra/pathtracer/core/filters.cuh>
#include <spectra/pathtracer/core/lights.cuh>
#include <spectra/pathtracer/core/materials.cuh>
#include <spectra/pathtracer/core/media.cuh>
#include <spectra/pathtracer/core/paramdict.cuh>
#include <spectra/pathtracer/core/render_config.cuh>
#include <spectra/pathtracer/core/samplers.cuh>
#include <spectra/pathtracer/core/textures.cuh>
#include <spectra/pathtracer/util/color.cuh>
#include <spectra/pathtracer/util/colorspace.cuh>
#include <spectra/pathtracer/util/file.h>
#include <spectra/pathtracer/util/image.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/mesh.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/transform.cuh>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

import spectra.scene;

namespace spectra::pathtracer {
    namespace {
        [[nodiscard]] SquareMatrix<4> ToPathtracerMatrix(const std::array<float, 16>& matrix) {
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

        [[nodiscard]] Transform ToPathtracerTransform(const math::Transform& transform) {
            return Transform(ToPathtracerMatrix(transform.matrix), ToPathtracerMatrix(transform.inverse));
        }

        [[nodiscard]] const RGBColorSpace* ToPathtracerColorSpace(scene::ColorSpace colorSpace) {
            switch (colorSpace) {
            case scene::ColorSpace::sRGB: return RGBColorSpace::SRGB();
            case scene::ColorSpace::DCI_P3: return RGBColorSpace::DCI_P3();
            case scene::ColorSpace::Rec2020: return RGBColorSpace::Rec2020();
            case scene::ColorSpace::ACES2065_1: return RGBColorSpace::ACES2065_1();
            }
            throw std::runtime_error("Unknown Spectra scene color space.");
        }

        [[nodiscard]] FileLoc ToFileLoc(const scene::SceneSourceLocation& source, const std::string& fallback) {
            FileLoc location(source.filename.empty() ? std::string_view(fallback) : std::string_view(source.filename));
            location.line   = source.line;
            location.column = source.column;
            return location;
        }

        [[nodiscard]] std::string SourceString(const scene::SceneSourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::string FormatGpuSupportReport(const scene::SceneTranslationReport& report) {
            std::string message = "Scene is not supported by the current GPU pathtracer:";
            for (const scene::SceneDiagnostic& diagnostic : report.diagnostics) message += std::format("\n  {}: {}", SourceString(diagnostic.source), diagnostic.message);
            return message;
        }

        [[nodiscard]] bool ContainsName(const std::set<std::string>& values, const std::string& value) {
            return values.find(value) != values.end();
        }

        [[nodiscard]] std::string OneStringParameter(const std::vector<scene::SceneParameter>& parameters, const std::string& name, std::string fallback) {
            for (const scene::SceneParameter& parameter : parameters) {
                if (parameter.name != name) continue;
                if (const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values); values != nullptr && !values->empty()) return values->front();
            }
            return fallback;
        }

        void AddDiagnostic(scene::SceneTranslationReport* report, scene::SceneSourceLocation source, std::string message) {
            report->supported = false;
            report->diagnostics.push_back(scene::SceneDiagnostic{
                .source  = std::move(source),
                .message = std::move(message),
            });
        }

        void ValidateTransform(scene::SceneTranslationReport* report, const scene::SceneTransformSet& transform, const scene::SceneSourceLocation& source, const std::string_view owner) {
            if (transform.animated) AddDiagnostic(report, source, std::format("{} uses animated transforms, which are represented by the scene document but are not supported by the current GPU pathtracer", owner));
        }

        void ValidateEntityType(scene::SceneTranslationReport* report, const scene::SceneEntity& entity, const std::set<std::string>& supported, const std::string_view kind) {
            if (!ContainsName(supported, entity.type)) AddDiagnostic(report, entity.source, std::format("GPU pathtracer does not support {} type \"{}\"", kind, entity.type));
        }

        void AppendParameterValues(ParsedParameter* parsedParameter, const scene::SceneParameter& parameter) {
            if (const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values)) {
                for (float value : *values) parsedParameter->AddFloat(value);
                return;
            }
            if (const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values)) {
                for (int value : *values) parsedParameter->AddInt(value);
                return;
            }
            if (const std::vector<std::uint8_t>* values = std::get_if<std::vector<std::uint8_t>>(&parameter.values)) {
                for (std::uint8_t value : *values) parsedParameter->AddBool(value != 0);
                return;
            }
            const std::vector<std::string>& values = std::get<std::vector<std::string>>(parameter.values);
            for (const std::string& value : values) parsedParameter->AddString(value);
        }

        [[nodiscard]] bool IsImageTexture(const std::string& type) {
            return type == "imagemap" || type == "ptex";
        }

        [[nodiscard]] pstd::pmr::memory_resource* RequireMemoryResource(pstd::pmr::memory_resource* memoryResource) {
            if (memoryResource == nullptr) throw std::runtime_error("Compiled pathtracer scene requires a memory resource.");
            return memoryResource;
        }

        class PathtracerSceneCompiler {
        public:
            PathtracerSceneCompiler(const scene::SceneSnapshot& sourceScene, CompiledPathtracerScene& compiledScene, const RenderConfig& config, std::optional<Point2i> resolutionOverride) : source(sourceScene), compiled(compiledScene), renderConfig(config), filmResolutionOverride(resolutionOverride), location(sourceScene.source) {}

            void Compile() {
                const scene::SceneTranslationReport supportReport = AnalyzePathtracerSceneSupport(this->source);
                if (!supportReport.supported) throw std::runtime_error(FormatGpuSupportReport(supportReport));

                this->RegisterResourceNames();
                this->SetRenderSettings(this->source.renderSettings);
                for (const scene::SceneMaterial& material : this->source.materials) this->AddMaterial(material);
                for (const scene::SceneTexture& texture : this->source.textures) this->AddTexture(texture);
                for (const scene::SceneMedium& medium : this->source.media) this->AddMedium(medium);

                this->compiled.media   = this->CreateMedia();
                this->compiled.sampler = this->CreateSampler();
                this->compiled.camera  = this->CreateCamera();

                for (const scene::SceneLight& light : this->source.lights) this->AddLight(light);
                for (const scene::SceneShape& shape : this->source.shapes) this->AddShape(shape);
                for (const scene::SceneObjectDefinition& definition : this->source.objectDefinitions) this->AddObjectDefinition(definition);
                for (const scene::SceneObjectInstance& instance : this->source.objectInstances) this->AddObjectInstance(instance);

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
            [[nodiscard]] ParameterDictionary MakeParameterDictionary(const std::vector<scene::SceneParameter>& parameters, scene::ColorSpace colorSpace) const {
                ParsedParameterVector parsedParameters{};
                for (const scene::SceneParameter& parameter : parameters) {
                    if (parameter.type.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty type.", this->source.source));
                    if (parameter.name.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty name.", this->source.source));
                    ParsedParameter* parsedParameter = new ParsedParameter(ToFileLoc(parameter.source, this->source.source));
                    parsedParameter->type            = parameter.type;
                    parsedParameter->name            = parameter.name;
                    parsedParameter->mayBeUnused     = parameter.mayBeUnused;
                    parsedParameter->colorSpace      = ToPathtracerColorSpace(parameter.colorSpace);
                    AppendParameterValues(parsedParameter, parameter);
                    parsedParameters.push_back(parsedParameter);
                }

                return ParameterDictionary(std::move(parsedParameters), ToPathtracerColorSpace(colorSpace));
            }

            [[nodiscard]] PathtracerSceneEntity MakeEntity(const scene::SceneEntity& entity) const {
                if (entity.type.empty()) throw std::runtime_error(std::format("{} scene entity has an empty type.", this->source.source));
                return PathtracerSceneEntity{
                    .name       = entity.type,
                    .loc        = ToFileLoc(entity.source, this->source.source),
                    .parameters = this->MakeParameterDictionary(entity.parameters, entity.colorSpace),
                };
            }

            void OverrideIntegerParameter(scene::SceneEntity* entity, std::string name, int value) const {
                for (scene::SceneParameter& parameter : entity->parameters) {
                    if (parameter.type != "integer" || parameter.name != name) continue;
                    parameter.values = std::vector<int>{value};
                    return;
                }
                entity->parameters.push_back(scene::SceneParameter{
                    .type       = "integer",
                    .name       = std::move(name),
                    .values     = std::vector<int>{value},
                    .colorSpace = entity->colorSpace,
                    .source     = entity->source,
                });
            }

            void RequireRenderSettings() const {
                if (!this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings must be configured before adding world content.", this->source.source));
            }

            void RequireMaterial(const std::string& materialName) const {
                if (materialName.empty()) throw std::runtime_error(std::format("{} scene shape must specify a material.", this->source.source));
                if (this->materialNames.find(materialName) == this->materialNames.end()) throw std::runtime_error(std::format("{} scene references unknown material \"{}\".", this->source.source, materialName));
            }

            void RequireUniqueName(const std::set<std::string>& names, std::string_view kind, const std::string& name) const {
                if (name.empty()) throw std::runtime_error(std::format("{} scene {} name must not be empty.", this->source.source, kind));
                if (names.find(name) != names.end()) throw std::runtime_error(std::format("{} scene {} \"{}\" is already defined.", this->source.source, kind, name));
            }

            void ConsumeMatchingTypeParameter(const PathtracerSceneEntity& entity, std::string_view kind) const {
                const std::string parameterType = entity.parameters.GetOneString("type", "");
                if (!parameterType.empty() && parameterType != entity.name) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s type parameter \"%s\" does not match entity type \"%s\".", kind, parameterType, entity.name));
            }

            void RequireMatchingTypeParameter(const PathtracerSceneEntity& entity, std::string_view kind) const {
                const std::string parameterType = entity.parameters.GetOneString("type", "");
                if (parameterType.empty()) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s requires \"string type\".", kind));
                if (parameterType != entity.name) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s type parameter \"%s\" does not match entity type \"%s\".", kind, parameterType, entity.name));
            }

            void RegisterResourceNames() {
                for (const scene::SceneMaterial& material : this->source.materials) {
                    this->RequireUniqueName(this->materialNames, "material", material.name);
                    this->materialNames.insert(material.name);
                }
                for (const scene::SceneMedium& medium : this->source.media) {
                    this->RequireUniqueName(this->mediumNames, "medium", medium.name);
                    this->mediumNames.insert(medium.name);
                }
                for (const scene::SceneObjectDefinition& definition : this->source.objectDefinitions) {
                    this->RequireUniqueName(this->objectDefinitionNames, "object definition", definition.name);
                    this->objectDefinitionNames.insert(definition.name);
                }
            }

            [[nodiscard]] Transform RenderFromWorldTransform() const {
                this->RequireRenderSettings();
                return this->cameraEntity.cameraTransform.RenderFromWorld();
            }

            [[nodiscard]] Transform SceneTransform(const scene::SceneTransformSet& transform, const scene::SceneSourceLocation& source) const {
                if (transform.animated) throw std::runtime_error(std::format("{}: animated transform reached GPU scene compiler after validation.", SourceString(source)));
                return ToPathtracerTransform(transform.start);
            }

            [[nodiscard]] Transform RenderFromObjectTransform(const scene::SceneTransformSet& transform, const scene::SceneSourceLocation& source) const {
                return this->RenderFromWorldTransform() * this->SceneTransform(transform, source);
            }

            [[nodiscard]] PathtracerShapeSceneEntity MakeShapeEntity(const scene::SceneShape& shape) const {
                const std::string materialName = shape.materialName;
                this->RequireMaterial(materialName);
                Transform renderFromObject = this->RenderFromObjectTransform(shape.transform, shape.entity.source);
                Allocator allocator        = this->compiled.threadAllocators.Get();
                PathtracerSceneEntity base = this->MakeEntity(shape.entity);
                PathtracerShapeSceneEntity entity{
                    .name               = std::move(base.name),
                    .loc                = base.loc,
                    .parameters         = std::move(base.parameters),
                    .renderFromObject   = allocator.new_object<Transform>(renderFromObject),
                    .objectFromRender   = allocator.new_object<Transform>(Inverse(renderFromObject)),
                    .reverseOrientation = shape.reverseOrientation,
                    .materialName       = materialName,
                    .insideMedium       = shape.mediumInterface.inside,
                    .outsideMedium      = shape.mediumInterface.outside,
                };
                if (!shape.mediumInterface.inside.empty() || !shape.mediumInterface.outside.empty()) this->compiled.haveMedia = true;
                if (shape.areaLight.has_value()) entity.areaLight = this->MakeEntity(shape.areaLight->entity);
                return entity;
            }

            [[nodiscard]] Medium FindMedium(const std::string& name, const FileLoc* loc) const {
                if (name.empty()) return nullptr;
                std::map<std::string, Medium>::const_iterator iter = this->compiled.media.find(name);
                if (iter == this->compiled.media.end()) throw std::runtime_error(diagnostics::Format(loc, "%s: medium not defined", name));
                return iter->second;
            }

            void SetRenderSettings(const scene::SceneRenderSettings& settings) {
                if (this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings are already configured.", this->source.source));

                PathtracerSceneEntity filterEntity = this->MakeEntity(settings.filter);
                scene::SceneEntity film            = settings.film;
                if (this->filmResolutionOverride.has_value()) {
                    if (this->filmResolutionOverride->x <= 0 || this->filmResolutionOverride->y <= 0) throw std::runtime_error("Spectra interactive film resolution must be positive.");
                    this->OverrideIntegerParameter(&film, "xresolution", this->filmResolutionOverride->x);
                    this->OverrideIntegerParameter(&film, "yresolution", this->filmResolutionOverride->y);
                }
                PathtracerSceneEntity filmEntity   = this->MakeEntity(film);
                this->samplerEntity                = this->MakeEntity(settings.sampler);
                this->compiled.integrator          = this->MakeEntity(settings.integrator);
                this->compiled.accelerator         = this->MakeEntity(settings.accelerator);
                PathtracerSceneEntity cameraEntity = this->MakeEntity(settings.camera);
                const Transform worldFromCamera    = this->SceneTransform(settings.cameraTransform, settings.camera.source);
                this->cameraEntity                 = PathtracerCameraSceneEntity{
                    .name            = std::move(cameraEntity.name),
                    .loc             = cameraEntity.loc,
                    .parameters      = std::move(cameraEntity.parameters),
                    .cameraTransform = CameraTransform(AnimatedTransform(worldFromCamera, settings.cameraTransform.startTime, worldFromCamera, settings.cameraTransform.endTime), this->renderConfig.rendering_space),
                    .medium          = settings.cameraMedium,
                };

                this->compiled.filmColorSpace = filmEntity.parameters.ColorSpace();
                this->renderSettingsReady     = true;

                Allocator alloc       = this->compiled.threadAllocators.Get();
                this->compiled.filter = Filter::Create(filterEntity.name, filterEntity.parameters, &filterEntity.loc, alloc);

                Float exposureTime = this->cameraEntity.parameters.GetOneFloat("shutterclose", 1.0f) - this->cameraEntity.parameters.GetOneFloat("shutteropen", 0.0f);
                if (exposureTime <= 0.0f) throw std::runtime_error(diagnostics::Format(&this->cameraEntity.loc, "The specified camera shutter times imply that the shutter does not open. A black image will result."));

                this->compiled.film = Film::Create(filmEntity.name, filmEntity.parameters, exposureTime, this->cameraEntity.cameraTransform, this->compiled.filter, this->renderConfig, &filmEntity.loc, alloc);
            }

            [[nodiscard]] Sampler CreateSampler() const {
                Allocator alloc = this->compiled.threadAllocators.Get();
                Point2i res     = this->compiled.film.FullResolution();
                return Sampler::Create(this->samplerEntity.name, this->samplerEntity.parameters, res, this->renderConfig, &this->samplerEntity.loc, alloc);
            }

            [[nodiscard]] Camera CreateCamera() const {
                Allocator alloc     = this->compiled.threadAllocators.Get();
                Medium cameraMedium = this->FindMedium(this->cameraEntity.medium, &this->cameraEntity.loc);
                return Camera::Create(this->cameraEntity.name, this->cameraEntity.parameters, cameraMedium, this->cameraEntity.cameraTransform, this->compiled.film, &this->cameraEntity.loc, alloc);
            }

            void AddMaterial(const scene::SceneMaterial& material) {
                PathtracerSceneEntity entity = this->MakeEntity(material.entity);
                this->ConsumeMatchingTypeParameter(entity, "material");
                std::lock_guard<std::mutex> lock(this->materialMutex);
                this->StartLoadingNormalMaps(entity.parameters);
                this->materials.push_back(std::make_pair(material.name, std::move(entity)));
            }

            void AddTexture(const scene::SceneTexture& texture) {
                this->RequireRenderSettings();
                if (texture.kind == "float")
                    this->RequireUniqueName(this->floatTextureNames, "float texture", texture.name);
                else
                    this->RequireUniqueName(this->spectrumTextureNames, "spectrum texture", texture.name);

                PathtracerSceneEntity base = this->MakeEntity(texture.entity);
                PathtracerTransformedSceneEntity entity{
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
                        return FloatTexture::Create(entity.name, renderFromTexture, textureParameters, &entity.loc, alloc);
                    };
                    this->floatTextureJobs[texture.name] = RunAsync(create);
                } else {
                    this->asyncSpectrumTextures.push_back(std::make_pair(texture.name, entity));
                    auto create = [entity, this]() {
                        Allocator alloc             = this->compiled.threadAllocators.Get();
                        Transform renderFromTexture = entity.renderFromObject;
                        TextureParameterDictionary textureParameters(&entity.parameters, nullptr);
                        return SpectrumTexture::Create(entity.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &entity.loc, alloc);
                    };
                    this->spectrumTextureJobs[texture.name] = RunAsync(create);
                }
            }

            void AddMedium(const scene::SceneMedium& medium) {
                this->RequireRenderSettings();

                PathtracerSceneEntity base = this->MakeEntity(medium.entity);
                this->RequireMatchingTypeParameter(base, "medium");
                PathtracerTransformedSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(medium.transform, medium.entity.source),
                };

                auto create = [entity, this]() { return Medium::Create(entity.name, entity.parameters, entity.renderFromObject, &entity.loc, this->compiled.threadAllocators.Get()); };

                std::lock_guard<std::mutex> lock(this->mediaMutex);
                this->mediumJobs[medium.name] = RunAsync(create);
            }

            void AddLight(const scene::SceneLight& light) {
                this->RequireRenderSettings();

                PathtracerSceneEntity base = this->MakeEntity(light.entity);
                PathtracerLightSceneEntity entity{
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

            void AddShape(const scene::SceneShape& shape) {
                this->RequireRenderSettings();
                this->compiled.shapes.push_back(this->MakeShapeEntity(shape));
            }

            void AddObjectDefinition(const scene::SceneObjectDefinition& definition) {
                this->RequireRenderSettings();
                PathtracerInstanceDefinitionSceneEntity entity{
                    .name = definition.name,
                    .loc  = ToFileLoc(definition.source, this->source.source),
                };
                entity.shapes.reserve(definition.shapes.size());
                for (const scene::SceneShape& shape : definition.shapes) {
                    PathtracerShapeSceneEntity shapeEntity = this->MakeShapeEntity(shape);
                    if (shapeEntity.areaLight.has_value()) throw std::runtime_error(std::format("{} scene object definition \"{}\" contains an area light shape; instanced area lights are not supported.", this->source.source, definition.name));
                    entity.shapes.push_back(std::move(shapeEntity));
                }

                this->compiled.instanceDefinitions[definition.name] = std::move(entity);
            }

            void AddObjectInstance(const scene::SceneObjectInstance& instance) {
                this->RequireRenderSettings();
                if (this->objectDefinitionNames.find(instance.definitionName) == this->objectDefinitionNames.end()) throw std::runtime_error(std::format("{} scene references unknown object definition \"{}\".", this->source.source, instance.definitionName));

                Transform worldFromRender = Inverse(this->RenderFromWorldTransform());
                this->compiled.instances.push_back({
                    .name               = instance.definitionName,
                    .loc                = ToFileLoc(instance.source, this->source.source),
                    .renderFromInstance = this->RenderFromObjectTransform(instance.transform, instance.source) * worldFromRender,
                });
            }

            [[nodiscard]] std::map<std::string, Medium> CreateMedia() {
                std::map<std::string, Medium> mediaMap;
                this->mediaMutex.lock();
                for (std::pair<const std::string, AsyncJob<Medium>*>& mediumJob : this->mediumJobs) {
                    while (mediaMap.find(mediumJob.first) == mediaMap.end()) {
                        pstd::optional<Medium> medium = mediumJob.second->TryGetResult(&this->mediaMutex);
                        if (medium) mediaMap[mediumJob.first] = *medium;
                    }
                }
                this->mediumJobs.clear();
                this->mediaMutex.unlock();
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
                for (std::pair<const std::string, AsyncJob<Image*>*>& job : this->normalMapJobs) {
                    SPECTRA_CHECK(this->normalMaps.find(job.first) == this->normalMaps.end());
                    this->normalMaps[job.first] = job.second->GetResult();
                }
                this->normalMapJobs.clear();

                for (const std::pair<std::string, PathtracerSceneEntity>& material : this->materials) {
                    const std::string& name             = material.first;
                    const PathtracerSceneEntity& entity = material.second;
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
                for (std::pair<const std::string, AsyncJob<FloatTexture>*>& texture : this->floatTextureJobs) textures.floatTextures[texture.first] = texture.second->GetResult();
                this->floatTextureJobs.clear();
                for (std::pair<const std::string, AsyncJob<SpectrumTexture>*>& texture : this->spectrumTextureJobs) textures.albedoSpectrumTextures[texture.first] = texture.second->GetResult();
                this->spectrumTextureJobs.clear();
                this->textureMutex.unlock();

                Allocator alloc = this->compiled.threadAllocators.Get();
                for (const std::pair<std::string, PathtracerTransformedSceneEntity>& texture : this->asyncSpectrumTextures) {
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, nullptr);
                    SpectrumTexture unboundedTexture                   = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, alloc);
                    SpectrumTexture illuminantTexture                  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, alloc);
                    textures.unboundedSpectrumTextures[texture.first]  = unboundedTexture;
                    textures.illuminantSpectrumTextures[texture.first] = illuminantTexture;
                }

                for (const std::pair<std::string, PathtracerTransformedSceneEntity>& texture : this->serialFloatTextures) {
                    Allocator alloc             = this->compiled.threadAllocators.Get();
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
                    textures.floatTextures[texture.first] = FloatTexture::Create(texture.second.name, renderFromTexture, textureParameters, &texture.second.loc, alloc);
                }

                for (const std::pair<std::string, PathtracerTransformedSceneEntity>& texture : this->serialSpectrumTextures) {
                    Allocator alloc             = this->compiled.threadAllocators.Get();
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
                    textures.albedoSpectrumTextures[texture.first]     = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &texture.second.loc, alloc);
                    textures.unboundedSpectrumTextures[texture.first]  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, alloc);
                    textures.illuminantSpectrumTextures[texture.first] = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, alloc);
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
                    const PathtracerShapeSceneEntity& shape = this->compiled.shapes[index];
                    if (!shape.areaLight.has_value()) continue;

                    std::map<std::string, Material>::const_iterator materialIter = this->compiled.materials.find(shape.materialName);
                    if (materialIter == this->compiled.materials.end()) throw std::runtime_error(diagnostics::Format(&shape.loc, "%s: no named material defined.", shape.materialName));

                    if (!materialIter->second) throw std::runtime_error(diagnostics::Format(&shape.loc, "Area light shape \"%s\" cannot use an interface material.", shape.name));

                    pstd::vector<Shape> shapeObjects = Shape::Create(shape.name, shape.renderFromObject, shape.objectFromRender, shape.reverseOrientation, shape.parameters, this->compiled.textures.floatTextures, this->renderConfig, &shape.loc, this->compiled.meshBufferCache, alloc);
                    FloatTexture alphaTexture        = getAlphaTexture(shape.parameters, &shape.loc);
                    MediumInterface mediumInterface(this->FindMedium(shape.insideMedium, &shape.loc), this->FindMedium(shape.outsideMedium, &shape.loc));
                    pstd::vector<Light>* shapeLights = new pstd::vector<Light>(alloc);
                    for (Shape shapeObject : shapeObjects) {
                        Light areaLight = Light::CreateArea(shape.areaLight->name, shape.areaLight->parameters, *shape.renderFromObject, mediumInterface, shapeObject, alphaTexture, &shape.areaLight->loc, this->compiled.lightSpectrumCache, alloc);
                        if (areaLight) {
                            lights.push_back(areaLight);
                            shapeLights->push_back(areaLight);
                        }
                    }
                    this->compiled.shapeIndexToAreaLights[static_cast<int>(index)] = shapeLights;
                }

                std::lock_guard<std::mutex> lock(this->lightMutex);
                for (AsyncJob<Light>* job : this->lightJobs) lights.push_back(job->GetResult());
                return lights;
            }

            const scene::SceneSnapshot& source;
            CompiledPathtracerScene& compiled;
            RenderConfig renderConfig;
            std::optional<Point2i> filmResolutionOverride{};
            FileLoc location{};
            bool renderSettingsReady{false};
            PathtracerSceneEntity samplerEntity{};
            PathtracerCameraSceneEntity cameraEntity{};
            std::mutex mediaMutex;
            std::map<std::string, AsyncJob<Medium>*> mediumJobs{};
            std::mutex materialMutex;
            std::map<std::string, AsyncJob<Image*>*> normalMapJobs{};
            std::map<std::string, Image*> normalMaps{};
            std::vector<std::pair<std::string, PathtracerSceneEntity>> materials{};
            std::mutex lightMutex;
            std::vector<AsyncJob<Light>*> lightJobs{};
            std::mutex textureMutex;
            std::vector<std::pair<std::string, PathtracerTransformedSceneEntity>> serialFloatTextures{};
            std::vector<std::pair<std::string, PathtracerTransformedSceneEntity>> serialSpectrumTextures{};
            std::vector<std::pair<std::string, PathtracerTransformedSceneEntity>> asyncSpectrumTextures{};
            std::set<std::string> loadingTextureFilenames{};
            std::map<std::string, AsyncJob<FloatTexture>*> floatTextureJobs{};
            std::map<std::string, AsyncJob<SpectrumTexture>*> spectrumTextureJobs{};
            std::set<std::string> materialNames{};
            std::set<std::string> mediumNames{};
            std::set<std::string> floatTextureNames{};
            std::set<std::string> spectrumTextureNames{};
            std::set<std::string> objectDefinitionNames{};
        };
    } // namespace

    scene::SceneTranslationReport AnalyzePathtracerSceneSupport(const scene::SceneSnapshot& scene) {
        static const std::set<std::string> supportedFilters{"box", "gaussian", "mitchell", "sinc", "triangle"};
        static const std::set<std::string> supportedFilms{"rgb", "gbuffer", "spectral"};
        static const std::set<std::string> supportedCameras{"perspective", "orthographic", "realistic", "spherical"};
        static const std::set<std::string> supportedSamplers{"zsobol", "paddedsobol", "halton", "sobol", "pmj02bn", "independent", "stratified"};
        static const std::set<std::string> supportedIntegrators{"path", "volpath"};
        static const std::set<std::string> supportedAccelerators{"bvh"};
        static const std::set<std::string> supportedMaterials{"none", "interface", "diffuse", "coateddiffuse", "coatedconductor", "diffusetransmission", "dielectric", "thindielectric", "hair", "conductor", "measured", "subsurface", "mix"};
        static const std::set<std::string> supportedTextures{"constant", "scale", "mix", "directionmix", "bilerp", "imagemap", "checkerboard", "dots", "fbm", "wrinkled", "windy", "marble", "ptex"};
        static const std::set<std::string> supportedMedia{"homogeneous", "uniformgrid", "rgbgrid", "cloud", "nanovdb"};
        static const std::set<std::string> supportedLights{"point", "spot", "goniometric", "projection", "distant", "infinite"};
        static const std::set<std::string> supportedAreaLights{"diffuse"};
        static const std::set<std::string> supportedShapes{"sphere", "cylinder", "disk", "bilinearmesh", "curve", "trianglemesh", "plymesh", "loopsubdiv"};
        static const std::set<std::string> supportedLightSamplers{"uniform", "power", "bvh", "exhaustive"};

        scene::SceneTranslationReport report{.target = "Spectra Pathtracer"};
        ValidateEntityType(&report, scene.renderSettings.filter, supportedFilters, "pixel filter");
        ValidateEntityType(&report, scene.renderSettings.film, supportedFilms, "film");
        ValidateEntityType(&report, scene.renderSettings.camera, supportedCameras, "camera");
        ValidateEntityType(&report, scene.renderSettings.sampler, supportedSamplers, "sampler");
        ValidateEntityType(&report, scene.renderSettings.integrator, supportedIntegrators, "integrator");
        ValidateEntityType(&report, scene.renderSettings.accelerator, supportedAccelerators, "accelerator");
        ValidateTransform(&report, scene.renderSettings.cameraTransform, scene.renderSettings.camera.source, "camera");

        for (const scene::SceneOption& option : scene.renderSettings.options) AddDiagnostic(&report, option.source, std::format("PBRT Option \"{}\" is represented by the scene document but is not wired into the current pathtracer runtime", option.name));

        const std::string lightSampler = OneStringParameter(scene.renderSettings.integrator.parameters, "lightsampler", "");
        if (!lightSampler.empty() && !ContainsName(supportedLightSamplers, lightSampler)) AddDiagnostic(&report, scene.renderSettings.integrator.source, std::format("GPU pathtracer does not support light sampler \"{}\"", lightSampler));

        std::set<std::string> materials{};
        for (const scene::SceneMaterial& material : scene.materials) {
            materials.insert(material.name);
            ValidateEntityType(&report, material.entity, supportedMaterials, "material");
        }

        std::set<std::string> media{};
        for (const scene::SceneMedium& medium : scene.media) {
            media.insert(medium.name);
            ValidateEntityType(&report, medium.entity, supportedMedia, "medium");
            ValidateTransform(&report, medium.transform, medium.entity.source, std::format("medium \"{}\"", medium.name));
        }

        for (const scene::SceneTexture& texture : scene.textures) {
            if (texture.kind != "float" && texture.kind != "spectrum") AddDiagnostic(&report, texture.entity.source, std::format("GPU pathtracer does not support texture value kind \"{}\"", texture.kind));
            ValidateEntityType(&report, texture.entity, supportedTextures, "texture");
            if (texture.kind == "float" && texture.entity.type == "marble") AddDiagnostic(&report, texture.entity.source, "\"marble\" is only a spectrum texture in the GPU pathtracer");
            if (texture.kind == "spectrum" && (texture.entity.type == "fbm" || texture.entity.type == "wrinkled" || texture.entity.type == "windy")) AddDiagnostic(&report, texture.entity.source, std::format("\"{}\" is only a float texture in the GPU pathtracer", texture.entity.type));
            ValidateTransform(&report, texture.transform, texture.entity.source, std::format("texture \"{}\"", texture.name));
        }

        const auto validateShape = [&report, &materials, &media](const scene::SceneShape& shape, const std::string_view owner) {
            ValidateEntityType(&report, shape.entity, supportedShapes, "shape");
            ValidateTransform(&report, shape.transform, shape.entity.source, owner);
            if (shape.materialName.empty() || !ContainsName(materials, shape.materialName)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown material \"{}\"", owner, shape.materialName));
            if (!shape.mediumInterface.inside.empty() && !ContainsName(media, shape.mediumInterface.inside)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown inside medium \"{}\"", owner, shape.mediumInterface.inside));
            if (!shape.mediumInterface.outside.empty() && !ContainsName(media, shape.mediumInterface.outside)) AddDiagnostic(&report, shape.entity.source, std::format("{} references unknown outside medium \"{}\"", owner, shape.mediumInterface.outside));
            if (shape.areaLight.has_value()) ValidateEntityType(&report, shape.areaLight->entity, supportedAreaLights, "area light");
        };

        for (const scene::SceneLight& light : scene.lights) {
            ValidateEntityType(&report, light.entity, supportedLights, "light");
            ValidateTransform(&report, light.transform, light.entity.source, std::format("light \"{}\"", light.name));
            if (!light.medium.empty() && !ContainsName(media, light.medium)) AddDiagnostic(&report, light.entity.source, std::format("light \"{}\" references unknown medium \"{}\"", light.name, light.medium));
        }

        for (const scene::SceneShape& shape : scene.shapes) validateShape(shape, std::format("shape \"{}\"", shape.name));

        std::set<std::string> objectDefinitions{};
        for (const scene::SceneObjectDefinition& definition : scene.objectDefinitions) {
            objectDefinitions.insert(definition.name);
            for (const scene::SceneShape& shape : definition.shapes) {
                validateShape(shape, std::format("object definition \"{}\" shape", definition.name));
                if (shape.areaLight.has_value()) AddDiagnostic(&report, shape.entity.source, std::format("object definition \"{}\" contains an area light shape; instanced area lights are not supported by the current GPU pathtracer", definition.name));
            }
        }

        for (const scene::SceneObjectInstance& instance : scene.objectInstances) {
            if (!ContainsName(objectDefinitions, instance.definitionName)) AddDiagnostic(&report, instance.source, std::format("object instance references unknown definition \"{}\"", instance.definitionName));
            ValidateTransform(&report, instance.transform, instance.source, std::format("object instance \"{}\"", instance.name));
        }

        return report;
    }

    CompiledPathtracerScene::CompiledPathtracerScene(pstd::pmr::memory_resource* memoryResource) : allLights(Allocator(RequireMemoryResource(memoryResource))), lightSpectrumCache(Allocator(RequireMemoryResource(memoryResource))), threadAllocators([memoryResource]() { return Allocator(RequireMemoryResource(memoryResource)); }) {}

    std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const scene::SceneSnapshot& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride) {
        std::unique_ptr<CompiledPathtracerScene> compiled = std::make_unique<CompiledPathtracerScene>(memoryResource);
        PathtracerSceneCompiler compiler(scene, *compiled, config, filmResolutionOverride);
        compiler.Compile();
        return compiled;
    }
} // namespace spectra::pathtracer
