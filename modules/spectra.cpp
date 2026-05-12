module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <ImSequencer.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>
module spectra;
import std;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::ImageAspectFlags aspect, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return VK_FALSE;
    }
} // namespace

namespace xayah {
    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
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
            glfwSetWindowUserPointer(this->surface.window.get(), this);
            glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
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
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) throw std::runtime_error("Device does not support shaderDrawParameters");
            if (!supported_features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy) throw std::runtime_error("Device does not support samplerAnisotropy");
            if (!supported_features.get<vk::PhysicalDeviceFeatures2>().features.fillModeNonSolid) throw std::runtime_error("Device does not support fillModeNonSolid");
            if (!supported_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore) throw std::runtime_error("Device does not support timelineSemaphore");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}, {}, {}};
            enabled_features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy  = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceFeatures2>().features.fillModeNonSolid   = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore    = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2     = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering     = VK_TRUE;

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
        this->create_swapchain();
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
            const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->context.command_pool, vk::CommandBufferLevel::ePrimary, this->sync.frame_count};
            this->sync.command_buffers = vk::raii::CommandBuffers{this->context.device, command_buffer_allocate_info};
            if (this->sync.command_buffers.size() != this->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

            this->sync.image_available_semaphores.reserve(this->sync.frame_count);
            this->sync.in_flight_fences.reserve(this->sync.frame_count);
            for (std::uint32_t frame_index = 0; frame_index < this->sync.frame_count; ++frame_index) {
                this->sync.image_available_semaphores.emplace_back(this->context.device, semaphore_create_info);
                this->sync.in_flight_fences.emplace_back(this->context.device, fence_create_info);
            }
        }
        {
            if (this->imgui.initialized) throw std::runtime_error("ImGui is already initialized");
            if (this->surface.window.get() == nullptr) throw std::runtime_error("Cannot initialize ImGui without a GLFW window");
            if (this->swapchain.images.empty()) throw std::runtime_error("Cannot initialize ImGui without swapchain images");

            bool context_created            = false;
            bool glfw_backend_initialized   = false;
            bool vulkan_backend_initialized = false;
            try {
                constexpr std::array descriptor_pool_sizes{
                    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
                    vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000},
                };
                const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{
                    vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                    1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()),
                    static_cast<std::uint32_t>(descriptor_pool_sizes.size()),
                    descriptor_pool_sizes.data(),
                };
                this->imgui.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};
                this->imgui.color_format    = this->swapchain.format;
                this->imgui.min_image_count = std::max(2u, this->sync.frame_count);
                this->imgui.image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
                if (this->imgui.image_count < this->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                context_created = true;

                ImGuiIO& io = ImGui::GetIO();
                if (this->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
                if (this->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
                ImGui::StyleColorsDark();
                if (this->imgui.viewports) {
                    ImGuiStyle& style                 = ImGui::GetStyle();
                    style.WindowRounding              = 0.0f;
                    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
                }

                if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
                glfw_backend_initialized = true;

                auto color_attachment_format = static_cast<VkFormat>(this->imgui.color_format);
                VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
                pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                pipeline_rendering_create_info.colorAttachmentCount    = 1;
                pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

                ImGui_ImplVulkan_InitInfo init_info{};
                init_info.ApiVersion                  = VK_API_VERSION_1_4;
                init_info.Instance                    = static_cast<VkInstance>(*this->context.instance);
                init_info.PhysicalDevice              = static_cast<VkPhysicalDevice>(*this->context.physical_device);
                init_info.Device                      = static_cast<VkDevice>(*this->context.device);
                init_info.QueueFamily                 = this->context.graphics_queue_index;
                init_info.Queue                       = static_cast<VkQueue>(*this->context.graphics_queue);
                init_info.DescriptorPool              = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
                init_info.MinImageCount               = this->imgui.min_image_count;
                init_info.ImageCount                  = this->imgui.image_count;
                init_info.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
                init_info.UseDynamicRendering         = true;
                init_info.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
                if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
                vulkan_backend_initialized = true;
                this->imgui.initialized    = true;
            } catch (...) {
                if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
                if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
                if (context_created) ImGui::DestroyContext();
                this->imgui.descriptor_pool = nullptr;
                this->imgui.color_format    = vk::Format::eUndefined;
                this->imgui.min_image_count = 2;
                this->imgui.image_count     = 2;
                this->imgui.initialized     = false;
                throw;
            }
        }

        {
            constexpr std::string_view reset{"\x1b[0m"};
            constexpr std::string_view bold{"\x1b[1m"};
            constexpr std::string_view dim{"\x1b[2m"};
            constexpr std::string_view cyan{"\x1b[36m"};
            constexpr std::string_view green{"\x1b[32m"};
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
            const auto features                                            = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
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
            std::println("  {}[ENABLED]{} samplerAnisotropy", green, reset);
            std::println("  {}[ENABLED]{} fillModeNonSolid", green, reset);
            std::println("  {}[ENABLED]{} shaderDrawParameters", green, reset);
            std::println("  {}[ENABLED]{} timelineSemaphore", green, reset);
            std::println("  {}[ENABLED]{} synchronization2", green, reset);
            std::println("  {}[ENABLED]{} dynamicRendering", green, reset);
            std::println("  {}[support]{} robustBufferAccess: {}", dim, reset, static_cast<bool>(features.get<vk::PhysicalDeviceFeatures2>().features.robustBufferAccess));

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

            std::println("\n{}{}Swapchain Plan{}", bold, magenta, reset);
            std::println("  format       {} / {}", vk::to_string(this->swapchain.format), vk::to_string(this->swapchain.color_space));
            std::println("  extent       {} x {}", this->swapchain.extent.width, this->swapchain.extent.height);
            std::println("  image count  {}", this->swapchain.image_count);
            std::println("  images       {}", this->swapchain.images.size());
            std::println("  image views  {}", this->swapchain.image_views.size());
            std::println("  layouts      {}", this->swapchain.image_layouts.size());
            std::println("  present mode {}", vk::to_string(this->swapchain.present_mode));
            std::println("  usage        {}", vk::to_string(this->swapchain.usage));
            std::println("  depth format {}", vk::to_string(this->swapchain.depth_format));
            std::println("  depth aspect {}", vk::to_string(this->swapchain.depth_aspect));
            std::println("  depth layout {}", vk::to_string(this->swapchain.depth_layout));

            std::println("\n{}{}Frame Sync{}", bold, magenta, reset);
            std::println("  frames in flight {}", this->sync.frame_count);
            std::println("  current frame    {}", this->sync.frame_index);
            std::println("  command buffers  {}", this->sync.command_buffers.size());
            std::println("  image available  {}", this->sync.image_available_semaphores.size());
            std::println("  render finished  {}", this->sync.render_finished_semaphores.size());
            std::println("  image in flight  {}", this->sync.image_in_flight_frame.size());
            std::println("  in-flight fences {}", this->sync.in_flight_fences.size());

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
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            this->imgui.descriptor_pool = nullptr;
            this->imgui.color_format    = vk::Format::eUndefined;
            this->imgui.min_image_count = 2;
            this->imgui.image_count     = 2;
            this->imgui.initialized     = false;
        }
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }

        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool = nullptr;
        this->imgui.color_format    = vk::Format::eUndefined;
        this->imgui.min_image_count = 2;
        this->imgui.image_count     = 2;
        this->imgui.initialized     = false;
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
        this->context.command_pool   = nullptr;
        this->swapchain.depth_view   = nullptr;
        this->swapchain.depth_image  = nullptr;
        this->swapchain.depth_memory = nullptr;
        this->swapchain.image_views.clear();
        this->swapchain.handle = nullptr;
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
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

    void Spectra::run() {
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }

        this->context.device.waitIdle();
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present = false;
        frame.frame_index            = this->sync.frame_index;
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        return true;
    }

    void Spectra::record_frame(const FrameState& frame) {
        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);
        this->begin_rendering(command_buffer, frame.image_index);
        this->end_rendering(command_buffer, frame.image_index);
        this->draw_imgui();
        this->render_imgui(command_buffer, frame.image_index);
        transition_image_layout(command_buffer, this->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame) {
        if (this->imgui.viewports) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        const vk::SemaphoreSubmitInfo wait_semaphore_info{*this->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, 1, &wait_semaphore_info, 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
                frame.recreate_after_present = true;
            else if (present_result == vk::Result::eErrorSurfaceLostKHR)
                frame.recreate_after_present = true;
            else if (present_result != vk::Result::eSuccess)
                throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
        } catch (const vk::SystemError& error) {
            if (error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eSuboptimalKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR))
                frame.recreate_after_present = true;
            else
                throw;
        }
        if (frame.recreate_after_present) this->recreate_swapchain();

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }

    void Spectra::draw_imgui() {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");

        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + 12.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.02f);
        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.035f, 0.045f, 0.055f, 0.62f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.28f, 0.55f, 0.90f, 0.55f});
        ImGui::Begin("Spectra Stats", nullptr, window_flags);
        {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            const ImVec2 window_size = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRectFilled(window_pos, ImVec2{window_pos.x + 4.0f, window_pos.y + window_size.y}, IM_COL32(76, 158, 255, 210), 8.0f, ImDrawFlags_RoundCornersLeft);
        }

        const ImVec4 label_color{0.58f, 0.66f, 0.75f, 1.0f};
        const ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        const ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};
        const ImVec4 muted_color{0.70f, 0.76f, 0.82f, 1.0f};
        const float fps = ImGui::GetIO().Framerate;
        const ImVec4 fps_color = fps >= 55.0f ? ImVec4{0.46f, 0.90f, 0.62f, 1.0f} : ImVec4{0.95f, 0.68f, 0.36f, 1.0f};

        ImGui::TextColored(accent_color, "Spectra");
        ImGui::SameLine();
        ImGui::TextColored(muted_color, "Vulkan 1.4 RAII");
        ImGui::Separator();
        if (ImGui::BeginTable("Stats", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Framebuffer");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%u x %u", this->swapchain.extent.width, this->swapchain.extent.height);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Swapchain");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%u images", static_cast<std::uint32_t>(this->swapchain.images.size()));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Frame slot");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%u / %u", this->sync.frame_index, this->sync.frame_count);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "Timeline");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, "%d / %d", this->timeline.current_frame, this->timeline.frame_max);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "FPS");
            ImGui::TableNextColumn();
            ImGui::TextColored(fps_color, "%.1f", fps);
            ImGui::EndTable();
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        this->draw_timeline();
    }

    void Spectra::draw_timeline() {
        if (this->timeline.frame_min > this->timeline.frame_max) throw std::runtime_error("Invalid timeline frame range");
        this->timeline.current_frame = std::clamp(this->timeline.current_frame, this->timeline.frame_min, this->timeline.frame_max);
        this->timeline.first_frame   = std::clamp(this->timeline.first_frame, this->timeline.frame_min, this->timeline.frame_max);

        struct TimelineSequence final : ImSequencer::SequenceInterface {
            int frame_min{0};
            int frame_max{0};
            int row_start_frame{0};
            int row_end_frame{0};

            TimelineSequence(const int frame_min, const int frame_max) : frame_min{frame_min}, frame_max{frame_max}, row_start_frame{frame_min}, row_end_frame{frame_max} {}

            int GetFrameMin() const override {
                return this->frame_min;
            }

            int GetFrameMax() const override {
                return this->frame_max;
            }

            int GetItemCount() const override {
                return 1;
            }

            const char* GetItemLabel(const int index) const override {
                if (index != 0) throw std::runtime_error("Timeline row index out of range");
                return "Frame";
            }

            void Get(const int index, int** start, int** end, int* type, unsigned int* color) override {
                if (index != 0) throw std::runtime_error("Timeline row index out of range");
                if (start != nullptr) *start = &this->row_start_frame;
                if (end != nullptr) *end = &this->row_end_frame;
                if (type != nullptr) *type = 0;
                if (color != nullptr) *color = 0x4E79A7;
            }
        };

        constexpr float timeline_height = 160.0f;
        const ImGuiViewport* viewport   = ImGui::GetMainViewport();
        if (viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (viewport->WorkSize.x <= 320.0f || viewport->WorkSize.y <= timeline_height) throw std::runtime_error("Viewport is too small for fixed timeline");

        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowPos(ImVec2{viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - timeline_height}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{viewport->WorkSize.x, timeline_height}, ImGuiCond_Always);
        constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;

        TimelineSequence sequence{this->timeline.frame_min, this->timeline.frame_max};
        constexpr int sequence_options = ImSequencer::SEQUENCER_CHANGE_FRAME;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("Spectra Timeline", nullptr, window_flags);
        ImGui::Text("Frame %d / %d", this->timeline.current_frame, this->timeline.frame_max);
        ImSequencer::Sequencer(&sequence, &this->timeline.current_frame, nullptr, nullptr, &this->timeline.first_frame, sequence_options);
        this->timeline.current_frame = std::clamp(this->timeline.current_frame, this->timeline.frame_min, this->timeline.frame_max);
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void Spectra::render_imgui(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t image_index) {
        ImGui::Render();
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();
    }

    void Spectra::begin_rendering(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t image_index) {
        {
            constexpr vk::PipelineStageFlags2 depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            constexpr vk::AccessFlags2 depth_access        = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            const std::array attachment_barriers{
                vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eAllCommands,
                    {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    this->swapchain.image_layouts[image_index],
                    vk::ImageLayout::eColorAttachmentOptimal,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    this->swapchain.images[image_index],
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                },
                vk::ImageMemoryBarrier2{
                    depth_stages,
                    depth_access,
                    depth_stages,
                    depth_access,
                    this->swapchain.depth_layout,
                    vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    *this->swapchain.depth_image,
                    {this->swapchain.depth_aspect, 0, 1, 0, 1},
                },
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(attachment_barriers.size()), attachment_barriers.data()};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[image_index] = vk::ImageLayout::eColorAttachmentOptimal;
        this->swapchain.depth_layout               = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        constexpr vk::ClearValue color_clear_value{vk::ClearColorValue{std::array{0.02f, 0.02f, 0.025f, 1.0f}}};
        constexpr vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            color_clear_value,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->swapchain.depth_view,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear_value,
        };
        const vk::RenderingAttachmentInfo* stencil_attachment = static_cast<bool>(this->swapchain.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? &depth_attachment : nullptr;
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, &depth_attachment, stencil_attachment};
        command_buffer.beginRendering(rendering_info);
    }

    void Spectra::end_rendering(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t) {
        command_buffer.endRendering();
    }

    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities   = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes     = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);
            if (surface_formats.empty()) throw std::runtime_error("Surface has no formats");
            if (present_modes.empty()) throw std::runtime_error("Surface has no present modes");

            if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
                this->swapchain.format      = vk::Format::eB8G8R8A8Srgb;
                this->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
            } else {
                this->swapchain.format      = surface_formats.front().format;
                this->swapchain.color_space = surface_formats.front().colorSpace;

                bool selected = false;
                constexpr std::array preferred_surface_formats{
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                };
                for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_surface_formats) {
                    for (const vk::SurfaceFormatKHR& surface_format : surface_formats) {
                        if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) {
                            this->swapchain.format      = surface_format.format;
                            this->swapchain.color_space = surface_format.colorSpace;
                            selected                    = true;
                            break;
                        }
                    }
                    if (selected) break;
                }
            }

            this->swapchain.present_mode = vk::PresentModeKHR::eFifo;
            for (const vk::PresentModeKHR present_mode : present_modes) {
                if (present_mode == vk::PresentModeKHR::eMailbox) {
                    this->swapchain.present_mode = present_mode;
                    break;
                }
                if (present_mode == vk::PresentModeKHR::eImmediate) this->swapchain.present_mode = present_mode;
            }

            this->swapchain.extent = surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? vk::Extent2D{std::clamp(this->surface.extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width), std::clamp(this->surface.extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)} : surface_capabilities.currentExtent;
            if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

            this->swapchain.image_count = surface_capabilities.minImageCount + 1;
            if (this->swapchain.image_count < 2) this->swapchain.image_count = 2;
            if (surface_capabilities.maxImageCount != 0 && this->swapchain.image_count > surface_capabilities.maxImageCount) this->swapchain.image_count = surface_capabilities.maxImageCount;

            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) throw std::runtime_error("Swapchain must support color attachment usage");
            this->swapchain.usage = vk::ImageUsageFlagBits::eColorAttachment;
            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst) == vk::ImageUsageFlagBits::eTransferDst) this->swapchain.usage |= vk::ImageUsageFlagBits::eTransferDst;
            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferSrc) == vk::ImageUsageFlagBits::eTransferSrc) this->swapchain.usage |= vk::ImageUsageFlagBits::eTransferSrc;
        }
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            auto composite_alpha                                  = vk::CompositeAlphaFlagBitsKHR::eOpaque;
            if (!(surface_capabilities.supportedCompositeAlpha & composite_alpha)) {
                if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
                else if (surface_capabilities.supportedCompositeAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
                    composite_alpha = vk::CompositeAlphaFlagBitsKHR::eInherit;
                else
                    throw std::runtime_error("Surface has no supported composite alpha mode");
            }

            const vk::SurfaceTransformFlagBitsKHR pre_transform = surface_capabilities.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity ? vk::SurfaceTransformFlagBitsKHR::eIdentity : surface_capabilities.currentTransform;
            const vk::SwapchainCreateInfoKHR swapchain_create_info{
                {},
                *this->surface.surface,
                this->swapchain.image_count,
                this->swapchain.format,
                this->swapchain.color_space,
                this->swapchain.extent,
                1,
                this->swapchain.usage,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                pre_transform,
                composite_alpha,
                this->swapchain.present_mode,
                VK_TRUE,
                *old_swapchain,
            };
            this->swapchain.handle = vk::raii::SwapchainKHR{this->context.device, swapchain_create_info};
        }
        {
            this->swapchain.images = this->swapchain.handle.getImages();
            if (this->swapchain.images.empty()) throw std::runtime_error("Swapchain has no images");
            this->swapchain.image_layouts.assign(this->swapchain.images.size(), vk::ImageLayout::eUndefined);
        }
        {
            this->swapchain.image_views.clear();
            this->swapchain.image_views.reserve(this->swapchain.images.size());
            for (const vk::Image image : this->swapchain.images) {
                const vk::ImageViewCreateInfo image_view_create_info{
                    {},
                    image,
                    vk::ImageViewType::e2D,
                    this->swapchain.format,
                    {},
                    {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
                };
                this->swapchain.image_views.emplace_back(this->context.device, image_view_create_info);
            }
            if (this->swapchain.image_views.size() != this->swapchain.images.size()) throw std::runtime_error("Failed to create all swapchain image views");
        }
        {
            bool selected = false;
            for (constexpr std::array depth_formats{vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint}; const vk::Format format : depth_formats) {
                if (static_cast<bool>(this->context.physical_device.getFormatProperties(format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment)) {
                    this->swapchain.depth_format = format;
                    selected                     = true;
                    break;
                }
            }
            if (!selected) throw std::runtime_error("No supported Vulkan depth format");
            this->swapchain.depth_aspect = this->swapchain.depth_format == vk::Format::eD32SfloatS8Uint || this->swapchain.depth_format == vk::Format::eD24UnormS8Uint ? vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth;
            this->swapchain.depth_layout = vk::ImageLayout::eUndefined;

            const vk::ImageCreateInfo depth_image_create_info{
                {},
                vk::ImageType::e2D,
                this->swapchain.depth_format,
                vk::Extent3D{this->swapchain.extent.width, this->swapchain.extent.height, 1},
                1,
                1,
                vk::SampleCountFlagBits::e1,
                vk::ImageTiling::eOptimal,
                vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransientAttachment,
                vk::SharingMode::eExclusive,
                0,
                nullptr,
                vk::ImageLayout::eUndefined,
            };
            this->swapchain.depth_image = vk::raii::Image{this->context.device, depth_image_create_info};

            const vk::MemoryRequirements depth_memory_requirements = this->swapchain.depth_image.getMemoryRequirements();
            const vk::PhysicalDeviceMemoryProperties memory        = this->context.physical_device.getMemoryProperties();
            std::uint32_t memory_type_index                        = std::numeric_limits<std::uint32_t>::max();
            for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
                if ((depth_memory_requirements.memoryTypeBits & (1u << index)) != 0 && (memory.memoryTypes[index].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal) == vk::MemoryPropertyFlagBits::eDeviceLocal) {
                    memory_type_index = index;
                    break;
                }
            }
            if (memory_type_index == std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("No suitable Vulkan memory type for depth image");
            const vk::MemoryAllocateInfo depth_memory_allocate_info{
                depth_memory_requirements.size,
                memory_type_index,
            };
            this->swapchain.depth_memory = vk::raii::DeviceMemory{this->context.device, depth_memory_allocate_info};
            this->swapchain.depth_image.bindMemory(*this->swapchain.depth_memory, 0);

            const vk::ImageViewCreateInfo depth_view_create_info{
                {},
                *this->swapchain.depth_image,
                vk::ImageViewType::e2D,
                this->swapchain.depth_format,
                {},
                {this->swapchain.depth_aspect, 0, 1, 0, 1},
            };
            this->swapchain.depth_view = vk::raii::ImageView{this->context.device, depth_view_create_info};
        }
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            this->sync.render_finished_semaphores.clear();
            this->sync.render_finished_semaphores.reserve(this->swapchain.images.size());
            for (std::uint32_t image_index = 0; image_index < this->swapchain.images.size(); ++image_index) this->sync.render_finished_semaphores.emplace_back(this->context.device, semaphore_create_info);
            this->sync.image_in_flight_frame.assign(this->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    void Spectra::recreate_swapchain() {
        {
            int width  = 0;
            int height = 0;
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
                if (width == 0 || height == 0) glfwWaitEvents();
            }
            this->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }

        this->context.device.waitIdle();

        vk::raii::SwapchainKHR old_swapchain = std::move(this->swapchain.handle);
        this->swapchain.depth_view           = nullptr;
        this->swapchain.depth_image          = nullptr;
        this->swapchain.depth_memory         = nullptr;
        this->swapchain.depth_format         = {};
        this->swapchain.depth_aspect         = {};
        this->swapchain.depth_layout         = vk::ImageLayout::eUndefined;
        this->swapchain.image_views.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_in_flight_frame.clear();
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
        this->create_swapchain(std::move(old_swapchain));
        {
            const std::uint32_t image_count = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized during swapchain recreation");
            if (this->imgui.color_format != this->swapchain.format || this->imgui.image_count != image_count) {
                const bool docking   = this->imgui.docking;
                const bool viewports = this->imgui.viewports;
                ImGui_ImplVulkan_Shutdown();
                ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
                this->imgui.descriptor_pool = nullptr;
                this->imgui.color_format    = this->swapchain.format;
                this->imgui.min_image_count = std::max(2u, this->sync.frame_count);
                this->imgui.image_count     = image_count;
                this->imgui.docking         = docking;
                this->imgui.viewports       = viewports;
                this->imgui.initialized     = false;
                if (this->imgui.image_count < this->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

                bool context_created            = false;
                bool glfw_backend_initialized   = false;
                bool vulkan_backend_initialized = false;
                try {
                    constexpr std::array descriptor_pool_sizes{
                        vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eCombinedImageSampler, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eUniformTexelBuffer, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eStorageTexelBuffer, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eUniformBufferDynamic, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eStorageBufferDynamic, 1000},
                        vk::DescriptorPoolSize{vk::DescriptorType::eInputAttachment, 1000},
                    };
                    const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{
                        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
                        1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()),
                        static_cast<std::uint32_t>(descriptor_pool_sizes.size()),
                        descriptor_pool_sizes.data(),
                    };
                    this->imgui.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};

                    IMGUI_CHECKVERSION();
                    ImGui::CreateContext();
                    context_created = true;

                    ImGuiIO& io = ImGui::GetIO();
                    if (this->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
                    if (this->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
                    ImGui::StyleColorsDark();
                    if (this->imgui.viewports) {
                        ImGuiStyle& style                 = ImGui::GetStyle();
                        style.WindowRounding              = 0.0f;
                        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
                    }

                    if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
                    glfw_backend_initialized = true;

                    const auto color_attachment_format = static_cast<VkFormat>(this->imgui.color_format);
                    VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
                    pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                    pipeline_rendering_create_info.colorAttachmentCount    = 1;
                    pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

                    ImGui_ImplVulkan_InitInfo init_info{};
                    init_info.ApiVersion                  = VK_API_VERSION_1_4;
                    init_info.Instance                    = static_cast<VkInstance>(*this->context.instance);
                    init_info.PhysicalDevice              = static_cast<VkPhysicalDevice>(*this->context.physical_device);
                    init_info.Device                      = static_cast<VkDevice>(*this->context.device);
                    init_info.QueueFamily                 = this->context.graphics_queue_index;
                    init_info.Queue                       = static_cast<VkQueue>(*this->context.graphics_queue);
                    init_info.DescriptorPool              = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
                    init_info.MinImageCount               = this->imgui.min_image_count;
                    init_info.ImageCount                  = this->imgui.image_count;
                    init_info.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
                    init_info.UseDynamicRendering         = true;
                    init_info.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
                    if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
                    vulkan_backend_initialized = true;
                    this->imgui.initialized    = true;
                } catch (...) {
                    if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
                    if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
                    if (context_created) ImGui::DestroyContext();
                    this->imgui.descriptor_pool = nullptr;
                    this->imgui.color_format    = vk::Format::eUndefined;
                    this->imgui.min_image_count = 2;
                    this->imgui.image_count     = 2;
                    this->imgui.docking         = docking;
                    this->imgui.viewports       = viewports;
                    this->imgui.initialized     = false;
                    throw;
                }
            }
        }
        this->surface.resize_requested = false;
    }
} // namespace xayah
