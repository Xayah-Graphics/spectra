export module spectra.rasterizer.math;

import std;

namespace spectra::rasterizer::math {
    export struct Vector3 {
        float x{};
        float y{};
        float z{};
    };

    export struct Quaternion {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct Transform {
        Vector3 position{};
        Quaternion rotation{};
        Vector3 scale{1.0f, 1.0f, 1.0f};
    };

    export struct Matrix4 {
        std::array<float, 16> values{};

        [[nodiscard]] static Matrix4 identity() {
            Matrix4 matrix{};
            matrix(0u, 0u) = 1.0f;
            matrix(1u, 1u) = 1.0f;
            matrix(2u, 2u) = 1.0f;
            matrix(3u, 3u) = 1.0f;
            return matrix;
        }

        [[nodiscard]] float& operator()(const std::size_t row, const std::size_t column) {
            return this->values.at(row * 4u + column);
        }

        [[nodiscard]] const float& operator()(const std::size_t row, const std::size_t column) const {
            return this->values.at(row * 4u + column);
        }
    };

    export struct CameraBasis {
        Vector3 eye{};
        Vector3 target{};
        Vector3 forward{};
        Vector3 side{};
        Vector3 up{};
    };

    export [[nodiscard]] constexpr Vector3 operator+(const Vector3 lhs, const Vector3 rhs) noexcept {
        return Vector3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }

    export [[nodiscard]] constexpr Vector3 operator-(const Vector3 lhs, const Vector3 rhs) noexcept {
        return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    export [[nodiscard]] constexpr Vector3 operator-(const Vector3 value) noexcept {
        return Vector3{-value.x, -value.y, -value.z};
    }

    export [[nodiscard]] constexpr Vector3 operator*(const Vector3 value, const float scale) noexcept {
        return Vector3{value.x * scale, value.y * scale, value.z * scale};
    }

    export [[nodiscard]] constexpr Vector3 operator*(const float scale, const Vector3 value) noexcept {
        return value * scale;
    }

    export [[nodiscard]] constexpr Vector3 operator/(const Vector3 value, const float scale) noexcept {
        return Vector3{value.x / scale, value.y / scale, value.z / scale};
    }

    export constexpr Vector3& operator+=(Vector3& lhs, const Vector3 rhs) noexcept {
        lhs = lhs + rhs;
        return lhs;
    }

    export constexpr Vector3& operator-=(Vector3& lhs, const Vector3 rhs) noexcept {
        lhs = lhs - rhs;
        return lhs;
    }

    export constexpr Vector3& operator*=(Vector3& value, const float scale) noexcept {
        value = value * scale;
        return value;
    }

    export constexpr Vector3& operator/=(Vector3& value, const float scale) noexcept {
        value = value / scale;
        return value;
    }

    export [[nodiscard]] Matrix4 operator*(const Matrix4& lhs, const Matrix4& rhs) {
        Matrix4 result{};
        for (std::size_t row = 0; row < 4u; ++row) {
            for (std::size_t column = 0; column < 4u; ++column) {
                for (std::size_t index = 0; index < 4u; ++index) result(row, column) += lhs(row, index) * rhs(index, column);
            }
        }
        return result;
    }

    export [[nodiscard]] constexpr float dot(const Vector3 lhs, const Vector3 rhs) noexcept {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    export [[nodiscard]] constexpr Vector3 cross(const Vector3 lhs, const Vector3 rhs) noexcept {
        return Vector3{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }

    export [[nodiscard]] float length(const Vector3 value) {
        return std::sqrt(dot(value, value));
    }

    export [[nodiscard]] Vector3 normalize(const Vector3 value) {
        const float vector_length = length(value);
        if (!std::isfinite(vector_length) || vector_length <= 0.0f) throw std::runtime_error("Cannot normalize a zero-length rasterizer vector");
        return value / vector_length;
    }

    export [[nodiscard]] CameraBasis camera_basis(const Vector3 eye, const Vector3 target, const Vector3 up) {
        CameraBasis basis{};
        basis.eye     = eye;
        basis.target  = target;
        basis.forward = normalize(basis.target - basis.eye);
        const Vector3 normalized_up = normalize(up);
        basis.side = normalize(cross(normalized_up, basis.forward));
        basis.up   = cross(basis.forward, basis.side);
        return basis;
    }

    export [[nodiscard]] Matrix4 transform_matrix(const Transform& transform) {
        const Quaternion rotation  = transform.rotation;
        const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(length_squared) || length_squared <= 0.0f) throw std::runtime_error("Rasterizer scene transform contains an invalid quaternion");
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        const float x              = rotation.x * inverse_length;
        const float y              = rotation.y * inverse_length;
        const float z              = rotation.z * inverse_length;
        const float w              = rotation.w * inverse_length;

        Matrix4 scale = Matrix4::identity();
        scale(0u, 0u) = transform.scale.x;
        scale(1u, 1u) = transform.scale.y;
        scale(2u, 2u) = transform.scale.z;

        Matrix4 rotate = Matrix4::identity();
        rotate(0u, 0u) = 1.0f - 2.0f * y * y - 2.0f * z * z;
        rotate(0u, 1u) = 2.0f * x * y + 2.0f * w * z;
        rotate(0u, 2u) = 2.0f * x * z - 2.0f * w * y;
        rotate(1u, 0u) = 2.0f * x * y - 2.0f * w * z;
        rotate(1u, 1u) = 1.0f - 2.0f * x * x - 2.0f * z * z;
        rotate(1u, 2u) = 2.0f * y * z + 2.0f * w * x;
        rotate(2u, 0u) = 2.0f * x * z + 2.0f * w * y;
        rotate(2u, 1u) = 2.0f * y * z - 2.0f * w * x;
        rotate(2u, 2u) = 1.0f - 2.0f * x * x - 2.0f * y * y;

        Matrix4 translate = Matrix4::identity();
        translate(3u, 0u) = transform.position.x;
        translate(3u, 1u) = transform.position.y;
        translate(3u, 2u) = transform.position.z;
        return scale * rotate * translate;
    }

    export [[nodiscard]] Matrix4 look_at_matrix(const CameraBasis& basis) {
        Matrix4 view = Matrix4::identity();
        view(0u, 0u) = basis.side.x;
        view(1u, 0u)            = basis.side.y;
        view(2u, 0u)            = basis.side.z;
        view(0u, 1u)            = basis.up.x;
        view(1u, 1u)            = basis.up.y;
        view(2u, 1u)            = basis.up.z;
        view(0u, 2u)            = -basis.forward.x;
        view(1u, 2u)            = -basis.forward.y;
        view(2u, 2u)            = -basis.forward.z;
        view(3u, 0u)            = -dot(basis.side, basis.eye);
        view(3u, 1u)            = -dot(basis.up, basis.eye);
        view(3u, 2u)            = dot(basis.forward, basis.eye);
        return view;
    }

    export [[nodiscard]] Matrix4 inverse_look_at_matrix(const CameraBasis& basis) {
        Matrix4 inverse_view = Matrix4::identity();
        inverse_view(0u, 0u) = basis.side.x;
        inverse_view(0u, 1u) = basis.side.y;
        inverse_view(0u, 2u) = basis.side.z;
        inverse_view(1u, 0u) = basis.up.x;
        inverse_view(1u, 1u) = basis.up.y;
        inverse_view(1u, 2u) = basis.up.z;
        inverse_view(2u, 0u) = -basis.forward.x;
        inverse_view(2u, 1u) = -basis.forward.y;
        inverse_view(2u, 2u) = -basis.forward.z;
        inverse_view(3u, 0u) = basis.eye.x;
        inverse_view(3u, 1u) = basis.eye.y;
        inverse_view(3u, 2u) = basis.eye.z;
        return inverse_view;
    }

    export [[nodiscard]] Matrix4 perspective_matrix(const float vertical_fov_degrees, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float f = 1.0f / std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
        Matrix4 projection{};
        projection(0u, 0u) = f / aspect;
        projection(1u, 1u) = -f;
        projection(2u, 2u) = far_plane / (near_plane - far_plane);
        projection(2u, 3u) = -1.0f;
        projection(3u, 2u) = -(far_plane * near_plane) / (far_plane - near_plane);
        return projection;
    }

    export [[nodiscard]] Matrix4 inverse_perspective_matrix(const float vertical_fov_degrees, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float f           = 1.0f / std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
        const float depth_scale = -(far_plane * near_plane) / (far_plane - near_plane);
        Matrix4 inverse_projection{};
        inverse_projection(0u, 0u) = aspect / f;
        inverse_projection(1u, 1u) = -1.0f / f;
        inverse_projection(2u, 3u) = 1.0f / depth_scale;
        inverse_projection(3u, 2u) = -1.0f;
        inverse_projection(3u, 3u) = far_plane / (near_plane - far_plane) / depth_scale;
        return inverse_projection;
    }

    export [[nodiscard]] Vector3 transform_point(const Matrix4& matrix, const Vector3 point) {
        return Vector3{
            point.x * matrix(0u, 0u) + point.y * matrix(1u, 0u) + point.z * matrix(2u, 0u) + matrix(3u, 0u),
            point.x * matrix(0u, 1u) + point.y * matrix(1u, 1u) + point.z * matrix(2u, 1u) + matrix(3u, 1u),
            point.x * matrix(0u, 2u) + point.y * matrix(1u, 2u) + point.z * matrix(2u, 2u) + matrix(3u, 2u),
        };
    }
} // namespace spectra::rasterizer::math
