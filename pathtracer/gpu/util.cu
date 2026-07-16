#include <pathtracer/gpu/util.cuh>

namespace spectra {
    int GPUInit(std::optional<int> cudaDevice) {
        int device = cudaDevice.value_or(0);
        CUDA_CHECK(cudaSetDevice(device));
        CUDA_CHECK(cudaFree(nullptr));

        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitStackSize, 8192));
        CUDA_CHECK(cudaDeviceSetLimit(cudaLimitPrintfFifoSize, 32 * 1024 * 1024));
        CUDA_CHECK(cudaDeviceSetCacheConfig(cudaFuncCachePreferL1));

        return device;
    }

    void GPUThreadInit(const int cudaDevice) {
        CUDA_CHECK(cudaSetDevice(cudaDevice));
    }

    void GPUWait(cudaStream_t stream) {
        CUDA_CHECK(cudaStreamSynchronize(stream));
    }

} // namespace spectra
