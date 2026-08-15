#ifndef SPECTRA_SDK_CUDA_TYPES_H
#define SPECTRA_SDK_CUDA_TYPES_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace spectra::sdk {
    struct Half {
        std::uint16_t bits{};
    };

    struct Half4 {
        Half x{};
        Half y{};
        Half z{};
        Half w{};
    };

    struct Float2 {
        float x{};
        float y{};
    };

    struct Float3 {
        float x{};
        float y{};
        float z{};
    };

    struct Float4 {
        float x{};
        float y{};
        float z{};
        float w{};
    };

    struct UInt3 {
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t z{};
    };

    struct Transform {
        float matrix[16]{};
    };

    struct Sphere {
        Float3 position{};
        float radius{};
    };

    struct Instance {
        std::uint64_t instance_id{};
        Transform transform{};
    };

    struct Point {
        Float3 position{};
        float radius{};
        Float4 color{};
        float scalar{};
    };

    struct Line {
        Float3 first_position{};
        float width{};
        Float3 second_position{};
        Float4 color{};
        float scalar{};
    };

    struct Vector {
        Float3 origin{};
        float width{};
        Float3 vector{};
        Float4 color{};
        float scalar{};
    };

    static_assert(std::is_standard_layout_v<Half> && std::is_trivially_copyable_v<Half> && sizeof(Half) == 2u);
    static_assert(std::is_standard_layout_v<Half4> && std::is_trivially_copyable_v<Half4> && sizeof(Half4) == 8u);
    static_assert(std::is_standard_layout_v<Float2> && std::is_trivially_copyable_v<Float2> && sizeof(Float2) == 8u);
    static_assert(std::is_standard_layout_v<Float3> && std::is_trivially_copyable_v<Float3> && sizeof(Float3) == 12u);
    static_assert(std::is_standard_layout_v<Float4> && std::is_trivially_copyable_v<Float4> && sizeof(Float4) == 16u);
    static_assert(std::is_standard_layout_v<UInt3> && std::is_trivially_copyable_v<UInt3> && sizeof(UInt3) == 12u);
    static_assert(std::is_standard_layout_v<Transform> && std::is_trivially_copyable_v<Transform> && sizeof(Transform) == 64u);
    static_assert(std::is_standard_layout_v<Sphere> && std::is_trivially_copyable_v<Sphere> && sizeof(Sphere) == 16u);
    static_assert(std::is_standard_layout_v<Instance> && std::is_trivially_copyable_v<Instance> && sizeof(Instance) == 72u);
    static_assert(std::is_standard_layout_v<Point> && std::is_trivially_copyable_v<Point> && sizeof(Point) == 36u);
    static_assert(std::is_standard_layout_v<Line> && std::is_trivially_copyable_v<Line> && sizeof(Line) == 48u);
    static_assert(std::is_standard_layout_v<Vector> && std::is_trivially_copyable_v<Vector> && sizeof(Vector) == 48u);
    static_assert(offsetof(Line, width) == 12u && offsetof(Line, second_position) == 16u && offsetof(Line, color) == 28u && offsetof(Line, scalar) == 44u);
    static_assert(offsetof(Vector, width) == 12u && offsetof(Vector, vector) == 16u && offsetof(Vector, color) == 28u && offsetof(Vector, scalar) == 44u);
}

#endif
