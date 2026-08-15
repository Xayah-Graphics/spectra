module spectra.runtime.graphics;

import std;
import vulkan;

namespace spectra {
    namespace {
        [[nodiscard]] constexpr std::array<const char*, 7> required_device_extensions() noexcept {
            return {
                vk::EXTDescriptorHeapExtensionName,
                vk::KHRShaderUntypedPointersExtensionName,
                vk::EXTShaderObjectExtensionName,
                vk::EXTMeshShaderExtensionName,
                vk::EXTShaderAtomicFloatExtensionName,
#if defined(_WIN32)
                vk::KHRExternalMemoryWin32ExtensionName,
                vk::KHRExternalSemaphoreWin32ExtensionName,
#else
                vk::KHRExternalMemoryFdExtensionName,
                vk::KHRExternalSemaphoreFdExtensionName,
#endif
            };
        }

        [[nodiscard]] constexpr std::array<const char*, 6> ray_tracing_device_extensions() noexcept {
            return {
                vk::KHRAccelerationStructureExtensionName,
                vk::KHRDeferredHostOperationsExtensionName,
                vk::KHRRayTracingPipelineExtensionName,
                vk::KHRRayTracingMaintenance1ExtensionName,
                vk::KHRRayTracingPositionFetchExtensionName,
                vk::KHRRayQueryExtensionName,
            };
        }
    } // namespace

    VulkanInstance::VulkanInstance(const std::string_view application_name, const std::span<const char* const> extensions) {
        const std::string application_name_string{application_name};
        const vk::ApplicationInfo application_info{application_name_string.c_str(), vk::makeApiVersion(0u, 2u, 0u, 0u), "Spectra", vk::makeApiVersion(0u, 2u, 0u, 0u), vk::ApiVersion14};
        this->instance = vk::raii::Instance{this->loader, vk::InstanceCreateInfo{{}, &application_info, 0, nullptr, static_cast<std::uint32_t>(extensions.size()), extensions.data()}};
    }

    VulkanGraphics::VulkanGraphics(VulkanInstance& instance, const vk::SurfaceKHR surface) : context{instance, surface} {
        this->select_physical_device();
        this->create_device();
    }

    void VulkanGraphics::select_physical_device() {
        constexpr std::array base_extensions        = required_device_extensions();
        constexpr std::array ray_tracing_extensions = ray_tracing_device_extensions();
        std::uint32_t selected_score{};
        for (const vk::raii::PhysicalDevice& candidate : this->context.instance.instance.enumeratePhysicalDevices()) {
            const vk::PhysicalDeviceProperties candidate_properties = candidate.getProperties();
            if (candidate_properties.apiVersion < vk::ApiVersion14) continue;
            const std::vector<vk::ExtensionProperties> available_extensions = candidate.enumerateDeviceExtensionProperties();
            if (!std::ranges::all_of(base_extensions, [&available_extensions](const char* required) { return std::ranges::contains(available_extensions, std::string_view{required}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); })) continue;
            if (this->context.surface && (!std::ranges::contains(available_extensions, std::string_view{vk::KHRSwapchainExtensionName}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }) || !std::ranges::contains(available_extensions, std::string_view{vk::KHRSwapchainMaintenance1ExtensionName}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }))) continue;
            if (this->context.surface && !candidate.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>().get<vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR>().swapchainMaintenance1) continue;

            const auto features = candidate.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR, vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR, vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT, vk::PhysicalDeviceCooperativeVectorFeaturesNV>();
            if (!features.get<vk::PhysicalDeviceFeatures2>().features.shaderInt64 || !features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy) continue;
            if (!features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) continue;
            if (!features.get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress || !features.get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout || !features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore) continue;
            if (!features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 || !features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) continue;
            if (!features.get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap) continue;
            if (!features.get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers) continue;
            if (!features.get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject) continue;
            if (!features.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader) continue;
            if (!features.get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd) continue;
            const bool ray_tracing_extensions_available = std::ranges::all_of(ray_tracing_extensions, [&available_extensions](const char* required) { return std::ranges::contains(available_extensions, std::string_view{required}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); });
            const bool ray_tracing_available            = ray_tracing_extensions_available && features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure && features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline && features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1 && features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2 && features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch && features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery;
            const bool cooperative_vector_extension     = std::ranges::contains(available_extensions, std::string_view{vk::NVCooperativeVectorExtensionName}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; });
            const std::vector<vk::CooperativeVectorPropertiesNV> cooperative_vector_properties = cooperative_vector_extension ? candidate.getCooperativeVectorPropertiesNV() : std::vector<vk::CooperativeVectorPropertiesNV>{};
            const bool neural_field_combination = std::ranges::any_of(cooperative_vector_properties, [](const vk::CooperativeVectorPropertiesNV& property) {
                return property.inputType == vk::ComponentTypeKHR::eFloat16 && property.inputInterpretation == vk::ComponentTypeKHR::eFloat16 && property.matrixInterpretation == vk::ComponentTypeKHR::eFloat16 && property.resultType == vk::ComponentTypeKHR::eFloat16;
            });
            const bool neural_field_available = cooperative_vector_extension && neural_field_combination && features.get<vk::PhysicalDeviceCooperativeVectorFeaturesNV>().cooperativeVector && features.get<vk::PhysicalDeviceVulkan11Features>().storageBuffer16BitAccess && features.get<vk::PhysicalDeviceVulkan12Features>().shaderFloat16;

#if defined(_WIN32)
            constexpr vk::ExternalMemoryHandleTypeFlagBits external_memory_handle       = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
            constexpr vk::ExternalSemaphoreHandleTypeFlagBits external_semaphore_handle = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueWin32;
#else
            constexpr vk::ExternalMemoryHandleTypeFlagBits external_memory_handle       = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
            constexpr vk::ExternalSemaphoreHandleTypeFlagBits external_semaphore_handle = vk::ExternalSemaphoreHandleTypeFlagBits::eOpaqueFd;
#endif
            constexpr vk::BufferUsageFlags external_buffer_usage = vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
            const vk::ExternalMemoryProperties external_memory   = candidate.getExternalBufferProperties(vk::PhysicalDeviceExternalBufferInfo{{}, external_buffer_usage, external_memory_handle}).externalMemoryProperties;
            if (!(external_memory.externalMemoryFeatures & vk::ExternalMemoryFeatureFlagBits::eExportable) || !(external_memory.compatibleHandleTypes & external_memory_handle)) continue;
            const vk::SemaphoreTypeCreateInfo timeline_type{vk::SemaphoreType::eTimeline, 0};
            const vk::ExternalSemaphoreProperties external_semaphore = candidate.getExternalSemaphoreProperties(vk::PhysicalDeviceExternalSemaphoreInfo{external_semaphore_handle, &timeline_type});
            if (!(external_semaphore.externalSemaphoreFeatures & vk::ExternalSemaphoreFeatureFlagBits::eExportable) || !(external_semaphore.compatibleHandleTypes & external_semaphore_handle)) continue;

            const std::vector<vk::QueueFamilyProperties> queue_families = candidate.getQueueFamilyProperties();
            std::uint32_t graphics_family_index                         = static_cast<std::uint32_t>(queue_families.size());
            for (std::uint32_t index = 0; index < queue_families.size(); ++index) {
                if (!static_cast<bool>(queue_families[index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                if (this->context.surface && !candidate.getSurfaceSupportKHR(index, this->context.surface)) continue;
                graphics_family_index = index;
                break;
            }
            if (graphics_family_index == queue_families.size()) continue;
            const std::uint32_t score = (ray_tracing_available ? 8u : 0u) + (candidate_properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu ? 4u : 2u) + (neural_field_available ? 1u : 0u);
            if (score <= selected_score) continue;
            selected_score              = score;
            this->physical_device       = candidate;
            this->queue_family_index    = graphics_family_index;
            this->ray_tracing_supported = ray_tracing_available;
            this->neural_field_supported = neural_field_available;
        }
        if (!*this->physical_device) throw std::runtime_error(this->context.surface ? "Spectra editor requires a Vulkan 1.4 GPU with Descriptor Heap, Shader Untyped Pointers, Shader Object, Mesh Shader, shader buffer float atomics, and Swapchain Maintenance 1" : "Spectra requires a Vulkan 1.4 GPU with Descriptor Heap, Shader Untyped Pointers, Shader Object, Mesh Shader, and shader buffer float atomics");
    }

    void VulkanGraphics::create_device() {
        constexpr std::array base_extensions = required_device_extensions();
        std::vector<const char*> enabled_extensions{base_extensions.begin(), base_extensions.end()};
        if (this->ray_tracing_supported) {
            constexpr std::array ray_tracing_extensions = ray_tracing_device_extensions();
            enabled_extensions.insert(enabled_extensions.end(), ray_tracing_extensions.begin(), ray_tracing_extensions.end());
        }
        if (this->neural_field_supported) enabled_extensions.push_back(vk::NVCooperativeVectorExtensionName);
        if (this->context.surface) {
            enabled_extensions.push_back(vk::KHRSwapchainExtensionName);
            enabled_extensions.push_back(vk::KHRSwapchainMaintenance1ExtensionName);
        }
        constexpr std::array queue_priorities{1.0f};
        const vk::DeviceQueueCreateInfo queue_create_info{{}, this->queue_family_index, 1, queue_priorities.data()};
        const vk::Bool32 neural_field_features = this->neural_field_supported;
        const auto enable_base = [neural_field_features](auto& features) {
            features.template get<vk::PhysicalDeviceFeatures2>().features.shaderInt64                            = vk::True;
            features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy                      = vk::True;
            features.template get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters                     = vk::True;
            features.template get<vk::PhysicalDeviceVulkan12Features>().bufferDeviceAddress                      = vk::True;
            features.template get<vk::PhysicalDeviceVulkan12Features>().scalarBlockLayout                        = vk::True;
            features.template get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore                        = vk::True;
            features.template get<vk::PhysicalDeviceVulkan13Features>().synchronization2                         = vk::True;
            features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering                         = vk::True;
            features.template get<vk::PhysicalDeviceDescriptorHeapFeaturesEXT>().descriptorHeap                  = vk::True;
            features.template get<vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR>().shaderUntypedPointers    = vk::True;
            features.template get<vk::PhysicalDeviceShaderObjectFeaturesEXT>().shaderObject                      = vk::True;
            features.template get<vk::PhysicalDeviceMeshShaderFeaturesEXT>().meshShader                          = vk::True;
            features.template get<vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT>().shaderBufferFloat32AtomicAdd = vk::True;
            features.template get<vk::PhysicalDeviceVulkan11Features>().storageBuffer16BitAccess                  = neural_field_features;
            features.template get<vk::PhysicalDeviceVulkan12Features>().shaderFloat16                             = neural_field_features;
        };
        if (this->ray_tracing_supported) {
            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR, vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR, vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT> features{};
            enable_base(features);
            vk::PhysicalDeviceCooperativeVectorFeaturesNV cooperative_vector{this->neural_field_supported};
            if (this->neural_field_supported) cooperative_vector.pNext = features.get<vk::PhysicalDeviceFeatures2>().pNext, features.get<vk::PhysicalDeviceFeatures2>().pNext = &cooperative_vector;
            vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance{vk::True, features.get<vk::PhysicalDeviceFeatures2>().pNext};
            if (this->context.surface) features.get<vk::PhysicalDeviceFeatures2>().pNext = &swapchain_maintenance;
            features.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>().accelerationStructure                 = vk::True;
            features.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>().rayTracingPipeline                       = vk::True;
            features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingMaintenance1               = vk::True;
            features.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>().rayTracingPipelineTraceRaysIndirect2 = vk::True;
            features.get<vk::PhysicalDeviceRayTracingPositionFetchFeaturesKHR>().rayTracingPositionFetch             = vk::True;
            features.get<vk::PhysicalDeviceRayQueryFeaturesKHR>().rayQuery                                           = vk::True;
            this->device                                                                                             = vk::raii::Device{this->physical_device, vk::DeviceCreateInfo{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_extensions.size()), enabled_extensions.data(), nullptr, &features.get<vk::PhysicalDeviceFeatures2>()}};
        } else {
            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceDescriptorHeapFeaturesEXT, vk::PhysicalDeviceShaderUntypedPointersFeaturesKHR, vk::PhysicalDeviceShaderObjectFeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceShaderAtomicFloatFeaturesEXT> features{};
            enable_base(features);
            vk::PhysicalDeviceCooperativeVectorFeaturesNV cooperative_vector{this->neural_field_supported};
            if (this->neural_field_supported) cooperative_vector.pNext = features.get<vk::PhysicalDeviceFeatures2>().pNext, features.get<vk::PhysicalDeviceFeatures2>().pNext = &cooperative_vector;
            vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance{vk::True, features.get<vk::PhysicalDeviceFeatures2>().pNext};
            if (this->context.surface) features.get<vk::PhysicalDeviceFeatures2>().pNext = &swapchain_maintenance;
            this->device = vk::raii::Device{this->physical_device, vk::DeviceCreateInfo{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_extensions.size()), enabled_extensions.data(), nullptr, &features.get<vk::PhysicalDeviceFeatures2>()}};
        }
        this->queue = vk::raii::Queue{this->device, this->queue_family_index, 0};

        const auto properties                                 = this->physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties, vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
        this->descriptor_heap_properties                      = properties.get<vk::PhysicalDeviceDescriptorHeapPropertiesEXT>();
        const vk::PhysicalDeviceIDProperties& device_identity = properties.get<vk::PhysicalDeviceIDProperties>();
        std::ranges::copy(device_identity.deviceUUID, this->identity.uuid.begin());
        std::ranges::copy(device_identity.deviceLUID, this->identity.luid.begin());
        this->identity.node_mask = device_identity.deviceNodeMask;
        this->identity.luid_valid = device_identity.deviceLUIDValid;
        if (this->ray_tracing_supported) {
            const auto ray_tracing_properties       = this->physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
            this->acceleration_structure_properties = ray_tracing_properties.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
            this->ray_tracing_properties            = ray_tracing_properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        }
    }
} // namespace spectra
