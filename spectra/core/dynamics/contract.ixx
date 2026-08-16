export module spectra.dynamics;

import spectra.scene;
import std;

namespace spectra::dynamics {
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

    export struct TriangleMeshDataset {
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        std::uint32_t attributes{};
    };

    export struct SphereSetDataset {
        std::uint32_t capacity{};
    };

    export struct InstanceTransformDataset {
        std::uint32_t capacity{};
    };

    export struct ParticleSetDataset {
        std::uint32_t capacity{};
        float radius{};
        std::vector<FieldDescriptor> fields{};
    };

    export struct SegmentDataset {
        std::uint32_t capacity{};
    };

    export struct VectorDataset {
        std::uint32_t capacity{};
    };

    export struct FieldDataset {
        math::UInt3 resolution{};
        std::vector<FieldDescriptor> fields{};
    };

    export struct ImageDataset {
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

    export struct CameraDataset {
        std::array<std::uint32_t, 2> extent{};
        std::vector<CameraDescriptor> cameras{};
    };

    export struct HashGridRadianceFieldDataset {};

    export struct DatasetDescriptor {
        std::string id{};
        std::variant<TriangleMeshDataset, SphereSetDataset, InstanceTransformDataset, ParticleSetDataset, SegmentDataset, VectorDataset, FieldDataset, ImageDataset, CameraDataset, HashGridRadianceFieldDataset> details;

        template <typename Details>
        DatasetDescriptor(std::string id, Details details) : id(std::move(id)), details(std::move(details)) {}
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
