#ifndef SPECTRA_PATHTRACER_COMPILED_SCENE_H
#define SPECTRA_PATHTRACER_COMPILED_SCENE_H

#include <map>
#include <memory>
#include <optional>
#include <pathtracer/base/film.cuh>
#include <pathtracer/base/filter.cuh>
#include <pathtracer/base/light.cuh>
#include <pathtracer/base/material.cuh>
#include <pathtracer/base/medium.cuh>
#include <pathtracer/base/sampler.cuh>
#include <pathtracer/base/shape.cuh>
#include <pathtracer/core/cameras.cuh>
#include <pathtracer/core/paramdict.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/util/buffercache.cuh>
#include <pathtracer/util/containers.cuh>
#include <pathtracer/util/parallel.cuh>
#include <pathtracer/util/pstd.cuh>
#include <stdexcept>
#include <string>
#include <vector>

namespace spectra {
    struct MeasuredBxDFData;
} // namespace spectra

namespace spectra::pathtracer {
    [[nodiscard]] inline pstd::pmr::memory_resource* RequireCompiledSceneMemoryResource(pstd::pmr::memory_resource* memoryResource) {
        if (memoryResource == nullptr) throw std::runtime_error("Compiled pathtracer scene requires a memory resource.");
        return memoryResource;
    }

    struct PathtracerSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
    };

    struct PathtracerCameraSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        CameraTransform cameraTransform{};
        std::string medium{};
    };

    struct PathtracerTransformedSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        Transform renderFromObject{};
    };

    struct PathtracerLightSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        Transform renderFromObject{};
        std::string medium{};
    };

    struct PathtracerShapeSceneEntity {
        std::string name{};
        FileLoc loc{};
        ParameterDictionary parameters{};
        const Transform* renderFromObject = nullptr;
        const Transform* objectFromRender = nullptr;
        bool reverseOrientation{false};
        std::string materialName{};
        std::optional<PathtracerSceneEntity> areaLight{};
        std::string insideMedium{};
        std::string outsideMedium{};
    };

    struct PathtracerInstanceDefinitionSceneEntity {
        std::string name{};
        FileLoc loc{};
        std::vector<PathtracerShapeSceneEntity> shapes{};
    };

    struct PathtracerInstanceSceneEntity {
        std::string name{};
        FileLoc loc{};
        Transform renderFromInstance{};
    };

    class CompiledPathtracerScene {
    public:
        explicit CompiledPathtracerScene(pstd::pmr::memory_resource* memoryResource) : allLights(Allocator(RequireCompiledSceneMemoryResource(memoryResource))), lightSpectrumCache(Allocator(RequireCompiledSceneMemoryResource(memoryResource))), threadAllocators([memoryResource]() { return Allocator(RequireCompiledSceneMemoryResource(memoryResource)); }) {}

        CompiledPathtracerScene(const CompiledPathtracerScene&)                = delete;
        CompiledPathtracerScene(CompiledPathtracerScene&&) noexcept            = delete;
        CompiledPathtracerScene& operator=(const CompiledPathtracerScene&)     = delete;
        CompiledPathtracerScene& operator=(CompiledPathtracerScene&&) noexcept = delete;

        PathtracerSceneEntity integrator{};
        PathtracerSceneEntity accelerator{};
        const RGBColorSpace* filmColorSpace{nullptr};
        std::vector<PathtracerShapeSceneEntity> shapes{};
        std::vector<PathtracerInstanceSceneEntity> instances{};
        std::map<std::string, PathtracerInstanceDefinitionSceneEntity> instanceDefinitions{};
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
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_COMPILED_SCENE_H
