module spectra.scene_runtime;

import std;
import spectra.scene;

namespace spectra::scene_runtime {
    void DynamicSceneHostServiceRouter::set_viewport_voxel_buffer_backend(std::move_only_function<DynamicSceneViewportVoxelBufferAllocation(const DynamicSceneViewportVoxelBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback) {
        if (!request_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer request callback must not be empty");
        if (!release_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer release callback must not be empty");
        this->request_viewport_voxel_buffer_callback = std::move(request_callback);
        this->release_viewport_voxel_buffer_callback = std::move(release_callback);
        this->last_error_message.clear();
    }

    void DynamicSceneHostServiceRouter::clear_viewport_voxel_buffer_backend() noexcept {
        this->request_viewport_voxel_buffer_callback = nullptr;
        this->release_viewport_voxel_buffer_callback = nullptr;
        this->last_error_message.clear();
    }

    void DynamicSceneHostServiceRouter::set_volume_buffer_backend(std::move_only_function<DynamicSceneVolumeBufferAllocation(const DynamicSceneVolumeBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback) {
        if (!request_callback) throw std::runtime_error("Dynamic scene volume buffer request callback must not be empty");
        if (!release_callback) throw std::runtime_error("Dynamic scene volume buffer release callback must not be empty");
        this->request_volume_buffer_callback = std::move(request_callback);
        this->release_volume_buffer_callback = std::move(release_callback);
        this->last_error_message.clear();
    }

    void DynamicSceneHostServiceRouter::clear_volume_buffer_backend() noexcept {
        this->request_volume_buffer_callback = nullptr;
        this->release_volume_buffer_callback = nullptr;
        this->volume_buffer_allocations.clear();
        this->last_error_message.clear();
    }

    DynamicSceneViewportVoxelBufferAllocation DynamicSceneHostServiceRouter::request_viewport_voxel_buffer(const DynamicSceneViewportVoxelBufferRequest& request) {
        try {
            if (!this->request_viewport_voxel_buffer_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer backend is not available");
            this->last_error_message.clear();
            return this->request_viewport_voxel_buffer_callback(request);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    void DynamicSceneHostServiceRouter::release_viewport_voxel_buffer(const std::uint64_t resource_id) {
        try {
            if (!this->release_viewport_voxel_buffer_callback) throw std::runtime_error("Dynamic scene viewport voxel buffer backend is not available");
            this->last_error_message.clear();
            this->release_viewport_voxel_buffer_callback(resource_id);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    DynamicSceneVolumeBufferAllocation DynamicSceneHostServiceRouter::request_volume_buffer(const DynamicSceneVolumeBufferRequest& request) {
        try {
            if (!this->request_volume_buffer_callback) throw std::runtime_error("Dynamic scene volume buffer backend is not available");
            this->last_error_message.clear();
            DynamicSceneVolumeBufferAllocation allocation = this->request_volume_buffer_callback(request);
            if (allocation.resource_id == 0u) throw std::runtime_error("Dynamic scene volume buffer backend returned a zero resource id");
            if (allocation.byte_size == 0u) throw std::runtime_error("Dynamic scene volume buffer backend returned a zero byte size");
            if (!this->volume_buffer_allocations.emplace(allocation.resource_id, allocation).second) throw std::runtime_error(std::format("Dynamic scene volume buffer resource {} already exists", allocation.resource_id));
            return allocation;
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    void DynamicSceneHostServiceRouter::release_volume_buffer(const std::uint64_t resource_id) {
        try {
            if (!this->release_volume_buffer_callback) throw std::runtime_error("Dynamic scene volume buffer backend is not available");
            const std::map<std::uint64_t, DynamicSceneVolumeBufferAllocation>::iterator found = this->volume_buffer_allocations.find(resource_id);
            if (found == this->volume_buffer_allocations.end()) throw std::runtime_error(std::format("Dynamic scene volume buffer resource {} does not exist", resource_id));
            this->last_error_message.clear();
            this->release_volume_buffer_callback(resource_id);
            this->volume_buffer_allocations.erase(found);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    std::string_view DynamicSceneHostServiceRouter::last_error() const {
        return this->last_error_message;
    }
} // namespace spectra::scene_runtime
