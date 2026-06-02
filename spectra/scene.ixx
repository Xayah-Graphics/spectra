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

        enum class TextureKind { Float, Spectrum };
        enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

        struct SceneParameter {
            std::string type{};
            std::string name{};
            std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{};
            bool mayBeUnused{false};
        };

        struct SceneParameters {
            ColorSpace colorSpace{ColorSpace::sRGB};
            std::vector<SceneParameter> values{};
        };

        struct SceneComponent {
            std::string type{};
            SceneParameters parameters{};
        };

        struct SceneCamera {
            SceneCameraId id{};
            std::string type{"perspective"};
            SceneParameters parameters{};
            math::Transform worldFromCamera{};
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
            SceneMaterialId id{};
            std::string name{};
            std::string type{};
            SceneParameters parameters{};
        };

        struct SceneTexture {
            SceneTextureId id{};
            TextureKind kind{TextureKind::Spectrum};
            std::string name{};
            std::string type{};
            SceneParameters parameters{};
            math::Transform worldFromTexture{};
        };

        struct SceneMedium {
            SceneMediumId id{};
            std::string name{};
            std::string type{};
            SceneParameters parameters{};
            math::Transform worldFromMedium{};
        };

        struct SceneLight {
            SceneLightId id{};
            std::string type{};
            SceneParameters parameters{};
            math::Transform worldFromLight{};
            std::string medium{};
        };

        struct SceneAreaLight {
            std::string type{};
            SceneParameters parameters{};
        };

        struct SceneShape {
            SceneShapeId id{};
            std::string type{};
            SceneParameters parameters{};
            math::Transform worldFromObject{};
            bool reverseOrientation{false};
            std::string material{};
            std::optional<SceneAreaLight> areaLight{};
            std::string insideMedium{};
            std::string outsideMedium{};
        };

        struct SceneObjectDefinition {
            SceneObjectDefinitionId id{};
            std::string name{};
            std::vector<SceneShape> shapes{};
        };

        struct SceneObjectInstance {
            SceneObjectInstanceId id{};
            std::string name{};
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

            friend class EditableScene;
        };

        class EditableScene {
        public:
            EditableScene() = default;
            explicit EditableScene(SceneSnapshot snapshot);

            [[nodiscard]] bool loaded() const;
            [[nodiscard]] std::shared_ptr<const SceneSnapshot> snapshot() const;
            [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit);

        private:
            void assignMissingIds(SceneSnapshot& snapshot);
            [[nodiscard]] std::uint64_t nextSceneId();

            std::shared_ptr<const SceneSnapshot> currentSnapshot{};
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
        [[nodiscard]] EditableScene BuildScene(std::string_view name);
    } // namespace spectra::scene
}
