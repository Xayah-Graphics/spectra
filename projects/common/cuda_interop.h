#pragma once

#include <cstddef>
#include <cstdint>
#include <spectra/plugin_api.h>

namespace xayah::projects::cuda_interop {
    struct ImportedBuffer {
        void* cuda_external_memory{};
        void* device_pointer{};
        std::uint64_t byte_size{};
    };

    struct ImportedTimelineSemaphore {
        void* cuda_external_semaphore{};
    };

    void select_matching_device(const std::uint8_t* uuid, const std::uint8_t* luid, std::uint32_t node_mask);
    [[nodiscard]] ImportedBuffer import_buffer(SpectraPluginExternalHandle external_memory_handle, std::uint64_t byte_size);
    void destroy_imported_buffer(ImportedBuffer& buffer) noexcept;
    [[nodiscard]] ImportedTimelineSemaphore import_timeline_semaphore(SpectraPluginExternalHandle timeline_semaphore_handle);
    void destroy_imported_timeline_semaphore(ImportedTimelineSemaphore& timeline_semaphore) noexcept;
    void wait_timeline(void* cuda_stream, const ImportedTimelineSemaphore& timeline_semaphore, std::uint64_t value);
    void signal_timeline(void* cuda_stream, const ImportedTimelineSemaphore& timeline_semaphore, std::uint64_t value);
    void synchronize_stream(void* cuda_stream);
    void pack_float3_buffer(void* cuda_stream, void* destination, const float* x, const float* y, const float* z, std::uint64_t count);
    void copy_float_buffer(void* cuda_stream, void* destination, const float* source, std::uint64_t count);
} // namespace xayah::projects::cuda_interop
