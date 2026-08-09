module spectra.runtime;

import :frames;
import std;
import vulkan;

namespace spectra {
    namespace {
        constexpr vk::DeviceSize upload_frame_size = 64u * 1024u * 1024u;

        [[nodiscard]] constexpr vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }
    } // namespace

    VulkanFrames::VulkanFrames(VulkanGraphics& graphics, GpuResources& resources) : context{graphics, resources} {
        this->uploads.buffer        = resources.create_buffer(upload_frame_size * frames_in_flight, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->frame.command_pool    = vk::raii::CommandPool{graphics.device, vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphics.queue_family_index}};
        this->frame.command_buffers = vk::raii::CommandBuffers{graphics.device, vk::CommandBufferAllocateInfo{*this->frame.command_pool, vk::CommandBufferLevel::ePrimary, frames_in_flight}};
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) this->frame.fences.emplace_back(graphics.device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
    }

    FrameContext VulkanFrames::begin_frame() {
        const vk::raii::Fence& fence = this->frame.fences[this->frame.current_slot_index];
        if (this->context.graphics.device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra frame fence wait failed");
        this->uploads.offsets[this->frame.current_slot_index] = 0;
        this->deferred.destructions[this->frame.current_slot_index].clear();
        for (const std::uint32_t index : this->deferred.resource_indices[this->frame.current_slot_index]) this->context.resources.reclaim_resource_descriptor(index);
        this->deferred.resource_indices[this->frame.current_slot_index].clear();
        for (const std::uint32_t index : this->deferred.sampler_indices[this->frame.current_slot_index]) this->context.resources.reclaim_sampler_descriptor(index);
        this->deferred.sampler_indices[this->frame.current_slot_index].clear();
        this->frame.submit_waits[this->frame.current_slot_index].clear();
        this->frame.submit_signals[this->frame.current_slot_index].clear();

        const vk::raii::CommandBuffer& command_buffer = this->frame.command_buffers[this->frame.current_slot_index];
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        return FrameContext{this->frame.current_slot_index, command_buffer};
    }

    std::uint32_t VulkanFrames::submit_frame() {
        const std::uint32_t submitted_slot_index      = this->frame.current_slot_index;
        const vk::raii::CommandBuffer& command_buffer = this->frame.command_buffers[this->frame.current_slot_index];
        command_buffer.end();
        this->context.graphics.device.resetFences(*this->frame.fences[this->frame.current_slot_index]);
        std::vector<vk::SemaphoreSubmitInfo>& submit_waits = this->frame.submit_waits[this->frame.current_slot_index];
        const vk::CommandBufferSubmitInfo command_info{*command_buffer};
        std::vector<vk::SemaphoreSubmitInfo>& submit_signals = this->frame.submit_signals[this->frame.current_slot_index];
        this->context.graphics.queue.submit2(vk::SubmitInfo2{{}, static_cast<std::uint32_t>(submit_waits.size()), submit_waits.data(), 1, &command_info, static_cast<std::uint32_t>(submit_signals.size()), submit_signals.data()}, *this->frame.fences[this->frame.current_slot_index]);
        this->frame.current_slot_index = (this->frame.current_slot_index + 1u) % frames_in_flight;
        return submitted_slot_index;
    }

    void VulkanFrames::wait_frame(const std::uint32_t frame_slot_index) const {
        if (this->context.graphics.device.waitForFences(*this->frame.fences[frame_slot_index], vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra frame fence wait failed");
    }

    GpuUploadSlice VulkanFrames::stage_upload(const std::span<const std::byte> data, const vk::DeviceSize alignment) {
        const std::uint32_t slot          = this->frame.current_slot_index;
        const vk::DeviceSize local_offset = align_up(this->uploads.offsets[slot], alignment);
        if (local_offset + data.size_bytes() > upload_frame_size) throw std::runtime_error("Spectra per-frame upload ring is exhausted");
        const vk::DeviceSize offset = slot * upload_frame_size + local_offset;
        std::memcpy(static_cast<std::byte*>(this->uploads.buffer.mapped) + offset, data.data(), data.size_bytes());
        this->uploads.offsets[slot] = local_offset + data.size_bytes();
        return {*this->uploads.buffer.buffer, offset, data.size_bytes()};
    }

    void VulkanFrames::defer_destruction(std::move_only_function<void()> destruction) {
        this->deferred.destructions[this->frame.current_slot_index].push_back(std::move(destruction));
    }

    void VulkanFrames::retire_resource_descriptor(const DescriptorHandle handle) noexcept {
        this->deferred.resource_indices[this->frame.current_slot_index].push_back(handle.slot_index);
    }

    void VulkanFrames::retire_sampler_descriptor(const DescriptorHandle handle) noexcept {
        this->deferred.sampler_indices[this->frame.current_slot_index].push_back(handle.slot_index);
    }

    void VulkanFrames::enqueue_external_wait(const GpuExternalTimelineSemaphore& timeline, const std::uint64_t value, const vk::PipelineStageFlags2 stages) {
        this->frame.submit_waits[this->frame.current_slot_index].emplace_back(*timeline.semaphore, value, stages, 0);
    }

    void VulkanFrames::enqueue_external_signal(const GpuExternalTimelineSemaphore& timeline, const std::uint64_t value, const vk::PipelineStageFlags2 stages) {
        this->frame.submit_signals[this->frame.current_slot_index].emplace_back(*timeline.semaphore, value, stages, 0);
    }

    void VulkanFrames::enqueue_wait(const vk::SemaphoreSubmitInfo wait) {
        this->frame.submit_waits[this->frame.current_slot_index].push_back(wait);
    }

    void VulkanFrames::enqueue_signal(const vk::SemaphoreSubmitInfo signal) {
        this->frame.submit_signals[this->frame.current_slot_index].push_back(signal);
    }

} // namespace spectra
