#include <spectra/pathtracer/util/lowdiscrepancy.h>

#include <spectra/pathtracer/util/math.h>
#include <spectra/pathtracer/util/primes.h>

namespace spectra
{
    // Low Discrepancy Function Definitions
    pstd::vector<DigitPermutation>* ComputeRadicalInversePermutations(uint32_t seed,
                                                                      Allocator alloc)
    {
        pstd::vector<DigitPermutation>* perms =
            alloc.new_object<pstd::vector<DigitPermutation>>(alloc);
        perms->resize(PrimeTableSize);
        for (int i = 0; i < PrimeTableSize; ++i)
            (*perms)[i] = DigitPermutation(Primes[i], seed, alloc);
        return perms;
    }
} // namespace spectra
