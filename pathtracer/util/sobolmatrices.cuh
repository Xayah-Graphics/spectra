#ifndef SPECTRA_PATHTRACER_UTIL_SOBOLMATRICES_H
#define SPECTRA_PATHTRACER_UTIL_SOBOLMATRICES_H

#include <cstdint>
#include <pathtracer/util/float.cuh>

namespace spectra {
    // Sobol Matrix Declarations
    static constexpr int NSobolDimensions = 1024;
    static constexpr int SobolMatrixSize  = 52;
#if defined(__CUDA_ARCH__)
    extern __device__ const uint32_t SobolMatrices32[NSobolDimensions * SobolMatrixSize];
    extern __device__ const uint64_t VdCSobolMatrices[][SobolMatrixSize];
    extern __device__ const uint64_t VdCSobolMatricesInv[][SobolMatrixSize];
#else
    extern const uint32_t SobolMatrices32[NSobolDimensions * SobolMatrixSize];
    extern const uint64_t VdCSobolMatrices[][SobolMatrixSize];
    extern const uint64_t VdCSobolMatricesInv[][SobolMatrixSize];
#endif
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_SOBOLMATRICES_H
