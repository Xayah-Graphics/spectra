export module spectra.rasterizer.math;

import spectra.scene.math;
import std;

namespace spectra::rasterizer::math {
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

    export [[nodiscard]] Matrix4 operator*(const Matrix4& lhs, const Matrix4& rhs) {
        Matrix4 result{};
        for (std::size_t row = 0; row < 4u; ++row) {
            for (std::size_t column = 0; column < 4u; ++column) {
                for (std::size_t index = 0; index < 4u; ++index) result(row, column) += lhs(row, index) * rhs(index, column);
            }
        }
        return result;
    }

    export [[nodiscard]] Matrix4 transform_matrix(const scene::Transform& transform) {
        const scene::Quaternion rotation = transform.rotation;
        const float x = rotation.x;
        const float y = rotation.y;
        const float z = rotation.z;
        const float w = rotation.w;

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

    export [[nodiscard]] scene::Vector3 transform_point(const Matrix4& matrix, const scene::Vector3 point) {
        return scene::Vector3{
            point.x * matrix(0u, 0u) + point.y * matrix(1u, 0u) + point.z * matrix(2u, 0u) + matrix(3u, 0u),
            point.x * matrix(0u, 1u) + point.y * matrix(1u, 1u) + point.z * matrix(2u, 1u) + matrix(3u, 1u),
            point.x * matrix(0u, 2u) + point.y * matrix(1u, 2u) + point.z * matrix(2u, 2u) + matrix(3u, 2u),
        };
    }
} // namespace spectra::rasterizer::math
