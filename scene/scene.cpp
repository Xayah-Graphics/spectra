module spectra.scene;

import std;

namespace spectra::scene {
    float Float3::length() const noexcept {
        return std::sqrt(this->dot(*this));
    }

    Float3 Float3::normalized() const noexcept {
        return *this / this->length();
    }

    Float3 Transform::transform_point(const Float3 point) const noexcept {
        return {
            this->matrix[0] * point.x + this->matrix[1] * point.y + this->matrix[2] * point.z + this->matrix[3],
            this->matrix[4] * point.x + this->matrix[5] * point.y + this->matrix[6] * point.z + this->matrix[7],
            this->matrix[8] * point.x + this->matrix[9] * point.y + this->matrix[10] * point.z + this->matrix[11],
        };
    }

    Float3 Transform::transform_vector(const Float3 vector) const noexcept {
        return {
            this->matrix[0] * vector.x + this->matrix[1] * vector.y + this->matrix[2] * vector.z,
            this->matrix[4] * vector.x + this->matrix[5] * vector.y + this->matrix[6] * vector.z,
            this->matrix[8] * vector.x + this->matrix[9] * vector.y + this->matrix[10] * vector.z,
        };
    }

    Transform Transform::operator*(const Transform& child) const noexcept {
        Transform result{{}};
        for (std::uint32_t row = 0; row < 4; ++row)
            for (std::uint32_t column = 0; column < 4; ++column)
                for (std::uint32_t inner = 0; inner < 4; ++inner) result.matrix[row * 4u + column] += this->matrix[row * 4u + inner] * child.matrix[inner * 4u + column];
        return result;
    }

    Transform Transform::inverse() const {
        std::array<std::array<double, 8>, 4> augmented{};
        for (std::size_t row = 0; row != 4; ++row) {
            for (std::size_t column = 0; column != 4; ++column) augmented[row][column] = this->matrix[row * 4 + column];
            augmented[row][row + 4] = 1.0;
        }
        for (std::size_t column = 0; column != 4; ++column) {
            std::size_t pivot = column;
            for (std::size_t row = column + 1; row != 4; ++row)
                if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column])) pivot = row;
            if (std::abs(augmented[pivot][column]) < 1.0e-12) throw std::runtime_error("Transform is singular");
            if (pivot != column) std::swap(augmented[pivot], augmented[column]);
            const double divisor = augmented[column][column];
            for (double& value : augmented[column]) value /= divisor;
            for (std::size_t row = 0; row != 4; ++row) {
                if (row == column) continue;
                const double factor = augmented[row][column];
                for (std::size_t entry = 0; entry != 8; ++entry) augmented[row][entry] -= factor * augmented[column][entry];
            }
        }
        Transform result{};
        for (std::size_t row = 0; row != 4; ++row)
            for (std::size_t column = 0; column != 4; ++column) result.matrix[row * 4 + column] = static_cast<float>(augmented[row][column + 4]);
        return result;
    }

    Transform Transform::look_at(const Float3 position, const Float3 target, const Float3 up) noexcept {
        const Float3 forward   = (target - position).normalized();
        const Float3 right     = forward.cross(up).normalized();
        const Float3 actual_up = right.cross(forward);
        return Transform{{
            right.x,
            actual_up.x,
            -forward.x,
            position.x,
            right.y,
            actual_up.y,
            -forward.y,
            position.y,
            right.z,
            actual_up.z,
            -forward.z,
            position.z,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        }};
    }

    Bounds3 Bounds3::empty() noexcept {
        return {
            {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()},
            {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()},
        };
    }

    void Bounds3::include(const Float3 point) noexcept {
        this->minimum.x = std::min(this->minimum.x, point.x);
        this->minimum.y = std::min(this->minimum.y, point.y);
        this->minimum.z = std::min(this->minimum.z, point.z);
        this->maximum.x = std::max(this->maximum.x, point.x);
        this->maximum.y = std::max(this->maximum.y, point.y);
        this->maximum.z = std::max(this->maximum.z, point.z);
    }

    void Bounds3::include(const Bounds3 bounds) noexcept {
        this->include(bounds.minimum);
        this->include(bounds.maximum);
    }

    bool Bounds3::valid() const noexcept {
        return this->minimum.x <= this->maximum.x && this->minimum.y <= this->maximum.y && this->minimum.z <= this->maximum.z;
    }

    Float3 Bounds3::center() const noexcept {
        return (this->minimum + this->maximum) * 0.5f;
    }

    Float3 Bounds3::diagonal() const noexcept {
        return this->maximum - this->minimum;
    }

    float Bounds3::radius() const noexcept {
        return std::max(this->diagonal().length() * 0.5f, 0.01f);
    }

    Bounds3 Bounds3::transformed(const Transform& transform) const noexcept {
        Bounds3 result = Bounds3::empty();
        for (const float x : {this->minimum.x, this->maximum.x})
            for (const float y : {this->minimum.y, this->maximum.y})
                for (const float z : {this->minimum.z, this->maximum.z}) result.include(transform.transform_point({x, y, z}));
        return result;
    }

    namespace {
        [[nodiscard]] float triangle_area(const Float3 first, const Float3 second, const Float3 third) noexcept {
            return 0.5f * (second - first).cross(third - first).length();
        }

        [[nodiscard]] float radians(const float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

    } // namespace

    Bounds3 geometry_bounds(const Geometry& geometry) noexcept {
        return std::visit(
            [](const auto& data) -> Bounds3 {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    Bounds3 result = Bounds3::empty();
                    for (const Float3 position : data.positions) result.include(position);
                    return result;
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>)
                    return {{-data.radius, -data.radius, data.z_min}, {data.radius, data.radius, data.z_max}};
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>)
                    return data.bounds;
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>)
                    return {{data.minimum.x, data.minimum.y, 0.0f}, {data.maximum.x, data.maximum.y, 0.0f}};
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>)
                    return {{-data.radius, -data.radius, data.height}, {data.radius, data.radius, data.height}};
                else
                    return {{-data.radius, -data.radius, data.z_min}, {data.radius, data.radius, data.z_max}};
            },
            geometry.data);
    }

    float surface_area(const Geometry& geometry) noexcept {
        return std::visit(
            [](const auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    float area{};
                    for (std::size_t index = 0; index < data.indices.size(); index += 3) area += triangle_area(data.positions[data.indices[index]], data.positions[data.indices[index + 1]], data.positions[data.indices[index + 2]]);
                    return area;
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>)
                    return radians(data.phi_max) * data.radius * (data.z_max - data.z_min);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                    const Float3 extent = data.bounds.diagonal();
                    return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>)
                    return (data.maximum.x - data.minimum.x) * (data.maximum.y - data.minimum.y);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>)
                    return 0.5f * radians(data.phi_max) * (data.radius * data.radius - data.inner_radius * data.inner_radius);
                else
                    return radians(data.phi_max) * data.radius * (data.z_max - data.z_min);
            },
            geometry.data);
    }

    CameraFrame CameraResource::frame() const noexcept {
        const std::array<float, 16>& matrix = this->transform.matrix;
        return {
            {matrix[3], matrix[7], matrix[11]},
            Float3{matrix[0], matrix[4], matrix[8]}.normalized(),
            Float3{matrix[1], matrix[5], matrix[9]}.normalized(),
            Float3{-matrix[2], -matrix[6], -matrix[10]}.normalized(),
        };
    }

    CameraMatrices CameraResource::matrices() const noexcept {
        const std::array<float, 16>& transform = this->transform.matrix;
        const std::array<float, 16> view{
            transform[0],
            transform[4],
            transform[8],
            -(transform[0] * transform[3] + transform[4] * transform[7] + transform[8] * transform[11]),
            transform[1],
            transform[5],
            transform[9],
            -(transform[1] * transform[3] + transform[5] * transform[7] + transform[9] * transform[11]),
            transform[2],
            transform[6],
            transform[10],
            -(transform[2] * transform[3] + transform[6] * transform[7] + transform[10] * transform[11]),
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        std::array<float, 16> projection{};
        std::array<float, 16> inverse_projection{};
        if (const PerspectiveCameraData* perspective = std::get_if<PerspectiveCameraData>(&this->data)) {
            const float width           = perspective->screen.maximum.x - perspective->screen.minimum.x;
            const float height          = perspective->screen.maximum.y - perspective->screen.minimum.y;
            const float inverse_tangent = 1.0f / std::tan(perspective->vertical_fov * std::numbers::pi_v<float> / 360.0f);
            projection                  = {
                2.0f * inverse_tangent / width,
                0.0f,
                (perspective->screen.maximum.x + perspective->screen.minimum.x) / width,
                0.0f,
                0.0f,
                2.0f * inverse_tangent / height,
                (perspective->screen.maximum.y + perspective->screen.minimum.y) / height,
                0.0f,
                0.0f,
                0.0f,
                perspective->far_plane / (perspective->near_plane - perspective->far_plane),
                perspective->far_plane * perspective->near_plane / (perspective->near_plane - perspective->far_plane),
                0.0f,
                0.0f,
                -1.0f,
                0.0f,
            };
            inverse_projection = {
                1.0f / projection[0],
                0.0f,
                0.0f,
                projection[2] / projection[0],
                0.0f,
                1.0f / projection[5],
                0.0f,
                projection[6] / projection[5],
                0.0f,
                0.0f,
                0.0f,
                -1.0f,
                0.0f,
                0.0f,
                1.0f / projection[11],
                projection[10] / projection[11],
            };
        } else {
            const OrthographicCameraData& orthographic = std::get<OrthographicCameraData>(this->data);
            const float width                          = orthographic.screen.maximum.x - orthographic.screen.minimum.x;
            const float height                         = orthographic.screen.maximum.y - orthographic.screen.minimum.y;
            projection                                 = {
                2.0f / width,
                0.0f,
                0.0f,
                -(orthographic.screen.maximum.x + orthographic.screen.minimum.x) / width,
                0.0f,
                2.0f / height,
                0.0f,
                -(orthographic.screen.maximum.y + orthographic.screen.minimum.y) / height,
                0.0f,
                0.0f,
                1.0f / (orthographic.near_plane - orthographic.far_plane),
                orthographic.near_plane / (orthographic.near_plane - orthographic.far_plane),
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            inverse_projection = {
                1.0f / projection[0],
                0.0f,
                0.0f,
                -projection[3] / projection[0],
                0.0f,
                1.0f / projection[5],
                0.0f,
                -projection[7] / projection[5],
                0.0f,
                0.0f,
                1.0f / projection[10],
                -projection[11] / projection[10],
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
        }
        const Transform view_transform{view};
        const Transform projection_transform{projection};
        const Transform inverse_projection_transform{inverse_projection};
        return {
            view,
            projection,
            (projection_transform * view_transform).matrix,
            (this->transform * inverse_projection_transform).matrix,
        };
    }

    BlackbodySpectrum::BlackbodySpectrum(const float temperature) noexcept : temperature(temperature) {
        if (temperature <= 0.0f) return;
        constexpr float speed_of_light     = 299792458.0f;
        constexpr float planck_constant    = 6.62606957e-34f;
        constexpr float boltzmann_constant = 1.3806488e-23f;
        const float maximum_wavelength     = 2.8977721e-3f / temperature * 1.0e9f;
        const float wavelength_meters      = maximum_wavelength * 1.0e-9f;
        this->normalization                = std::pow(wavelength_meters, 5.0f) * (std::exp(planck_constant * speed_of_light / (wavelength_meters * boltzmann_constant * temperature)) - 1.0f) / (2.0f * planck_constant * speed_of_light * speed_of_light);
    }

    float BlackbodySpectrum::evaluate(const float wavelength) const noexcept {
        if (this->temperature <= 0.0f) return 0.0f;
        constexpr float speed_of_light     = 299792458.0f;
        constexpr float planck_constant    = 6.62606957e-34f;
        constexpr float boltzmann_constant = 1.3806488e-23f;
        const float wavelength_meters      = wavelength * 1.0e-9f;
        return (2.0f * planck_constant * speed_of_light * speed_of_light) / (std::pow(wavelength_meters, 5.0f) * (std::exp(planck_constant * speed_of_light / (wavelength_meters * boltzmann_constant * this->temperature)) - 1.0f)) * this->normalization;
    }

    float PiecewiseLinearSpectrum::evaluate(const float wavelength) const noexcept {
        if (this->wavelengths.empty() || wavelength < this->wavelengths.front() || wavelength > this->wavelengths.back()) return 0.0f;
        const std::vector<float>::const_iterator upper = std::ranges::upper_bound(this->wavelengths, wavelength);
        if (upper == this->wavelengths.begin()) return this->values.front();
        if (upper == this->wavelengths.end()) return this->values.back();
        const std::size_t upper_index = static_cast<std::size_t>(std::distance(this->wavelengths.begin(), upper));
        const std::size_t lower_index = upper_index - 1;
        const float value             = (wavelength - this->wavelengths[lower_index]) / (this->wavelengths[upper_index] - this->wavelengths[lower_index]);
        return std::lerp(this->values[lower_index], this->values[upper_index], value);
    }

    Bounds3 particle_bounds(const ParticleSet& particles) noexcept {
        Bounds3 result = Bounds3::empty();
        for (std::size_t index = 0; index != particles.positions.size(); ++index) {
            const Float3 position = particles.positions[index];
            const float radius    = particles.radii[index];
            result.include(Float3{position.x - radius, position.y - radius, position.z - radius});
            result.include(Float3{position.x + radius, position.y + radius, position.z + radius});
        }
        return result;
    }


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
                const Float3 normal          = (mesh.positions[vertex_1] - mesh.positions[vertex_0]).cross(mesh.positions[vertex_2] - mesh.positions[vertex_0]);
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
            if (!instance.visible) continue;
            const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance.prototype, &Prototype::id);
            for (const Primitive& primitive : prototype.primitives) include_primitive_bounds(result, *this, primitive, instance.transform);
        }
        return result;
    }

    std::optional<Bounds3> SceneView::local_bounds(const InstanceId id) const noexcept {
        const std::vector<Instance>::const_iterator instance = std::ranges::find(this->resources.instances, id, &Instance::id);
        if (instance == this->resources.instances.end()) return std::nullopt;
        const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance->prototype, &Prototype::id);
        Bounds3 result             = Bounds3::empty();
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
            if (instance == this->resources.instances.end() || !instance->visible) continue;
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

    void Scene::mark_all_changed() noexcept {
        this->publish(SceneChange::All);
    }

    void Scene::publish(const SceneChange changes) noexcept {
        ++this->current_revision.value;
        this->current_revision.changes = this->current_revision.changes | changes;
    }

    SceneUpdate::SceneUpdate(Scene& scene) noexcept : scene(&scene) {}

    void SceneUpdate::begin_frame() noexcept {
        this->frame_changes = SceneChange::None;
        this->frame_open    = true;
    }

    void SceneUpdate::commit_frame() noexcept {
        this->frame_open = false;
        if (this->frame_changes != SceneChange::None) this->scene->publish(std::exchange(this->frame_changes, SceneChange::None));
    }

    void SceneUpdate::mark(const SceneChange changes) noexcept {
        this->publish(changes);
    }

    void SceneUpdate::publish(const SceneChange changes) noexcept {
        if (this->frame_open)
            this->frame_changes = this->frame_changes | changes;
        else
            this->scene->publish(changes);
    }

    void SceneUpdate::update_dynamic_setup(std::optional<DynamicSetup> setup) {
        this->scene->dynamic_setup = std::move(setup);
        this->publish(SceneChange::Metadata);
    }

    void SceneUpdate::update_triangle_mesh(const GeometryId geometry, const std::span<const Float3> positions, const std::span<const Float3> normals, const std::span<const Float3> tangents, const std::span<const Float2> texture_coordinates, const std::span<const std::uint32_t> indices) {
        Geometry& resource          = *std::ranges::find(this->scene->resources.geometries, geometry, &Geometry::id);
        TriangleMeshGeometry& mesh  = std::get<TriangleMeshGeometry>(resource.data);
        const bool topology_changed = mesh.positions.size() != positions.size() || mesh.indices.size() != indices.size() || !std::ranges::equal(mesh.indices, indices);
        mesh.positions.assign(positions.begin(), positions.end());
        mesh.normals.assign(normals.begin(), normals.end());
        mesh.tangents.assign(tangents.begin(), tangents.end());
        mesh.texture_coordinates.assign(texture_coordinates.begin(), texture_coordinates.end());
        mesh.indices.assign(indices.begin(), indices.end());
        if (mesh.normals.empty()) generate_normals(mesh);
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        this->publish(SceneChange::Geometry);
    }

    void SceneUpdate::update_particle_set(const ParticleSetId particles, const std::span<const Float3> positions, const std::span<const float> radii, const std::span<const Float3> velocities, const std::span<const Float3> colors, const std::span<const float> temperatures, const std::span<const MaterialId> particle_materials) {
        ParticleSet& resource       = *std::ranges::find(this->scene->resources.particle_sets, particles, &ParticleSet::id);
        const bool topology_changed = resource.positions.size() != positions.size();
        resource.positions.assign(positions.begin(), positions.end());
        resource.radii.assign(radii.begin(), radii.end());
        resource.velocities.assign(velocities.begin(), velocities.end());
        resource.colors.assign(colors.begin(), colors.end());
        resource.temperatures.assign(temperatures.begin(), temperatures.end());
        resource.particle_materials.assign(particle_materials.begin(), particle_materials.end());
        ++resource.revision.content;
        if (topology_changed) ++resource.revision.topology;
        this->publish(SceneChange::Visualization);
    }

    void SceneUpdate::update_density_grid(const VolumeId volume, const VolumeRegion region, const std::span<const float> density, const std::span<const float> temperature, const std::span<const float> emission_scale) {
        Volume& resource        = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        DensityGridVolume& grid = std::get<DensityGridVolume>(resource.data);
        update_grid_region(grid.density, grid.resolution, region, density);
        update_grid_region(grid.temperature, grid.resolution, region, temperature);
        update_grid_region(grid.emission_scale, grid.resolution, region, emission_scale);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->publish(SceneChange::Volume);
    }

    void SceneUpdate::update_rgb_grid(const VolumeId volume, const VolumeRegion region, const std::span<const Float3> sigma_a, const std::span<const Float3> sigma_s, const std::span<const Float3> emission) {
        Volume& resource    = *std::ranges::find(this->scene->resources.volumes, volume, &Volume::id);
        RgbGridVolume& grid = std::get<RgbGridVolume>(resource.data);
        update_grid_region(grid.sigma_a, grid.resolution, region, sigma_a);
        update_grid_region(grid.sigma_s, grid.resolution, region, sigma_s);
        update_grid_region(grid.emission, grid.resolution, region, emission);
        grid.asset = {};
        ++resource.revision.content;
        include_dirty_region(resource, region);
        this->publish(SceneChange::Volume);
    }

    void SceneUpdate::update_transform(const InstanceId instance, Transform transform) {
        Instance& resource = *std::ranges::find(this->scene->resources.instances, instance, &Instance::id);
        resource.transform = std::move(transform);
        ++resource.revision.content;
        this->publish(SceneChange::Transform);
    }

} // namespace spectra::scene
