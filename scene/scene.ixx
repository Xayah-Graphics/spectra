export module spectra.scene;

export import :math;
import std;

namespace spectra::scene {
    export struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export enum class SceneDirtyFlags : std::uint32_t {
        None     = 0,
        Timeline = 1u << 0u,
        Frame    = 1u << 1u,
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

    export enum class SceneTimelineMode {
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

    export struct SceneTimeline {
        SceneTimelineMode mode{SceneTimelineMode::Playback};
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
        SceneTimeline timeline{};
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

    export class SceneEditBuilder {
    public:
        void replaceTimeline(SceneTimeline timeline);
        void replaceFrame(SceneFrameSnapshot frame);

    private:
        std::optional<SceneTimeline> timelineReplacement{};
        std::optional<SceneFrameSnapshot> frameReplacement{};
        SceneDirtyFlags dirty{SceneDirtyFlags::None};

        friend class SceneWorkspace;
    };

    export class SceneWorkspace {
    public:
        explicit SceneWorkspace(SceneDocument document);

        [[nodiscard]] SceneRevision revision() const;
        [[nodiscard]] std::shared_ptr<const SceneDocument> document() const;
        [[nodiscard]] SceneTimeline timeline() const;
        [[nodiscard]] SceneResolvedFrame resolved_frame() const;
        [[nodiscard]] SceneDirtyFlags commit(SceneEditBuilder edit);

    private:
        SceneRevision currentRevision{};
        std::shared_ptr<const SceneDocument> currentDocument{};
        SceneTimeline currentTimeline{};
    };

    export struct SceneFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    export [[nodiscard]] FrameCursor MakeFrameCursor(const SceneFrameInfo& info);

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

    export [[nodiscard]] PbrtSceneInfo DescribeScene(const PbrtSceneSnapshot& scene);

    export [[nodiscard]] PbrtSceneSnapshot ParsePbrtScene(std::string_view scene_id);

    export [[nodiscard]] SceneDocument MakePreviewSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene);
    export [[nodiscard]] SceneDocument LoadPreviewSceneDocumentFromPbrt(std::string_view scene_id);
} // namespace spectra::scene

