export module spectra.dynamics.gpu;

export import spectra.dynamics;
export import spectra.runtime.resources;
import spectra.scene;
import std;

namespace spectra::dynamics {
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
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
        MeshUpdateMode update_mode{MeshUpdateMode::Deformable};
    };

    export struct GpuSphereSetUpdate {
        scene::SphereSetId sphere_set_id{};
        GpuBufferView spheres{};
        std::uint32_t count{};
    };

    export struct GpuInstanceTransformUpdate {
        GpuBufferView instances{};
        std::uint32_t count{};
    };

    export struct GpuFieldUpdate {
        scene::VolumeId volume_id{};
        math::UInt3 resolution{};
        math::Transform local_from_grid{};
        std::vector<GpuFieldChannelView> channels{};
        std::optional<scene::VolumeRegion> dirty_region{};
    };

    export struct GpuSceneUpdate {
        std::variant<GpuTriangleMeshUpdate, GpuSphereSetUpdate, GpuInstanceTransformUpdate, GpuFieldUpdate> data{};
    };

    export struct VisualizationStyle {
        scene::DynamicVisualizationView view{};
        math::Transform transform{};
    };

    export struct GpuPointVisualization {
        VisualizationStyle style{};
        GpuBufferView points{};
        std::uint32_t count{};
    };

    export struct GpuSegmentVisualization {
        VisualizationStyle style{};
        GpuBufferView segments{};
        std::uint32_t count{};
    };

    export struct GpuCurveVisualization {
        VisualizationStyle style{};
        GpuBufferView curves{};
        std::uint32_t count{};
    };

    export struct GpuVectorVisualization {
        VisualizationStyle style{};
        GpuBufferView vectors{};
        std::uint32_t count{};
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
        std::uint32_t count{};
    };

    export struct GpuTransformVisualization {
        VisualizationStyle style{};
        GpuBufferView transforms{};
        std::uint32_t count{};
    };

    export struct GpuSurfaceVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        std::optional<GpuBufferView> indices{};
        std::optional<GpuBufferView> scalars{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
    };

    export struct GpuVisualization {
        std::variant<GpuPointVisualization, GpuSegmentVisualization, GpuCurveVisualization, GpuVectorVisualization, GpuFieldVisualization, GpuImageVisualization, GpuCameraObservationVisualization, GpuTransformVisualization, GpuSurfaceVisualization> data{};
    };

    export struct GpuTelemetryUpdate {
        std::size_t system_index{};
        GpuBufferView values{};
        std::uint32_t value_count{};
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

    export struct DynamicSnapshot {
        SimulationTimeline simulation{};
        std::vector<GpuSceneUpdate> scene_updates{};
        std::vector<GpuVisualization> visualizations{};
        std::vector<GpuTelemetryUpdate> telemetry{};
    };
} // namespace spectra::dynamics
