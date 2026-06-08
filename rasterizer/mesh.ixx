export module spectra.rasterizer.mesh;

export import spectra.rasterizer.common;

import std;

namespace spectra::rasterizer {
    export struct SceneMesh {
        std::string name{};
        std::vector<SceneVector3> positions{};
        std::vector<SceneVector3> normals{};
        std::vector<SceneVector2> texcoords{};
        std::vector<std::uint32_t> indices{};
        std::string materialName{};
        SceneTransform transform{};
        bool dynamic{false};
        SceneSourceLocation source{};
    };

    export struct SceneCloth {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        SceneTransform transform{};
        float massPerArea{1.0f};
        float stretchStiffness{1.0f};
        float bendStiffness{0.2f};
        bool dynamic{true};
        SceneSourceLocation source{};
    };

    export struct SceneRigidBody {
        std::string name{};
        std::string meshName{};
        std::string materialName{};
        SceneTransform transform{};
        SceneVector3 linearVelocity{};
        SceneVector3 angularVelocity{};
        float mass{1.0f};
        bool staticBody{false};
        SceneSourceLocation source{};
    };

    export struct SceneCollider {
        std::string name{};
        std::string meshName{};
        SceneTransform transform{};
        float friction{0.5f};
        float restitution{0.1f};
        bool dynamic{false};
        SceneSourceLocation source{};
    };
} // namespace spectra::rasterizer
