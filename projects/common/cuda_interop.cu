#include "cuda_interop.h"
#include <algorithm>
#include <array>
#include <cuda_runtime.h>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>

namespace xayah::projects::cuda_interop {
    namespace {
        void check_cuda(const cudaError_t result, const char* operation) {
            if (result == cudaSuccess) return;
            throw std::runtime_error(std::string{operation} + ": " + cudaGetErrorString(result));
        }

        __global__ void pack_float3_kernel(float3* destination, const float* x, const float* y, const float* z, const std::uint64_t count) {
            const std::uint64_t index = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
            if (index < count) destination[index] = {x[index], y[index], z[index]};
        }
    } // namespace

    void select_matching_device(const std::uint8_t* uuid, const std::uint8_t* luid, const std::uint32_t node_mask) {
        int device_count{};
        check_cuda(cudaGetDeviceCount(&device_count), "cudaGetDeviceCount");
        for (int device_index = 0; device_index < device_count; ++device_index) {
            cudaDeviceProp device_properties{};
            check_cuda(cudaGetDeviceProperties(&device_properties, device_index), "cudaGetDeviceProperties");
            if (!std::ranges::equal(std::span{reinterpret_cast<const std::uint8_t*>(device_properties.uuid.bytes), 16}, std::span{uuid, 16})) continue;
            if (!std::ranges::equal(std::span{reinterpret_cast<const std::uint8_t*>(device_properties.luid), 8}, std::span{luid, 8}) || device_properties.luidDeviceNodeMask != node_mask) throw std::runtime_error("CUDA and Vulkan LUID/device-node identity disagree");
            check_cuda(cudaSetDevice(device_index), "cudaSetDevice");
            return;
        }
        throw std::runtime_error("CUDA cannot find the Vulkan physical device UUID");
    }

    ImportedBuffer import_buffer(void* external_memory_handle, const std::uint64_t byte_size) {
        cudaExternalMemoryHandleDesc description{};
        description.type                = cudaExternalMemoryHandleTypeOpaqueWin32;
        description.handle.win32.handle = external_memory_handle;
        description.size                = byte_size;
        cudaExternalMemory_t external_memory{};
        check_cuda(cudaImportExternalMemory(&external_memory, &description), "cudaImportExternalMemory");
        cudaExternalMemoryBufferDesc buffer_description{};
        buffer_description.size = byte_size;
        void* device_pointer{};
        check_cuda(cudaExternalMemoryGetMappedBuffer(&device_pointer, external_memory, &buffer_description), "cudaExternalMemoryGetMappedBuffer");
        return {external_memory, device_pointer, byte_size};
    }

    void destroy_imported_buffer(ImportedBuffer& buffer) noexcept {
        if (buffer.device_pointer) cudaFree(buffer.device_pointer);
        if (buffer.cuda_external_memory) cudaDestroyExternalMemory(static_cast<cudaExternalMemory_t>(buffer.cuda_external_memory));
        buffer = {};
    }

    ImportedTimelineSemaphore import_timeline_semaphore(void* timeline_semaphore_handle) {
        cudaExternalSemaphoreHandleDesc description{};
        description.type                = cudaExternalSemaphoreHandleTypeTimelineSemaphoreWin32;
        description.handle.win32.handle = timeline_semaphore_handle;
        cudaExternalSemaphore_t external_semaphore{};
        check_cuda(cudaImportExternalSemaphore(&external_semaphore, &description), "cudaImportExternalSemaphore");
        return {external_semaphore};
    }

    void destroy_imported_timeline_semaphore(ImportedTimelineSemaphore& timeline_semaphore) noexcept {
        if (timeline_semaphore.cuda_external_semaphore) cudaDestroyExternalSemaphore(static_cast<cudaExternalSemaphore_t>(timeline_semaphore.cuda_external_semaphore));
        timeline_semaphore = {};
    }

    void wait_timeline(void* cuda_stream, const ImportedTimelineSemaphore& timeline_semaphore, const std::uint64_t value) {
        cudaExternalSemaphoreWaitParams parameters{};
        parameters.params.fence.value                    = value;
        const cudaExternalSemaphore_t external_semaphore = static_cast<cudaExternalSemaphore_t>(timeline_semaphore.cuda_external_semaphore);
        check_cuda(cudaWaitExternalSemaphoresAsync(&external_semaphore, &parameters, 1, static_cast<cudaStream_t>(cuda_stream)), "cudaWaitExternalSemaphoresAsync");
    }

    void signal_timeline(void* cuda_stream, const ImportedTimelineSemaphore& timeline_semaphore, const std::uint64_t value) {
        cudaExternalSemaphoreSignalParams parameters{};
        parameters.params.fence.value                    = value;
        const cudaExternalSemaphore_t external_semaphore = static_cast<cudaExternalSemaphore_t>(timeline_semaphore.cuda_external_semaphore);
        check_cuda(cudaSignalExternalSemaphoresAsync(&external_semaphore, &parameters, 1, static_cast<cudaStream_t>(cuda_stream)), "cudaSignalExternalSemaphoresAsync");
    }

    void synchronize_stream(void* cuda_stream) {
        check_cuda(cudaStreamSynchronize(static_cast<cudaStream_t>(cuda_stream)), "cudaStreamSynchronize");
    }

    void pack_float3_buffer(void* cuda_stream, void* destination, const float* x, const float* y, const float* z, const std::uint64_t count) {
        constexpr std::uint32_t block_size = 256;
        pack_float3_kernel<<<static_cast<unsigned>((count + block_size - 1) / block_size), block_size, 0, static_cast<cudaStream_t>(cuda_stream)>>>(static_cast<float3*>(destination), x, y, z, count);
        check_cuda(cudaGetLastError(), "pack_float3_kernel");
    }

    void copy_float_buffer(void* cuda_stream, void* destination, const float* source, const std::uint64_t count) {
        check_cuda(cudaMemcpyAsync(destination, source, count * sizeof(float), cudaMemcpyDeviceToDevice, static_cast<cudaStream_t>(cuda_stream)), "cudaMemcpyAsync DeviceToDevice");
    }
} // namespace xayah::projects::cuda_interop
