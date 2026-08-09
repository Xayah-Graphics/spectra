export module spectra.runtime;

export import :graphics;
export import :resources;
export import :frames;

import std;
import vulkan;

namespace spectra {
    export struct VulkanRuntime {
        explicit VulkanRuntime(VulkanInstance& instance, vk::SurfaceKHR surface = {});
        ~VulkanRuntime();

        VulkanRuntime(const VulkanRuntime&)            = delete;
        VulkanRuntime(VulkanRuntime&&)                 = delete;
        VulkanRuntime& operator=(const VulkanRuntime&) = delete;
        VulkanRuntime& operator=(VulkanRuntime&&)      = delete;

        VulkanGraphics graphics;
        GpuResources resources;
        VulkanFrames frames;
    };
} // namespace spectra
