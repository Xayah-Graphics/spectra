export module spectra.dynamics;

import spectra.scene;
import std;

namespace spectra::dynamics {
    export enum class ParameterApplication : std::uint32_t {
        Live,
        Reset,
        Recreate,
    };

    export enum class MeshUpdateMode : std::uint32_t {
        Deformable,
    };

    export struct VolumeFieldDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        scene::VolumeFieldKind kind{scene::VolumeFieldKind::Float};
        scene::VolumeFieldSampling sampling{scene::VolumeFieldSampling::Cell};
        scene::VolumeVectorSpace vector_space{scene::VolumeVectorSpace::Local};
        std::uint32_t buffer_offset{};
        std::uint32_t buffer_count{1u};
    };

    export struct TriangleMeshDataset {
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
        std::uint32_t attributes{};
    };

    export struct SphereSetDataset {
        std::uint32_t capacity{};
    };

    export struct InstanceTransformDataset {
        std::uint32_t capacity{};
    };

    export struct PointDataset {
        std::uint32_t capacity{};
    };

    export struct SegmentDataset {
        std::uint32_t capacity{};
    };

    export struct VectorDataset {
        std::uint32_t capacity{};
    };

    export struct FieldDataset {
        math::UInt3 resolution{};
        std::vector<VolumeFieldDescriptor> fields{};
        math::Transform local_from_grid{};
    };

    export struct ImageDataset {
        std::array<std::uint32_t, 2> extent{};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
    };

    export struct DatasetDescriptor {
        std::string id{};
        std::optional<scene::DynamicSceneResourceKind> resource_kind{};
        std::variant<TriangleMeshDataset, SphereSetDataset, InstanceTransformDataset, PointDataset, SegmentDataset, VectorDataset, FieldDataset, ImageDataset> details{TriangleMeshDataset{}};
    };

    export struct ParameterDescriptor {
        std::string id{};
        std::string name{};
        std::string unit{};
        std::string section_id{};
        std::string description{};
        ParameterApplication application_mode{ParameterApplication::Live};
        scene::DynamicParameterValue value{};
        scene::DynamicParameterValue minimum{};
        scene::DynamicParameterValue maximum{};
        scene::DynamicParameterValue step{};
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
        std::string phase{};
        std::string headline{};
        std::string message{};
        std::vector<std::optional<TelemetryValue>> values{};
        std::deque<TelemetrySample> history{};
    };

    export struct ProviderDescriptor {
        std::string id{};
        std::vector<DatasetDescriptor> datasets{};
        std::vector<ParameterDescriptor> parameters{};
        std::vector<TelemetryDescriptor> telemetry{};
    };

    export struct SimulationTimeline {
        std::uint64_t step{};
        double seconds{};
    };

} // namespace spectra::dynamics
