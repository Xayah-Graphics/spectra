module spectra.util.math;

import std;

extern "C++" {
namespace spectra::math {
    namespace {
        [[nodiscard]] std::array<float, 16> MultiplyMatrix(const std::array<float, 16>& a, const std::array<float, 16>& b) {
            std::array<float, 16> result{};
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    for (std::size_t index = 0; index < 4; ++index) result[row * 4 + column] += a[row * 4 + index] * b[index * 4 + column];
                }
            }
            return result;
        }

        [[nodiscard]] std::array<float, 16> Transpose(const std::array<float, 16>& matrix) {
            return {
                matrix[0],
                matrix[4],
                matrix[8],
                matrix[12],
                matrix[1],
                matrix[5],
                matrix[9],
                matrix[13],
                matrix[2],
                matrix[6],
                matrix[10],
                matrix[14],
                matrix[3],
                matrix[7],
                matrix[11],
                matrix[15],
            };
        }
    } // namespace

    Vector3 operator-(const Point3& a, const Point3& b) {
        return Vector3{a.x - b.x, a.y - b.y, a.z - b.z};
    }

    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return Vector3{
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
        };
    }

    float Length(const Vector3& vector) {
        return std::sqrt(vector.x * vector.x + vector.y * vector.y + vector.z * vector.z);
    }

    Vector3 Normalize(const Vector3& vector) {
        const float length = Length(vector);
        if (!(length > 0.0f)) throw std::runtime_error("Cannot normalize a zero-length scene vector.");
        return Vector3{vector.x / length, vector.y / length, vector.z / length};
    }

    Transform Multiply(const Transform& a, const Transform& b) {
        return Transform{
            .matrix  = MultiplyMatrix(a.matrix, b.matrix),
            .inverse = MultiplyMatrix(b.inverse, a.inverse),
        };
    }

    Transform Inverse(const Transform& transform) {
        return Transform{
            .matrix  = transform.inverse,
            .inverse = transform.matrix,
        };
    }

    Transform Translate(const Vector3& delta) {
        return Transform{
            .matrix =
                {
                    1.0f,
                    0.0f,
                    0.0f,
                    delta.x,
                    0.0f,
                    1.0f,
                    0.0f,
                    delta.y,
                    0.0f,
                    0.0f,
                    1.0f,
                    delta.z,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                },
            .inverse =
                {
                    1.0f,
                    0.0f,
                    0.0f,
                    -delta.x,
                    0.0f,
                    1.0f,
                    0.0f,
                    -delta.y,
                    0.0f,
                    0.0f,
                    1.0f,
                    -delta.z,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                },
        };
    }

    Transform Scale(float x, float y, float z) {
        return Transform{
            .matrix =
                {
                    x,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    y,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    z,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                },
            .inverse =
                {
                    1.0f / x,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f / y,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f / z,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                },
        };
    }

    Transform Rotate(float degrees, Vector3 axis) {
        constexpr float radiansPerDegree = 0.017453292519943295769f;
        axis                             = Normalize(axis);
        const float sinTheta             = std::sin(degrees * radiansPerDegree);
        const float cosTheta             = std::cos(degrees * radiansPerDegree);
        const float oneMinusCosTheta     = 1.0f - cosTheta;
        const std::array<float, 16> matrix{
            axis.x * axis.x + (1.0f - axis.x * axis.x) * cosTheta,
            axis.x * axis.y * oneMinusCosTheta - axis.z * sinTheta,
            axis.x * axis.z * oneMinusCosTheta + axis.y * sinTheta,
            0.0f,
            axis.x * axis.y * oneMinusCosTheta + axis.z * sinTheta,
            axis.y * axis.y + (1.0f - axis.y * axis.y) * cosTheta,
            axis.y * axis.z * oneMinusCosTheta - axis.x * sinTheta,
            0.0f,
            axis.x * axis.z * oneMinusCosTheta - axis.y * sinTheta,
            axis.y * axis.z * oneMinusCosTheta + axis.x * sinTheta,
            axis.z * axis.z + (1.0f - axis.z * axis.z) * cosTheta,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        return Transform{
            .matrix  = matrix,
            .inverse = Transpose(matrix),
        };
    }

    Transform LookAt(const Point3& position, const Point3& look, const Vector3& up) {
        const Vector3 direction = Normalize(look - position);
        const Vector3 right     = Normalize(Cross(Normalize(up), direction));
        const Vector3 newUp     = Cross(direction, right);
        const std::array<float, 16> worldFromCamera{
            right.x,
            newUp.x,
            direction.x,
            position.x,
            right.y,
            newUp.y,
            direction.y,
            position.y,
            right.z,
            newUp.z,
            direction.z,
            position.z,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        const std::array<float, 16> cameraFromWorld{
            right.x,
            right.y,
            right.z,
            -(right.x * position.x + right.y * position.y + right.z * position.z),
            newUp.x,
            newUp.y,
            newUp.z,
            -(newUp.x * position.x + newUp.y * position.y + newUp.z * position.z),
            direction.x,
            direction.y,
            direction.z,
            -(direction.x * position.x + direction.y * position.y + direction.z * position.z),
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        return Transform{
            .matrix  = cameraFromWorld,
            .inverse = worldFromCamera,
        };
    }

    Transform Compose(std::initializer_list<Transform> transforms) {
        Transform result{};
        for (const Transform& transform : transforms) result = Multiply(result, transform);
        return result;
    }
} // namespace spectra::math
}
