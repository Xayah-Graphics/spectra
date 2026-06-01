#ifndef SPECTRA_PATHTRACER_GPU_MEMORY_H
#define SPECTRA_PATHTRACER_GPU_MEMORY_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <spectra/pathtracer/util/check.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/math.h>
#include <spectra/pathtracer/util/pstd.h>
#include <type_traits>
#include <unordered_map>

namespace spectra {
    class CUDAMemoryResource : public pstd::pmr::memory_resource {
        void* do_allocate(size_t size, size_t alignment);
        void do_deallocate(void* p, size_t bytes, size_t alignment);

        bool do_is_equal(const memory_resource& other) const noexcept {
            return this == &other;
        }
    };

    class CUDATrackedMemoryResource : public CUDAMemoryResource {
    public:
        void* do_allocate(size_t size, size_t alignment);
        void do_deallocate(void* p, size_t bytes, size_t alignment);

        bool do_is_equal(const memory_resource& other) const noexcept {
            return this == &other;
        }

        void PrefetchToGPU() const;
        size_t BytesAllocated() const {
            return bytesAllocated;
        }

        static CUDATrackedMemoryResource singleton;

    private:
        mutable std::mutex mutex;
        std::atomic<size_t> bytesAllocated{};
        std::unordered_map<void*, size_t> allocations;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_GPU_MEMORY_H
