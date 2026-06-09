export module spectra.scene.pbrt;

export import spectra.scene;
import std;

namespace spectra::scene {
    export inline constexpr std::string_view CornellBoxSceneId = "cornell-box/cornell-box.pbrt";

    export enum class PbrtSceneDirtyFlags : std::uint32_t {
        None     = 0,
        Snapshot = 1,
    };

    export enum class PbrtColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

    export struct PbrtSceneParameter {
        std::string type{};
        std::string name{};
        std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{std::vector<float>{}};
        bool mayBeUnused{false};
        PbrtColorSpace colorSpace{PbrtColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneEntity {
        std::string type{};
        std::vector<PbrtSceneParameter> parameters{};
        PbrtColorSpace colorSpace{PbrtColorSpace::sRGB};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneTransform {
        std::array<float, 16> matrix{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        std::array<float, 16> inverse{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    };

    export struct PbrtSceneTransformSet {
        PbrtSceneTransform start{};
        PbrtSceneTransform end{};
        float startTime{0.0f};
        float endTime{1.0f};
        bool animated{false};
    };

    export struct PbrtSceneOption {
        std::string name{};
        std::string value{};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneMediumInterface {
        std::string inside{};
        std::string outside{};
    };

    export struct PbrtSceneRenderSettings {
        PbrtSceneEntity filter{.type = "gaussian"};
        PbrtSceneEntity film{.type = "rgb"};
        PbrtSceneEntity camera{.type = "perspective"};
        PbrtSceneEntity sampler{.type = "zsobol"};
        PbrtSceneEntity integrator{.type = "volpath"};
        PbrtSceneEntity accelerator{.type = "bvh"};
        PbrtSceneTransformSet cameraTransform{};
        std::string cameraMedium{};
        std::vector<PbrtSceneOption> options{};
    };

    export struct PbrtSceneMaterial {
        std::string name{};
        PbrtSceneEntity entity{};
    };

    export struct PbrtSceneTexture {
        std::string name{};
        std::string kind{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
    };

    export struct PbrtSceneMedium {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
    };

    export struct PbrtSceneLight {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
        std::string medium{};
    };

    export struct PbrtSceneAreaLight {
        PbrtSceneEntity entity{};
    };

    export struct PbrtSceneShape {
        std::string name{};
        PbrtSceneEntity entity{};
        PbrtSceneTransformSet transform{};
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<PbrtSceneAreaLight> areaLight{};
        PbrtSceneMediumInterface mediumInterface{};
    };

    export struct PbrtSceneObjectDefinition {
        std::string name{};
        std::vector<PbrtSceneShape> shapes{};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneObjectInstance {
        std::string name{};
        std::string definitionName{};
        PbrtSceneTransformSet transform{};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneSnapshot {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        PbrtSceneRenderSettings renderSettings{};
        std::vector<PbrtSceneMaterial> materials{};
        std::vector<PbrtSceneTexture> textures{};
        std::vector<PbrtSceneMedium> media{};
        std::vector<PbrtSceneLight> lights{};
        std::vector<PbrtSceneShape> shapes{};
        std::vector<PbrtSceneObjectDefinition> objectDefinitions{};
        std::vector<PbrtSceneObjectInstance> objectInstances{};
    };

    export struct PbrtSceneEditBatch {
        SceneRevision beforeRevision{};
        SceneRevision afterRevision{};
        PbrtSceneDirtyFlags dirty{PbrtSceneDirtyFlags::None};
    };

    export class PbrtSceneEditBuilder {
    public:
        void replaceSnapshot(PbrtSceneSnapshot snapshot, PbrtSceneDirtyFlags dirty);

    private:
        std::optional<PbrtSceneSnapshot> replacement{};
        PbrtSceneDirtyFlags dirty{PbrtSceneDirtyFlags::None};

        friend class PbrtSceneWorkspace;
    };

    export class PbrtSceneWorkspace {
    public:
        PbrtSceneWorkspace() = default;
        explicit PbrtSceneWorkspace(PbrtSceneSnapshot snapshot);

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] std::shared_ptr<const PbrtSceneSnapshot> snapshot() const;
        [[nodiscard]] PbrtSceneEditBatch commit(PbrtSceneEditBuilder edit);
        [[nodiscard]] PbrtSceneEditBatch changes_since(SceneRevision revision) const;

    private:
        [[nodiscard]] PbrtSceneEditBatch fullEdit(SceneRevision before) const;

        std::shared_ptr<const PbrtSceneSnapshot> currentSnapshot{};
        std::optional<PbrtSceneEditBatch> lastEdit{};
    };

    export struct PbrtSceneInfo {
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

    export struct PbrtSceneDiagnostic {
        SceneSourceLocation source{};
        std::string message{};
    };

    export enum class PbrtSceneProbeFeatureCategory {
        PixelFilter,
        Film,
        Camera,
        Sampler,
        Integrator,
        Accelerator,
        Material,
        Texture,
        Medium,
        Light,
        AreaLight,
        Shape,
        LightSampler,
        Option,
        AnimatedTransform,
    };

    export struct PbrtSceneProbeFeature {
        PbrtSceneProbeFeatureCategory category{PbrtSceneProbeFeatureCategory::Option};
        std::string type{};
        std::string kind{};
        SceneSourceLocation source{};
    };

    export struct PbrtSceneProbeReport {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        std::vector<PbrtSceneProbeFeature> features{};
        std::vector<PbrtSceneDiagnostic> diagnostics{};
    };

    export struct PbrtSceneTranslationReport {
        std::string target{};
        bool supported{true};
        std::vector<PbrtSceneDiagnostic> diagnostics{};
    };

    export [[nodiscard]] PbrtSceneInfo DescribeScene(const PbrtSceneSnapshot& scene);

    export enum class PbrtSceneCatalogEntryState {
        Pending,
        Candidate,
        NonScene,
        Invalid,
    };

    export struct PbrtSceneCatalogEntry {
        std::string id{};
        std::string displayName{};
        std::string group{};
        std::filesystem::path relativePath{};
        std::filesystem::path sourcePath{};
        PbrtSceneCatalogEntryState state{PbrtSceneCatalogEntryState::Pending};
        SceneRevision revision{};
        std::optional<PbrtSceneProbeReport> probe{};
        std::vector<PbrtSceneDiagnostic> issues{};
    };

    export struct PbrtSceneCatalog {
        std::filesystem::path root{};
        std::vector<PbrtSceneCatalogEntry> entries{};
        std::size_t pending_count{};
        std::size_t candidate_count{};
        std::size_t non_scene_count{};
        std::size_t invalid_count{};
    };

    export class PbrtCatalogSession final {
    public:
        explicit PbrtCatalogSession(std::string initial_scene_id);
        ~PbrtCatalogSession() noexcept;

        PbrtCatalogSession(const PbrtCatalogSession& other) = delete;
        PbrtCatalogSession(PbrtCatalogSession&& other) = delete;
        PbrtCatalogSession& operator=(const PbrtCatalogSession& other) = delete;
        PbrtCatalogSession& operator=(PbrtCatalogSession&& other) = delete;

        void start_background_probe_workers();
        void stop_background_probe_workers() noexcept;
        void stop_background_probe_workers_if_idle() noexcept;

        [[nodiscard]] std::shared_ptr<PbrtSceneWorkspace> workspace() const;
        [[nodiscard]] PbrtSceneCatalog catalog_snapshot() const;
        [[nodiscard]] std::size_t active_scene_index() const;
        [[nodiscard]] PbrtSceneSnapshot parse_scene(std::size_t scene_index) const;
        [[nodiscard]] PbrtSceneEditBatch commit_scene(std::size_t scene_index, PbrtSceneSnapshot snapshot);

    private:
        void run_background_probe_worker(std::stop_token stop_token);
        void refresh_catalog_counts();
        [[nodiscard]] bool has_background_probe_work_locked() const;
        [[nodiscard]] std::optional<std::size_t> next_catalog_probe_index_locked() const;

        std::shared_ptr<PbrtSceneWorkspace> currentWorkspace{};
        mutable std::mutex catalogMutex{};
        std::condition_variable_any backgroundCondition{};
        PbrtSceneCatalog catalog{};
        std::vector<bool> catalogProbeClaimed{};
        std::size_t activeSceneIndex{};
        std::vector<std::jthread> backgroundWorkers{};
    };

    export [[nodiscard]] PbrtSceneCatalog DiscoverPbrtSceneCatalog();
    export [[nodiscard]] PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    export [[nodiscard]] PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    export [[nodiscard]] PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    export [[nodiscard]] PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    export void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry);
    export void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    export [[nodiscard]] PbrtSceneWorkspace BuildPbrtScene(std::string_view name);
} // namespace spectra::scene
