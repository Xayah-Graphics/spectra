#include <algorithm>
#include <cuda.h>
#include <cuda_runtime.h>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/gpu/util.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/check.cuh>
#include <stdexcept>
#include <utility>

namespace spectra::pathtracer {
    namespace {
        std::mutex& CUDAMemoryMutex() {
            static std::mutex mutex;
            return mutex;
        }
    } // namespace

    PathtracerMemoryScope::PathtracerMemoryScope(const PathtracerMemoryScopeKind kind, std::string label) : kind(kind), label(std::move(label)) {
        if (this->label.empty()) throw std::runtime_error("Pathtracer memory scope requires a non-empty label");
    }

    PathtracerMemoryScope::~PathtracerMemoryScope() noexcept {
        this->ReleaseAllNoexcept();
    }

    [[nodiscard]] PathtracerMemoryScopeKind PathtracerMemoryScope::scope_kind() const noexcept {
        return this->kind;
    }

    [[nodiscard]] const std::string& PathtracerMemoryScope::scope_label() const noexcept {
        return this->label;
    }

    [[nodiscard]] PathtracerMemorySnapshot PathtracerMemoryScope::snapshot() const {
        std::lock_guard<std::mutex> lock(this->mutex);
        return PathtracerMemorySnapshot{
            .currentBytes    = this->currentBytes,
            .peakBytes       = this->peakBytes,
            .liveAllocations = this->allocations.size(),
            .peakAllocations = this->peakAllocations,
        };
    }

    void PathtracerMemoryScope::ReleaseAll() {
        std::lock_guard<std::mutex> cudaLock(CUDAMemoryMutex());
        std::lock_guard<std::mutex> allocationLock(this->mutex);
        for (auto iter = this->allocations.begin(); iter != this->allocations.end();) {
            void* ptr                    = iter->first;
            const AllocationRecord record = iter->second;
            if (record.kind != PathtracerAllocationKind::Managed) throw std::runtime_error("Pathtracer memory scope contains a non-managed allocation");
            CUDA_CHECK(cudaFree(ptr));
            this->currentBytes -= record.bytes;
            iter = this->allocations.erase(iter);
        }
    }

    void PathtracerMemoryScope::ReleaseAllNoexcept() noexcept {
        try {
            this->ReleaseAll();
        } catch (...) {
        }
    }

    void PathtracerMemoryScope::PrefetchManagedToGPU() const {
        int deviceIndex = 0;
        CUDA_CHECK(cudaGetDevice(&deviceIndex));

        std::lock_guard<std::mutex> lock(this->mutex);
        for (const std::pair<void* const, AllocationRecord>& allocation : this->allocations) {
            cudaMemLocation location = {};
            location.type            = cudaMemLocationTypeDevice;
            location.id              = deviceIndex;
            CUDA_CHECK(cudaMemPrefetchAsync(allocation.first, allocation.second.bytes, location, 0));
        }
        CUDA_CHECK(cudaDeviceSynchronize());
    }

    void* PathtracerMemoryScope::do_allocate(const std::size_t bytes, const std::size_t alignment) {
        if (bytes == 0) return nullptr;

        std::lock_guard<std::mutex> cudaLock(CUDAMemoryMutex());
        void* ptr = nullptr;
        CUDA_CHECK(cudaMallocManaged(&ptr, bytes));
        CHECK_EQ(0, reinterpret_cast<std::uintptr_t>(ptr) % alignment);

        std::lock_guard<std::mutex> allocationLock(this->mutex);
        this->note_allocation_locked(ptr, bytes, alignment, PathtracerAllocationKind::Managed);
        return ptr;
    }

    void PathtracerMemoryScope::do_deallocate(void* ptr, const std::size_t bytes, const std::size_t alignment) {
        if (ptr == nullptr) return;

        std::lock_guard<std::mutex> cudaLock(CUDAMemoryMutex());
        {
            std::lock_guard<std::mutex> allocationLock(this->mutex);
            const AllocationRecord record = this->remove_allocation_locked(ptr, bytes, alignment);
            static_cast<void>(record);
        }
        CUDA_CHECK(cudaFree(ptr));
    }

    bool PathtracerMemoryScope::do_is_equal(const memory_resource& other) const noexcept {
        return this == &other;
    }

    void PathtracerMemoryScope::note_allocation_locked(void* ptr, const std::size_t bytes, const std::size_t alignment, const PathtracerAllocationKind kind) {
        if (ptr == nullptr) throw std::runtime_error("Pathtracer memory scope cannot track a null allocation");
        if (this->allocations.find(ptr) != this->allocations.end()) throw std::runtime_error("Pathtracer memory scope received a duplicate allocation pointer");
        this->allocations.emplace(ptr,
            AllocationRecord{
                .bytes     = bytes,
                .alignment = alignment,
                .kind      = kind,
            });
        this->currentBytes += bytes;
        this->peakBytes       = std::max(this->peakBytes, this->currentBytes);
        this->peakAllocations = std::max(this->peakAllocations, this->allocations.size());
    }

    [[nodiscard]] PathtracerMemoryScope::AllocationRecord PathtracerMemoryScope::remove_allocation_locked(void* ptr, const std::size_t bytes, const std::size_t alignment) {
        auto iter = this->allocations.find(ptr);
        if (iter == this->allocations.end()) throw std::runtime_error("Pathtracer memory scope received an unknown deallocation pointer");
        const AllocationRecord record = iter->second;
        if (record.bytes != bytes) throw std::runtime_error("Pathtracer memory scope deallocation size mismatch");
        if (record.alignment != alignment) throw std::runtime_error("Pathtracer memory scope deallocation alignment mismatch");
        this->allocations.erase(iter);
        this->currentBytes -= record.bytes;
        return record;
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
        CUDA_CHECK(cudaStreamCreate(&this->stream));
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
