module spectra.util.math;

import std;

namespace spectra::math {
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
        if (!bounds.valid()) return;
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
        if (!this->valid()) return Bounds3::empty();
        Bounds3 result = Bounds3::empty();
        for (const float x : {this->minimum.x, this->maximum.x})
            for (const float y : {this->minimum.y, this->maximum.y})
                for (const float z : {this->minimum.z, this->maximum.z}) result.include(transform.transform_point({x, y, z}));
        return result;
    }
} // namespace spectra::math
