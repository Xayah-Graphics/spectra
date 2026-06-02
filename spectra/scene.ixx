export module spectra.scene;

export import spectra.util.math;
import std;

export extern "C++" {
    namespace spectra::scene {
        struct SceneRevision {
            std::uint64_t value{};

            friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
        };

        struct SceneCameraId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneCameraId&, const SceneCameraId&) = default;
        };

        struct SceneMaterialId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneMaterialId&, const SceneMaterialId&) = default;
        };

        struct SceneTextureId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneTextureId&, const SceneTextureId&) = default;
        };

        struct SceneMediumId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneMediumId&, const SceneMediumId&) = default;
        };

        struct SceneLightId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneLightId&, const SceneLightId&) = default;
        };

        struct SceneShapeId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneShapeId&, const SceneShapeId&) = default;
        };

        struct SceneObjectDefinitionId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneObjectDefinitionId&, const SceneObjectDefinitionId&) = default;
        };

        struct SceneObjectInstanceId {
            std::uint64_t value{};

            friend auto operator<=>(const SceneObjectInstanceId&, const SceneObjectInstanceId&) = default;
        };

        enum class SceneDirtyFlags : std::uint32_t {
            None           = 0,
            Camera         = 1u << 0u,
            Film           = 1u << 1u,
            RenderSettings = 1u << 2u,
            Transform      = 1u << 3u,
            Geometry       = 1u << 4u,
            Material       = 1u << 5u,
            Texture        = 1u << 6u,
            Light          = 1u << 7u,
            Medium         = 1u << 8u,
            Topology       = 1u << 9u,
            CompiledScene  = 1u << 10u,
        };

        [[nodiscard]] SceneDirtyFlags operator|(SceneDirtyFlags left, SceneDirtyFlags right);
        [[nodiscard]] SceneDirtyFlags operator&(SceneDirtyFlags left, SceneDirtyFlags right);
        SceneDirtyFlags& operator|=(SceneDirtyFlags& left, SceneDirtyFlags right);
        [[nodiscard]] bool HasDirtyFlag(SceneDirtyFlags flags, SceneDirtyFlags flag);

        enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };
        enum class TextureKind { Float, Spectrum };
        enum class FilmKind { Rgb };
        enum class CameraKind { Perspective };
        enum class SamplerKind { Halton, ZSobol };
        enum class IntegratorKind { VolPath };
        enum class AcceleratorKind { Bvh };

        struct SceneRgb {
            float r{};
            float g{};
            float b{};
        };

        struct SceneTextureReference {
            SceneTextureId texture{};
        };

        struct SceneFloatInput {
            std::variant<float, SceneTextureReference> value{0.0f};
        };

        struct SceneSpectrumInput {
            std::variant<SceneRgb, std::vector<float>, SceneTextureReference> value{SceneRgb{0.5f, 0.5f, 0.5f}};
        };

        struct SceneFilmSettings {
            FilmKind kind{FilmKind::Rgb};
            std::string filename{};
            std::string sensor{};
            int xResolution{1920};
            int yResolution{1080};
            float iso{100.0f};
            std::optional<float> whiteBalance{};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneCamera {
            SceneCameraId id{};
            CameraKind kind{CameraKind::Perspective};
            math::Transform worldFromCamera{};
            std::optional<SceneMediumId> medium{};
            float fovDegrees{60.0f};
            float shutterOpen{0.0f};
            float shutterClose{1.0f};
        };

        struct SceneSamplerSettings {
            SamplerKind kind{SamplerKind::ZSobol};
            int pixelSamples{16};
        };

        struct SceneIntegratorSettings {
            IntegratorKind kind{IntegratorKind::VolPath};
            std::optional<int> maxDepth{};
        };

        struct SceneAcceleratorSettings {
            AcceleratorKind kind{AcceleratorKind::Bvh};
        };

        struct SceneRenderSettings {
            SceneFilmSettings film{};
            SceneCamera camera{};
            SceneSamplerSettings sampler{};
            SceneIntegratorSettings integrator{};
            SceneAcceleratorSettings accelerator{};
        };

        struct SceneDiffuseMaterial {
            SceneSpectrumInput reflectance{SceneRgb{0.5f, 0.5f, 0.5f}};
        };

        struct SceneCoatedDiffuseMaterial {
            SceneSpectrumInput reflectance{SceneRgb{0.5f, 0.5f, 0.5f}};
            SceneFloatInput roughness{0.001f};
            std::optional<SceneFloatInput> displacement{};
        };

        struct SceneInterfaceMaterial {};

        struct SceneMaterial {
            SceneMaterialId id{};
            std::string name{};
            std::variant<SceneDiffuseMaterial, SceneCoatedDiffuseMaterial, SceneInterfaceMaterial> value{SceneDiffuseMaterial{}};
        };

        struct SceneImageTexture {
            std::string filename{};
            float uScale{1.0f};
            float vScale{1.0f};
        };

        struct SceneConstantFloatTexture {
            float value{};
        };

        struct SceneScaleFloatTexture {
            SceneTextureId scale{};
            SceneTextureId texture{};
        };

        struct SceneTexture {
            SceneTextureId id{};
            TextureKind kind{TextureKind::Spectrum};
            std::string name{};
            std::variant<SceneImageTexture, SceneConstantFloatTexture, SceneScaleFloatTexture> value{SceneImageTexture{}};
            math::Transform worldFromTexture{};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneNanoVdbMedium {
            std::string filename{};
            std::vector<float> sigmaS{};
            std::vector<float> sigmaA{};
            float leScale{1.0f};
            float temperatureCutoff{1.0f};
            float temperatureScale{1.0f};
        };

        struct SceneMedium {
            SceneMediumId id{};
            std::string name{};
            std::variant<SceneNanoVdbMedium> value{SceneNanoVdbMedium{}};
            math::Transform worldFromMedium{};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneInfiniteLight {
            std::string filename{};
            float scale{1.0f};
        };

        struct SceneLight {
            SceneLightId id{};
            std::string name{};
            std::variant<SceneInfiniteLight> value{SceneInfiniteLight{}};
            math::Transform worldFromLight{};
            std::optional<SceneMediumId> medium{};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneDiffuseAreaLight {
            SceneSpectrumInput emission{SceneRgb{1.0f, 1.0f, 1.0f}};
        };

        struct SceneAreaLight {
            std::variant<SceneDiffuseAreaLight> value{SceneDiffuseAreaLight{}};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneSphere {
            float radius{1.0f};
        };

        struct SceneDisk {
            float radius{1.0f};
        };

        struct ScenePlyMesh {
            std::string filename{};
        };

        struct SceneShape {
            SceneShapeId id{};
            std::string name{};
            std::variant<SceneSphere, SceneDisk, ScenePlyMesh> value{SceneSphere{}};
            math::Transform worldFromObject{};
            bool reverseOrientation{false};
            SceneMaterialId material{};
            std::optional<SceneAreaLight> areaLight{};
            std::optional<SceneMediumId> insideMedium{};
            std::optional<SceneMediumId> outsideMedium{};
            ColorSpace colorSpace{ColorSpace::sRGB};
        };

        struct SceneObjectDefinition {
            SceneObjectDefinitionId id{};
            std::string name{};
            std::vector<SceneShape> shapes{};
        };

        struct SceneObjectInstance {
            SceneObjectInstanceId id{};
            std::string name{};
            SceneObjectDefinitionId definition{};
            math::Transform worldFromInstance{};
        };

        struct SceneSnapshot {
            SceneRevision revision{};
            std::string name{};
            std::string title{};
            std::string source{};
            SceneRenderSettings renderSettings{};
            std::vector<SceneMaterial> materials{};
            std::vector<SceneTexture> textures{};
            std::vector<SceneMedium> media{};
            std::vector<SceneLight> lights{};
            std::vector<SceneShape> shapes{};
            std::vector<SceneObjectDefinition> objectDefinitions{};
            std::vector<SceneObjectInstance> objectInstances{};
        };

        struct SceneEditBatch {
            SceneRevision beforeRevision{};
            SceneRevision afterRevision{};
            SceneDirtyFlags dirty{SceneDirtyFlags::None};
            std::vector<SceneCameraId> cameras{};
            std::vector<SceneMaterialId> materials{};
            std::vector<SceneTextureId> textures{};
            std::vector<SceneMediumId> media{};
            std::vector<SceneLightId> lights{};
            std::vector<SceneShapeId> shapes{};
            std::vector<SceneObjectDefinitionId> objectDefinitions{};
            std::vector<SceneObjectInstanceId> objectInstances{};
        };

        class SceneEditBuilder {
        public:
            void replaceSnapshot(SceneSnapshot snapshot, SceneDirtyFlags dirty);

        private:
            std::optional<SceneSnapshot> replacement{};
            SceneDirtyFlags dirty{SceneDirtyFlags::None};

            friend class SceneWorkspace;
        };

        class SceneWorkspace {
        public:
            SceneWorkspace() = default;
            explicit SceneWorkspace(SceneSnapshot snapshot);

            [[nodiscard]] bool loaded() const;
            [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshot() const;
            [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit);
            [[nodiscard]] SceneEditBatch changes_since(SceneRevision revision) const;

        private:
            void assignMissingIds(SceneSnapshot& snapshot);
            [[nodiscard]] std::uint64_t nextSceneId();
            [[nodiscard]] SceneEditBatch fullEdit(SceneRevision before, const SceneSnapshot& snapshot) const;

            std::shared_ptr<const SceneSnapshot> currentSnapshot{};
            std::optional<SceneEditBatch> lastEdit{};
            std::uint64_t nextId{1};
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

        [[nodiscard]] SceneInfo DescribeScene(const SceneSnapshot& scene);
        [[nodiscard]] SceneWorkspace BuildScene(std::string_view name);
    } // namespace spectra::scene
}
