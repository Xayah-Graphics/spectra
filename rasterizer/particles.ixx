export module spectra.rasterizer.particles;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export struct SceneParticleSet {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> velocities{};
        std::vector<float> radii{};
        std::vector<SceneVector4> colors{};
        std::string materialName{};
        SceneTransform transform{};
        float mass{1.0f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct ScenePointCloud {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> normals{};
        std::vector<SceneVector4> colors{};
        std::vector<float> radii{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{true};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
