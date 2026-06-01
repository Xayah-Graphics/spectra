#ifndef SPECTRA_SCENE_H
#define SPECTRA_SCENE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace spectra::scene {
    enum class TextureKind { Float, Spectrum };
    enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

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

    struct SceneParameter {
        std::string type{};
        std::string name{};
        std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{};
        bool mayBeUnused{false};
    };

    struct SceneParameters {
        ColorSpace colorSpace{ColorSpace::sRGB};
        std::vector<SceneParameter> values{};
    };

    struct SceneComponent {
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneCamera {
        std::string type{"perspective"};
        SceneParameters parameters{};
        Transform worldFromCamera{};
        std::string medium{};
        float fovDegrees{};
    };

    struct SceneRenderSettings {
        SceneComponent filter{"gaussian", {}};
        SceneComponent film{"rgb", {}};
        SceneCamera camera{};
        SceneComponent sampler{"zsobol", {}};
        SceneComponent integrator{"volpath", {}};
        SceneComponent accelerator{"bvh", {}};
    };

    struct SceneMaterial {
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneTexture {
        TextureKind kind{TextureKind::Spectrum};
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromTexture{};
    };

    struct SceneMedium {
        std::string name{};
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromMedium{};
    };

    struct SceneLight {
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromLight{};
        std::string medium{};
    };

    struct SceneAreaLight {
        std::string type{};
        SceneParameters parameters{};
    };

    struct SceneShape {
        std::string type{};
        SceneParameters parameters{};
        Transform worldFromObject{};
        bool reverseOrientation{false};
        std::string material{};
        std::optional<SceneAreaLight> areaLight{};
        std::string insideMedium{};
        std::string outsideMedium{};
    };

    struct SceneObjectDefinition {
        std::string name{};
        std::vector<SceneShape> shapes{};
    };

    struct SceneObjectInstance {
        std::string name{};
        Transform worldFromInstance{};
    };

    struct Scene {
        std::string name{};
        std::string title{};
        std::string source{};
        SceneRenderSettings renderSettings{};
        std::vector<SceneMaterial> materials{};
        std::vector<SceneTexture> textures{};
        std::vector<SceneMedium> media{};
        std::vector<SceneLight> lights{};
        std::vector<SceneShape> shapes{};
        std::vector<SceneObjectDefinition> objectDefinitions{};
        std::vector<SceneObjectInstance> objectInstances{};
    };

    struct SceneInfo {
        std::string name{};
        std::string title{};
        std::string camera{};
        std::string sampler{};
        std::string integrator{};
        std::string accelerator{};
        std::size_t shape_count{};
        std::size_t material_count{};
        std::size_t texture_count{};
        std::size_t medium_count{};
        std::size_t light_count{};
        std::size_t area_light_count{};
        std::size_t infinite_light_count{};
        std::size_t object_definition_count{};
        std::size_t object_instance_count{};
        float camera_fov_degrees{};
    };

    [[nodiscard]] SceneInfo DescribeScene(const Scene& scene);
    [[nodiscard]] SceneInfo SceneInfoFor(std::string_view name);
    [[nodiscard]] Scene BuildScene(std::string_view name);
} // namespace spectra::scene

#endif // SPECTRA_SCENE_H
