#ifndef SPECTRA_PATHTRACER_COMPILED_SCENE_H
#define SPECTRA_PATHTRACER_COMPILED_SCENE_H

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
    struct SceneProbeReport;
    struct SceneTranslationReport;
    struct SceneSnapshot;
} // namespace spectra::scene

namespace spectra {
    struct MeasuredBxDFData;
} // namespace spectra

namespace spectra::pathtracer {
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
        explicit CompiledPathtracerScene(pstd::pmr::memory_resource* memoryResource);

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

    [[nodiscard]] spectra::scene::SceneTranslationReport AnalyzePathtracerSceneProbe(const spectra::scene::SceneProbeReport& probe);
    [[nodiscard]] spectra::scene::SceneTranslationReport AnalyzePathtracerSceneSupport(const spectra::scene::SceneSnapshot& scene);
    [[nodiscard]] std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const spectra::scene::SceneSnapshot& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_COMPILED_SCENE_H
