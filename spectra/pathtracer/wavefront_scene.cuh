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
#include <spectra/pathtracer/core/render_config.cuh>
#include <spectra/pathtracer/util/buffercache.cuh>
#include <spectra/pathtracer/util/containers.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <string>
#include <vector>

namespace spectra::scene {
    struct Scene;
} // namespace spectra::scene

namespace spectra {
    struct MeasuredBxDFData;
} // namespace spectra

namespace spectra::pathtracer {
    struct WavefrontSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
    };

    struct WavefrontCameraSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        CameraTransform cameraTransform{};
        std::string medium{};
    };

    struct WavefrontTransformedSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        Transform renderFromObject{};
    };

    struct WavefrontLightSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        Transform renderFromObject{};
        std::string medium{};
    };

    struct WavefrontShapeSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
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
        const RGBColorSpace* filmColorSpace{nullptr};
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
        MeshBufferCache meshBufferCache{};
        InternCache<DenselySampledSpectrum> lightSpectrumCache;
        std::map<std::string, spectra::MeasuredBxDFData*> measuredBxDFData{};
        ThreadLocal<Allocator> threadAllocators;
    };

    [[nodiscard]] std::unique_ptr<WavefrontScene> CreateWavefrontScene(const scene::Scene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_WAVEFRONT_SCENE_H
