export module spectra.rasterizer.splats;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export struct SceneSplatSet {
        std::string name{};
        std::vector<SceneVector3> centers{};
        std::vector<SceneQuaternion> rotations{};
        std::vector<SceneVector3> scales{};
        std::vector<SceneVector4> colors{};
        std::vector<float> opacities{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
