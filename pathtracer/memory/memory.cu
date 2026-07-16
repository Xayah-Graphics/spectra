#include <cuda.h>
#include <cuda_runtime.h>
#include <algorithm>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/check.cuh>
#include <new>
#include <stdexcept>
#include <utility>

namespace spectra::pathtracer {
    PathtracerHostMemoryScope::~PathtracerHostMemoryScope() noexcept {
        this->ReleaseAllNoexcept();
    }

    void PathtracerHostMemoryScope::ReleaseAll() {
        std::lock_guard<std::mutex> allocationLock(this->mutex);
        for (auto iter = this->allocations.begin(); iter != this->allocations.end();) {
            ::operator delete(iter->first, std::align_val_t(iter->second));
            iter = this->allocations.erase(iter);
        }
    }

    void PathtracerHostMemoryScope::ReleaseAllNoexcept() noexcept {
        try {
            this->ReleaseAll();
        } catch (...) {
        }
    }

    void* PathtracerHostMemoryScope::do_allocate(const std::size_t bytes, const std::size_t alignment) {
        if (bytes == 0) return nullptr;
        const std::size_t allocationAlignment = std::max(alignment, alignof(std::max_align_t));
        void* ptr = ::operator new(bytes, std::align_val_t(allocationAlignment));

        try {
            std::lock_guard<std::mutex> allocationLock(this->mutex);
            this->allocations.emplace(ptr, allocationAlignment);
        } catch (...) {
            ::operator delete(ptr, std::align_val_t(allocationAlignment));
            throw;
        }
        return ptr;
    }

    void PathtracerHostMemoryScope::do_deallocate(void* ptr, const std::size_t, const std::size_t) {
        if (ptr == nullptr) return;

        std::size_t alignment;
        {
            std::lock_guard<std::mutex> allocationLock(this->mutex);
            alignment = this->allocations.at(ptr);
            this->allocations.erase(ptr);
        }
        ::operator delete(ptr, std::align_val_t(alignment));
    }

    bool PathtracerHostMemoryScope::do_is_equal(const memory_resource& other) const noexcept {
        return this == &other;
    }

    PathtracerDeviceMemoryScope::~PathtracerDeviceMemoryScope() noexcept {
        this->ReleaseAllNoexcept();
    }

    void PathtracerDeviceMemoryScope::ReleaseAll() {
        std::lock_guard<std::mutex> lock(this->mutex);
        for (auto iter = this->allocations.begin(); iter != this->allocations.end();) {
            CUDA_CHECK(cudaFree(*iter));
            iter = this->allocations.erase(iter);
        }
    }

    void PathtracerDeviceMemoryScope::ReleaseAllNoexcept() noexcept {
        try {
            this->ReleaseAll();
        } catch (...) {
        }
    }

    void* PathtracerDeviceMemoryScope::do_allocate(const std::size_t bytes, const std::size_t) {
        if (bytes == 0) return nullptr;
        void* ptr{};
        CUDA_CHECK(cudaMalloc(&ptr, bytes));
        try {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->allocations.emplace(ptr);
        } catch (...) {
            CUDA_CHECK(cudaFree(ptr));
            throw;
        }
        return ptr;
    }

    void PathtracerDeviceMemoryScope::do_deallocate(void* ptr, const std::size_t, const std::size_t) {
        if (ptr == nullptr) return;
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            this->allocations.erase(ptr);
        }
        CUDA_CHECK(cudaFree(ptr));
    }

    bool PathtracerDeviceMemoryScope::do_is_equal(const memory_resource& other) const noexcept {
        return this == &other;
    }

    PathtracerDeviceBuffer::PathtracerDeviceBuffer(const std::size_t bytes) {
        this->Allocate(bytes);
    }

    PathtracerDeviceBuffer::~PathtracerDeviceBuffer() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerDeviceBuffer::PathtracerDeviceBuffer(PathtracerDeviceBuffer&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)), bytes(std::exchange(other.bytes, 0)) {}

    PathtracerDeviceBuffer& PathtracerDeviceBuffer::operator=(PathtracerDeviceBuffer&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->ptr   = std::exchange(other.ptr, nullptr);
        this->bytes = std::exchange(other.bytes, 0);
        return *this;
    }

    void PathtracerDeviceBuffer::Allocate(const std::size_t bytes) {
        if (bytes == 0) throw std::runtime_error("Pathtracer device buffer cannot allocate zero bytes");
        if (this->ptr != nullptr) throw std::runtime_error("Pathtracer device buffer is already allocated");
        CUDA_CHECK(cudaMalloc(&this->ptr, bytes));
        this->bytes = bytes;
    }

    void PathtracerDeviceBuffer::Release() {
        if (this->ptr == nullptr) return;
        void* releasePtr = std::exchange(this->ptr, nullptr);
        this->bytes      = 0;
        CUDA_CHECK(cudaFree(releasePtr));
    }

    void PathtracerDeviceBuffer::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] void* PathtracerDeviceBuffer::data() const noexcept {
        return this->ptr;
    }

    [[nodiscard]] CUdeviceptr PathtracerDeviceBuffer::device_ptr() const noexcept {
        return reinterpret_cast<CUdeviceptr>(this->ptr);
    }

    [[nodiscard]] std::size_t PathtracerDeviceBuffer::size_bytes() const noexcept {
        return this->bytes;
    }

    [[nodiscard]] bool PathtracerDeviceBuffer::empty() const noexcept {
        return this->ptr == nullptr;
    }

    PathtracerPinnedHostBuffer::PathtracerPinnedHostBuffer(const std::size_t bytes) {
        this->Allocate(bytes);
    }

    PathtracerPinnedHostBuffer::~PathtracerPinnedHostBuffer() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerPinnedHostBuffer::PathtracerPinnedHostBuffer(PathtracerPinnedHostBuffer&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)), bytes(std::exchange(other.bytes, 0)) {}

    PathtracerPinnedHostBuffer& PathtracerPinnedHostBuffer::operator=(PathtracerPinnedHostBuffer&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->ptr   = std::exchange(other.ptr, nullptr);
        this->bytes = std::exchange(other.bytes, 0);
        return *this;
    }

    void PathtracerPinnedHostBuffer::Allocate(const std::size_t bytes) {
        if (bytes == 0) throw std::runtime_error("Pathtracer pinned host buffer cannot allocate zero bytes");
        if (this->ptr != nullptr) throw std::runtime_error("Pathtracer pinned host buffer is already allocated");
        CUDA_CHECK(cudaMallocHost(&this->ptr, bytes));
        this->bytes = bytes;
    }

    void PathtracerPinnedHostBuffer::Release() {
        if (this->ptr == nullptr) return;
        void* releasePtr = std::exchange(this->ptr, nullptr);
        this->bytes      = 0;
        CUDA_CHECK(cudaFreeHost(releasePtr));
    }

    void PathtracerPinnedHostBuffer::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] void* PathtracerPinnedHostBuffer::data() const noexcept {
        return this->ptr;
    }

    [[nodiscard]] std::size_t PathtracerPinnedHostBuffer::size_bytes() const noexcept {
        return this->bytes;
    }

    [[nodiscard]] bool PathtracerPinnedHostBuffer::empty() const noexcept {
        return this->ptr == nullptr;
    }

    void PathtracerDeviceArena::FinishUploads(cudaStream_t stream) {
        std::lock_guard lock(mutex);
        if (stagingBuffers.empty()) return;
        CUDA_CHECK(cudaStreamSynchronize(stream));
        stagingBuffers.clear();
    }

    PathtracerCudaEvent::PathtracerCudaEvent(const unsigned int flags) {
        this->Create(flags);
    }

    PathtracerCudaEvent::~PathtracerCudaEvent() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaEvent::PathtracerCudaEvent(PathtracerCudaEvent&& other) noexcept : event(std::exchange(other.event, nullptr)) {}

    PathtracerCudaEvent& PathtracerCudaEvent::operator=(PathtracerCudaEvent&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->event = std::exchange(other.event, nullptr);
        return *this;
    }

    void PathtracerCudaEvent::Create(const unsigned int flags) {
        if (this->event != nullptr) throw std::runtime_error("Pathtracer CUDA event is already created");
        CUDA_CHECK(cudaEventCreateWithFlags(&this->event, flags));
    }

    void PathtracerCudaEvent::Release() {
        if (this->event == nullptr) return;
        cudaEvent_t releaseEvent = std::exchange(this->event, nullptr);
        CUDA_CHECK(cudaEventDestroy(releaseEvent));
    }

    void PathtracerCudaEvent::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaEvent_t PathtracerCudaEvent::get() const noexcept {
        return this->event;
    }

    [[nodiscard]] bool PathtracerCudaEvent::valid() const noexcept {
        return this->event != nullptr;
    }

    PathtracerCudaStream::~PathtracerCudaStream() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaStream::PathtracerCudaStream(PathtracerCudaStream&& other) noexcept : stream(std::exchange(other.stream, nullptr)) {}

    PathtracerCudaStream& PathtracerCudaStream::operator=(PathtracerCudaStream&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->stream = std::exchange(other.stream, nullptr);
        return *this;
    }

    void PathtracerCudaStream::Create() {
        if (this->stream != nullptr) throw std::runtime_error("Pathtracer CUDA stream is already created");
        CUDA_CHECK(cudaStreamCreateWithFlags(&this->stream, cudaStreamNonBlocking));
    }

    void PathtracerCudaStream::Release() {
        if (this->stream == nullptr) return;
        cudaStream_t releaseStream = std::exchange(this->stream, nullptr);
        CUDA_CHECK(cudaStreamDestroy(releaseStream));
    }

    void PathtracerCudaStream::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaStream_t PathtracerCudaStream::get() const noexcept {
        return this->stream;
    }

    [[nodiscard]] bool PathtracerCudaStream::valid() const noexcept {
        return this->stream != nullptr;
    }

    PathtracerCudaMappedBuffer::~PathtracerCudaMappedBuffer() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaMappedBuffer::PathtracerCudaMappedBuffer(PathtracerCudaMappedBuffer&& other) noexcept : ptr(std::exchange(other.ptr, nullptr)) {}

    PathtracerCudaMappedBuffer& PathtracerCudaMappedBuffer::operator=(PathtracerCudaMappedBuffer&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->ptr = std::exchange(other.ptr, nullptr);
        return *this;
    }

    void PathtracerCudaMappedBuffer::Adopt(void* mappedPtr) {
        if (mappedPtr == nullptr) throw std::runtime_error("Pathtracer CUDA mapped buffer cannot adopt a null pointer");
        if (this->ptr != nullptr) throw std::runtime_error("Pathtracer CUDA mapped buffer already owns a pointer");
        this->ptr = mappedPtr;
    }

    void PathtracerCudaMappedBuffer::Release() {
        if (this->ptr == nullptr) return;
        void* releasePtr = std::exchange(this->ptr, nullptr);
        CUDA_CHECK(cudaFree(releasePtr));
    }

    void PathtracerCudaMappedBuffer::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] void* PathtracerCudaMappedBuffer::data() const noexcept {
        return this->ptr;
    }

    [[nodiscard]] bool PathtracerCudaMappedBuffer::empty() const noexcept {
        return this->ptr == nullptr;
    }

    PathtracerCudaExternalMemory::~PathtracerCudaExternalMemory() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaExternalMemory::PathtracerCudaExternalMemory(PathtracerCudaExternalMemory&& other) noexcept : memory(std::exchange(other.memory, nullptr)) {}

    PathtracerCudaExternalMemory& PathtracerCudaExternalMemory::operator=(PathtracerCudaExternalMemory&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->memory = std::exchange(other.memory, nullptr);
        return *this;
    }

    void PathtracerCudaExternalMemory::Import(const cudaExternalMemoryHandleDesc& desc) {
        if (this->memory != nullptr) throw std::runtime_error("Pathtracer CUDA external memory is already imported");
        CUDA_CHECK(cudaImportExternalMemory(&this->memory, &desc));
    }

    void PathtracerCudaExternalMemory::Release() {
        if (this->memory == nullptr) return;
        cudaExternalMemory_t releaseMemory = std::exchange(this->memory, nullptr);
        CUDA_CHECK(cudaDestroyExternalMemory(releaseMemory));
    }

    void PathtracerCudaExternalMemory::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaExternalMemory_t PathtracerCudaExternalMemory::get() const noexcept {
        return this->memory;
    }

    [[nodiscard]] bool PathtracerCudaExternalMemory::valid() const noexcept {
        return this->memory != nullptr;
    }

    PathtracerCudaExternalSemaphore::~PathtracerCudaExternalSemaphore() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaExternalSemaphore::PathtracerCudaExternalSemaphore(PathtracerCudaExternalSemaphore&& other) noexcept : semaphore(std::exchange(other.semaphore, nullptr)) {}

    PathtracerCudaExternalSemaphore& PathtracerCudaExternalSemaphore::operator=(PathtracerCudaExternalSemaphore&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->semaphore = std::exchange(other.semaphore, nullptr);
        return *this;
    }

    void PathtracerCudaExternalSemaphore::Import(const cudaExternalSemaphoreHandleDesc& desc) {
        if (this->semaphore != nullptr) throw std::runtime_error("Pathtracer CUDA external semaphore is already imported");
        CUDA_CHECK(cudaImportExternalSemaphore(&this->semaphore, &desc));
    }

    void PathtracerCudaExternalSemaphore::Release() {
        if (this->semaphore == nullptr) return;
        cudaExternalSemaphore_t releaseSemaphore = std::exchange(this->semaphore, nullptr);
        CUDA_CHECK(cudaDestroyExternalSemaphore(releaseSemaphore));
    }

    void PathtracerCudaExternalSemaphore::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaExternalSemaphore_t PathtracerCudaExternalSemaphore::get() const noexcept {
        return this->semaphore;
    }

    [[nodiscard]] bool PathtracerCudaExternalSemaphore::valid() const noexcept {
        return this->semaphore != nullptr;
    }

    PathtracerCudaMipmappedArray::~PathtracerCudaMipmappedArray() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaMipmappedArray::PathtracerCudaMipmappedArray(PathtracerCudaMipmappedArray&& other) noexcept : array(std::exchange(other.array, nullptr)) {}

    PathtracerCudaMipmappedArray& PathtracerCudaMipmappedArray::operator=(PathtracerCudaMipmappedArray&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->array = std::exchange(other.array, nullptr);
        return *this;
    }

    void PathtracerCudaMipmappedArray::Allocate(const cudaChannelFormatDesc& channelDesc, const cudaExtent extent, const unsigned int levels, const unsigned int flags) {
        if (levels == 0) throw std::runtime_error("Pathtracer CUDA mipmapped array requires at least one level");
        if (this->array != nullptr) throw std::runtime_error("Pathtracer CUDA mipmapped array is already allocated");
        CUDA_CHECK(cudaMallocMipmappedArray(&this->array, &channelDesc, extent, levels, flags));
    }

    void PathtracerCudaMipmappedArray::Release() {
        if (this->array == nullptr) return;
        cudaMipmappedArray_t releaseArray = std::exchange(this->array, nullptr);
        CUDA_CHECK(cudaFreeMipmappedArray(releaseArray));
    }

    void PathtracerCudaMipmappedArray::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaMipmappedArray_t PathtracerCudaMipmappedArray::get() const noexcept {
        return this->array;
    }

    [[nodiscard]] bool PathtracerCudaMipmappedArray::valid() const noexcept {
        return this->array != nullptr;
    }

    PathtracerCudaTextureObject::~PathtracerCudaTextureObject() noexcept {
        this->ReleaseNoexcept();
    }

    PathtracerCudaTextureObject::PathtracerCudaTextureObject(PathtracerCudaTextureObject&& other) noexcept : texture(std::exchange(other.texture, 0)) {}

    PathtracerCudaTextureObject& PathtracerCudaTextureObject::operator=(PathtracerCudaTextureObject&& other) noexcept {
        if (this == &other) return *this;
        this->ReleaseNoexcept();
        this->texture = std::exchange(other.texture, 0);
        return *this;
    }

    void PathtracerCudaTextureObject::Create(const cudaResourceDesc& resourceDesc, const cudaTextureDesc& textureDesc) {
        if (this->texture != 0) throw std::runtime_error("Pathtracer CUDA texture object is already created");
        CUDA_CHECK(cudaCreateTextureObject(&this->texture, &resourceDesc, &textureDesc, nullptr));
    }

    void PathtracerCudaTextureObject::Release() {
        if (this->texture == 0) return;
        cudaTextureObject_t releaseTexture = std::exchange(this->texture, 0);
        CUDA_CHECK(cudaDestroyTextureObject(releaseTexture));
    }

    void PathtracerCudaTextureObject::ReleaseNoexcept() noexcept {
        try {
            this->Release();
        } catch (...) {
        }
    }

    [[nodiscard]] cudaTextureObject_t PathtracerCudaTextureObject::get() const noexcept {
        return this->texture;
    }

    [[nodiscard]] bool PathtracerCudaTextureObject::valid() const noexcept {
        return this->texture != 0;
    }
} // namespace spectra::pathtracer
