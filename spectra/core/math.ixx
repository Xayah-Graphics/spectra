export module spectra.math;

import std;

namespace spectra::math {
    export struct Float2 {
        float x{};
        float y{};

        auto operator<=>(const Float2&) const = default;
    };

    export struct Float3 {
        float x{};
        float y{};
        float z{};

        [[nodiscard]] constexpr Float3 operator+(const Float3 right) const noexcept {
            return {this->x + right.x, this->y + right.y, this->z + right.z};
        }
        [[nodiscard]] constexpr Float3 operator-(const Float3 right) const noexcept {
            return {this->x - right.x, this->y - right.y, this->z - right.z};
        }
        [[nodiscard]] constexpr Float3 operator-() const noexcept {
            return {-this->x, -this->y, -this->z};
        }
        [[nodiscard]] constexpr Float3 operator*(const float scale) const noexcept {
            return {this->x * scale, this->y * scale, this->z * scale};
        }
        [[nodiscard]] constexpr Float3 operator/(const float scale) const noexcept {
            return {this->x / scale, this->y / scale, this->z / scale};
        }
        [[nodiscard]] constexpr float dot(const Float3 right) const noexcept {
            return this->x * right.x + this->y * right.y + this->z * right.z;
        }
        [[nodiscard]] constexpr Float3 cross(const Float3 right) const noexcept {
            return {this->y * right.z - this->z * right.y, this->z * right.x - this->x * right.z, this->x * right.y - this->y * right.x};
        }
        [[nodiscard]] float length() const noexcept;
        [[nodiscard]] Float3 normalized() const noexcept;

        auto operator<=>(const Float3&) const = default;
    };

    export struct Float4 {
        float x{};
        float y{};
        float z{};
        float w{};

        auto operator<=>(const Float4&) const = default;
    };

    export struct UInt3 {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};

        auto operator<=>(const UInt3&) const = default;
    };

    export struct Transform {
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

        [[nodiscard]] Float3 transform_point(Float3 point) const noexcept;
        [[nodiscard]] Float3 transform_vector(Float3 vector) const noexcept;
        [[nodiscard]] Transform operator*(const Transform& child) const noexcept;
        [[nodiscard]] Transform inverse() const;
        [[nodiscard]] static Transform look_at(Float3 position, Float3 target, Float3 up) noexcept;

        auto operator<=>(const Transform&) const = default;
    };

    export struct Bounds3 {
        Float3 minimum{};
        Float3 maximum{};

        [[nodiscard]] static Bounds3 empty() noexcept;
        void include(Float3 point) noexcept;
        void include(Bounds3 bounds) noexcept;
        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] Float3 center() const noexcept;
        [[nodiscard]] Float3 diagonal() const noexcept;
        [[nodiscard]] float radius() const noexcept;
        [[nodiscard]] Bounds3 transformed(const Transform& transform) const noexcept;

        auto operator<=>(const Bounds3&) const = default;
    };
} // namespace spectra::math
