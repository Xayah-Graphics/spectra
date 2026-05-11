module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
module spectra;
import std;

namespace {
    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return VK_FALSE;
    }
} // namespace

namespace xayah {
    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;

        constexpr std::array enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
        constexpr std::array enabled_device_extensions{vk::KHRSwapchainExtensionName};
        std::vector<const char*> enabled_instance_extensions{};

        {
            std::uint32_t glfw_extension_count = 0;
            const char** glfw_extensions       = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            if (glfw_extensions == nullptr) throw std::runtime_error("Failed to get GLFW Vulkan instance extensions");
            enabled_instance_extensions = {glfw_extensions, glfw_extensions + glfw_extension_count};
            enabled_instance_extensions.push_back(vk::EXTDebugUtilsExtensionName);

            const std::vector<vk::LayerProperties> available_layers = this->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : enabled_instance_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : enabled_instance_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

            const vk::ApplicationInfo application_info{
                std::string{app_name}.c_str(),
                VK_MAKE_VERSION(1, 0, 0),
                std::string{engine_name}.c_str(),
                VK_MAKE_VERSION(1, 0, 0),
                vk::ApiVersion14,
            };
            const vk::InstanceCreateInfo instance_create_info{
                {},
                &application_info,
                static_cast<std::uint32_t>(enabled_instance_layers.size()),
                enabled_instance_layers.data(),
                static_cast<std::uint32_t>(enabled_instance_extensions.size()),
                enabled_instance_extensions.data(),
            };
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }
        {
            constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
                {},
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                &debug_callback,
            };
            this->context.debug_messenger = this->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
        }
        {
            if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            this->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), std::string{app_name}.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
            if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
        }
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
            this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
        }
        {
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
                if (physical_device.getProperties().apiVersion < VK_API_VERSION_1_4) continue;

                const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
                if (const auto found = std::ranges::find(available_extensions, std::string_view{vk::KHRSwapchainExtensionName}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) continue;

                const std::vector<vk::QueueFamilyProperties> queue_families = physical_device.getQueueFamilyProperties();
                for (std::uint32_t queue_family_index = 0; queue_family_index < queue_families.size(); ++queue_family_index) {
                    if (!static_cast<bool>(queue_families[queue_family_index].queueFlags & vk::QueueFlagBits::eGraphics)) continue;
                    if (!physical_device.getSurfaceSupportKHR(queue_family_index, this->surface.surface)) continue;
                    this->context.physical_device      = physical_device;
                    this->context.graphics_queue_index = queue_family_index;
                    selected                           = true;
                    break;
                }
                if (selected) break;
            }
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain and graphics-present queue support");
        }
        {
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore) throw std::runtime_error("Device does not support timelineSemaphore");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2  = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering  = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{
                {},
                1,
                &queue_create_info,
                0,
                nullptr,
                static_cast<std::uint32_t>(enabled_device_extensions.size()),
                enabled_device_extensions.data(),
                nullptr,
                &enabled_features.get<vk::PhysicalDeviceFeatures2>(),
            };
            this->context.device         = vk::raii::Device{this->context.physical_device, device_create_info};
            this->context.graphics_queue = vk::raii::Queue{this->context.device, this->context.graphics_queue_index, 0};
        }
        {
            const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->context.graphics_queue_index};
            this->context.command_pool = vk::raii::CommandPool{this->context.device, command_pool_create_info};
        }

        {
            constexpr std::string_view reset{"\x1b[0m"};
            constexpr std::string_view bold{"\x1b[1m"};
            constexpr std::string_view dim{"\x1b[2m"};
            constexpr std::string_view cyan{"\x1b[36m"};
            constexpr std::string_view green{"\x1b[32m"};
            constexpr std::string_view yellow{"\x1b[33m"};
            constexpr std::string_view magenta{"\x1b[35m"};
            constexpr std::string_view blue{"\x1b[34m"};

            const auto enabled_instance_layer     = [&](const char* name) { return std::ranges::find(enabled_instance_layers, std::string_view{name}, [](const char* layer) { return std::string_view{layer}; }) != enabled_instance_layers.end(); };
            const auto enabled_instance_extension = [&](const char* name) { return std::ranges::find(enabled_instance_extensions, std::string_view{name}, [](const char* extension) { return std::string_view{extension}; }) != enabled_instance_extensions.end(); };
            const auto enabled_device_extension   = [&](const char* name) { return std::ranges::find(enabled_device_extensions, std::string_view{name}, [](const char* extension) { return std::string_view{extension}; }) != enabled_device_extensions.end(); };

            const std::vector<vk::LayerProperties> instance_layers         = this->context.context.enumerateInstanceLayerProperties();
            const std::vector<vk::ExtensionProperties> instance_extensions = this->context.context.enumerateInstanceExtensionProperties();
            const std::vector<vk::ExtensionProperties> device_extensions   = this->context.physical_device.enumerateDeviceExtensionProperties();
            const std::vector<vk::QueueFamilyProperties> queue_families    = this->context.physical_device.getQueueFamilyProperties();
            const auto properties_chain                                    = this->context.physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
            const vk::PhysicalDeviceProperties& properties                 = properties_chain.get<vk::PhysicalDeviceProperties2>().properties;
            const vk::PhysicalDeviceDriverProperties& driver               = properties_chain.get<vk::PhysicalDeviceDriverProperties>();
            const auto features                                            = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
            const vk::PhysicalDeviceMemoryProperties memory                = this->context.physical_device.getMemoryProperties();
            const vk::SurfaceCapabilitiesKHR surface_capabilities          = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats        = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes            = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);

            std::println("\n{}{}Spectra Vulkan Context{}", bold, cyan, reset);
            std::println("{}{}{}", dim, std::string(96, '='), reset);
            std::println("{}Application{}       {}", blue, reset, std::string{app_name});
            std::println("{}Engine{}            {}", blue, reset, std::string{engine_name});
            std::println("{}Window requested{}  {} x {}", blue, reset, window_width, window_height);
            std::println("{}Framebuffer{}       {} x {}", blue, reset, this->surface.extent.width, this->surface.extent.height);
            std::println("{}Selected device{}   {}", blue, reset, properties.deviceName.data());
            std::println("{}Device type{}       {}", blue, reset, vk::to_string(properties.deviceType));
            std::println("{}Vendor / Device{}   0x{:04X} / 0x{:04X}", blue, reset, properties.vendorID, properties.deviceID);
            std::println("{}Vulkan API{}        {}.{}.{}", blue, reset, vk::apiVersionMajor(properties.apiVersion), vk::apiVersionMinor(properties.apiVersion), vk::apiVersionPatch(properties.apiVersion));
            std::println("{}Driver{}            {} ({})", blue, reset, driver.driverName.data(), vk::to_string(driver.driverID));
            std::println("{}Driver info{}       {}", blue, reset, driver.driverInfo.data());
            std::println("{}Driver version{}    {}", blue, reset, properties.driverVersion);
            std::println("{}Conformance{}       {}.{}.{}.{}", blue, reset, driver.conformanceVersion.major, driver.conformanceVersion.minor, driver.conformanceVersion.subminor, driver.conformanceVersion.patch);

            std::println("\n{}{}Instance Layers{} {}", bold, magenta, reset, instance_layers.size());
            for (const vk::LayerProperties& layer : instance_layers) {
                const std::string_view status = enabled_instance_layer(layer.layerName.data()) ? "ENABLED" : "available";
                const std::string_view color  = enabled_instance_layer(layer.layerName.data()) ? green : dim;
                std::println("  {}[{:<9}]{} {:<44} spec {}.{}.{} impl {} {}", color, status, reset, layer.layerName.data(), vk::apiVersionMajor(layer.specVersion), vk::apiVersionMinor(layer.specVersion), vk::apiVersionPatch(layer.specVersion), layer.implementationVersion, layer.description.data());
            }

            std::println("\n{}{}Instance Extensions{} {}", bold, magenta, reset, instance_extensions.size());
            for (const vk::ExtensionProperties& extension : instance_extensions) {
                const std::string_view status = enabled_instance_extension(extension.extensionName.data()) ? "ENABLED" : "available";
                const std::string_view color  = enabled_instance_extension(extension.extensionName.data()) ? green : dim;
                std::println("  {}[{:<9}]{} {:<48} spec {}", color, status, reset, extension.extensionName.data(), extension.specVersion);
            }

            std::println("\n{}{}Device Extensions{} {}", bold, magenta, reset, device_extensions.size());
            for (const vk::ExtensionProperties& extension : device_extensions) {
                const std::string_view status = enabled_device_extension(extension.extensionName.data()) ? "ENABLED" : "available";
                const std::string_view color  = enabled_device_extension(extension.extensionName.data()) ? green : dim;
                std::println("  {}[{:<9}]{} {:<48} spec {}", color, status, reset, extension.extensionName.data(), extension.specVersion);
            }

            std::println("\n{}{}Enabled Device Features{}", bold, magenta, reset);
            std::println("  {}[ENABLED]{} timelineSemaphore", green, reset);
            std::println("  {}[ENABLED]{} synchronization2", green, reset);
            std::println("  {}[ENABLED]{} dynamicRendering", green, reset);
            std::println("  {}[support]{} samplerAnisotropy: {}", dim, reset, static_cast<bool>(features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy));
            std::println("  {}[support]{} fillModeNonSolid: {}", dim, reset, static_cast<bool>(features.get<vk::PhysicalDeviceFeatures2>().features.fillModeNonSolid));

            std::println("\n{}{}Queue Families{} {}", bold, magenta, reset, queue_families.size());
            for (std::uint32_t index = 0; index < queue_families.size(); ++index) {
                const bool selected = index == this->context.graphics_queue_index;
                const bool present  = this->context.physical_device.getSurfaceSupportKHR(index, this->surface.surface);
                std::println("  {}[{:<8}]{} #{:<2} queues {:<2} present {:<5} flags {}", selected ? green : dim, selected ? "SELECTED" : "available", reset, index, queue_families[index].queueCount, present, vk::to_string(queue_families[index].queueFlags));
            }

            std::println("\n{}{}Surface{}", bold, magenta, reset);
            if (surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max())
                std::println("  extent current variable  min {} x {}  max {} x {}", surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height);
            else
                std::println("  extent current {} x {}  min {} x {}  max {} x {}", surface_capabilities.currentExtent.width, surface_capabilities.currentExtent.height, surface_capabilities.minImageExtent.width, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.width, surface_capabilities.maxImageExtent.height);
            std::println("  image count   min {}  max {}", surface_capabilities.minImageCount, surface_capabilities.maxImageCount);
            std::println("  transform     current {}  supported {}", vk::to_string(surface_capabilities.currentTransform), vk::to_string(surface_capabilities.supportedTransforms));
            std::println("  usage         {}", vk::to_string(surface_capabilities.supportedUsageFlags));
            std::println("  formats       {}", surface_formats.size());
            for (const vk::SurfaceFormatKHR& format : surface_formats) std::println("    {} / {}", vk::to_string(format.format), vk::to_string(format.colorSpace));
            std::println("  present modes {}", present_modes.size());
            for (const vk::PresentModeKHR present_mode : present_modes) std::println("    {}", vk::to_string(present_mode));

            std::println("\n{}{}Memory{}", bold, magenta, reset);
            for (std::uint32_t index = 0; index < memory.memoryHeapCount; ++index) std::println("  heap #{:<2} {:>10.1f} MiB  {}", index, static_cast<double>(memory.memoryHeaps[index].size) / 1024.0 / 1024.0, vk::to_string(memory.memoryHeaps[index].flags));

            std::println("\n{}{}Limits{}", bold, magenta, reset);
            std::println("  maxImageDimension2D      {}", properties.limits.maxImageDimension2D);
            std::println("  maxSamplerAnisotropy     {}", properties.limits.maxSamplerAnisotropy);
            std::println("  maxBoundDescriptorSets   {}", properties.limits.maxBoundDescriptorSets);
            std::println("  timestampPeriod          {}", properties.limits.timestampPeriod);
            std::println("{}{}{}\n", dim, std::string(96, '='), reset);
        }
    } catch (...) {
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }

        this->context.command_pool    = nullptr;
        this->context.graphics_queue  = nullptr;
        this->context.device          = nullptr;
        this->surface.surface         = nullptr;
        this->surface.window          = nullptr;
        this->context.physical_device = nullptr;
        this->context.debug_messenger = nullptr;
        this->context.instance        = nullptr;
        if (this->surface.glfw_initialized) glfwTerminate();
        this->surface.glfw_initialized = false;
    }
} // namespace xayah
