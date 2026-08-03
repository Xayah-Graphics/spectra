module spectra.runtime;

import std;
import vulkan;

namespace spectra {
    VulkanRuntime::VulkanRuntime(const std::string_view application_name, const vk::Extent2D initial_extent) : platform(application_name, initial_extent), graphics(this->platform, application_name), resources(this->graphics), frames(this->platform, this->graphics, this->resources) {}

    VulkanRuntime::~VulkanRuntime() {
        this->graphics.device.waitIdle();
    }
} // namespace spectra
