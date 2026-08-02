export module spectra.render;

import spectra;
import spectra.scene.dynamics;
import spectra.scene;
import std;
import vulkan;

namespace spectra::render {
    export [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path);

    export enum class ImageFileFormat : std::uint8_t {
        Png,
        Exr,
    };

    export struct RenderOutput {
        const GpuImage& image;
        DescriptorHandle sampled_descriptor{};
        vk::ImageLayout layout{};
        vk::PipelineStageFlags2 stage{};
        vk::AccessFlags2 access{};
        float exposure{};
    };

    export struct RenderReadback {
        vk::Extent2D extent{};
        std::uint32_t accumulated_samples{};
        std::vector<scene::Float4> radiance{};
        std::vector<scene::Float3> albedo{};
        std::vector<scene::Float3> shading_normals{};
        std::vector<scene::Float3> geometric_normals{};
        std::vector<scene::Float3> positions{};
        std::vector<float> depths{};
        std::vector<scene::Float2> texture_coordinates{};
        std::vector<std::uint64_t> object_ids{};
        std::vector<std::uint32_t> primitive_ids{};
        std::vector<std::uint64_t> material_ids{};
        std::vector<std::uint8_t> valid{};
    };

    export enum class GpuMeshUpdateMode : std::uint8_t {
        Immutable,
        Deformable,
        TopologyChanging,
    };

    export struct GpuGeometryBinding {
        scene::GeometryId geometry{};
        GpuMeshUpdateMode mode{GpuMeshUpdateMode::Immutable};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
    };

    export enum class GpuGeometryKind : std::uint8_t {
        Triangle,
        Procedural,
    };

    export enum class GpuDrawKind : std::uint8_t {
        Geometry,
        ParticleSet,
    };

    export struct GpuAccelerationStructure {
        GpuBuffer storage{};
        vk::raii::AccelerationStructureKHR structure{nullptr};
        vk::DeviceAddress address{};

        GpuAccelerationStructure()                                               = default;
        GpuAccelerationStructure(GpuAccelerationStructure&&) noexcept            = default;
        GpuAccelerationStructure& operator=(GpuAccelerationStructure&&) noexcept = default;
        GpuAccelerationStructure(const GpuAccelerationStructure&)                = delete;
        GpuAccelerationStructure& operator=(const GpuAccelerationStructure&)     = delete;
    };

    export struct GpuGeometry {
        scene::GeometryId id{};
        GpuMeshUpdateMode mode{GpuMeshUpdateMode::Immutable};
        GpuBuffer positions{};
        GpuBuffer normals{};
        GpuBuffer tangents{};
        GpuBuffer texture_coordinates{};
        GpuBuffer indices{};
        GpuBuffer aabbs{};
        DescriptorHandle positions_descriptor{};
        DescriptorHandle normals_descriptor{};
        DescriptorHandle tangents_descriptor{};
        DescriptorHandle texture_coordinates_descriptor{};
        DescriptorHandle indices_descriptor{};
        GpuAccelerationStructure blas{};
        GpuGeometryKind acceleration_kind{GpuGeometryKind::Triangle};
        std::uint32_t acceleration_primitive_count{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        std::uint32_t attribute_flags{};
        bool gpu_modified{};

        GpuGeometry()                                  = default;
        GpuGeometry(GpuGeometry&&) noexcept            = default;
        GpuGeometry& operator=(GpuGeometry&&) noexcept = default;
        GpuGeometry(const GpuGeometry&)                = delete;
        GpuGeometry& operator=(const GpuGeometry&)     = delete;
    };

    export struct GpuParticleSet {
        scene::ParticleSetId id{};
        GpuBuffer positions{};
        GpuBuffer radii{};
        GpuBuffer velocities{};
        GpuBuffer colors{};
        GpuBuffer temperatures{};
        GpuBuffer materials{};
        DescriptorHandle positions_descriptor{};
        DescriptorHandle radii_descriptor{};
        DescriptorHandle velocities_descriptor{};
        DescriptorHandle colors_descriptor{};
        DescriptorHandle temperatures_descriptor{};
        DescriptorHandle materials_descriptor{};
        std::uint32_t particle_count{};
        std::uint32_t particle_capacity{};
        std::uint32_t attribute_flags{};
        bool gpu_modified{};

        GpuParticleSet()                                     = default;
        GpuParticleSet(GpuParticleSet&&) noexcept            = default;
        GpuParticleSet& operator=(GpuParticleSet&&) noexcept = default;
        GpuParticleSet(const GpuParticleSet&)                = delete;
        GpuParticleSet& operator=(const GpuParticleSet&)     = delete;
    };

    export enum class GpuVolumeField : std::uint8_t {
        Density,
        Temperature,
        EmissionScale,
        SigmaA,
        SigmaS,
        Emission,
        NanoVdbDensity,
        NanoVdbTemperature,
        Count,
    };

    export struct GpuVolume {
        scene::VolumeId id{};
        scene::ResourceRevision revision{};
        scene::UInt3 resolution{};
        std::array<GpuBuffer, static_cast<std::size_t>(GpuVolumeField::Count)> fields{};
        std::array<DescriptorHandle, static_cast<std::size_t>(GpuVolumeField::Count)> descriptors{};
        std::array<bool, static_cast<std::size_t>(GpuVolumeField::Count)> present{};
        std::optional<scene::VolumeRegion> dirty_region{};
        bool gpu_modified{};
    };

    export struct VolumeVectorField {
        scene::VolumeId volume{};
        DescriptorHandle velocity{};
        scene::UInt3 resolution{};
        scene::Bounds3 bounds{};
        scene::Transform transform{};
    };

    export enum class FrozenDataKind : std::uint8_t {
        GeometryPosition,
        GeometryNormal,
        GeometryTangent,
        GeometryTextureCoordinate,
        GeometryIndex,
        ParticlePosition,
        ParticleRadius,
        ParticleVelocity,
        ParticleColor,
        ParticleTemperature,
        ParticleMaterial,
        VolumeField,
    };

    export struct FrozenDataCopy {
        FrozenDataKind kind{};
        std::uint32_t resource{};
        GpuVolumeField volume_field{};
        vk::DeviceSize offset{};
        std::uint64_t count{};
    };

    export struct FrozenScene {
        scene::Scene scene{};
        GpuBuffer readback{};
        std::vector<FrozenDataCopy> copies{};
        std::uint64_t revision{};

        void materialize();
    };

    export struct GpuTextureImage {
        GpuImage image{};
        DescriptorHandle image_descriptor{};
        DescriptorHandle sampler_descriptor{};
    };

    export [[nodiscard]] GpuTextureImage upload_texture_image(Spectra& runtime, const scene::ImageTexture& texture, vk::Format format, vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer* command_buffer = nullptr);
    export struct GpuDraw {
        GpuDrawKind kind{GpuDrawKind::Geometry};
        std::uint32_t resource_index{};
        std::uint32_t instance_index{};
        std::uint32_t scene_instance_index{};
        std::uint32_t prototype_primitive_index{};
    };

    export struct GpuScene {
        scene::Scene state{};

        GpuScene(Spectra& runtime, const scene::Scene& source, const std::filesystem::path& shader_directory, std::span<const GpuGeometryBinding> geometry_bindings = {}, std::span<const std::pair<scene::ParticleSetId, std::uint32_t>> particle_capacities = {}, std::span<const scene::InstanceId> hidden_instances = {});
        ~GpuScene();

        GpuScene(const GpuScene&)            = delete;
        GpuScene(GpuScene&&)                 = delete;
        GpuScene& operator=(const GpuScene&) = delete;
        GpuScene& operator=(GpuScene&&)      = delete;

        [[nodiscard]] scene::SceneChange synchronize(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] scene::SceneChange apply(const scene::dynamics::PublishedFrame& frame, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] const GpuTextureImage& texture_image(const scene::Texture& texture, vk::Format format) const;
        [[nodiscard]] FrozenScene record_frozen_scene(const vk::raii::CommandBuffer& command_buffer, const scene::CameraResource& camera, vk::Extent2D extent, float exposure) const;

    private:
        void begin_external_updates(std::span<const scene::GeometryId> geometries, std::span<const scene::ParticleSetId> particle_sets, std::span<const scene::VolumeId> volumes);
        void end_external_updates(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_geometry(scene::GeometryId geometry, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, std::uint32_t vertex_count, std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_particles(scene::ParticleSetId particles, const GpuBuffer* positions, const GpuBuffer* radii, const GpuBuffer* velocities, const GpuBuffer* colors, const GpuBuffer* temperatures, const GpuBuffer* materials, DescriptorHandle materials_descriptor, std::uint32_t particle_count, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_volume(scene::VolumeId volume, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, std::uint64_t voxel_count, scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_state(const vk::raii::CommandBuffer& command_buffer);

        struct HostVolumeVector {
            scene::VolumeId volume{};
            GpuBuffer buffer{};
            DescriptorHandle descriptor{};
            std::uint64_t capacity{};
        };

        void cache_texture_images(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void generate_dynamic_attributes(GpuGeometry& geometry, bool generate_normals, bool generate_tangents, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuGeometry create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuParticleSet create_particle_set(const scene::ParticleSet& particles, scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr, std::uint32_t capacity = 0);
        [[nodiscard]] GpuVolume create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void update_volumes(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] std::vector<vk::AccelerationStructureInstanceKHR> acceleration_structure_instance_data(scene::SceneView scene);
        void update_bottom_level(GpuGeometry& geometry, const scene::Geometry& source, const vk::raii::CommandBuffer& command_buffer);
        void update_particle_set(GpuParticleSet& particles, const scene::ParticleSet& source, scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] vk::DeviceAddress acquire_scratch(vk::DeviceSize size, bool immediate);
        [[nodiscard]] GpuAccelerationStructure build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, std::uint32_t primitive_count, GpuMeshUpdateMode mode, const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuAccelerationStructure build_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances);

        Spectra* runtime{};
        vk::raii::ShaderEXT dynamic_mesh_clear_shader{nullptr};
        vk::raii::ShaderEXT dynamic_mesh_accumulate_shader{nullptr};
        vk::raii::ShaderEXT dynamic_mesh_normalize_shader{nullptr};
        vk::raii::ShaderEXT particle_material_shader{nullptr};
        GpuBuffer material_lookup{};
        DescriptorHandle material_lookup_descriptor{};
        std::uint32_t material_count{};
        std::map<std::pair<std::string, vk::Format>, std::size_t> texture_image_indices{};
        std::vector<GpuTextureImage> texture_images{};
        std::vector<HostVolumeVector> host_volume_vectors{};
        GpuBuffer acceleration_structure_instances{};
        GpuBuffer immediate_scratch{};
        std::array<GpuBuffer, Spectra::frames_in_flight> frame_scratch{};
        std::array<vk::DeviceSize, Spectra::frames_in_flight> scratch_offsets{};
        std::vector<std::pair<scene::InstanceId, scene::Transform>> instance_placements{};
        scene::SceneRevision uploaded_revision{};
        std::vector<scene::GeometryId> external_geometries{};
        std::vector<scene::ParticleSetId> external_particle_sets{};
        std::vector<scene::VolumeId> external_volumes{};
        std::vector<GpuGeometryBinding> geometry_bindings{};
        bool rebuilt_external_bottom_level{};
        scene::SceneChange binding_changes{scene::SceneChange::None};

    public:
        std::vector<GpuGeometry> geometries{};
        std::vector<GpuParticleSet> particle_sets{};
        std::vector<GpuVolume> volumes{};
        std::vector<VolumeVectorField> volume_vector_fields{};
        std::vector<GpuDraw> draws{};
        std::vector<std::uint32_t> acceleration_draw_indices{};
        std::vector<scene::InstanceId> source_instances{};
        std::vector<scene::InstanceId> acceleration_source_instances{};
        GpuAccelerationStructure top_level_acceleration_structure{};
    };

} // namespace spectra::render
