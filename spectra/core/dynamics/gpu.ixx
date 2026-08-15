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

    export struct GpuVolumeFieldView {
        VolumeFieldDescriptor field{};
        std::vector<GpuBufferView> values{};
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
        std::vector<GpuVolumeFieldView> fields{};
    };

    export struct GpuHashGridRadianceFieldUpdate {
        scene::NeuralFieldId neural_field_id{};
        GpuBufferView hash_grid{};
        GpuBufferView density_input{};
        GpuBufferView density_output{};
        GpuBufferView rgb_input{};
        GpuBufferView rgb_hidden{};
        GpuBufferView rgb_output{};
        GpuBufferView occupancy{};
    };

    export struct GpuSceneUpdate {
        std::variant<GpuTriangleMeshUpdate, GpuSphereSetUpdate, GpuInstanceTransformUpdate, GpuFieldUpdate, GpuHashGridRadianceFieldUpdate> data{};
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

    export struct GpuVectorVisualization {
        VisualizationStyle style{};
        GpuBufferView vectors{};
        std::uint32_t count{};
    };

    export struct GpuImageVisualization {
        VisualizationStyle style{};
        ImageDataset image{};
        GpuBufferView pixels{};
    };

    export struct GpuSurfaceVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        std::optional<GpuBufferView> indices{};
        std::optional<GpuBufferView> colors{};
        std::optional<GpuBufferView> scalars{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
    };

    export struct GpuVisualization {
        std::variant<GpuPointVisualization, GpuSegmentVisualization, GpuVectorVisualization, GpuImageVisualization, GpuSurfaceVisualization> data{};
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
        std::vector<GpuSceneUpdate> scene_updates{};
        std::vector<GpuVisualization> visualizations{};
    };
} // namespace spectra::dynamics
