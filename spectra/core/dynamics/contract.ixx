module;

#include <spectra/plugin_api.h>

export module spectra.dynamics;

import spectra.runtime;
import spectra.scene;
import std;

namespace spectra::dynamics {
    export enum class ParameterApplication : std::uint32_t {
        Live          = static_cast<std::uint32_t>(SpectraPluginParameterApplication::Live),
        ResetRequired = static_cast<std::uint32_t>(SpectraPluginParameterApplication::ResetRequired),
    };

    export enum class DatasetKind : std::uint32_t {
        Mesh                 = static_cast<std::uint32_t>(SpectraPluginDatasetKind::Mesh),
        PointSet             = static_cast<std::uint32_t>(SpectraPluginDatasetKind::PointSet),
        SegmentSet           = static_cast<std::uint32_t>(SpectraPluginDatasetKind::SegmentSet),
        CurveSet             = static_cast<std::uint32_t>(SpectraPluginDatasetKind::CurveSet),
        VectorSet            = static_cast<std::uint32_t>(SpectraPluginDatasetKind::VectorSet),
        Field                = static_cast<std::uint32_t>(SpectraPluginDatasetKind::Field),
        Image                = static_cast<std::uint32_t>(SpectraPluginDatasetKind::Image),
        CameraObservationSet = static_cast<std::uint32_t>(SpectraPluginDatasetKind::CameraObservationSet),
        TransformSet         = static_cast<std::uint32_t>(SpectraPluginDatasetKind::TransformSet),
    };

    export enum class DatasetBufferKind : std::uint32_t {
        MeshPosition          = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::MeshPosition),
        MeshNormal            = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::MeshNormal),
        MeshTangent           = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::MeshTangent),
        MeshTextureCoordinate = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::MeshTextureCoordinate),
        MeshIndex             = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::MeshIndex),
        Point                 = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::Point),
        Segment               = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::Segment),
        Curve                 = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::Curve),
        Vector                = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::Vector),
        FieldChannel          = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::FieldChannel),
        ImagePixel            = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::ImagePixel),
        CameraObservation     = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::CameraObservation),
        Transform             = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::Transform),
        TelemetryValue        = static_cast<std::uint32_t>(SpectraPluginDatasetBufferKind::TelemetryValue),
    };

    export enum class FieldChannelKind : std::uint32_t {
        Float  = static_cast<std::uint32_t>(SpectraPluginFieldChannelKind::Float),
        Float3 = static_cast<std::uint32_t>(SpectraPluginFieldChannelKind::Float3),
    };

    export enum class ImageFormat : std::uint32_t {
        Rgba8Unorm  = static_cast<std::uint32_t>(SpectraPluginImageFormat::Rgba8Unorm),
        Rgba16Float = static_cast<std::uint32_t>(SpectraPluginImageFormat::Rgba16Float),
        Rgba32Float = static_cast<std::uint32_t>(SpectraPluginImageFormat::Rgba32Float),
    };

    export enum class MeshUpdateMode : std::uint32_t {
        Deformable       = static_cast<std::uint32_t>(SpectraPluginMeshUpdateMode::Deformable),
        TopologyChanging = static_cast<std::uint32_t>(SpectraPluginMeshUpdateMode::TopologyChanging),
    };

    export struct FieldChannelDescriptor {
        std::string id{};
        FieldChannelKind kind{FieldChannelKind::Float};
    };

    export struct DatasetBufferDescriptor {
        DatasetBufferKind kind{};
        std::uint32_t channel_index{};
    };

    export struct DatasetDescriptor {
        std::string id{};
        DatasetKind kind{DatasetKind::Mesh};
        std::uint64_t capacity{};
        std::uint64_t secondary_capacity{};
        MeshUpdateMode mesh_update_mode{MeshUpdateMode::Deformable};
        std::vector<DatasetBufferDescriptor> buffers{};
        math::UInt3 resolution{};
        std::vector<FieldChannelDescriptor> field_channels{};
        std::array<std::uint32_t, 2> image_extent{};
        ImageFormat image_format{ImageFormat::Rgba8Unorm};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
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

    export struct SectionDescriptor {
        std::string id{};
        std::string name{};
    };

    export enum class TelemetryKind : std::uint32_t {
        Boolean = static_cast<std::uint32_t>(SpectraPluginTelemetryKind::Boolean),
        Integer = static_cast<std::uint32_t>(SpectraPluginTelemetryKind::Integer),
        Float   = static_cast<std::uint32_t>(SpectraPluginTelemetryKind::Float),
        Float3  = static_cast<std::uint32_t>(SpectraPluginTelemetryKind::Float3),
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
        std::vector<SectionDescriptor> sections{};
        std::vector<TelemetryDescriptor> telemetry{};
    };

    export struct SimulationTimeline {
        std::uint64_t simulation_step{};
        double simulation_seconds{};
    };

    export struct GpuDatasetBufferView {
        DatasetBufferKind kind{};
        std::uint32_t channel_index{};
        const GpuBuffer* buffer{};
        DescriptorHandle descriptor{};
    };

    export struct GpuSceneDatasetView {
        DatasetKind kind{};
        std::variant<scene::GeometryId, scene::SphereSetId, scene::VolumeId> resource_id{};
        std::vector<GpuDatasetBufferView> buffers{};
        std::uint64_t active_count{};
        std::uint64_t secondary_count{};
        math::UInt3 resolution{};
        std::vector<FieldChannelDescriptor> field_channels{};
        std::optional<scene::VolumeRegion> dirty_region{};
        MeshUpdateMode mesh_update_mode{MeshUpdateMode::Deformable};
    };

    export struct GpuVisualizationDatasetView {
        DatasetKind kind{};
        scene::DynamicVisualizationView view{};
        std::vector<GpuDatasetBufferView> buffers{};
        std::uint64_t active_count{};
        std::uint64_t secondary_count{};
        math::UInt3 resolution{};
        std::array<std::uint32_t, 2> image_extent{};
        ImageFormat image_format{ImageFormat::Rgba8Unorm};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
        std::vector<FieldChannelDescriptor> field_channels{};
        math::Transform transform{};
    };

    export struct MeshOutputBinding {
        scene::GeometryId geometry_id{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
    };

    export struct SphereSetOutputBinding {
        scene::SphereSetId sphere_set_id{};
        std::uint32_t capacity{};
    };

    export struct DynamicFrame {
        std::uint64_t simulation_step{};
        double simulation_seconds{};
        std::vector<GpuSceneDatasetView> scene_updates{};
    };
} // namespace spectra::dynamics
