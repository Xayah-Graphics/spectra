module spectra.runtime;

import std;
import vulkan;

namespace spectra {
    VulkanRuntime::VulkanRuntime(VulkanInstance& instance, const vk::SurfaceKHR surface) : graphics(instance, surface), resources(this->graphics), frames(this->graphics, this->resources) {}

    VulkanRuntime::~VulkanRuntime() {
        this->graphics.device.waitIdle();
    }
} // namespace spectra
