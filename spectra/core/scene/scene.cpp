module spectra.scene;

import std;

namespace spectra::scene {
    namespace {
        constexpr std::array<double, 16> gauss_legendre_nodes{
            0.048307665687738316,
            0.14447196158279649,
            0.23928736225213707,
            0.33186860228212765,
            0.42135127613063535,
            0.50689990893222939,
            0.58771575724076233,
            0.66304426693021520,
            0.73218211874028968,
            0.79448379596794241,
            0.84936761373256997,
            0.89632115576605212,
            0.93490607593773969,
            0.96476225558750643,
            0.98561151154526834,
            0.99726386184948156,
        };

        constexpr std::array<double, 16> gauss_legendre_weights{
            0.096540088514727801,
            0.095638720079274859,
            0.093844399080804566,
            0.091173878695763885,
            0.087652093004403811,
            0.083311924226946755,
            0.078193895787070306,
            0.072345794108848506,
            0.065822222776361847,
            0.058684093478535547,
            0.050998059262376176,
            0.042835898022226681,
            0.034273862913021433,
            0.025392065309262059,
            0.016274394730905671,
            0.007018610009470097,
        };

        template <std::invocable<double> Function>
        [[nodiscard]] double integrate_gauss_legendre_32(Function&& function, const double minimum, const double maximum) noexcept {
            const double midpoint = std::midpoint(minimum, maximum);
            const double extent   = (maximum - minimum) * 0.5;
            double result{};
            for (std::size_t index = 0; index != gauss_legendre_nodes.size(); ++index) {
                const double offset = extent * gauss_legendre_nodes[index];
                result += gauss_legendre_weights[index] * (function(midpoint - offset) + function(midpoint + offset));
            }
            return extent * result;
        }

        [[nodiscard]] float triangle_area(const math::Float3 first, const math::Float3 second, const math::Float3 third) noexcept {
            return 0.5f * (second - first).cross(third - first).length();
        }

        [[nodiscard]] float radians(const float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

    } // namespace

    EntityKind entity_kind(const EntityReference& entity) noexcept {
        return std::visit(
            []<typename Type>(const Type&) {
                if constexpr (std::same_as<Type, std::monostate>) return EntityKind::None;
                else if constexpr (std::same_as<Type, InstanceId>) return EntityKind::Instance;
                else if constexpr (std::same_as<Type, CameraId>) return EntityKind::Camera;
                else if constexpr (std::same_as<Type, LightId>) return EntityKind::Light;
                else if constexpr (std::same_as<Type, EntityReference::AreaEmitter>) return EntityKind::AreaEmitter;
                else if constexpr (std::same_as<Type, ParticleSetId>) return EntityKind::ParticleSet;
                else if constexpr (std::same_as<Type, VolumeId>) return EntityKind::Volume;
                else return EntityKind::NeuralField;
            },
            entity.data);
    }

    std::uint64_t entity_id(const EntityReference& entity) noexcept {
        return std::visit(
            []<typename Type>(const Type& value) -> std::uint64_t {
                if constexpr (std::same_as<Type, std::monostate>) return 0;
                else if constexpr (std::same_as<Type, EntityReference::AreaEmitter>) return value.light.value;
                else return value.value;
            },
            entity.data);
    }

    math::Bounds3 geometry_bounds(const Geometry& geometry) noexcept {
        return std::visit(
            [](const auto& data) -> math::Bounds3 {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    math::Bounds3 result = math::Bounds3::empty();
                    for (const math::Float3 position : data.positions) result.include(position);
                    return result;
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) return {{-data.radius, -data.radius, data.z_min}, {data.radius, data.radius, data.z_max}};
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) return data.bounds;
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) return {{data.minimum.x, data.minimum.y, 0.0f}, {data.maximum.x, data.maximum.y, 0.0f}};
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) return {{-data.radius, -data.radius, data.height}, {data.radius, data.radius, data.height}};
                else return {{-data.radius, -data.radius, data.z_min}, {data.radius, data.radius, data.z_max}};
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
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) return radians(data.phi_max) * data.radius * (data.z_max - data.z_min);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                    const math::Float3 extent = data.bounds.diagonal();
                    return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) return (data.maximum.x - data.minimum.x) * (data.maximum.y - data.minimum.y);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) return 0.5f * radians(data.phi_max) * (data.radius * data.radius - data.inner_radius * data.inner_radius);
                else return radians(data.phi_max) * data.radius * (data.z_max - data.z_min);
            },
            geometry.data);
    }

    float surface_area(const Geometry& geometry, const math::Transform& transform) noexcept {
        const math::Float3 x        = transform.transform_vector({1.0f, 0.0f, 0.0f});
        const math::Float3 y        = transform.transform_vector({0.0f, 1.0f, 0.0f});
        const math::Float3 z        = transform.transform_vector({0.0f, 0.0f, 1.0f});
        const math::Float3 normal_x = y.cross(z);
        const math::Float3 normal_y = z.cross(x);
        const math::Float3 normal_z = x.cross(y);
        const auto area_scale       = [&](const math::Float3 normal) { return (normal_x * normal.x + normal_y * normal.y + normal_z * normal.z).length(); };
        return std::visit(
            [&](const auto& data) -> float {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    float area{};
                    for (std::size_t index = 0; index < data.indices.size(); index += 3) area += triangle_area(transform.transform_point(data.positions[data.indices[index]]), transform.transform_point(data.positions[data.indices[index + 1]]), transform.transform_point(data.positions[data.indices[index + 2]]));
                    return area;
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>) {
                    const double radius  = data.radius;
                    const double phi_max = radians(data.phi_max);
                    const double area    = radius
                                      * integrate_gauss_legendre_32(
                                          [&](const double phi) {
                                              return integrate_gauss_legendre_32(
                                                  [&](const double height) {
                                                      const double radial = std::sqrt(radius * radius - height * height) / radius;
                                                      return area_scale({static_cast<float>(radial * std::cos(phi)), static_cast<float>(radial * std::sin(phi)), static_cast<float>(height / radius)});
                                                  },
                                                  data.z_min, data.z_max);
                                          },
                                          0.0, phi_max);
                    return static_cast<float>(area);
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                    const math::Float3 extent = data.bounds.diagonal();
                    return 2.0f * (extent.x * extent.y * normal_z.length() + extent.x * extent.z * normal_y.length() + extent.y * extent.z * normal_x.length());
                } else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>) return (data.maximum.x - data.minimum.x) * (data.maximum.y - data.minimum.y) * normal_z.length();
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>) return 0.5f * radians(data.phi_max) * (data.radius * data.radius - data.inner_radius * data.inner_radius) * normal_z.length();
                else {
                    const double area = data.radius * (data.z_max - data.z_min) * integrate_gauss_legendre_32([&](const double phi) { return area_scale({static_cast<float>(std::cos(phi)), static_cast<float>(std::sin(phi)), 0.0f}); }, 0.0, radians(data.phi_max));
                    return static_cast<float>(area);
                }
            },
            geometry.data);
    }

    math::Bounds3 sphere_set_bounds(const SphereSet& spheres) noexcept {
        math::Bounds3 result = math::Bounds3::empty();
        for (std::size_t index = 0; index != spheres.positions.size(); ++index) {
            const math::Float3 position = spheres.positions[index];
            const float radius          = spheres.radii[index];
            result.include(math::Float3{position.x - radius, position.y - radius, position.z - radius});
            result.include(math::Float3{position.x + radius, position.y + radius, position.z + radius});
        }
        return result;
    }

    FieldKind field_kind(const VolumeField& field) noexcept {
        return std::visit(
            []<typename Type>(const Type&) {
                if constexpr (std::same_as<Type, ScalarVolumeField>) return FieldKind::Float;
                else if constexpr (std::same_as<Type, VectorVolumeField>) return FieldKind::Float3;
                else if constexpr (std::same_as<Type, CategoryVolumeField>) return FieldKind::UInt32;
                else return FieldKind::MacFloat3;
            },
            field.data);
    }

    VolumeFieldSampling field_sampling(const VolumeField& field) noexcept {
        return std::visit(
            []<typename Type>(const Type& data) {
                if constexpr (std::same_as<Type, MacVolumeField>) return VolumeFieldSampling::Cell;
                else return data.sampling;
            },
            field.data);
    }

    VolumeVectorSpace field_vector_space(const VolumeField& field) noexcept {
        return std::visit(
            []<typename Type>(const Type& data) {
                if constexpr (std::same_as<Type, VectorVolumeField> || std::same_as<Type, MacVolumeField>) return data.vector_space;
                else return VolumeVectorSpace::Local;
            },
            field.data);
    }

    FieldKind field_kind(const ParticleField& field) noexcept {
        return std::visit(
            []<typename Type>(const Type&) {
                if constexpr (std::same_as<Type, ScalarParticleField>) return FieldKind::Float;
                else if constexpr (std::same_as<Type, VectorParticleField>) return FieldKind::Float3;
                else return FieldKind::UInt32;
            },
            field.data);
    }

    VolumeVectorSpace field_vector_space(const ParticleField& field) noexcept {
        return std::visit(
            []<typename Type>(const Type& data) {
                if constexpr (std::same_as<Type, VectorParticleField>) return data.vector_space;
                else return VolumeVectorSpace::Local;
            },
            field.data);
    }

    math::Bounds3 particle_set_bounds(const ParticleSet& particles) noexcept {
        return particles.domain.transformed(particles.transform);
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

    CameraFrame Camera::frame() const noexcept {
        const std::array<float, 16>& matrix = this->transform.matrix;
        return {
            {matrix[3], matrix[7], matrix[11]},
            math::Float3{matrix[0], matrix[4], matrix[8]}.normalized(),
            math::Float3{matrix[1], matrix[5], matrix[9]}.normalized(),
            math::Float3{-matrix[2], -matrix[6], -matrix[10]}.normalized(),
        };
    }

    CameraMatrices Camera::matrices() const {
        const math::Transform view_transform = this->transform.inverse();
        const std::array<float, 16>& view    = view_transform.matrix;
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
        const math::Transform projection_transform{projection};
        const math::Transform inverse_projection_transform{inverse_projection};
        return {
            view,
            projection,
            (projection_transform * view_transform).matrix,
            (this->transform * inverse_projection_transform).matrix,
        };
    }

    namespace {
        bool include_primitive_bounds(math::Bounds3& bounds, const ResolvedSceneView& scene, const Primitive& primitive, const math::Transform& parent) noexcept {
            math::Bounds3 local{};
            if (primitive.geometry.value != 0) local = geometry_bounds(*std::ranges::find(scene.resources.geometries, primitive.geometry, &Geometry::id));
            else if (primitive.spheres.value != 0) local = sphere_set_bounds(*std::ranges::find(scene.resources.sphere_sets, primitive.spheres, &SphereSet::id));
            else return false;
            bounds.include(local.transformed(parent * primitive.transform));
            return true;
        }
    } // namespace

    math::Bounds3 ResolvedSceneView::bounds() const noexcept {
        math::Bounds3 result = math::Bounds3::empty();
        for (const Instance& instance : this->resources.instances) {
            if (!instance.visible) continue;
            const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance.prototype, &Prototype::id);
            for (const Primitive& primitive : prototype.primitives) include_primitive_bounds(result, *this, primitive, instance.transform);
        }
        for (const Volume& volume : this->resources.volumes)
            if (volume.visible) result.include(volume.domain.transformed(volume.transform));
        for (const ParticleSet& particles : this->resources.particle_sets)
            if (particles.visible) result.include(particle_set_bounds(particles));
        for (const NeuralField& field : this->resources.neural_fields)
            if (field.visible) result.include(NeuralField::local_bounds.transformed(field.transform));
        return result;
    }

    std::optional<math::Bounds3> ResolvedSceneView::local_bounds(const InstanceId instance_id) const noexcept {
        const std::vector<Instance>::const_iterator instance = std::ranges::find(this->resources.instances, instance_id, &Instance::id);
        if (instance == this->resources.instances.end()) return std::nullopt;
        const Prototype& prototype = *std::ranges::find(this->resources.prototypes, instance->prototype, &Prototype::id);
        math::Bounds3 result       = math::Bounds3::empty();
        bool found_any{};
        for (const Primitive& primitive : prototype.primitives) found_any = include_primitive_bounds(result, *this, primitive, math::Transform{}) || found_any;
        if (!found_any) return std::nullopt;
        return result;
    }

    std::optional<math::Bounds3> ResolvedSceneView::bounds(const std::span<const InstanceId> instances) const noexcept {
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

    ResolvedSceneView Scene::view() const noexcept {
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
    }

    void Scene::mark_changed(const SceneChange changes) noexcept {
        ++this->current_revision.number;
        this->current_revision.changes = this->current_revision.changes | changes;
    }

} // namespace spectra::scene
