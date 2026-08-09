module spectra.scene.document;

import spectra.scene.format;
import std;

namespace spectra {
    namespace {
        template <class Element>
        void update_grid_region(std::vector<Element>& destination, const math::UInt3 resolution, const scene::VolumeRegion region, const std::span<const Element> source) {
            if (source.empty()) return;
            const std::uint32_t width  = region.maximum.x - region.minimum.x;
            const std::uint32_t height = region.maximum.y - region.minimum.y;
            for (std::uint32_t z = 0; z != region.maximum.z - region.minimum.z; ++z)
                for (std::uint32_t y = 0; y != height; ++y) {
                    const std::size_t destination_offset = (static_cast<std::size_t>(region.minimum.z + z) * resolution.y + region.minimum.y + y) * resolution.x + region.minimum.x;
                    const std::size_t source_offset      = (static_cast<std::size_t>(z) * height + y) * width;
                    std::ranges::copy(source.subspan(source_offset, width), destination.begin() + destination_offset);
                }
        }

        void include_dirty_region(scene::Volume& volume, const scene::VolumeRegion region) {
            if (!volume.dirty_region) {
                volume.dirty_region = region;
                return;
            }
            volume.dirty_region = {
                {
                    std::min(volume.dirty_region->minimum.x, region.minimum.x),
                    std::min(volume.dirty_region->minimum.y, region.minimum.y),
                    std::min(volume.dirty_region->minimum.z, region.minimum.z),
                },
                {
                    std::max(volume.dirty_region->maximum.x, region.maximum.x),
                    std::max(volume.dirty_region->maximum.y, region.maximum.y),
                    std::max(volume.dirty_region->maximum.z, region.maximum.z),
                },
            };
        }

        void generate_normals(scene::TriangleMeshGeometry& mesh) {
            mesh.normals.assign(mesh.positions.size(), math::Float3{});
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
                const std::uint32_t vertex_0 = mesh.indices[index];
                const std::uint32_t vertex_1 = mesh.indices[index + 1];
                const std::uint32_t vertex_2 = mesh.indices[index + 2];
                const math::Float3 normal    = (mesh.positions[vertex_1] - mesh.positions[vertex_0]).cross(mesh.positions[vertex_2] - mesh.positions[vertex_0]);
                for (const std::uint32_t vertex : {vertex_0, vertex_1, vertex_2}) {
                    mesh.normals[vertex].x += normal.x;
                    mesh.normals[vertex].y += normal.y;
                    mesh.normals[vertex].z += normal.z;
                }
            }
            for (math::Float3& normal : mesh.normals) normal = normal.normalized();
        }
    } // namespace

    void SceneDocument::save() {
        scene::save_scene(this->content.source, this->content.path, this->content.path);
        this->content.modified = false;
    }

    void SceneDocument::save_as(const std::filesystem::path& scene_path) {
        scene::save_scene(this->content.source, scene_path, this->content.path);
        if (this->content.source.dynamic_setup && std::filesystem::absolute(scene_path.parent_path()).lexically_normal() != std::filesystem::absolute(this->content.path.parent_path()).lexically_normal()) {
            std::vector<std::string> providers{};
            for (const scene::DynamicSystem& system : this->content.source.dynamic_setup->systems)
                if (!std::ranges::contains(providers, system.provider_id)) providers.emplace_back(system.provider_id);
            for (const std::string& provider : providers) {
                const std::filesystem::path filename = scene::provider_library_filename(provider);
                std::filesystem::copy_file(this->content.path.parent_path() / filename, scene_path.parent_path() / filename);
            }
        }
        this->content.path     = scene_path;
        this->content.modified = false;
    }

    void SceneDocument::begin_update(scene::Scene& target_scene) noexcept {
        this->update.target  = &target_scene;
        this->update.changes = scene::SceneChange::None;
        this->update.open    = true;
    }

    void SceneDocument::commit_update() noexcept {
        this->update.open = false;
        if (this->update.changes != scene::SceneChange::None) this->update.target->mark_changed(std::exchange(this->update.changes, scene::SceneChange::None));
        this->update.target = nullptr;
    }

    void SceneDocument::mark_change(scene::Scene& target_scene, const scene::SceneChange changes) noexcept {
        if (&target_scene == &this->content.source) this->content.modified = true;
        if (this->update.open && this->update.target == &target_scene)
            this->update.changes = this->update.changes | changes;
        else
            target_scene.mark_changed(changes);
    }

    void SceneDocument::update_dynamic_system_parameters(scene::Scene& target_scene, const std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters) {
        target_scene.dynamic_setup->systems[system_index].parameters = std::move(parameters);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }

    void SceneDocument::update_triangle_mesh(scene::Scene& target_scene, const scene::GeometryId geometry_id, const std::span<const math::Float3> positions, const std::span<const math::Float3> normals, const std::span<const math::Float3> tangents, const std::span<const math::Float2> texture_coordinates, const std::span<const std::uint32_t> indices) {
        scene::Geometry& resource         = *std::ranges::find(target_scene.resources.geometries, geometry_id, &scene::Geometry::id);
        scene::TriangleMeshGeometry& mesh = std::get<scene::TriangleMeshGeometry>(resource.data);
        const bool topology_changed       = mesh.positions.size() != positions.size() || mesh.indices.size() != indices.size() || !std::ranges::equal(mesh.indices, indices);
        mesh.positions.assign(positions.begin(), positions.end());
        mesh.normals.assign(normals.begin(), normals.end());
        mesh.tangents.assign(tangents.begin(), tangents.end());
        mesh.texture_coordinates.assign(texture_coordinates.begin(), texture_coordinates.end());
        mesh.indices.assign(indices.begin(), indices.end());
        if (mesh.normals.empty()) generate_normals(mesh);
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        this->mark_change(target_scene, scene::SceneChange::Geometry);
    }

    void SceneDocument::update_particle_set(scene::Scene& target_scene, const scene::ParticleSetId particle_set_id, const std::span<const math::Float3> positions, const std::span<const float> radii, const std::span<const math::Float3> velocities, const std::span<const math::Float3> colors, const std::span<const float> temperatures, const std::span<const scene::MaterialId> particle_materials) {
        scene::ParticleSet& resource = *std::ranges::find(target_scene.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        const bool topology_changed  = resource.positions.size() != positions.size();
        resource.positions.assign(positions.begin(), positions.end());
        resource.radii.assign(radii.begin(), radii.end());
        resource.velocities.assign(velocities.begin(), velocities.end());
        resource.colors.assign(colors.begin(), colors.end());
        resource.temperatures.assign(temperatures.begin(), temperatures.end());
        resource.particle_materials.assign(particle_materials.begin(), particle_materials.end());
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        this->mark_change(target_scene, scene::SceneChange::Visualization);
    }

    void SceneDocument::update_density_grid(scene::Scene& target_scene, const scene::VolumeId volume_id, const scene::VolumeRegion region, const std::span<const float> density, const std::span<const float> temperature, const std::span<const float> emission_scale) {
        scene::Volume& resource        = *std::ranges::find(target_scene.resources.volumes, volume_id, &scene::Volume::id);
        scene::DensityGridVolume& grid = std::get<scene::DensityGridVolume>(resource.data);
        update_grid_region(grid.density, grid.resolution, region, density);
        update_grid_region(grid.temperature, grid.resolution, region, temperature);
        update_grid_region(grid.emission_scale, grid.resolution, region, emission_scale);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->mark_change(target_scene, scene::SceneChange::Volume);
    }

    void SceneDocument::update_rgb_grid(scene::Scene& target_scene, const scene::VolumeId volume_id, const scene::VolumeRegion region, const std::span<const math::Float3> sigma_a, const std::span<const math::Float3> sigma_s, const std::span<const math::Float3> emission) {
        scene::Volume& resource    = *std::ranges::find(target_scene.resources.volumes, volume_id, &scene::Volume::id);
        scene::RgbGridVolume& grid = std::get<scene::RgbGridVolume>(resource.data);
        update_grid_region(grid.sigma_a, grid.resolution, region, sigma_a);
        update_grid_region(grid.sigma_s, grid.resolution, region, sigma_s);
        update_grid_region(grid.emission, grid.resolution, region, emission);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->mark_change(target_scene, scene::SceneChange::Volume);
    }

    void SceneDocument::update_transform(scene::Scene& target_scene, const scene::InstanceId instance_id, math::Transform transform) {
        scene::Instance& resource = *std::ranges::find(target_scene.resources.instances, instance_id, &scene::Instance::id);
        resource.transform        = std::move(transform);
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Transform);
    }
} // namespace spectra
