#ifndef SPECTRA_PATHTRACER_UTIL_PRIMES_H
#define SPECTRA_PATHTRACER_UTIL_PRIMES_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/pstd.cuh>

namespace spectra {
    // Prime Table Declarations
    static constexpr int PrimeTableSize = 1000;
#if defined(__CUDA_ARCH__)
    extern __device__ const int Primes[PrimeTableSize];
#else
    extern const int Primes[PrimeTableSize];
#endif
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_PRIMES_H
