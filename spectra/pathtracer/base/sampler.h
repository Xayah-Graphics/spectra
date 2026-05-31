#ifndef SPECTRA_PATHTRACER_BASE_SAMPLER_H
#define SPECTRA_PATHTRACER_BASE_SAMPLER_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <string>
#include <vector>

namespace spectra
{
    class ParameterDictionary;
    struct FileLoc;

    // CameraSample Definition
    struct CameraSample
    {
        Point2f pFilm;
        Point2f pLens;
        Float time = 0;
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
    class DebugMLTSampler;

    // Sampler Definition
    class Sampler
        : public TaggedPointer< // Sampler Types
            PMJ02BNSampler, IndependentSampler, StratifiedSampler, HaltonSampler,
            PaddedSobolSampler, SobolSampler, ZSobolSampler, MLTSampler, DebugMLTSampler

        >
    {
    public:
        // Sampler Interface
        using TaggedPointer::TaggedPointer;

        static Sampler Create(const std::string& name, const ParameterDictionary& parameters,
                              Point2i fullResolution, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU int SamplesPerPixel() const;

        SPECTRA_CPU_GPU void StartPixelSample(Point2i p, int sampleIndex,
                                              int dimension = 0);

        SPECTRA_CPU_GPU Float Get1D();
        SPECTRA_CPU_GPU Point2f Get2D();

        SPECTRA_CPU_GPU Point2f GetPixel2D();

        Sampler Clone(Allocator alloc = {});

    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_SAMPLER_H
