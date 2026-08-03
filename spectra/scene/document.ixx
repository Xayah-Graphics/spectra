export module spectra.scene:document;

import :model;
import std;

namespace spectra {
    export struct SceneDocument {
        struct {
            bool loaded{};
            scene::Scene source{};
            scene::Scene evaluated{};
            std::filesystem::path path{};
        } content;

        struct {
            scene::Scene* target{};
            scene::SceneChange changes{scene::SceneChange::None};
            bool open{};
        } update;

        void close() noexcept;
        void save();
        void save_as(const std::filesystem::path& scene_path);

        void begin_update(scene::Scene& target_scene) noexcept;
        void commit_update() noexcept;
        void mark_change(scene::Scene& target_scene, scene::SceneChange changes) noexcept;
        void update_dynamic_setup(scene::Scene& target_scene, std::optional<scene::DynamicSetup> setup);
        void update_triangle_mesh(scene::Scene& target_scene, scene::GeometryId geometry_id, std::span<const math::Float3> positions, std::span<const math::Float3> normals, std::span<const math::Float3> tangents, std::span<const math::Float2> texture_coordinates, std::span<const std::uint32_t> indices);
        void update_particle_set(scene::Scene& target_scene, scene::ParticleSetId particle_set_id, std::span<const math::Float3> positions, std::span<const float> radii, std::span<const math::Float3> velocities, std::span<const math::Float3> colors, std::span<const float> temperatures, std::span<const scene::MaterialId> particle_materials);
        void update_density_grid(scene::Scene& target_scene, scene::VolumeId volume_id, scene::VolumeRegion region, std::span<const float> density, std::span<const float> temperature, std::span<const float> emission_scale);
        void update_rgb_grid(scene::Scene& target_scene, scene::VolumeId volume_id, scene::VolumeRegion region, std::span<const math::Float3> sigma_a, std::span<const math::Float3> sigma_s, std::span<const math::Float3> emission);
        void update_transform(scene::Scene& target_scene, scene::InstanceId instance_id, math::Transform transform);
    };
} // namespace spectra
