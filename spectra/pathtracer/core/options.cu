#include <spectra/pathtracer/core/options.cuh>
#include <spectra/pathtracer/gpu/util.cuh>

namespace spectra {
    SpectraOptions* Options;

    __constant__ BasicSpectraOptions OptionsGPU;

    void CopyOptionsToGPU() {
        CUDA_CHECK(cudaMemcpyToSymbol(OptionsGPU, Options, sizeof(OptionsGPU)));
    }
} // namespace spectra
