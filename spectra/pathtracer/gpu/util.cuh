#ifndef SPECTRA_PATHTRACER_GPU_UTIL_H
#define SPECTRA_PATHTRACER_GPU_UTIL_H

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <map>
#include <spectra/pathtracer/core/diagnostics.cuh>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <typeindex>
#include <typeinfo>
#include <vector>

#ifdef NVTX
#ifdef UNICODE
#undef UNICODE
#endif
#include <nvtx3/nvToolsExt.h>

#ifdef RGB
#undef RGB
#endif // RGB
#endif

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
    inline int GetBlockSize(const char* description, F kernel) {
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
    void GPUParallelFor(const char* description, int nItems, F func);

    template <typename F>
    void GPUParallelFor(const char* description, int nItems, F func) {
#ifdef NVTX
        nvtxRangePush(description);
#endif
        auto kernel = &Kernel<F>;

        int blockSize = GetBlockSize(description, kernel);

        int gridSize = (nItems + blockSize - 1) / blockSize;
        kernel<<<gridSize, blockSize>>>(func, nItems);

#ifdef SPECTRA_DEBUG_BUILD
        CUDA_CHECK(cudaDeviceSynchronize());
#endif
#ifdef NVTX
        nvtxRangePop();
#endif
    }

#endif // __NVCC__

    // GPU Synchronization Function Declarations
    void GPUWait();

    void GPUInit();
    void GPUThreadInit();

    void GPUMemset(void* ptr, int byte, size_t bytes);

    void GPURegisterThread(const char* name);
    void GPUNameStream(cudaStream_t stream, const char* name);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_GPU_UTIL_H
