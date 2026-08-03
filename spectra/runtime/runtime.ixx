export module spectra.runtime;

export import :platform;
export import :graphics;
export import :resources;
export import :frames;

import std;
import vulkan;

namespace spectra {
    export struct VulkanRuntime {
        explicit VulkanRuntime(std::string_view application_name = "Spectra", vk::Extent2D initial_extent = {1920, 1080});
        ~VulkanRuntime();

        VulkanRuntime(const VulkanRuntime&)            = delete;
        VulkanRuntime(VulkanRuntime&&)                 = delete;
        VulkanRuntime& operator=(const VulkanRuntime&) = delete;
        VulkanRuntime& operator=(VulkanRuntime&&)      = delete;

        WindowPlatform platform;
        VulkanGraphics graphics;
        GpuResources resources;
        VulkanFrames frames;
    };
} // namespace spectra
