module spectra.runtime;

import std;
import vulkan;

namespace spectra {
    VulkanRuntime::VulkanRuntime(const std::string_view application_name) : graphics(application_name), resources(this->graphics), frames(this->graphics, this->resources) {}

    VulkanRuntime::VulkanRuntime(WindowPlatform& platform, const std::string_view application_name) : graphics(platform, application_name), resources(this->graphics), frames(this->graphics, this->resources) {}

    VulkanRuntime::~VulkanRuntime() {
        this->graphics.device.waitIdle();
    }
} // namespace spectra
