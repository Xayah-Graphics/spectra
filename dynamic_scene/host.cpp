module spectra.dynamic_scene.host;

import std;

namespace spectra::dynamic_scene {
    void HostServiceRouter::set_gpu_buffer_backend(std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback) {
        if (!request_callback) throw std::runtime_error("Dynamic scene GPU buffer request callback must not be empty");
        if (!release_callback) throw std::runtime_error("Dynamic scene GPU buffer release callback must not be empty");
        this->request_gpu_buffer_callback = std::move(request_callback);
        this->release_gpu_buffer_callback = std::move(release_callback);
        this->last_error_message.clear();
    }

    void HostServiceRouter::clear_gpu_buffer_backend() noexcept {
        this->request_gpu_buffer_callback = nullptr;
        this->release_gpu_buffer_callback = nullptr;
        this->gpu_buffer_allocations.clear();
        this->last_error_message.clear();
    }

    GpuBufferAllocation HostServiceRouter::request_gpu_buffer(const GpuBufferRequest& request) {
        try {
            if (!this->request_gpu_buffer_callback) throw std::runtime_error("Dynamic scene GPU buffer backend is not available");
            this->last_error_message.clear();
            GpuBufferAllocation allocation = this->request_gpu_buffer_callback(request);
            if (allocation.resource_id == 0u) throw std::runtime_error("Dynamic scene GPU buffer backend returned a zero resource id");
            if (allocation.byte_size == 0u) throw std::runtime_error("Dynamic scene GPU buffer backend returned a zero byte size");
            if (allocation.kind != request.kind) throw std::runtime_error(std::format("Dynamic scene GPU buffer backend returned kind {} for request kind {}", allocation.kind, request.kind));
            if (!this->gpu_buffer_allocations.emplace(allocation.resource_id, allocation).second) throw std::runtime_error(std::format("Dynamic scene GPU buffer resource {} already exists", allocation.resource_id));
            return allocation;
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    void HostServiceRouter::release_gpu_buffer(const std::uint64_t resource_id) {
        try {
            if (!this->release_gpu_buffer_callback) throw std::runtime_error("Dynamic scene GPU buffer backend is not available");
            const std::map<std::uint64_t, GpuBufferAllocation>::iterator found = this->gpu_buffer_allocations.find(resource_id);
            if (found == this->gpu_buffer_allocations.end()) throw std::runtime_error(std::format("Dynamic scene GPU buffer resource {} does not exist", resource_id));
            this->last_error_message.clear();
            this->release_gpu_buffer_callback(resource_id);
            this->gpu_buffer_allocations.erase(found);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    std::string_view HostServiceRouter::last_error() const {
        return this->last_error_message;
    }
} // namespace spectra::dynamic_scene
