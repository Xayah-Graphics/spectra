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

        [[nodiscard]] float& at(const std::size_t row, const std::size_t column) {
            return this->values.at(row * 4u + column);
        }

        [[nodiscard]] const float& at(const std::size_t row, const std::size_t column) const {
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

    export [[nodiscard]] Matrix4 identity_matrix() {
        Matrix4 matrix{};
        matrix.at(0u, 0u) = 1.0f;
        matrix.at(1u, 1u) = 1.0f;
        matrix.at(2u, 2u) = 1.0f;
        matrix.at(3u, 3u) = 1.0f;
        return matrix;
    }

    export [[nodiscard]] Matrix4 multiply_matrix(const Matrix4& lhs, const Matrix4& rhs) {
        Matrix4 result{};
        for (std::size_t row = 0; row < 4u; ++row) {
            for (std::size_t column = 0; column < 4u; ++column) {
                for (std::size_t index = 0; index < 4u; ++index) result.at(row, column) += lhs.at(row, index) * rhs.at(index, column);
            }
        }
        return result;
    }

    export [[nodiscard]] float dot_vector(const Vector3 lhs, const Vector3 rhs) {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    export [[nodiscard]] Vector3 cross_vector(const Vector3 lhs, const Vector3 rhs) {
        return Vector3{
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x,
        };
    }

    export [[nodiscard]] Vector3 subtract_vector(const Vector3 lhs, const Vector3 rhs) {
        return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    export [[nodiscard]] Vector3 add_vector(const Vector3 lhs, const Vector3 rhs) {
        return Vector3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }

    export [[nodiscard]] Vector3 scale_vector(const Vector3 value, const float scale) {
        return Vector3{value.x * scale, value.y * scale, value.z * scale};
    }

    export [[nodiscard]] float length_vector(const Vector3 value) {
        return std::sqrt(dot_vector(value, value));
    }

    export [[nodiscard]] Vector3 normalize_vector(const Vector3 value) {
        const float length = length_vector(value);
        if (!std::isfinite(length) || length <= 0.0f) throw std::runtime_error("Cannot normalize a zero-length rasterizer vector");
        return Vector3{value.x / length, value.y / length, value.z / length};
    }

    export [[nodiscard]] CameraBasis camera_basis(const Vector3 eye, const Vector3 target) {
        CameraBasis basis{};
        basis.eye     = eye;
        basis.target  = target;
        basis.forward = normalize_vector(subtract_vector(basis.target, basis.eye));
        constexpr Vector3 world_up{0.0f, 1.0f, 0.0f};
        basis.side = normalize_vector(cross_vector(basis.forward, world_up));
        basis.up   = cross_vector(basis.side, basis.forward);
        return basis;
    }

    export [[nodiscard]] CameraBasis orbit_camera_basis(const Vector3 target, const float yaw_radians, const float pitch_radians, const float distance) {
        if (!std::isfinite(distance) || distance <= 0.0f) throw std::runtime_error("Rasterizer viewport camera distance must be positive");
        const float cos_pitch = std::cos(pitch_radians);
        const Vector3 direction{
            std::sin(yaw_radians) * cos_pitch,
            std::sin(pitch_radians),
            std::cos(yaw_radians) * cos_pitch,
        };
        return camera_basis(add_vector(target, scale_vector(direction, distance)), target);
    }

    export [[nodiscard]] Matrix4 transform_matrix(const Transform& transform) {
        const Quaternion rotation = transform.rotation;
        const float length_squared = rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w;
        if (!std::isfinite(length_squared) || length_squared <= 0.0f) throw std::runtime_error("Rasterizer scene transform contains an invalid quaternion");
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        const float x              = rotation.x * inverse_length;
        const float y              = rotation.y * inverse_length;
        const float z              = rotation.z * inverse_length;
        const float w              = rotation.w * inverse_length;

        Matrix4 scale = identity_matrix();
        scale.at(0u, 0u) = transform.scale.x;
        scale.at(1u, 1u) = transform.scale.y;
        scale.at(2u, 2u) = transform.scale.z;

        Matrix4 rotate = identity_matrix();
        rotate.at(0u, 0u) = 1.0f - 2.0f * y * y - 2.0f * z * z;
        rotate.at(0u, 1u) = 2.0f * x * y + 2.0f * w * z;
        rotate.at(0u, 2u) = 2.0f * x * z - 2.0f * w * y;
        rotate.at(1u, 0u) = 2.0f * x * y - 2.0f * w * z;
        rotate.at(1u, 1u) = 1.0f - 2.0f * x * x - 2.0f * z * z;
        rotate.at(1u, 2u) = 2.0f * y * z + 2.0f * w * x;
        rotate.at(2u, 0u) = 2.0f * x * z + 2.0f * w * y;
        rotate.at(2u, 1u) = 2.0f * y * z - 2.0f * w * x;
        rotate.at(2u, 2u) = 1.0f - 2.0f * x * x - 2.0f * y * y;

        Matrix4 translate = identity_matrix();
        translate.at(3u, 0u) = transform.position.x;
        translate.at(3u, 1u) = transform.position.y;
        translate.at(3u, 2u) = transform.position.z;
        return multiply_matrix(multiply_matrix(scale, rotate), translate);
    }

    export [[nodiscard]] Matrix4 look_at_matrix(const Vector3 eye, const Vector3 target) {
        const CameraBasis basis = camera_basis(eye, target);
        Matrix4 view = identity_matrix();
        view.at(0u, 0u) = basis.side.x;
        view.at(1u, 0u) = basis.side.y;
        view.at(2u, 0u) = basis.side.z;
        view.at(0u, 1u) = basis.up.x;
        view.at(1u, 1u) = basis.up.y;
        view.at(2u, 1u) = basis.up.z;
        view.at(0u, 2u) = -basis.forward.x;
        view.at(1u, 2u) = -basis.forward.y;
        view.at(2u, 2u) = -basis.forward.z;
        view.at(3u, 0u) = -dot_vector(basis.side, eye);
        view.at(3u, 1u) = -dot_vector(basis.up, eye);
        view.at(3u, 2u) = dot_vector(basis.forward, eye);
        return view;
    }

    export [[nodiscard]] Matrix4 inverse_look_at_matrix(const CameraBasis& basis) {
        Matrix4 inverse_view = identity_matrix();
        inverse_view.at(0u, 0u) = basis.side.x;
        inverse_view.at(0u, 1u) = basis.side.y;
        inverse_view.at(0u, 2u) = basis.side.z;
        inverse_view.at(1u, 0u) = basis.up.x;
        inverse_view.at(1u, 1u) = basis.up.y;
        inverse_view.at(1u, 2u) = basis.up.z;
        inverse_view.at(2u, 0u) = -basis.forward.x;
        inverse_view.at(2u, 1u) = -basis.forward.y;
        inverse_view.at(2u, 2u) = -basis.forward.z;
        inverse_view.at(3u, 0u) = basis.eye.x;
        inverse_view.at(3u, 1u) = basis.eye.y;
        inverse_view.at(3u, 2u) = basis.eye.z;
        return inverse_view;
    }

    export [[nodiscard]] Matrix4 perspective_matrix(const float vertical_fov_degrees, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float f = 1.0f / std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
        Matrix4 projection{};
        projection.at(0u, 0u) = f / aspect;
        projection.at(1u, 1u) = -f;
        projection.at(2u, 2u) = far_plane / (near_plane - far_plane);
        projection.at(2u, 3u) = -1.0f;
        projection.at(3u, 2u) = -(far_plane * near_plane) / (far_plane - near_plane);
        return projection;
    }

    export [[nodiscard]] Matrix4 inverse_perspective_matrix(const float vertical_fov_degrees, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float f = 1.0f / std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f);
        const float depth_scale = -(far_plane * near_plane) / (far_plane - near_plane);
        Matrix4 inverse_projection{};
        inverse_projection.at(0u, 0u) = aspect / f;
        inverse_projection.at(1u, 1u) = -1.0f / f;
        inverse_projection.at(2u, 3u) = 1.0f / depth_scale;
        inverse_projection.at(3u, 2u) = -1.0f;
        inverse_projection.at(3u, 3u) = far_plane / (near_plane - far_plane) / depth_scale;
        return inverse_projection;
    }

    export [[nodiscard]] Matrix4 orthographic_matrix(const float vertical_size, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_size) || vertical_size <= 0.0f) throw std::runtime_error("Rasterizer orthographic vertical size must be positive");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float horizontal_size = vertical_size * aspect;
        Matrix4 projection = identity_matrix();
        projection.at(0u, 0u) = 2.0f / horizontal_size;
        projection.at(1u, 1u) = -2.0f / vertical_size;
        projection.at(2u, 2u) = 1.0f / (near_plane - far_plane);
        projection.at(3u, 2u) = near_plane / (near_plane - far_plane);
        return projection;
    }

    export [[nodiscard]] Matrix4 inverse_orthographic_matrix(const float vertical_size, const float aspect, const float near_plane, const float far_plane) {
        if (!std::isfinite(vertical_size) || vertical_size <= 0.0f) throw std::runtime_error("Rasterizer orthographic vertical size must be positive");
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Rasterizer camera aspect ratio must be positive");
        if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0.0f || far_plane <= near_plane) throw std::runtime_error("Rasterizer camera clipping planes are invalid");
        const float horizontal_size = vertical_size * aspect;
        Matrix4 inverse_projection = identity_matrix();
        inverse_projection.at(0u, 0u) = horizontal_size * 0.5f;
        inverse_projection.at(1u, 1u) = -vertical_size * 0.5f;
        inverse_projection.at(2u, 2u) = near_plane - far_plane;
        inverse_projection.at(3u, 2u) = -near_plane;
        return inverse_projection;
    }

    export [[nodiscard]] float perspective_pan_scale(const float distance, const float vertical_fov_degrees, const float viewport_height) {
        if (!std::isfinite(distance) || distance <= 0.0f) throw std::runtime_error("Rasterizer viewport camera distance must be positive");
        if (!std::isfinite(viewport_height) || viewport_height <= 0.0f) throw std::runtime_error("Rasterizer viewport height must be positive");
        if (!std::isfinite(vertical_fov_degrees) || vertical_fov_degrees <= 0.0f || vertical_fov_degrees >= 179.0f) throw std::runtime_error("Rasterizer camera vertical FOV must be in (0, 179)");
        return 2.0f * distance * std::tan(vertical_fov_degrees * std::numbers::pi_v<float> / 360.0f) / viewport_height;
    }

    export [[nodiscard]] Vector3 transform_point(const Matrix4& matrix, const Vector3 point) {
        return Vector3{
            point.x * matrix.at(0u, 0u) + point.y * matrix.at(1u, 0u) + point.z * matrix.at(2u, 0u) + matrix.at(3u, 0u),
            point.x * matrix.at(0u, 1u) + point.y * matrix.at(1u, 1u) + point.z * matrix.at(2u, 1u) + matrix.at(3u, 1u),
            point.x * matrix.at(0u, 2u) + point.y * matrix.at(1u, 2u) + point.z * matrix.at(2u, 2u) + matrix.at(3u, 2u),
        };
    }
} // namespace spectra::rasterizer::math
