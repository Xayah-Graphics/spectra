export module spectra.runtime.frames;

import spectra.runtime.graphics;
import spectra.runtime.resources;
import std;
import vulkan;

namespace spectra {
    enum class DescriptorKind : std::uint8_t {
        Resource,
        Sampler,
    };

    export struct VulkanFrames;

    export struct DescriptorLease {
        DescriptorLease() = default;
        ~DescriptorLease();
        DescriptorLease(DescriptorLease&& other) noexcept;
        DescriptorLease& operator=(DescriptorLease&& other) noexcept;
        DescriptorLease(const DescriptorLease&)            = delete;
        DescriptorLease& operator=(const DescriptorLease&) = delete;

        [[nodiscard]] operator DescriptorHandle() const noexcept {
            return this->value;
        }

        [[nodiscard]] explicit operator bool() const noexcept {
            return static_cast<bool>(this->value);
        }

        [[nodiscard]] DescriptorHandle handle() const noexcept {
            return this->value;
        }

        void reset() noexcept;

    private:
        friend VulkanFrames;

        DescriptorLease(VulkanFrames& frames, DescriptorHandle value, DescriptorKind kind) noexcept;

        VulkanFrames* frames{};
        DescriptorHandle value{};
        DescriptorKind kind{DescriptorKind::Resource};
    };

    export struct GpuUploadSlice {
        vk::Buffer buffer{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct FrameContext {
        std::uint32_t slot_index{};
        const vk::raii::CommandBuffer& command_buffer;
    };

    export struct VulkanFrames {
        static constexpr std::uint32_t frames_in_flight = 2;

        VulkanFrames(VulkanGraphics& graphics, GpuResources& resources);
        ~VulkanFrames();

        VulkanFrames(const VulkanFrames&)            = delete;
        VulkanFrames(VulkanFrames&&)                 = delete;
        VulkanFrames& operator=(const VulkanFrames&) = delete;
        VulkanFrames& operator=(VulkanFrames&&)      = delete;

        [[nodiscard]] FrameContext begin_frame();
        [[nodiscard]] std::uint32_t submit_frame();
        void wait_frame(std::uint32_t frame_slot_index) const;
        [[nodiscard]] GpuUploadSlice stage_upload(std::span<const std::byte> data, vk::DeviceSize alignment = 16);
        [[nodiscard]] DescriptorLease allocate_resource_descriptor();
        [[nodiscard]] DescriptorLease allocate_sampler_descriptor();
        void defer_destruction(std::move_only_function<void()> destruction);
        void retire_resource_descriptor(DescriptorHandle handle) noexcept;
        void retire_sampler_descriptor(DescriptorHandle handle) noexcept;
        void enqueue_external_wait(const GpuExternalTimelineSemaphore& timeline, std::uint64_t value, vk::PipelineStageFlags2 stages);
        void enqueue_external_signal(const GpuExternalTimelineSemaphore& timeline, std::uint64_t value, vk::PipelineStageFlags2 stages);
        void enqueue_wait(vk::SemaphoreSubmitInfo wait);
        void enqueue_signal(vk::SemaphoreSubmitInfo signal);

        struct {
            VulkanGraphics& graphics;
            GpuResources& resources;
        } context;

        struct {
            std::array<std::vector<GpuBuffer>, frames_in_flight> buffers{};
            std::array<std::size_t, frames_in_flight> current_buffers{};
            std::array<vk::DeviceSize, frames_in_flight> offsets{};
        } uploads;

        struct {
            vk::raii::CommandPool command_pool{nullptr};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Fence> fences{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> submit_waits{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> submit_signals{};
            std::array<std::uint64_t, frames_in_flight> submitted_serials{};
            std::uint64_t next_submission_serial{1};
            std::uint64_t completed_serial{};
            std::uint32_t current_slot_index{};
            bool recording{};
        } frame;

        struct {
            struct Destruction {
                std::uint64_t serial{};
                std::move_only_function<void()> callback{};
            };
            struct Descriptor {
                std::uint64_t serial{};
                std::uint32_t index{};
            };
            std::vector<Destruction> destructions{};
            std::vector<Descriptor> resource_indices{};
            std::vector<Descriptor> sampler_indices{};
        } deferred;
    };
} // namespace spectra
