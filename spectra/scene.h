#ifndef SPECTRA_SCENE_H
#define SPECTRA_SCENE_H

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
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
#include <utility>
#include <vector>

namespace spectra::scene {
    // SceneEntity Definition
    struct SceneEntity {
        InternedString name;
        FileLoc loc;
        ParameterDictionary parameters;
        static InternCache<std::string> internedStrings;
    };

    struct TransformedSceneEntity : SceneEntity {
        AnimatedTransform renderFromObject;
    };

    // CameraSceneEntity Definition
    struct CameraSceneEntity : SceneEntity {
        CameraTransform cameraTransform;
        std::string medium;
    };

    struct ShapeSceneEntity : SceneEntity {
        const Transform *renderFromObject = nullptr, *objectFromRender = nullptr;
        bool reverseOrientation = false;
        int materialIndex; // one of these two...  std::variant?
        std::string materialName;
        int lightIndex = -1;
        std::string insideMedium, outsideMedium;
    };

    struct AnimatedShapeSceneEntity : TransformedSceneEntity {
        const Transform* identity = nullptr;
        bool reverseOrientation   = false;
        int materialIndex; // one of these two...  std::variant?
        std::string materialName;
        int lightIndex = -1;
        std::string insideMedium, outsideMedium;
    };

    struct InstanceDefinitionSceneEntity {
        InstanceDefinitionSceneEntity() = default;

        InstanceDefinitionSceneEntity(const std::string& name, FileLoc loc) : name(SceneEntity::internedStrings.Lookup(name)), loc(loc) {}


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
        InstanceSceneEntity() = default;

        InstanceSceneEntity(const std::string& n, FileLoc loc, const AnimatedTransform& renderFromInstanceAnim) : name(SceneEntity::internedStrings.Lookup(n)), loc(loc), renderFromInstanceAnim(new AnimatedTransform(renderFromInstanceAnim)) {
            SPECTRA_CHECK(this->renderFromInstanceAnim->IsAnimated());
        }

        InstanceSceneEntity(const std::string& n, FileLoc loc, const Transform* renderFromInstance) : name(SceneEntity::internedStrings.Lookup(n)), loc(loc), renderFromInstance(renderFromInstance) {}


        InternedString name;
        FileLoc loc;
        AnimatedTransform* renderFromInstanceAnim = nullptr;
        const Transform* renderFromInstance       = nullptr;
    };


    // MaxTransforms Definition
    constexpr int MaxTransforms = 2;

    // TransformSet Definition
    struct TransformSet {
        // TransformSet Public Methods
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

    struct SceneDescriptionFileLocation {
        std::string filename{};
        int line   = 0;
        int column = 0;
    };

    struct SceneDescriptionParameter {
        std::string type{};
        std::string name{};
        SceneDescriptionFileLocation location{};
        std::vector<float> floats{};
        std::vector<int> ints{};
        std::vector<std::string> strings{};
        std::vector<std::uint8_t> bools{};
        bool mayBeUnused = false;
    };

    enum class SceneDescriptionTextureValueType { Unknown, Float, Spectrum };

    struct SceneDescriptionRenderSetting {
        bool present = false;
        std::string type{};
        std::string name{};
        SceneDescriptionFileLocation location{};
        Transform transform{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionTexture {
        std::string name{};
        SceneDescriptionTextureValueType valueType = SceneDescriptionTextureValueType::Unknown;
        std::string implementation{};
        SceneDescriptionFileLocation location{};
        Transform transform{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionMaterial {
        std::string name{};
        std::string type{};
        bool named = false;
        SceneDescriptionFileLocation location{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionMedium {
        std::string name{};
        std::string type{};
        SceneDescriptionFileLocation location{};
        Transform transform{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionMediumBinding {
        std::string inside{};
        std::string outside{};
        SceneDescriptionFileLocation location{};
    };

    struct SceneDescriptionLight {
        std::string type{};
        bool area = false;
        std::string outsideMedium{};
        SceneDescriptionFileLocation location{};
        Transform transform{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionShape {
        std::string type{};
        std::string materialName{};
        int materialIndex = -1;
        std::string insideMedium{};
        std::string outsideMedium{};
        std::string objectDefinitionName{};
        std::string areaLightType{};
        bool reverseOrientation = false;
        bool animatedTransform  = false;
        SceneDescriptionFileLocation location{};
        Transform transform{};
        std::vector<SceneDescriptionParameter> parameters{};
    };

    struct SceneDescriptionObjectDefinition {
        std::string name{};
        SceneDescriptionFileLocation location{};
        std::vector<std::size_t> shapeIndices{};
    };

    struct SceneDescriptionObjectInstance {
        std::string name{};
        bool animatedTransform = false;
        SceneDescriptionFileLocation location{};
        Transform transform{};
    };

    struct SceneDescription {
        SceneDescriptionRenderSetting pixelFilter{};
        SceneDescriptionRenderSetting film{};
        SceneDescriptionRenderSetting sampler{};
        SceneDescriptionRenderSetting accelerator{};
        SceneDescriptionRenderSetting integrator{};
        SceneDescriptionRenderSetting camera{};
        std::vector<SceneDescriptionTexture> textures{};
        std::vector<SceneDescriptionMaterial> materials{};
        std::vector<SceneDescriptionMedium> mediums{};
        std::vector<SceneDescriptionMediumBinding> mediumBindings{};
        std::vector<SceneDescriptionLight> lights{};
        std::vector<SceneDescriptionShape> shapes{};
        std::vector<SceneDescriptionObjectDefinition> objectDefinitions{};
        std::vector<SceneDescriptionObjectInstance> objectInstances{};

        void Clear();
    };

    struct SceneDescriptionBuilderState;

    class SceneDescriptionBuilder {
    public:
        explicit SceneDescriptionBuilder(SceneDescription* description);
        ~SceneDescriptionBuilder();

        SceneDescriptionBuilder(const SceneDescriptionBuilder&)            = delete;
        SceneDescriptionBuilder& operator=(const SceneDescriptionBuilder&) = delete;
        SceneDescriptionBuilder(SceneDescriptionBuilder&&)                 = delete;
        SceneDescriptionBuilder& operator=(SceneDescriptionBuilder&&)      = delete;

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
        void EndOfFiles();

        bool IsImportAllowed() const;
        std::unique_ptr<SceneDescriptionBuilder> CopyForImport();
        void MergeImported(std::unique_ptr<SceneDescriptionBuilder> imported);

    private:
        std::unique_ptr<SceneDescriptionBuilderState> state{};
    };

    // Scene Definition
    class Scene {
    public:
        // Scene Public Methods
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

        // Scene Public Members
        SceneEntity integrator, accelerator;
        const RGBColorSpace* filmColorSpace;
        std::vector<ShapeSceneEntity> shapes;
        std::vector<AnimatedShapeSceneEntity> animatedShapes;
        std::vector<InstanceSceneEntity> instances;
        std::map<InternedString, InstanceDefinitionSceneEntity*> instanceDefinitions;

    private:
        // Scene Private Methods
        Medium GetMedium(const std::string& name, const FileLoc* loc);

        void startLoadingNormalMaps(const ParameterDictionary& parameters);

        // Scene Private Members
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
        int nMissingTextures = 0;

        std::mutex shapeMutex, animatedShapeMutex;
        std::mutex instanceDefinitionMutex, instanceUseMutex;
    };

    // SceneBuilder Definition
    class SceneBuilder {
    public:
        // SceneBuilder Public Methods
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

        void EndOfFiles();

        bool IsImportAllowed() const;
        std::unique_ptr<SceneBuilder> CopyForImport();
        void MergeImported(std::unique_ptr<SceneBuilder> imported);
        void MergeImported(SceneBuilder*);

    private:
        // SceneBuilder::GraphicsState Definition
        struct GraphicsState {
            // GraphicsState Public Methods
            GraphicsState();

            template <typename F>
            void ForActiveTransforms(F func) {
                for (int i = 0; i < MaxTransforms; ++i)
                    if (activeTransformBits & (1 << i)) ctm[i] = func(ctm[i]);
            }

            // GraphicsState Public Members
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

        // SceneBuilder Private Members
        Scene* scene;

        enum class BlockState { OptionsBlock, WorldBlock };

        BlockState currentBlock = BlockState::OptionsBlock;
        GraphicsState graphicsState;
        static constexpr int StartTransformBits = 1 << 0;
        static constexpr int EndTransformBits   = 1 << 1;
        static constexpr int AllTransformsBits  = (1 << MaxTransforms) - 1;
        std::map<std::string, TransformSet> namedCoordinateSystems;
        spectra::Transform renderFromWorld;
        InternCache<spectra::Transform> transformCache;
        std::vector<GraphicsState> pushedGraphicsStates;
        std::vector<std::pair<char, FileLoc>> pushStack; // 'a': attribute, 'o': object
        struct ActiveInstanceDefinition {
            ActiveInstanceDefinition(std::string name, FileLoc loc) : entity(name, loc) {}

            std::mutex mutex;
            std::atomic<int> activeImports{1};
            InstanceDefinitionSceneEntity entity;
            ActiveInstanceDefinition* parent = nullptr;
        };

        ActiveInstanceDefinition* activeInstanceDefinition = nullptr;

        // Buffer these both to avoid mutex contention and so that they are
        // consistently ordered across runs.
        std::vector<ShapeSceneEntity> shapes;
        std::vector<InstanceSceneEntity> instanceUses;
        std::vector<std::unique_ptr<SceneBuilder>> importedBuilders;

        std::set<std::string> namedMaterialNames, mediumNames;
        std::set<std::string> floatTextureNames, spectrumTextureNames, instanceNames;
        std::optional<Point2i> filmResolutionOverride{};
        bool filmSeen            = false;
        int currentMaterialIndex = 0, currentLightIndex = -1;
        SceneEntity sampler;
        SceneEntity film, integrator, filter, accelerator;
        CameraSceneEntity camera;
    };

    void ParseFiles(SceneBuilder* target, pstd::span<const std::string> filenames);
    void ParseString(SceneBuilder* target, std::string str);
    void ParseFiles(SceneDescriptionBuilder* target, pstd::span<const std::string> filenames);
    void ParseString(SceneDescriptionBuilder* target, std::string str);
} // namespace spectra::scene

#endif // SPECTRA_SCENE_H
