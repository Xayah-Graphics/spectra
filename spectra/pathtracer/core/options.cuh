#ifndef SPECTRA_PATHTRACER_CORE_OPTIONS_H
#define SPECTRA_PATHTRACER_CORE_OPTIONS_H

#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    // RenderingCoordinateSystem Definition
    enum class RenderingCoordinateSystem { Camera, CameraWorld, World };

    // BasicSpectraOptions Definition
    struct BasicSpectraOptions {
        int seed                = 0;
        bool quiet              = false;
        bool disablePixelJitter = false, disableWavelengthJitter = false;
        bool disableTextureFiltering             = false;
        RenderingCoordinateSystem renderingSpace = RenderingCoordinateSystem::CameraWorld;
    };

    // SpectraOptions Definition
    struct SpectraOptions : BasicSpectraOptions {
        int nThreads = 0;
        pstd::optional<int> pixelSamples;
        pstd::optional<int> gpuDevice;
        std::string imageFile;
        pstd::optional<Bounds2f> cropWindow;
        pstd::optional<Bounds2i> pixelBounds;
        Float displacementEdgeScale = 1;
    };

    // Options Global Variable Declaration
    extern SpectraOptions* Options;

#if defined(__CUDACC__)
    extern __constant__ BasicSpectraOptions OptionsGPU;
#endif // __CUDACC__

    void CopyOptionsToGPU();

    // Options Inline Functions
    __host__ __device__ inline const BasicSpectraOptions& GetOptions();

    __host__ __device__ inline const BasicSpectraOptions& GetOptions() {
#if defined(__CUDA_ARCH__)
        return OptionsGPU;
#else
        return *Options;
#endif
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_OPTIONS_H
