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

        struct ParameterSpec {
            std::string type{};
            std::string name{};
            std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{};
            bool mayBeUnused{false};
            scene::ColorSpace colorSpace{scene::ColorSpace::sRGB};
        };

        void AppendParameterValues(ParsedParameter* parsedParameter, const ParameterSpec& parameter) {
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

        [[nodiscard]] ParameterSpec FloatParameter(std::string type, std::string name, std::vector<float> values, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) {
            return ParameterSpec{
                .type       = std::move(type),
                .name       = std::move(name),
                .values     = std::move(values),
                .colorSpace = colorSpace,
            };
        }

        [[nodiscard]] ParameterSpec IntegerParameter(std::string name, std::vector<int> values) {
            return ParameterSpec{
                .type   = "integer",
                .name   = std::move(name),
                .values = std::move(values),
            };
        }

        [[nodiscard]] ParameterSpec StringParameter(std::string type, std::string name, std::vector<std::string> values, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) {
            return ParameterSpec{
                .type       = std::move(type),
                .name       = std::move(name),
                .values     = std::move(values),
                .colorSpace = colorSpace,
            };
        }

        [[nodiscard]] ParameterSpec StringParameter(std::string name, std::vector<std::string> values, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) {
            return StringParameter("string", std::move(name), std::move(values), colorSpace);
        }

        [[nodiscard]] std::string TextureReferenceName(const std::map<scene::SceneTextureId, std::string>& names, scene::SceneTextureId textureId) {
            std::map<scene::SceneTextureId, std::string>::const_iterator iter = names.find(textureId);
            if (iter == names.end()) throw std::runtime_error("Spectra scene references an unknown texture id");
            return iter->second;
        }

        void AppendFloatInput(std::vector<ParameterSpec>* parameters, std::string name, const scene::SceneFloatInput& input, const std::map<scene::SceneTextureId, std::string>& textureNames, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) {
            if (const float* value = std::get_if<float>(&input.value)) {
                parameters->push_back(FloatParameter("float", std::move(name), std::vector<float>{*value}, colorSpace));
                return;
            }
            const scene::SceneTextureReference& reference = std::get<scene::SceneTextureReference>(input.value);
            parameters->push_back(StringParameter("texture", std::move(name), std::vector<std::string>{TextureReferenceName(textureNames, reference.texture)}, colorSpace));
        }

        void AppendSpectrumInput(std::vector<ParameterSpec>* parameters, std::string name, const scene::SceneSpectrumInput& input, const std::map<scene::SceneTextureId, std::string>& textureNames, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) {
            if (const scene::SceneRgb* rgb = std::get_if<scene::SceneRgb>(&input.value)) {
                parameters->push_back(FloatParameter("rgb", std::move(name), std::vector<float>{rgb->r, rgb->g, rgb->b}, colorSpace));
                return;
            }
            if (const std::vector<float>* samples = std::get_if<std::vector<float>>(&input.value)) {
                parameters->push_back(FloatParameter("spectrum", std::move(name), *samples, colorSpace));
                return;
            }
            const scene::SceneTextureReference& reference = std::get<scene::SceneTextureReference>(input.value);
            parameters->push_back(StringParameter("texture", std::move(name), std::vector<std::string>{TextureReferenceName(textureNames, reference.texture)}, colorSpace));
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
            [[nodiscard]] ParameterDictionary MakeParameterDictionary(const std::vector<ParameterSpec>& parameters, scene::ColorSpace colorSpace) const {
                ParsedParameterVector parsedParameters{};
                for (const ParameterSpec& parameter : parameters) {
                    if (parameter.type.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty type.", this->source.source));
                    if (parameter.name.empty()) throw std::runtime_error(std::format("{} scene parameter has an empty name.", this->source.source));
                    ParsedParameter* parsedParameter = new ParsedParameter(this->location);
                    parsedParameter->type            = parameter.type;
                    parsedParameter->name            = parameter.name;
                    parsedParameter->mayBeUnused     = parameter.mayBeUnused;
                    parsedParameter->colorSpace      = ToPathtracerColorSpace(parameter.colorSpace);
                    AppendParameterValues(parsedParameter, parameter);
                    parsedParameters.push_back(parsedParameter);
                }

                return ParameterDictionary(std::move(parsedParameters), ToPathtracerColorSpace(colorSpace));
            }

            [[nodiscard]] PathtracerSceneEntity MakeEntity(const std::string& type, const std::vector<ParameterSpec>& parameters, scene::ColorSpace colorSpace = scene::ColorSpace::sRGB) const {
                if (type.empty()) throw std::runtime_error(std::format("{} scene entity has an empty type.", this->source.source));
                return PathtracerSceneEntity{
                    .name       = type,
                    .loc        = this->location,
                    .parameters = this->MakeParameterDictionary(parameters, colorSpace),
                };
            }

            [[nodiscard]] std::string MaterialName(scene::SceneMaterialId id) const {
                std::map<scene::SceneMaterialId, std::string>::const_iterator iter = this->materialIdToName.find(id);
                if (iter == this->materialIdToName.end()) throw std::runtime_error(std::format("{} scene references an unknown material id.", this->source.source));
                return iter->second;
            }

            [[nodiscard]] std::string MediumName(std::optional<scene::SceneMediumId> id) const {
                if (!id.has_value()) return "";
                std::map<scene::SceneMediumId, std::string>::const_iterator iter = this->mediumIdToName.find(*id);
                if (iter == this->mediumIdToName.end()) throw std::runtime_error(std::format("{} scene references an unknown medium id.", this->source.source));
                return iter->second;
            }

            [[nodiscard]] std::string ObjectDefinitionName(scene::SceneObjectDefinitionId id) const {
                std::map<scene::SceneObjectDefinitionId, std::string>::const_iterator iter = this->objectDefinitionIdToName.find(id);
                if (iter == this->objectDefinitionIdToName.end()) throw std::runtime_error(std::format("{} scene references an unknown object definition id.", this->source.source));
                return iter->second;
            }

            [[nodiscard]] std::string TextureName(scene::SceneTextureId id) const {
                return TextureReferenceName(this->textureIdToName, id);
            }

            [[nodiscard]] std::string FilmName(scene::FilmKind kind) const {
                switch (kind) {
                case scene::FilmKind::Rgb: return "rgb";
                }
                throw std::runtime_error("Unknown Spectra film kind.");
            }

            [[nodiscard]] std::string SamplerName(scene::SamplerKind kind) const {
                switch (kind) {
                case scene::SamplerKind::Halton: return "halton";
                case scene::SamplerKind::ZSobol: return "zsobol";
                }
                throw std::runtime_error("Unknown Spectra sampler kind.");
            }

            [[nodiscard]] std::string IntegratorName(scene::IntegratorKind kind) const {
                switch (kind) {
                case scene::IntegratorKind::VolPath: return "volpath";
                }
                throw std::runtime_error("Unknown Spectra integrator kind.");
            }

            [[nodiscard]] std::string AcceleratorName(scene::AcceleratorKind kind) const {
                switch (kind) {
                case scene::AcceleratorKind::Bvh: return "bvh";
                }
                throw std::runtime_error("Unknown Spectra accelerator kind.");
            }

            [[nodiscard]] std::string CameraName(scene::CameraKind kind) const {
                switch (kind) {
                case scene::CameraKind::Perspective: return "perspective";
                }
                throw std::runtime_error("Unknown Spectra camera kind.");
            }

            [[nodiscard]] std::string ShapeName(const scene::SceneShape& shape) const {
                if (std::holds_alternative<scene::SceneSphere>(shape.value)) return "sphere";
                if (std::holds_alternative<scene::SceneDisk>(shape.value)) return "disk";
                if (std::holds_alternative<scene::ScenePlyMesh>(shape.value)) return "plymesh";
                throw std::runtime_error("Unknown Spectra shape kind.");
            }

            [[nodiscard]] std::string TextureEntityName(const scene::SceneTexture& texture) const {
                if (std::holds_alternative<scene::SceneImageTexture>(texture.value)) return "imagemap";
                if (std::holds_alternative<scene::SceneConstantFloatTexture>(texture.value)) return "constant";
                if (std::holds_alternative<scene::SceneScaleFloatTexture>(texture.value)) return "scale";
                throw std::runtime_error("Unknown Spectra texture kind.");
            }

            [[nodiscard]] std::string MaterialEntityName(const scene::SceneMaterial& material) const {
                if (std::holds_alternative<scene::SceneDiffuseMaterial>(material.value)) return "diffuse";
                if (std::holds_alternative<scene::SceneCoatedDiffuseMaterial>(material.value)) return "coateddiffuse";
                if (std::holds_alternative<scene::SceneInterfaceMaterial>(material.value)) return "interface";
                throw std::runtime_error("Unknown Spectra material kind.");
            }

            [[nodiscard]] std::vector<ParameterSpec> FilmParameters(scene::SceneFilmSettings film) const {
                if (this->filmResolutionOverride.has_value()) {
                    if (this->filmResolutionOverride->x <= 0 || this->filmResolutionOverride->y <= 0) throw std::runtime_error("Spectra interactive film resolution must be positive.");
                    film.xResolution = this->filmResolutionOverride->x;
                    film.yResolution = this->filmResolutionOverride->y;
                }
                std::vector<ParameterSpec> parameters{
                    StringParameter("filename", std::vector<std::string>{film.filename}, film.colorSpace),
                    IntegerParameter("xresolution", std::vector<int>{film.xResolution}),
                    IntegerParameter("yresolution", std::vector<int>{film.yResolution}),
                };
                if (!film.sensor.empty()) parameters.push_back(StringParameter("sensor", std::vector<std::string>{film.sensor}, film.colorSpace));
                parameters.push_back(FloatParameter("float", "iso", std::vector<float>{film.iso}, film.colorSpace));
                if (film.whiteBalance.has_value()) parameters.push_back(FloatParameter("float", "whitebalance", std::vector<float>{*film.whiteBalance}, film.colorSpace));
                return parameters;
            }

            [[nodiscard]] std::vector<ParameterSpec> CameraParameters(const scene::SceneCamera& camera) const {
                return {
                    FloatParameter("float", "fov", std::vector<float>{camera.fovDegrees}),
                    FloatParameter("float", "shutteropen", std::vector<float>{camera.shutterOpen}),
                    FloatParameter("float", "shutterclose", std::vector<float>{camera.shutterClose}),
                };
            }

            [[nodiscard]] std::vector<ParameterSpec> SamplerParameters(const scene::SceneSamplerSettings& sampler) const {
                return {IntegerParameter("pixelsamples", std::vector<int>{sampler.pixelSamples})};
            }

            [[nodiscard]] std::vector<ParameterSpec> IntegratorParameters(const scene::SceneIntegratorSettings& integrator) const {
                if (integrator.maxDepth.has_value()) return {IntegerParameter("maxdepth", std::vector<int>{*integrator.maxDepth})};
                return {};
            }

            [[nodiscard]] std::vector<ParameterSpec> MaterialParameters(const scene::SceneMaterial& material) const {
                std::vector<ParameterSpec> parameters{};
                if (const scene::SceneDiffuseMaterial* diffuse = std::get_if<scene::SceneDiffuseMaterial>(&material.value)) {
                    AppendSpectrumInput(&parameters, "reflectance", diffuse->reflectance, this->textureIdToName);
                    return parameters;
                }
                if (const scene::SceneCoatedDiffuseMaterial* coatedDiffuse = std::get_if<scene::SceneCoatedDiffuseMaterial>(&material.value)) {
                    AppendSpectrumInput(&parameters, "reflectance", coatedDiffuse->reflectance, this->textureIdToName);
                    AppendFloatInput(&parameters, "roughness", coatedDiffuse->roughness, this->textureIdToName);
                    if (coatedDiffuse->displacement.has_value()) AppendFloatInput(&parameters, "displacement", *coatedDiffuse->displacement, this->textureIdToName);
                    return parameters;
                }
                if (std::holds_alternative<scene::SceneInterfaceMaterial>(material.value)) return {};
                throw std::runtime_error("Unknown Spectra material kind.");
            }

            [[nodiscard]] std::vector<ParameterSpec> TextureParameters(const scene::SceneTexture& texture) const {
                if (const scene::SceneImageTexture* image = std::get_if<scene::SceneImageTexture>(&texture.value)) {
                    return {
                        StringParameter("filename", std::vector<std::string>{image->filename}, texture.colorSpace),
                        FloatParameter("float", "uscale", std::vector<float>{image->uScale}, texture.colorSpace),
                        FloatParameter("float", "vscale", std::vector<float>{image->vScale}, texture.colorSpace),
                    };
                }
                if (const scene::SceneConstantFloatTexture* constant = std::get_if<scene::SceneConstantFloatTexture>(&texture.value)) return {FloatParameter("float", "value", std::vector<float>{constant->value}, texture.colorSpace)};
                if (const scene::SceneScaleFloatTexture* scale = std::get_if<scene::SceneScaleFloatTexture>(&texture.value)) {
                    return {
                        StringParameter("texture", "scale", std::vector<std::string>{this->TextureName(scale->scale)}, texture.colorSpace),
                        StringParameter("texture", "tex", std::vector<std::string>{this->TextureName(scale->texture)}, texture.colorSpace),
                    };
                }
                throw std::runtime_error("Unknown Spectra texture kind.");
            }

            [[nodiscard]] std::vector<ParameterSpec> MediumParameters(const scene::SceneMedium& medium) const {
                const scene::SceneNanoVdbMedium& nanovdb = std::get<scene::SceneNanoVdbMedium>(medium.value);
                return {
                    StringParameter("filename", std::vector<std::string>{nanovdb.filename}, medium.colorSpace),
                    FloatParameter("spectrum", "sigma_s", nanovdb.sigmaS, medium.colorSpace),
                    FloatParameter("spectrum", "sigma_a", nanovdb.sigmaA, medium.colorSpace),
                    FloatParameter("float", "Lescale", std::vector<float>{nanovdb.leScale}, medium.colorSpace),
                    FloatParameter("float", "temperaturecutoff", std::vector<float>{nanovdb.temperatureCutoff}, medium.colorSpace),
                    FloatParameter("float", "temperaturescale", std::vector<float>{nanovdb.temperatureScale}, medium.colorSpace),
                };
            }

            [[nodiscard]] std::vector<ParameterSpec> LightParameters(const scene::SceneLight& light) const {
                const scene::SceneInfiniteLight& infinite = std::get<scene::SceneInfiniteLight>(light.value);
                return {
                    StringParameter("filename", std::vector<std::string>{infinite.filename}, light.colorSpace),
                    FloatParameter("float", "scale", std::vector<float>{infinite.scale}, light.colorSpace),
                };
            }

            [[nodiscard]] std::vector<ParameterSpec> AreaLightParameters(const scene::SceneAreaLight& areaLight) const {
                const scene::SceneDiffuseAreaLight& diffuse = std::get<scene::SceneDiffuseAreaLight>(areaLight.value);
                std::vector<ParameterSpec> parameters{};
                AppendSpectrumInput(&parameters, "L", diffuse.emission, this->textureIdToName, areaLight.colorSpace);
                return parameters;
            }

            [[nodiscard]] std::vector<ParameterSpec> ShapeParameters(const scene::SceneShape& shape) const {
                if (const scene::SceneSphere* sphere = std::get_if<scene::SceneSphere>(&shape.value)) return {FloatParameter("float", "radius", std::vector<float>{sphere->radius}, shape.colorSpace)};
                if (const scene::SceneDisk* disk = std::get_if<scene::SceneDisk>(&shape.value)) return {FloatParameter("float", "radius", std::vector<float>{disk->radius}, shape.colorSpace)};
                if (const scene::ScenePlyMesh* plymesh = std::get_if<scene::ScenePlyMesh>(&shape.value)) return {StringParameter("filename", std::vector<std::string>{plymesh->filename}, shape.colorSpace)};
                throw std::runtime_error("Unknown Spectra shape kind.");
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

            template <typename Id>
            void RegisterName(std::map<Id, std::string>* names, Id id, std::string_view kind, const std::string& name) {
                if (id.value == 0) throw std::runtime_error(std::format("{} scene {} \"{}\" must have an assigned id before pathtracer compilation.", this->source.source, kind, name));
                if (name.empty()) throw std::runtime_error(std::format("{} scene {} name must not be empty.", this->source.source, kind));
                if (!names->emplace(id, name).second) throw std::runtime_error(std::format("{} scene {} id {} is already registered.", this->source.source, kind, id.value));
            }

            void RegisterResourceNames() {
                for (const scene::SceneMaterial& material : this->source.materials) this->RegisterName(&this->materialIdToName, material.id, "material", material.name);
                for (const scene::SceneTexture& texture : this->source.textures) this->RegisterName(&this->textureIdToName, texture.id, "texture", texture.name);
                for (const scene::SceneMedium& medium : this->source.media) this->RegisterName(&this->mediumIdToName, medium.id, "medium", medium.name);
                for (const scene::SceneObjectDefinition& definition : this->source.objectDefinitions) this->RegisterName(&this->objectDefinitionIdToName, definition.id, "object definition", definition.name);
            }

            [[nodiscard]] Transform RenderFromWorldTransform() const {
                this->RequireRenderSettings();
                return this->cameraEntity.cameraTransform.RenderFromWorld();
            }

            [[nodiscard]] Transform RenderFromObjectTransform(const math::Transform& worldFromObject) const {
                return this->RenderFromWorldTransform() * ToPathtracerTransform(worldFromObject);
            }

            [[nodiscard]] PathtracerShapeSceneEntity MakeShapeEntity(const scene::SceneShape& shape) const {
                const std::string materialName = this->MaterialName(shape.material);
                this->RequireMaterial(materialName);
                Transform renderFromObject = this->RenderFromObjectTransform(shape.worldFromObject);
                Allocator allocator        = this->compiled.threadAllocators.Get();
                PathtracerSceneEntity base = this->MakeEntity(this->ShapeName(shape), this->ShapeParameters(shape), shape.colorSpace);
                PathtracerShapeSceneEntity entity{
                    .name               = std::move(base.name),
                    .loc                = base.loc,
                    .parameters         = std::move(base.parameters),
                    .renderFromObject   = allocator.new_object<Transform>(renderFromObject),
                    .objectFromRender   = allocator.new_object<Transform>(Inverse(renderFromObject)),
                    .reverseOrientation = shape.reverseOrientation,
                    .materialName       = materialName,
                    .insideMedium       = this->MediumName(shape.insideMedium),
                    .outsideMedium      = this->MediumName(shape.outsideMedium),
                };
                if (shape.insideMedium.has_value() || shape.outsideMedium.has_value()) this->compiled.haveMedia = true;
                if (shape.areaLight.has_value()) entity.areaLight = this->MakeEntity("diffuse", this->AreaLightParameters(*shape.areaLight), shape.areaLight->colorSpace);
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
                if (settings.camera.fovDegrees <= 0.0f) throw std::runtime_error(std::format("{} scene camera fov must be positive.", this->source.source));

                PathtracerSceneEntity filterEntity = this->MakeEntity("gaussian", {});
                PathtracerSceneEntity filmEntity   = this->MakeEntity(this->FilmName(settings.film.kind), this->FilmParameters(settings.film), settings.film.colorSpace);
                this->samplerEntity                = this->MakeEntity(this->SamplerName(settings.sampler.kind), this->SamplerParameters(settings.sampler));
                this->compiled.integrator          = this->MakeEntity(this->IntegratorName(settings.integrator.kind), this->IntegratorParameters(settings.integrator));
                this->compiled.accelerator         = this->MakeEntity(this->AcceleratorName(settings.accelerator.kind), {});
                PathtracerSceneEntity cameraEntity = this->MakeEntity(this->CameraName(settings.camera.kind), this->CameraParameters(settings.camera));
                const Transform worldFromCamera    = ToPathtracerTransform(settings.camera.worldFromCamera);
                this->cameraEntity                 = PathtracerCameraSceneEntity{
                    .name            = std::move(cameraEntity.name),
                    .loc             = cameraEntity.loc,
                    .parameters      = std::move(cameraEntity.parameters),
                    .cameraTransform = CameraTransform(AnimatedTransform(worldFromCamera, 0.0f, worldFromCamera, 1.0f), this->renderConfig.rendering_space),
                    .medium          = this->MediumName(settings.camera.medium),
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
                PathtracerSceneEntity entity = this->MakeEntity(this->MaterialEntityName(material), this->MaterialParameters(material));
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

                const std::string textureEntityName = this->TextureEntityName(texture);
                PathtracerSceneEntity base          = this->MakeEntity(textureEntityName, this->TextureParameters(texture), texture.colorSpace);
                PathtracerTransformedSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(texture.worldFromTexture),
                };

                std::lock_guard<std::mutex> lock(this->textureMutex);
                if (texture.kind == scene::TextureKind::Float) {
                    this->floatTextureNames.insert(texture.name);
                    if (!IsImageTexture(textureEntityName)) {
                        this->serialFloatTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                } else {
                    this->spectrumTextureNames.insert(texture.name);
                    if (!IsImageTexture(textureEntityName)) {
                        this->serialSpectrumTextures.push_back(std::make_pair(texture.name, std::move(entity)));
                        return;
                    }
                }

                std::string filename = entity.parameters.GetOneString("filename", "");
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

                PathtracerSceneEntity base = this->MakeEntity("nanovdb", this->MediumParameters(medium), medium.colorSpace);
                PathtracerTransformedSceneEntity entity{
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

                PathtracerSceneEntity base = this->MakeEntity("infinite", this->LightParameters(light), light.colorSpace);
                PathtracerLightSceneEntity entity{
                    .name             = std::move(base.name),
                    .loc              = base.loc,
                    .parameters       = std::move(base.parameters),
                    .renderFromObject = this->RenderFromObjectTransform(light.worldFromLight),
                    .medium           = this->MediumName(light.medium),
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
                this->RequireUniqueName(this->objectDefinitionNames, "object definition", definition.name);
                PathtracerInstanceDefinitionSceneEntity entity{
                    .name = definition.name,
                    .loc  = this->location,
                };
                entity.shapes.reserve(definition.shapes.size());
                for (const scene::SceneShape& shape : definition.shapes) {
                    PathtracerShapeSceneEntity shapeEntity = this->MakeShapeEntity(shape);
                    if (shapeEntity.areaLight.has_value()) throw std::runtime_error(std::format("{} scene object definition \"{}\" contains an area light shape; instanced area lights are not supported.", this->source.source, definition.name));
                    entity.shapes.push_back(std::move(shapeEntity));
                }

                this->compiled.instanceDefinitions[definition.name] = std::move(entity);
                this->objectDefinitionNames.insert(definition.name);
            }

            void AddObjectInstance(const scene::SceneObjectInstance& instance) {
                this->RequireRenderSettings();
                const std::string definitionName = this->ObjectDefinitionName(instance.definition);
                if (this->objectDefinitionNames.find(definitionName) == this->objectDefinitionNames.end()) throw std::runtime_error(std::format("{} scene references unknown object definition \"{}\".", this->source.source, definitionName));

                Transform worldFromRender = Inverse(this->RenderFromWorldTransform());
                this->compiled.instances.push_back({
                    .name               = definitionName,
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
            const RenderConfig& renderConfig;
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
            std::map<scene::SceneMaterialId, std::string> materialIdToName{};
            std::map<scene::SceneTextureId, std::string> textureIdToName{};
            std::map<scene::SceneMediumId, std::string> mediumIdToName{};
            std::map<scene::SceneObjectDefinitionId, std::string> objectDefinitionIdToName{};
        };
    } // namespace

    CompiledPathtracerScene::CompiledPathtracerScene(pstd::pmr::memory_resource* memoryResource) : allLights(Allocator(RequireMemoryResource(memoryResource))), lightSpectrumCache(Allocator(RequireMemoryResource(memoryResource))), threadAllocators([memoryResource]() { return Allocator(RequireMemoryResource(memoryResource)); }) {}

    std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const scene::SceneSnapshot& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride) {
        std::unique_ptr<CompiledPathtracerScene> compiled = std::make_unique<CompiledPathtracerScene>(memoryResource);
        PathtracerSceneCompiler compiler(scene, *compiled, config, filmResolutionOverride);
        compiler.Compile();
        return compiled;
    }
} // namespace spectra::pathtracer
