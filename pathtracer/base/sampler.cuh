#ifndef SPECTRA_PATHTRACER_BASE_SAMPLER_H
#define SPECTRA_PATHTRACER_BASE_SAMPLER_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    class ParameterDictionary;
    struct FileLoc;

    namespace pathtracer {
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
    class MLTSampler;

    // Sampler Definition
    class Sampler : public TaggedPointer< // Sampler Types
                        PMJ02BNSampler, IndependentSampler, StratifiedSampler, HaltonSampler, PaddedSobolSampler, SobolSampler, ZSobolSampler, MLTSampler

                        > {
    public:
        // Sampler Interface
        using TaggedPointer::TaggedPointer;

        static Sampler Create(const std::string& name, const ParameterDictionary& parameters, Point2i fullResolution, const pathtracer::RenderConfig& config, const FileLoc* loc, Allocator alloc);

        __host__ __device__ int SamplesPerPixel() const;

        __host__ __device__ void StartPixelSample(Point2i p, int sampleIndex, int dimension = 0);

        __host__ __device__ Float Get1D();
        __host__ __device__ Point2f Get2D();

        __host__ __device__ Point2f GetPixel2D();

        Sampler Clone(Allocator alloc = {});
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_SAMPLER_H
