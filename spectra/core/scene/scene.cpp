module spectra.scene;

import std;

namespace spectra::scene {
    namespace {
        [[nodiscard]] float triangle_area(const math::Float3 first, const math::Float3 second, const math::Float3 third) noexcept {
            return 0.5f * (second - first).cross(third - first).length();
        }

        [[nodiscard]] float radians(const float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

    } // namespace

    math::Bounds3 geometry_bounds(const Geometry& geometry) noexcept {
        return std::visit(
            [](const auto& data) -> math::Bounds3 {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    math::Bounds3 result = math::Bounds3::empty();
                    for (const math::Float3 position : data.positions) result.include(position);
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
                    const math::Float3 extent = data.bounds.diagonal();
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

    CameraFrame Camera::frame() const noexcept {
        const std::array<float, 16>& matrix = this->transform.matrix;
        return {
            {matrix[3], matrix[7], matrix[11]},
            math::Float3{matrix[0], matrix[4], matrix[8]}.normalized(),
            math::Float3{matrix[1], matrix[5], matrix[9]}.normalized(),
            math::Float3{-matrix[2], -matrix[6], -matrix[10]}.normalized(),
        };
    }

    CameraMatrices Camera::matrices() const noexcept {
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
            const float width           = perspective->screen_window.maximum.x - perspective->screen_window.minimum.x;
            const float height          = perspective->screen_window.maximum.y - perspective->screen_window.minimum.y;
            const float inverse_tangent = 1.0f / std::tan(perspective->vertical_fov * std::numbers::pi_v<float> / 360.0f);
            projection                  = {
                2.0f * inverse_tangent / width,
                0.0f,
                (perspective->screen_window.maximum.x + perspective->screen_window.minimum.x) / width,
                0.0f,
                0.0f,
                2.0f * inverse_tangent / height,
                (perspective->screen_window.maximum.y + perspective->screen_window.minimum.y) / height,
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
            const float width                          = orthographic.screen_window.maximum.x - orthographic.screen_window.minimum.x;
            const float height                         = orthographic.screen_window.maximum.y - orthographic.screen_window.minimum.y;
            projection                                 = {
                2.0f / width,
                0.0f,
                0.0f,
                -(orthographic.screen_window.maximum.x + orthographic.screen_window.minimum.x) / width,
                0.0f,
                2.0f / height,
                0.0f,
                -(orthographic.screen_window.maximum.y + orthographic.screen_window.minimum.y) / height,
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
        const math::Transform view_transform{view};
        const math::Transform projection_transform{projection};
        const math::Transform inverse_projection_transform{inverse_projection};
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

    math::Bounds3 particle_bounds(const ParticleSet& particles) noexcept {
        math::Bounds3 result = math::Bounds3::empty();
        for (std::size_t index = 0; index != particles.positions.size(); ++index) {
            const math::Float3 position = particles.positions[index];
            const float radius          = particles.radii[index];
            result.include(math::Float3{position.x - radius, position.y - radius, position.z - radius});
            result.include(math::Float3{position.x + radius, position.y + radius, position.z + radius});
        }
        return result;
    }

    namespace {
        bool include_primitive_bounds(math::Bounds3& bounds, const SceneView& scene, const Primitive& primitive, const math::Transform& parent) noexcept {
            math::Bounds3 local{};
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

    math::Bounds3 SceneView::bounds() const noexcept {
        math::Bounds3 result = math::Bounds3::empty();
        for (const Instance& instance : this->resources.instances) {
            if (!instance.visible) continue;
            const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance.prototype, &Prototype::id);
            for (const Primitive& primitive : prototype.primitives) include_primitive_bounds(result, *this, primitive, instance.transform);
        }
        return result;
    }

    std::optional<math::Bounds3> SceneView::local_bounds(const InstanceId instance_id) const noexcept {
        const std::vector<Instance>::const_iterator instance = std::ranges::find(this->resources.instances, instance_id, &Instance::id);
        if (instance == this->resources.instances.end()) return std::nullopt;
        const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance->prototype, &Prototype::id);
        math::Bounds3 result       = math::Bounds3::empty();
        bool found_any{};
        for (const Primitive& primitive : prototype.primitives) found_any = include_primitive_bounds(result, *this, primitive, math::Transform{}) || found_any;
        if (!found_any) return std::nullopt;
        return result;
    }

    std::optional<math::Bounds3> SceneView::bounds(const std::span<const InstanceId> instances) const noexcept {
        if (instances.empty()) return std::nullopt;
        math::Bounds3 result = math::Bounds3::empty();
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

    const Camera& Scene::camera() const noexcept {
        return *std::ranges::find(this->resources.cameras, this->active_camera, &Camera::id);
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
        this->mark_changed(SceneChange::All);
    }

    void Scene::mark_changed(const SceneChange changes) noexcept {
        ++this->current_revision.number;
        this->current_revision.changes = this->current_revision.changes | changes;
    }

} // namespace spectra::scene
