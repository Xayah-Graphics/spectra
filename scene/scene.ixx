export module spectra.scene;

import std;

namespace spectra::scene {
    export struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export enum class SceneDirtyFlags : std::uint32_t {
        None     = 0,
        Document = 1u << 0u,
        Timeline = 1u << 1u,
        Frame    = 1u << 2u,
    };

    export [[nodiscard]] constexpr SceneDirtyFlags operator|(const SceneDirtyFlags lhs, const SceneDirtyFlags rhs) {
        return static_cast<SceneDirtyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    export [[nodiscard]] constexpr bool HasSceneDirtyFlag(const SceneDirtyFlags flags, const SceneDirtyFlags flag) {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0u;
    }

    export struct SceneSourceLocation {
        std::string filename{};
        int line{1};
        int column{1};
    };

    export struct Vector3 {
        float x{};
        float y{};
        float z{};
    };

    export struct Vector4 {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct Quaternion {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct Transform {
        Vector3 position{};
        Quaternion rotation{};
        Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    export struct SceneMaterial {
        std::string name{};
        Vector4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
        Vector3 emissionColor{};
        float emissionStrength{};
        float roughness{0.5f};
        float metallic{};
    };

    export enum class SceneLightKind {
        Directional,
        Point,
        Spot,
        Area,
        Environment,
    };

    export struct SceneLight {
        std::string name{};
        SceneLightKind kind{SceneLightKind::Directional};
        Transform transform{};
        Vector3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float coneAngleDegrees{45.0f};
        SceneSourceLocation source{};
    };

    export struct SceneCamera {
        std::string name{};
        Transform transform{};
        Vector3 target{};
        Vector3 up{0.0f, 1.0f, 0.0f};
        float verticalFovDegrees{45.0f};
        float nearPlane{0.01f};
        float farPlane{200.0f};
        SceneSourceLocation source{};
    };

    export struct SceneCameraState {
        Vector3 eye{};
        Vector3 target{};
        Vector3 up{0.0f, 1.0f, 0.0f};
        float verticalFovDegrees{45.0f};
    };

    export struct SceneCameraSnapshot {
        SceneRevision revision{};
        SceneCameraState state{};
    };

    export class SceneCameraWorkspace {
    public:
        SceneCameraWorkspace() = default;

        SceneCameraWorkspace(const SceneCameraWorkspace& other) = delete;
        SceneCameraWorkspace(SceneCameraWorkspace&& other) = delete;
        SceneCameraWorkspace& operator=(const SceneCameraWorkspace& other) = delete;
        SceneCameraWorkspace& operator=(SceneCameraWorkspace&& other) = delete;
        ~SceneCameraWorkspace() = default;

        void ensure_camera(std::string scene_id, SceneCameraState state);
        [[nodiscard]] SceneCameraSnapshot snapshot(std::string_view scene_id) const;
        [[nodiscard]] SceneCameraSnapshot commit(std::string_view scene_id, SceneCameraState state);

    private:
        mutable std::mutex mutex{};
        std::map<std::string, SceneCameraSnapshot> cameras{};
    };

    export struct SceneMesh {
        std::string name{};
        std::vector<Vector3> positions{};
        std::vector<Vector3> normals{};
        std::vector<std::uint32_t> indices{};
        std::string materialName{};
        Transform transform{};
        bool dynamic{false};
        SceneSourceLocation source{};
    };

    export struct SceneParticleSet {
        std::string name{};
        std::vector<Vector3> positions{};
        std::vector<Vector3> velocities{};
        std::vector<float> radii{};
        std::vector<Vector4> colors{};
        std::string materialName{};
        Transform transform{};
        float mass{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct ScenePointCloud {
        std::string name{};
        std::vector<Vector3> positions{};
        std::vector<Vector3> normals{};
        std::vector<Vector4> colors{};
        std::vector<float> radii{};
        std::string materialName{};
        Transform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export enum class SceneVolumeKind {
        LiquidLevelSet,
        GasDensity,
        GasTemperature,
        GasVelocity,
        SignedDistanceField,
        TruncatedSignedDistanceField,
    };

    export enum class SceneVolumeChannelLayout {
        CellCentered,
        FaceX,
        FaceY,
        FaceZ,
    };

    export struct SceneVolumeChannel {
        std::string name{};
        SceneVolumeChannelLayout layout{SceneVolumeChannelLayout::CellCentered};
        std::array<std::uint32_t, 3> dimensions{};
        std::vector<float> values{};
    };

    export struct SceneVolumeGrid {
        std::string name{};
        SceneVolumeKind kind{SceneVolumeKind::GasDensity};
        std::array<std::uint32_t, 3> dimensions{};
        Vector3 origin{};
        Vector3 voxelSize{1.0f, 1.0f, 1.0f};
        std::vector<SceneVolumeChannel> channels{};
        std::string materialName{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export enum class SceneCurveTopology {
        Segments,
        Polyline,
    };

    export struct SceneCurveSet {
        std::string name{};
        SceneCurveTopology topology{SceneCurveTopology::Polyline};
        std::vector<Vector3> points{};
        std::vector<std::uint32_t> curveOffsets{};
        std::vector<float> radii{};
        std::vector<Vector4> colors{};
        std::string materialName{};
        Transform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneSplatSet {
        std::string name{};
        std::vector<Vector3> centers{};
        std::vector<Quaternion> rotations{};
        std::vector<Vector3> scales{};
        std::vector<Vector4> colors{};
        std::vector<float> opacities{};
        std::string materialName{};
        Transform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneLineSet {
        std::string name{};
        std::vector<Vector3> points{};
        std::vector<std::uint32_t> indices{};
        std::vector<Vector4> colors{};
        Transform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export enum class SceneDebugPrimitiveKind {
        Point,
        Sphere,
        Box,
        Axis,
    };

    export struct SceneDebugPrimitive {
        std::string name{};
        SceneDebugPrimitiveKind kind{SceneDebugPrimitiveKind::Point};
        Transform transform{};
        Vector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float size{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneVectorField {
        std::string name{};
        std::vector<Vector3> origins{};
        std::vector<Vector3> vectors{};
        std::vector<Vector4> colors{};
        float scale{1.0f};
        Transform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneCloth {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        Transform transform{};
        float massPerArea{1.0f};
        float stretchStiffness{1.0f};
        float bendStiffness{0.2f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneRigidBody {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        Transform transform{};
        Vector3 linearVelocity{};
        Vector3 angularVelocity{};
        float mass{1.0f};
        bool staticBody{false};
        SceneSourceLocation source{};
    };

    export struct SceneCollider {
        std::string name{};
        std::string meshName{};
        Transform transform{};
        float friction{0.5f};
        float restitution{0.1f};
        bool dynamic{false};
        SceneSourceLocation source{};
    };

    export struct SceneDocument {
        SceneRevision revision{};
        std::string name{};
        std::string title{};
        std::string source{};
        Vector3 gravity{0.0f, -9.8f, 0.0f};
        double framesPerSecond{24.0};
        bool timelineEnabled{true};
        std::optional<SceneCamera> camera{};
        std::vector<SceneMaterial> materials{};
        std::vector<SceneLight> lights{};
        std::vector<SceneMesh> meshes{};
        std::vector<SceneParticleSet> particleSets{};
        std::vector<ScenePointCloud> pointClouds{};
        std::vector<SceneVolumeGrid> volumes{};
        std::vector<SceneCurveSet> curveSets{};
        std::vector<SceneSplatSet> splatSets{};
        std::vector<SceneLineSet> lineSets{};
        std::vector<SceneDebugPrimitive> debugPrimitives{};
        std::vector<SceneVectorField> vectorFields{};
        std::vector<SceneCloth> cloths{};
        std::vector<SceneRigidBody> rigidBodies{};
        std::vector<SceneCollider> colliders{};
    };

    export enum class SimulationTimelineMode {
        Live,
        Record,
        Playback,
    };

    export struct FrameCursor {
        std::uint64_t frameIndex{};
        double timeSeconds{};
    };

    export struct SceneFrameSnapshot {
        SceneRevision revision{};
        FrameCursor cursor{};
        std::vector<SceneMesh> meshes{};
        std::vector<SceneParticleSet> particleSets{};
        std::vector<ScenePointCloud> pointClouds{};
        std::vector<SceneVolumeGrid> volumes{};
        std::vector<SceneCurveSet> curveSets{};
        std::vector<SceneSplatSet> splatSets{};
        std::vector<SceneLineSet> lineSets{};
        std::vector<SceneDebugPrimitive> debugPrimitives{};
        std::vector<SceneVectorField> vectorFields{};
        std::vector<SceneCloth> cloths{};
        std::vector<SceneRigidBody> rigidBodies{};
        std::vector<SceneCollider> colliders{};
    };

    export struct SimulationTimeline {
        SimulationTimelineMode mode{SimulationTimelineMode::Playback};
        double framesPerSecond{24.0};
        bool playing{true};
        bool loop{true};
        FrameCursor cursor{};
        std::uint64_t selectedFrameIndex{};
        std::uint64_t resetRequestSerial{};
        std::uint64_t clearRecordingRequestSerial{};
        std::optional<SceneFrameSnapshot> currentFrame{};
        std::vector<SceneFrameSnapshot> recordedFrames{};
    };

    export struct SceneResolvedFrame {
        SceneRevision revision{};
        std::shared_ptr<const SceneDocument> document{};
        SimulationTimeline timeline{};
        std::optional<SceneFrameSnapshot> frame{};
        std::vector<SceneMesh> meshes{};
        std::vector<SceneParticleSet> particleSets{};
        std::vector<ScenePointCloud> pointClouds{};
        std::vector<SceneVolumeGrid> volumes{};
        std::vector<SceneCurveSet> curveSets{};
        std::vector<SceneSplatSet> splatSets{};
        std::vector<SceneLineSet> lineSets{};
        std::vector<SceneDebugPrimitive> debugPrimitives{};
        std::vector<SceneVectorField> vectorFields{};
        std::vector<SceneCloth> cloths{};
        std::vector<SceneRigidBody> rigidBodies{};
        std::vector<SceneCollider> colliders{};
    };

    export struct SceneEditBatch {
        SceneRevision beforeRevision{};
        SceneRevision afterRevision{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};
    };

    export class SceneEditBuilder {
    public:
        void replaceDocument(SceneDocument document);
        void replaceTimeline(SimulationTimeline timeline);
        void replaceFrame(SceneFrameSnapshot frame);

    private:
        std::optional<SceneDocument> documentReplacement{};
        std::optional<SimulationTimeline> timelineReplacement{};
        std::optional<SceneFrameSnapshot> frameReplacement{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};

        friend class SceneWorkspace;
    };

    export class SceneWorkspace {
    public:
        SceneWorkspace() = default;
        explicit SceneWorkspace(SceneDocument document);

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] SceneRevision revision() const;
        [[nodiscard]] std::shared_ptr<const SceneDocument> document() const;
        [[nodiscard]] SimulationTimeline timeline() const;
        [[nodiscard]] std::optional<SceneFrameSnapshot> frame() const;
        [[nodiscard]] SceneResolvedFrame resolved_frame() const;
        [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit);

    private:
        SceneRevision currentRevision{};
        std::shared_ptr<const SceneDocument> currentDocument{};
        SimulationTimeline currentTimeline{};
    };

    export enum class SceneSourceKind {
        Static,
        Simulation,
    };

    export struct SceneFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    export [[nodiscard]] FrameCursor MakeFrameCursor(const SceneFrameInfo& info);

    export template <typename Adapter>
    concept SceneSimulationAdapter = std::default_initializable<Adapter> && requires(Adapter& adapter, const Adapter& const_adapter, const SceneFrameInfo& frame) {
        { Adapter::project_id() } -> std::convertible_to<std::string_view>;
        { Adapter::project_title() } -> std::convertible_to<std::string_view>;
        { const_adapter.create_document() } -> std::same_as<SceneDocument>;
        { adapter.reset() } -> std::same_as<SceneFrameSnapshot>;
        { adapter.step(frame) } -> std::same_as<SceneFrameSnapshot>;
    };

    export class SceneSourceRuntime {
    public:
        SceneSourceRuntime() = default;

        SceneSourceRuntime(const SceneSourceRuntime& other) = delete;
        SceneSourceRuntime(SceneSourceRuntime&& other) = delete;
        SceneSourceRuntime& operator=(const SceneSourceRuntime& other) = delete;
        SceneSourceRuntime& operator=(SceneSourceRuntime&& other) = delete;
        virtual ~SceneSourceRuntime() noexcept = default;

        [[nodiscard]] virtual std::string_view id() const = 0;
        [[nodiscard]] virtual std::string_view title() const = 0;
        [[nodiscard]] virtual SceneDocument create_document() const = 0;
        [[nodiscard]] virtual SceneFrameSnapshot reset() = 0;
        [[nodiscard]] virtual SceneFrameSnapshot step(const SceneFrameInfo& frame) = 0;
    };

    export template <SceneSimulationAdapter Adapter>
    class SceneSourceRuntimeModel final : public SceneSourceRuntime {
    public:
        SceneSourceRuntimeModel() = default;

        [[nodiscard]] std::string_view id() const override {
            return Adapter::project_id();
        }

        [[nodiscard]] std::string_view title() const override {
            return Adapter::project_title();
        }

        [[nodiscard]] SceneDocument create_document() const override {
            return this->adapter.create_document();
        }

        [[nodiscard]] SceneFrameSnapshot reset() override {
            return this->adapter.reset();
        }

        [[nodiscard]] SceneFrameSnapshot step(const SceneFrameInfo& frame) override {
            return this->adapter.step(frame);
        }

    private:
        Adapter adapter{};
    };

    export struct SceneSourceEntry {
        std::string id{};
        std::string title{};
        SceneSourceKind kind{SceneSourceKind::Static};
        std::move_only_function<SceneDocument()> create_static_document{};
        std::move_only_function<std::unique_ptr<SceneSourceRuntime>()> create_simulation_runtime{};
    };

    export class SceneSourceRegistry final {
    public:
        SceneSourceRegistry() = default;

        SceneSourceRegistry(const SceneSourceRegistry& other) = delete;
        SceneSourceRegistry(SceneSourceRegistry&& other) noexcept = default;
        SceneSourceRegistry& operator=(const SceneSourceRegistry& other) = delete;
        SceneSourceRegistry& operator=(SceneSourceRegistry&& other) noexcept = default;
        ~SceneSourceRegistry() noexcept = default;

        void register_static_scene(std::string id, std::string title, std::move_only_function<SceneDocument()> create_document);

        template <SceneSimulationAdapter Adapter>
        void register_simulation() {
            const std::string id{Adapter::project_id()};
            this->ensure_unique_scene_id(id);
            this->entries.push_back(SceneSourceEntry{
                .id                        = id,
                .title                     = std::string{Adapter::project_title()},
                .kind                      = SceneSourceKind::Simulation,
                .create_simulation_runtime = [] { return std::make_unique<SceneSourceRuntimeModel<Adapter>>(); },
            });
        }

        [[nodiscard]] std::unique_ptr<SceneSourceRuntime> create_simulation_runtime(std::size_t index);
        [[nodiscard]] SceneDocument create_static_document(std::size_t index);
        [[nodiscard]] const SceneSourceEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;

    private:
        void ensure_unique_scene_id(const std::string& id) const;

        std::vector<SceneSourceEntry> entries{};

        friend class SceneSession;
    };

    export class SceneSession final {
    public:
        explicit SceneSession(SceneSourceRegistry registry);

        SceneSession(const SceneSession& other) = delete;
        SceneSession(SceneSession&& other) = delete;
        SceneSession& operator=(const SceneSession& other) = delete;
        SceneSession& operator=(SceneSession&& other) = delete;
        ~SceneSession() noexcept = default;

        [[nodiscard]] std::shared_ptr<SceneWorkspace> active_workspace();
        [[nodiscard]] const SceneSourceEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] std::size_t active_index() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] bool pending_switch() const;

        void request_activate(std::size_t index);
        [[nodiscard]] bool apply_pending_scene();
        void update_active_scene(double delta_seconds);

    private:
        struct SceneSlot {
            std::unique_ptr<SceneSourceRuntime> runtime{};
            std::shared_ptr<SceneWorkspace> workspace{};
            double simulation_accumulator_seconds{};
            double simulation_time_seconds{};
            std::uint64_t simulation_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        [[nodiscard]] SceneSlot& ensure_slot(std::size_t index);
        [[nodiscard]] SceneDocument create_simulation_slot(std::size_t index, SceneSlot* slot);
        void reset_simulation(SceneSlot& slot, SimulationTimeline timeline);

        SceneSourceRegistry registry{};
        std::vector<SceneSlot> slots{};
        std::size_t currentActiveIndex{};
        std::optional<std::size_t> pendingActiveIndex{};
    };

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

    export class PbrtSceneWorkspace {
    public:
        PbrtSceneWorkspace() = default;
        explicit PbrtSceneWorkspace(PbrtSceneSnapshot snapshot);

        [[nodiscard]] bool loaded() const;
        [[nodiscard]] std::shared_ptr<const PbrtSceneSnapshot> snapshot() const;
        [[nodiscard]] PbrtSceneEditBatch replace_snapshot(PbrtSceneSnapshot snapshot);
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

    export class PbrtSceneBrowserSession final {
    public:
        explicit PbrtSceneBrowserSession(std::string initial_scene_id);
        ~PbrtSceneBrowserSession() noexcept;

        PbrtSceneBrowserSession(const PbrtSceneBrowserSession& other) = delete;
        PbrtSceneBrowserSession(PbrtSceneBrowserSession&& other) = delete;
        PbrtSceneBrowserSession& operator=(const PbrtSceneBrowserSession& other) = delete;
        PbrtSceneBrowserSession& operator=(PbrtSceneBrowserSession&& other) = delete;

        void start_background_probe_workers();
        void stop_background_probe_workers() noexcept;
        void stop_background_probe_workers_if_idle() noexcept;

        [[nodiscard]] std::shared_ptr<PbrtSceneWorkspace> workspace() const;
        [[nodiscard]] PbrtSceneCatalog catalog_snapshot() const;
        [[nodiscard]] std::size_t active_scene_index() const;
        [[nodiscard]] std::size_t selected_scene_index() const;
        void select_scene(std::size_t scene_index);
        [[nodiscard]] PbrtSceneSnapshot parse_selected_scene() const;
        [[nodiscard]] PbrtSceneEditBatch commit_selected_scene(PbrtSceneSnapshot snapshot);

    private:
        void run_background_probe_worker(std::stop_token stop_token);
        void refresh_catalog_counts();
        [[nodiscard]] PbrtSceneSnapshot parse_scene(std::size_t scene_index) const;
        [[nodiscard]] PbrtSceneEditBatch commit_scene(std::size_t scene_index, PbrtSceneSnapshot snapshot);
        [[nodiscard]] bool has_background_probe_work_locked() const;
        [[nodiscard]] std::optional<std::size_t> next_catalog_probe_index_locked() const;

        std::shared_ptr<PbrtSceneWorkspace> currentWorkspace{};
        mutable std::mutex catalogMutex{};
        std::condition_variable_any backgroundCondition{};
        PbrtSceneCatalog catalog{};
        std::vector<bool> catalogProbeClaimed{};
        std::size_t activeSceneIndex{};
        std::size_t selectedSceneIndex{};
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

    export [[nodiscard]] SceneDocument MakePreviewSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene);
    export [[nodiscard]] SceneDocument LoadPreviewSceneDocumentFromPbrt(std::string_view scene_id);
} // namespace spectra::scene

