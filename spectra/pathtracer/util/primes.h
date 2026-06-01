#ifndef SPECTRA_PATHTRACER_UTIL_PRIMES_H
#define SPECTRA_PATHTRACER_UTIL_PRIMES_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/pstd.h>

namespace spectra {
    // Prime Table Declarations
    static constexpr int PrimeTableSize = 1000;
    extern SPECTRA_CONST int Primes[PrimeTableSize];
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_PRIMES_H
