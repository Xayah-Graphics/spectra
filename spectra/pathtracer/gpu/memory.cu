#include <cuda.h>
#include <cuda_runtime.h>
#include <spectra/pathtracer/gpu/memory.cuh>
#include <spectra/pathtracer/gpu/util.cuh>
#include <spectra/pathtracer/util/check.cuh>

namespace spectra {
    void* CUDAMemoryResource::do_allocate(size_t size, size_t alignment) {
        void* ptr;
        CUDA_CHECK(cudaMallocManaged(&ptr, size));
        CHECK_EQ(0, intptr_t(ptr) % alignment);
        return ptr;
    }

    void CUDAMemoryResource::do_deallocate(void* p, size_t bytes, size_t alignment) {
        CUDA_CHECK(cudaFree(p));
    }

    void* CUDATrackedMemoryResource::do_allocate(size_t size, size_t alignment) {
        if (size == 0) return nullptr;

        void* ptr;
        CUDA_CHECK(cudaMallocManaged(&ptr, size));
        DCHECK_EQ(0, intptr_t(ptr) % alignment);

        std::lock_guard<std::mutex> lock(mutex);
        allocations[ptr] = size;
        bytesAllocated += size;

        return ptr;
    }

    void CUDATrackedMemoryResource::do_deallocate(void* p, size_t size, size_t alignment) {
        if (!p) return;

        CUDA_CHECK(cudaFree(p));

        std::lock_guard<std::mutex> lock(mutex);
        auto iter = allocations.find(p);
        DCHECK(iter != allocations.end());
        allocations.erase(iter);
        bytesAllocated -= size;
    }

    void CUDATrackedMemoryResource::PrefetchToGPU() const {
        int deviceIndex;
        CUDA_CHECK(cudaGetDevice(&deviceIndex));

        std::lock_guard<std::mutex> lock(mutex);

        size_t bytes = 0;
        for (auto iter : allocations) {
            cudaMemLocation location = {};
            location.type            = cudaMemLocationTypeDevice;
            location.id              = deviceIndex;
            CUDA_CHECK(cudaMemPrefetchAsync(iter.first, iter.second, location, 0 /* stream */));
            bytes += iter.second;
        }
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    CUDATrackedMemoryResource CUDATrackedMemoryResource::singleton;
} // namespace spectra
