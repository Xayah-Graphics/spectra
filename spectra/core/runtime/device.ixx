export module spectra.runtime.device;

import std;
import vulkan;

namespace spectra::runtime {
    export struct GpuDeviceIdentity {
        std::array<std::uint8_t, 16> uuid{};
        std::array<std::uint8_t, 8> luid{};
        std::uint32_t node_mask{};
        bool luid_valid{};
    };

    export struct VulkanInstance {
        explicit VulkanInstance(std::string_view application_name, std::span<const char* const> extensions = {});

        VulkanInstance(const VulkanInstance&)            = delete;
        VulkanInstance(VulkanInstance&&)                 = delete;
        VulkanInstance& operator=(const VulkanInstance&) = delete;
        VulkanInstance& operator=(VulkanInstance&&)      = delete;

        vk::raii::Context loader{};
        vk::raii::Instance instance{nullptr};
    };

    export struct VulkanDevice {
        explicit VulkanDevice(VulkanInstance& instance, vk::SurfaceKHR surface = {});

        VulkanDevice(const VulkanDevice&)            = delete;
        VulkanDevice(VulkanDevice&&)                 = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
        VulkanDevice& operator=(VulkanDevice&&)      = delete;

        struct {
            VulkanInstance& instance;
            vk::SurfaceKHR surface{};
        } context;

        vk::raii::PhysicalDevice physical_device{nullptr};
        vk::raii::Device logical{nullptr};
        vk::raii::Queue queue{nullptr};
        std::uint32_t queue_family_index{};
        vk::PhysicalDeviceDescriptorHeapPropertiesEXT descriptor_heap_properties{};
        vk::PhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties{};
        vk::PhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_properties{};
        GpuDeviceIdentity identity{};
        bool ray_tracing_supported{};
        bool neural_field_supported{};

    private:
        void select_physical_device();
        void create_device();
    };
} // namespace spectra::runtime
