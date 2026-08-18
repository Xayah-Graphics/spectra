module;

#include <Windows.h>

#include <GLFW/glfw3.h>

module spectra.editor.platform.presentation;

import std;
import vulkan;

namespace spectra::editor {
    VulkanSurface::VulkanSurface(WindowPlatform& platform, runtime::VulkanInstance& instance) : surface(instance.instance, vk::Win32SurfaceCreateInfoKHR{{}, GetModuleHandleW(nullptr), platform.native_window}) {}

    VulkanPresentation::VulkanPresentation(WindowPlatform& platform, VulkanSurface& surface, runtime::VulkanDevice& device, runtime::VulkanFrames& frames) : context{platform, surface, device, frames} {
        for (std::uint32_t index = 0; index < runtime::VulkanFrames::frames_in_flight; ++index) this->presentation.image_available.emplace_back(device.logical, vk::SemaphoreCreateInfo{});
        this->recreate_swapchain();
    }

    VulkanPresentation::~VulkanPresentation() {
        this->context.device.logical.waitIdle();
        this->wait_presentations();
    }

    std::optional<PresentedFrameContext> VulkanPresentation::begin_frame() {
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) return std::nullopt;
        const vk::Extent2D framebuffer_extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        if (this->presentation.extent != framebuffer_extent) this->recreate_swapchain();

        const std::uint32_t frame_slot_index = this->context.frames.retire_frame();
        vk::ResultValue<std::uint32_t> acquired{vk::Result::eSuccess, 0};
        try {
            acquired = this->presentation.swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->presentation.image_available[frame_slot_index]);
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return std::nullopt;
        }
        if (this->presentation.present_pending[acquired.value]) {
            if (this->context.device.logical.waitForFences(*this->presentation.present_fences[acquired.value], vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra presentation fence wait failed");
            this->presentation.present_pending[acquired.value] = false;
        }
        const runtime::FrameContext frame       = this->context.frames.begin_frame();
        this->presentation.acquired_image_index = acquired.value;
        this->presentation.acquired_suboptimal  = acquired.result == vk::Result::eSuboptimalKHR;
        this->context.frames.enqueue_wait(vk::SemaphoreSubmitInfo{*this->presentation.image_available[frame.slot_index], 0, vk::PipelineStageFlagBits2::eColorAttachmentOutput, 0});
        return PresentedFrameContext{frame, {this->presentation.images[acquired.value], *this->presentation.views[acquired.value], this->presentation.extent, this->presentation.layouts[acquired.value]}};
    }

    void VulkanPresentation::present_frame() {
        this->presentation.layouts[this->presentation.acquired_image_index] = vk::ImageLayout::ePresentSrcKHR;
        this->context.frames.enqueue_signal(vk::SemaphoreSubmitInfo{*this->presentation.render_finished[this->presentation.acquired_image_index], 0, vk::PipelineStageFlagBits2::eAllCommands, 0});
        (void) this->context.frames.submit_frame();

        const vk::Semaphore render_finished = *this->presentation.render_finished[this->presentation.acquired_image_index];
        const vk::SwapchainKHR swapchain    = *this->presentation.swapchain;
        const vk::Fence present_fence       = *this->presentation.present_fences[this->presentation.acquired_image_index];
        this->context.device.logical.resetFences(present_fence);
        const vk::SwapchainPresentFenceInfoKHR present_fence_info{1, &present_fence};
        const vk::PresentInfoKHR present_info{1, &render_finished, 1, &swapchain, &this->presentation.acquired_image_index, nullptr, &present_fence_info};
        const vk::Result present_result                                             = static_cast<vk::Result>(this->context.device.queue.getDispatcher()->vkQueuePresentKHR(static_cast<VkQueue>(*this->context.device.queue), reinterpret_cast<const VkPresentInfoKHR*>(&present_info)));
        const bool presentation_enqueued                                            = present_result == vk::Result::eSuccess || present_result == vk::Result::eSuboptimalKHR || present_result == vk::Result::eErrorOutOfDateKHR || present_result == vk::Result::eErrorSurfaceLostKHR || present_result == vk::Result::eErrorFullScreenExclusiveModeLostEXT || present_result == vk::Result::eErrorPresentTimingQueueFullEXT;
        this->presentation.present_pending[this->presentation.acquired_image_index] = presentation_enqueued;
        if (present_result == vk::Result::eErrorOutOfDateKHR) {
            this->recreate_swapchain();
            return;
        }
        if (present_result != vk::Result::eSuccess && present_result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::format("Spectra presentation failed: {}", vk::to_string(present_result)));
        if (this->presentation.acquired_suboptimal || present_result == vk::Result::eSuboptimalKHR) this->recreate_swapchain();
    }
    void VulkanPresentation::recreate_swapchain() {
        this->context.device.logical.waitIdle();
        this->wait_presentations();
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) {
            this->presentation.extent = vk::Extent2D{};
            return;
        }
        const vk::SurfaceKHR surface                        = *this->context.surface.surface;
        const vk::SurfaceCapabilitiesKHR capabilities       = this->context.device.physical_device.getSurfaceCapabilitiesKHR(surface);
        const std::vector<vk::SurfaceFormatKHR> formats     = this->context.device.physical_device.getSurfaceFormatsKHR(surface);
        const std::vector<vk::PresentModeKHR> present_modes = this->context.device.physical_device.getSurfacePresentModesKHR(surface);
        constexpr vk::SurfaceFormatKHR required_format{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
        if (!std::ranges::contains(formats, required_format)) throw std::runtime_error("Spectra requires a B8G8R8A8 sRGB swapchain");
        if (!std::ranges::contains(present_modes, vk::PresentModeKHR::eMailbox)) throw std::runtime_error("Spectra requires mailbox presentation");
        if (!static_cast<bool>(capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)) throw std::runtime_error("Spectra requires identity surface transforms");
        if (!static_cast<bool>(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) throw std::runtime_error("Spectra requires opaque swapchain composition");
        if (capabilities.minImageCount > 3 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < 3)) throw std::runtime_error("Spectra requires triple-buffered presentation");

        this->presentation.extent            = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        const vk::SwapchainKHR old_swapchain = *this->presentation.swapchain;
        const vk::SwapchainCreateInfoKHR create_info{{}, surface, 3, required_format.format, required_format.colorSpace, this->presentation.extent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, 0, nullptr, vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eMailbox, vk::True, old_swapchain};
        vk::raii::SwapchainKHR replacement{this->context.device.logical, create_info};
        this->presentation.views.clear();
        this->presentation.swapchain = std::move(replacement);
        this->presentation.images    = this->presentation.swapchain.getImages();
        this->presentation.layouts.assign(this->presentation.images.size(), vk::ImageLayout::eUndefined);
        this->presentation.render_finished.clear();
        this->presentation.present_fences.clear();
        this->presentation.present_pending.assign(this->presentation.images.size(), false);
        this->presentation.render_finished.reserve(this->presentation.images.size());
        this->presentation.present_fences.reserve(this->presentation.images.size());
        for (const vk::Image image : this->presentation.images) {
            this->presentation.views.emplace_back(this->context.device.logical, vk::ImageViewCreateInfo{{}, image, vk::ImageViewType::e2D, required_format.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}});
            this->presentation.render_finished.emplace_back(this->context.device.logical, vk::SemaphoreCreateInfo{});
            this->presentation.present_fences.emplace_back(this->context.device.logical, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled});
        }
    }

    void VulkanPresentation::wait_presentations() {
        for (std::uint32_t index = 0; index < this->presentation.present_pending.size(); ++index) {
            if (!this->presentation.present_pending[index]) continue;
            if (this->context.device.logical.waitForFences(*this->presentation.present_fences[index], vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Spectra presentation fence wait failed");
            this->presentation.present_pending[index] = false;
        }
    }

} // namespace spectra::editor
