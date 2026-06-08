module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer.host;

import std;

namespace spectra::rasterizer {
    const vk::raii::PhysicalDevice& HostView::physical_device() {
        return this->physicalDeviceCallback();
    }

    const vk::raii::Device& HostView::device() {
        return this->deviceCallback();
    }

    std::uint32_t HostView::frame_count() {
        return this->frameCountCallback();
    }

    vk::Extent2D HostView::swapchain_extent() {
        return this->swapchainExtentCallback();
    }

    void HostView::register_panel(Panel panel) {
        this->registerPanelCallback(std::move(panel));
    }

    void HostView::register_sidebar_tab(SidebarTab tab) {
        this->registerSidebarTabCallback(std::move(tab));
    }

    void HostView::register_toolbar_action(ToolbarAction action) {
        this->registerToolbarActionCallback(std::move(action));
    }
} // namespace spectra::rasterizer
