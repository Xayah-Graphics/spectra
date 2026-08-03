module;

#include <GLFW/glfw3.h>

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

    VulkanFrames::VulkanFrames(WindowPlatform& platform, VulkanGraphics& graphics, GpuResources& resources) : context{platform, graphics, resources} {
        this->uploads.buffer        = resources.create_buffer(upload_frame_size * frames_in_flight, vk::BufferUsageFlagBits::eTransferSrc, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, true);
        this->frame.command_pool    = vk::raii::CommandPool{graphics.device, vk::CommandPoolCreateInfo{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, graphics.queue_family_index}};
        this->frame.command_buffers = vk::raii::CommandBuffers{graphics.device, vk::CommandBufferAllocateInfo{*this->frame.command_pool, vk::CommandBufferLevel::ePrimary, frames_in_flight}};
        for (std::uint32_t index = 0; index < frames_in_flight; ++index) {
            this->frame.image_available.emplace_back(graphics.device, vk::SemaphoreCreateInfo{});
            this->frame.fences.emplace_back(graphics.device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
        }
        this->recreate_swapchain();
    }

    void VulkanFrames::recreate_swapchain() {
        this->context.graphics.device.waitIdle();
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) {
            this->presentation.extent = vk::Extent2D{};
            return;
        }
        const vk::SurfaceCapabilitiesKHR capabilities       = this->context.graphics.physical_device.getSurfaceCapabilitiesKHR(*this->context.graphics.instance.surface);
        const std::vector<vk::SurfaceFormatKHR> formats     = this->context.graphics.physical_device.getSurfaceFormatsKHR(*this->context.graphics.instance.surface);
        const std::vector<vk::PresentModeKHR> present_modes = this->context.graphics.physical_device.getSurfacePresentModesKHR(*this->context.graphics.instance.surface);
        constexpr vk::SurfaceFormatKHR required_format{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
        if (!std::ranges::contains(formats, required_format)) throw std::runtime_error("Spectra requires a B8G8R8A8 sRGB swapchain");
        if (!std::ranges::contains(present_modes, vk::PresentModeKHR::eMailbox)) throw std::runtime_error("Spectra requires mailbox presentation");
        if (!static_cast<bool>(capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)) throw std::runtime_error("Spectra requires identity surface transforms");
        if (!static_cast<bool>(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) throw std::runtime_error("Spectra requires opaque swapchain composition");
        if (capabilities.minImageCount > 3 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < 3)) throw std::runtime_error("Spectra requires triple-buffered presentation");

        this->presentation.extent            = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        const vk::SwapchainKHR old_swapchain = *this->presentation.swapchain;
        const vk::SwapchainCreateInfoKHR create_info{{}, *this->context.graphics.instance.surface, 3, required_format.format, required_format.colorSpace, this->presentation.extent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, 0, nullptr, vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eMailbox, vk::True, old_swapchain};
        vk::raii::SwapchainKHR replacement{this->context.graphics.device, create_info};
        this->presentation.views.clear();
        this->presentation.swapchain = std::move(replacement);
        this->presentation.images    = this->presentation.swapchain.getImages();
        this->presentation.layouts.assign(this->presentation.images.size(), vk::ImageLayout::eUndefined);
        this->presentation.render_finished.clear();
        this->presentation.render_finished.reserve(this->presentation.images.size());
        for (const vk::Image image : this->presentation.images) {
            this->presentation.views.emplace_back(this->context.graphics.device, vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, required_format.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            this->presentation.render_finished.emplace_back(this->context.graphics.device, vk::SemaphoreCreateInfo{});
        }
    }

    std::optional<FrameContext> VulkanFrames::begin_frame() {
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) return std::nullopt;
        const vk::Extent2D framebuffer_extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        if (this->presentation.extent != framebuffer_extent) this->recreate_swapchain();

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

        vk::ResultValue<std::uint32_t> acquired{vk::Result::eSuccess, 0};
        try {
            acquired = this->presentation.swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->frame.image_available[this->frame.current_slot_index]);
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return std::nullopt;
        }
        this->presentation.acquired_image_index = acquired.value;
        this->presentation.acquired_suboptimal  = acquired.result == vk::Result::eSuboptimalKHR;
        this->context.graphics.device.resetFences(*fence);
        const vk::raii::CommandBuffer& command_buffer = this->frame.command_buffers[this->frame.current_slot_index];
        command_buffer.reset();
        command_buffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        return FrameContext{this->frame.current_slot_index, command_buffer, {this->presentation.images[acquired.value], *this->presentation.views[acquired.value], this->presentation.extent, this->presentation.layouts[acquired.value]}};
    }

    bool VulkanFrames::present_frame() {
        const vk::raii::CommandBuffer& command_buffer = this->frame.command_buffers[this->frame.current_slot_index];
        command_buffer.end();
        this->presentation.layouts[this->presentation.acquired_image_index] = vk::ImageLayout::ePresentSrcKHR;
        std::vector<vk::SemaphoreSubmitInfo>& submit_waits                  = this->frame.submit_waits[this->frame.current_slot_index];
        submit_waits.insert(submit_waits.begin(), vk::SemaphoreSubmitInfo{*this->frame.image_available[this->frame.current_slot_index], 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput, 0});
        const vk::CommandBufferSubmitInfo command_info{*command_buffer};
        std::vector<vk::SemaphoreSubmitInfo>& submit_signals = this->frame.submit_signals[this->frame.current_slot_index];
        submit_signals.emplace_back(*this->presentation.render_finished[this->presentation.acquired_image_index], 0, vk::PipelineStageFlagBits2::eAllCommands, 0);
        this->context.graphics.queue.submit2(vk::SubmitInfo2{{}, static_cast<std::uint32_t>(submit_waits.size()), submit_waits.data(), 1, &command_info, static_cast<std::uint32_t>(submit_signals.size()), submit_signals.data()}, *this->frame.fences[this->frame.current_slot_index]);

        const vk::Semaphore render_finished = *this->presentation.render_finished[this->presentation.acquired_image_index];
        const vk::SwapchainKHR swapchain    = *this->presentation.swapchain;
        vk::Result present_result{};
        try {
            present_result = this->context.graphics.queue.presentKHR(vk::PresentInfoKHR{1, &render_finished, 1, &swapchain, &this->presentation.acquired_image_index});
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }
        if (this->presentation.acquired_suboptimal || present_result == vk::Result::eSuboptimalKHR) this->recreate_swapchain();
        this->frame.current_slot_index = (this->frame.current_slot_index + 1u) % frames_in_flight;
        return true;
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
} // namespace spectra
