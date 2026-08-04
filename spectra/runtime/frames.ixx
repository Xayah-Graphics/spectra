export module spectra.runtime:frames;

import :graphics;
import :platform;
import :resources;
import std;
import vulkan;

namespace spectra {
    export struct GpuUploadSlice {
        vk::Buffer buffer{};
        vk::DeviceSize offset{};
        vk::DeviceSize size{};
    };

    export struct PresentationTarget {
        vk::Image image{};
        vk::ImageView view{};
        vk::Extent2D extent{};
        vk::ImageLayout image_layout{};
    };

    export struct FrameContext {
        std::uint32_t slot_index{};
        const vk::raii::CommandBuffer& command_buffer;
    };

    export struct PresentedFrameContext {
        FrameContext frame;
        PresentationTarget presentation_target{};
    };

    export struct VulkanFrames {
        static constexpr std::uint32_t frames_in_flight = 2;

        VulkanFrames(VulkanGraphics& graphics, GpuResources& resources);

        VulkanFrames(const VulkanFrames&)            = delete;
        VulkanFrames(VulkanFrames&&)                 = delete;
        VulkanFrames& operator=(const VulkanFrames&) = delete;
        VulkanFrames& operator=(VulkanFrames&&)      = delete;

        [[nodiscard]] FrameContext begin_frame();
        [[nodiscard]] std::uint32_t submit_frame();
        void wait_frame(std::uint32_t frame_slot_index) const;
        [[nodiscard]] GpuUploadSlice stage_upload(std::span<const std::byte> data, vk::DeviceSize alignment = 16);
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
            GpuBuffer buffer{};
            std::array<vk::DeviceSize, frames_in_flight> offsets{};
        } uploads;

        struct {
            vk::raii::CommandPool command_pool{nullptr};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Fence> fences{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> submit_waits{};
            std::array<std::vector<vk::SemaphoreSubmitInfo>, frames_in_flight> submit_signals{};
            std::uint32_t current_slot_index{};
        } frame;

        struct {
            std::array<std::vector<std::move_only_function<void()>>, frames_in_flight> destructions{};
            std::array<std::vector<std::uint32_t>, frames_in_flight> resource_indices{};
            std::array<std::vector<std::uint32_t>, frames_in_flight> sampler_indices{};
        } deferred;
    };

    export struct VulkanPresentation {
        VulkanPresentation(WindowPlatform& platform, VulkanGraphics& graphics, VulkanFrames& frames);

        VulkanPresentation(const VulkanPresentation&)            = delete;
        VulkanPresentation(VulkanPresentation&&)                 = delete;
        VulkanPresentation& operator=(const VulkanPresentation&) = delete;
        VulkanPresentation& operator=(VulkanPresentation&&)      = delete;

        [[nodiscard]] std::optional<PresentedFrameContext> begin_frame();
        [[nodiscard]] bool present_frame();

        struct {
            WindowPlatform& platform;
            VulkanGraphics& graphics;
            VulkanFrames& frames;
        } context;

        struct {
            vk::raii::SwapchainKHR swapchain{nullptr};
            std::vector<vk::Image> images{};
            std::vector<vk::raii::ImageView> views{};
            std::vector<vk::ImageLayout> layouts{};
            std::vector<vk::raii::Semaphore> image_available{};
            std::vector<vk::raii::Semaphore> render_finished{};
            vk::Extent2D extent{};
            std::uint32_t acquired_image_index{};
            bool acquired_suboptimal{};
        } presentation;

    private:
        void recreate_swapchain();
    };
} // namespace spectra
