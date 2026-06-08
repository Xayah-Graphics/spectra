export module spectra.rasterizer.debug;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export struct SceneLineSet {
        std::string name{};
        std::vector<SceneVector3> points{};
        std::vector<std::uint32_t> indices{};
        std::vector<SceneVector4> colors{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export enum class SceneDebugPrimitiveKind {
        Point,
        Sphere,
        Box,
        Axis,
    };

    export struct SceneDebugPrimitive {
        std::string name{};
        SceneDebugPrimitiveKind kind{SceneDebugPrimitiveKind::Point};
        SceneTransform transform{};
        SceneVector4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float size{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneVectorField {
        std::string name{};
        std::vector<SceneVector3> origins{};
        std::vector<SceneVector3> vectors{};
        std::vector<SceneVector4> colors{};
        float scale{1.0f};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
