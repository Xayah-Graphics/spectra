module spectra.scene;

import std;

namespace spectra::scene {
    namespace {
        template <class Element>
        void update_grid_region(std::vector<Element>& destination, const UInt3 resolution, const VolumeRegion region, const std::span<const Element> source) {
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

        void include_dirty_region(Volume& volume, const VolumeRegion region) {
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

        void generate_normals(TriangleMeshGeometry& mesh) {
            mesh.normals.assign(mesh.positions.size(), Float3{});
            for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
                const std::uint32_t vertex_0 = mesh.indices[index];
                const std::uint32_t vertex_1 = mesh.indices[index + 1];
                const std::uint32_t vertex_2 = mesh.indices[index + 2];
                const Float3 normal = (mesh.positions[vertex_1] - mesh.positions[vertex_0]).cross(mesh.positions[vertex_2] - mesh.positions[vertex_0]);
                for (const std::uint32_t vertex : {vertex_0, vertex_1, vertex_2}) {
                    mesh.normals[vertex].x += normal.x;
                    mesh.normals[vertex].y += normal.y;
                    mesh.normals[vertex].z += normal.z;
                }
            }
            for (Float3& normal : mesh.normals) normal = normal.normalized();
        }

        bool include_primitive_bounds(Bounds3& bounds, const SceneView& scene, const Primitive& primitive, const Transform& parent) noexcept {
            Bounds3 local{};
            if (primitive.geometry.value != 0)
                local = geometry_bounds(*std::ranges::find(scene.resources.geometries, primitive.geometry, &Geometry::id));
            else if (primitive.particles.value != 0)
                local = particle_bounds(*std::ranges::find(scene.resources.particle_sets, primitive.particles, &ParticleSet::id));
            else
                return false;
            bounds.include(local.transformed(parent * primitive.transform));
            return true;
        }
    } // namespace

    Bounds3 SceneView::bounds() const noexcept {
        Bounds3 result = Bounds3::empty();
        for (const Instance& instance : this->resources.instances) {
            const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance.prototype, &Prototype::id);
            for (const Primitive& primitive : prototype.primitives) include_primitive_bounds(result, *this, primitive, instance.transform);
        }
        return result;
    }

    std::optional<Bounds3> SceneView::local_bounds(const InstanceId id) const noexcept {
        const std::vector<Instance>::const_iterator instance = std::ranges::find(this->resources.instances, id, &Instance::id);
        if (instance == this->resources.instances.end()) return std::nullopt;
        const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance->prototype, &Prototype::id);
        Bounds3 result = Bounds3::empty();
        bool found_any{};
        for (const Primitive& primitive : prototype.primitives) found_any = include_primitive_bounds(result, *this, primitive, Transform{}) || found_any;
        if (!found_any) return std::nullopt;
        return result;
    }

    std::optional<Bounds3> SceneView::bounds(const std::span<const InstanceId> instances) const noexcept {
        if (instances.empty()) return std::nullopt;
        Bounds3 result = Bounds3::empty();
        bool found_any{};
        for (const InstanceId id : instances) {
            const std::vector<Instance>::const_iterator instance = std::ranges::find(this->resources.instances, id, &Instance::id);
            if (instance == this->resources.instances.end()) continue;
            const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance->prototype, &Prototype::id);
            for (const Primitive& primitive : prototype.primitives) found_any = include_primitive_bounds(result, *this, primitive, instance->transform) || found_any;
        }
        if (!found_any) return std::nullopt;
        return result;
    }

    SceneView Scene::view() const noexcept {
        return {
            this->resources,
            this->camera(),
            this->film(),
            this->sampler(),
            this->transport,
            this->current_revision,
        };
    }

    const CameraResource& Scene::camera() const noexcept {
        return *std::ranges::find(this->resources.cameras, this->active_camera, &CameraResource::id);
    }

    const Film& Scene::film() const noexcept {
        return *std::ranges::find(this->resources.films, this->active_film, &Film::id);
    }

    const Sampler& Scene::sampler() const noexcept {
        return *std::ranges::find(this->resources.samplers, this->active_sampler, &Sampler::id);
    }

    SceneRevision Scene::revision() const noexcept {
        return this->current_revision;
    }

    void Scene::acknowledge_changes() noexcept {
        this->current_revision.changes = SceneChange::None;
        for (Volume& volume : this->resources.volumes) volume.dirty_region.reset();
    }

    void Scene::rebuild_resource_state() noexcept {
        this->next_geometry_id = 1;
        for (const Geometry& resource : this->resources.geometries) this->next_geometry_id = std::max(this->next_geometry_id, resource.id.value + 1);
        this->next_particle_set_id = 1;
        for (const ParticleSet& resource : this->resources.particle_sets) this->next_particle_set_id = std::max(this->next_particle_set_id, resource.id.value + 1);
        this->next_volume_id = 1;
        for (const Volume& resource : this->resources.volumes) this->next_volume_id = std::max(this->next_volume_id, resource.id.value + 1);
        this->next_texture_id = 1;
        for (const Texture& resource : this->resources.textures) this->next_texture_id = std::max(this->next_texture_id, resource.id.value + 1);
        this->next_material_id = 1;
        for (const MaterialResource& resource : this->resources.materials) this->next_material_id = std::max(this->next_material_id, resource.id.value + 1);
        this->next_medium_id = 1;
        for (const Medium& resource : this->resources.media) this->next_medium_id = std::max(this->next_medium_id, resource.id.value + 1);
        this->next_light_id = 1;
        for (const Light& resource : this->resources.lights) this->next_light_id = std::max(this->next_light_id, resource.id.value + 1);
        this->next_prototype_id = 1;
        for (const Prototype& resource : this->resources.prototypes) this->next_prototype_id = std::max(this->next_prototype_id, resource.id.value + 1);
        this->next_instance_id = 1;
        for (const Instance& resource : this->resources.instances) this->next_instance_id = std::max(this->next_instance_id, resource.id.value + 1);
        this->next_camera_id = 1;
        for (const CameraResource& resource : this->resources.cameras) this->next_camera_id = std::max(this->next_camera_id, resource.id.value + 1);
        this->next_film_id = 1;
        for (const Film& resource : this->resources.films) this->next_film_id = std::max(this->next_film_id, resource.id.value + 1);
        this->next_sampler_id = 1;
        for (const Sampler& resource : this->resources.samplers) this->next_sampler_id = std::max(this->next_sampler_id, resource.id.value + 1);
        this->publish(SceneChange::All);
    }

    void Scene::publish(const SceneChange changes) noexcept {
        ++this->current_revision.value;
        this->current_revision.changes = this->current_revision.changes | changes;
    }

    SceneWriter::SceneWriter(Scene& scene) noexcept : scene(&scene) {}

    MaterialId SceneWriter::create_diffuse_material(const Float3 reflectance) {
        const MaterialId id{this->scene->next_material_id++};
        this->scene->resources.materials.emplace_back(id, std::format("Material {}", id.value), ResourceRevision{},
            DiffuseMaterialData{
                .reflectance = {reflectance, {}},
            });
        this->scene->publish(SceneChange::Material);
        return id;
    }

    LightId SceneWriter::create_diffuse_area_light(const Float3 radiance, const EmissionSidedness sidedness) {
        const LightId id{this->scene->next_light_id++};
        this->scene->resources.lights.emplace_back(id, std::format("Light {}", id.value), ResourceRevision{},
            DiffuseAreaLight{
                .radiance  = {radiance, {}, SpectrumEncoding::RgbIlluminant},
                .sidedness = sidedness,
            });
        this->scene->publish(SceneChange::Light);
        return id;
    }

    GeometryId SceneWriter::create_triangle_mesh(const std::span<const Float3> positions, const std::span<const Float3> normals, const std::span<const Float3> tangents, const std::span<const Float2> texture_coordinates, const std::span<const std::uint32_t> indices, const GeometryUpdateMode update_mode) {
        const GeometryId id{this->scene->next_geometry_id++};
        this->scene->resources.geometries.emplace_back(id, std::format("Geometry {}", id.value), ResourceRevision{},
            TriangleMeshGeometry{
                .update_mode         = update_mode,
                .positions           = {positions.begin(), positions.end()},
                .normals             = {normals.begin(), normals.end()},
                .tangents            = {tangents.begin(), tangents.end()},
                .texture_coordinates = {texture_coordinates.begin(), texture_coordinates.end()},
                .indices             = {indices.begin(), indices.end()},
            });
        TriangleMeshGeometry& mesh = std::get<TriangleMeshGeometry>(this->scene->resources.geometries.back().data);
        if (mesh.normals.empty()) generate_normals(mesh);
        this->scene->publish(SceneChange::Geometry);
        return id;
    }

    ParticleSetId SceneWriter::create_particle_set(const std::span<const Float3> positions, const std::span<const float> radii, const std::span<const Float3> velocities, const std::span<const Float3> colors, const std::span<const float> temperatures, const MaterialId material, const std::span<const MaterialId> particle_materials, const GeometryUpdateMode update_mode) {
        const ParticleSetId id{this->scene->next_particle_set_id++};
        this->scene->resources.particle_sets.push_back(ParticleSet{
            .id                 = id,
            .name               = std::format("Particle Set {}", id.value),
            .update_mode        = update_mode,
            .positions          = {positions.begin(), positions.end()},
            .radii              = {radii.begin(), radii.end()},
            .velocities         = {velocities.begin(), velocities.end()},
            .colors             = {colors.begin(), colors.end()},
            .temperatures       = {temperatures.begin(), temperatures.end()},
            .material           = material,
            .particle_materials = {particle_materials.begin(), particle_materials.end()},
        });
        this->scene->publish(SceneChange::Geometry);
        return id;
    }

    PrototypeId SceneWriter::create_prototype(Primitive primitive) {
        const PrototypeId id{this->scene->next_prototype_id++};
        this->scene->resources.prototypes.emplace_back(id, std::format("Prototype {}", id.value), ResourceRevision{}, std::vector<Primitive>{std::move(primitive)});
        this->scene->publish(SceneChange::Geometry);
        return id;
    }

    InstanceId SceneWriter::create_instance(const PrototypeId prototype, Transform transform) {
        const InstanceId id{this->scene->next_instance_id++};
        this->scene->resources.instances.emplace_back(id, std::format("Instance {}", id.value), ResourceRevision{}, prototype, std::move(transform));
        this->scene->publish(SceneChange::Transform);
        return id;
    }

    CameraId SceneWriter::define_perspective_camera(Transform transform, const float vertical_fov, const float near_plane, const float far_plane) {
        if (this->scene->active_camera.value == 0) {
            const CameraId id{this->scene->next_camera_id++};
            this->scene->active_camera = id;
            this->scene->resources.cameras.emplace_back(id, "Main Camera", ResourceRevision{}, std::move(transform), 1.0f, MediumId{},
                PerspectiveCameraData{
                    .vertical_fov = vertical_fov,
                    .near_plane   = near_plane,
                    .far_plane    = far_plane,
                });
        } else {
            CameraResource& camera = *std::ranges::find(this->scene->resources.cameras, this->scene->active_camera, &CameraResource::id);
            PerspectiveCameraData perspective{
                .vertical_fov = vertical_fov,
                .screen = std::visit([](const auto& data) { return data.screen; }, camera.data),
                .lens_radius = std::visit([](const auto& data) { return data.lens_radius; }, camera.data),
                .focal_distance = std::visit([](const auto& data) { return data.focal_distance; }, camera.data),
                .near_plane = near_plane,
                .far_plane = far_plane,
            };
            camera.transform = std::move(transform);
            camera.data      = std::move(perspective);
            ++camera.revision.content;
        }
        this->scene->publish(SceneChange::Camera);
        return this->scene->active_camera;
    }

    FilmId SceneWriter::define_rgb_film(const std::array<std::uint32_t, 2> resolution, Filter filter) {
        if (this->scene->active_film.value == 0) {
            const FilmId id{this->scene->next_film_id++};
            this->scene->active_film = id;
            this->scene->resources.films.push_back(Film{
                .id            = id,
                .name          = "Main Film",
                .resolution    = resolution,
                .pixel_maximum = resolution,
                .filter        = std::move(filter),
                .gbuffer       = true,
            });
        } else {
            Film& film         = *std::ranges::find(this->scene->resources.films, this->scene->active_film, &Film::id);
            film.resolution    = resolution;
            film.pixel_maximum = resolution;
            film.filter        = std::move(filter);
            ++film.revision.content;
        }
        this->scene->publish(SceneChange::Film);
        return this->scene->active_film;
    }

    SamplerId SceneWriter::define_sampler(const SamplerKind kind, const std::uint32_t samples_per_pixel, const std::uint32_t seed) {
        if (this->scene->active_sampler.value == 0) {
            const SamplerId id{this->scene->next_sampler_id++};
            this->scene->active_sampler = id;
            this->scene->resources.samplers.emplace_back(id, "Main Sampler", ResourceRevision{}, kind, samples_per_pixel, seed);
        } else {
            Sampler& sampler          = *std::ranges::find(this->scene->resources.samplers, this->scene->active_sampler, &Sampler::id);
            sampler.kind              = kind;
            sampler.samples_per_pixel = samples_per_pixel;
            sampler.seed              = seed;
            ++sampler.revision.content;
        }
        this->scene->publish(SceneChange::Sampler);
        return this->scene->active_sampler;
    }

    void SceneWriter::update_sampler(Sampler sampler) {
        Sampler& resource = *std::ranges::find(this->scene->resources.samplers, sampler.id, &Sampler::id);
        sampler.id        = resource.id;
        sampler.revision  = resource.revision;
        ++sampler.revision.content;
        resource = std::move(sampler);
        this->scene->publish(SceneChange::Sampler);
    }

    void SceneWriter::update_film(Film film) {
        Film& resource = *std::ranges::find(this->scene->resources.films, film.id, &Film::id);
        film.id        = resource.id;
        film.revision  = resource.revision;
        ++film.revision.content;
        resource = std::move(film);
        this->scene->publish(SceneChange::Film);
    }

    void SceneWriter::update_transport(TransportSettings transport) {
        this->scene->transport = std::move(transport);
        this->scene->publish(SceneChange::Transport);
    }

    void SceneWriter::update_triangle_mesh(const GeometryId geometry, const std::span<const Float3> positions, const std::span<const Float3> normals, const std::span<const Float3> tangents, const std::span<const Float2> texture_coordinates, const std::span<const std::uint32_t> indices) {
        Geometry& resource         = *std::ranges::find(this->scene->resources.geometries, geometry, &Geometry::id);
        TriangleMeshGeometry& mesh = std::get<TriangleMeshGeometry>(resource.data);
        mesh.positions.assign(positions.begin(), positions.end());
        mesh.normals.assign(normals.begin(), normals.end());
        mesh.tangents.assign(tangents.begin(), tangents.end());
        mesh.texture_coordinates.assign(texture_coordinates.begin(), texture_coordinates.end());
        mesh.indices.assign(indices.begin(), indices.end());
        if (mesh.normals.empty()) generate_normals(mesh);
        ++resource.revision.content;
        if (mesh.update_mode == GeometryUpdateMode::TopologyChanging) ++resource.revision.topology;
        this->scene->publish(SceneChange::Geometry);
    }

    void SceneWriter::update_particle_set(const ParticleSetId particles, const std::span<const Float3> positions, const std::span<const float> radii, const std::span<const Float3> velocities, const std::span<const Float3> colors, const std::span<const float> temperatures, const std::span<const MaterialId> particle_materials) {
        ParticleSet& resource            = *std::ranges::find(this->scene->resources.particle_sets, particles, &ParticleSet::id);
        const std::size_t previous_count = resource.positions.size();
        resource.positions.assign(positions.begin(), positions.end());
        resource.radii.assign(radii.begin(), radii.end());
        resource.velocities.assign(velocities.begin(), velocities.end());
        resource.colors.assign(colors.begin(), colors.end());
        resource.temperatures.assign(temperatures.begin(), temperatures.end());
        resource.particle_materials.assign(particle_materials.begin(), particle_materials.end());
        ++resource.revision.content;
        if (resource.update_mode == GeometryUpdateMode::TopologyChanging && previous_count != resource.positions.size()) ++resource.revision.topology;
        this->scene->publish(SceneChange::Geometry);
    }

    void SceneWriter::replace_density_grid(const VolumeId volume, const UInt3 resolution, const std::span<const float> density, const std::span<const float> temperature, const std::span<const float> emission_scale) {
        Volume& resource            = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        DensityGridVolume& grid     = std::get<DensityGridVolume>(resource.data);
        const bool topology_changed = grid.resolution != resolution || grid.temperature.empty() != temperature.empty() || grid.emission_scale.empty() != emission_scale.empty();
        grid.resolution             = resolution;
        grid.asset                  = {};
        grid.density.assign(density.begin(), density.end());
        grid.temperature.assign(temperature.begin(), temperature.end());
        grid.emission_scale.assign(emission_scale.begin(), emission_scale.end());
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        resource.dirty_region = VolumeRegion{{}, resolution};
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::update_density_grid(const VolumeId volume, const VolumeRegion region, const std::span<const float> density, const std::span<const float> temperature, const std::span<const float> emission_scale) {
        Volume& resource        = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        DensityGridVolume& grid = std::get<DensityGridVolume>(resource.data);
        update_grid_region(grid.density, grid.resolution, region, density);
        update_grid_region(grid.temperature, grid.resolution, region, temperature);
        update_grid_region(grid.emission_scale, grid.resolution, region, emission_scale);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::replace_rgb_grid(const VolumeId volume, const UInt3 resolution, const std::span<const Float3> sigma_a, const std::span<const Float3> sigma_s, const std::span<const Float3> emission) {
        Volume& resource            = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        RgbGridVolume& grid         = std::get<RgbGridVolume>(resource.data);
        const bool topology_changed = grid.resolution != resolution || grid.sigma_a.empty() != sigma_a.empty() || grid.sigma_s.empty() != sigma_s.empty() || grid.emission.empty() != emission.empty();
        grid.resolution             = resolution;
        grid.asset                  = {};
        grid.sigma_a.assign(sigma_a.begin(), sigma_a.end());
        grid.sigma_s.assign(sigma_s.begin(), sigma_s.end());
        grid.emission.assign(emission.begin(), emission.end());
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        resource.dirty_region = VolumeRegion{{}, resolution};
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::update_rgb_grid(const VolumeId volume, const VolumeRegion region, const std::span<const Float3> sigma_a, const std::span<const Float3> sigma_s, const std::span<const Float3> emission) {
        Volume& resource    = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        RgbGridVolume& grid = std::get<RgbGridVolume>(resource.data);
        update_grid_region(grid.sigma_a, grid.resolution, region, sigma_a);
        update_grid_region(grid.sigma_s, grid.resolution, region, sigma_s);
        update_grid_region(grid.emission, grid.resolution, region, emission);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::replace_nanovdb(const VolumeId volume, const Bounds3 bounds, NanoVdbVolume data) {
        Volume& resource = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        resource.bounds  = bounds;
        resource.data    = std::move(data);
        resource.dirty_region.reset();
        ++resource.revision.content;
        ++resource.revision.topology;
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::update_procedural_cloud(const VolumeId volume, ProceduralCloudVolume data) {
        Volume& resource = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        resource.data    = std::move(data);
        resource.dirty_region.reset();
        ++resource.revision.content;
        this->scene->publish(SceneChange::Volume);
    }

    void SceneWriter::update_transform(const InstanceId instance, Transform transform) {
        Instance& resource = *std::ranges::find(this->scene->resources.instances, instance, &Instance::id);
        resource.transform = std::move(transform);
        ++resource.revision.content;
        this->scene->publish(SceneChange::Transform);
    }

    void SceneWriter::update_diffuse_material(const MaterialId material, const Float3 reflectance) {
        MaterialResource& resource                                     = *std::ranges::find(this->scene->resources.materials, material, &MaterialResource::id);
        std::get<DiffuseMaterialData>(resource.data).reflectance.value = reflectance;
        ++resource.revision.content;
        this->scene->publish(SceneChange::Material);
    }

    void SceneWriter::update_camera(CameraResource camera) {
        CameraResource& current = *std::ranges::find(this->scene->resources.cameras, this->scene->active_camera, &CameraResource::id);
        camera.id               = current.id;
        camera.name             = current.name;
        camera.revision         = current.revision;
        ++camera.revision.content;
        current = std::move(camera);
        this->scene->publish(SceneChange::Camera);
    }

    void SceneWriter::rename_geometry(const GeometryId geometry, std::string name) {
        std::ranges::find(this->scene->resources.geometries, geometry, &Geometry::id)->name = std::move(name);
        this->scene->publish(SceneChange::Metadata);
    }

    void SceneWriter::rename_instance(const InstanceId instance, std::string name) {
        std::ranges::find(this->scene->resources.instances, instance, &Instance::id)->name = std::move(name);
        this->scene->publish(SceneChange::Metadata);
    }

    void SceneWriter::rename_material(const MaterialId material, std::string name) {
        std::ranges::find(this->scene->resources.materials, material, &MaterialResource::id)->name = std::move(name);
        this->scene->publish(SceneChange::Metadata);
    }

    void SceneWriter::rename_camera(std::string name) {
        std::ranges::find(this->scene->resources.cameras, this->scene->active_camera, &CameraResource::id)->name = std::move(name);
        this->scene->publish(SceneChange::Metadata);
    }
} // namespace spectra::scene
