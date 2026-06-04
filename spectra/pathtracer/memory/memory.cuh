#ifndef SPECTRA_PATHTRACER_MEMORY_MEMORY_H
#define SPECTRA_PATHTRACER_MEMORY_MEMORY_H

#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <spectra/pathtracer/util/pstd.cuh>
#include <string>
#include <unordered_map>

namespace spectra::pathtracer {
    enum class PathtracerMemoryScopeKind {
        Runtime,
        Scene,
        Frame,
        Transient,
        Texture,
        OptiX,
        Interop,
    };

    enum class PathtracerAllocationKind {
        Managed,
        Device,
        HostPinned,
        MipmappedArray,
        TextureObject,
        ExternalMemory,
        ExternalSemaphore,
        Event,
        Stream,
        OptiXHandle,
    };

    struct PathtracerMemorySnapshot {
        std::size_t currentBytes{};
        std::size_t peakBytes{};
        std::size_t liveAllocations{};
        std::size_t peakAllocations{};
    };

    class PathtracerMemoryScope final : public pstd::pmr::memory_resource {
    public:
        PathtracerMemoryScope(PathtracerMemoryScopeKind kind, std::string label);
        ~PathtracerMemoryScope() noexcept override;

        PathtracerMemoryScope(const PathtracerMemoryScope& other)                = delete;
        PathtracerMemoryScope(PathtracerMemoryScope&& other) noexcept            = delete;
        PathtracerMemoryScope& operator=(const PathtracerMemoryScope& other)     = delete;
        PathtracerMemoryScope& operator=(PathtracerMemoryScope&& other) noexcept = delete;

        [[nodiscard]] PathtracerMemoryScopeKind scope_kind() const noexcept;
        [[nodiscard]] const std::string& scope_label() const noexcept;
        [[nodiscard]] PathtracerMemorySnapshot snapshot() const;

        void ReleaseAll();
        void ReleaseAllNoexcept() noexcept;
        void PrefetchManagedToGPU() const;

    private:
        struct AllocationRecord {
            std::size_t bytes{};
            std::size_t alignment{};
            PathtracerAllocationKind kind{PathtracerAllocationKind::Managed};
        };

        void* do_allocate(std::size_t bytes, std::size_t alignment) override;
        void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;
        bool do_is_equal(const pstd::pmr::memory_resource& other) const noexcept override;

        void note_allocation_locked(void* ptr, std::size_t bytes, std::size_t alignment, PathtracerAllocationKind kind);
        [[nodiscard]] AllocationRecord remove_allocation_locked(void* ptr, std::size_t bytes, std::size_t alignment);

        PathtracerMemoryScopeKind kind{};
        std::string label{};
        mutable std::mutex mutex{};
        std::unordered_map<void*, AllocationRecord> allocations{};
        std::size_t currentBytes{};
        std::size_t peakBytes{};
        std::size_t peakAllocations{};
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
