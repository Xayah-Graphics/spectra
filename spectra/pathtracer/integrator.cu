#include <spectra/pathtracer/integrator.h>

#include <spectra/pathtracer/base/bxdf.h>
#include <spectra/pathtracer/base/medium.h>
#include <spectra/pathtracer/core/bssrdf.h>
#include <spectra/pathtracer/core/bxdfs.h>
#include <spectra/pathtracer/core/cameras.h>
#include <spectra/pathtracer/core/film.h>
#include <spectra/pathtracer/core/filters.h>
#include <spectra/pathtracer/gpu/memory.h>
#include <spectra/pathtracer/core/interaction.h>
#include <spectra/pathtracer/core/lights.h>
#include <spectra/pathtracer/core/lightsamplers.h>
#include <spectra/pathtracer/core/materials.h>
#include <spectra/pathtracer/core/media.h>
#include <spectra/pathtracer/core/options.h>
#include <spectra/pathtracer/core/samplers.h>
#include <spectra/pathtracer/core/shapes.h>
#include <spectra/pathtracer/core/textures.h>
#include <spectra/pathtracer/util/buffercache.h>
#include <spectra/pathtracer/util/bluenoise.h>
#include <spectra/pathtracer/util/color.h>
#include <spectra/pathtracer/util/colorspace.h>
#include <spectra/pathtracer/util/containers.h>
#include <spectra/pathtracer/util/file.h>
#include <spectra/pathtracer/util/image.h>
#include <spectra/pathtracer/core/diagnostics.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/parallel.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/sampling.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/string.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>
#include <spectra/pathtracer/optix/aggregate.h>
#include <spectra/scene.h>

#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

#include <cuda.h>
#include <cuda_runtime.h>

#include <ImfThreading.h>

#ifdef SPECTRA_IS_WINDOWS
#include <Windows.h>
#endif  // SPECTRA_IS_WINDOWS

#ifdef interface
#undef interface
#endif  // interface

namespace spectra::pathtracer::detail
{
    static bool runtime_initialized = false;

    void InitializeGpuRuntimeState()
    {
        GPUInit();
        CopyOptionsToGPU();
    }

    void CopyRuntimeOptionsToGPU()
    {
        CopyOptionsToGPU();
    }

    void InitializeStaticRuntimeData()
    {
        pstd::pmr::monotonic_buffer_resource* buffer_resource = new pstd::pmr::monotonic_buffer_resource(1024 * 1024, &CUDATrackedMemoryResource::singleton);
        Allocator alloc(buffer_resource);

        ColorEncoding::Init(alloc);
        Spectra::Init(alloc);
        RGBToSpectrumTable::Init(alloc);
        RGBColorSpace::Init(alloc);
        Triangle::Init(alloc);
        BilinearPatch::Init(alloc);

        InitBufferCaches();
    }
} // namespace spectra::pathtracer::detail

namespace spectra::pathtracer
{
#ifdef SPECTRA_IS_WINDOWS
    static void report_windows_exception(const char* message)
    {
        std::fprintf(stderr, "Spectra: %s\n", message);
        std::fflush(stderr);
    }

    static LONG WINAPI handle_windows_exception(PEXCEPTION_POINTERS info)
    {
        switch (info->ExceptionRecord->ExceptionCode)
        {
        case EXCEPTION_ACCESS_VIOLATION:
            report_windows_exception("Access violation--terminating execution");
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            report_windows_exception("Array bounds violation--terminating execution");
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            report_windows_exception("Accessed misaligned data--terminating execution");
            break;
        case EXCEPTION_STACK_OVERFLOW:
            report_windows_exception("Stack overflow--terminating execution");
            break;
        default:
            std::fprintf(stderr, "Spectra: Program generated exception %d--terminating execution\n", int(info->ExceptionRecord->ExceptionCode));
            std::fflush(stderr);
        }
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif  // SPECTRA_IS_WINDOWS

    GpuRuntime::GpuRuntime(const SpectraOptions& options)
    {
        if (detail::runtime_initialized) throw std::runtime_error(spectra::diagnostics::Format("Spectra GPU runtime cannot initialize more than once per process"));
        if (Options != nullptr) throw std::runtime_error(spectra::diagnostics::Format("Spectra GPU runtime cannot initialize while runtime options are active"));

        Options = new SpectraOptions(options);

        Imf::setGlobalThreadCount(options.nThreads ? options.nThreads : AvailableCores());

#ifdef SPECTRA_IS_WINDOWS
        SetUnhandledExceptionFilter(handle_windows_exception);
        if (Options->gpuDevice && !std::getenv("CUDA_VISIBLE_DEVICES"))
        {
            std::string env = "CUDA_VISIBLE_DEVICES=" + std::to_string(*Options->gpuDevice);
            _putenv(env.c_str());
            *Options->gpuDevice = 0;
        }
#endif  // SPECTRA_IS_WINDOWS

        const int thread_count = Options->nThreads != 0 ? Options->nThreads : AvailableCores();
        ParallelInit(thread_count);

        detail::InitializeGpuRuntimeState();
        detail::InitializeStaticRuntimeData();

        detail::runtime_initialized = true;
        this->initialized = true;
    }

    GpuRuntime::~GpuRuntime() noexcept
    {
        if (!this->initialized) return;

        try
        {
            this->WaitGpuNoexcept();

            ParallelCleanup();
            delete Options;
            Options = nullptr;
        }
        catch (...)
        {
        }
    }

    void GpuRuntime::ResetOptions(const SpectraOptions& options)
    {
        if (!this->initialized) throw std::runtime_error(spectra::diagnostics::Format("Spectra GPU runtime is not initialized."));
        if (Options == nullptr) throw std::runtime_error(spectra::diagnostics::Format("Spectra global options are unavailable."));
        *Options = options;
        detail::CopyRuntimeOptionsToGPU();
    }

    void GpuRuntime::WaitGpuNoexcept() const noexcept
    {
        try
        {
            if (Options != nullptr) GPUWait();
        }
        catch (...)
        {
        }
    }

    template <typename F>
    void WavefrontPathtracer::ParallelFor(const char* description, int nItems, F&& func)
    {
        spectra::GPUParallelFor(description, nItems, func);
    }

    template <typename F>
    void WavefrontPathtracer::Do(const char* description, F&& func)
    {
        spectra::GPUParallelFor(description, 1, [=] SPECTRA_GPU(int) mutable { func(); });
    }

    Bounds3f WavefrontPathtracer::Bounds() const
    {
        SPECTRA_CHECK(aggregate != nullptr);
        return aggregate->Bounds();
    }

    void WavefrontPathtracer::ReleaseAggregate()
    {
        delete aggregate;
        aggregate = nullptr;
    }

    static void updateMaterialNeeds(
        Material m, pstd::array<bool, Material::NumTags()>* haveBasicEvalMaterial,
        pstd::array<bool, Material::NumTags()>* haveUniversalEvalMaterial,
        bool* haveSubsurface, bool* haveMedia)
    {
        *haveMedia |= (m == nullptr); // interface material
        if (!m)
            return;

        if (MixMaterial* mix = m.CastOrNullptr<MixMaterial>(); mix)
        {
            // This is a somewhat odd place for this check, but it's convenient...
            if (!m.CanEvaluateTextures(BasicTextureEvaluator()))
                throw std::runtime_error(spectra::diagnostics::Format("\"mix\" material has a texture that can't be evaluated with the "
                                "BasicTextureEvaluator, which is all that is currently supported "
                                "in the Spectra pathtracer."));

            updateMaterialNeeds(mix->GetMaterial(0), haveBasicEvalMaterial,
                                haveUniversalEvalMaterial, haveSubsurface, haveMedia);
            updateMaterialNeeds(mix->GetMaterial(1), haveBasicEvalMaterial,
                                haveUniversalEvalMaterial, haveSubsurface, haveMedia);
            return;
        }

        *haveSubsurface |= m.HasSubsurfaceScattering();

        FloatTexture displace = m.GetDisplacement();
        if (m.CanEvaluateTextures(BasicTextureEvaluator()) &&
            (!displace || BasicTextureEvaluator().CanEvaluate({displace}, {})))
            (*haveBasicEvalMaterial)[m.Tag()] = true;
        else
            (*haveUniversalEvalMaterial)[m.Tag()] = true;
    }

    WavefrontPathtracer::WavefrontPathtracer(
        pstd::pmr::memory_resource* memoryResource, scene::Scene& scene)
        : memoryResource(memoryResource)
    {
        ThreadLocal<Allocator> threadAllocators(
            [memoryResource]() { return Allocator(memoryResource); });

        Allocator alloc = threadAllocators.Get();

        // Allocate all the data structures that represent the scene...
        std::map<std::string, Medium> media = scene.CreateMedia();

        // "haveMedia" is a bit of a misnomer in that determines both whether
        // queues are allocated for the medium sampling kernels, and they are
        // launched as well as whether the ray marching shadow ray kernel is
        // launched... Thus, it will be true if there actually are no media,
        // but some "interface" materials are present in the scene.
        haveMedia = false;
        // Check the shapes and instance definitions...
        for (const auto& shape : scene.shapes)
            if (!shape.insideMedium.empty() || !shape.outsideMedium.empty())
                haveMedia = true;
        for (const auto& shape : scene.animatedShapes)
            if (!shape.insideMedium.empty() || !shape.outsideMedium.empty())
                haveMedia = true;
        for (const auto& instanceDefinition : scene.instanceDefinitions)
        {
            for (const auto& shape : instanceDefinition.second->shapes)
                if (!shape.insideMedium.empty() || !shape.outsideMedium.empty())
                    haveMedia = true;
            for (const auto& shape : instanceDefinition.second->animatedShapes)
                if (!shape.insideMedium.empty() || !shape.outsideMedium.empty())
                    haveMedia = true;
        }

        // Textures
        NamedTextures textures = scene.CreateTextures();

        pstd::vector<Light> allLights;
        std::map<int, pstd::vector<Light>*> shapeIndexToAreaLights;

        infiniteLights = alloc.new_object<pstd::vector<Light>>(alloc);

        for (Light l : scene.CreateLights(textures, &shapeIndexToAreaLights))
        {
            if (l.Is<UniformInfiniteLight>() || l.Is<ImageInfiniteLight>() ||
                l.Is<PortalImageInfiniteLight>())
                infiniteLights->push_back(l);

            allLights.push_back(l);
        }

        std::map<std::string, Material> namedMaterials;
        std::vector<Material> materials;
        scene.CreateMaterials(textures, &namedMaterials, &materials);

        haveBasicEvalMaterial.fill(false);
        haveUniversalEvalMaterial.fill(false);
        haveSubsurface = false;
        for (Material m : materials)
            updateMaterialNeeds(m, &haveBasicEvalMaterial, &haveUniversalEvalMaterial,
                                &haveSubsurface, &haveMedia);
        for (const auto& m : namedMaterials)
            updateMaterialNeeds(m.second, &haveBasicEvalMaterial, &haveUniversalEvalMaterial,
                                &haveSubsurface, &haveMedia);

        // Retrieve these here so that the CPU isn't writing to managed memory
        // concurrently with the OptiX acceleration-structure construction work
        // that follows. (Verbotten on Windows.)
        camera = scene.GetCamera();
        film = camera.GetFilm();
        filter = film.GetFilter();
        sampler = scene.GetSampler();

        CUDATrackedMemoryResource* mr =
            dynamic_cast<CUDATrackedMemoryResource*>(memoryResource);
        SPECTRA_CHECK(mr);
        aggregate = new optix::SpectraOptiXAggregate(
            scene, mr, textures, shapeIndexToAreaLights, media, namedMaterials, materials);

        // Preprocess the light sources
        for (Light light : allLights)
            light.Preprocess(aggregate->Bounds());

        bool haveLights = !allLights.empty();
        for (const auto& m : media)
            haveLights |= m.second.IsEmissive();
        if (!haveLights)
            throw std::runtime_error(spectra::diagnostics::Format("No light sources specified"));

        std::string lightSamplerName =
            scene.integrator.parameters.GetOneString("lightsampler", "bvh");
        if (allLights.size() == 1)
            lightSamplerName = "uniform";
        lightSampler = LightSampler::Create(lightSamplerName, allLights, alloc);

        if (scene.integrator.name != "path" && scene.integrator.name != "volpath")
            spectra::diagnostics::PrintWarning(&scene.integrator.loc,
                          "Ignoring specified integrator \"%s\": the Spectra pathtracer "
                          "always uses a \"volpath\" integrator.",
                          scene.integrator.name);

        // Integrator parameters
        regularize = scene.integrator.parameters.GetOneBool("regularize", false);
        maxDepth = scene.integrator.parameters.GetOneInt("maxdepth", 5);

        initializeVisibleSurface = film.UsesVisibleSurface();
        samplesPerPixel = sampler.SamplesPerPixel();

        // Allocate storage for all the queues/buffers...

        // Compute number of scanlines to render per pass
        Vector2i resolution = film.PixelBounds().Diagonal();
        int maxSamples = 1024 * 1024;
        scanlinesPerPass = std::max(1, maxSamples / resolution.x);
        int nPasses = (resolution.y + scanlinesPerPass - 1) / scanlinesPerPass;
        scanlinesPerPass = (resolution.y + nPasses - 1) / nPasses;
        maxQueueSize = resolution.x * scanlinesPerPass;

        pixelSampleState = spectra::SOA<PixelSampleState>(maxQueueSize, alloc);

        rayQueues[0] = alloc.new_object<RayQueue>(maxQueueSize, alloc);
        rayQueues[1] = alloc.new_object<RayQueue>(maxQueueSize, alloc);

        shadowRayQueue = alloc.new_object<ShadowRayQueue>(maxQueueSize, alloc);

        if (haveSubsurface)
        {
            bssrdfEvalQueue =
                alloc.new_object<GetBSSRDFAndProbeRayQueue>(maxQueueSize, alloc);
            subsurfaceScatterQueue =
                alloc.new_object<SubsurfaceScatterQueue>(maxQueueSize, alloc);
        }

        if (infiniteLights->size())
            escapedRayQueue = alloc.new_object<EscapedRayQueue>(maxQueueSize, alloc);
        hitAreaLightQueue = alloc.new_object<HitAreaLightQueue>(maxQueueSize, alloc);

        basicEvalMaterialQueue = alloc.new_object<MaterialEvalQueue>(
            maxQueueSize, alloc,
            pstd::MakeConstSpan(&haveBasicEvalMaterial[1], haveBasicEvalMaterial.size() - 1));
        universalEvalMaterialQueue = alloc.new_object<MaterialEvalQueue>(
            maxQueueSize, alloc,
            pstd::MakeConstSpan(&haveUniversalEvalMaterial[1],
                                haveUniversalEvalMaterial.size() - 1));

        if (haveMedia)
        {
            mediumSampleQueue = alloc.new_object<MediumSampleQueue>(maxQueueSize, alloc);

            pstd::array<bool, PhaseFunction::NumTags()> havePhase;
            havePhase.fill(true);
            mediumScatterQueue =
                alloc.new_object<MediumScatterQueue>(maxQueueSize, alloc, havePhase);
        }
    }

    Float WavefrontPathtracer::Render()
    {
        Bounds2i pixelBounds = film.PixelBounds();

        std::chrono::steady_clock::time_point renderStart = std::chrono::steady_clock::now();
        // Prefetch allocations to GPU memory
        PrefetchGPUAllocations();

        // Loop over sample indices and evaluate pixel samples
        int firstSampleIndex = 0, lastSampleIndex = samplesPerPixel;
        int totalSamples = lastSampleIndex - firstSampleIndex;
        if (!Options->quiet)
        {
            Vector2i resolution = pixelBounds.Diagonal();
            std::fprintf(stdout, "Rendering %d samples at %dx%d\n", totalSamples,
                         resolution.x, resolution.y);
            std::fflush(stdout);
        }

        std::chrono::steady_clock::time_point lastProgressReport =
            std::chrono::steady_clock::now();
        for (int sampleIndex = firstSampleIndex; sampleIndex < lastSampleIndex; ++sampleIndex)
        {
            RenderSample(pixelBounds, Transform{}, sampleIndex);

            if (!Options->quiet)
            {
                std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
                bool lastSample = sampleIndex + 1 == lastSampleIndex;
                if (lastSample || now - lastProgressReport >= std::chrono::seconds(1))
                {
                    int completedSamples = sampleIndex - firstSampleIndex + 1;
                    double elapsedSeconds =
                        std::chrono::duration<double>(now - renderStart).count();
                    std::fprintf(stdout, "Rendering sample %d/%d, elapsed %.1fs\n",
                                 completedSamples, totalSamples, elapsedSeconds);
                    std::fflush(stdout);
                    lastProgressReport = now;
                }
            }
        }

        GPUWait();
        Float seconds = Float(std::chrono::duration<double>(
                                  std::chrono::steady_clock::now() - renderStart)
                                  .count());
        if (!Options->quiet)
        {
            std::fprintf(stdout, "Rendering completed in %.1fs\n", seconds);
            std::fflush(stdout);
        }

        return seconds;
    }

    void WavefrontPathtracer::RenderSample(Bounds2i pixelBounds,
                                         Transform cameraMotion,
                                         int sampleIndex)
    {
        for (int y0 = pixelBounds.pMin.y; y0 < pixelBounds.pMax.y;
             y0 += scanlinesPerPass)
        {
            RayQueue* cameraRayQueue = CurrentRayQueue(0);
            Do(
                "Reset ray queue", SPECTRA_CPU_GPU_LAMBDA()
                {
                    cameraRayQueue->Reset();
                });

            GenerateCameraRays(y0, cameraMotion, sampleIndex);

            for (int wavefrontDepth = 0; true; ++wavefrontDepth)
            {
                RayQueue* nextQueue = NextRayQueue(wavefrontDepth);
                Do(
                    "Reset queues before tracing rays", SPECTRA_CPU_GPU_LAMBDA()
                    {
                        nextQueue->Reset();
                        if (mediumSampleQueue)
                            mediumSampleQueue->Reset();
                        if (mediumScatterQueue)
                            mediumScatterQueue->Reset();

                        if (escapedRayQueue)
                            escapedRayQueue->Reset();
                        hitAreaLightQueue->Reset();

                        basicEvalMaterialQueue->Reset();
                        universalEvalMaterialQueue->Reset();

                        if (bssrdfEvalQueue)
                            bssrdfEvalQueue->Reset();
                        if (subsurfaceScatterQueue)
                            subsurfaceScatterQueue->Reset();
                    });

                GenerateRaySamples(wavefrontDepth, sampleIndex);

                aggregate->IntersectClosest(
                    maxQueueSize, CurrentRayQueue(wavefrontDepth), escapedRayQueue,
                    hitAreaLightQueue, basicEvalMaterialQueue, universalEvalMaterialQueue,
                    mediumSampleQueue, NextRayQueue(wavefrontDepth));

                SampleMediumInteraction(wavefrontDepth);

                HandleEscapedRays();

                HandleEmissiveIntersection();

                if (wavefrontDepth == maxDepth)
                    break;

                EvaluateMaterialsAndBSDFs(wavefrontDepth, cameraMotion);

                TraceShadowRays(wavefrontDepth);

                SampleSubsurface(wavefrontDepth);
            }

            UpdateFilm();
        }
    }

    void WavefrontPathtracer::ResetFilm(Bounds2i pixelBounds)
    {
        Vector2i resolution = pixelBounds.Diagonal();
        ParallelFor(
            "Reset pixels", resolution.x * resolution.y,
            SPECTRA_CPU_GPU_LAMBDA(int i)
            {
                int x = i % resolution.x, y = i / resolution.x;
                film.ResetPixel(pixelBounds.pMin + Vector2i(x, y));
            });
    }

    void WavefrontPathtracer::HandleEscapedRays()
    {
        if (!escapedRayQueue)
            return;
        ForAllQueued(
            "Handle escaped rays", escapedRayQueue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(const spectra::EscapedRayWorkItem w)
            {
                // Compute weighted radiance for escaped ray
                SampledSpectrum L(0.f);
                for (const auto& light : *infiniteLights)
                {
                    if (SampledSpectrum Le = light.Le(Ray(w.rayo, w.rayd), w.lambda); Le)
                    {
                        // Compute path radiance contribution from infinite light

                        if (w.depth == 0 || w.specularBounce)
                        {
                            L += w.beta * Le / w.r_u.Average();
                        }
                        else
                        {
                            // Compute MIS-weighted radiance contribution from infinite light
                            LightSampleContext ctx = w.prevIntrCtx;
                            Float lightChoicePDF = lightSampler.PMF(ctx, light);
                            SampledSpectrum r_l =
                                w.r_l * lightChoicePDF * light.PDF_Li(ctx, w.rayd, true);
                            L += w.beta * Le / (w.r_u + r_l).Average();
                        }
                    }
                }

                // Update pixel radiance if ray's radiance is nonzero
                if (L)
                {

                    L += pixelSampleState.L[w.pixelIndex];
                    pixelSampleState.L[w.pixelIndex] = L;
                }
            });
    }

    void WavefrontPathtracer::HandleEmissiveIntersection()
    {
        ForAllQueued(
            "Handle emitters hit by indirect rays", hitAreaLightQueue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(const spectra::HitAreaLightWorkItem w)
            {
                // Find emitted radiance from surface that ray hit
                SampledSpectrum Le = w.areaLight.L(w.p, w.n, w.uv, w.wo, w.lambda);
                if (!Le)
                    return;

                // Compute area light's weighted radiance contribution to the path
                SampledSpectrum L(0.f);
                if (w.depth == 0 || w.specularBounce)
                {
                    L = w.beta * Le / w.r_u.Average();
                }
                else
                {
                    // Compute MIS-weighted radiance contribution from area light
                    Vector3f wi = -w.wo;
                    LightSampleContext ctx = w.prevIntrCtx;
                    Float lightChoicePDF = lightSampler.PMF(ctx, w.areaLight);
                    Float lightPDF = lightChoicePDF * w.areaLight.PDF_Li(ctx, wi, true);

                    SampledSpectrum r_u = w.r_u;
                    SampledSpectrum r_l = w.r_l * lightPDF;
                    L = w.beta * Le / (r_u + r_l).Average();
                }


                // Update _L_ in _PixelSampleState_ for area light's radiance
                L += pixelSampleState.L[w.pixelIndex];
                pixelSampleState.L[w.pixelIndex] = L;
            });
    }

    void WavefrontPathtracer::TraceShadowRays(int wavefrontDepth)
    {
        if (haveMedia)
            aggregate->IntersectShadowTr(maxQueueSize, shadowRayQueue, &pixelSampleState);
        else
            aggregate->IntersectShadow(maxQueueSize, shadowRayQueue, &pixelSampleState);
        // Reset shadow ray queue
        Do(
            "Reset shadowRayQueue", SPECTRA_CPU_GPU_LAMBDA()
            {
                shadowRayQueue->Reset();
            });
    }

    void WavefrontPathtracer::PrefetchGPUAllocations()
    {
        int deviceIndex;
        SPECTRA_CUDA_CHECK(cudaGetDevice(&deviceIndex));
        int hasConcurrentManagedAccess;
        SPECTRA_CUDA_CHECK(cudaDeviceGetAttribute(&hasConcurrentManagedAccess,
            cudaDevAttrConcurrentManagedAccess, deviceIndex));

        // Copy all the scene data structures over to GPU memory.  This
        // ensures that there isn't a big performance hitch for the first batch
        // of rays as that stuff is copied over on demand.
        if (hasConcurrentManagedAccess)
        {
            // Set things up so that we can still have read from the
            // WavefrontPathtracer struct on the CPU without hurting
            // performance. (This makes it possible to use the values of things
            // like WavefrontPathtracer::haveSubsurface to conditionally launch
            // kernels according to what's in the scene...)
            cudaMemLocation location = {};
            location.type = cudaMemLocationTypeDevice;
            location.id = 0; // For ReadMostly: device ID is ignored

            SPECTRA_CUDA_CHECK(cudaMemAdvise(this, sizeof(*this), cudaMemAdviseSetReadMostly,
                location));
            location.id = deviceIndex;
            SPECTRA_CUDA_CHECK(cudaMemAdvise(this, sizeof(*this), cudaMemAdviseSetPreferredLocation,
                location));

            // Copy all the scene data structures over to GPU memory.  This
            // ensures that there isn't a big performance hitch for the first batch
            // of rays as that stuff is copied over on demand.
            CUDATrackedMemoryResource* mr =
                dynamic_cast<CUDATrackedMemoryResource*>(memoryResource);
            SPECTRA_CHECK(mr);
            mr->PrefetchToGPU();
        }
    }

    void WavefrontPathtracer::GenerateCameraRays(int y0, Transform movingFromCamera,
                                               int sampleIndex)
    {
        // Define _generateRays_ lambda function
        auto generateRays = [=, this](auto sampler)
        {
            if constexpr (!std::is_same_v<std::remove_reference_t<decltype(*sampler)>, MLTSampler> &&
                !std::is_same_v<std::remove_reference_t<decltype(*sampler)>, DebugMLTSampler>)
                GenerateCameraRays<std::remove_reference_t<decltype(*sampler)>>(
                    y0, movingFromCamera, sampleIndex);
        };

        sampler.DispatchCPU(generateRays);
    }

    template <typename ConcreteSampler>
    void WavefrontPathtracer::GenerateCameraRays(int y0, Transform movingFromCamera,
                                               int sampleIndex)
    {
        RayQueue* rayQueue = CurrentRayQueue(0);
        ParallelFor(
            "Generate camera rays", maxQueueSize, SPECTRA_CPU_GPU_LAMBDA(int pixelIndex)
            {
                // Enqueue camera ray and set pixel state for sample
                // Compute pixel coordinates for _pixelIndex_
                Bounds2i pixelBounds = film.PixelBounds();
                int xResolution = pixelBounds.pMax.x - pixelBounds.pMin.x;
                Point2i pPixel(pixelBounds.pMin.x + pixelIndex % xResolution,
                                     y0 + pixelIndex / xResolution);
                pixelSampleState.pPixel[pixelIndex] = pPixel;

                // Test pixel coordinates against pixel bounds
                if (!InsideExclusive(pPixel, pixelBounds))
                    return;

                // Initialize _Sampler_ for current pixel and sample
                ConcreteSampler pixelSampler = *sampler.Cast<ConcreteSampler>();
                pixelSampler.StartPixelSample(pPixel, sampleIndex, 0);

                // Sample wavelengths for ray path
                Float lu = pixelSampler.Get1D();
                if (GetOptions().disableWavelengthJitter)
                    lu = 0.5f;
                SampledWavelengths lambda = film.SampleWavelengths(lu);

                // Generate _CameraSample_ and corresponding ray
                CameraSample cameraSample = GetCameraSample(pixelSampler, pPixel, filter);
                pstd::optional<CameraRay> cameraRay =
                    camera.GenerateRay(cameraSample, lambda);
                if (cameraRay)
                    cameraRay->ray = movingFromCamera(cameraRay->ray);

                // Initialize remainder of _PixelSampleState_ for ray
                pixelSampleState.L[pixelIndex] = SampledSpectrum(0.f);
                pixelSampleState.lambda[pixelIndex] = lambda;
                pixelSampleState.filterWeight[pixelIndex] = cameraSample.filterWeight;
                if (initializeVisibleSurface)
                    pixelSampleState.visibleSurface[pixelIndex] = VisibleSurface();

                // Enqueue camera ray for intersection tests
                if (cameraRay)
                {
                    rayQueue->PushCameraRay(cameraRay->ray, lambda, pixelIndex);
                    pixelSampleState.cameraRayWeight[pixelIndex] = cameraRay->weight;
                }
                else
                    pixelSampleState.cameraRayWeight[pixelIndex] = SampledSpectrum(0);
            });
    }

    void WavefrontPathtracer::GenerateRaySamples(int wavefrontDepth, int sampleIndex)
    {
        auto generateSamples = [=, this](auto sampler)
        {
            if constexpr (!std::is_same_v<std::remove_reference_t<decltype(*sampler)>, MLTSampler> &&
                !std::is_same_v<std::remove_reference_t<decltype(*sampler)>, DebugMLTSampler>)
                GenerateRaySamples<std::remove_reference_t<decltype(*sampler)>>(
                    wavefrontDepth, sampleIndex);
        };
        sampler.DispatchCPU(generateSamples);
    }

    template <typename ConcreteSampler>
    void WavefrontPathtracer::GenerateRaySamples(int wavefrontDepth, int sampleIndex)
    {
        // Generate description string _desc_ for ray sample generation
        std::string desc = std::string("Generate ray samples - ") + ConcreteSampler::Name();

        RayQueue* rayQueue = CurrentRayQueue(wavefrontDepth);
        spectra::ForAllQueued(
            desc.c_str(), rayQueue, maxQueueSize, SPECTRA_CPU_GPU_LAMBDA(const spectra::RayWorkItem w)
            {
                // Generate samples for ray segment at current sample index
                // Find first sample dimension
                int dimension = 6 + 7 * w.depth;
                if (haveSubsurface)
                    dimension += 3 * w.depth;

                // Initialize _Sampler_ for pixel, sample index, and dimension
                ConcreteSampler pixelSampler = *sampler.Cast<ConcreteSampler>();
                Point2i pPixel = pixelSampleState.pPixel[w.pixelIndex];
                pixelSampler.StartPixelSample(pPixel, sampleIndex, dimension);

                // Initialize _RaySamples_ structure with sample values
                RaySamples rs;
                rs.direct.uc = pixelSampler.Get1D();
                rs.direct.u = pixelSampler.Get2D();
                // Initialize remaining samples in _rs_
                rs.indirect.uc = pixelSampler.Get1D();
                rs.indirect.u = pixelSampler.Get2D();
                rs.indirect.rr = pixelSampler.Get1D();
                // Possibly initialize subsurface samples in _rs_
                rs.haveSubsurface = haveSubsurface;
                if (haveSubsurface)
                {
                    rs.subsurface.uc = pixelSampler.Get1D();
                    rs.subsurface.u = pixelSampler.Get2D();
                }

                // Store _RaySamples_ in pixel sample state
                pixelSampleState.samples[w.pixelIndex] = rs;
            });
    }

    struct EvaluateMaterialCallback
    {
        int wavefrontDepth;
        WavefrontPathtracer* pathtracer;
        Transform movingFromCamera;

        template <typename ConcreteMaterial>
        void operator()()
        {
            if constexpr (!std::is_same_v<ConcreteMaterial, MixMaterial>)
                pathtracer->EvaluateMaterialAndBSDF<ConcreteMaterial>(wavefrontDepth,
                                                                      movingFromCamera);
        }
    };

    void WavefrontPathtracer::EvaluateMaterialsAndBSDFs(int wavefrontDepth,
                                                      Transform movingFromCamera)
    {
        ForEachType(EvaluateMaterialCallback{wavefrontDepth, this, movingFromCamera},
                          Material::Types());
    }

    template <typename ConcreteMaterial>
    void WavefrontPathtracer::EvaluateMaterialAndBSDF(int wavefrontDepth,
                                                    Transform movingFromCamera)
    {
        int index = Material::TypeIndex<ConcreteMaterial>();
        if (haveBasicEvalMaterial[index])
            EvaluateMaterialAndBSDF<ConcreteMaterial, BasicTextureEvaluator>(
                basicEvalMaterialQueue, movingFromCamera, wavefrontDepth);
        if (haveUniversalEvalMaterial[index])
            EvaluateMaterialAndBSDF<ConcreteMaterial, UniversalTextureEvaluator>(
                universalEvalMaterialQueue, movingFromCamera, wavefrontDepth);
    }

    template <typename ConcreteMaterial, typename TextureEvaluator>
    void WavefrontPathtracer::EvaluateMaterialAndBSDF(MaterialEvalQueue* evalQueue,
                                                    Transform movingFromCamera,
                                                    int wavefrontDepth)
    {
        // Get BSDF for items in _evalQueue_ and sample illumination
        // Construct _desc_ for material/texture evaluation kernel
        std::string desc = ConcreteMaterial::Name();
        desc += " + BxDF eval (";
        desc += std::is_same_v<TextureEvaluator, BasicTextureEvaluator> ? "Basic" : "Universal";
        desc += " tex)";

        RayQueue* nextRayQueue = NextRayQueue(wavefrontDepth);
        auto queue = evalQueue->Get<MaterialEvalWorkItem<ConcreteMaterial>>();
        spectra::ForAllQueued(
            desc.c_str(), queue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(const spectra::MaterialEvalWorkItem<ConcreteMaterial> w)
            {
                // Evaluate material and BSDF for ray intersection
                TextureEvaluator texEval;
                // Compute differentials for position and $(u,v)$ at intersection point
                Vector3f dpdx, dpdy;
                Float dudx = 0, dudy = 0, dvdx = 0, dvdy = 0;
                if (!GetOptions().disableTextureFiltering)
                {
                    Point3f pc = movingFromCamera.ApplyInverse(Point3f(w.pi));
                    Normal3f nc = movingFromCamera.ApplyInverse(w.n);
                    camera.Approximate_dp_dxy(pc, nc, w.time, samplesPerPixel, &dpdx, &dpdy);
                    Vector3f dpdu = w.dpdu, dpdv = w.dpdv;
                    // Estimate screen-space change in $(u,v)$
                    // Compute $\transpose{\XFORM{A}} \XFORM{A}$ and its determinant
                    Float ata00 = Dot(dpdu, dpdu), ata01 = Dot(dpdu, dpdv);
                    Float ata11 = Dot(dpdv, dpdv);
                    Float invDet = 1 / DifferenceOfProducts(ata00, ata11, ata01, ata01);
                    invDet = IsFinite(invDet) ? invDet : 0.f;

                    // Compute $\transpose{\XFORM{A}} \VEC{b}$ for $x$ and $y$
                    Float atb0x = Dot(dpdu, dpdx), atb1x = Dot(dpdv, dpdx);
                    Float atb0y = Dot(dpdu, dpdy), atb1y = Dot(dpdv, dpdy);

                    // Compute $u$ and $v$ derivatives with respect to $x$ and $y$
                    dudx = DifferenceOfProducts(ata11, atb0x, ata01, atb1x) * invDet;
                    dvdx = DifferenceOfProducts(ata00, atb1x, ata01, atb0x) * invDet;
                    dudy = DifferenceOfProducts(ata11, atb0y, ata01, atb1y) * invDet;
                    dvdy = DifferenceOfProducts(ata00, atb1y, ata01, atb0y) * invDet;

                    // Clamp derivatives of $u$ and $v$ to reasonable values
                    dudx = IsFinite(dudx) ? Clamp(dudx, -1e8f, 1e8f) : 0.f;
                    dvdx = IsFinite(dvdx) ? Clamp(dvdx, -1e8f, 1e8f) : 0.f;
                    dudy = IsFinite(dudy) ? Clamp(dudy, -1e8f, 1e8f) : 0.f;
                    dvdy = IsFinite(dvdy) ? Clamp(dvdy, -1e8f, 1e8f) : 0.f;
                }

                // Compute shading normal if bump or normal mapping is being used
                Normal3f ns = w.ns;
                Vector3f dpdus = w.dpdus;
                FloatTexture displacement = w.material->GetDisplacement();
                const Image* normalMap = w.material->GetNormalMap();
                if (normalMap)
                {
                    // Call _NormalMap()_ to find shading geometry
                    NormalBumpEvalContext bctx =
                        w.GetNormalBumpEvalContext(dudx, dudy, dvdx, dvdy);
                    Vector3f dpdvs;
                    NormalMap(*normalMap, bctx, &dpdus, &dpdvs);
                    ns = Normal3f(Normalize(Cross(dpdus, dpdvs)));
                    ns = spectra::FaceForward(ns, w.n);
                }
                else if (displacement)
                {
                    // Call _BumpMap()_ to find shading geometry
                    if (displacement)
                        SPECTRA_DCHECK(texEval.CanEvaluate({displacement}, {}));
                    NormalBumpEvalContext bctx =
                        w.GetNormalBumpEvalContext(dudx, dudy, dvdx, dvdy);
                    Vector3f dpdvs;
                    spectra::BumpMap(texEval, displacement, bctx, &dpdus, &dpdvs);
                    ns = Normal3f(Normalize(Cross(dpdus, dpdvs)));
                    ns = spectra::FaceForward(ns, w.n);
                }

                // Get BSDF at intersection point
                SampledWavelengths lambda = w.lambda;
                MaterialEvalContext ctx =
                    w.GetMaterialEvalContext(dudx, dudy, dvdx, dvdy, ns, dpdus);
                typename ConcreteMaterial::BxDF bxdf =
                    w.material->GetBxDF(texEval, ctx, lambda);
                BSDF bsdf(ctx.ns, ctx.dpdus, &bxdf);
                // Handle terminated secondary wavelengths after BSDF creation
                if (lambda.SecondaryTerminated())
                    pixelSampleState.lambda[w.pixelIndex] = lambda;

                // Regularize BSDF, if appropriate
                if (regularize && w.anyNonSpecularBounces)
                    bsdf.Regularize();

                // Initialize _VisibleSurface_ at first intersection if necessary
                if (w.depth == 0 && initializeVisibleSurface)
                {
                    SurfaceInteraction isect;
                    isect.pi = w.pi;
                    isect.n = w.n;
                    isect.shading.n = ns;
                    isect.uv = w.uv;
                    isect.wo = w.wo;
                    isect.time = w.time;
                    isect.dpdx = dpdx;
                    isect.dpdy = dpdy;

                    // Estimate BSDF's albedo
                    // Define sample arrays _ucRho_ and _uRho_ for reflectance estimate
                    constexpr int nRhoSamples = 16;
                    const Float ucRho[nRhoSamples] = {
                        0.75741637, 0.37870818, 0.7083487, 0.18935409, 0.9149363, 0.35417435,
                        0.5990858, 0.09467703, 0.8578725, 0.45746812, 0.686759, 0.17708716,
                        0.9674518, 0.2995429, 0.5083201, 0.047338516
                    };
                    const Point2f uRho[nRhoSamples] = {
                        Point2f(0.855985, 0.570367), Point2f(0.381823, 0.851844),
                        Point2f(0.285328, 0.764262), Point2f(0.733380, 0.114073),
                        Point2f(0.542663, 0.344465), Point2f(0.127274, 0.414848),
                        Point2f(0.964700, 0.947162), Point2f(0.594089, 0.643463),
                        Point2f(0.095109, 0.170369), Point2f(0.825444, 0.263359),
                        Point2f(0.429467, 0.454469), Point2f(0.244460, 0.816459),
                        Point2f(0.756135, 0.731258), Point2f(0.516165, 0.152852),
                        Point2f(0.180888, 0.214174), Point2f(0.898579, 0.503897)
                    };

                    SampledSpectrum albedo = bsdf.rho(isect.wo, ucRho, uRho);

                    pixelSampleState.visibleSurface[w.pixelIndex] =
                        VisibleSurface(isect, albedo, lambda);
                }

                // Sample BSDF and enqueue indirect ray at intersection point
                Vector3f wo = w.wo;
                RaySamples raySamples = pixelSampleState.samples[w.pixelIndex];
                pstd::optional<BSDFSample> bsdfSample =
                    bsdf.Sample_f<typename ConcreteMaterial::BxDF>(
                        wo, raySamples.indirect.uc, raySamples.indirect.u);
                if (bsdfSample)
                {
                    // Compute updated path throughput and PDFs and enqueue indirect ray
                    Vector3f wi = bsdfSample->wi;
                    SampledSpectrum beta =
                        w.beta * bsdfSample->f * AbsDot(wi, ns) / bsdfSample->pdf;
                    SampledSpectrum r_u = w.r_u, r_l;


                    // Update _r_u_ based on BSDF sample PDF
                    if (bsdfSample->pdfIsProportional)
                        r_l =
                            r_u / bsdf.PDF<typename ConcreteMaterial::BxDF>(
                                wo, bsdfSample->wi);
                    else
                        r_l = r_u / bsdfSample->pdf;

                    // Update _etaScale_ accounting for BSDF scattering
                    Float etaScale = w.etaScale;
                    if (bsdfSample->IsTransmission())
                        etaScale *= Sqr(bsdfSample->eta);

                    // Apply Russian roulette to indirect ray based on weighted path
                    // throughput
                    SampledSpectrum rrBeta = beta * etaScale / r_u.Average();
                    // Note: depth >= 1 here to match VolPathIntegrator (which increments
                    // depth earlier).
                    if (rrBeta.MaxComponentValue() < 1 && w.depth >= 1)
                    {
                        Float q = std::max<Float>(0, 1 - rrBeta.MaxComponentValue());
                        if (raySamples.indirect.rr < q)
                        {
                            beta = SampledSpectrum(0.f);
                        }
                        else
                            beta /= 1 - q;
                    }

                    if (beta)
                    {
                        // Initialize spawned ray and enqueue for next ray depth
                        if (bsdfSample->IsTransmission() &&
                            w.material->HasSubsurfaceScattering())
                        {
                            bssrdfEvalQueue->Push(w.material, lambda, beta, r_u,
                                                  Point3f(w.pi), wo, w.n, ns, dpdus, w.uv,
                                                  w.depth, w.mediumInterface, etaScale,
                                                  w.pixelIndex);
                        }
                        else
                        {
                            Ray ray = SpawnRay(w.pi, w.n, w.time, wi);
                            // Initialize _ray_ medium if media are present
                            if (haveMedia)
                                ray.medium = spectra::Dot(ray.d, w.n) > 0
                                                 ? w.mediumInterface.outside
                                                 : w.mediumInterface.inside;

                            bool anyNonSpecularBounces =
                                !bsdfSample->IsSpecular() || w.anyNonSpecularBounces;
                            LightSampleContext ctx(w.pi, w.n, ns);
                            nextRayQueue->PushIndirectRay(
                                ray, w.depth + 1, ctx, beta, r_u, r_l, lambda,
                                etaScale, bsdfSample->IsSpecular(), anyNonSpecularBounces,
                                w.pixelIndex);

                        }
                    }
                }

                // Sample light and enqueue shadow ray at intersection point
                BxDFFlags flags = bsdf.Flags();
                if (IsNonSpecular(flags))
                {
                    // Choose a light source with the _LightSampler_
                    LightSampleContext ctx(w.pi, w.n, ns);
                    if (IsReflective(flags) && !IsTransmissive(flags))
                        ctx.pi = spectra::OffsetRayOrigin(ctx.pi, w.n, wo);
                    else if (IsTransmissive(flags) && IsReflective(flags))
                        ctx.pi = spectra::OffsetRayOrigin(ctx.pi, w.n, -wo);
                    pstd::optional<SampledLight> sampledLight =
                        lightSampler.Sample(ctx, raySamples.direct.uc);
                    if (!sampledLight)
                        return;
                    Light light = sampledLight->light;

                    // Sample light source and evaluate BSDF for direct lighting
                    pstd::optional<LightLiSample> ls =
                        light.SampleLi(ctx, raySamples.direct.u, lambda, true);
                    if (!ls || !ls->L || ls->pdf == 0)
                        return;
                    Vector3f wi = ls->wi;
                    SampledSpectrum f =
                        bsdf.f<typename ConcreteMaterial::BxDF>(wo, wi);
                    if (!f)
                        return;

                    // Compute path throughput and path PDFs for light sample
                    SampledSpectrum beta = w.beta * f * AbsDot(wi, ns);


                    Float lightPDF = ls->pdf * sampledLight->p;
                    // This causes r_u to be zero for the shadow ray, so that
                    // part of MIS just becomes a no-op.
                    Float bsdfPDF = IsDeltaLight(light.Type())
                                              ? 0.f
                                              : bsdf.PDF<typename ConcreteMaterial::BxDF>(wo, wi);
                    SampledSpectrum r_u = w.r_u * bsdfPDF;
                    SampledSpectrum r_l = w.r_u * lightPDF;

                    // Enqueue shadow ray with tentative radiance contribution
                    SampledSpectrum Ld = beta * ls->L;
                    Ray ray = SpawnRayTo(w.pi, w.n, w.time, ls->pLight.pi, ls->pLight.n);
                    // Initialize _ray_ medium if media are present
                    if (haveMedia)
                        ray.medium = spectra::Dot(ray.d, w.n) > 0
                                         ? w.mediumInterface.outside
                                         : w.mediumInterface.inside;

                    shadowRayQueue->Push(ShadowRayWorkItem{
                        ray, 1 - Float(0.0001f), lambda, Ld,
                        r_u, r_l, w.pixelIndex
                    });

                }
            });
    }

    struct SampleMediumScatteringCallback
    {
        int wavefrontDepth;
        WavefrontPathtracer* pathtracer;

        template <typename PhaseFunction>
        void operator()()
        {
            pathtracer->SampleMediumScattering<PhaseFunction>(wavefrontDepth);
        }
    };

    void WavefrontPathtracer::SampleMediumInteraction(int wavefrontDepth)
    {
        if (!haveMedia)
            return;

        RayQueue* nextRayQueue = NextRayQueue(wavefrontDepth);
        ForAllQueued(
            "Sample medium interaction", mediumSampleQueue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(spectra::MediumSampleWorkItem w)
            {
                Ray ray = w.ray;
                Float tMax = w.tMax;


                SampledWavelengths lambda = w.lambda;
                SampledSpectrum beta = w.beta;
                SampledSpectrum r_u = w.r_u;
                SampledSpectrum r_l = w.r_l;
                SampledSpectrum L(0.f);
                RNG rng(Hash(ray.o, tMax), Hash(ray.d));


                // Sample the medium according to T_maj, the homogeneous
                // transmission function based on the majorant.
                bool scattered = false;

                RaySamples raySamples = pixelSampleState.samples[w.pixelIndex];
                Float uDist = rng.Uniform<Float>();
                Float uMode = rng.Uniform<Float>();

                SampledSpectrum T_maj = SampleT_maj(
                    ray, tMax, uDist, rng, lambda,
                    [&](Point3f p, MediumProperties mp, SampledSpectrum sigma_maj,
                        SampledSpectrum T_maj)
                    {

                        // Add emission, if present.  Always do this and scale
                        // by sigma_a/sigma_maj rather than only doing it
                        // (without scaling) at absorption events.
                        if (w.depth < maxDepth && mp.Le)
                        {
                            Float pr = sigma_maj[0] * T_maj[0];
                            SampledSpectrum r_e = r_u * sigma_maj * T_maj / pr;

                            // Update _L_ for medium emission
                            if (r_e)
                                L += beta * mp.sigma_a * T_maj * mp.Le /
                                    (pr * r_e.Average());
                        }

                        // Compute probabilities for each type of scattering.
                        Float pAbsorb = mp.sigma_a[0] / sigma_maj[0];
                        Float pScatter = mp.sigma_s[0] / sigma_maj[0];
                        Float pNull = std::max<Float>(0, 1 - pAbsorb - pScatter);

                        // And randomly choose one.
                        int mode = SampleDiscrete({pAbsorb, pScatter, pNull}, uMode);

                        if (mode == 0)
                        {
                            // Absorption--done.
                            beta = SampledSpectrum(0.f);
                            // Tell the medium to stop traversal.
                            return false;
                        }
                        else if (mode == 1)
                        {
                            // Scattering.
                            Float pr = T_maj[0] * mp.sigma_s[0];
                            beta *= T_maj * mp.sigma_s / pr;
                            r_u *= T_maj * mp.sigma_s / pr;

                            // Enqueue medium scattering work.
                            auto enqueue = [=, this](auto ptr)
                            {
                                mediumScatterQueue->Push(spectra::MediumScatterWorkItem<
                                    std::remove_const_t<std::remove_reference_t<decltype(*ptr)>>>{
                                    p, w.depth, lambda, beta, r_u, ptr, -ray.d, ray.time,
                                    w.etaScale, ray.medium, w.pixelIndex
                                });
                            };
                            DCHECK_RARE(1e-6f, !beta);
                            if (beta && r_u)
                                mp.phase.Dispatch(enqueue);

                            scattered = true;

                            return false;
                        }
                        else
                        {
                            // Null scattering.
                            SampledSpectrum sigma_n =
                                ClampZero(sigma_maj - mp.sigma_a - mp.sigma_s);

                            Float pr = T_maj[0] * sigma_n[0];
                            beta *= T_maj * sigma_n / pr;
                            if (pr == 0)
                                beta = SampledSpectrum(0.f);
                            r_u *= T_maj * sigma_n / pr;
                            r_l *= T_maj * sigma_maj / pr;

                            uMode = rng.Uniform<Float>();

                            return beta && r_u;
                        }
                    });
                if (!scattered && beta)
                {
                    beta *= T_maj / T_maj[0];
                    r_u *= T_maj / T_maj[0];
                    r_l *= T_maj / T_maj[0];
                }


                // Add any emission found to its pixel sample's L value.
                if (L)
                {
                    SampledSpectrum Lp = pixelSampleState.L[w.pixelIndex];
                    pixelSampleState.L[w.pixelIndex] = Lp + L;
                }

                // There's no more work to do if there was a scattering event in
                // the medium.
                if (scattered || !beta || !r_u || w.depth == maxDepth)
                    return;

                // Otherwise, enqueue bump and medium work.
                if (w.tMax == std::numeric_limits<Float>::infinity())
                {
                    // no intersection
                    if (escapedRayQueue)
                    {
                        escapedRayQueue->Push(EscapedRayWorkItem{
                            ray.o, ray.d, w.depth, lambda, w.pixelIndex, beta,
                            (int)w.specularBounce, r_u, r_l, w.prevIntrCtx
                        });
                    }
                    return;
                }

                Material material = w.material;

                const MixMaterial* mix = material.CastOrNullptr<MixMaterial>();
                while (mix)
                {
                    SurfaceInteraction intr(w.pi, w.uv, w.wo, w.dpdus, w.dpdvs, w.dndus,
                                                  w.dndvs, ray.time, false /* flip normal */);
                    intr.faceIndex = w.faceIndex;
                    MaterialEvalContext ctx(intr);
                    material = mix->ChooseMaterial(BasicTextureEvaluator(), ctx);
                    mix = material.CastOrNullptr<MixMaterial>();
                }

                if (!material)
                {
                    Interaction intr(w.pi, w.n);
                    intr.mediumInterface = &w.mediumInterface;
                    Ray newRay = intr.SpawnRay(ray.d);
                    nextRayQueue->PushIndirectRay(
                        newRay, w.depth, w.prevIntrCtx, beta, r_u, r_l, lambda,
                        w.etaScale, w.specularBounce, w.anyNonSpecularBounces, w.pixelIndex);
                    return;
                }

                if (w.areaLight)
                {
                    hitAreaLightQueue->Push(HitAreaLightWorkItem{
                        w.areaLight, Point3f(w.pi), w.n, w.uv, -ray.d, lambda, w.depth, beta,
                        r_u, r_l, w.prevIntrCtx, w.specularBounce, w.pixelIndex
                    });
                }

                FloatTexture displacement = material.GetDisplacement();

                MaterialEvalQueue* q =
                (material.CanEvaluateTextures(BasicTextureEvaluator()) &&
                    (!displacement ||
                        BasicTextureEvaluator().CanEvaluate({displacement}, {})))
                    ? basicEvalMaterialQueue
                    : universalEvalMaterialQueue;


                auto enqueue = [=](auto ptr)
                {
                    q->Push<MaterialEvalWorkItem<std::remove_reference_t<decltype(*ptr)>>>(
                        spectra::MaterialEvalWorkItem<std::remove_reference_t<decltype(*ptr)>>{
                            ptr,
                            w.pi,
                            w.n,
                            w.dpdu,
                            w.dpdv,
                            ray.time,
                            w.depth,
                            w.ns,
                            w.dpdus,
                            w.dpdvs,
                            w.dndus,
                            w.dndvs,
                            w.uv,
                            w.faceIndex,
                            lambda,
                            w.pixelIndex,
                            w.anyNonSpecularBounces,
                            -ray.d,
                            beta,
                            r_u,
                            w.etaScale,
                            w.mediumInterface
                        });
                };
                material.Dispatch(enqueue);
            });

        if (wavefrontDepth == maxDepth)
            return;

        ForEachType(SampleMediumScatteringCallback{wavefrontDepth, this},
                          PhaseFunction::Types());
    }

    template <typename ConcretePhaseFunction>
    void WavefrontPathtracer::SampleMediumScattering(int wavefrontDepth)
    {
        RayQueue* currentRayQueue = CurrentRayQueue(wavefrontDepth);
        RayQueue* nextRayQueue = NextRayQueue(wavefrontDepth);

        std::string desc =
            std::string("Sample direct/indirect - ") + ConcretePhaseFunction::Name();
        spectra::ForAllQueued(
            desc.c_str(),
            mediumScatterQueue->Get<MediumScatterWorkItem<ConcretePhaseFunction>>(),
            maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(const spectra::MediumScatterWorkItem<ConcretePhaseFunction> w)
            {
                RaySamples raySamples = pixelSampleState.samples[w.pixelIndex];
                Vector3f wo = w.wo;

                // Sample direct lighting at medium scattering event.  First,
                // choose a light source.
                LightSampleContext ctx(Point3fi(w.p), Normal3f(0, 0, 0), Normal3f(0, 0, 0));
                pstd::optional<SampledLight> sampledLight =
                    lightSampler.Sample(ctx, raySamples.direct.uc);

                if (sampledLight)
                {
                    Light light = sampledLight->light;
                    // And now sample a point on the light.
                    pstd::optional<LightLiSample> ls =
                        light.SampleLi(ctx, raySamples.direct.u, w.lambda, true);
                    if (ls && ls->L && ls->pdf > 0)
                    {
                        Vector3f wi = ls->wi;
                        SampledSpectrum beta = w.beta * w.phase->p(wo, wi);


                        // Compute PDFs for direct lighting MIS calculation.
                        Float lightPDF = ls->pdf * sampledLight->p;
                        Float phasePDF =
                            IsDeltaLight(light.Type()) ? 0.f : w.phase->PDF(wo, wi);
                        SampledSpectrum r_u = w.r_u * phasePDF;
                        SampledSpectrum r_l = w.r_u * lightPDF;

                        SampledSpectrum Ld = beta * ls->L;
                        Ray ray(w.p, ls->pLight.p() - w.p, w.time, w.medium);

                        // Enqueue shadow ray
                        shadowRayQueue->Push(ShadowRayWorkItem{
                            ray, 1 - Float(0.0001f),
                            w.lambda, Ld, r_u, r_l,
                            w.pixelIndex
                        });

                    }
                }

                // Sample indirect lighting.
                pstd::optional<PhaseFunctionSample> phaseSample =
                    w.phase->Sample_p(wo, raySamples.indirect.u);
                if (!phaseSample || phaseSample->pdf == 0)
                    return;

                SampledSpectrum beta = w.beta * phaseSample->p / phaseSample->pdf;
                SampledSpectrum r_u = w.r_u;
                SampledSpectrum r_l = w.r_u / phaseSample->pdf;

                // Russian roulette
                SampledSpectrum rrBeta = beta * w.etaScale / r_u.Average();
                if (rrBeta.MaxComponentValue() < 1 && w.depth >= 1)
                {
                    Float q = std::max<Float>(0, 1 - rrBeta.MaxComponentValue());
                    if (raySamples.indirect.rr < q)
                    {
                        return;
                    }
                    beta /= 1 - q;
                }

                Ray ray(w.p, phaseSample->wi, w.time, w.medium);
                bool specularBounce = false;
                bool anyNonSpecularBounces = true;

                // Spawn indirect ray.
                nextRayQueue->PushIndirectRay(ray, w.depth + 1, ctx, beta, r_u, r_l,
                                              w.lambda, w.etaScale, specularBounce,
                                              anyNonSpecularBounces, w.pixelIndex);
            });
    }

    void WavefrontPathtracer::SampleSubsurface(int wavefrontDepth)
    {
        if (!haveSubsurface)
            return;

        RayQueue* nextRayQueue = NextRayQueue(wavefrontDepth);

        ForAllQueued(
            "Get BSSRDF and enqueue probe ray", bssrdfEvalQueue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(const spectra::GetBSSRDFAndProbeRayWorkItem w)
            {
                const SubsurfaceMaterial* material = w.material.Cast<SubsurfaceMaterial>();
                MaterialEvalContext ctx = w.GetMaterialEvalContext();
                SampledWavelengths lambda = w.lambda;
                SubsurfaceMaterial::BSSRDF bssrdf =
                    material->GetBSSRDF(BasicTextureEvaluator(), ctx, lambda);

                RaySamples raySamples = pixelSampleState.samples[w.pixelIndex];
                Float uc = raySamples.subsurface.uc;
                Point2f u = raySamples.subsurface.u;

                pstd::optional<BSSRDFProbeSegment> probeSeg = bssrdf.SampleSp(uc, u);
                if (probeSeg)
                    subsurfaceScatterQueue->Push(probeSeg->p0, probeSeg->p1, w.depth,
                                                 material, bssrdf, lambda, w.beta, w.r_u,
                                                 w.mediumInterface, w.etaScale, w.pixelIndex);
            });

        aggregate->IntersectOneRandom(maxQueueSize, subsurfaceScatterQueue);

        ForAllQueued(
            "Handle out-scattering after SSS", subsurfaceScatterQueue, maxQueueSize,
            SPECTRA_CPU_GPU_LAMBDA(spectra::SubsurfaceScatterWorkItem w)
            {
                if (w.reservoirPDF == 0)
                    return;

                TabulatedBSSRDF bssrdf = w.bssrdf;
                TabulatedBSSRDF::BxDF bxdf;

                SubsurfaceInteraction& intr = w.ssi;
                BSSRDFSample bssrdfSample = bssrdf.ProbeIntersectionToSample(intr, &bxdf);

                if (!bssrdfSample.Sp || !bssrdfSample.pdf)
                    return;

                Float pr = w.reservoirPDF * bssrdfSample.pdf[0];
                SampledSpectrum betap = w.beta * bssrdfSample.Sp / pr;
                SampledSpectrum r_u = w.r_u * bssrdfSample.pdf / bssrdfSample.pdf[0];
                SampledWavelengths lambda = w.lambda;
                RaySamples raySamples = pixelSampleState.samples[w.pixelIndex];
                Vector3f wo = bssrdfSample.wo;
                BSDF& bsdf = bssrdfSample.Sw;
                Float time = 0;

                // Indirect...
                {
                    Point2f u = raySamples.indirect.u;
                    Float uc = raySamples.indirect.uc;

                    pstd::optional<BSDFSample> bsdfSample =
                        bsdf.Sample_f<TabulatedBSSRDF::BxDF>(wo, uc, u);
                    if (bsdfSample)
                    {
                        Vector3f wi = bsdfSample->wi;
                        SampledSpectrum beta =
                            betap * bsdfSample->f * AbsDot(wi, intr.ns) / bsdfSample->pdf;
                        SampledSpectrum indir_r_u = r_u;


                        SampledSpectrum r_l;
                        if (bsdfSample->pdfIsProportional)
                            r_l =
                                r_u / bsdf.PDF<TabulatedBSSRDF::BxDF>(
                                    wo, bsdfSample->wi);
                        else
                            r_l = r_u / bsdfSample->pdf;

                        Float etaScale = w.etaScale;
                        if (bsdfSample->IsTransmission())
                            etaScale *= Sqr(bsdfSample->eta);

                        // Russian roulette
                        SampledSpectrum rrBeta = beta * etaScale / indir_r_u.Average();
                        if (rrBeta.MaxComponentValue() < 1 && w.depth > 1)
                        {
                            Float q = std::max<Float>(0, 1 - rrBeta.MaxComponentValue());
                            if (raySamples.indirect.rr < q)
                            {
                                beta = SampledSpectrum(0.f);
                            }
                            else
                                beta /= 1 - q;
                        }

                        if (beta)
                        {
                            Ray ray = SpawnRay(intr.pi, intr.n, time, wi);
                            if (haveMedia)
                                ray.medium = Dot(ray.d, intr.n) > 0
                                                 ? w.mediumInterface.outside
                                                 : w.mediumInterface.inside;

                            // || rather than | is intentional, to avoid the read if
                            // possible...
                            bool anyNonSpecularBounces = true;

                            LightSampleContext ctx(intr.pi, intr.n, intr.ns);
                            nextRayQueue->PushIndirectRay(
                                ray, w.depth + 1, ctx, beta, indir_r_u, r_l, lambda,
                                etaScale, bsdfSample->IsSpecular(), anyNonSpecularBounces,
                                w.pixelIndex);

                        }
                    }
                }

                // Direct lighting...
                if (IsNonSpecular(bsdf.Flags()))
                {
                    LightSampleContext ctx(intr.pi, intr.n, intr.ns);
                    pstd::optional<SampledLight> sampledLight =
                        lightSampler.Sample(ctx, raySamples.direct.uc);
                    if (!sampledLight)
                        return;
                    Light light = sampledLight->light;

                    pstd::optional<LightLiSample> ls =
                        light.SampleLi(ctx, raySamples.direct.u, lambda, true);
                    if (!ls || !ls->L || ls->pdf == 0)
                        return;

                    Vector3f wi = ls->wi;
                    SampledSpectrum f = bsdf.f<TabulatedBSSRDF::BxDF>(wo, wi);
                    if (!f)
                        return;

                    SampledSpectrum beta = betap * f * AbsDot(wi, intr.ns);


                    Float lightPDF = ls->pdf * sampledLight->p;
                    // This causes r_u to be zero for the shadow ray, so that
                    // part of MIS just becomes a no-op.
                    Float bsdfPDF = IsDeltaLight(light.Type())
                                              ? 0.f
                                              : bsdf.PDF<TabulatedBSSRDF::BxDF>(wo, wi);
                    SampledSpectrum r_l = r_u * lightPDF;
                    r_u *= bsdfPDF;

                    SampledSpectrum Ld = beta * ls->L;


                    Ray ray = SpawnRayTo(intr.pi, intr.n, time, ls->pLight.pi, ls->pLight.n);
                    if (haveMedia)
                        ray.medium = Dot(ray.d, intr.n) > 0
                                         ? w.mediumInterface.outside
                                         : w.mediumInterface.inside;

                    shadowRayQueue->Push(ShadowRayWorkItem{
                        ray, 1 - Float(0.0001f), lambda, Ld,
                        r_u, r_l, w.pixelIndex
                    });
                }
            });

        TraceShadowRays(wavefrontDepth);
    }

    void WavefrontPathtracer::UpdateFilm()
    {
        ParallelFor(
            "Update film", maxQueueSize, SPECTRA_CPU_GPU_LAMBDA(int pixelIndex)
            {
                // Check pixel against film bounds
                Point2i pPixel = pixelSampleState.pPixel[pixelIndex];
                if (!InsideExclusive(pPixel, film.PixelBounds()))
                    return;

                // Compute final weighted radiance value
                SampledSpectrum Lw = SampledSpectrum(pixelSampleState.L[pixelIndex]) *
                    pixelSampleState.cameraRayWeight[pixelIndex];

                // Provide sample radiance value to film
                SampledWavelengths lambda = pixelSampleState.lambda[pixelIndex];
                Float filterWeight = pixelSampleState.filterWeight[pixelIndex];
                if (initializeVisibleSurface)
                {
                    // Call _Film::AddSample()_ with _VisibleSurface_ for pixel sample
                    VisibleSurface visibleSurface =
                        pixelSampleState.visibleSurface[pixelIndex];
                    film.AddSample(pPixel, Lw, lambda, &visibleSurface, filterWeight);
                }
                else
                    film.AddSample(pPixel, Lw, lambda, nullptr, filterWeight);
            });
    }

    void WavefrontPathtracer::UpdateFramebufferFromFilm(Bounds2i pixelBounds,
                                                      Float exposure, float* rgba)
    {
        Vector2i resolution = pixelBounds.Diagonal();
        ParallelFor(
            "Update RGBA framebuffer", resolution.x * resolution.y,
            SPECTRA_CPU_GPU_LAMBDA(int index)
            {
                Point2i p(index % resolution.x, index / resolution.x);
                RGB rgb = exposure * film.GetPixelRGB(p + pixelBounds.pMin);
                int offset = 4 * index;
                rgba[offset] = static_cast<float>(rgb.r);
                rgba[offset + 1] = static_cast<float>(rgb.g);
                rgba[offset + 2] = static_cast<float>(rgb.b);
                rgba[offset + 3] = 1.0f;
            });
    }
} // namespace spectra::pathtracer
