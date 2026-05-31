#ifndef SPECTRA_PATHTRACER_CORE_OPTIONS_H
#define SPECTRA_PATHTRACER_CORE_OPTIONS_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <string>

namespace spectra
{
    // RenderingCoordinateSystem Definition
    enum class RenderingCoordinateSystem { Camera, CameraWorld, World };

    std::string ToString(const RenderingCoordinateSystem&);

    // BasicSpectraOptions Definition
    struct BasicSpectraOptions
    {
        int seed = 0;
        bool quiet = false;
        bool disablePixelJitter = false, disableWavelengthJitter = false;
        bool disableTextureFiltering = false;
        RenderingCoordinateSystem renderingSpace = RenderingCoordinateSystem::CameraWorld;
    };

    // SpectraOptions Definition
    struct SpectraOptions : BasicSpectraOptions
    {
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
#endif  // __CUDACC__

    void CopyOptionsToGPU();

    // Options Inline Functions
    SPECTRA_CPU_GPU inline const BasicSpectraOptions& GetOptions();

    SPECTRA_CPU_GPU inline const BasicSpectraOptions& GetOptions()
    {
#if defined(SPECTRA_IS_GPU_CODE)
        return OptionsGPU;
#else
        return *Options;
#endif
    }
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_CORE_OPTIONS_H
