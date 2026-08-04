module;

#include <Windows.h>

export module spectra.runtime:graphics;

import :platform;
import std;
import vulkan;

namespace spectra {
    export struct GpuDeviceIdentity {
        std::array<std::uint8_t, 16> uuid{};
        std::array<std::uint8_t, 8> luid{};
        std::uint32_t node_mask{};
    };

    export struct VulkanGraphics {
        explicit VulkanGraphics(std::string_view application_name);
        VulkanGraphics(WindowPlatform& platform, std::string_view application_name);

        VulkanGraphics(const VulkanGraphics&)            = delete;
        VulkanGraphics(VulkanGraphics&&)                 = delete;
        VulkanGraphics& operator=(const VulkanGraphics&) = delete;
        VulkanGraphics& operator=(VulkanGraphics&&)      = delete;

        struct {
            vk::raii::Context loader{};
            vk::raii::Instance instance{nullptr};
            vk::raii::SurfaceKHR surface{nullptr};
        } instance;

        vk::raii::PhysicalDevice physical_device{nullptr};
        vk::raii::Device device{nullptr};
        vk::raii::Queue queue{nullptr};
        std::uint32_t queue_family_index{};
        vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties{};
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties{};
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties{};
        GpuDeviceIdentity identity{};

    private:
        void create_instance(std::string_view application_name, WindowPlatform* platform);
        void select_physical_device();
        void create_device();
    };
} // namespace spectra
