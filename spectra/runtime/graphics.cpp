module;

#include <Windows.h>

module spectra.runtime;

import :graphics;
import std;
import vulkan;

namespace spectra {
    namespace {
        [[nodiscard]] constexpr std::array<const char*, 14> required_device_extensions() noexcept {
            return {
                vk::EXTDescriptorHeapExtensionName,
                vk::KHRShaderUntypedPointersExtensionName,
                vk::KHRAccelerationStructureExtensionName,
                vk::KHRDeferredHostOperationsExtensionName,
                vk::KHRRayTracingPipelineExtensionName,
                vk::KHRRayTracingMaintenance1ExtensionName,
                vk::KHRRayTracingPositionFetchExtensionName,
                vk::KHRRayQueryExtensionName,
                vk::KHRPipelineLibraryExtensionName,
                vk::EXTShaderObjectExtensionName,
                vk::EXTMeshShaderExtensionName,
                vk::EXTShaderAtomicFloatExtensionName,
                vk::KHRExternalMemoryWin32ExtensionName,
                vk::KHRExternalSemaphoreWin32ExtensionName,
            };
        }
    } // namespace

    VulkanGraphics::VulkanGraphics(const std::string_view application_name) {
        this->create_instance(application_name, nullptr);
        this->select_physical_device();
        this->create_device();
    }

    VulkanGraphics::VulkanGraphics(WindowPlatform& platform, const std::string_view application_name) {
        this->create_instance(application_name, &platform);
        this->select_physical_device();
        this->create_device();
    }

    void VulkanGraphics::create_instance(const std::string_view application_name, WindowPlatform* platform) {
        const std::string application_name_string{application_name};
        const vk::ApplicationInfo application_info{application_name_string.c_str(), vk::makeApiVersion(0u, 2u, 0u, 0u), "Spectra", vk::makeApiVersion(0u, 2u, 0u, 0u), vk::ApiVersion14};
        constexpr std::array presentation_extensions{vk::KHRSurfaceExtensionName, vk::KHRWin32SurfaceExtensionName};
        const std::span<const char* const> instance_extensions = platform ? std::span<const char* const>{presentation_extensions} : std::span<const char* const>{};
        this->instance.instance = vk::raii::Instance{this->instance.loader, vk::InstanceCreateInfo{{}, &application_info, 0, nullptr, static_cast<std::uint32_t>(instance_extensions.size()), instance_extensions.data()}};
        if (platform) this->instance.surface = vk::raii::SurfaceKHR{this->instance.instance, vk::Win32SurfaceCreateInfoKHR{{}, GetModuleHandleW(nullptr), platform->native_window}};
    }

    void VulkanGraphics::select_physical_device() {
        constexpr std::array base_extensions = required_device_extensions();
        for (const vk::raii::PhysicalDevice& candidate : this->instance.instance.enumeratePhysicalDevices()) {
            const vk::PhysicalDeviceProperties candidate_properties = candidate.getProperties();
            if (candidate_properties.apiVersion < vk::ApiVersion14 || candidate_properties.deviceType != vk::PhysicalDeviceType::eDiscreteGpu) continue;
            const std::vector<vk::ExtensionProperties> available_extensions = candidate.enumerateDeviceExtensionProperties();
            if (!std::ranges::all_of(base_extensions, [&available_extensions](const char* required) { return std::ranges::contains(available_extensions, std::string_view{required}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); })) continue;
            if (*this->instance.surface && !std::ranges::contains(available_extensions, std::string_view{vk::KHRSwapchainExtensionName}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; })) continue;

            const auto features = candidate.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR, vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR, vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>();
            if (!features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64) continue;
            if (!features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) continue;
            if (!features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress || !features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout) continue;
            if (!features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 || !features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) continue;
            if (!features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap) continue;
            if (!features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers) continue;
            if (!features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure) continue;
            if (!features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline) continue;
            if (!features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1 || !features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2) continue;
            if (!features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch) continue;
            if (!features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery) continue;
            if (!features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject) continue;
            if (!features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader) continue;
            if (!features.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd) continue;

            const std::vector<vk::QueueFamilyProperties> queue_families = candidate.getQueueFamilyProperties();
            std::uint32_t graphics_family_index                         = static_cast<std::uint32_t>(queue_families.size());
            for (std::uint32_t index = 0; index < queue_families.size(); ++index) {
                if (!static_cast<bool>(queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                if (*this->instance.surface && !candidate.getSurfaceSupportKHR(index, *this->instance.surface)) continue;
                graphics_family_index = index;
                break;
            }
            if (graphics_family_index == queue_families.size()) continue;
            this->physical_device    = candidate;
            this->queue_family_index = graphics_family_index;
            break;
        }
        if (!*this->physical_device) throw std::runtime_error("Spectra requires a discrete Vulkan 1.4 GPU with Descriptor Heap, Shader Untyped Pointers, Shader Object, Mesh Shader, and the complete KHR ray tracing pipeline profile");
    }

    void VulkanGraphics::create_device() {
        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR, vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR, vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT> enabled_features{};
        enabled_features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64                                         = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters                                  = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress                                   = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout                                     = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2                                      = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering                                      = vk::True;
        enabled_features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap                               = vk::True;
        enabled_features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers                 = vk::True;
        enabled_features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure                 = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline                       = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1               = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2 = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch             = vk::True;
        enabled_features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery                                           = vk::True;
        enabled_features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject                                   = vk::True;
        enabled_features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader                                       = vk::True;
        enabled_features.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd              = vk::True;

        constexpr std::array base_extensions = required_device_extensions();
        std::vector<const char*> enabled_extensions{base_extensions.begin(), base_extensions.end()};
        if (*this->instance.surface) enabled_extensions.push_back(vk::KHRSwapchainExtensionName);
        constexpr std::array queue_priorities{1.0f};
        const vk::DeviceQueueCreateInfo queue_create_info{{}, this->queue_family_index, 1, queue_priorities.data()};
        this->device = vk::raii::Device{this->physical_device, vk::DeviceCreateInfo{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_extensions.size()), enabled_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()}};
        this->queue  = vk::raii::Queue{this->device, this->queue_family_index, 0};

        const auto properties                                 = this->physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties, vk::PhysicalDeviceDescriptorHeapPropertiesEXT, vk::PhysicalDeviceAccelerationStructurePropertiesKHR, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        this->descriptor_heap_properties                      = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
        const vk::PhysicalDeviceIDProperties& device_identity = properties.get<vk::PhysicalDeviceIDProperties>();
        std::ranges::copy(device_identity.deviceUUID, this->identity.uuid.begin());
        std::ranges::copy(device_identity.deviceLUID, this->identity.luid.begin());
        this->identity.node_mask                = device_identity.deviceNodeMask;
        this->acceleration_structure_properties = properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
        this->ray_tracing_properties            = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    }
} // namespace spectra
