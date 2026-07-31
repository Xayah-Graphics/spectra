module spectra.scene.schema;

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
                for (std::uint32_t inner = 0; inner < 4; ++inner)
                    result.matrix[row * 4u + column] += this->matrix[row * 4u + inner] * child.matrix[inner * 4u + column];
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
        const Float3 forward = (target - position).normalized();
        const Float3 right   = forward.cross(up).normalized();
        const Float3 actual_up = right.cross(forward);
        return Transform{{
            right.x, actual_up.x, -forward.x, position.x,
            right.y, actual_up.y, -forward.y, position.y,
            right.z, actual_up.z, -forward.z, position.z,
            0.0f, 0.0f, 0.0f, 1.0f,
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
                for (const float z : {this->minimum.z, this->maximum.z})
                    result.include(transform.transform_point({x, y, z}));
        return result;
    }

    namespace {
        [[nodiscard]] float triangle_area(
            const Float3 first,
            const Float3 second,
            const Float3 third) noexcept {
            return 0.5f * (second - first).cross(third - first).length();
        }

        [[nodiscard]] float radians(const float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

    }

    Bounds3 geometry_bounds(const Geometry& geometry) noexcept {
        return std::visit(
            [](const auto& data) -> Bounds3 {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, TriangleMeshGeometry>) {
                    Bounds3 result = Bounds3::empty();
                    for (const Float3 position : data.positions) result.include(position);
                    return result;
                }
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>)
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
                    for (std::size_t index = 0; index < data.indices.size(); index += 3)
                        area += triangle_area(
                            data.positions[data.indices[index]],
                            data.positions[data.indices[index + 1]],
                            data.positions[data.indices[index + 2]]);
                    return area;
                }
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, SphereGeometry>)
                    return radians(data.phi_max) * data.radius * (data.z_max - data.z_min);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, BoxGeometry>) {
                    const Float3 extent = data.bounds.diagonal();
                    return 2.0f * (extent.x * extent.y + extent.x * extent.z + extent.y * extent.z);
                }
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, RectangleGeometry>)
                    return (data.maximum.x - data.minimum.x) * (data.maximum.y - data.minimum.y);
                else if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, DiskGeometry>)
                    return 0.5f * radians(data.phi_max) *
                        (data.radius * data.radius - data.inner_radius * data.inner_radius);
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
            transform[0], transform[4], transform[8], -(transform[0] * transform[3] + transform[4] * transform[7] + transform[8] * transform[11]),
            transform[1], transform[5], transform[9], -(transform[1] * transform[3] + transform[5] * transform[7] + transform[9] * transform[11]),
            transform[2], transform[6], transform[10], -(transform[2] * transform[3] + transform[6] * transform[7] + transform[10] * transform[11]),
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        std::array<float, 16> projection{};
        std::array<float, 16> inverse_projection{};
        if (const PerspectiveCameraData* perspective = std::get_if<PerspectiveCameraData>(&this->data)) {
            const float width           = perspective->screen.maximum.x - perspective->screen.minimum.x;
            const float height          = perspective->screen.maximum.y - perspective->screen.minimum.y;
            const float inverse_tangent = 1.0f / std::tan(perspective->vertical_fov * std::numbers::pi_v<float> / 360.0f);
            projection = {
                2.0f * inverse_tangent / width, 0.0f, (perspective->screen.maximum.x + perspective->screen.minimum.x) / width, 0.0f,
                0.0f, 2.0f * inverse_tangent / height, (perspective->screen.maximum.y + perspective->screen.minimum.y) / height, 0.0f,
                0.0f, 0.0f, perspective->far_plane / (perspective->near_plane - perspective->far_plane), perspective->far_plane * perspective->near_plane / (perspective->near_plane - perspective->far_plane),
                0.0f, 0.0f, -1.0f, 0.0f,
            };
            inverse_projection = {
                1.0f / projection[0], 0.0f, 0.0f, projection[2] / projection[0],
                0.0f, 1.0f / projection[5], 0.0f, projection[6] / projection[5],
                0.0f, 0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, 1.0f / projection[11], projection[10] / projection[11],
            };
        } else {
            const OrthographicCameraData& orthographic = std::get<OrthographicCameraData>(this->data);
            const float width                           = orthographic.screen.maximum.x - orthographic.screen.minimum.x;
            const float height                          = orthographic.screen.maximum.y - orthographic.screen.minimum.y;
            projection = {
                2.0f / width, 0.0f, 0.0f, -(orthographic.screen.maximum.x + orthographic.screen.minimum.x) / width,
                0.0f, 2.0f / height, 0.0f, -(orthographic.screen.maximum.y + orthographic.screen.minimum.y) / height,
                0.0f, 0.0f, 1.0f / (orthographic.near_plane - orthographic.far_plane), orthographic.near_plane / (orthographic.near_plane - orthographic.far_plane),
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            inverse_projection = {
                1.0f / projection[0], 0.0f, 0.0f, -projection[3] / projection[0],
                0.0f, 1.0f / projection[5], 0.0f, -projection[7] / projection[5],
                0.0f, 0.0f, 1.0f / projection[10], -projection[11] / projection[10],
                0.0f, 0.0f, 0.0f, 1.0f,
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

    BlackbodySpectrum::BlackbodySpectrum(const float temperature) noexcept
        : temperature(temperature) {
        if (temperature <= 0.0f) return;
        constexpr float speed_of_light = 299792458.0f;
        constexpr float planck_constant = 6.62606957e-34f;
        constexpr float boltzmann_constant = 1.3806488e-23f;
        const float maximum_wavelength = 2.8977721e-3f / temperature * 1.0e9f;
        const float wavelength_meters = maximum_wavelength * 1.0e-9f;
        this->normalization =
            std::pow(wavelength_meters, 5.0f) *
            (
                std::exp(
                    planck_constant * speed_of_light /
                    (wavelength_meters * boltzmann_constant * temperature)) -
                1.0f
            ) /
            (2.0f * planck_constant * speed_of_light * speed_of_light);
    }

    float BlackbodySpectrum::evaluate(const float wavelength) const noexcept {
        if (this->temperature <= 0.0f) return 0.0f;
        constexpr float speed_of_light = 299792458.0f;
        constexpr float planck_constant = 6.62606957e-34f;
        constexpr float boltzmann_constant = 1.3806488e-23f;
        const float wavelength_meters = wavelength * 1.0e-9f;
        return
            (2.0f * planck_constant * speed_of_light * speed_of_light) /
            (
                std::pow(wavelength_meters, 5.0f) *
                (
                    std::exp(
                        planck_constant * speed_of_light /
                        (wavelength_meters * boltzmann_constant * this->temperature)) -
                    1.0f
                )
            ) *
            this->normalization;
    }

    float PiecewiseLinearSpectrum::evaluate(const float wavelength) const noexcept {
        if (this->wavelengths.empty() ||
            wavelength < this->wavelengths.front() ||
            wavelength > this->wavelengths.back())
            return 0.0f;
        const std::vector<float>::const_iterator upper =
            std::ranges::upper_bound(this->wavelengths, wavelength);
        if (upper == this->wavelengths.begin()) return this->values.front();
        if (upper == this->wavelengths.end()) return this->values.back();
        const std::size_t upper_index =
            static_cast<std::size_t>(std::distance(this->wavelengths.begin(), upper));
        const std::size_t lower_index = upper_index - 1;
        const float value =
            (wavelength - this->wavelengths[lower_index]) /
            (this->wavelengths[upper_index] - this->wavelengths[lower_index]);
        return std::lerp(this->values[lower_index], this->values[upper_index], value);
    }

    Bounds3 particle_bounds(
        const ParticleSet& particles) noexcept {
        Bounds3 result = Bounds3::empty();
        for (std::size_t index = 0;
             index != particles.positions.size();
             ++index) {
            const Float3 position =
                particles.positions[index];
            const float radius =
                particles.radii[index];
            result.include(Float3{position.x - radius, position.y - radius, position.z - radius});
            result.include(Float3{position.x + radius, position.y + radius, position.z + radius});
        }
        return result;
    }

} // namespace spectra::scene
