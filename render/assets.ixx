export module spectra.render.assets;

import spectra;
import spectra.scene;
import std;
import vulkan;

namespace spectra::render {
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

        GpuAccelerationStructure() = default;
        GpuAccelerationStructure(GpuAccelerationStructure&&) noexcept = default;
        GpuAccelerationStructure& operator=(GpuAccelerationStructure&&) noexcept = default;
        GpuAccelerationStructure(const GpuAccelerationStructure&) = delete;
        GpuAccelerationStructure& operator=(const GpuAccelerationStructure&) = delete;
    };

    export struct GpuGeometry {
        scene::GeometryId id{};
        scene::GeometryUpdateMode update_mode{scene::GeometryUpdateMode::Static};
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
        GpuGeometryKind acceleration_kind{
            GpuGeometryKind::Triangle};
        std::uint32_t acceleration_primitive_count{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
        std::uint32_t attribute_flags{};

        GpuGeometry() = default;
        GpuGeometry(GpuGeometry&&) noexcept = default;
        GpuGeometry& operator=(GpuGeometry&&) noexcept = default;
        GpuGeometry(const GpuGeometry&) = delete;
        GpuGeometry& operator=(const GpuGeometry&) = delete;
    };

    export struct GpuParticleSet {
        scene::ParticleSetId id{};
        scene::GeometryUpdateMode update_mode{
            scene::GeometryUpdateMode::Static};
        GpuBuffer positions{};
        GpuBuffer radii{};
        GpuBuffer colors{};
        GpuBuffer aabbs{};
        DescriptorHandle positions_descriptor{};
        DescriptorHandle radii_descriptor{};
        DescriptorHandle colors_descriptor{};
        GpuAccelerationStructure blas{};
        std::uint32_t particle_count{};
        std::uint32_t attribute_flags{};

        GpuParticleSet() = default;
        GpuParticleSet(GpuParticleSet&&) noexcept = default;
        GpuParticleSet& operator=(GpuParticleSet&&) noexcept = default;
        GpuParticleSet(const GpuParticleSet&) = delete;
        GpuParticleSet& operator=(const GpuParticleSet&) = delete;
    };

    export struct GpuTextureImage {
        GpuImage image{};
        DescriptorHandle image_descriptor{};
        DescriptorHandle sampler_descriptor{};
    };

    export [[nodiscard]] GpuTextureImage
    upload_texture_image(
        GpuDevice& gpu,
        const scene::ImageTexture& texture,
        vk::Format format,
        vk::PipelineStageFlags2 destination_stages,
        const vk::raii::CommandBuffer* command_buffer = nullptr);
    export struct GpuDraw {
        GpuDrawKind kind{GpuDrawKind::Geometry};
        std::uint32_t resource_index{};
        std::uint32_t instance_index{};
        std::uint32_t scene_instance_index{};
        std::uint32_t prototype_primitive_index{};
    };

    export struct GpuAssetCache {
        GpuAssetCache(GpuDevice& gpu, scene::SceneView scene);
        ~GpuAssetCache();

        GpuAssetCache(const GpuAssetCache&) = delete;
        GpuAssetCache(GpuAssetCache&&) = delete;
        GpuAssetCache& operator=(const GpuAssetCache&) = delete;
        GpuAssetCache& operator=(GpuAssetCache&&) = delete;

        void synchronize(
            scene::SceneView scene,
            const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] const GpuTextureImage& texture_image(
            const scene::Texture& texture,
            vk::Format format) const;

    private:
        void cache_texture_images(
            scene::SceneView scene,
            const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuGeometry create_geometry(
            const scene::Geometry& geometry,
            const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuParticleSet create_particle_set(
            const scene::ParticleSet& particles,
            const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] std::vector<vk::AccelerationStructureInstanceKHR> acceleration_structure_instance_data(scene::SceneView scene);
        void update_bottom_level(
            GpuGeometry& geometry,
            const scene::Geometry& source,
            const vk::raii::CommandBuffer& command_buffer);
        void update_bottom_level(
            GpuParticleSet& particles,
            const scene::ParticleSet& source,
            const vk::raii::CommandBuffer& command_buffer);
        void update_top_level(
            std::span<const vk::AccelerationStructureInstanceKHR> instances,
            const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] vk::DeviceAddress acquire_scratch(
            vk::DeviceSize size,
            bool immediate);
        [[nodiscard]] GpuAccelerationStructure build_bottom_level(
            const vk::AccelerationStructureGeometryKHR& geometry,
            std::uint32_t primitive_count,
            scene::GeometryUpdateMode update_mode,
            const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuAccelerationStructure build_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances);

        GpuDevice* gpu{};
        std::map<
            std::pair<std::string, vk::Format>,
            std::size_t>
            texture_image_indices{};
        std::vector<GpuTextureImage>
            texture_images{};
        std::vector<GpuGeometry> gpu_geometries{};
        std::vector<GpuParticleSet> gpu_particle_sets{};
        std::vector<GpuDraw> gpu_draws{};
        GpuBuffer acceleration_structure_instances{};
        GpuBuffer immediate_scratch{};
        std::array<GpuBuffer, Spectra::frames_in_flight> frame_scratch{};
        std::array<vk::DeviceSize, Spectra::frames_in_flight> scratch_offsets{};
        std::vector<scene::InstanceId> source_instance_ids{};
        scene::SceneRevision uploaded_revision{};
        GpuAccelerationStructure tlas{};

    public:
        const std::vector<GpuGeometry>& geometries;
        const std::vector<GpuParticleSet>& particle_sets;
        const std::vector<GpuDraw>& draws;
        const std::vector<scene::InstanceId>& source_instances;
        const GpuAccelerationStructure& top_level_acceleration_structure;
    };

} // namespace spectra::render
