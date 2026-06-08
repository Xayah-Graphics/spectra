export module spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export struct SceneRevision {
        std::uint64_t value{};

        friend auto operator<=>(const SceneRevision&, const SceneRevision&) = default;
    };

    export enum class SceneDirtyFlags : std::uint32_t {
        None            = 0,
        Document        = 1u << 0u,
        Timeline        = 1u << 1u,
        Frame           = 1u << 2u,
        RenderResources = 1u << 3u,
    };

    export [[nodiscard]] constexpr SceneDirtyFlags operator|(const SceneDirtyFlags lhs, const SceneDirtyFlags rhs) {
        return static_cast<SceneDirtyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    export [[nodiscard]] constexpr bool HasSceneDirtyFlag(const SceneDirtyFlags flags, const SceneDirtyFlags flag) {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0u;
    }

    export struct SceneSourceLocation {
        std::string filename{};
        int line{1};
        int column{1};
    };

    export struct SceneVector2 {
        float x{};
        float y{};
    };

    export struct SceneVector3 {
        float x{};
        float y{};
        float z{};
    };

    export struct SceneVector4 {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct SceneQuaternion {
        float x{};
        float y{};
        float z{};
        float w{1.0f};
    };

    export struct SceneTransform {
        SceneVector3 position{};
        SceneQuaternion rotation{};
        SceneVector3 scale{1.0f, 1.0f, 1.0f};
    };
} // namespace spectra::rasterizer
