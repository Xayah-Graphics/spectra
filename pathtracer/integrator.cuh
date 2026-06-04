#ifndef SPECTRA_PATHTRACER_INTEGRATOR_H
#define SPECTRA_PATHTRACER_INTEGRATOR_H

#include <memory>
#include <optional>
#include <pathtracer/base/bxdf.cuh>
#include <pathtracer/base/camera.cuh>
#include <pathtracer/base/film.cuh>
#include <pathtracer/base/filter.cuh>
#include <pathtracer/base/light.cuh>
#include <pathtracer/base/lightsampler.cuh>
#include <pathtracer/base/sampler.cuh>
#include <pathtracer/core/kernel_config.cuh>
#include <pathtracer/core/render_config.cuh>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/parallel.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/wavefront/workitems.cuh>
#include <pathtracer/wavefront/workqueue.cuh>

namespace spectra::scene {
    struct SceneSnapshot;
} // namespace spectra::scene

namespace spectra::optix {
    class SpectraOptiXAggregate;
} // namespace spectra::optix

namespace spectra::pathtracer {
    class CompiledPathtracerScene;
    class PathtracerRuntimeResources;

    class GpuRuntime {
    public:
        explicit GpuRuntime(const RuntimeConfig& config);
        ~GpuRuntime() noexcept;

        GpuRuntime(const GpuRuntime&)                = delete;
        GpuRuntime(GpuRuntime&&) noexcept            = delete;
        GpuRuntime& operator=(const GpuRuntime&)     = delete;
        GpuRuntime& operator=(GpuRuntime&&) noexcept = delete;

        void UploadKernelConfig(const KernelConfig& config);
        void WaitGpuNoexcept() const noexcept;

    private:
        std::unique_ptr<PathtracerRuntimeResources> resources{};
        bool initialized{false};
        int cudaDevice{};
    };

    class WavefrontPathtracer {
    public:
        WavefrontPathtracer(pstd::pmr::memory_resource* memoryResource, CompiledPathtracerScene& compiledScene, const RenderConfig& config);
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
        void ParallelFor(int nItems, F&& func);

        template <typename F>
        void Do(F&& func);

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
        RenderConfig renderConfig;

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

        CompiledPathtracerScene* compiledScene    = nullptr;
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
