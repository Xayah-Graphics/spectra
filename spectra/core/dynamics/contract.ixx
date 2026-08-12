module;

#include <spectra/plugin_api.h>

export module spectra.dynamics;

import spectra.scene;
import std;

namespace spectra::dynamics {
    export [[nodiscard]] inline std::filesystem::path provider_library_filename(const std::string_view provider_id) {
#if defined(_WIN32)
        return std::filesystem::path{std::string{provider_id} + ".spectra-plugin.dll"};
#else
        return std::filesystem::path{std::string{provider_id} + ".spectra-plugin.so"};
#endif
    }

    export enum class ParameterApplication : std::uint32_t {
        Live          = static_cast<std::uint32_t>(SpectraPluginParameterApplication::Live),
        ResetRequired = static_cast<std::uint32_t>(SpectraPluginParameterApplication::ResetRequired),
    };

    export enum class DatasetKind : std::uint32_t {
        TriangleMesh         = static_cast<std::uint32_t>(SpectraPluginDatasetKind::TriangleMesh),
        SphereSet            = static_cast<std::uint32_t>(SpectraPluginDatasetKind::SphereSet),
        InstanceTransformSet = static_cast<std::uint32_t>(SpectraPluginDatasetKind::InstanceTransformSet),
        PointSet             = static_cast<std::uint32_t>(SpectraPluginDatasetKind::PointSet),
        SegmentSet           = static_cast<std::uint32_t>(SpectraPluginDatasetKind::SegmentSet),
        CurveSet             = static_cast<std::uint32_t>(SpectraPluginDatasetKind::CurveSet),
        VectorSet            = static_cast<std::uint32_t>(SpectraPluginDatasetKind::VectorSet),
        Field                = static_cast<std::uint32_t>(SpectraPluginDatasetKind::Field),
        Image                = static_cast<std::uint32_t>(SpectraPluginDatasetKind::Image),
        CameraObservationSet = static_cast<std::uint32_t>(SpectraPluginDatasetKind::CameraObservationSet),
        TransformSet         = static_cast<std::uint32_t>(SpectraPluginDatasetKind::TransformSet),
    };

    export enum class BufferSemantic : std::uint32_t {
        TrianglePosition          = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TrianglePosition),
        TriangleNormal            = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TriangleNormal),
        TriangleTangent           = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TriangleTangent),
        TriangleTextureCoordinate = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TriangleTextureCoordinate),
        TriangleIndex             = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TriangleIndex),
        Sphere                    = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Sphere),
        InstanceTransform         = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::InstanceTransform),
        Point                     = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Point),
        Segment                   = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Segment),
        Curve                     = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Curve),
        Vector                    = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Vector),
        FieldChannel              = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::FieldChannel),
        ImagePixel                = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::ImagePixel),
        CameraObservation         = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::CameraObservation),
        Transform                 = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::Transform),
        TelemetryValue            = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::TelemetryValue),
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

    export enum class TransferFunction : std::uint32_t {
        Linear = static_cast<std::uint32_t>(SpectraPluginTransferFunction::Linear),
        Srgb   = static_cast<std::uint32_t>(SpectraPluginTransferFunction::Srgb),
    };

    export enum class MeshUpdateMode : std::uint32_t {
        Deformable       = static_cast<std::uint32_t>(SpectraPluginMeshUpdateMode::Deformable),
        TopologyChanging = static_cast<std::uint32_t>(SpectraPluginMeshUpdateMode::TopologyChanging),
    };

    export struct FieldChannelDescriptor {
        std::string id{};
        FieldChannelKind kind{FieldChannelKind::Float};
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

    export struct CurveDataset {
        std::uint32_t capacity{};
    };

    export struct VectorDataset {
        std::uint32_t capacity{};
    };

    export struct FieldDataset {
        math::UInt3 resolution{};
        std::vector<FieldChannelDescriptor> channels{};
        math::Transform local_from_grid{};
    };

    export struct ImageDataset {
        std::array<std::uint32_t, 2> extent{};
        ImageFormat format{ImageFormat::Rgba8Unorm};
        scene::SpectrumColorSpace color_space{scene::SpectrumColorSpace::Srgb};
        TransferFunction transfer_function{TransferFunction::Linear};
    };

    export struct CameraObservationDataset {
        std::uint32_t capacity{};
        ImageDataset images{};
    };

    export struct TransformDataset {
        std::uint32_t capacity{};
    };

    export struct DatasetDescriptor {
        std::string id{};
        std::variant<TriangleMeshDataset, SphereSetDataset, InstanceTransformDataset, PointDataset, SegmentDataset, CurveDataset, VectorDataset, FieldDataset, ImageDataset, CameraObservationDataset, TransformDataset> details{TriangleMeshDataset{}};
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
        std::uint64_t step{};
        double seconds{};
    };

} // namespace spectra::dynamics
