module;

#include "shaders/shader_semantics.h"

export module spectra.render.gpu_scene;

import spectra.dynamics.gpu;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

namespace spectra {
    export inline constexpr std::uint32_t gpu_geometry_attribute_normal             = shader_semantics::gpu_attribute_normal;
    export inline constexpr std::uint32_t gpu_geometry_attribute_tangent            = shader_semantics::gpu_attribute_tangent;
    export inline constexpr std::uint32_t gpu_geometry_attribute_texture_coordinate = shader_semantics::gpu_attribute_texture_coordinate;

    export enum class GpuMeshUpdateMode : std::uint8_t {
        Immutable,
        Deformable,
        TopologyChanging,
    };

    export enum class AccelerationGeometryKind : std::uint8_t {
        Triangle,
        Procedural,
    };

    export enum class GpuScenePrimitiveKind : std::uint8_t {
        Geometry,
        SphereSet,
    };

    export enum class GpuSceneChange : std::uint8_t {
        None      = 0,
        Geometry  = 1 << 0,
        Volume    = 1 << 1,
        Structure = 1 << 2,
        Transform = 1 << 3,
    };

    export [[nodiscard]] constexpr GpuSceneChange operator|(const GpuSceneChange left, const GpuSceneChange right) noexcept {
        return static_cast<GpuSceneChange>(std::to_underlying(left) | std::to_underlying(right));
    }

    export [[nodiscard]] constexpr GpuSceneChange operator&(const GpuSceneChange left, const GpuSceneChange right) noexcept {
        return static_cast<GpuSceneChange>(std::to_underlying(left) & std::to_underlying(right));
    }

    export struct GpuSceneUpdate {
        scene::SceneChange scene_changes{scene::SceneChange::None};
        GpuSceneChange gpu_changes{GpuSceneChange::None};
        std::uint64_t revision{};
        std::uint64_t structure_revision{};
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
        DescriptorLease positions_descriptor{};
        DescriptorLease normals_descriptor{};
        DescriptorLease tangents_descriptor{};
        DescriptorLease texture_coordinates_descriptor{};
        DescriptorLease indices_descriptor{};
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

    export struct GpuSphereSet {
        scene::SphereSetId sphere_set_id{};
        GpuBuffer positions{};
        GpuBuffer radii{};
        GpuBuffer axis_aligned_boxes{};
        DescriptorLease positions_descriptor{};
        DescriptorLease radii_descriptor{};
        DescriptorLease axis_aligned_boxes_descriptor{};
        GpuAccelerationStructure bottom_level_acceleration_structure{};
        std::uint32_t sphere_count{};
        std::uint32_t sphere_capacity{};
        bool cpu_data_stale{};

        GpuSphereSet()                                   = default;
        GpuSphereSet(GpuSphereSet&&) noexcept            = default;
        GpuSphereSet& operator=(GpuSphereSet&&) noexcept = default;
        GpuSphereSet(const GpuSphereSet&)                = delete;
        GpuSphereSet& operator=(const GpuSphereSet&)     = delete;
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
        std::array<DescriptorLease, static_cast<std::size_t>(GpuVolumeField::Count)> descriptors{};
        std::array<bool, static_cast<std::size_t>(GpuVolumeField::Count)> field_present{};
        std::optional<scene::VolumeRegion> dirty_region{};
        bool cpu_data_stale{};
    };

    export struct GpuTextureImage {
        GpuImage image{};
        DescriptorLease image_descriptor{};
        DescriptorLease sampler_descriptor{};
        std::string cache_revision{};
    };

    export struct GpuScenePrimitive {
        GpuScenePrimitiveKind kind{GpuScenePrimitiveKind::Geometry};
        std::uint32_t resource_index{};
        std::uint32_t scene_primitive_index{};
        std::uint32_t scene_instance_index{};
        std::uint32_t prototype_primitive_index{};
    };

    export struct GpuSceneView {
        std::span<const GpuGeometry> geometries{};
        std::span<const GpuSphereSet> sphere_sets{};
        std::span<const GpuVolume> volumes{};
        std::span<const GpuScenePrimitive> primitives{};
        std::span<const std::uint32_t> acceleration_primitive_indices{};
        std::span<const scene::InstanceId> primitive_instance_ids{};
        std::span<const scene::InstanceId> acceleration_instance_ids{};
        vk::DeviceAddress acceleration_structure{};
        DescriptorHandle primitive_transforms{};
        const GpuBuffer* primitive_transform_buffer{};
        DescriptorHandle instance_bounds{};
        std::span<const math::Bounds3> resolved_instance_bounds{};
        math::Bounds3 resolved_scene_bounds{math::Bounds3::empty()};
        std::uint64_t revision{};
        std::uint64_t structure_revision{};
    };

    export struct GpuScene {
        GpuScene(VulkanRuntime& runtime, std::filesystem::path shader_directory) noexcept;
        ~GpuScene();

        GpuScene(const GpuScene&)            = delete;
        GpuScene(GpuScene&&)                 = delete;
        GpuScene& operator=(const GpuScene&) = delete;
        GpuScene& operator=(GpuScene&&)      = delete;

        void initialize(const scene::Scene& source_scene, std::span<const dynamics::MeshOutputBinding> mesh_bindings = {}, std::span<const dynamics::SphereSetOutputBinding> sphere_set_bindings = {});
        void destroy() noexcept;
        [[nodiscard]] const GpuTextureImage& texture_image(const scene::Texture& texture, vk::Format format) const;
        [[nodiscard]] GpuSceneView view() const noexcept;
        [[nodiscard]] GpuSceneUpdate apply(const dynamics::DynamicSnapshot& snapshot, scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuSceneUpdate synchronize(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);

    private:
        struct {
            VulkanRuntime& runtime;
            std::filesystem::path shader_directory{};
        } context;

        struct {
            vk::raii::ShaderEXT attribute_clear_shader{nullptr};
            vk::raii::ShaderEXT attribute_accumulation_shader{nullptr};
            vk::raii::ShaderEXT attribute_normalization_shader{nullptr};
            vk::raii::ShaderEXT bounds_clear_shader{nullptr};
            vk::raii::ShaderEXT bounds_accumulation_shader{nullptr};
            vk::raii::ShaderEXT sphere_unpack_shader{nullptr};
            vk::raii::ShaderEXT instance_apply_shader{nullptr};
            std::map<std::pair<scene::TextureId, vk::Format>, std::size_t> texture_image_indices{};
            std::vector<GpuTextureImage> texture_images{};
            GpuBuffer acceleration_structure_instances{};
            DescriptorLease acceleration_structure_instances_descriptor{};
            GpuBuffer primitive_transforms{};
            DescriptorLease primitive_transforms_descriptor{};
            GpuBuffer dynamic_instance_bindings{};
            DescriptorLease dynamic_instance_bindings_descriptor{};
            GpuBuffer instance_bounds{};
            DescriptorLease instance_bounds_descriptor{};
            std::array<GpuBuffer, VulkanFrames::frames_in_flight> instance_bounds_readbacks{};
            std::array<std::uint32_t, VulkanFrames::frames_in_flight> instance_bounds_readback_counts{};
            std::vector<math::Bounds3> resolved_instance_bounds{};
            math::Bounds3 resolved_scene_bounds{math::Bounds3::empty()};
            GpuBuffer immediate_scratch{};
            std::array<GpuBuffer, VulkanFrames::frames_in_flight> frame_scratch{};
            std::array<vk::DeviceSize, VulkanFrames::frames_in_flight> scratch_offsets{};
            scene::SceneRevision synchronized_revision{};
            std::vector<scene::GeometryId> external_geometries{};
            std::vector<scene::SphereSetId> external_sphere_sets{};
            std::vector<scene::VolumeId> external_volumes{};
            std::vector<dynamics::MeshOutputBinding> mesh_bindings{};
            bool external_bottom_level_rebuilt{};
            scene::SceneChange resource_binding_changes{scene::SceneChange::None};
            GpuSceneChange dynamic_changes{GpuSceneChange::None};
            std::uint64_t dynamic_revision{};
            std::uint64_t dynamic_structure_revision{};
            std::vector<GpuGeometry> geometries{};
            std::vector<GpuSphereSet> sphere_sets{};
            std::vector<GpuVolume> volumes{};
            std::vector<GpuScenePrimitive> primitives{};
            std::vector<std::uint32_t> acceleration_primitive_indices{};
            std::vector<scene::InstanceId> primitive_instance_ids{};
            std::vector<scene::InstanceId> acceleration_instance_ids{};
            GpuAccelerationStructure top_level_acceleration_structure{};
            bool instance_bounds_dirty{};
        } resources;

        void initialize_resources(scene::SceneView scene, std::span<const dynamics::MeshOutputBinding> mesh_bindings, std::span<const dynamics::SphereSetOutputBinding> sphere_set_bindings, const vk::raii::CommandBuffer* command_buffer);
        void cache_texture_images(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuGeometry create_geometry(const scene::Geometry& geometry, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuSphereSet create_sphere_set(const scene::SphereSet& spheres, const vk::raii::CommandBuffer& command_buffer, std::uint32_t capacity = 0);
        [[nodiscard]] GpuVolume create_volume(const scene::Volume& volume, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] std::vector<vk::AccelerationStructureInstanceKHR> acceleration_structure_instance_data(scene::SceneView scene);
        [[nodiscard]] vk::DeviceAddress acquire_acceleration_scratch(vk::DeviceSize size, bool immediate);
        void update_bottom_level(GpuGeometry& geometry, const scene::Geometry& source_geometry, const vk::raii::CommandBuffer& command_buffer);
        void generate_missing_attributes(GpuGeometry& geometry, bool generate_normals, bool generate_tangents, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_geometry(scene::GeometryId geometry_id, const GpuBuffer* positions, const GpuBuffer* normals, const GpuBuffer* tangents, const GpuBuffer* texture_coordinates, const GpuBuffer* indices, std::uint32_t vertex_count, std::uint32_t index_count, const vk::raii::CommandBuffer& command_buffer);
        void update_sphere_set(GpuSphereSet& spheres, const scene::SphereSet& source_spheres, const vk::raii::CommandBuffer& command_buffer);
        void update_sphere_set_acceleration(GpuSphereSet& spheres, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_sphere_set(scene::SphereSetId sphere_set_id, DescriptorHandle spheres_descriptor, std::uint32_t sphere_count, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_instance_transforms(const dynamics::GpuInstanceTransformUpdate& update, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_external_volume(scene::VolumeId volume_id, const GpuBuffer* density, const GpuBuffer* temperature, const GpuBuffer* emission_scale, const GpuBuffer* sigma_a, const GpuBuffer* sigma_s, const GpuBuffer* emission, std::uint64_t voxel_count, scene::VolumeRegion dirty_region, const vk::raii::CommandBuffer& command_buffer);
        void update_volumes(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_instance_state(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void update_instance_bounds(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void resolve_instance_bounds(std::uint32_t frame_slot_index);
        void update_top_level_from_gpu(std::uint32_t instance_count, const vk::raii::CommandBuffer& command_buffer);
        void update_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances, const vk::raii::CommandBuffer& command_buffer);
        void synchronize_scene(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        void begin_external_updates(std::span<const scene::GeometryId> geometry_ids, std::span<const scene::SphereSetId> sphere_set_ids, std::span<const scene::VolumeId> volume_ids);
        void end_external_updates(scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] GpuAccelerationStructure build_bottom_level(const vk::AccelerationStructureGeometryKHR& geometry, std::uint32_t primitive_count, GpuMeshUpdateMode update_mode, const vk::raii::CommandBuffer& command_buffer, std::uint32_t maximum_primitive_count = 0);
        [[nodiscard]] GpuAccelerationStructure build_top_level(std::span<const vk::AccelerationStructureInstanceKHR> instances, std::uint32_t maximum_primitive_count, const vk::raii::CommandBuffer& command_buffer);
    };
} // namespace spectra
