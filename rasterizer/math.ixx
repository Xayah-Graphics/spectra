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

    export [[nodiscard]] Vector3 transform_point(const Matrix4& matrix, const Vector3 point) {
        return Vector3{
            point.x * matrix(0u, 0u) + point.y * matrix(1u, 0u) + point.z * matrix(2u, 0u) + matrix(3u, 0u),
            point.x * matrix(0u, 1u) + point.y * matrix(1u, 1u) + point.z * matrix(2u, 1u) + matrix(3u, 1u),
            point.x * matrix(0u, 2u) + point.y * matrix(1u, 2u) + point.z * matrix(2u, 2u) + matrix(3u, 2u),
        };
    }
} // namespace spectra::rasterizer::math
