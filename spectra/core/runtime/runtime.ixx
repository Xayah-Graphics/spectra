export module spectra.runtime;

export import spectra.runtime.graphics;
export import spectra.runtime.resources;
export import spectra.runtime.frames;

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

    export [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path);
} // namespace spectra
