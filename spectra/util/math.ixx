export module spectra.util.math;

import std;

export extern "C++" {
    namespace spectra::math {
        struct Point3 {
            float x{};
            float y{};
            float z{};
        };

        struct Vector3 {
            float x{};
            float y{};
            float z{};
        };

        struct Transform {
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

        [[nodiscard]] Vector3 operator-(const Point3& a, const Point3& b);
        [[nodiscard]] Vector3 Cross(const Vector3& a, const Vector3& b);
        [[nodiscard]] float Length(const Vector3& vector);
        [[nodiscard]] Vector3 Normalize(const Vector3& vector);
        [[nodiscard]] Transform Multiply(const Transform& a, const Transform& b);
        [[nodiscard]] Transform Inverse(const Transform& transform);
        [[nodiscard]] Transform Translate(const Vector3& delta);
        [[nodiscard]] Transform Scale(float x, float y, float z);
        [[nodiscard]] Transform Rotate(float degrees, Vector3 axis);
        [[nodiscard]] Transform LookAt(const Point3& position, const Point3& look, const Vector3& up);
        [[nodiscard]] Transform Compose(std::initializer_list<Transform> transforms);
    } // namespace spectra::math
}
