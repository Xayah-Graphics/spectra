#ifndef SPECTRA_PATHTRACER_WAVEFRONT_SCENE_H
#define SPECTRA_PATHTRACER_WAVEFRONT_SCENE_H

#include <map>
#include <memory>
#include <optional>
#include <spectra/pathtracer/base/film.cuh>
#include <spectra/pathtracer/base/filter.cuh>
#include <spectra/pathtracer/base/light.cuh>
#include <spectra/pathtracer/base/material.cuh>
#include <spectra/pathtracer/base/medium.cuh>
#include <spectra/pathtracer/base/sampler.cuh>
#include <spectra/pathtracer/base/shape.cuh>
#include <spectra/pathtracer/core/cameras.cuh>
#include <spectra/pathtracer/core/paramdict.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/scene.h>
#include <string>
#include <vector>

namespace spectra::pathtracer {
    struct WavefrontSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
    };

    struct WavefrontCameraSceneEntity : WavefrontSceneEntity {
        CameraTransform cameraTransform{};
        std::string medium{};
    };

    struct WavefrontTransformedSceneEntity : WavefrontSceneEntity {
        Transform renderFromObject{};
    };

    struct WavefrontMediumSceneEntity : WavefrontTransformedSceneEntity {};

    struct WavefrontTextureSceneEntity : WavefrontTransformedSceneEntity {};

    struct WavefrontLightSceneEntity : WavefrontTransformedSceneEntity {
        std::string medium{};
    };

    struct WavefrontShapeSceneEntity : WavefrontSceneEntity {
        const Transform* renderFromObject = nullptr;
        const Transform* objectFromRender = nullptr;
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<WavefrontSceneEntity> areaLight{};
        std::string insideMedium{};
        std::string outsideMedium{};
    };

    struct WavefrontInstanceDefinitionSceneEntity {
        std::string name{};
        FileLoc loc{};
        std::vector<WavefrontShapeSceneEntity> shapes{};
    };

    struct WavefrontInstanceSceneEntity {
        std::string name{};
        FileLoc loc{};
        Transform renderFromInstance{};
    };

    class WavefrontScene {
    public:
        explicit WavefrontScene(pstd::pmr::memory_resource* memoryResource);

        WavefrontScene(const WavefrontScene&)                = delete;
        WavefrontScene(WavefrontScene&&) noexcept            = delete;
        WavefrontScene& operator=(const WavefrontScene&)     = delete;
        WavefrontScene& operator=(WavefrontScene&&) noexcept = delete;

        WavefrontSceneEntity integrator{};
        WavefrontSceneEntity accelerator{};
        const RGBColorSpace* filmColorSpace{RGBColorSpace::sRGB};
        std::vector<WavefrontShapeSceneEntity> shapes{};
        std::vector<WavefrontInstanceSceneEntity> instances{};
        std::map<std::string, WavefrontInstanceDefinitionSceneEntity> instanceDefinitions{};
        NamedTextures textures{};
        std::map<std::string, Medium> media{};
        std::map<std::string, Material> materials{};
        std::map<int, pstd::vector<Light>*> shapeIndexToAreaLights{};
        pstd::vector<Light> allLights{};
        pstd::vector<Light>* infiniteLights{};
        Filter filter{};
        Film film{};
        Sampler sampler{};
        Camera camera{};
        bool haveMedia{false};
        ThreadLocal<Allocator> threadAllocators;
    };

    [[nodiscard]] std::unique_ptr<WavefrontScene> CreateWavefrontScene(const scene::Scene& scene, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_WAVEFRONT_SCENE_H
