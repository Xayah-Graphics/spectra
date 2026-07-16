#ifndef SPECTRA_PATHTRACER_BASE_SAMPLER_H
#define SPECTRA_PATHTRACER_BASE_SAMPLER_H

#include <cuda_runtime_api.h>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    class ParameterDictionary;
    struct FileLoc;

    namespace pathtracer {
        class PathtracerDeviceArena;
        struct RenderConfig;
    } // namespace pathtracer

    // CameraSample Definition
    struct CameraSample {
        Point2f pFilm;
        Point2f pLens;
        Float time         = 0;
        Float filterWeight = 1;
    };

    // Sampler Declarations
    class HaltonSampler;
    class PaddedSobolSampler;
    class PMJ02BNSampler;
    class IndependentSampler;
    class SobolSampler;
    class StratifiedSampler;
    class ZSobolSampler;

    // Sampler Definition
    class Sampler : public TaggedPointer< // Sampler Types
                        PMJ02BNSampler, IndependentSampler, StratifiedSampler, HaltonSampler, PaddedSobolSampler, SobolSampler, ZSobolSampler

                        > {
    public:
        // Sampler Interface
        using TaggedPointer::TaggedPointer;

        static Sampler Create(const std::string& name, const ParameterDictionary& parameters, Point2i fullResolution, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream);

        __host__ __device__ int SamplesPerPixel() const;

        __host__ __device__ void StartPixelSample(Point2i p, int sampleIndex, int dimension = 0);

        __host__ __device__ Float Get1D();
        __host__ __device__ Point2f Get2D();

        __host__ __device__ Point2f GetPixel2D();
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_SAMPLER_H
