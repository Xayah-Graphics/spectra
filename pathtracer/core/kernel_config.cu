#include <pathtracer/core/kernel_config.cuh>
#include <pathtracer/gpu/util.cuh>

namespace spectra::pathtracer {
    KernelConfig KernelConfigHost{};

    __constant__ KernelConfig KernelConfigGPU{};

    KernelConfig KernelConfigFrom(const RenderConfig& config) {
        return KernelConfig{
            .seed                      = config.seed,
            .disable_pixel_jitter      = config.disable_pixel_jitter,
            .disable_wavelength_jitter = config.disable_wavelength_jitter,
            .disable_texture_filtering = config.disable_texture_filtering,
        };
    }

    void UploadKernelConfig(const KernelConfig& config) {
        KernelConfigHost = config;
        CUDA_CHECK(cudaMemcpyToSymbol(KernelConfigGPU, &KernelConfigHost, sizeof(KernelConfig)));
    }
} // namespace spectra::pathtracer
