#ifndef SPECTRA_SCENE_H
#define SPECTRA_SCENE_H

#include <array>
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
#include <spectra/pathtracer/util/containers.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/string.h>
#include <spectra/pathtracer/util/transform.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace spectra::scene {
    struct SceneEntity {
        InternedString name;
        FileLoc loc;
        ParameterDictionary parameters;
        static InternCache<std::string> internedStrings;
    };

    struct TransformedSceneEntity : SceneEntity {
        AnimatedTransform renderFromObject;
    };

    struct CameraSceneEntity : SceneEntity {
        CameraTransform cameraTransform;
        std::string medium;
    };

    struct ShapeSceneEntity : SceneEntity {
        const Transform *renderFromObject = nullptr, *objectFromRender = nullptr;
        bool reverseOrientation = false;
        int materialIndex;
        std::string materialName;
        int lightIndex = -1;
        std::string insideMedium, outsideMedium;
    };

    struct AnimatedShapeSceneEntity : TransformedSceneEntity {
        const Transform* identity = nullptr;
        bool reverseOrientation   = false;
        int materialIndex;
        std::string materialName;
        int lightIndex = -1;
        std::string insideMedium, outsideMedium;
    };

    struct InstanceDefinitionSceneEntity {
        InternedString name;
        FileLoc loc;
        std::vector<ShapeSceneEntity> shapes;
        std::vector<AnimatedShapeSceneEntity> animatedShapes;
    };

    struct MediumSceneEntity : TransformedSceneEntity {};

    struct TextureSceneEntity : TransformedSceneEntity {};

    struct LightSceneEntity : TransformedSceneEntity {
        std::string medium;
    };

    struct InstanceSceneEntity {
        InternedString name;
        FileLoc loc;
        AnimatedTransform* renderFromInstanceAnim = nullptr;
        const Transform* renderFromInstance       = nullptr;
    };


    constexpr int MaxTransforms = 2;

    struct TransformSet {
        Transform& operator[](int i) {
            SPECTRA_CHECK_GE(i, 0);
            SPECTRA_CHECK_LT(i, MaxTransforms);
            return t[i];
        }

        const Transform& operator[](int i) const {
            SPECTRA_CHECK_GE(i, 0);
            SPECTRA_CHECK_LT(i, MaxTransforms);
            return t[i];
        }

        bool IsAnimated() const {
            for (int i = 0; i < MaxTransforms - 1; ++i)
                if (t[i] != t[i + 1]) return true;
            return false;
        }

    private:
        Transform t[MaxTransforms];
    };

    inline TransformSet Inverse(const TransformSet& ts) {
        TransformSet tInv;
        for (int i = 0; i < MaxTransforms; ++i) tInv[i] = spectra::Inverse(ts[i]);
        return tInv;
    }

    struct SceneInfo {
        std::string_view name{};
        std::string_view title{};
        std::string_view camera{};
        std::string_view sampler{};
        std::string_view integrator{};
        std::string_view accelerator{};
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

    class Scene {
    public:
        Scene();

        void SetOptions(SceneEntity filter, SceneEntity film, CameraSceneEntity camera, SceneEntity sampler, SceneEntity integrator, SceneEntity accelerator);

        void AddNamedMaterial(std::string name, SceneEntity material);
        int AddMaterial(SceneEntity material);
        void AddMedium(MediumSceneEntity medium);
        void AddFloatTexture(std::string name, TextureSceneEntity texture);
        void AddSpectrumTexture(std::string name, TextureSceneEntity texture);
        void AddLight(LightSceneEntity light);
        int AddAreaLight(SceneEntity light);
        void AddShapes(pstd::span<ShapeSceneEntity> shape);
        void AddAnimatedShape(AnimatedShapeSceneEntity shape);
        void AddInstanceDefinition(InstanceDefinitionSceneEntity instance);
        void AddInstanceUses(pstd::span<InstanceSceneEntity> in);

        Camera GetCamera();
        Sampler GetSampler();

        void CreateMaterials(const NamedTextures& sceneTextures, std::map<std::string, Material>* namedMaterials, std::vector<Material>* materials);

        std::vector<Light> CreateLights(const NamedTextures& textures, std::map<int, pstd::vector<Light>*>* shapeIndexToAreaLights);

        std::map<std::string, Medium> CreateMedia();

        NamedTextures CreateTextures();

        SceneEntity integrator, accelerator;
        const RGBColorSpace* filmColorSpace;
        std::vector<ShapeSceneEntity> shapes;
        std::vector<AnimatedShapeSceneEntity> animatedShapes;
        std::vector<InstanceSceneEntity> instances;
        std::map<InternedString, InstanceDefinitionSceneEntity*> instanceDefinitions;

    private:
        Medium GetMedium(const std::string& name, const FileLoc* loc);

        void startLoadingNormalMaps(const ParameterDictionary& parameters);

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

        std::vector<std::pair<std::string, SceneEntity>> namedMaterials;
        std::vector<SceneEntity> materials;

        std::mutex lightMutex;
        std::vector<AsyncJob<Light>*> lightJobs;

        std::mutex areaLightMutex;
        std::vector<SceneEntity> areaLights;

        std::mutex textureMutex;
        std::vector<std::pair<std::string, TextureSceneEntity>> serialFloatTextures;
        std::vector<std::pair<std::string, TextureSceneEntity>> serialSpectrumTextures;
        std::vector<std::pair<std::string, TextureSceneEntity>> asyncSpectrumTextures;
        std::set<std::string> loadingTextureFilenames;
        std::map<std::string, AsyncJob<FloatTexture>*> floatTextureJobs;
        std::map<std::string, AsyncJob<SpectrumTexture>*> spectrumTextureJobs;

        std::mutex shapeMutex, animatedShapeMutex;
        std::mutex instanceDefinitionMutex, instanceUseMutex;
    };

    class SceneBuilder {
    public:
        SceneBuilder(Scene* scene);
        SceneBuilder(Scene* scene, Point2i filmResolutionOverride);
        void Option(const std::string& name, const std::string& value, FileLoc loc);
        void Identity(FileLoc loc);
        void Translate(Float dx, Float dy, Float dz, FileLoc loc);
        void Rotate(Float angle, Float ax, Float ay, Float az, FileLoc loc);
        void Scale(Float sx, Float sy, Float sz, FileLoc loc);
        void LookAt(Float ex, Float ey, Float ez, Float lx, Float ly, Float lz, Float ux, Float uy, Float uz, FileLoc loc);
        void ConcatTransform(Float transform[16], FileLoc loc);
        void Transform(Float transform[16], FileLoc loc);
        void CoordinateSystem(const std::string&, FileLoc loc);
        void CoordSysTransform(const std::string&, FileLoc loc);
        void ActiveTransformAll(FileLoc loc);
        void ActiveTransformEndTime(FileLoc loc);
        void ActiveTransformStartTime(FileLoc loc);
        void TransformTimes(Float start, Float end, FileLoc loc);
        void ColorSpace(const std::string& n, FileLoc loc);
        void PixelFilter(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void Film(const std::string& type, ParsedParameterVector params, FileLoc loc);
        void Sampler(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void Accelerator(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void Integrator(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void Camera(const std::string&, ParsedParameterVector params, FileLoc loc);
        void MakeNamedMedium(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void MediumInterface(const std::string& insideName, const std::string& outsideName, FileLoc loc);
        void WorldBegin(FileLoc loc);
        void AttributeBegin(FileLoc loc);
        void AttributeEnd(FileLoc loc);
        void Attribute(const std::string& target, ParsedParameterVector params, FileLoc loc);
        void Texture(const std::string& name, const std::string& type, const std::string& texname, ParsedParameterVector params, FileLoc loc);
        void Material(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void MakeNamedMaterial(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void NamedMaterial(const std::string& name, FileLoc loc);
        void LightSource(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void AreaLightSource(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void Shape(const std::string& name, ParsedParameterVector params, FileLoc loc);
        void ReverseOrientation(FileLoc loc);
        void ObjectBegin(const std::string& name, FileLoc loc);
        void ObjectEnd(FileLoc loc);
        void ObjectInstance(const std::string& name, FileLoc loc);

        void Finish();

    private:
        struct GraphicsState {
            template <typename F>
            void ForActiveTransforms(F func) {
                for (int i = 0; i < MaxTransforms; ++i)
                    if (activeTransformBits & (1 << i)) ctm[i] = func(ctm[i]);
            }

            std::string currentInsideMedium, currentOutsideMedium;

            int currentMaterialIndex = 0;
            std::string currentMaterialName;

            std::string areaLightName;
            ParameterDictionary areaLightParams;
            FileLoc areaLightLoc;

            ParsedParameterVector shapeAttributes;
            ParsedParameterVector lightAttributes;
            ParsedParameterVector materialAttributes;
            ParsedParameterVector mediumAttributes;
            ParsedParameterVector textureAttributes;
            bool reverseOrientation         = false;
            const RGBColorSpace* colorSpace = RGBColorSpace::sRGB;
            TransformSet ctm;
            uint32_t activeTransformBits = AllTransformsBits;
            Float transformStartTime = 0, transformEndTime = 1;
        };

        spectra::Transform RenderFromObject(int index) const {
            return spectra::Transform((renderFromWorld * graphicsState.ctm[index]).GetMatrix());
        }

        AnimatedTransform RenderFromObject() const {
            return {RenderFromObject(0), graphicsState.transformStartTime, RenderFromObject(1), graphicsState.transformEndTime};
        }

        bool CTMIsAnimated() const {
            return graphicsState.ctm.IsAnimated();
        }

        void RequireOptions(std::string_view command, const FileLoc& loc) const;
        void RequireWorld(std::string_view command, const FileLoc& loc) const;

        enum class ScenePhase { Options, World };
        enum class ScopeKind { Attribute, Object };

        struct Scope {
            ScopeKind kind;
            FileLoc loc;
        };

        Scene* scene;

        ScenePhase currentPhase = ScenePhase::Options;
        GraphicsState graphicsState;
        static constexpr int StartTransformBits = 1 << 0;
        static constexpr int EndTransformBits   = 1 << 1;
        static constexpr int AllTransformsBits  = (1 << MaxTransforms) - 1;
        std::map<std::string, TransformSet> namedCoordinateSystems;
        spectra::Transform renderFromWorld;
        InternCache<spectra::Transform> transformCache;
        std::vector<GraphicsState> pushedGraphicsStates;
        std::vector<Scope> pushStack;
        std::optional<InstanceDefinitionSceneEntity> activeInstanceDefinition{};

        std::vector<ShapeSceneEntity> shapes;
        std::vector<InstanceSceneEntity> instanceUses;

        std::set<std::string> namedMaterialNames, mediumNames;
        std::set<std::string> floatTextureNames, spectrumTextureNames, instanceNames;
        std::optional<Point2i> filmResolutionOverride{};
        bool filmSeen = false;
        SceneEntity sampler;
        SceneEntity film, integrator, filter, accelerator;
        CameraSceneEntity camera;
    };

    struct BuiltScene {
        std::unique_ptr<Scene> scene{};
        std::unique_ptr<SceneBuilder> builder{};
    };

    [[nodiscard]] const SceneInfo& SceneInfoFor(std::string_view name);
    [[nodiscard]] BuiltScene BuildScene(std::string_view name, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::scene

#endif // SPECTRA_SCENE_H
