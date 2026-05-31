#include <spectra/pathtracer/core/options.h>

#include <spectra/pathtracer/gpu/util.h>

namespace spectra
{
    SpectraOptions* Options;

    __constant__ BasicSpectraOptions OptionsGPU;

    void CopyOptionsToGPU()
    {
        CUDA_CHECK(cudaMemcpyToSymbol(OptionsGPU, Options, sizeof(OptionsGPU)));
    }

    std::string ToString(const RenderingCoordinateSystem& r)
    {
        if (r == RenderingCoordinateSystem::Camera)
            return "RenderingCoordinateSystem::Camera";
        else if (r == RenderingCoordinateSystem::CameraWorld)
            return "RenderingCoordinateSystem::CameraWorld";
        else
        {
            CHECK(r == RenderingCoordinateSystem::World);
            return "RenderingCoordinateSystem::World";
        }
    }
} // namespace spectra
