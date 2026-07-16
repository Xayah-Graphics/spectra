#ifndef SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H
#define SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H

#include <cuda/atomic>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/parallel.cuh>
#include <pathtracer/util/pstd.cuh>
#include <utility>

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
            size = w.size;
            return *this;
        }

        __device__ int Size() const {
            return size;
        }

        __device__ void Reset() {
            size = 0;
        }

        __device__ int Push(WorkItem w) {
            int index      = AllocateEntry();
            (*this)[index] = w;
            return index;
        }

    protected:
        // WorkQueue Protected Methods
        __device__ int AllocateEntry() {
            cuda::atomic_ref<int, cuda::thread_scope_device> count(size);
            return count.fetch_add(1, cuda::std::memory_order_relaxed);
        }

    private:
        // WorkQueue Private Members
        int size{};
    };

    // WorkQueue Inline Functions
    template <typename F, typename WorkItem>
    void ForAllQueued(const WorkQueue<WorkItem>* q, int maxQueued, cudaStream_t stream, F&& func) {
        GPUParallelFor(maxQueued, [=] __device__(int index) mutable {
            if (index >= q->Size()) return;
            func((*q)[index]);
        }, stream);
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

        template <typename T>
        __host__ __device__ const WorkQueue<T>* Get() const {
            return &pstd::get<WorkQueue<T>>(queues);
        }

        MultiWorkQueue(int n, Allocator alloc, pstd::span<const bool> haveType) {
            int index = 0;
            ((*Get<Ts>() = WorkQueue<Ts>(haveType[index++] ? n : 1, alloc)), ...);
        }

        template <typename T>
        __device__ int Size() const {
            return Get<T>()->Size();
        }

        template <typename T>
        __device__ int Push(const T& value) {
            return Get<T>()->Push(value);
        }

        __device__ void Reset() {
            (Get<Ts>()->Reset(), ...);
        }

    private:
        // MultiWorkQueue Private Members
        pstd::tuple<WorkQueue<Ts>...> queues;
    };

    template <typename WorkItem, typename Queue, typename Function>
    struct MultiWorkQueueForEach {
        const Queue* queues;
        Function function;

        __device__ void operator()(int index) {
            const WorkQueue<WorkItem>* queue = queues->template Get<WorkItem>();
            if (index >= queue->Size()) return;
            function((*queue)[index]);
        }
    };

    template <typename WorkItem, typename... Ts, typename F>
    void ForAllQueuedType(const MultiWorkQueue<TypePack<Ts...>>* queues, int maxQueued, cudaStream_t stream, F function) {
        GPUParallelFor(maxQueued, MultiWorkQueueForEach<WorkItem, MultiWorkQueue<TypePack<Ts...>>, F>{queues, function}, stream);
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_WAVEFRONT_WORKQUEUE_H
