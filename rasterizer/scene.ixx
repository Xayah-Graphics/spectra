export module spectra.rasterizer.scene;

export import spectra.rasterizer.common;
export import spectra.rasterizer.mesh;
export import spectra.rasterizer.particles;
export import spectra.rasterizer.volume;
export import spectra.rasterizer.curves;
export import spectra.rasterizer.splats;
export import spectra.rasterizer.debug;

import std;

namespace spectra::rasterizer {
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
