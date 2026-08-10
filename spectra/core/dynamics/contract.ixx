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
        TriangleMesh         = static_cast<std::uint32_t>(SpectraPluginDatasetKind::TriangleMesh),
        SphereSet            = static_cast<std::uint32_t>(SpectraPluginDatasetKind::SphereSet),
        InstanceTransformSet = static_cast<std::uint32_t>(SpectraPluginDatasetKind::InstanceTransformSet),
        SceneBoundsSet       = static_cast<std::uint32_t>(SpectraPluginDatasetKind::SceneBoundsSet),
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
        SceneBounds               = static_cast<std::uint32_t>(SpectraPluginBufferSemantic::SceneBounds),
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

    export enum class BoundsDomain : std::uint32_t {
        Local = static_cast<std::uint32_t>(SpectraPluginBoundsDomain::Local),
        World = static_cast<std::uint32_t>(SpectraPluginBoundsDomain::World),
    };

    export enum class BoundResourceKind : std::uint32_t {
        System    = static_cast<std::uint32_t>(SpectraPluginBoundResourceKind::System),
        Geometry  = static_cast<std::uint32_t>(SpectraPluginBoundResourceKind::Geometry),
        SphereSet = static_cast<std::uint32_t>(SpectraPluginBoundResourceKind::SphereSet),
        Instance  = static_cast<std::uint32_t>(SpectraPluginBoundResourceKind::Instance),
        Volume    = static_cast<std::uint32_t>(SpectraPluginBoundResourceKind::Volume),
    };

    export struct SceneBound {
        math::Float3 minimum{};
        BoundResourceKind resource_kind{BoundResourceKind::System};
        math::Float3 maximum{};
        BoundsDomain domain{BoundsDomain::World};
        std::uint64_t resource_id{};
        std::uint64_t reserved{};
        math::Transform world_from_local{};
    };

    export struct FieldChannelDescriptor {
        std::string id{};
        FieldChannelKind kind{FieldChannelKind::Float};
    };

    export struct TriangleMeshDataset {
        std::uint64_t vertex_capacity{};
        std::uint64_t index_capacity{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
        std::uint32_t attributes{};
    };

    export struct SphereSetDataset {
        std::uint64_t capacity{};
    };

    export struct InstanceTransformDataset {
        std::uint64_t capacity{};
    };

    export struct SceneBoundsDataset {
        std::uint64_t capacity{};
        BoundsDomain domain{BoundsDomain::World};
    };

    export struct PointDataset {
        std::uint64_t capacity{};
    };

    export struct SegmentDataset {
        std::uint64_t capacity{};
    };

    export struct CurveDataset {
        std::uint64_t capacity{};
    };

    export struct VectorDataset {
        std::uint64_t capacity{};
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
        std::uint64_t capacity{};
        ImageDataset images{};
    };

    export struct TransformDataset {
        std::uint64_t capacity{};
    };

    export struct DatasetDescriptor {
        std::string id{};
        std::variant<TriangleMeshDataset, SphereSetDataset, InstanceTransformDataset, SceneBoundsDataset, PointDataset, SegmentDataset, CurveDataset, VectorDataset, FieldDataset, ImageDataset, CameraObservationDataset, TransformDataset> details{TriangleMeshDataset{}};
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

    export struct PresentationTimeline {
        std::uint64_t frame{};
        double seconds{};
    };

    export struct GpuBufferView {
        const GpuBuffer* buffer{};
        DescriptorHandle descriptor{};
    };

    export struct GpuFieldChannelView {
        FieldChannelDescriptor channel{};
        GpuBufferView values{};
    };

    export struct GpuTriangleMeshUpdate {
        scene::GeometryId geometry_id{};
        GpuBufferView positions{};
        std::optional<GpuBufferView> normals{};
        std::optional<GpuBufferView> tangents{};
        std::optional<GpuBufferView> texture_coordinates{};
        std::optional<GpuBufferView> indices{};
        std::uint64_t vertex_count{};
        std::uint64_t index_count{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
    };

    export struct GpuSphereSetUpdate {
        scene::SphereSetId sphere_set_id{};
        GpuBufferView spheres{};
        std::uint64_t count{};
    };

    export struct GpuInstanceTransformUpdate {
        GpuBufferView instances{};
        std::uint64_t count{};
    };

    export struct GpuFieldUpdate {
        scene::VolumeId volume_id{};
        math::UInt3 resolution{};
        math::Transform local_from_grid{};
        std::vector<GpuFieldChannelView> channels{};
        std::optional<scene::VolumeRegion> dirty_region{};
    };

    export struct GpuSceneBoundsUpdate {
        GpuBufferView bounds{};
        std::uint64_t count{};
        BoundsDomain domain{BoundsDomain::World};
    };

    export struct GpuSceneUpdate {
        std::variant<GpuTriangleMeshUpdate, GpuSphereSetUpdate, GpuInstanceTransformUpdate, GpuFieldUpdate, GpuSceneBoundsUpdate> data{};
    };

    export struct VisualizationStyle {
        scene::DynamicVisualizationView view{};
        math::Transform transform{};
    };

    export struct GpuPointVisualization {
        VisualizationStyle style{};
        GpuBufferView points{};
        std::uint64_t count{};
    };

    export struct GpuSegmentVisualization {
        VisualizationStyle style{};
        GpuBufferView segments{};
        std::uint64_t count{};
    };

    export struct GpuCurveVisualization {
        VisualizationStyle style{};
        GpuBufferView curves{};
        std::uint64_t count{};
    };

    export struct GpuVectorVisualization {
        VisualizationStyle style{};
        GpuBufferView vectors{};
        std::uint64_t count{};
    };

    export struct GpuFieldVisualization {
        VisualizationStyle style{};
        math::UInt3 resolution{};
        math::Transform local_from_grid{};
        GpuFieldChannelView channel{};
    };

    export struct GpuImageVisualization {
        VisualizationStyle style{};
        ImageDataset image{};
        GpuBufferView pixels{};
    };

    export struct GpuCameraObservationVisualization {
        VisualizationStyle style{};
        CameraObservationDataset dataset{};
        GpuBufferView observations{};
        GpuBufferView images{};
        std::uint64_t count{};
    };

    export struct GpuTransformVisualization {
        VisualizationStyle style{};
        GpuBufferView transforms{};
        std::uint64_t count{};
    };

    export struct GpuSurfaceVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        GpuBufferView indices{};
        GpuBufferView scalars{};
        std::uint64_t vertex_count{};
        std::uint64_t index_count{};
    };

    export struct GpuVisualization {
        std::variant<GpuPointVisualization, GpuSegmentVisualization, GpuCurveVisualization, GpuVectorVisualization, GpuFieldVisualization, GpuImageVisualization, GpuCameraObservationVisualization, GpuTransformVisualization, GpuSurfaceVisualization> data{};
    };

    export struct GpuTelemetryUpdate {
        std::size_t system_index{};
        GpuBufferView values{};
        std::uint64_t value_count{};
        std::string phase{};
        std::string headline{};
        std::string message{};
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
        SimulationTimeline simulation{};
        PresentationTimeline presentation{};
        std::vector<GpuSceneUpdate> scene_updates{};
        std::vector<GpuVisualization> visualizations{};
        std::vector<GpuTelemetryUpdate> telemetry{};
    };
} // namespace spectra::dynamics
