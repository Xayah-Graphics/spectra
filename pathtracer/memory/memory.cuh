#ifndef SPECTRA_PATHTRACER_MEMORY_MEMORY_H
#define SPECTRA_PATHTRACER_MEMORY_MEMORY_H

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <pathtracer/util/pstd.cuh>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spectra::pathtracer {
    class PathtracerHostMemoryScope final : public pstd::pmr::memory_resource {
    public:
        PathtracerHostMemoryScope() = default;
        ~PathtracerHostMemoryScope() noexcept override;

        PathtracerHostMemoryScope(const PathtracerHostMemoryScope& other)                = delete;
        PathtracerHostMemoryScope(PathtracerHostMemoryScope&& other) noexcept            = delete;
        PathtracerHostMemoryScope& operator=(const PathtracerHostMemoryScope& other)     = delete;
        PathtracerHostMemoryScope& operator=(PathtracerHostMemoryScope&& other) noexcept = delete;

        void ReleaseAll();
        void ReleaseAllNoexcept() noexcept;
    private:
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;
        bool do_is_equal(const memory_resource& other) const noexcept override;

        std::mutex mutex{};
        std::unordered_map<void*, std::size_t> allocations{};
    };

    class PathtracerDeviceMemoryScope final : public pstd::pmr::memory_resource {
    public:
        PathtracerDeviceMemoryScope() = default;
        ~PathtracerDeviceMemoryScope() noexcept override;

        PathtracerDeviceMemoryScope(const PathtracerDeviceMemoryScope& other)                = delete;
        PathtracerDeviceMemoryScope(PathtracerDeviceMemoryScope&& other) noexcept            = delete;
        PathtracerDeviceMemoryScope& operator=(const PathtracerDeviceMemoryScope& other)     = delete;
        PathtracerDeviceMemoryScope& operator=(PathtracerDeviceMemoryScope&& other) noexcept = delete;

        void ReleaseAll();
        void ReleaseAllNoexcept() noexcept;

    private:
        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;
        bool do_is_equal(const memory_resource& other) const noexcept override;

        std::mutex mutex{};
        std::unordered_set<void*> allocations{};
    };

    class PathtracerDeviceBuffer final {
    public:
        PathtracerDeviceBuffer() = default;
        explicit PathtracerDeviceBuffer(std::size_t bytes);
        ~PathtracerDeviceBuffer() noexcept;

        PathtracerDeviceBuffer(const PathtracerDeviceBuffer& other)                = delete;
        PathtracerDeviceBuffer& operator=(const PathtracerDeviceBuffer& other)     = delete;
        PathtracerDeviceBuffer(PathtracerDeviceBuffer&& other) noexcept;
        PathtracerDeviceBuffer& operator=(PathtracerDeviceBuffer&& other) noexcept;

        void Allocate(std::size_t bytes);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] void* data() const noexcept;
        [[nodiscard]] CUdeviceptr device_ptr() const noexcept;
        [[nodiscard]] std::size_t size_bytes() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

    private:
        void* ptr{};
        std::size_t bytes{};
    };

    class PathtracerPinnedHostBuffer final {
    public:
        PathtracerPinnedHostBuffer() = default;
        explicit PathtracerPinnedHostBuffer(std::size_t bytes);
        ~PathtracerPinnedHostBuffer() noexcept;

        PathtracerPinnedHostBuffer(const PathtracerPinnedHostBuffer& other)                = delete;
        PathtracerPinnedHostBuffer& operator=(const PathtracerPinnedHostBuffer& other)     = delete;
        PathtracerPinnedHostBuffer(PathtracerPinnedHostBuffer&& other) noexcept;
        PathtracerPinnedHostBuffer& operator=(PathtracerPinnedHostBuffer&& other) noexcept;

        void Allocate(std::size_t bytes);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] void* data() const noexcept;
        [[nodiscard]] std::size_t size_bytes() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

    private:
        void* ptr{};
        std::size_t bytes{};
    };

    class PathtracerDeviceArena final {
    public:
        PathtracerDeviceArena() = default;

        PathtracerDeviceArena(const PathtracerDeviceArena& other)                = delete;
        PathtracerDeviceArena(PathtracerDeviceArena&& other) noexcept            = delete;
        PathtracerDeviceArena& operator=(const PathtracerDeviceArena& other)     = delete;
        PathtracerDeviceArena& operator=(PathtracerDeviceArena&& other) noexcept = delete;

        template <typename T>
        [[nodiscard]] T* Store(const T& value, cudaStream_t stream) {
            return StoreArray(&value, 1, stream);
        }

        template <typename T>
        [[nodiscard]] T* StoreArray(const T* values, std::size_t count, cudaStream_t stream) {
            if (count == 0) return nullptr;
            std::lock_guard lock(mutex);
            const std::size_t bytes = sizeof(T) * count;
            buffers.emplace_back(bytes);
            stagingBuffers.emplace_back(bytes);
            std::memcpy(stagingBuffers.back().data(), values, bytes);
            cudaError_t result = cudaMemcpyAsync(buffers.back().data(), stagingBuffers.back().data(), bytes, cudaMemcpyHostToDevice, stream);
            if (result != cudaSuccess) throw std::runtime_error(cudaGetErrorString(result));
            return static_cast<T*>(buffers.back().data());
        }

        void FinishUploads(cudaStream_t stream);

    private:
        std::mutex mutex{};
        std::vector<PathtracerDeviceBuffer> buffers{};
        std::vector<PathtracerPinnedHostBuffer> stagingBuffers{};
    };

    class PathtracerCudaEvent final {
    public:
        PathtracerCudaEvent() = default;
        explicit PathtracerCudaEvent(unsigned int flags);
        ~PathtracerCudaEvent() noexcept;

        PathtracerCudaEvent(const PathtracerCudaEvent& other)                = delete;
        PathtracerCudaEvent& operator=(const PathtracerCudaEvent& other)     = delete;
        PathtracerCudaEvent(PathtracerCudaEvent&& other) noexcept;
        PathtracerCudaEvent& operator=(PathtracerCudaEvent&& other) noexcept;

        void Create(unsigned int flags = cudaEventDefault);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaEvent_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaEvent_t event{};
    };

    class PathtracerCudaStream final {
    public:
        PathtracerCudaStream() = default;
        ~PathtracerCudaStream() noexcept;

        PathtracerCudaStream(const PathtracerCudaStream& other)                = delete;
        PathtracerCudaStream& operator=(const PathtracerCudaStream& other)     = delete;
        PathtracerCudaStream(PathtracerCudaStream&& other) noexcept;
        PathtracerCudaStream& operator=(PathtracerCudaStream&& other) noexcept;

        void Create();
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaStream_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaStream_t stream{};
    };

    class PathtracerCudaMappedBuffer final {
    public:
        PathtracerCudaMappedBuffer() = default;
        ~PathtracerCudaMappedBuffer() noexcept;

        PathtracerCudaMappedBuffer(const PathtracerCudaMappedBuffer& other)                = delete;
        PathtracerCudaMappedBuffer& operator=(const PathtracerCudaMappedBuffer& other)     = delete;
        PathtracerCudaMappedBuffer(PathtracerCudaMappedBuffer&& other) noexcept;
        PathtracerCudaMappedBuffer& operator=(PathtracerCudaMappedBuffer&& other) noexcept;

        void Adopt(void* mappedPtr);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] void* data() const noexcept;
        [[nodiscard]] bool empty() const noexcept;

    private:
        void* ptr{};
    };

    class PathtracerCudaExternalMemory final {
    public:
        PathtracerCudaExternalMemory() = default;
        ~PathtracerCudaExternalMemory() noexcept;

        PathtracerCudaExternalMemory(const PathtracerCudaExternalMemory& other)                = delete;
        PathtracerCudaExternalMemory& operator=(const PathtracerCudaExternalMemory& other)     = delete;
        PathtracerCudaExternalMemory(PathtracerCudaExternalMemory&& other) noexcept;
        PathtracerCudaExternalMemory& operator=(PathtracerCudaExternalMemory&& other) noexcept;

        void Import(const cudaExternalMemoryHandleDesc& desc);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaExternalMemory_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaExternalMemory_t memory{};
    };

    class PathtracerCudaExternalSemaphore final {
    public:
        PathtracerCudaExternalSemaphore() = default;
        ~PathtracerCudaExternalSemaphore() noexcept;

        PathtracerCudaExternalSemaphore(const PathtracerCudaExternalSemaphore& other)                = delete;
        PathtracerCudaExternalSemaphore& operator=(const PathtracerCudaExternalSemaphore& other)     = delete;
        PathtracerCudaExternalSemaphore(PathtracerCudaExternalSemaphore&& other) noexcept;
        PathtracerCudaExternalSemaphore& operator=(PathtracerCudaExternalSemaphore&& other) noexcept;

        void Import(const cudaExternalSemaphoreHandleDesc& desc);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaExternalSemaphore_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaExternalSemaphore_t semaphore{};
    };

    class PathtracerCudaMipmappedArray final {
    public:
        PathtracerCudaMipmappedArray() = default;
        ~PathtracerCudaMipmappedArray() noexcept;

        PathtracerCudaMipmappedArray(const PathtracerCudaMipmappedArray& other)                = delete;
        PathtracerCudaMipmappedArray& operator=(const PathtracerCudaMipmappedArray& other)     = delete;
        PathtracerCudaMipmappedArray(PathtracerCudaMipmappedArray&& other) noexcept;
        PathtracerCudaMipmappedArray& operator=(PathtracerCudaMipmappedArray&& other) noexcept;

        void Allocate(const cudaChannelFormatDesc& channelDesc, cudaExtent extent, unsigned int levels, unsigned int flags);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaMipmappedArray_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaMipmappedArray_t array{};
    };

    class PathtracerCudaTextureObject final {
    public:
        PathtracerCudaTextureObject() = default;
        ~PathtracerCudaTextureObject() noexcept;

        PathtracerCudaTextureObject(const PathtracerCudaTextureObject& other)                = delete;
        PathtracerCudaTextureObject& operator=(const PathtracerCudaTextureObject& other)     = delete;
        PathtracerCudaTextureObject(PathtracerCudaTextureObject&& other) noexcept;
        PathtracerCudaTextureObject& operator=(PathtracerCudaTextureObject&& other) noexcept;

        void Create(const cudaResourceDesc& resourceDesc, const cudaTextureDesc& textureDesc);
        void Release();
        void ReleaseNoexcept() noexcept;

        [[nodiscard]] cudaTextureObject_t get() const noexcept;
        [[nodiscard]] bool valid() const noexcept;

    private:
        cudaTextureObject_t texture{};
    };
} // namespace spectra::pathtracer

#endif // SPECTRA_PATHTRACER_MEMORY_MEMORY_H
