export module spectra.render:common;

export import spectra.runtime;
export import spectra.scene;
export import spectra.scene.dynamics;

import std;
import vulkan;

namespace spectra {
    export [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path);

    export enum class CaptureFormat : std::uint8_t {
        Png,
        Exr,
    };

    export struct RenderOutput {
        const GpuImage& image;
        DescriptorHandle sampled_descriptor{};
        vk::ImageLayout image_layout{};
        vk::PipelineStageFlags2 source_stage{};
        vk::AccessFlags2 source_access{};
        float exposure{};
    };

    export struct RenderGBufferReadback {
        vk::Extent2D extent{};
        std::uint32_t accumulated_samples{};
        std::vector<math::Float4> radiance{};
        std::vector<math::Float3> albedo{};
        std::vector<math::Float3> shading_normals{};
        std::vector<math::Float3> geometric_normals{};
        std::vector<math::Float3> positions{};
        std::vector<float> depths{};
        std::vector<math::Float2> texture_coordinates{};
        std::vector<std::uint64_t> object_ids{};
        std::vector<std::uint32_t> primitive_ids{};
        std::vector<std::uint64_t> material_ids{};
        std::vector<std::uint8_t> pixel_validity{};
    };

    export struct RendererDescriptor {
        std::string_view id{};
        std::string_view name{};

        auto operator<=>(const RendererDescriptor&) const = default;
    };

    export struct RenderView {
        scene::Camera camera{};
        vk::Extent2D extent{};
        std::uint64_t camera_revision{};
    };

    export struct RenderProgress {
        std::uint32_t completed{};
        std::uint32_t target{};
        bool paused{};
    };

    export template <typename Type>
    concept SceneRenderer = requires(Type& renderer, const Type& constant_renderer, scene::SceneView scene, scene::SceneChange changes, const RenderView& view, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot) {
        { Type::descriptor } -> std::convertible_to<RendererDescriptor>;
        { Type::renders_visualizations } -> std::convertible_to<bool>;
        { renderer.invalidate(changes) } -> std::same_as<void>;
        { renderer.prepare(scene, view, command_buffer) } -> std::same_as<void>;
        { renderer.record(command_buffer, frame_slot) } -> std::same_as<void>;
        { constant_renderer.output() } -> std::same_as<RenderOutput>;
    };

    export template <typename Type>
    concept ProgressiveSceneRenderer = SceneRenderer<Type> && requires(Type& renderer, const Type& constant_renderer) {
        { constant_renderer.progress() } -> std::same_as<RenderProgress>;
        { renderer.set_paused(true) } -> std::same_as<void>;
        { renderer.reset() } -> std::same_as<void>;
    };

    export template <typename Type>
    concept GBufferSceneRenderer = SceneRenderer<Type> && requires(Type& renderer) {
        { renderer.readback() } -> std::same_as<RenderGBufferReadback>;
    };

    export enum class GpuMeshUpdateMode : std::uint8_t {
        Immutable,
        Deformable,
        TopologyChanging,
    };

    export struct GpuGeometryBinding {
        scene::GeometryId geometry_id{};
        GpuMeshUpdateMode update_mode{GpuMeshUpdateMode::Immutable};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
    };

    export enum class AccelerationGeometryKind : std::uint8_t {
        Triangle,
        Procedural,
    };

    export enum class GpuScenePrimitiveKind : std::uint8_t {
        Geometry,
        ParticleSet,
    };

    export struct GpuAccelerationStructure {
        GpuBuffer storage{};
        vk::raii::AccelerationStructureKHR acceleration_structure{nullptr};
        vk::DeviceAddress address{};

        GpuAccelerationStructure()                                               = default;
        GpuAccelerationStructure(GpuAccelerationStructure&&) noexcept            = default;
        GpuAccelerationStructure& operator=(GpuAccelerationStructure&&) noexcept = default;
        GpuAccelerationStructure(const GpuAccelerationStructure&)                = delete;
        GpuAccelerationStructure& operator=(const GpuAccelerationStructure&)     = delete;
    };

    export struct GpuGeometry {
        scene::GeometryId geometry_id{};
        GpuMeshUpdateMode update_mode{GpuMeshUpdateMode::Immutable};
        GpuBuffer positions{};
        GpuBuffer normals{};
        GpuBuffer tangents{};
        GpuBuffer texture_coordinates{};
        GpuBuffer indices{};
        GpuBuffer axis_aligned_boxes{};
        DescriptorHandle positions_descriptor{};
        DescriptorHandle normals_descriptor{};
        DescriptorHandle tangents_descriptor{};
        DescriptorHandle texture_coordinates_descriptor{};
        DescriptorHandle indices_descriptor{};
        GpuAccelerationStructure bottom_level_acceleration_structure{};
        AccelerationGeometryKind acceleration_kind{AccelerationGeometryKind::Triangle};
        std::uint32_t acceleration_primitive_count{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
        std::uint32_t vertex_capacity{};
        std::uint32_t index_capacity{};
        std::uint32_t attribute_mask{};
        bool cpu_data_stale{};

        GpuGeometry()                                  = default;
        GpuGeometry(GpuGeometry&&) noexcept            = default;
        GpuGeometry& operator=(GpuGeometry&&) noexcept = default;
        GpuGeometry(const GpuGeometry&)                = delete;
        GpuGeometry& operator=(const GpuGeometry&)     = delete;
    };

    export struct GpuParticleSet {
        scene::ParticleSetId particle_set_id{};
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
        std::uint32_t attribute_mask{};
        bool cpu_data_stale{};

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
        scene::VolumeId volume_id{};
        scene::ResourceRevision revision{};
        math::UInt3 resolution{};
        std::array<GpuBuffer, static_cast<std::size_t>(GpuVolumeField::Count)> fields{};
        std::array<DescriptorHandle, static_cast<std::size_t>(GpuVolumeField::Count)> descriptors{};
        std::array<bool, static_cast<std::size_t>(GpuVolumeField::Count)> field_present{};
        std::optional<scene::VolumeRegion> dirty_region{};
        bool cpu_data_stale{};
    };

    export struct GpuVolumeVelocityField {
        scene::VolumeId volume_id{};
        DescriptorHandle velocity_descriptor{};
        math::UInt3 resolution{};
        math::Bounds3 bounds{};
        math::Transform transform{};
    };

    export enum class FrozenSceneReadbackKind : std::uint8_t {
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

    export struct FrozenSceneReadbackRegion {
        FrozenSceneReadbackKind kind{};
        std::uint32_t resource_index{};
        GpuVolumeField volume_field{};
        vk::DeviceSize offset{};
        std::uint64_t element_count{};
    };

    export struct FrozenSceneSnapshot {
        scene::Scene frozen_scene{};
        GpuBuffer readback_buffer{};
        std::vector<FrozenSceneReadbackRegion> readback_regions{};

        void materialize();
    };

    export struct GpuTextureImage {
        GpuImage image{};
        DescriptorHandle image_descriptor{};
        DescriptorHandle sampler_descriptor{};
    };

    export [[nodiscard]] GpuTextureImage upload_texture_image(VulkanRuntime& runtime, const scene::ImageTexture& texture, vk::Format format, vk::PipelineStageFlags2 destination_stages, const vk::raii::CommandBuffer* command_buffer = nullptr);

    export struct GpuScenePrimitive {
        GpuScenePrimitiveKind kind{GpuScenePrimitiveKind::Geometry};
        std::uint32_t resource_index{};
        std::uint32_t scene_primitive_index{};
        std::uint32_t scene_instance_index{};
        std::uint32_t prototype_primitive_index{};
    };

    struct GpuVolumeVelocityStorage {
        scene::VolumeId volume_id{};
        GpuBuffer buffer{};
        DescriptorHandle velocity_descriptor{};
        std::uint64_t capacity{};
    };

    export struct GpuScene {
        GpuScene(VulkanRuntime& runtime, SceneDocument& document, DynamicWorld& dynamics, std::filesystem::path shader_directory) noexcept;
        ~GpuScene();

        GpuScene(const GpuScene&)            = delete;
        GpuScene(GpuScene&&)                 = delete;
        GpuScene& operator=(const GpuScene&) = delete;
        GpuScene& operator=(GpuScene&&)      = delete;

        void initialize(const scene::Scene& source_scene, std::span<const GpuGeometryBinding> geometry_bindings = {}, std::span<const std::pair<scene::ParticleSetId, std::uint32_t>> particle_capacities = {}, std::span<const scene::InstanceId> hidden_instances = {});
        void destroy() noexcept;
        [[nodiscard]] scene::SceneChange synchronize(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] scene::SceneChange apply(const dynamics::DynamicFrame& frame, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] const GpuTextureImage& texture_image(const scene::Texture& texture, vk::Format format) const;
        [[nodiscard]] FrozenSceneSnapshot record_frozen_scene_snapshot(const vk::raii::CommandBuffer& command_buffer, const scene::Camera& camera, vk::Extent2D extent, float exposure);

        struct {
            VulkanRuntime& runtime;
            SceneDocument& document;
            DynamicWorld& dynamics;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXT attribute_clear_shader{nullptr};
            vk::raii::ShaderEXT attribute_accumulation_shader{nullptr};
            vk::raii::ShaderEXT attribute_normalization_shader{nullptr};
            vk::raii::ShaderEXT particle_material_shader{nullptr};
            GpuBuffer particle_material_lookup_buffer{};
            DescriptorHandle particle_material_lookup_descriptor{};
            std::uint32_t particle_material_lookup_count{};
            std::map<std::pair<std::string, vk::Format>, std::size_t> texture_image_indices{};
            std::vector<GpuTextureImage> texture_images{};
            std::vector<GpuVolumeVelocityStorage> volume_velocity_storage{};
            GpuBuffer acceleration_structure_instances{};
            GpuBuffer immediate_scratch{};
            std::array<GpuBuffer, VulkanFrames::frames_in_flight> frame_scratch{};
            std::array<vk::DeviceSize, VulkanFrames::frames_in_flight> scratch_offsets{};
            std::vector<std::pair<scene::InstanceId, math::Transform>> instance_transforms{};
            scene::SceneRevision synchronized_revision{};
            std::vector<scene::GeometryId> external_geometries{};
            std::vector<scene::ParticleSetId> external_particle_sets{};
            std::vector<scene::VolumeId> external_volumes{};
            std::vector<GpuGeometryBinding> geometry_bindings{};
            bool external_bottom_level_rebuilt{};
            scene::SceneChange resource_binding_changes{scene::SceneChange::None};
            std::vector<GpuGeometry> geometries{};
            std::vector<GpuParticleSet> particle_sets{};
            std::vector<GpuVolume> volumes{};
            std::vector<GpuVolumeVelocityField> volume_velocity_fields{};
            std::vector<GpuScenePrimitive> primitives{};
            std::vector<std::uint32_t> acceleration_primitive_indices{};
            std::vector<scene::InstanceId> primitive_instance_ids{};
            std::vector<scene::InstanceId> acceleration_instance_ids{};
            GpuAccelerationStructure top_level_acceleration_structure{};
            bool initialized{};
        } resources;

    private:
        void begin_external_updates(std::span<const scene::GeometryId> geometry_ids, std::span<const scene::ParticleSetId> particle_set_ids, std::span<const scene::VolumeId> volume_ids);
        void end_external_updates(const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_geometry(scene::GeometryId geometry_id, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, std::uint32_t vertex_count, std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_particle_set(scene::ParticleSetId particle_set_id, const GpuBuffer* positions, const GpuBuffer* radii, const GpuBuffer* velocities, const GpuBuffer* colors, const GpuBuffer* temperatures, const GpuBuffer* materials, DescriptorHandle materials_descriptor, std::uint32_t particle_count, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_volume(scene::VolumeId volume_id, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, std::uint64_t voxel_count, scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(const vk::raii::CommandBuffer& command_buffer);
        void cache_texture_images(scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void generate_dynamic_attributes(GpuGeometry& geometry, bool generate_normals, bool generate_tangents, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuGeometry create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuParticleSet create_particle_set(const scene::ParticleSet& particles, scene::SceneView scene, const vk::raii::CommandBuffer* command_buffer = nullptr, std::uint32_t capacity = 0);
        [[nodiscard]] GpuVolume create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer* command_buffer = nullptr);
        void update_volumes(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] std::vector<vk::AccelerationStructureInstanceKHR> acceleration_structure_instance_data(scene::SceneView scene);
        void update_bottom_level(GpuGeometry& geometry, const scene::Geometry& source_geometry, const vk::raii::CommandBuffer& command_buffer);
        void update_particle_set(GpuParticleSet& particles, const scene::ParticleSet& source_particles, scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] vk::DeviceAddress acquire_acceleration_scratch(vk::DeviceSize size, bool immediate);
        [[nodiscard]] GpuAccelerationStructure build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, std::uint32_t primitive_count, GpuMeshUpdateMode update_mode, const vk::raii::CommandBuffer* command_buffer = nullptr);
        [[nodiscard]] GpuAccelerationStructure build_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances);
    };
} // namespace spectra
