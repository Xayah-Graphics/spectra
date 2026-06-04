export module spectra.scene;

export import spectra.util.math;
import std;

export extern "C++" {
    namespace spectra::scene {
        struct SceneRevision {
            std::uint64_t value{};

            friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
        };

        enum class SceneDirtyFlags : std::uint32_t {
            None     = 0,
            Snapshot = 1,
        };

        enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

        struct SceneSourceLocation {
            std::string filename{};
            int line{1};
            int column{1};
        };

        struct SceneParameter {
            std::string type{};
            std::string name{};
            std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{std::vector<float>{}};
            bool mayBeUnused{false};
            ColorSpace colorSpace{ColorSpace::sRGB};
            SceneSourceLocation source{};
        };

        struct SceneEntity {
            std::string type{};
            std::vector<SceneParameter> parameters{};
            ColorSpace colorSpace{ColorSpace::sRGB};
            SceneSourceLocation source{};
        };

        struct SceneTransformSet {
            math::Transform start{};
            math::Transform end{};
            float startTime{0.0f};
            float endTime{1.0f};
            bool animated{false};
        };

        struct SceneOption {
            std::string name{};
            std::string value{};
            SceneSourceLocation source{};
        };

        struct SceneMediumInterface {
            std::string inside{};
            std::string outside{};
        };

        struct SceneRenderSettings {
            SceneEntity filter{.type = "gaussian"};
            SceneEntity film{.type = "rgb"};
            SceneEntity camera{.type = "perspective"};
            SceneEntity sampler{.type = "zsobol"};
            SceneEntity integrator{.type = "volpath"};
            SceneEntity accelerator{.type = "bvh"};
            SceneTransformSet cameraTransform{};
            std::string cameraMedium{};
            std::vector<SceneOption> options{};
        };

        struct SceneMaterial {
            std::string name{};
            SceneEntity entity{};
        };

        struct SceneTexture {
            std::string name{};
            std::string kind{};
            SceneEntity entity{};
            SceneTransformSet transform{};
        };

        struct SceneMedium {
            std::string name{};
            SceneEntity entity{};
            SceneTransformSet transform{};
        };

        struct SceneLight {
            std::string name{};
            SceneEntity entity{};
            SceneTransformSet transform{};
            std::string medium{};
        };

        struct SceneAreaLight {
            SceneEntity entity{};
        };

        struct SceneShape {
            std::string name{};
            SceneEntity entity{};
            SceneTransformSet transform{};
            bool reverseOrientation{false};
            std::string materialName{};
            std::optional<SceneAreaLight> areaLight{};
            SceneMediumInterface mediumInterface{};
        };

        struct SceneObjectDefinition {
            std::string name{};
            std::vector<SceneShape> shapes{};
            SceneSourceLocation source{};
        };

        struct SceneObjectInstance {
            std::string name{};
            std::string definitionName{};
            SceneTransformSet transform{};
            SceneSourceLocation source{};
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
            [[nodiscard]] SceneEditBatch fullEdit(SceneRevision before) const;

            std::shared_ptr<const SceneSnapshot> currentSnapshot{};
            std::optional<SceneEditBatch> lastEdit{};
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

        struct SceneDiagnostic {
            SceneSourceLocation source{};
            std::string message{};
        };

        struct SceneTranslationReport {
            std::string target{};
            bool supported{true};
            std::vector<SceneDiagnostic> diagnostics{};
        };

        struct SceneTranslationTarget {
            std::string rendererName{};
            std::function<SceneTranslationReport(const SceneSnapshot&)> analyze{};
        };

        enum class SceneCatalogEntryState {
            Pending,
            Ready,
            Invalid,
        };

        struct SceneCatalogEntry {
            std::string id{};
            std::string displayName{};
            std::string group{};
            std::filesystem::path relativePath{};
            std::filesystem::path sourcePath{};
            SceneCatalogEntryState state{SceneCatalogEntryState::Pending};
            SceneRevision revision{};
            std::optional<SceneInfo> info{};
            std::vector<SceneDiagnostic> issues{};
        };

        struct SceneCatalog {
            std::filesystem::path root{};
            std::vector<SceneCatalogEntry> entries{};
            std::size_t pending_count{};
            std::size_t ready_count{};
            std::size_t invalid_count{};
        };

        [[nodiscard]] SceneInfo DescribeScene(const SceneSnapshot& scene);
        [[nodiscard]] SceneCatalog DiscoverSceneCatalog();
        [[nodiscard]] SceneSnapshot ParseSceneCatalogEntry(const SceneCatalogEntry& entry);
        [[nodiscard]] SceneSnapshot ParseSceneCatalogEntry(const SceneCatalogEntry& entry, std::stop_token stop_token);
        void ValidateSceneCatalogEntry(SceneCatalogEntry& entry);
        void ValidateSceneCatalogEntry(SceneCatalogEntry& entry, std::stop_token stop_token);
        [[nodiscard]] SceneWorkspace BuildScene(std::string_view name);
    } // namespace spectra::scene
}
