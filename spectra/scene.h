#ifndef SPECTRA_SCENE_H
#define SPECTRA_SCENE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <spectra/pathtracer/core/cameras.h>
#include <spectra/pathtracer/core/diagnostics.h>
#include <spectra/pathtracer/core/paramdict.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/transform.h>
#include <string>
#include <string_view>
#include <vector>

namespace spectra::scene {
    enum class TextureKind { Float, Spectrum };

    struct SceneParameter {
        std::string type{};
        std::string name{};
        std::vector<Float> floats{};
        std::vector<int> integers{};
        std::vector<std::string> strings{};
        std::vector<std::uint8_t> booleans{};
        bool mayBeUnused{false};
    };

    struct SceneParameters {
        const RGBColorSpace* colorSpace{RGBColorSpace::sRGB};
        std::vector<SceneParameter> values{};
    };

    struct SceneComponent {
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneCamera {
        std::string type{"perspective"};
        SceneParameters parameters{};
        Transform worldFromCamera{};
        std::string medium{};
        float fovDegrees{};
    };

    struct SceneRenderSettings {
        SceneComponent filter{"gaussian", {}};
        SceneComponent film{"rgb", {}};
        SceneCamera camera{};
        SceneComponent sampler{"zsobol", {}};
        SceneComponent integrator{"volpath", {}};
        SceneComponent accelerator{"bvh", {}};
    };

    struct SceneMaterial {
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneTexture {
        TextureKind kind{TextureKind::Spectrum};
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromTexture{};
    };

    struct SceneMedium {
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromMedium{};
    };

    struct SceneLight {
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromLight{};
        std::string medium{};
    };

    struct SceneAreaLight {
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneShape {
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromObject{};
        bool reverseOrientation{false};
        std::string material{};
        std::optional<SceneAreaLight> areaLight{};
        std::string insideMedium{};
        std::string outsideMedium{};
    };

    struct SceneObjectDefinition {
        std::string name{};
        std::vector<SceneShape> shapes{};
    };

    struct SceneObjectInstance {
        std::string name{};
        Transform worldFromInstance{};
    };

    struct SceneInfo {
        std::string name{};
        std::string title{};
        std::string camera{};
        std::string sampler{};
        std::string integrator{};
        std::string accelerator{};
        std::size_t shape_count{};
        std::size_t material_count{};
        std::size_t texture_count{};
        std::size_t medium_count{};
        std::size_t light_count{};
        std::size_t area_light_count{};
        std::size_t infinite_light_count{};
        std::size_t object_definition_count{};
        std::size_t object_instance_count{};
        float camera_fov_degrees{};
    };

    struct SceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
    };

    struct CameraSceneEntity : SceneEntity {
        CameraTransform cameraTransform{};
        std::string medium{};
    };

    struct TransformedSceneEntity : SceneEntity {
        Transform renderFromObject{};
    };

    struct MediumSceneEntity : TransformedSceneEntity {};

    struct TextureSceneEntity : TransformedSceneEntity {};

    struct LightSceneEntity : TransformedSceneEntity {
        std::string medium{};
    };

    struct ShapeSceneEntity : SceneEntity {
        const Transform* renderFromObject = nullptr;
        const Transform* objectFromRender = nullptr;
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<SceneEntity> areaLight{};
        std::string insideMedium{};
        std::string outsideMedium{};
    };

    struct InstanceDefinitionSceneEntity {
        std::string name{};
        FileLoc loc{};
        std::vector<ShapeSceneEntity> shapes{};
    };

    struct InstanceSceneEntity {
        std::string name{};
        FileLoc loc{};
        Transform renderFromInstance{};
    };

    class Scene {
    public:
        explicit Scene(std::string name, std::string title, std::string source, std::optional<Point2i> filmResolutionOverride = {});

        void SetRenderSettings(SceneRenderSettings settings);
        void AddMaterial(SceneMaterial material);
        void AddTexture(SceneTexture texture);
        void AddMedium(SceneMedium medium);
        void AddLight(SceneLight light);
        void AddShape(SceneShape shape);
        void AddObjectDefinition(SceneObjectDefinition definition);
        void AddObjectInstance(SceneObjectInstance instance);

        [[nodiscard]] SceneInfo Info() const;
        [[nodiscard]] Camera GetCamera();
        [[nodiscard]] Sampler GetSampler();

        void CreateMaterials(const NamedTextures& sceneTextures, std::map<std::string, Material>* materials);
        [[nodiscard]] std::vector<Light> CreateLights(const NamedTextures& textures, const std::map<std::string, Material>& materials, std::map<int, pstd::vector<Light>*>* shapeIndexToAreaLights);
        [[nodiscard]] std::map<std::string, Medium> CreateMedia();
        [[nodiscard]] NamedTextures CreateTextures();

        SceneEntity integrator{};
        SceneEntity accelerator{};
        const RGBColorSpace* filmColorSpace{RGBColorSpace::sRGB};
        std::vector<ShapeSceneEntity> shapes{};
        std::vector<InstanceSceneEntity> instances{};
        std::map<std::string, InstanceDefinitionSceneEntity> instanceDefinitions{};

    private:
        [[nodiscard]] FileLoc Location() const;
        [[nodiscard]] ParameterDictionary MakeParameterDictionary(const SceneParameters& parameters) const;
        [[nodiscard]] SceneEntity MakeEntity(const std::string& type, const SceneParameters& parameters) const;
        [[nodiscard]] Transform RenderFromWorldTransform() const;
        [[nodiscard]] Transform RenderFromObjectTransform(const Transform& worldFromObject) const;
        [[nodiscard]] ShapeSceneEntity MakeShapeEntity(const SceneShape& shape) const;
        [[nodiscard]] Medium GetMedium(const std::string& name, const FileLoc* loc);

        void ApplyFilmResolutionOverride(SceneParameters* parameters) const;
        void StartLoadingNormalMaps(const ParameterDictionary& parameters);
        void RequireRenderSettings() const;
        void RequireMaterial(const std::string& materialName) const;
        void RequireUniqueName(const std::set<std::string>& names, std::string_view kind, const std::string& name) const;

        std::string name{};
        std::string title{};
        std::string source{};
        std::optional<Point2i> filmResolutionOverride{};
        bool renderSettingsReady{false};
        float cameraFovDegrees{};
        std::string samplerName{};
        CameraSceneEntity cameraEntity{};

        AsyncJob<Sampler>* samplerJob = nullptr;
        mutable ThreadLocal<Allocator> threadAllocators;
        Camera camera;
        Film film;
        std::mutex cameraJobMutex;
        AsyncJob<Camera>* cameraJob = nullptr;
        std::mutex samplerJobMutex;
        Sampler sampler;
        std::mutex mediaMutex;
        std::map<std::string, AsyncJob<Medium>*> mediumJobs;
        std::map<std::string, Medium> mediaMap;
        std::mutex materialMutex;
        std::map<std::string, AsyncJob<Image*>*> normalMapJobs;
        std::map<std::string, Image*> normalMaps;
        std::vector<std::pair<std::string, SceneEntity>> materials;
        std::mutex lightMutex;
        std::vector<AsyncJob<Light>*> lightJobs;
        std::mutex textureMutex;
        std::vector<std::pair<std::string, TextureSceneEntity>> serialFloatTextures;
        std::vector<std::pair<std::string, TextureSceneEntity>> serialSpectrumTextures;
        std::vector<std::pair<std::string, TextureSceneEntity>> asyncSpectrumTextures;
        std::set<std::string> loadingTextureFilenames;
        std::map<std::string, AsyncJob<FloatTexture>*> floatTextureJobs;
        std::map<std::string, AsyncJob<SpectrumTexture>*> spectrumTextureJobs;
        std::set<std::string> materialNames;
        std::set<std::string> mediumNames;
        std::set<std::string> floatTextureNames;
        std::set<std::string> spectrumTextureNames;
        std::set<std::string> objectDefinitionNames;
        std::size_t lightCount{};
        std::size_t infiniteLightCount{};
        std::size_t areaLightCount{};
    };

    [[nodiscard]] SceneInfo SceneInfoFor(std::string_view name);
    [[nodiscard]] std::unique_ptr<Scene> BuildScene(std::string_view name, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::scene

#endif // SPECTRA_SCENE_H
