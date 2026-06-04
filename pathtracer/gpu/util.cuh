#ifndef SPECTRA_PATHTRACER_GPU_UTIL_H
#define SPECTRA_PATHTRACER_GPU_UTIL_H

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <map>
#include <optional>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/util/check.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/parallel.cuh>
#include <typeindex>
#include <typeinfo>
#include <vector>

#define CUDA_CHECK(EXPR)                                            \
    if (EXPR != cudaSuccess) {                                      \
        cudaError_t error = cudaGetLastError();                     \
        SPECTRA_FATAL("CUDA error: %s", cudaGetErrorString(error)); \
    } else /* eat semicolon */

#define CU_CHECK(EXPR)                                              \
    do {                                                            \
        CUresult result = EXPR;                                     \
        if (result != CUDA_SUCCESS) {                               \
            const char* str;                                        \
            CHECK_EQ(CUDA_SUCCESS, cuGetErrorString(result, &str)); \
            SPECTRA_FATAL("CUDA error: %s", str);                   \
        }                                                           \
    } while (false) /* eat semicolon */

namespace spectra {
    template <typename F>
    inline int GetBlockSize(F kernel) {
        // Note: this isn't reentrant, but that's fine for our purposes...
        static std::map<std::type_index, int> kernelBlockSizes;

        std::type_index index = std::type_index(typeid(F));

        auto iter = kernelBlockSizes.find(index);
        if (iter != kernelBlockSizes.end()) return iter->second;

        int minGridSize, blockSize;
        CUDA_CHECK(cudaOccupancyMaxPotentialBlockSize(&minGridSize, &blockSize, kernel, 0, 0));
        kernelBlockSizes[index] = blockSize;

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
    void GPUParallelFor(int nItems, F func);

    template <typename F>
    void GPUParallelFor(int nItems, F func) {
        auto kernel = &Kernel<F>;

        int blockSize = GetBlockSize(kernel);

        int gridSize = (nItems + blockSize - 1) / blockSize;
        kernel<<<gridSize, blockSize>>>(func, nItems);
    }

#endif // __NVCC__

    // GPU Synchronization Function Declarations
    void GPUWait();

    [[nodiscard]] int GPUInit(std::optional<int> cudaDevice);
    void GPUThreadInit(int cudaDevice);

    void GPUMemset(void* ptr, int byte, size_t bytes);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_GPU_UTIL_H
