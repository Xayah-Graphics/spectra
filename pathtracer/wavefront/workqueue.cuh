#ifndef SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H
#define SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H

#include <atomic>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/parallel.cuh>
#include <pathtracer/util/pstd.cuh>
#include <utility>

#if defined(__CUDA_ARCH__)
#include <cuda/atomic>
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
            size.store(w.size.load());
            return *this;
        }

        __host__ __device__ int Size() const {
#if defined(__CUDA_ARCH__)
            return size.load(cuda::std::memory_order_relaxed);
#else
            return size.load(std::memory_order_relaxed);
#endif
        }

        __host__ __device__ void Reset() {
#if defined(__CUDA_ARCH__)
            size.store(0, cuda::std::memory_order_relaxed);
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
            return size.fetch_add(1, cuda::std::memory_order_relaxed);
#else
            return size.fetch_add(1, std::memory_order_relaxed);
#endif
        }

    private:
        // WorkQueue Private Members
#if defined(__CUDA_ARCH__)
        cuda::atomic<int, cuda::thread_scope_device> size{0};
#else
        std::atomic<int> size{0};
#endif // __CUDA_ARCH__
    };

    // WorkQueue Inline Functions
    template <typename F, typename WorkItem>
    void ForAllQueued(const WorkQueue<WorkItem>* q, int maxQueued, F&& func) {
        GPUParallelFor(maxQueued, [=] __device__(int index) mutable {
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
