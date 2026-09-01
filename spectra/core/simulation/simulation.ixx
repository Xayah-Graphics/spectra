export module spectra.simulation;

import spectra.scene;
import std;

namespace spectra::simulation {
    export enum class ParameterApplication : std::uint32_t {
        Live,
        Reset,
        Recreate,
    };

    export struct FieldDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        scene::FieldKind kind{scene::FieldKind::Float};
        scene::VolumeFieldSampling sampling{scene::VolumeFieldSampling::Cell};
        scene::VolumeVectorSpace vector_space{scene::VolumeVectorSpace::Local};
        std::uint32_t buffer_offset{};
        std::uint32_t buffer_count{1u};
    };

    export struct TriangleMeshOutput {
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        std::uint32_t attributes{};
    };

    export struct MeshFieldOutput {
        std::uint32_t capacity{};
        std::string anchor_id{};
        scene::MeshElementDomain domain{scene::MeshElementDomain::Vertex};
        FieldDescriptor field{};
    };

    export struct IndexedPointOutput {
        std::uint32_t capacity{};
        std::string anchor_id{};
    };

    export struct IndexedSegmentOutput {
        std::uint32_t capacity{};
        std::string anchor_id{};
    };

    export struct SphereSetOutput {
        std::uint32_t capacity{};
    };

    export struct InstanceTransformOutput {
        std::uint32_t capacity{};
    };

    export struct ParticleSetOutput {
        std::uint32_t capacity{};
        float radius{};
        std::vector<FieldDescriptor> fields{};
    };

    export struct SegmentOutput {
        std::uint32_t capacity{};
    };

    export struct VectorOutput {
        std::uint32_t capacity{};
    };

    export struct FieldOutput {
        math::UInt3 resolution{};
        std::vector<FieldDescriptor> fields{};
    };

    export struct ImageOutput {
        std::array<std::uint32_t, 2> extent{};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
    };

    export struct CameraDescriptor {
        math::Float3 right{};
        math::Float3 down{};
        math::Float3 forward{};
        math::Float3 position{};
        math::Float2 focal{};
        math::Float2 principal{};
    };

    export struct CameraOutput {
        std::array<std::uint32_t, 2> extent{};
        std::vector<CameraDescriptor> cameras{};
    };

    export struct HashGridRadianceFieldOutput {};

    export struct OutputDescriptor {
        std::string id{};
        std::optional<scene::SimulationVisualization> default_visualization{};
        std::variant<TriangleMeshOutput, MeshFieldOutput, IndexedPointOutput, IndexedSegmentOutput, SphereSetOutput, InstanceTransformOutput, ParticleSetOutput, SegmentOutput, VectorOutput, FieldOutput, ImageOutput, CameraOutput, HashGridRadianceFieldOutput> details;

        template <typename Details>
        OutputDescriptor(std::string id, std::optional<scene::SimulationVisualization> visualization, Details details) : id(std::move(id)), default_visualization(std::move(visualization)), details(std::move(details)) {}
    };

    export struct ParameterDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        std::string section_id{};
        std::string description{};
        ParameterApplication application_mode{ParameterApplication::Live};
        scene::SimulationParameterValue value{};
        scene::SimulationParameterValue minimum{};
        scene::SimulationParameterValue maximum{};
        scene::SimulationParameterValue step{};
        std::vector<std::string> enumerators{};
    };

    export enum class TelemetryKind : std::uint32_t {
        Boolean,
        Integer,
        Float,
        Float3,
    };

    export struct TelemetryDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        std::string section_id{};
        TelemetryKind kind{TelemetryKind::Float};
        bool plot{};
    };

    export struct TelemetryValue {
        TelemetryKind kind{TelemetryKind::Float};
        std::int64_t integer{};
        std::array<double, 3> floating{};
    };

    export struct TelemetrySample {
        std::uint64_t simulation_step{};
        double simulation_seconds{};
        std::vector<TelemetryValue> values{};
    };

    export struct TelemetrySnapshot {
        std::vector<std::optional<TelemetryValue>> values{};
        std::deque<TelemetrySample> history{};
    };

    export struct ProviderDescriptor {
        std::string id{};
        std::chrono::sys_seconds build_time{};
        std::vector<OutputDescriptor> outputs{};
        std::vector<ParameterDescriptor> parameters{};
        std::vector<TelemetryDescriptor> telemetry{};
    };

    export struct SimulationTimeline {
        std::uint64_t step{};
        double seconds{};
    };

    export struct PresentationSequence {
        std::uint64_t frame_count{};
        double start_seconds{};
        double frame_seconds{};

        friend bool operator==(const PresentationSequence&, const PresentationSequence&) = default;
    };

    export struct PresentationFrame {
        std::uint64_t index{};
        double seconds{};
    };

} // namespace spectra::simulation
