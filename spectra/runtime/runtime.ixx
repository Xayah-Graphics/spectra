export module spectra.runtime;

export import :platform;
export import :graphics;
export import :resources;
export import :frames;

import std;
import vulkan;

namespace spectra {
    export struct VulkanRuntime {
        explicit VulkanRuntime(std::string_view application_name = "Spectra");
        VulkanRuntime(WindowPlatform& platform, std::string_view application_name);
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
