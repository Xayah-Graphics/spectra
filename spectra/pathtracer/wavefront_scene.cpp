#include <cstddef>
#include <cstdint>
#include <cuda_runtime_api.h>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <spectra/pathtracer/base/material.cuh>
#include <spectra/pathtracer/base/shape.cuh>
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
#include <spectra/pathtracer/util/file.cuh>
#include <spectra/pathtracer/util/image.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/mesh.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/transform.cuh>
#include <spectra/pathtracer/wavefront_scene.cuh>
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

        [[nodiscard]] Transform ToPathtracerTransform(const scene::Transform& transform) {
            return Transform(ToPathtracerMatrix(transform.matrix), ToPathtracerMatrix(transform.inverse));
        }

        [[nodiscard]] const RGBColorSpace* ToPathtracerColorSpace(scene::ColorSpace colorSpace) {
            switch (colorSpace) {
            case scene::ColorSpace::sRGB: return RGBColorSpace::sRGB;
            case scene::ColorSpace::DCI_P3: return RGBColorSpace::DCI_P3;
            case scene::ColorSpace::Rec2020: return RGBColorSpace::Rec2020;
            case scene::ColorSpace::ACES2065_1: return RGBColorSpace::ACES2065_1;
            }
            throw std::runtime_error("Unknown Spectra scene color space.");
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
            if (memoryResource == nullptr) throw std::runtime_error("Wavefront scene requires a memory resource.");
            return memoryResource;
        }

        class WavefrontSceneCompiler {
        public:
            WavefrontSceneCompiler(const scene::Scene& sourceScene, WavefrontScene& compiledScene, const RenderConfig& config, std::optional<Point2i> resolutionOverride) : source(sourceScene), compiled(compiledScene), renderConfig(config), filmResolutionOverride(resolutionOverride), location(sourceScene.source) {}

            void Compile() {
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
            [[nodiscard]] ParameterDictionary MakeParameterDictionary(const scene::SceneParameters& parameters) const {
                const RGBColorSpace* colorSpace = ToPathtracerColorSpace(parameters.colorSpace);

                ParsedParameterVector parsedParameters{};
                for (const scene::SceneParameter& parameter : parameters.values) {
                    if (parameter.type.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty type.", this->source.source));
                    if (parameter.name.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty name.", this->source.source));
                    ParsedParameter* parsedParameter = new ParsedParameter(this->location);
                    parsedParameter->type            = parameter.type;
                    parsedParameter->name            = parameter.name;
                    parsedParameter->mayBeUnused     = parameter.mayBeUnused;
                    parsedParameter->colorSpace      = colorSpace;
                    AppendParameterValues(parsedParameter, parameter);
                    parsedParameters.push_back(parsedParameter);
                }

                return ParameterDictionary(std::move(parsedParameters), colorSpace);
            }

            [[nodiscard]] WavefrontSceneEntity MakeEntity(const std::string& type, const scene::SceneParameters& parameters) const {
                if (type.empty()) throw std::runtime_error(std::format("{} scene entity has an empty type.", this->source.source));
                return WavefrontSceneEntity{
                    .name       = type,
                    .loc        = this->location,
                    .parameters = this->MakeParameterDictionary(parameters),
                };
            }

            void ApplyFilmResolutionOverride(scene::SceneParameters* parameters) const {
                if (!this->filmResolutionOverride.has_value()) return;
                if (this->filmResolutionOverride->x <= 0 || this->filmResolutionOverride->y <= 0) throw std::runtime_error("Spectra interactive film resolution must be positive.");

                for (std::size_t index = 0; index < parameters->values.size();) {
                    const std::string& name = parameters->values[index].name;
                    if (name == "xresolution" || name == "yresolution")
                        parameters->values.erase(parameters->values.begin() + static_cast<std::ptrdiff_t>(index));
                    else
                        ++index;
                }

                parameters->values.push_back({
                    .type   = "integer",
                    .name   = "xresolution",
                    .values = std::vector<int>{this->filmResolutionOverride->x},
                });
                parameters->values.push_back({
                    .type   = "integer",
                    .name   = "yresolution",
                    .values = std::vector<int>{this->filmResolutionOverride->y},
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

            [[nodiscard]] Transform RenderFromWorldTransform() const {
                this->RequireRenderSettings();
                return this->cameraEntity.cameraTransform.RenderFromWorld();
            }

            [[nodiscard]] Transform RenderFromObjectTransform(const scene::Transform& worldFromObject) const {
                return this->RenderFromWorldTransform() * ToPathtracerTransform(worldFromObject);
            }

            [[nodiscard]] WavefrontShapeSceneEntity MakeShapeEntity(const scene::SceneShape& shape) const {
                this->RequireMaterial(shape.material);
                Transform renderFromObject = this->RenderFromObjectTransform(shape.worldFromObject);
                Allocator allocator        = this->compiled.threadAllocators.Get();
                WavefrontSceneEntity base  = this->MakeEntity(shape.type, shape.parameters);
                WavefrontShapeSceneEntity entity{
                    .name               = std::move(base.name),
                    .loc                = base.loc,
                    .parameters         = std::move(base.parameters),
                    .renderFromObject   = allocator.new_object<Transform>(renderFromObject),
                    .objectFromRender   = allocator.new_object<Transform>(Inverse(renderFromObject)),
                    .reverseOrientation = shape.reverseOrientation,
                    .materialName       = shape.material,
                    .insideMedium       = shape.insideMedium,
                    .outsideMedium      = shape.outsideMedium,
                };
                if (!shape.insideMedium.empty() || !shape.outsideMedium.empty()) this->compiled.haveMedia = true;
                if (shape.areaLight.has_value()) entity.areaLight = this->MakeEntity(shape.areaLight->type, shape.areaLight->parameters);
                return entity;
            }

            [[nodiscard]] Medium FindMedium(const std::string& name, const FileLoc* loc) const {
                if (name.empty()) return nullptr;
                std::map<std::string, Medium>::const_iterator iter = this->compiled.media.find(name);
                if (iter == this->compiled.media.end()) throw std::runtime_error(diagnostics::Format(loc, "%s: medium not defined", name));
                return iter->second;
            }

            void SetRenderSettings(scene::SceneRenderSettings settings) {
                if (this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings are already configured.", this->source.source));
                if (settings.camera.fovDegrees <= 0.0f) throw std::runtime_error(std::format("{} scene camera fov must be positive.", this->source.source));
                this->ApplyFilmResolutionOverride(&settings.film.parameters);

                WavefrontSceneEntity filterEntity = this->MakeEntity(settings.filter.type, settings.filter.parameters);
                WavefrontSceneEntity filmEntity   = this->MakeEntity(settings.film.type, settings.film.parameters);
                this->samplerEntity               = this->MakeEntity(settings.sampler.type, settings.sampler.parameters);
                this->compiled.integrator         = this->MakeEntity(settings.integrator.type, settings.integrator.parameters);
                this->compiled.accelerator        = this->MakeEntity(settings.accelerator.type, settings.accelerator.parameters);
                WavefrontSceneEntity cameraEntity = this->MakeEntity(settings.camera.type, settings.camera.parameters);
                const Transform worldFromCamera   = ToPathtracerTransform(settings.camera.worldFromCamera);
                this->cameraEntity                = WavefrontCameraSceneEntity{
                    .name            = std::move(cameraEntity.name),
                    .loc             = cameraEntity.loc,
                    .parameters      = std::move(cameraEntity.parameters),
                    .cameraTransform = CameraTransform(AnimatedTransform(worldFromCamera, 0.0f, worldFromCamera, 1.0f), this->renderConfig.rendering_space),
                    .medium          = settings.camera.medium,
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
                this->RequireUniqueName(this->materialNames, "material", material.name);
                WavefrontSceneEntity entity = this->MakeEntity(material.type, material.parameters);
                std::lock_guard<std::mutex> lock(this->materialMutex);
                this->StartLoadingNormalMaps(entity.parameters);
                this->materials.push_back(std::make_pair(material.name, std::move(entity)));
                this->materialNames.insert(material.name);
            }

            void AddTexture(const scene::SceneTexture& texture) {
                this->RequireRenderSettings();
                if (texture.kind == scene::TextureKind::Float)
                    this->RequireUniqueName(this->floatTextureNames, "float texture", texture.name);
                else
                    this->RequireUniqueName(this->spectrumTextureNames, "spectrum texture", texture.name);

                WavefrontSceneEntity base = this->MakeEntity(texture.type, texture.parameters);
                WavefrontTransformedSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(texture.worldFromTexture),
                };

                std::lock_guard<std::mutex> lock(this->textureMutex);
                if (texture.kind == scene::TextureKind::Float) {
                    this->floatTextureNames.insert(texture.name);
                    if (!IsImageTexture(texture.type)) {
                        this->serialFloatTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                } else {
                    this->spectrumTextureNames.insert(texture.name);
                    if (!IsImageTexture(texture.type)) {
                        this->serialSpectrumTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                }

                std::string filename = ResolveFilename(entity.parameters.GetOneString("filename", ""));
                if (filename.empty()) throw std::runtime_error(diagnostics::Format(&entity.loc, "\"string filename\" not provided for image texture."));
                if (!FileExists(filename)) throw std::runtime_error(diagnostics::Format(&entity.loc, "%s: file not found.", filename));

                if (this->loadingTextureFilenames.find(filename) != this->loadingTextureFilenames.end()) {
                    if (texture.kind == scene::TextureKind::Float)
                        this->serialFloatTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                    else
                        this->serialSpectrumTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                    return;
                }

                this->loadingTextureFilenames.insert(filename);
                if (texture.kind == scene::TextureKind::Float) {
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
                this->RequireUniqueName(this->mediumNames, "medium", medium.name);

                WavefrontSceneEntity base = this->MakeEntity(medium.type, medium.parameters);
                WavefrontTransformedSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(medium.worldFromMedium),
                };

                auto create = [entity, this]() { return Medium::Create(entity.name, entity.parameters, entity.renderFromObject, &entity.loc, this->compiled.threadAllocators.Get()); };

                std::lock_guard<std::mutex> lock(this->mediaMutex);
                this->mediumJobs[medium.name] = RunAsync(create);
                this->mediumNames.insert(medium.name);
            }

            void AddLight(const scene::SceneLight& light) {
                this->RequireRenderSettings();

                WavefrontSceneEntity base = this->MakeEntity(light.type, light.parameters);
                WavefrontLightSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(light.worldFromLight),
                    .medium           = light.medium,
                };

                Medium lightMedium = this->FindMedium(entity.medium, &entity.loc);
                auto create        = [this, entity, lightMedium]() { return Light::Create(entity.name, entity.parameters, entity.renderFromObject, this->compiled.camera.GetCameraTransform(), lightMedium, &entity.loc, this->compiled.threadAllocators.Get()); };

                std::lock_guard<std::mutex> lock(this->lightMutex);
                this->lightJobs.push_back(RunAsync(create));
            }

            void AddShape(const scene::SceneShape& shape) {
                this->RequireRenderSettings();
                this->compiled.shapes.push_back(this->MakeShapeEntity(shape));
            }

            void AddObjectDefinition(const scene::SceneObjectDefinition& definition) {
                this->RequireRenderSettings();
                this->RequireUniqueName(this->objectDefinitionNames, "object definition", definition.name);
                WavefrontInstanceDefinitionSceneEntity entity{
                    .name = definition.name,
                    .loc  = this->location,
                };
                entity.shapes.reserve(definition.shapes.size());
                for (const scene::SceneShape& shape : definition.shapes) {
                    WavefrontShapeSceneEntity shapeEntity = this->MakeShapeEntity(shape);
                    if (shapeEntity.areaLight.has_value()) throw std::runtime_error(std::format("{} scene object definition \"{}\" contains an area light shape; instanced area lights are not supported.", this->source.source, definition.name));
                    entity.shapes.push_back(std::move(shapeEntity));
                }

                this->compiled.instanceDefinitions[definition.name] = std::move(entity);
                this->objectDefinitionNames.insert(definition.name);
            }

            void AddObjectInstance(const scene::SceneObjectInstance& instance) {
                this->RequireRenderSettings();
                if (this->objectDefinitionNames.find(instance.name) == this->objectDefinitionNames.end()) throw std::runtime_error(std::format("{} scene references unknown object definition \"{}\".", this->source.source, instance.name));

                Transform worldFromRender = Inverse(this->RenderFromWorldTransform());
                this->compiled.instances.push_back({
                    .name               = instance.name,
                    .loc                = this->location,
                    .renderFromInstance = this->RenderFromObjectTransform(instance.worldFromInstance) * worldFromRender,
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
                std::string filename = ResolveFilename(parameters.GetOneString("normalmap", ""));
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

                for (const std::pair<std::string, WavefrontSceneEntity>& material : this->materials) {
                    const std::string& name            = material.first;
                    const WavefrontSceneEntity& entity = material.second;
                    Allocator alloc                    = this->compiled.threadAllocators.Get();
                    std::string normalMapName          = ResolveFilename(entity.parameters.GetOneString("normalmap", ""));
                    Image* normalMap                   = nullptr;
                    if (!normalMapName.empty()) {
                        SPECTRA_CHECK(this->normalMaps.find(normalMapName) != this->normalMaps.end());
                        normalMap = this->normalMaps[normalMapName];
                    }

                    TextureParameterDictionary textureParameters(&entity.parameters, &this->compiled.textures);
                    Material createdMaterial       = Material::Create(entity.name, textureParameters, normalMap, this->compiled.materials, &entity.loc, alloc);
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
                for (const std::pair<std::string, WavefrontTransformedSceneEntity>& texture : this->asyncSpectrumTextures) {
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, nullptr);
                    SpectrumTexture unboundedTexture                   = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, alloc);
                    SpectrumTexture illuminantTexture                  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, alloc);
                    textures.unboundedSpectrumTextures[texture.first]  = unboundedTexture;
                    textures.illuminantSpectrumTextures[texture.first] = illuminantTexture;
                }

                for (const std::pair<std::string, WavefrontTransformedSceneEntity>& texture : this->serialFloatTextures) {
                    Allocator alloc             = this->compiled.threadAllocators.Get();
                    Transform renderFromTexture = texture.second.renderFromObject;
                    TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
                    textures.floatTextures[texture.first] = FloatTexture::Create(texture.second.name, renderFromTexture, textureParameters, &texture.second.loc, alloc);
                }

                for (const std::pair<std::string, WavefrontTransformedSceneEntity>& texture : this->serialSpectrumTextures) {
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
                    const WavefrontShapeSceneEntity& shape = this->compiled.shapes[index];
                    if (!shape.areaLight.has_value()) continue;

                    std::map<std::string, Material>::const_iterator materialIter = this->compiled.materials.find(shape.materialName);
                    if (materialIter == this->compiled.materials.end()) throw std::runtime_error(diagnostics::Format(&shape.loc, "%s: no named material defined.", shape.materialName));

                    if (!materialIter->second) throw std::runtime_error(diagnostics::Format(&shape.loc, "Area light shape \"%s\" cannot use an interface material.", shape.name));

                    pstd::vector<Shape> shapeObjects = Shape::Create(shape.name, shape.renderFromObject, shape.objectFromRender, shape.reverseOrientation, shape.parameters, this->compiled.textures.floatTextures, this->renderConfig, &shape.loc, alloc);
                    FloatTexture alphaTexture        = getAlphaTexture(shape.parameters, &shape.loc);
                    MediumInterface mediumInterface(this->FindMedium(shape.insideMedium, &shape.loc), this->FindMedium(shape.outsideMedium, &shape.loc));
                    pstd::vector<Light>* shapeLights = new pstd::vector<Light>(alloc);
                    for (Shape shapeObject : shapeObjects) {
                        Light areaLight = Light::CreateArea(shape.areaLight->name, shape.areaLight->parameters, *shape.renderFromObject, mediumInterface, shapeObject, alphaTexture, &shape.areaLight->loc, alloc);
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

            const scene::Scene& source;
            WavefrontScene& compiled;
            const RenderConfig& renderConfig;
            std::optional<Point2i> filmResolutionOverride{};
            FileLoc location{};
            bool renderSettingsReady{false};
            WavefrontSceneEntity samplerEntity{};
            WavefrontCameraSceneEntity cameraEntity{};
            std::mutex mediaMutex;
            std::map<std::string, AsyncJob<Medium>*> mediumJobs{};
            std::mutex materialMutex;
            std::map<std::string, AsyncJob<Image*>*> normalMapJobs{};
            std::map<std::string, Image*> normalMaps{};
            std::vector<std::pair<std::string, WavefrontSceneEntity>> materials{};
            std::mutex lightMutex;
            std::vector<AsyncJob<Light>*> lightJobs{};
            std::mutex textureMutex;
            std::vector<std::pair<std::string, WavefrontTransformedSceneEntity>> serialFloatTextures{};
            std::vector<std::pair<std::string, WavefrontTransformedSceneEntity>> serialSpectrumTextures{};
            std::vector<std::pair<std::string, WavefrontTransformedSceneEntity>> asyncSpectrumTextures{};
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

    WavefrontScene::WavefrontScene(pstd::pmr::memory_resource* memoryResource) : allLights(Allocator(RequireMemoryResource(memoryResource))), threadAllocators([memoryResource]() { return Allocator(RequireMemoryResource(memoryResource)); }) {}

    std::unique_ptr<WavefrontScene> CreateWavefrontScene(const scene::Scene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride) {
        std::unique_ptr<WavefrontScene> compiled = std::make_unique<WavefrontScene>(memoryResource);
        WavefrontSceneCompiler compiler(scene, *compiled, config, filmResolutionOverride);
        compiler.Compile();
        return compiled;
    }
} // namespace spectra::pathtracer
