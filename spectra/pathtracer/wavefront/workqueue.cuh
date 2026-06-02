#ifndef SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H
#define SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H

#include <atomic>
#include <spectra/pathtracer/gpu/util.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <utility>

#if defined(__CUDA_ARCH__)
#if defined(SPECTRA_IS_WINDOWS)
#if (__CUDA_ARCH__ < 700)
#define SPECTRA_USE_LEGACY_CUDA_ATOMICS
#endif
#else
#if (__CUDA_ARCH__ < 600)
#define SPECTRA_USE_LEGACY_CUDA_ATOMICS
#endif
#endif // SPECTRA_IS_WINDOWS

#ifndef SPECTRA_USE_LEGACY_CUDA_ATOMICS
#include <cuda/atomic>
#endif
#endif // __CUDA_ARCH__

namespace spectra {
    // WorkQueue Definition
    template <typename WorkItem>
    class WorkQueue : public SOA<WorkItem> {
    public:
        // WorkQueue Public Methods
        WorkQueue() = default;

        WorkQueue(int n, Allocator alloc) : SOA<WorkItem>(n, alloc) {}

        WorkQueue& operator=(const WorkQueue& w) {
            SOA<WorkItem>::operator=(w);
#if defined(__CUDA_ARCH__) && defined(SPECTRA_USE_LEGACY_CUDA_ATOMICS)
            size = w.size;
#else
            size.store(w.size.load());
#endif
            return *this;
        }

        __host__ __device__ int Size() const {
#if defined(__CUDA_ARCH__)
#ifdef SPECTRA_USE_LEGACY_CUDA_ATOMICS
            return size;
#else
            return size.load(cuda::std::memory_order_relaxed);
#endif
#else
            return size.load(std::memory_order_relaxed);
#endif
        }

        __host__ __device__ void Reset() {
#if defined(__CUDA_ARCH__)
#ifdef SPECTRA_USE_LEGACY_CUDA_ATOMICS
            size = 0;
#else
            size.store(0, cuda::std::memory_order_relaxed);
#endif
#else
            size.store(0, std::memory_order_relaxed);
#endif
        }

        __host__ __device__ int Push(WorkItem w) {
            int index      = AllocateEntry();
            (*this)[index] = w;
            return index;
        }

    protected:
        // WorkQueue Protected Methods
        __host__ __device__ int AllocateEntry() {
#if defined(__CUDA_ARCH__)
#ifdef SPECTRA_USE_LEGACY_CUDA_ATOMICS
            return atomicAdd(&size, 1);
#else
            return size.fetch_add(1, cuda::std::memory_order_relaxed);
#endif
#else
            return size.fetch_add(1, std::memory_order_relaxed);
#endif
        }

    private:
        // WorkQueue Private Members
#if defined(__CUDA_ARCH__)
#ifdef SPECTRA_USE_LEGACY_CUDA_ATOMICS
        int size = 0;
#else
        cuda::atomic<int, cuda::thread_scope_device> size{0};
#endif
#else
        std::atomic<int> size{0};
#endif // __CUDA_ARCH__
    };

    // WorkQueue Inline Functions
    template <typename F, typename WorkItem>
    void ForAllQueued(const char* desc, const WorkQueue<WorkItem>* q, int maxQueued, F&& func) {
        GPUParallelFor(desc, maxQueued, [=] __device__(int index) mutable {
            if (index >= q->Size()) return;
            func((*q)[index]);
        });
    }

    // MultiWorkQueue Definition
    template <typename T>
    class MultiWorkQueue;

    template <typename... Ts>
    class MultiWorkQueue<TypePack<Ts...>> {
    public:
        // MultiWorkQueue Public Methods
        template <typename T>
        __host__ __device__ WorkQueue<T>* Get() {
            return &pstd::get<WorkQueue<T>>(queues);
        }

        MultiWorkQueue(int n, Allocator alloc, pstd::span<const bool> haveType) {
            int index = 0;
            ((*Get<Ts>() = WorkQueue<Ts>(haveType[index++] ? n : 1, alloc)), ...);
        }

        template <typename T>
        __host__ __device__ int Size() const {
            return Get<T>()->Size();
        }

        template <typename T>
        __host__ __device__ int Push(const T& value) {
            return Get<T>()->Push(value);
        }

        __host__ __device__ void Reset() {
            (Get<Ts>()->Reset(), ...);
        }

    private:
        // MultiWorkQueue Private Members
        pstd::tuple<WorkQueue<Ts>...> queues;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H
