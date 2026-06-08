export module spectra.rasterizer.curves;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export enum class SceneCurveTopology {
        Segments,
        Polyline,
    };

    export struct SceneCurveSet {
        std::string name{};
        SceneCurveTopology topology{SceneCurveTopology::Polyline};
        std::vector<SceneVector3> points{};
        std::vector<std::uint32_t> curveOffsets{};
        std::vector<float> radii{};
        std::vector<SceneVector4> colors{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
