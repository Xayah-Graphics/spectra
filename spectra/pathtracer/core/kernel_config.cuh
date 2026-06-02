#ifndef SPECTRA_PATHTRACER_CORE_KERNEL_CONFIG_H
#define SPECTRA_PATHTRACER_CORE_KERNEL_CONFIG_H

#include <cuda_runtime_api.h>
#include <spectra/pathtracer/core/render_config.cuh>

namespace spectra::pathtracer {
    struct KernelConfig {
        int seed{};
        bool disable_pixel_jitter{};
        bool disable_wavelength_jitter{};
        bool disable_texture_filtering{};
    };

    extern KernelConfig KernelConfigHost;

#if defined(__CUDACC__)
    extern __constant__ KernelConfig KernelConfigGPU;
#endif // __CUDACC__

    [[nodiscard]] KernelConfig KernelConfigFrom(const RenderConfig& config);
    void UploadKernelConfig(const KernelConfig& config);

    __host__ __device__ inline const KernelConfig& CurrentKernelConfig() {
#if defined(__CUDA_ARCH__)
        return KernelConfigGPU;
#else
        return KernelConfigHost;
#endif
    }
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_CORE_KERNEL_CONFIG_H
