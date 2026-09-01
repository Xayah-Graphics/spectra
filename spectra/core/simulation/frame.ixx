export module spectra.simulation.frame;

export import spectra.simulation;
export import spectra.runtime.resources;
import spectra.scene;
import std;

namespace spectra::simulation {
    export struct GpuBufferView {
        const runtime::GpuBuffer* buffer{};
        runtime::DescriptorHandle descriptor{};
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
        scene::SimulationVisualization view{};
        math::Transform transform{};
    };

    export struct GpuSegmentVisualization {
        VisualizationStyle style{};
        GpuBufferView segments{};
        std::uint32_t count{};
    };

    export struct GpuIndexedPointVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        GpuBufferView indices{};
        std::uint32_t count{};
    };

    export struct GpuIndexedSegmentVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        GpuBufferView indices{};
        std::uint32_t count{};
    };

    export struct GpuMeshVectorVisualization {
        VisualizationStyle style{};
        GpuBufferView positions{};
        GpuBufferView vectors{};
        std::uint32_t count{};
    };

    export struct GpuVectorVisualization {
        VisualizationStyle style{};
        GpuBufferView vectors{};
        std::uint32_t count{};
    };

    export struct GpuImageVisualization {
        VisualizationStyle style{};
        ImageOutput image{};
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

    export struct GpuMeshFieldSurfaceVisualization {
        VisualizationStyle style{};
        scene::GeometryId geometry_id{};
        std::optional<GpuBufferView> colors{};
        std::optional<GpuBufferView> scalars{};
    };

    export struct GpuDerivedMeshVisualization {
        VisualizationStyle style{};
        scene::GeometryId geometry_id{};
    };

    export struct GpuVisualization {
        std::variant<GpuSegmentVisualization, GpuIndexedPointVisualization, GpuIndexedSegmentVisualization, GpuVectorVisualization, GpuMeshVectorVisualization, GpuImageVisualization, GpuSurfaceVisualization, GpuMeshFieldSurfaceVisualization, GpuDerivedMeshVisualization> data{};
    };

    export struct MeshOutputBinding {
        scene::GeometryId geometry_id{};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        std::optional<GpuBufferView> indices{};
        std::optional<GpuBufferView> texture_coordinates{};
    };

    export struct SphereSetOutputBinding {
        scene::SphereSetId sphere_set_id{};
        std::uint32_t capacity{};
    };

    export struct SimulationFrame {
        std::vector<GpuSceneUpdate> scene_updates{};
        std::vector<GpuVisualization> visualizations{};
    };
} // namespace spectra::simulation
