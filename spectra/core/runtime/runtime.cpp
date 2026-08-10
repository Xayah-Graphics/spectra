module spectra.runtime;

import std;
import vulkan;

namespace spectra {
    VulkanRuntime::VulkanRuntime(VulkanInstance& instance, const vk::SurfaceKHR surface) : graphics(instance, surface), resources(this->graphics), frames(this->graphics, this->resources) {}

    VulkanRuntime::~VulkanRuntime() {
        static_cast<void>(this->graphics.device.getDispatcher()->vkDeviceWaitIdle(*this->graphics.device));
    }
} // namespace spectra
