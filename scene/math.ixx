export module spectra.scene:math;

import std;

namespace spectra::scene {
    export struct Vector3 {
        float x{};
        float y{};
        float z{};
    };

    export struct Vector4 {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
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

    export struct PbrtSceneTransform {
        std::array<float, 16> matrix{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        std::array<float, 16> inverse{
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    };

    export struct PbrtSceneTransformSet {
        PbrtSceneTransform start{};
        PbrtSceneTransform end{};
        float startTime{0.0f};
        float endTime{1.0f};
        bool animated{false};
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

    export [[nodiscard]] constexpr float length_squared(const Vector3 value) noexcept {
        return dot(value, value);
    }

    export [[nodiscard]] bool is_finite(const Vector3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    export [[nodiscard]] float length(const Vector3 value) {
        return std::sqrt(length_squared(value));
    }

    export [[nodiscard]] Vector3 normalize(const Vector3 value, const std::string_view context) {
        const float vector_length = length(value);
        if (!std::isfinite(vector_length) || vector_length <= 0.0f) throw std::runtime_error(std::format("{} contains a zero-length vector", context));
        return value / vector_length;
    }
} // namespace spectra::scene
