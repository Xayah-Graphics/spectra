export module spectra.runtime;

export import spectra.runtime.device;
export import spectra.runtime.resources;
export import spectra.runtime.frames;

import std;
import vulkan;

namespace spectra::runtime {
    export struct VulkanRuntime {
        explicit VulkanRuntime(VulkanInstance& instance, vk::SurfaceKHR surface = {});
        ~VulkanRuntime();

        VulkanRuntime(const VulkanRuntime&)            = delete;
        VulkanRuntime(VulkanRuntime&&)                 = delete;
        VulkanRuntime& operator=(const VulkanRuntime&) = delete;
        VulkanRuntime& operator=(VulkanRuntime&&)      = delete;

        VulkanDevice device;
        GpuResources resources;
        VulkanFrames frames;
    };

    export [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path);
    export void record_default_graphics_state(const vk::raii::CommandBuffer& command_buffer);
} // namespace spectra::runtime
