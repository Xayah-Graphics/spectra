#ifndef SPECTRA_PATHTRACER_GPU_UTIL_H
#define SPECTRA_PATHTRACER_GPU_UTIL_H

#include <cuda_runtime_api.h>
#include <map>
#include <mutex>
#include <optional>
#include <pathtracer/core/diagnostics.cuh>

#define CUDA_CHECK(EXPR)                                                                                                  \
    do {                                                                                                                  \
        cudaError_t cuda_check_result = (EXPR);                                                                           \
        if (cuda_check_result != cudaSuccess) SPECTRA_FATAL("CUDA call %s failed: %s", #EXPR, cudaGetErrorString(cuda_check_result)); \
    } while (false)

namespace spectra {
    template <typename F>
    inline int GetBlockSize(F kernel) {
        static std::mutex mutex;
        static std::map<int, int> blockSizes;
        int device{};
        CUDA_CHECK(cudaGetDevice(&device));
        std::lock_guard<std::mutex> lock(mutex);
        if (auto iter = blockSizes.find(device); iter != blockSizes.end()) return iter->second;

        int minGridSize, blockSize;
        CUDA_CHECK(cudaOccupancyMaxPotentialBlockSize(&minGridSize, &blockSize, kernel, 0, 0));
        blockSizes[device] = blockSize;

        return blockSize;
    }

#ifdef __NVCC__
    template <typename F>
    __global__ void Kernel(F func, int nItems) {
        int tid = blockIdx.x * blockDim.x + threadIdx.x;
        if (tid >= nItems) return;

        func(tid);
    }

    // GPU Launch Function Declarations
    template <typename F>
    void GPUParallelFor(int nItems, F func, cudaStream_t stream);

    template <typename F>
    void GPUParallelFor(int nItems, F func, cudaStream_t stream) {
        auto kernel = &Kernel<F>;

        int blockSize = GetBlockSize(kernel);

        int gridSize = (nItems + blockSize - 1) / blockSize;
        kernel<<<gridSize, blockSize, 0, stream>>>(func, nItems);
    }
#endif // __NVCC__

    // GPU Synchronization Function Declarations
    void GPUWait(cudaStream_t stream);

    [[nodiscard]] int GPUInit(std::optional<int> cudaDevice);
    void GPUThreadInit(int cudaDevice);

} // namespace spectra

#endif // SPECTRA_PATHTRACER_GPU_UTIL_H
