// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef SPECTRA_PATHTRACER_INTEGRATOR_H
#define SPECTRA_PATHTRACER_INTEGRATOR_H

#include <src/base/bxdf.h>
#include <src/base/camera.h>
#include <src/base/film.h>
#include <src/base/filter.h>
#include <src/base/light.h>
#include <src/base/lightsampler.h>
#include <src/base/sampler.h>
#include <src/core/options.h>
#include <src/gpu/util.h>
#include <src/util/float.h>
#include <src/util/parallel.h>
#include <src/util/pstd.h>
#include <spectra/pathtracer/wavefront/workitems.h>
#include <spectra/pathtracer/wavefront/workqueue.h>

namespace spectra::scene
{
    class Scene;
} // namespace spectra::scene

namespace spectra::optix
{
    class SpectraOptiXAggregate;
} // namespace spectra::optix

namespace spectra::pathtracer
{
    class GpuRuntime
    {
    public:
        explicit GpuRuntime(const SpectraOptions& options);
        ~GpuRuntime() noexcept;

        GpuRuntime(const GpuRuntime&) = delete;
        GpuRuntime(GpuRuntime&&) noexcept = delete;
        GpuRuntime& operator=(const GpuRuntime&) = delete;
        GpuRuntime& operator=(GpuRuntime&&) noexcept = delete;

        void ResetOptions(const SpectraOptions& options);
        void WaitGpuNoexcept() const noexcept;

    private:
        bool initialized{false};
    };

    class WavefrontPathtracer
    {
    public:
        WavefrontPathtracer(pstd::pmr::memory_resource* memoryResource, scene::Scene& scene);

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

        RayQueue* CurrentRayQueue(int wavefrontDepth)
        {
            return rayQueues[wavefrontDepth & 1];
        }

        RayQueue* NextRayQueue(int wavefrontDepth)
        {
            return rayQueues[(wavefrontDepth + 1) & 1];
        }

        void PrefetchGPUAllocations();
        Bounds3f Bounds() const;
        void ReleaseAggregate();

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

        optix::SpectraOptiXAggregate* aggregate = nullptr;

        MediumSampleQueue* mediumSampleQueue = nullptr;
        MediumScatterQueue* mediumScatterQueue = nullptr;

        EscapedRayQueue* escapedRayQueue = nullptr;

        HitAreaLightQueue* hitAreaLightQueue = nullptr;

        MaterialEvalQueue* basicEvalMaterialQueue = nullptr;
        MaterialEvalQueue* universalEvalMaterialQueue = nullptr;

        ShadowRayQueue* shadowRayQueue = nullptr;

        GetBSSRDFAndProbeRayQueue* bssrdfEvalQueue = nullptr;
        SubsurfaceScatterQueue* subsurfaceScatterQueue = nullptr;
    };
} // namespace spectra::pathtracer

#endif
