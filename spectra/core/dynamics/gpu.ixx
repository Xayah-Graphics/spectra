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

    export struct CameraReferenceImage {
        scene::CameraId camera_id{};
        std::string group{};
        std::uint32_t index{};
        std::uint32_t count{};
        std::array<std::uint32_t, 2> extent{};
        math::Float2 focal{};
        math::Float2 principal{};
        GpuBufferView pixels{};
        std::uint32_t layer{};
    };

    export struct GpuFieldView {
        FieldDescriptor field{};
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

    export struct GpuParticleSetUpdate {
        scene::ParticleSetId particle_set_id{};
        GpuBufferView positions{};
        std::vector<GpuFieldView> fields{};
        std::uint32_t count{};
    };

    export struct GpuInstanceTransformUpdate {
        GpuBufferView instances{};
        std::uint32_t count{};
    };

    export struct GpuFieldUpdate {
        scene::VolumeId volume_id{};
        std::vector<GpuFieldView> fields{};
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
        std::variant<GpuTriangleMeshUpdate, GpuSphereSetUpdate, GpuParticleSetUpdate, GpuInstanceTransformUpdate, GpuFieldUpdate, GpuHashGridRadianceFieldUpdate> data{};
    };

    export struct VisualizationStyle {
        scene::DynamicVisualizationView view{};
        math::Transform transform{};
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
        std::variant<GpuSegmentVisualization, GpuVectorVisualization, GpuImageVisualization, GpuSurfaceVisualization> data{};
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
