module spectra.runtime.frames;

import std;
import vulkan;

namespace spectra {
    DescriptorLease::DescriptorLease(VulkanFrames& descriptor_frames, const DescriptorHandle descriptor_value, const DescriptorKind descriptor_kind) noexcept : frames{&descriptor_frames}, value{descriptor_value}, kind{descriptor_kind} {}

    DescriptorLease::~DescriptorLease() {
        this->reset();
    }

    DescriptorLease::DescriptorLease(DescriptorLease&& other) noexcept : frames{std::exchange(other.frames, nullptr)}, value{std::exchange(other.value, {})}, kind{other.kind} {}

    DescriptorLease& DescriptorLease::operator=(DescriptorLease&& other) noexcept {
        if (this == &other) return *this;
        this->reset();
        this->frames = std::exchange(other.frames, nullptr);
        this->value  = std::exchange(other.value, {});
        this->kind   = other.kind;
        return *this;
    }

    void DescriptorLease::reset() noexcept {
        if (!this->value) {
            this->frames = nullptr;
            return;
        }
        if (this->kind == DescriptorKind::Resource)
            this->frames->retire_resource_descriptor(this->value);
        else
            this->frames->retire_sampler_descriptor(this->value);
        this->frames = nullptr;
        this->value  = {};
    }

    namespace {
        constexpr vk::DeviceSize upload_frame_size = 64u * 1024u * 1024u;

        [[nodiscard]] constexpr vk::DeviceSize align_up(const vk::DeviceSize value, const vk::DeviceSize alignment) noexcept {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }
    } // namespace

    VulkanFrames::VulkanFrames(VulkanGraphics& graphics, GpuResources& resources) : context{graphics, resources} {
        for (std::vector<GpuBuffer>& buffers : this->uploads.buffers) buffers.emplace_back(resources.create_buffer(upload_frame_size, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true));
        this->frame.command_pool    = vk::raii::CommandPool{graphics.device, vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphics.queue_family_index}};
        this->frame.command_buffers = vk::raii::CommandBuffers{graphics.device, vk::CommandBufferAllocateInfo{*this->frame.command_pool, vk::CommandBufferLevel::ePrimary, frames_in_flight}};
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) this->frame.fences.emplace_back(graphics.device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
    }

    VulkanFrames::~VulkanFrames() {
        while (!this->deferred.destructions.empty()) {
            std::move_only_function<void()> callback = std::move(this->deferred.destructions.back().callback);
            this->deferred.destructions.pop_back();
            callback();
        }
        for (const auto descriptor : this->deferred.resource_indices) this->context.resources.reclaim_resource_descriptor(descriptor.index);
        for (const auto descriptor : this->deferred.sampler_indices) this->context.resources.reclaim_sampler_descriptor(descriptor.index);
        this->deferred.resource_indices.clear();
        this->deferred.sampler_indices.clear();
    }

    FrameContext VulkanFrames::begin_frame() {
        const vk::raii::Fence& fence = this->frame.fences[this->frame.current_slot_index];
        if (this->context.graphics.device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra frame fence wait failed");
        this->frame.completed_serial = std::max(this->frame.completed_serial, this->frame.submitted_serials[this->frame.current_slot_index]);
        this->uploads.offsets[this->frame.current_slot_index] = 0;
        this->uploads.current_buffers[this->frame.current_slot_index] = 0;
        std::erase_if(this->deferred.destructions, [this](auto& destruction) {
            if (destruction.serial > this->frame.completed_serial) return false;
            destruction.callback();
            return true;
        });
        std::erase_if(this->deferred.resource_indices, [this](const auto descriptor) {
            if (descriptor.serial > this->frame.completed_serial) return false;
            this->context.resources.reclaim_resource_descriptor(descriptor.index);
            return true;
        });
        std::erase_if(this->deferred.sampler_indices, [this](const auto descriptor) {
            if (descriptor.serial > this->frame.completed_serial) return false;
            this->context.resources.reclaim_sampler_descriptor(descriptor.index);
            return true;
        });
        this->frame.submit_waits[this->frame.current_slot_index].clear();
        this->frame.submit_signals[this->frame.current_slot_index].clear();

        const vk::raii::CommandBuffer& command_buffer = this->frame.command_buffers[this->frame.current_slot_index];
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        this->frame.recording = true;
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
        this->frame.submitted_serials[this->frame.current_slot_index] = this->frame.next_submission_serial++;
        this->frame.recording = false;
        this->frame.current_slot_index = (this->frame.current_slot_index + 1u) % frames_in_flight;
        return submitted_slot_index;
    }

    void VulkanFrames::wait_frame(const std::uint32_t frame_slot_index) const {
        if (this->context.graphics.device.waitForFences(*this->frame.fences[frame_slot_index], vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra frame fence wait failed");
    }

    GpuUploadSlice VulkanFrames::stage_upload(const std::span<const std::byte> data, const vk::DeviceSize alignment) {
        const std::uint32_t slot = this->frame.current_slot_index;
        vk::DeviceSize local_offset = align_up(this->uploads.offsets[slot], alignment);
        std::vector<GpuBuffer>& buffers = this->uploads.buffers[slot];
        if (local_offset + data.size_bytes() > buffers[this->uploads.current_buffers[slot]].size) {
            ++this->uploads.current_buffers[slot];
            this->uploads.offsets[slot] = 0;
            local_offset = 0;
            if (this->uploads.current_buffers[slot] == buffers.size()) buffers.emplace_back(this->context.resources.create_buffer(std::max<vk::DeviceSize>(upload_frame_size, align_up(data.size_bytes(), 64u * 1024u)), vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true));
        }
        GpuBuffer& buffer = buffers[this->uploads.current_buffers[slot]];
        std::memcpy(static_cast<std::byte*>(buffer.mapped) + local_offset, data.data(), data.size_bytes());
        this->uploads.offsets[slot] = local_offset + data.size_bytes();
        return {*buffer.buffer, local_offset, data.size_bytes()};
    }

    DescriptorLease VulkanFrames::allocate_resource_descriptor() {
        return DescriptorLease{*this, this->context.resources.acquire_resource_descriptor(), DescriptorKind::Resource};
    }

    DescriptorLease VulkanFrames::allocate_sampler_descriptor() {
        return DescriptorLease{*this, this->context.resources.acquire_sampler_descriptor(), DescriptorKind::Sampler};
    }

    void VulkanFrames::defer_destruction(std::move_only_function<void()> destruction) {
        const std::uint64_t serial = this->frame.recording ? this->frame.next_submission_serial : this->frame.next_submission_serial - 1u;
        this->deferred.destructions.emplace_back(serial, std::move(destruction));
    }

    void VulkanFrames::retire_resource_descriptor(const DescriptorHandle handle) noexcept {
        if (!handle) return;
        const std::uint64_t serial = this->frame.recording ? this->frame.next_submission_serial : this->frame.next_submission_serial - 1u;
        this->deferred.resource_indices.emplace_back(serial, handle.slot_index);
    }

    void VulkanFrames::retire_sampler_descriptor(const DescriptorHandle handle) noexcept {
        if (!handle) return;
        const std::uint64_t serial = this->frame.recording ? this->frame.next_submission_serial : this->frame.next_submission_serial - 1u;
        this->deferred.sampler_indices.emplace_back(serial, handle.slot_index);
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
