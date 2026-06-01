#ifndef SPECTRA_PATHTRACER_INTEGRATOR_H
#define SPECTRA_PATHTRACER_INTEGRATOR_H

#include <optional>
#include <spectra/pathtracer/base/bxdf.cuh>
#include <spectra/pathtracer/base/camera.cuh>
#include <spectra/pathtracer/base/film.cuh>
#include <spectra/pathtracer/base/filter.cuh>
#include <spectra/pathtracer/base/light.cuh>
#include <spectra/pathtracer/base/lightsampler.cuh>
#include <spectra/pathtracer/base/sampler.cuh>
#include <spectra/pathtracer/core/options.cuh>
#include <spectra/pathtracer/gpu/util.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/wavefront/workitems.cuh>
#include <spectra/pathtracer/wavefront/workqueue.cuh>

namespace spectra::scene {
    class Scene;
} // namespace spectra::scene

namespace spectra::optix {
    class SpectraOptiXAggregate;
} // namespace spectra::optix

namespace spectra::pathtracer {
    class GpuRuntime {
    public:
        explicit GpuRuntime(const SpectraOptions& options);
        ~GpuRuntime() noexcept;

        GpuRuntime(const GpuRuntime&)                = delete;
        GpuRuntime(GpuRuntime&&) noexcept            = delete;
        GpuRuntime& operator=(const GpuRuntime&)     = delete;
        GpuRuntime& operator=(GpuRuntime&&) noexcept = delete;

        void ResetOptions(const SpectraOptions& options);
        void WaitGpuNoexcept() const noexcept;

    private:
        bool initialized{false};
    };

    class WavefrontPathtracer {
    public:
        WavefrontPathtracer(pstd::pmr::memory_resource* memoryResource, const scene::Scene& scene, std::optional<Point2i> filmResolutionOverride = {});
        __host__ __device__ ~WavefrontPathtracer();

        Float Render();
        void RenderSample(Bounds2i pixelBounds, Transform cameraMotion, int sampleIndex);
        void ResetFilm(Bounds2i pixelBounds);
        void UpdateFilm();
        void UpdateFramebufferFromFilm(Bounds2i pixelBounds, Float exposure, float* rgba);

        void GenerateCameraRays(int y0, Transform movingFromCamera, int sampleIndex);
        template <typename Sampler>
        void GenerateCameraRays(int y0, Transform movingFromCamera, int sampleIndex);

        void GenerateRaySamples(int wavefrontDepth, int sampleIndex);
        template <typename Sampler>
        void GenerateRaySamples(int wavefrontDepth, int sampleIndex);

        void TraceShadowRays(int wavefrontDepth);
        void SampleMediumInteraction(int wavefrontDepth);
        template <typename PhaseFunction>
        void SampleMediumScattering(int wavefrontDepth);
        void SampleSubsurface(int wavefrontDepth);

        void HandleEscapedRays();
        void HandleEmissiveIntersection();

        void EvaluateMaterialsAndBSDFs(int wavefrontDepth, Transform movingFromCamera);
        template <typename ConcreteMaterial>
        void EvaluateMaterialAndBSDF(int wavefrontDepth, Transform movingFromCamera);
        template <typename ConcreteMaterial, typename TextureEvaluator>
        void EvaluateMaterialAndBSDF(MaterialEvalQueue* evalQueue, Transform movingFromCamera, int wavefrontDepth);

        template <typename F>
        void ParallelFor(const char* description, int nItems, F&& func);

        template <typename F>
        void Do(const char* description, F&& func);

        RayQueue* CurrentRayQueue(int wavefrontDepth) {
            return rayQueues[wavefrontDepth & 1];
        }

        RayQueue* NextRayQueue(int wavefrontDepth) {
            return rayQueues[(wavefrontDepth + 1) & 1];
        }

        void PrefetchGPUAllocations();
        Bounds3f Bounds() const;
        __host__ __device__ void ReleaseAggregate();

        bool initializeVisibleSurface;
        bool haveSubsurface;
        bool haveMedia;
        pstd::array<bool, Material::NumTags()> haveBasicEvalMaterial;
        pstd::array<bool, Material::NumTags()> haveUniversalEvalMaterial;

        pstd::pmr::memory_resource* memoryResource;

        Filter filter;
        Film film;
        Sampler sampler;
        Camera camera;
        pstd::vector<Light>* infiniteLights;
        LightSampler lightSampler;

        int maxDepth;
        int samplesPerPixel;
        bool regularize;

        int scanlinesPerPass;
        int maxQueueSize;

        SOA<PixelSampleState> pixelSampleState;

        RayQueue* rayQueues[2];

        optix::SpectraOptiXAggregate* aggregate   = nullptr;
        const WavefrontPathtracer* aggregateOwner = this;

        MediumSampleQueue* mediumSampleQueue   = nullptr;
        MediumScatterQueue* mediumScatterQueue = nullptr;

        EscapedRayQueue* escapedRayQueue = nullptr;

        HitAreaLightQueue* hitAreaLightQueue = nullptr;

        MaterialEvalQueue* basicEvalMaterialQueue     = nullptr;
        MaterialEvalQueue* universalEvalMaterialQueue = nullptr;

        ShadowRayQueue* shadowRayQueue = nullptr;

        GetBSSRDFAndProbeRayQueue* bssrdfEvalQueue     = nullptr;
        SubsurfaceScatterQueue* subsurfaceScatterQueue = nullptr;
    };
} // namespace spectra::pathtracer

#endif
