#include <cstddef>
#include <format>
#include <mutex>
#include <spectra/pathtracer/base/material.h>
#include <spectra/pathtracer/base/shape.h>
#include <spectra/pathtracer/core/paramdict.h>
#include <spectra/pathtracer/gpu/memory.h>
#include <spectra/pathtracer/util/color.h>
#include <spectra/pathtracer/util/colorspace.h>
#include <spectra/pathtracer/util/file.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/mesh.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/transform.h>
#include <spectra/scene.h>
#include <spectra/scene_builtin.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace spectra {
    void ParsedParameter::AddFloat(Float v) {
        SPECTRA_CHECK(ints.empty() && strings.empty() && bools.empty());
        floats.push_back(v);
    }

    void ParsedParameter::AddInt(int i) {
        SPECTRA_CHECK(floats.empty() && strings.empty() && bools.empty());
        ints.push_back(i);
    }

    void ParsedParameter::AddString(std::string_view str) {
        SPECTRA_CHECK(floats.empty() && ints.empty() && bools.empty());
        strings.push_back({str.begin(), str.end()});
    }

    void ParsedParameter::AddBool(bool v) {
        SPECTRA_CHECK(floats.empty() && ints.empty() && strings.empty());
        bools.push_back(v);
    }
} // namespace spectra

namespace spectra::scene {
    namespace {
        void AppendParameterValues(ParsedParameter* parsedParameter, const SceneParameter& parameter) {
            for (Float value : parameter.floats) parsedParameter->AddFloat(value);
            for (int value : parameter.integers) parsedParameter->AddInt(value);
            for (std::uint8_t value : parameter.booleans) parsedParameter->AddBool(value != 0);
            for (const std::string& value : parameter.strings) parsedParameter->AddString(value);
        }

        [[nodiscard]] bool IsImageTexture(const std::string& type) {
            return type == "imagemap" || type == "ptex";
        }
    } // namespace

    Scene::Scene(std::string name, std::string title, std::string source, std::optional<Point2i> filmResolutionOverride)
        : threadAllocators([]() {
              pstd::pmr::monotonic_buffer_resource* resource = new pstd::pmr::monotonic_buffer_resource(1024 * 1024, &CUDATrackedMemoryResource::singleton);
              return Allocator(resource);
          }),
          name(std::move(name)), title(std::move(title)), source(std::move(source)), filmResolutionOverride(filmResolutionOverride) {}

    FileLoc Scene::Location() const {
        return FileLoc{this->source};
    }

    ParameterDictionary Scene::MakeParameterDictionary(const SceneParameters& parameters) const {
        if (parameters.colorSpace == nullptr) throw std::runtime_error(std::format("{} scene parameters must specify a color space.", this->source));

        ParsedParameterVector parsedParameters{};
        const FileLoc location = this->Location();
        for (const SceneParameter& parameter : parameters.values) {
            if (parameter.type.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty type.", this->source));
            if (parameter.name.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty name.", this->source));
            ParsedParameter* parsedParameter = new ParsedParameter(location);
            parsedParameter->type            = parameter.type;
            parsedParameter->name            = parameter.name;
            parsedParameter->mayBeUnused     = parameter.mayBeUnused;
            parsedParameter->colorSpace      = parameters.colorSpace;
            AppendParameterValues(parsedParameter, parameter);
            parsedParameters.push_back(parsedParameter);
        }

        return ParameterDictionary(std::move(parsedParameters), parameters.colorSpace);
    }

    SceneEntity Scene::MakeEntity(const std::string& type, const SceneParameters& parameters) const {
        if (type.empty()) throw std::runtime_error(std::format("{} scene entity has an empty type.", this->source));
        return SceneEntity{
            .name       = type,
            .loc        = this->Location(),
            .parameters = this->MakeParameterDictionary(parameters),
        };
    }

    void Scene::ApplyFilmResolutionOverride(SceneParameters* parameters) const {
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
            .type     = "integer",
            .name     = "xresolution",
            .integers = {this->filmResolutionOverride->x},
        });
        parameters->values.push_back({
            .type     = "integer",
            .name     = "yresolution",
            .integers = {this->filmResolutionOverride->y},
        });
    }

    void Scene::RequireRenderSettings() const {
        if (!this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings must be configured before adding world content.", this->source));
    }

    void Scene::RequireMaterial(const std::string& materialName) const {
        if (materialName.empty()) throw std::runtime_error(std::format("{} scene shape must specify a material.", this->source));
        if (this->materialNames.find(materialName) == this->materialNames.end()) throw std::runtime_error(std::format("{} scene references unknown material \"{}\".", this->source, materialName));
    }

    void Scene::RequireUniqueName(const std::set<std::string>& names, std::string_view kind, const std::string& name) const {
        if (name.empty()) throw std::runtime_error(std::format("{} scene {} name must not be empty.", this->source, kind));
        if (names.find(name) != names.end()) throw std::runtime_error(std::format("{} scene {} \"{}\" is already defined.", this->source, kind, name));
    }

    Transform Scene::RenderFromWorldTransform() const {
        this->RequireRenderSettings();
        return this->cameraEntity.cameraTransform.RenderFromWorld();
    }

    Transform Scene::RenderFromObjectTransform(const Transform& worldFromObject) const {
        return this->RenderFromWorldTransform() * worldFromObject;
    }

    ShapeSceneEntity Scene::MakeShapeEntity(const SceneShape& shape) const {
        this->RequireMaterial(shape.material);
        Transform renderFromObject = this->RenderFromObjectTransform(shape.worldFromObject);
        Allocator allocator        = this->threadAllocators.Get();
        ShapeSceneEntity entity{};
        static_cast<SceneEntity&>(entity) = this->MakeEntity(shape.type, shape.parameters);
        entity.renderFromObject           = allocator.new_object<Transform>(renderFromObject);
        entity.objectFromRender           = allocator.new_object<Transform>(spectra::Inverse(renderFromObject));
        entity.reverseOrientation         = shape.reverseOrientation;
        entity.materialName               = shape.material;
        entity.insideMedium               = shape.insideMedium;
        entity.outsideMedium              = shape.outsideMedium;
        if (shape.areaLight.has_value()) {
            entity.areaLight = this->MakeEntity(shape.areaLight->type, shape.areaLight->parameters);
        }
        return entity;
    }

    void Scene::SetRenderSettings(SceneRenderSettings settings) {
        if (this->renderSettingsReady) throw std::runtime_error(std::format("{} scene render settings are already configured.", this->source));
        if (settings.camera.fovDegrees <= 0.0f) throw std::runtime_error(std::format("{} scene camera fov must be positive.", this->source));
        this->ApplyFilmResolutionOverride(&settings.film.parameters);

        SceneEntity filterEntity      = this->MakeEntity(settings.filter.type, settings.filter.parameters);
        SceneEntity filmEntity        = this->MakeEntity(settings.film.type, settings.film.parameters);
        SceneEntity samplerEntity     = this->MakeEntity(settings.sampler.type, settings.sampler.parameters);
        SceneEntity integratorEntity  = this->MakeEntity(settings.integrator.type, settings.integrator.parameters);
        SceneEntity acceleratorEntity = this->MakeEntity(settings.accelerator.type, settings.accelerator.parameters);

        CameraSceneEntity cameraEntity{};
        static_cast<SceneEntity&>(cameraEntity) = this->MakeEntity(settings.camera.type, settings.camera.parameters);
        cameraEntity.cameraTransform            = CameraTransform(AnimatedTransform(settings.camera.worldFromCamera, 0.0f, settings.camera.worldFromCamera, 1.0f));
        cameraEntity.medium                     = settings.camera.medium;

        this->filmColorSpace      = filmEntity.parameters.ColorSpace();
        this->integrator          = std::move(integratorEntity);
        this->accelerator         = std::move(acceleratorEntity);
        this->cameraEntity        = cameraEntity;
        this->cameraFovDegrees    = settings.camera.fovDegrees;
        this->samplerName         = samplerEntity.name;
        this->renderSettingsReady = true;

        Allocator alloc = this->threadAllocators.Get();
        Filter filter   = Filter::Create(filterEntity.name, filterEntity.parameters, &filterEntity.loc, alloc);

        Float exposureTime = cameraEntity.parameters.GetOneFloat("shutterclose", 1.0f) - cameraEntity.parameters.GetOneFloat("shutteropen", 0.0f);
        if (exposureTime <= 0.0f) throw std::runtime_error(spectra::diagnostics::Format(&cameraEntity.loc, "The specified camera shutter times imply that the shutter does not open. A black image will result."));

        this->film = Film::Create(filmEntity.name, filmEntity.parameters, exposureTime, cameraEntity.cameraTransform, filter, &filmEntity.loc, alloc);

        this->samplerJob = RunAsync([samplerEntity, this]() {
            Allocator alloc = this->threadAllocators.Get();
            Point2i res     = this->film.FullResolution();
            return Sampler::Create(samplerEntity.name, samplerEntity.parameters, res, &samplerEntity.loc, alloc);
        });

        this->cameraJob = RunAsync([cameraEntity, this]() {
            Allocator alloc     = this->threadAllocators.Get();
            Medium cameraMedium = this->GetMedium(cameraEntity.medium, &cameraEntity.loc);
            return Camera::Create(cameraEntity.name, cameraEntity.parameters, cameraMedium, cameraEntity.cameraTransform, this->film, &cameraEntity.loc, alloc);
        });
    }

    void Scene::AddMaterial(SceneMaterial material) {
        this->RequireUniqueName(this->materialNames, "material", material.name);
        SceneEntity entity = this->MakeEntity(material.type, material.parameters);
        std::lock_guard<std::mutex> lock(this->materialMutex);
        this->StartLoadingNormalMaps(entity.parameters);
        this->materials.push_back(std::make_pair(std::move(material.name), std::move(entity)));
        this->materialNames.insert(this->materials.back().first);
    }

    void Scene::AddTexture(SceneTexture texture) {
        this->RequireRenderSettings();
        if (texture.kind == TextureKind::Float)
            this->RequireUniqueName(this->floatTextureNames, "float texture", texture.name);
        else
            this->RequireUniqueName(this->spectrumTextureNames, "spectrum texture", texture.name);

        TextureSceneEntity entity{};
        static_cast<SceneEntity&>(entity) = this->MakeEntity(texture.type, texture.parameters);
        entity.renderFromObject           = this->RenderFromObjectTransform(texture.worldFromTexture);

        std::lock_guard<std::mutex> lock(this->textureMutex);
        if (texture.kind == TextureKind::Float) {
            this->floatTextureNames.insert(texture.name);
            if (!IsImageTexture(texture.type)) {
                this->serialFloatTextures.push_back(std::make_pair(std::move(texture.name), std::move(entity)));
                return;
            }
        } else {
            this->spectrumTextureNames.insert(texture.name);
            if (!IsImageTexture(texture.type)) {
                this->serialSpectrumTextures.push_back(std::make_pair(std::move(texture.name), std::move(entity)));
                return;
            }
        }

        std::string filename = ResolveFilename(entity.parameters.GetOneString("filename", ""));
        if (filename.empty()) throw std::runtime_error(spectra::diagnostics::Format(&entity.loc, "\"string filename\" not provided for image texture."));
        if (!FileExists(filename)) throw std::runtime_error(spectra::diagnostics::Format(&entity.loc, "%s: file not found.", filename));

        if (this->loadingTextureFilenames.find(filename) != this->loadingTextureFilenames.end()) {
            if (texture.kind == TextureKind::Float)
                this->serialFloatTextures.push_back(std::make_pair(std::move(texture.name), std::move(entity)));
            else
                this->serialSpectrumTextures.push_back(std::make_pair(std::move(texture.name), std::move(entity)));
            return;
        }

        this->loadingTextureFilenames.insert(filename);
        if (texture.kind == TextureKind::Float) {
            auto create = [entity, this]() {
                Allocator alloc             = this->threadAllocators.Get();
                Transform renderFromTexture = entity.renderFromObject;
                TextureParameterDictionary textureParameters(&entity.parameters, nullptr);
                return FloatTexture::Create(entity.name, renderFromTexture, textureParameters, &entity.loc, alloc);
            };
            this->floatTextureJobs[texture.name] = RunAsync(create);
        } else {
            this->asyncSpectrumTextures.push_back(std::make_pair(texture.name, entity));
            auto create = [entity, this]() {
                Allocator alloc             = this->threadAllocators.Get();
                Transform renderFromTexture = entity.renderFromObject;
                TextureParameterDictionary textureParameters(&entity.parameters, nullptr);
                return SpectrumTexture::Create(entity.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &entity.loc, alloc);
            };
            this->spectrumTextureJobs[texture.name] = RunAsync(create);
        }
    }

    void Scene::AddMedium(SceneMedium medium) {
        this->RequireRenderSettings();
        this->RequireUniqueName(this->mediumNames, "medium", medium.name);

        MediumSceneEntity entity{};
        static_cast<SceneEntity&>(entity) = this->MakeEntity(medium.type, medium.parameters);
        entity.renderFromObject           = this->RenderFromObjectTransform(medium.worldFromMedium);

        auto create = [entity, this]() { return Medium::Create(entity.name, entity.parameters, entity.renderFromObject, &entity.loc, this->threadAllocators.Get()); };

        std::lock_guard<std::mutex> lock(this->mediaMutex);
        this->mediumJobs[medium.name] = RunAsync(create);
        this->mediumNames.insert(std::move(medium.name));
    }

    void Scene::AddLight(SceneLight light) {
        this->RequireRenderSettings();

        LightSceneEntity entity{};
        static_cast<SceneEntity&>(entity) = this->MakeEntity(light.type, light.parameters);
        entity.renderFromObject           = this->RenderFromObjectTransform(light.worldFromLight);
        entity.medium                     = light.medium;

        Medium lightMedium = this->GetMedium(entity.medium, &entity.loc);
        auto create        = [this, entity, lightMedium]() { return Light::Create(entity.name, entity.parameters, entity.renderFromObject, this->GetCamera().GetCameraTransform(), lightMedium, &entity.loc, this->threadAllocators.Get()); };

        std::lock_guard<std::mutex> lock(this->lightMutex);
        this->lightJobs.push_back(RunAsync(create));
        ++this->lightCount;
        if (entity.name == "infinite" || entity.name == "portal") ++this->infiniteLightCount;
    }

    void Scene::AddShape(SceneShape shape) {
        this->RequireRenderSettings();
        ShapeSceneEntity entity = this->MakeShapeEntity(shape);
        if (entity.areaLight.has_value()) ++this->areaLightCount;
        this->shapes.push_back(std::move(entity));
    }

    void Scene::AddObjectDefinition(SceneObjectDefinition definition) {
        this->RequireRenderSettings();
        this->RequireUniqueName(this->objectDefinitionNames, "object definition", definition.name);
        InstanceDefinitionSceneEntity entity{
            .name = definition.name,
            .loc  = this->Location(),
        };
        entity.shapes.reserve(definition.shapes.size());
        for (const SceneShape& shape : definition.shapes) {
            ShapeSceneEntity shapeEntity = this->MakeShapeEntity(shape);
            if (shapeEntity.areaLight.has_value()) throw std::runtime_error(std::format("{} scene object definition \"{}\" contains an area light shape; instanced area lights are not supported.", this->source, definition.name));
            entity.shapes.push_back(std::move(shapeEntity));
        }

        this->instanceDefinitions[definition.name] = std::move(entity);
        this->objectDefinitionNames.insert(std::move(definition.name));
    }

    void Scene::AddObjectInstance(SceneObjectInstance instance) {
        this->RequireRenderSettings();
        if (this->objectDefinitionNames.find(instance.name) == this->objectDefinitionNames.end()) throw std::runtime_error(std::format("{} scene references unknown object definition \"{}\".", this->source, instance.name));

        Transform worldFromRender = spectra::Inverse(this->RenderFromWorldTransform());
        this->instances.push_back({
            .name               = std::move(instance.name),
            .loc                = this->Location(),
            .renderFromInstance = this->RenderFromObjectTransform(instance.worldFromInstance) * worldFromRender,
        });
    }

    SceneInfo Scene::Info() const {
        std::size_t definitionShapeCount = 0;
        for (const std::pair<const std::string, InstanceDefinitionSceneEntity>& definition : this->instanceDefinitions) definitionShapeCount += definition.second.shapes.size();

        return SceneInfo{
            .name                    = this->name,
            .title                   = this->title,
            .camera                  = this->cameraEntity.name,
            .sampler                 = this->samplerName,
            .integrator              = this->integrator.name,
            .accelerator             = this->accelerator.name,
            .shape_count             = this->shapes.size() + definitionShapeCount,
            .material_count          = this->materials.size(),
            .texture_count           = this->floatTextureNames.size() + this->spectrumTextureNames.size(),
            .medium_count            = this->mediumNames.size(),
            .light_count             = this->lightCount,
            .area_light_count        = this->areaLightCount,
            .infinite_light_count    = this->infiniteLightCount,
            .object_definition_count = this->instanceDefinitions.size(),
            .object_instance_count   = this->instances.size(),
            .camera_fov_degrees      = this->cameraFovDegrees,
        };
    }

    Camera Scene::GetCamera() {
        this->cameraJobMutex.lock();
        while (!this->camera) {
            pstd::optional<Camera> result = this->cameraJob->TryGetResult(&this->cameraJobMutex);
            if (result) this->camera = *result;
        }
        this->cameraJobMutex.unlock();
        return this->camera;
    }

    Sampler Scene::GetSampler() {
        this->samplerJobMutex.lock();
        while (!this->sampler) {
            pstd::optional<Sampler> result = this->samplerJob->TryGetResult(&this->samplerJobMutex);
            if (result) this->sampler = *result;
        }
        this->samplerJobMutex.unlock();
        return this->sampler;
    }

    Medium Scene::GetMedium(const std::string& name, const FileLoc* loc) {
        if (name.empty()) return nullptr;

        this->mediaMutex.lock();
        while (true) {
            if (std::map<std::string, Medium>::iterator iter = this->mediaMap.find(name); iter != this->mediaMap.end()) {
                Medium medium = iter->second;
                this->mediaMutex.unlock();
                return medium;
            }

            std::map<std::string, AsyncJob<Medium>*>::iterator job = this->mediumJobs.find(name);
            if (job == this->mediumJobs.end()) throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: medium is not defined.", name));

            pstd::optional<Medium> medium = job->second->TryGetResult(&this->mediaMutex);
            if (medium) {
                this->mediaMap[name] = *medium;
                this->mediumJobs.erase(job);
                this->mediaMutex.unlock();
                return *medium;
            }
        }
    }

    std::map<std::string, Medium> Scene::CreateMedia() {
        this->mediaMutex.lock();
        if (!this->mediumJobs.empty()) {
            for (std::pair<const std::string, AsyncJob<Medium>*>& mediumJob : this->mediumJobs) {
                while (this->mediaMap.find(mediumJob.first) == this->mediaMap.end()) {
                    pstd::optional<Medium> medium = mediumJob.second->TryGetResult(&this->mediaMutex);
                    if (medium) this->mediaMap[mediumJob.first] = *medium;
                }
            }
            this->mediumJobs.clear();
        }
        this->mediaMutex.unlock();
        return this->mediaMap;
    }

    void Scene::StartLoadingNormalMaps(const ParameterDictionary& parameters) {
        std::string filename = ResolveFilename(parameters.GetOneString("normalmap", ""));
        if (filename.empty()) return;
        if (this->normalMapJobs.find(filename) != this->normalMapJobs.end()) return;

        auto create = [filename, this]() {
            Allocator alloc          = this->threadAllocators.Get();
            ImageAndMetadata immeta  = Image::Read(filename, Allocator(), ColorEncoding::Linear);
            Image& image             = immeta.image;
            ImageChannelDesc rgbDesc = image.GetChannelDesc({"R", "G", "B"});
            if (!rgbDesc) throw std::runtime_error(spectra::diagnostics::Format("%s: normal map image must contain R, G, and B channels", filename));
            Image* normalMap = alloc.new_object<Image>(alloc);
            *normalMap       = image.SelectChannels(rgbDesc);
            return normalMap;
        };
        this->normalMapJobs[filename] = RunAsync(create);
    }

    void Scene::CreateMaterials(const NamedTextures& textures, std::map<std::string, Material>* materialsOut) {
        std::lock_guard<std::mutex> lock(this->materialMutex);
        for (std::pair<const std::string, AsyncJob<Image*>*>& job : this->normalMapJobs) {
            SPECTRA_CHECK(this->normalMaps.find(job.first) == this->normalMaps.end());
            this->normalMaps[job.first] = job.second->GetResult();
        }
        this->normalMapJobs.clear();

        for (const std::pair<std::string, SceneEntity>& material : this->materials) {
            const std::string& name   = material.first;
            const SceneEntity& entity = material.second;
            Allocator alloc           = this->threadAllocators.Get();
            std::string normalMapName = ResolveFilename(entity.parameters.GetOneString("normalmap", ""));
            Image* normalMap          = nullptr;
            if (!normalMapName.empty()) {
                SPECTRA_CHECK(this->normalMaps.find(normalMapName) != this->normalMaps.end());
                normalMap = this->normalMaps[normalMapName];
            }

            TextureParameterDictionary textureParameters(&entity.parameters, &textures);
            Material createdMaterial = Material::Create(entity.name, textureParameters, normalMap, *materialsOut, &entity.loc, alloc);
            (*materialsOut)[name]    = createdMaterial;
        }
    }

    NamedTextures Scene::CreateTextures() {
        NamedTextures textures;

        this->textureMutex.lock();
        for (std::pair<const std::string, AsyncJob<FloatTexture>*>& texture : this->floatTextureJobs) textures.floatTextures[texture.first] = texture.second->GetResult();
        this->floatTextureJobs.clear();
        for (std::pair<const std::string, AsyncJob<SpectrumTexture>*>& texture : this->spectrumTextureJobs) textures.albedoSpectrumTextures[texture.first] = texture.second->GetResult();
        this->spectrumTextureJobs.clear();
        this->textureMutex.unlock();

        Allocator alloc = this->threadAllocators.Get();
        for (const std::pair<std::string, TextureSceneEntity>& texture : this->asyncSpectrumTextures) {
            Transform renderFromTexture = texture.second.renderFromObject;
            TextureParameterDictionary textureParameters(&texture.second.parameters, nullptr);
            SpectrumTexture unboundedTexture                   = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, alloc);
            SpectrumTexture illuminantTexture                  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, alloc);
            textures.unboundedSpectrumTextures[texture.first]  = unboundedTexture;
            textures.illuminantSpectrumTextures[texture.first] = illuminantTexture;
        }

        for (const std::pair<std::string, TextureSceneEntity>& texture : this->serialFloatTextures) {
            Allocator alloc             = this->threadAllocators.Get();
            Transform renderFromTexture = texture.second.renderFromObject;
            TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
            textures.floatTextures[texture.first] = FloatTexture::Create(texture.second.name, renderFromTexture, textureParameters, &texture.second.loc, alloc);
        }

        for (const std::pair<std::string, TextureSceneEntity>& texture : this->serialSpectrumTextures) {
            Allocator alloc             = this->threadAllocators.Get();
            Transform renderFromTexture = texture.second.renderFromObject;
            TextureParameterDictionary textureParameters(&texture.second.parameters, &textures);
            textures.albedoSpectrumTextures[texture.first]     = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Albedo, &texture.second.loc, alloc);
            textures.unboundedSpectrumTextures[texture.first]  = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Unbounded, &texture.second.loc, alloc);
            textures.illuminantSpectrumTextures[texture.first] = SpectrumTexture::Create(texture.second.name, renderFromTexture, textureParameters, SpectrumType::Illuminant, &texture.second.loc, alloc);
        }

        return textures;
    }

    std::vector<Light> Scene::CreateLights(const NamedTextures& textures, const std::map<std::string, Material>& materials, std::map<int, pstd::vector<Light>*>* shapeIndexToAreaLights) {
        auto findMedium = [this](const std::string& name, const FileLoc* loc) -> Medium {
            if (name.empty()) return nullptr;
            std::map<std::string, Medium>::iterator iter = this->mediaMap.find(name);
            if (iter == this->mediaMap.end()) throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: medium not defined", name));
            return iter->second;
        };

        Allocator alloc = this->threadAllocators.Get();

        auto getAlphaTexture = [&](const ParameterDictionary& parameters, const FileLoc* loc) -> FloatTexture {
            std::string alphaTextureName = parameters.GetTexture("alpha");
            if (!alphaTextureName.empty()) {
                std::map<std::string, FloatTexture>::const_iterator iter = textures.floatTextures.find(alphaTextureName);
                if (iter == textures.floatTextures.end()) throw std::runtime_error(spectra::diagnostics::Format(loc, "%s: couldn't find float texture for \"alpha\" parameter.", alphaTextureName));
                if (!BasicTextureEvaluator().CanEvaluate({iter->second}, {})) return nullptr;
                return iter->second;
            }

            Float alpha = parameters.GetOneFloat("alpha", 1.0f);
            if (alpha < 1.0f) return alloc.new_object<FloatConstantTexture>(alpha);
            return nullptr;
        };

        std::vector<Light> lights;
        for (std::size_t index = 0; index < this->shapes.size(); ++index) {
            const ShapeSceneEntity& shape = this->shapes[index];
            if (!shape.areaLight.has_value()) continue;

            std::map<std::string, Material>::const_iterator materialIter = materials.find(shape.materialName);
            if (materialIter == materials.end()) throw std::runtime_error(spectra::diagnostics::Format(&shape.loc, "%s: no named material defined.", shape.materialName));

            if (!materialIter->second) throw std::runtime_error(spectra::diagnostics::Format(&shape.loc, "Area light shape \"%s\" cannot use an interface material.", shape.name));

            pstd::vector<Shape> shapeObjects = Shape::Create(shape.name, shape.renderFromObject, shape.objectFromRender, shape.reverseOrientation, shape.parameters, textures.floatTextures, &shape.loc, alloc);
            FloatTexture alphaTexture        = getAlphaTexture(shape.parameters, &shape.loc);
            MediumInterface mediumInterface(findMedium(shape.insideMedium, &shape.loc), findMedium(shape.outsideMedium, &shape.loc));
            pstd::vector<Light>* shapeLights = new pstd::vector<Light>(alloc);
            for (Shape shapeObject : shapeObjects) {
                Light areaLight = Light::CreateArea(shape.areaLight->name, shape.areaLight->parameters, *shape.renderFromObject, mediumInterface, shapeObject, alphaTexture, &shape.areaLight->loc, alloc);
                if (areaLight) {
                    lights.push_back(areaLight);
                    shapeLights->push_back(areaLight);
                }
            }
            (*shapeIndexToAreaLights)[static_cast<int>(index)] = shapeLights;
        }

        std::lock_guard<std::mutex> lock(this->lightMutex);
        for (AsyncJob<Light>* job : this->lightJobs) lights.push_back(job->GetResult());
        return lights;
    }

    SceneInfo SceneInfoFor(std::string_view name) {
        return BuiltinSceneInfoFor(name);
    }

    std::unique_ptr<Scene> BuildScene(std::string_view name, std::optional<Point2i> filmResolutionOverride) {
        return BuildBuiltinScene(name, filmResolutionOverride);
    }
} // namespace spectra::scene
