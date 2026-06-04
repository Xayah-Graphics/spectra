#ifndef SPECTRA_PATHTRACER_UTIL_PMJ02TABLES_H
#define SPECTRA_PATHTRACER_UTIL_PMJ02TABLES_H

#include <cstdint>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    // PMJ02BN Table Declarations
    constexpr int nPMJ02bnSets    = 5;
    constexpr int nPMJ02bnSamples = 65536;
#if defined(__CUDA_ARCH__)
    extern __device__ const uint32_t pmj02bnSamples[nPMJ02bnSets][nPMJ02bnSamples][2];
#else
    extern const uint32_t pmj02bnSamples[nPMJ02bnSets][nPMJ02bnSamples][2];
#endif

    // PMJ02BN Table Inline Functions
    __host__ __device__ inline Point2f GetPMJ02BNSample(int setIndex, int sampleIndex);

    __host__ __device__ inline Point2f GetPMJ02BNSample(int setIndex, int sampleIndex) {
        setIndex %= nPMJ02bnSets;
        DCHECK_LT(sampleIndex, nPMJ02bnSamples);
        sampleIndex %= nPMJ02bnSamples;

        // Convert from fixed-point.
#if defined(__CUDA_ARCH__)
        return Point2f(pmj02bnSamples[setIndex][sampleIndex][0] * 0x1p-32f, pmj02bnSamples[setIndex][sampleIndex][1] * 0x1p-32f);
#else
        // Double precision is key here for the pixel sample sorting, but not
        // necessary otherwise.
        return Point2f(pmj02bnSamples[setIndex][sampleIndex][0] * 0x1p-32, pmj02bnSamples[setIndex][sampleIndex][1] * 0x1p-32);
#endif
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_PMJ02TABLES_H
