module;

#include <Windows.h>

#include <GLFW/glfw3.h>

module spectra.editor.platform.presentation;

import std;
import vulkan;

namespace spectra {
    VulkanSurface::VulkanSurface(WindowPlatform& platform, VulkanInstance& instance) : surface(instance.instance, vk::Win32SurfaceCreateInfoKHR{{}, GetModuleHandleW(nullptr), platform.native_window}) {}

    VulkanPresentation::VulkanPresentation(WindowPlatform& platform, VulkanSurface& surface, VulkanGraphics& graphics, VulkanFrames& frames) : context{platform, surface, graphics, frames} {
        for (std::uint32_t index = 0; index < VulkanFrames::frames_in_flight; ++index) this->presentation.image_available.emplace_back(graphics.device, vk::SemaphoreCreateInfo{});
        this->recreate_swapchain();
    }

    VulkanPresentation::~VulkanPresentation() {
        this->context.frames.defer_destruction([swapchain = std::move(this->presentation.swapchain), views = std::move(this->presentation.views), image_available = std::move(this->presentation.image_available), render_finished = std::move(this->presentation.render_finished)]() mutable { views.clear(); });
    }

    void VulkanPresentation::recreate_swapchain() {
        this->context.graphics.device.waitIdle();
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) {
            this->presentation.extent = vk::Extent2D{};
            return;
        }
        const vk::SurfaceKHR surface                        = *this->context.surface.surface;
        const vk::SurfaceCapabilitiesKHR capabilities       = this->context.graphics.physical_device.getSurfaceCapabilitiesKHR(surface);
        const std::vector<vk::SurfaceFormatKHR> formats     = this->context.graphics.physical_device.getSurfaceFormatsKHR(surface);
        const std::vector<vk::PresentModeKHR> present_modes = this->context.graphics.physical_device.getSurfacePresentModesKHR(surface);
        constexpr vk::SurfaceFormatKHR required_format{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear};
        if (!std::ranges::contains(formats, required_format)) throw std::runtime_error("Spectra requires a B8G8R8A8 sRGB swapchain");
        if (!std::ranges::contains(present_modes, vk::PresentModeKHR::eMailbox)) throw std::runtime_error("Spectra requires mailbox presentation");
        if (!static_cast<bool>(capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity)) throw std::runtime_error("Spectra requires identity surface transforms");
        if (!static_cast<bool>(capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)) throw std::runtime_error("Spectra requires opaque swapchain composition");
        if (capabilities.minImageCount > 3 || (capabilities.maxImageCount != 0 && capabilities.maxImageCount < 3)) throw std::runtime_error("Spectra requires triple-buffered presentation");

        this->presentation.extent            = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        const vk::SwapchainKHR old_swapchain = *this->presentation.swapchain;
        const vk::SwapchainCreateInfoKHR create_info{{}, surface, 3, required_format.format, required_format.colorSpace, this->presentation.extent, 1, vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, 0, nullptr, vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eMailbox, vk::True, old_swapchain};
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

    std::optional<PresentedFrameContext> VulkanPresentation::begin_frame() {
        int width{};
        int height{};
        glfwGetFramebufferSize(this->context.platform.window, &width, &height);
        if (width == 0 || height == 0) return std::nullopt;
        const vk::Extent2D framebuffer_extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        if (this->presentation.extent != framebuffer_extent) this->recreate_swapchain();

        const std::uint32_t frame_slot_index = this->context.frames.frame.current_slot_index;
        vk::ResultValue<std::uint32_t> acquired{vk::Result::eSuccess, 0};
        try {
            acquired = this->presentation.swapchain.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->presentation.image_available[frame_slot_index]);
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return std::nullopt;
        }
        const FrameContext frame                = this->context.frames.begin_frame();
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
        vk::Result present_result{};
        try {
            present_result = this->context.graphics.queue.presentKHR(vk::PresentInfoKHR{1, &render_finished, 1, &swapchain, &this->presentation.acquired_image_index});
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return;
        }
        if (this->presentation.acquired_suboptimal || present_result == vk::Result::eSuboptimalKHR) this->recreate_swapchain();
    }
} // namespace spectra
