export module spectra.rasterizer.scene;

import std;

namespace spectra::rasterizer {
    export struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export enum class SceneDirtyFlags : std::uint32_t {
        None            = 0,
        Document        = 1u << 0u,
        Timeline        = 1u << 1u,
        Frame           = 1u << 2u,
        RenderResources = 1u << 3u,
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

    export struct SceneVector3 {
        float x{};
        float y{};
        float z{};
    };

    export struct SceneVector4 {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct SceneQuaternion {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct SceneTransform {
        SceneVector3 position{};
        SceneQuaternion rotation{};
        SceneVector3 scale{1.0f, 1.0f, 1.0f};
    };

    export struct SceneMaterial {
        std::string name{};
        SceneVector4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
        SceneVector3 emissionColor{};
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
        SceneTransform transform{};
        SceneVector3 color{1.0f, 1.0f, 1.0f};
        float intensity{1.0f};
        float coneAngleDegrees{45.0f};
        SceneSourceLocation source{};
    };

    export struct SceneCamera {
        std::string name{};
        SceneTransform transform{};
        SceneVector3 target{};
        float verticalFovDegrees{45.0f};
        float nearPlane{0.01f};
        float farPlane{200.0f};
        SceneSourceLocation source{};
    };

    export struct SceneMesh {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> normals{};
        std::vector<std::uint32_t> indices{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{false};
        SceneSourceLocation source{};
    };

    export struct SceneParticleSet {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> velocities{};
        std::vector<float> radii{};
        std::vector<SceneVector4> colors{};
        std::string materialName{};
        SceneTransform transform{};
        float mass{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct ScenePointCloud {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> normals{};
        std::vector<SceneVector4> colors{};
        std::vector<float> radii{};
        std::string materialName{};
        SceneTransform transform{};
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
        SceneVector3 origin{};
        SceneVector3 voxelSize{1.0f, 1.0f, 1.0f};
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
        std::vector<SceneVector3> points{};
        std::vector<std::uint32_t> curveOffsets{};
        std::vector<float> radii{};
        std::vector<SceneVector4> colors{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneSplatSet {
        std::string name{};
        std::vector<SceneVector3> centers{};
        std::vector<SceneQuaternion> rotations{};
        std::vector<SceneVector3> scales{};
        std::vector<SceneVector4> colors{};
        std::vector<float> opacities{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneLineSet {
        std::string name{};
        std::vector<SceneVector3> points{};
        std::vector<std::uint32_t> indices{};
        std::vector<SceneVector4> colors{};
        SceneTransform transform{};
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
        SceneTransform transform{};
        SceneVector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float size{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneVectorField {
        std::string name{};
        std::vector<SceneVector3> origins{};
        std::vector<SceneVector3> vectors{};
        std::vector<SceneVector4> colors{};
        float scale{1.0f};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneCloth {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        SceneTransform transform{};
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
        SceneTransform transform{};
        SceneVector3 linearVelocity{};
        SceneVector3 angularVelocity{};
        float mass{1.0f};
        bool staticBody{false};
        SceneSourceLocation source{};
    };

    export struct SceneCollider {
        std::string name{};
        std::string meshName{};
        SceneTransform transform{};
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
        SceneVector3 gravity{0.0f, -9.8f, 0.0f};
        double framesPerSecond{24.0};
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
        [[nodiscard]] SceneEditBatch commit(SceneEditBuilder edit);
        [[nodiscard]] SceneEditBatch changes_since(SceneRevision revision) const;

    private:
        [[nodiscard]] SceneEditBatch fullEdit(SceneRevision before) const;

        SceneRevision currentRevision{};
        std::shared_ptr<const SceneDocument> currentDocument{};
        SimulationTimeline currentTimeline{};
        std::optional<SceneEditBatch> lastEdit{};
    };
} // namespace spectra::rasterizer
