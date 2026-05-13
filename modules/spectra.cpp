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
import camera;
import scene;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

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

    std::vector<std::uint32_t> read_spirv(const std::filesystem::path& path) {
        std::ifstream file{path, std::ios::binary | std::ios::ate};
        if (!file) throw std::runtime_error(std::string{"Failed to open SPIR-V shader: "} + path.string());

        const std::streamoff byte_count = file.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::string{"Invalid SPIR-V shader size: "} + path.string());

        std::vector<std::uint32_t> code(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(code.data()), byte_count);
        if (!file) throw std::runtime_error(std::string{"Failed to read SPIR-V shader: "} + path.string());
        return code;
    }

    struct VolumeShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 4> camera_step{};
        std::array<float, 4> origin_opacity{};
        std::array<float, 4> spacing_value_min{};
        std::array<std::uint32_t, 4> resolution_kind{};
        std::array<std::uint32_t, 4> mode_options{};
        std::array<float, 4> slice_value_max{};
    };

    struct MeshShaderVertex {
        std::array<float, 4> position{};
        std::array<float, 4> normal{};
        std::array<float, 4> color{};
    };

    struct MeshShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 4> light_direction{};
    };

    struct ParticleShaderParticle {
        std::array<float, 4> position_radius{};
        std::array<float, 4> color{};
    };

    struct ParticleShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 4> camera_right_radius_scale{};
        std::array<float, 4> camera_up_unused{};
    };

    std::uint32_t memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching memory type for buffer");
    }

    void ensure_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& memory, vk::DeviceSize& buffer_size, const vk::DeviceSize requested_size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags required_properties) {
        if (requested_size == 0) throw std::runtime_error("Cannot create zero-size buffer");
        if (*buffer && *memory && buffer_size == requested_size) return;

        vk::raii::Buffer next_buffer{nullptr};
        vk::raii::DeviceMemory next_memory{nullptr};
        const vk::BufferCreateInfo buffer_create_info{{}, requested_size, usage, vk::SharingMode::eExclusive};
        next_buffer = vk::raii::Buffer{device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = next_buffer.getMemoryRequirements();
        const std::uint32_t type_index                   = memory_type_index(physical_device, memory_requirements.memoryTypeBits, required_properties);
        const vk::MemoryAllocateInfo allocate_info{memory_requirements.size, type_index};
        next_memory = vk::raii::DeviceMemory{device, allocate_info};
        next_buffer.bindMemory(*next_memory, 0);

        buffer      = std::move(next_buffer);
        memory      = std::move(next_memory);
        buffer_size = requested_size;
    }

    void write_buffer(const vk::raii::DeviceMemory& memory, const vk::DeviceSize buffer_size, const void* data, const vk::DeviceSize write_size) {
        if (!*memory) throw std::runtime_error("Cannot write unallocated buffer");
        if (write_size > buffer_size) throw std::runtime_error("Buffer write exceeds allocation size");
        void* mapped = memory.mapMemory(0, write_size);
        std::memcpy(mapped, data, static_cast<std::size_t>(write_size));
        memory.unmapMemory();
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
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().shaderDemoteToHelperInvocation) throw std::runtime_error("Device does not support shaderDemoteToHelperInvocation");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}, {}, {}};
            enabled_features.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy            = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceFeatures2>().features.fillModeNonSolid             = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters           = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan12Features>().timelineSemaphore              = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2               = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering               = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().shaderDemoteToHelperInvocation = VK_TRUE;

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
        this->create_viewport_pipeline();
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
        this->destroy_mesh_renderer();
        this->destroy_particles_renderer();
        this->destroy_volume_renderer();
        this->destroy_viewport_pipeline();
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
        this->destroy_volume_renderer();
        this->destroy_particles_renderer();
        this->destroy_mesh_renderer();
        this->destroy_viewport_pipeline();
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

    void Spectra::render(Scene& scene) {
        scene.validate();
        scene.initialize_selection();
        scene.validate_bake();
        if (scene.bake.mode == ScenePlaybackMode::baked) {
            this->timeline.frame_min     = scene.baked_frame_min();
            this->timeline.frame_max     = scene.baked_frame_max();
            this->timeline.current_frame = std::clamp(this->timeline.current_frame, this->timeline.frame_min, this->timeline.frame_max);
            this->timeline.first_frame   = this->timeline.frame_min;
        }

        if (!scene.meshes.empty()) this->create_mesh_renderer(scene);
        if (!scene.particles.empty()) this->create_particles_renderer(scene);
        if (!scene.volumes.empty()) this->create_volume_renderer(scene);
        try {
            while (!glfwWindowShouldClose(this->surface.window.get())) {
                FrameState frame{};
                if (!this->begin_frame(frame, scene)) continue;
                this->record_frame(frame, scene);
                this->end_frame(frame, scene);
            }

            this->context.device.waitIdle();
            this->destroy_volume_renderer();
            this->destroy_particles_renderer();
            this->destroy_mesh_renderer();
        } catch (...) {
            try {
                if (*this->context.device) this->context.device.waitIdle();
            } catch (...) {
            }
            this->destroy_volume_renderer();
            this->destroy_particles_renderer();
            this->destroy_mesh_renderer();
            throw;
        }
    }

    bool Spectra::begin_frame(FrameState& frame, const Scene& scene) {
        glfwPollEvents();
        if (this->surface.resize_requested) {
            this->recreate_swapchain(scene);
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
            this->recreate_swapchain(scene);
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
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");

        ImGuiIO& io                 = ImGui::GetIO();
        const ImVec2 mouse_position = io.MousePos;
        const float timeline_top    = viewport->WorkPos.y + viewport->WorkSize.y - this->timeline.height;
        const bool in_viewport      = mouse_position.x >= viewport->WorkPos.x && mouse_position.x < viewport->WorkPos.x + viewport->WorkSize.x && mouse_position.y >= viewport->WorkPos.y && mouse_position.y < timeline_top && !io.WantCaptureMouse;
        const bool right_mouse      = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool middle_mouse     = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        const bool left_mouse       = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool shift            = io.KeyShift;
        const bool alt              = io.KeyAlt;

        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) this->viewport.grid_visible = !this->viewport.grid_visible;
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_H, false)) this->viewport.camera.reset_home();

        CameraInput input{};
        input.delta_seconds = io.DeltaTime;
        input.mouse_delta_x = in_viewport ? io.MouseDelta.x : 0.0f;
        input.mouse_delta_y = in_viewport ? io.MouseDelta.y : 0.0f;
        input.mouse_wheel   = in_viewport ? io.MouseWheel : 0.0f;
        input.orbit         = in_viewport && ((right_mouse && !shift) || (alt && left_mouse));
        input.pan           = in_viewport && (middle_mouse || (right_mouse && shift));
        input.fly           = in_viewport && right_mouse && !shift;
        input.move_forward  = ImGui::IsKeyDown(ImGuiKey_W);
        input.move_backward = ImGui::IsKeyDown(ImGuiKey_S);
        input.move_left     = ImGui::IsKeyDown(ImGuiKey_A);
        input.move_right    = ImGui::IsKeyDown(ImGuiKey_D);
        input.move_up       = ImGui::IsKeyDown(ImGuiKey_E);
        input.move_down     = ImGui::IsKeyDown(ImGuiKey_Q);
        this->viewport.camera.update(input);
        return true;
    }

    void Spectra::record_frame(const FrameState& frame, Scene& scene) {
        if (this->timeline.frame_min > this->timeline.frame_max) throw std::runtime_error("Invalid timeline frame range");
        this->timeline.current_frame = std::clamp(this->timeline.current_frame, this->timeline.frame_min, this->timeline.frame_max);
        this->timeline.first_frame   = std::clamp(this->timeline.first_frame, this->timeline.frame_min, this->timeline.frame_max);
        scene.apply_playback_frame(this->timeline.current_frame);

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);

        {
            constexpr vk::PipelineStageFlags2 depth_stages = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
            constexpr vk::AccessFlags2 depth_access        = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
            const std::array attachment_barriers{
                vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eAllCommands,
                    {},
                    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                    vk::AccessFlagBits2::eColorAttachmentWrite,
                    this->swapchain.image_layouts[frame.image_index],
                    vk::ImageLayout::eColorAttachmentOptimal,
                    VK_QUEUE_FAMILY_IGNORED,
                    VK_QUEUE_FAMILY_IGNORED,
                    this->swapchain.images[frame.image_index],
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
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;
        this->swapchain.depth_layout                     = vk::ImageLayout::eDepthStencilAttachmentOptimal;

        constexpr vk::ClearValue color_clear_value{vk::ClearColorValue{std::array{0.02f, 0.02f, 0.025f, 1.0f}}};
        constexpr vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->swapchain.image_views[frame.image_index],
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
        const vk::RenderingInfo scene_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, &depth_attachment, stencil_attachment};
        command_buffer.beginRendering(scene_rendering_info);

        if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Cannot render viewport with zero swapchain extent");

        const vk::Viewport vulkan_viewport{0.0f, 0.0f, static_cast<float>(this->swapchain.extent.width), static_cast<float>(this->swapchain.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->swapchain.extent};
        const float aspect                          = static_cast<float>(this->swapchain.extent.width) / static_cast<float>(this->swapchain.extent.height);
        const std::array<float, 16> view_projection = this->viewport.camera.view_projection(aspect);

        if (this->viewport.grid_visible) {
            if (!*this->viewport.pipeline_layout || !*this->viewport.pipeline) throw std::runtime_error("Viewport pipeline is not initialized");

            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->viewport.pipeline);
            command_buffer.setViewport(0, vulkan_viewport);
            command_buffer.setScissor(0, scissor);
            command_buffer.pushConstants(*this->viewport.pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const float>{view_projection});
            command_buffer.draw(this->viewport.vertex_count, 1, 0, 0);
        }

        const std::array<float, 3> camera_position = this->viewport.camera.position();
        const std::array<float, 3> camera_right    = this->viewport.camera.right();
        const std::array<float, 3> camera_up       = this->viewport.camera.up();
        constexpr vk::BufferUsageFlags storage_buffer_usage{vk::BufferUsageFlagBits::eStorageBuffer};
        constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

        command_buffer.setViewport(0, vulkan_viewport);
        command_buffer.setScissor(0, scissor);

        const std::size_t scene_mesh_count      = scene.meshes.size();
        const std::size_t scene_particles_count = scene.particles.size();
        const std::size_t scene_volume_count    = scene.volumes.size();
        if (frame.frame_index >= this->sync.frame_count) throw std::runtime_error("Frame index is outside frame resource range");

        if (scene_mesh_count != 0) {
            if (!*this->mesh_renderer.pipeline_layout || !*this->mesh_renderer.surface_pipeline || !*this->mesh_renderer.wireframe_pipeline || this->mesh_renderer.descriptor_sets.size() == 0) throw std::runtime_error("Mesh renderer is not initialized");
            if (this->mesh_renderer.frame_resources.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_mesh_count) throw std::runtime_error("Mesh renderer resources do not match scene mesh count");
            if (this->mesh_renderer.descriptor_sets.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_mesh_count) throw std::runtime_error("Mesh descriptor sets do not match scene mesh count");

            for (std::size_t mesh_index = 0; mesh_index < scene_mesh_count; ++mesh_index) {
                const Mesh& mesh = scene.meshes[mesh_index];
                if (!mesh.visible) continue;
                const std::size_t resource_index = static_cast<std::size_t>(frame.frame_index) * scene_mesh_count + mesh_index;
                MeshDrawResources& resources     = this->mesh_renderer.frame_resources.at(resource_index);
                std::vector<std::uint32_t> wireframe_indices{};
                const std::uint32_t* draw_indices = mesh.indices.data();
                std::size_t draw_index_count      = mesh.indices.size();
                if (mesh.render_settings.display_mode == MeshDisplayMode::wireframe) {
                    if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 2)) throw std::runtime_error(std::string{"Mesh has too many indices for wireframe draw: "} + mesh.name);
                    wireframe_indices.reserve(mesh.indices.size() * 2);
                    for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
                        const std::uint32_t i0 = mesh.indices[index + 0];
                        const std::uint32_t i1 = mesh.indices[index + 1];
                        const std::uint32_t i2 = mesh.indices[index + 2];
                        wireframe_indices.emplace_back(i0);
                        wireframe_indices.emplace_back(i1);
                        wireframe_indices.emplace_back(i1);
                        wireframe_indices.emplace_back(i2);
                        wireframe_indices.emplace_back(i2);
                        wireframe_indices.emplace_back(i0);
                    }
                    draw_indices     = wireframe_indices.data();
                    draw_index_count = wireframe_indices.size();
                    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.wireframe_pipeline);
                } else {
                    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.surface_pipeline);
                }

                std::vector<MeshShaderVertex> shader_vertices{};
                shader_vertices.reserve(mesh.vertices.size());
                for (const MeshVertex& vertex : mesh.vertices) {
                    shader_vertices.emplace_back(MeshShaderVertex{
                        {vertex.position[0], vertex.position[1], vertex.position[2], 1.0f},
                        {vertex.normal[0], vertex.normal[1], vertex.normal[2], 0.0f},
                        {vertex.color[0], vertex.color[1], vertex.color[2], 1.0f},
                    });
                }

                MeshShaderParameters parameters{};
                parameters.view_projection = view_projection;
                parameters.light_direction = {-0.45f, -0.85f, -0.25f, 0.0f};

                ensure_buffer(this->context.physical_device, this->context.device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, shader_vertices.size() * sizeof(MeshShaderVertex), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(this->context.physical_device, this->context.device, resources.index_buffer, resources.index_memory, resources.index_size, draw_index_count * sizeof(std::uint32_t), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(this->context.physical_device, this->context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(MeshShaderParameters), storage_buffer_usage, upload_memory_properties);
                write_buffer(resources.vertex_memory, resources.vertex_size, shader_vertices.data(), shader_vertices.size() * sizeof(MeshShaderVertex));
                write_buffer(resources.index_memory, resources.index_size, draw_indices, draw_index_count * sizeof(std::uint32_t));
                write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(MeshShaderParameters));

                const std::array buffer_infos{
                    vk::DescriptorBufferInfo{*resources.vertex_buffer, 0, resources.vertex_size},
                    vk::DescriptorBufferInfo{*resources.index_buffer, 0, resources.index_size},
                    vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
                };
                const vk::DescriptorSet descriptor_set = *this->mesh_renderer.descriptor_sets[resource_index];
                const std::array writes{
                    vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
                    vk::WriteDescriptorSet{descriptor_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1]},
                    vk::WriteDescriptorSet{descriptor_set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[2]},
                };
                this->context.device.updateDescriptorSets(writes, {});

                command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
                command_buffer.draw(static_cast<std::uint32_t>(draw_index_count), 1, 0, 0);
            }
        }

        if (scene_particles_count != 0) {
            if (!*this->particles_renderer.pipeline_layout || !*this->particles_renderer.pipeline || this->particles_renderer.descriptor_sets.size() == 0) throw std::runtime_error("Particles renderer is not initialized");
            if (this->particles_renderer.frame_resources.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_particles_count) throw std::runtime_error("Particles renderer resources do not match scene particles count");
            if (this->particles_renderer.descriptor_sets.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_particles_count) throw std::runtime_error("Particles descriptor sets do not match scene particles count");

            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->particles_renderer.pipeline);
            for (std::size_t particles_index = 0; particles_index < scene_particles_count; ++particles_index) {
                const Particles& particles = scene.particles[particles_index];
                if (!particles.visible || particles.particles.empty()) continue;
                if (particles.render_settings.radius_scale <= 0.0f) throw std::runtime_error(std::string{"Particles radius scale must be positive: "} + particles.name);
                if (particles.particles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / 6)) throw std::runtime_error(std::string{"Particles object has too many particles for draw: "} + particles.name);

                const std::size_t resource_index = static_cast<std::size_t>(frame.frame_index) * scene_particles_count + particles_index;
                ParticleDrawResources& resources = this->particles_renderer.frame_resources.at(resource_index);

                std::vector<ParticleShaderParticle> shader_particles{};
                shader_particles.reserve(particles.particles.size());
                for (const Particle& particle : particles.particles) {
                    if (particle.radius <= 0.0f) throw std::runtime_error(std::string{"Particle radius must be positive: "} + particles.name);
                    shader_particles.emplace_back(ParticleShaderParticle{
                        {particle.position[0], particle.position[1], particle.position[2], particle.radius},
                        {particle.color[0], particle.color[1], particle.color[2], 1.0f},
                    });
                }

                ParticleShaderParameters parameters{};
                parameters.view_projection           = view_projection;
                parameters.camera_right_radius_scale = {camera_right[0], camera_right[1], camera_right[2], particles.render_settings.radius_scale};
                parameters.camera_up_unused          = {camera_up[0], camera_up[1], camera_up[2], 0.0f};

                ensure_buffer(this->context.physical_device, this->context.device, resources.particle_buffer, resources.particle_memory, resources.particle_size, shader_particles.size() * sizeof(ParticleShaderParticle), storage_buffer_usage, upload_memory_properties);
                ensure_buffer(this->context.physical_device, this->context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(ParticleShaderParameters), storage_buffer_usage, upload_memory_properties);
                write_buffer(resources.particle_memory, resources.particle_size, shader_particles.data(), shader_particles.size() * sizeof(ParticleShaderParticle));
                write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(ParticleShaderParameters));

                const std::array buffer_infos{
                    vk::DescriptorBufferInfo{*resources.particle_buffer, 0, resources.particle_size},
                    vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
                };
                const vk::DescriptorSet descriptor_set = *this->particles_renderer.descriptor_sets[resource_index];
                const std::array writes{
                    vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
                    vk::WriteDescriptorSet{descriptor_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1]},
                };
                this->context.device.updateDescriptorSets(writes, {});

                command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->particles_renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
                command_buffer.draw(static_cast<std::uint32_t>(particles.particles.size() * 6), 1, 0, 0);
            }
        }

        if (scene_volume_count != 0) {
            if (!*this->volume_renderer.pipeline_layout || !*this->volume_renderer.pipeline || this->volume_renderer.descriptor_sets.size() == 0) throw std::runtime_error("Volume renderer is not initialized");
            if (this->volume_renderer.frame_resources.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_volume_count) throw std::runtime_error("Volume renderer resources do not match scene volume count");
            if (this->volume_renderer.descriptor_sets.size() != static_cast<std::size_t>(this->sync.frame_count) * scene_volume_count) throw std::runtime_error("Volume descriptor sets do not match scene volume count");

            command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->volume_renderer.pipeline);
            for (std::size_t volume_index = 0; volume_index < scene_volume_count; ++volume_index) {
                const Volume& volume = scene.volumes[volume_index];
                if (!volume.visible) continue;
                const VolumeRenderSettings& render_settings = volume.render_settings;
                if (render_settings.opacity < 0.0f || render_settings.opacity > 1.0f) throw std::runtime_error(std::string{"Volume opacity must be in [0, 1]: "} + volume.name);
                if (render_settings.raymarch_step <= 0.0f) throw std::runtime_error(std::string{"Volume raymarch step must be positive: "} + volume.name);
                if (render_settings.value_min >= render_settings.value_max) throw std::runtime_error(std::string{"Volume value range is invalid: "} + volume.name);
                if (render_settings.slice_position < 0.0f || render_settings.slice_position > 1.0f) throw std::runtime_error(std::string{"Volume slice position must be in [0, 1]: "} + volume.name);

                const std::size_t resource_index = static_cast<std::size_t>(frame.frame_index) * scene_volume_count + volume_index;
                VolumeDrawResources& resources   = this->volume_renderer.frame_resources.at(resource_index);

                if (render_settings.grid_kind == VolumeGridKind::centered_scalar) {
                    const CenteredScalarGrid& grid = scene.render_centered_scalar_grid(volume);
                    const std::array spacing{
                        volume.size[0] / static_cast<float>(grid.resolution[0]),
                        volume.size[1] / static_cast<float>(grid.resolution[1]),
                        volume.size[2] / static_cast<float>(grid.resolution[2]),
                    };

                    VolumeShaderParameters parameters{};
                    parameters.view_projection   = view_projection;
                    parameters.camera_step       = {camera_position[0], camera_position[1], camera_position[2], render_settings.raymarch_step};
                    parameters.origin_opacity    = {volume.origin[0], volume.origin[1], volume.origin[2], render_settings.opacity};
                    parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], render_settings.value_min};
                    parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::centered_scalar)};
                    parameters.mode_options      = {static_cast<std::uint32_t>(render_settings.display_mode), static_cast<std::uint32_t>(render_settings.slice_axis), static_cast<std::uint32_t>(render_settings.color_map), 0};
                    parameters.slice_value_max   = {render_settings.slice_position, render_settings.value_max, 0.0f, 0.0f};

                    ensure_buffer(this->context.physical_device, this->context.device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                    ensure_buffer(this->context.physical_device, this->context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
                    write_buffer(resources.x_data_memory, resources.x_data_size, grid.values.data(), grid.values.size() * sizeof(float));
                    write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
                } else {
                    const StaggeredVectorGrid& grid = scene.render_staggered_vector_grid(volume);
                    const std::array spacing{
                        volume.size[0] / static_cast<float>(grid.resolution[0]),
                        volume.size[1] / static_cast<float>(grid.resolution[1]),
                        volume.size[2] / static_cast<float>(grid.resolution[2]),
                    };

                    VolumeShaderParameters parameters{};
                    parameters.view_projection   = view_projection;
                    parameters.camera_step       = {camera_position[0], camera_position[1], camera_position[2], render_settings.raymarch_step};
                    parameters.origin_opacity    = {volume.origin[0], volume.origin[1], volume.origin[2], render_settings.opacity};
                    parameters.spacing_value_min = {spacing[0], spacing[1], spacing[2], render_settings.value_min};
                    parameters.resolution_kind   = {grid.resolution[0], grid.resolution[1], grid.resolution[2], static_cast<std::uint32_t>(VolumeGridKind::staggered_vector)};
                    parameters.mode_options      = {static_cast<std::uint32_t>(render_settings.display_mode), static_cast<std::uint32_t>(render_settings.slice_axis), static_cast<std::uint32_t>(render_settings.color_map), 0};
                    parameters.slice_value_max   = {render_settings.slice_position, render_settings.value_max, 0.0f, 0.0f};

                    ensure_buffer(this->context.physical_device, this->context.device, resources.x_data_buffer, resources.x_data_memory, resources.x_data_size, grid.x_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                    ensure_buffer(this->context.physical_device, this->context.device, resources.y_data_buffer, resources.y_data_memory, resources.y_data_size, grid.y_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                    ensure_buffer(this->context.physical_device, this->context.device, resources.z_data_buffer, resources.z_data_memory, resources.z_data_size, grid.z_values.size() * sizeof(float), storage_buffer_usage, upload_memory_properties);
                    ensure_buffer(this->context.physical_device, this->context.device, resources.parameters_buffer, resources.parameters_memory, resources.parameters_size, sizeof(VolumeShaderParameters), storage_buffer_usage, upload_memory_properties);
                    write_buffer(resources.x_data_memory, resources.x_data_size, grid.x_values.data(), grid.x_values.size() * sizeof(float));
                    write_buffer(resources.y_data_memory, resources.y_data_size, grid.y_values.data(), grid.y_values.size() * sizeof(float));
                    write_buffer(resources.z_data_memory, resources.z_data_size, grid.z_values.data(), grid.z_values.size() * sizeof(float));
                    write_buffer(resources.parameters_memory, resources.parameters_size, &parameters, sizeof(VolumeShaderParameters));
                }

                const vk::raii::Buffer& y_data_buffer = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.y_data_buffer;
                const vk::raii::Buffer& z_data_buffer = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_buffer : resources.z_data_buffer;
                const vk::DeviceSize y_data_size      = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.y_data_size;
                const vk::DeviceSize z_data_size      = render_settings.grid_kind == VolumeGridKind::centered_scalar ? resources.x_data_size : resources.z_data_size;
                const std::array buffer_infos{
                    vk::DescriptorBufferInfo{*resources.x_data_buffer, 0, resources.x_data_size},
                    vk::DescriptorBufferInfo{*y_data_buffer, 0, y_data_size},
                    vk::DescriptorBufferInfo{*z_data_buffer, 0, z_data_size},
                    vk::DescriptorBufferInfo{*resources.parameters_buffer, 0, resources.parameters_size},
                };
                const vk::DescriptorSet descriptor_set = *this->volume_renderer.descriptor_sets[resource_index];
                const std::array writes{
                    vk::WriteDescriptorSet{descriptor_set, 0, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[0]},
                    vk::WriteDescriptorSet{descriptor_set, 1, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[1]},
                    vk::WriteDescriptorSet{descriptor_set, 2, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[2]},
                    vk::WriteDescriptorSet{descriptor_set, 3, 0, 1, vk::DescriptorType::eStorageBuffer, nullptr, &buffer_infos[3]},
                };
                this->context.device.updateDescriptorSets(writes, {});

                const std::uint32_t volume_vertex_count = render_settings.display_mode == VolumeDisplayMode::direct ? 36u : 6u;
                command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->volume_renderer.pipeline_layout, 0, vk::ArrayProxy<const vk::DescriptorSet>{descriptor_set}, {});
                command_buffer.draw(volume_vertex_count, 1, 0, 0);
            }
        }
        command_buffer.endRendering();

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");

        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(ImVec2{main_viewport->WorkPos.x + 12.0f, main_viewport->WorkPos.y + 12.0f}, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.02f);
        constexpr ImGuiWindowFlags stats_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{14.0f, 12.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.035f, 0.045f, 0.055f, 0.62f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.28f, 0.55f, 0.90f, 0.55f});
        ImGui::Begin("Spectra Stats", nullptr, stats_window_flags);
        {
            const ImVec2 window_pos  = ImGui::GetWindowPos();
            const ImVec2 window_size = ImGui::GetWindowSize();
            ImGui::GetWindowDrawList()->AddRectFilled(window_pos, ImVec2{window_pos.x + 4.0f, window_pos.y + window_size.y}, IM_COL32(76, 158, 255, 210), 8.0f, ImDrawFlags_RoundCornersLeft);
        }

        const ImVec4 label_color{0.58f, 0.66f, 0.75f, 1.0f};
        const ImVec4 value_color{0.92f, 0.96f, 1.0f, 1.0f};
        const ImVec4 accent_color{0.43f, 0.70f, 1.0f, 1.0f};
        const ImVec4 muted_color{0.70f, 0.76f, 0.82f, 1.0f};
        const float fps        = ImGui::GetIO().Framerate;
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
            ImGui::TextColored(label_color, "Playback");
            ImGui::TableNextColumn();
            ImGui::TextColored(value_color, scene.bake.mode == ScenePlaybackMode::baked ? "Baked" : "Live");

            if (scene.bake.mode == ScenePlaybackMode::baked) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextColored(label_color, "Bake frames");
                ImGui::TableNextColumn();
                ImGui::TextColored(value_color, "%zu", scene.bake.frames.size());
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(label_color, "FPS");
            ImGui::TableNextColumn();
            ImGui::TextColored(fps_color, "%.1f", fps);
            ImGui::EndTable();
        }

        const std::size_t volume_count    = scene.volumes.size();
        const std::size_t mesh_count      = scene.meshes.size();
        const std::size_t particles_count = scene.particles.size();
        const std::size_t object_count    = volume_count + mesh_count + particles_count;
        ImGui::Separator();
        ImGui::TextColored(accent_color, "Scene");
        ImGui::SameLine();
        ImGui::TextColored(muted_color, "%zu total / %zu volume%s / %zu mesh%s / %zu particle set%s", object_count, volume_count, volume_count == 1 ? "" : "s", mesh_count, mesh_count == 1 ? "" : "es", particles_count, particles_count == 1 ? "" : "s");
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{0.18f, 0.42f, 0.72f, 0.24f});
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4{0.22f, 0.50f, 0.86f, 0.34f});
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4{0.28f, 0.58f, 0.96f, 0.44f});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{0.0f, 3.0f});
        if (ImGui::BeginTable("SceneObjectList", 3, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            for (Volume& volume : scene.volumes) {
                const bool selected             = scene.selection.object_id == volume.id;
                const std::size_t scalars       = volume.centered_scalar_grids.size();
                const std::size_t vectors       = volume.staggered_vector_grids.size();
                const std::string id            = std::to_string(volume.id);
                const std::string label         = std::string{"Volume  "} + volume.name + "  " + std::to_string(scalars) + " scalar, " + std::to_string(vectors) + " vector##SceneVolumeSelect:" + id;
                const std::string visible_label = std::string{volume.visible ? "V" : "H"} + "##SceneVolumeVisible:" + id;
                const char* mode_text           = volume.render_settings.display_mode == VolumeDisplayMode::direct ? "D" : "S";
                const std::string mode_label    = std::string{mode_text} + "##SceneVolumeMode:" + id;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : volume.visible ? value_color : muted_color);
                if (ImGui::Selectable(label.c_str(), selected)) scene.select_object(volume.id);
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_Border, volume.visible ? ImVec4{0.24f, 0.82f, 0.55f, 0.78f} : ImVec4{0.86f, 0.30f, 0.32f, 0.78f});
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                if (ImGui::Button(visible_label.c_str(), ImVec2{26.0f, 22.0f})) volume.visible = !volume.visible;
                ImGui::PopStyleColor(5);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(volume.visible ? "Hide volume" : "Show volume");
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_Border, volume.render_settings.display_mode == VolumeDisplayMode::direct ? ImVec4{0.32f, 0.62f, 0.96f, 0.78f} : ImVec4{0.92f, 0.62f, 0.26f, 0.78f});
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                if (ImGui::Button(mode_label.c_str(), ImVec2{28.0f, 0.0f})) volume.render_settings.display_mode = volume.render_settings.display_mode == VolumeDisplayMode::direct ? VolumeDisplayMode::slice : VolumeDisplayMode::direct;
                ImGui::PopStyleColor(5);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(volume.render_settings.display_mode == VolumeDisplayMode::direct ? "Direct volume rendering" : "Slice volume rendering");
            }
            for (Mesh& mesh : scene.meshes) {
                const bool selected             = scene.selection.object_id == mesh.id;
                const std::size_t triangles     = mesh.indices.size() / 3;
                const std::string id            = std::to_string(mesh.id);
                const std::string label         = std::string{"Mesh  "} + mesh.name + "  " + std::to_string(mesh.vertices.size()) + " vertices, " + std::to_string(triangles) + " tris##SceneMeshSelect:" + id;
                const std::string visible_label = std::string{mesh.visible ? "V" : "H"} + "##SceneMeshVisible:" + id;
                const char* mode_text           = mesh.render_settings.display_mode == MeshDisplayMode::surface ? "S" : "W";
                const std::string mode_label    = std::string{mode_text} + "##SceneMeshMode:" + id;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : mesh.visible ? value_color : muted_color);
                if (ImGui::Selectable(label.c_str(), selected)) scene.select_object(mesh.id);
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_Border, mesh.visible ? ImVec4{0.24f, 0.82f, 0.55f, 0.78f} : ImVec4{0.86f, 0.30f, 0.32f, 0.78f});
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                if (ImGui::Button(visible_label.c_str(), ImVec2{26.0f, 22.0f})) mesh.visible = !mesh.visible;
                ImGui::PopStyleColor(5);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(mesh.visible ? "Hide mesh" : "Show mesh");
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_Border, mesh.render_settings.display_mode == MeshDisplayMode::surface ? ImVec4{0.66f, 0.48f, 0.96f, 0.78f} : ImVec4{0.28f, 0.80f, 0.88f, 0.78f});
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                if (ImGui::Button(mode_label.c_str(), ImVec2{28.0f, 0.0f})) mesh.render_settings.display_mode = mesh.render_settings.display_mode == MeshDisplayMode::surface ? MeshDisplayMode::wireframe : MeshDisplayMode::surface;
                ImGui::PopStyleColor(5);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(mesh.render_settings.display_mode == MeshDisplayMode::surface ? "Surface mesh rendering" : "Wireframe mesh rendering");
            }
            for (Particles& particles : scene.particles) {
                const bool selected             = scene.selection.object_id == particles.id;
                const std::string id            = std::to_string(particles.id);
                const std::string label         = std::string{"Particles  "} + particles.name + "  " + std::to_string(particles.particles.size()) + " particles##SceneParticlesSelect:" + id;
                const std::string visible_label = std::string{particles.visible ? "V" : "H"} + "##SceneParticlesVisible:" + id;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : particles.visible ? value_color : muted_color);
                if (ImGui::Selectable(label.c_str(), selected)) scene.select_object(particles.id);
                ImGui::PopStyleColor();
                ImGui::TableNextColumn();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.0f, 0.0f, 0.0f, 0.0f});
                ImGui::PushStyleColor(ImGuiCol_Border, particles.visible ? ImVec4{0.24f, 0.82f, 0.55f, 0.78f} : ImVec4{0.86f, 0.30f, 0.32f, 0.78f});
                ImGui::PushStyleColor(ImGuiCol_Text, value_color);
                if (ImGui::Button(visible_label.c_str(), ImVec2{26.0f, 22.0f})) particles.visible = !particles.visible;
                ImGui::PopStyleColor(5);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip(particles.visible ? "Hide particles" : "Show particles");
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(3);
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (scene.selection.object_id != 0) {
            const float inspector_window_width      = 360.0f;
            const float inspector_window_max_height = main_viewport->WorkSize.y - this->timeline.height - 24.0f;
            if (inspector_window_max_height <= 240.0f) throw std::runtime_error("Viewport is too small for fixed object inspector");
            ImGui::SetNextWindowViewport(main_viewport->ID);
            ImGui::SetNextWindowPos(ImVec2{main_viewport->WorkPos.x + main_viewport->WorkSize.x - inspector_window_width - 12.0f, main_viewport->WorkPos.y + 12.0f}, ImGuiCond_Always);
            ImGui::SetNextWindowSizeConstraints(ImVec2{inspector_window_width, 0.0f}, ImVec2{inspector_window_width, inspector_window_max_height});
            ImGui::SetNextWindowBgAlpha(0.18f);
            constexpr ImGuiWindowFlags inspector_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{16.0f, 14.0f});
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0.035f, 0.040f, 0.048f, 0.48f});
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0.35f, 0.62f, 0.95f, 0.42f});
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{0.08f, 0.095f, 0.11f, 0.42f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4{0.13f, 0.18f, 0.24f, 0.56f});
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4{0.18f, 0.26f, 0.34f, 0.68f});
            ImGui::Begin("Object Inspector", nullptr, inspector_window_flags);

            const SceneObjectRef active_object = scene.selected_object_ref();
            if (active_object.kind == SceneObjectKind::volume) {
                Volume& active_volume          = scene.volumes.at(active_object.index);
                VolumeRenderSettings& settings = active_volume.render_settings;

                ImGui::TextColored(accent_color, "Object Inspector");
                ImGui::SameLine();
                ImGui::TextColored(muted_color, "Volume");
                ImGui::Separator();

                if (ImGui::BeginTable("InspectorIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Name");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%s", active_volume.name.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Origin");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", active_volume.origin[0], active_volume.origin[1], active_volume.origin[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Size");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", active_volume.size[0], active_volume.size[1], active_volume.size[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Grids");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%zu scalar / %zu vector", active_volume.centered_scalar_grids.size(), active_volume.staggered_vector_grids.size());
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Grids");
                if (ImGui::BeginTable("InspectorGridList", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Resolution");
                    ImGui::TableHeadersRow();
                    for (const CenteredScalarGrid& grid : active_volume.centered_scalar_grids) {
                        const bool selected     = settings.grid_kind == VolumeGridKind::centered_scalar && grid.name == settings.grid_name;
                        const std::string label = std::string{"Scalar##VolumeGridScalar:"} + grid.name;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : label_color);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            settings.grid_kind = VolumeGridKind::centered_scalar;
                            settings.grid_name = grid.name;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopStyleColor();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(selected ? accent_color : value_color, "%s", grid.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextColored(muted_color, "%u x %u x %u", grid.resolution[0], grid.resolution[1], grid.resolution[2]);
                    }
                    for (const StaggeredVectorGrid& grid : active_volume.staggered_vector_grids) {
                        const bool selected     = settings.grid_kind == VolumeGridKind::staggered_vector && grid.name == settings.grid_name;
                        const std::string label = std::string{"Vector##VolumeGridVector:"} + grid.name;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::PushStyleColor(ImGuiCol_Text, selected ? accent_color : label_color);
                        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                            settings.grid_kind = VolumeGridKind::staggered_vector;
                            settings.grid_name = grid.name;
                        }
                        if (selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopStyleColor();
                        ImGui::TableNextColumn();
                        ImGui::TextColored(selected ? accent_color : value_color, "%s", grid.name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextColored(muted_color, "%u x %u x %u", grid.resolution[0], grid.resolution[1], grid.resolution[2]);
                    }
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Render");
                const char* mode_label = settings.display_mode == VolumeDisplayMode::direct ? "Direct" : "Slice";
                if (ImGui::BeginCombo("Mode", mode_label)) {
                    const bool direct_selected = settings.display_mode == VolumeDisplayMode::direct;
                    if (ImGui::Selectable("Direct", direct_selected)) settings.display_mode = VolumeDisplayMode::direct;
                    if (direct_selected) ImGui::SetItemDefaultFocus();

                    const bool slice_selected = settings.display_mode == VolumeDisplayMode::slice;
                    if (ImGui::Selectable("Slice", slice_selected)) settings.display_mode = VolumeDisplayMode::slice;
                    if (slice_selected) ImGui::SetItemDefaultFocus();
                    ImGui::EndCombo();
                }

                auto color_map_label = "Viridis";
                if (settings.color_map == VolumeColorMap::grayscale) color_map_label = "Grayscale";
                if (settings.color_map == VolumeColorMap::turbo) color_map_label = "Turbo";
                if (settings.color_map == VolumeColorMap::heat) color_map_label = "Heat";
                if (ImGui::BeginCombo("Color Map", color_map_label)) {
                    const bool grayscale_selected = settings.color_map == VolumeColorMap::grayscale;
                    if (ImGui::Selectable("Grayscale", grayscale_selected)) settings.color_map = VolumeColorMap::grayscale;
                    if (grayscale_selected) ImGui::SetItemDefaultFocus();

                    const bool viridis_selected = settings.color_map == VolumeColorMap::viridis;
                    if (ImGui::Selectable("Viridis", viridis_selected)) settings.color_map = VolumeColorMap::viridis;
                    if (viridis_selected) ImGui::SetItemDefaultFocus();

                    const bool turbo_selected = settings.color_map == VolumeColorMap::turbo;
                    if (ImGui::Selectable("Turbo", turbo_selected)) settings.color_map = VolumeColorMap::turbo;
                    if (turbo_selected) ImGui::SetItemDefaultFocus();

                    const bool heat_selected = settings.color_map == VolumeColorMap::heat;
                    if (ImGui::Selectable("Heat", heat_selected)) settings.color_map = VolumeColorMap::heat;
                    if (heat_selected) ImGui::SetItemDefaultFocus();
                    ImGui::EndCombo();
                }

                if (settings.display_mode == VolumeDisplayMode::slice) {
                    auto axis_label = "Y";
                    if (settings.slice_axis == VolumeSliceAxis::x) axis_label = "X";
                    if (settings.slice_axis == VolumeSliceAxis::z) axis_label = "Z";
                    if (ImGui::BeginCombo("Axis", axis_label)) {
                        const bool x_selected = settings.slice_axis == VolumeSliceAxis::x;
                        if (ImGui::Selectable("X", x_selected)) settings.slice_axis = VolumeSliceAxis::x;
                        if (x_selected) ImGui::SetItemDefaultFocus();

                        const bool y_selected = settings.slice_axis == VolumeSliceAxis::y;
                        if (ImGui::Selectable("Y", y_selected)) settings.slice_axis = VolumeSliceAxis::y;
                        if (y_selected) ImGui::SetItemDefaultFocus();

                        const bool z_selected = settings.slice_axis == VolumeSliceAxis::z;
                        if (ImGui::Selectable("Z", z_selected)) settings.slice_axis = VolumeSliceAxis::z;
                        if (z_selected) ImGui::SetItemDefaultFocus();
                        ImGui::EndCombo();
                    }
                    ImGui::SliderFloat("Slice", &settings.slice_position, 0.0f, 1.0f, "%.3f");
                }

                std::array value_range{settings.value_min, settings.value_max};
                if (ImGui::InputFloat2("Value Range", value_range.data())) {
                    settings.value_min = value_range[0];
                    settings.value_max = value_range[1];
                }
                ImGui::SliderFloat("Opacity", &settings.opacity, 0.0f, 1.0f, "%.3f");
                ImGui::InputFloat("Raymarch Step", &settings.raymarch_step, 0.001f, 0.01f, "%.4f");
            } else if (active_object.kind == SceneObjectKind::mesh) {
                Mesh& active_mesh            = scene.meshes.at(active_object.index);
                MeshRenderSettings& settings = active_mesh.render_settings;
                if (active_mesh.vertices.empty()) throw std::runtime_error(std::string{"Selected mesh has no vertices: "} + active_mesh.name);

                std::array<float, 3> bounds_min = active_mesh.vertices.front().position;
                std::array<float, 3> bounds_max = active_mesh.vertices.front().position;
                for (const MeshVertex& vertex : active_mesh.vertices) {
                    for (std::size_t axis = 0; axis < 3; ++axis) {
                        if (vertex.position[axis] < bounds_min[axis]) bounds_min[axis] = vertex.position[axis];
                        if (vertex.position[axis] > bounds_max[axis]) bounds_max[axis] = vertex.position[axis];
                    }
                }

                ImGui::TextColored(accent_color, "Object Inspector");
                ImGui::SameLine();
                ImGui::TextColored(muted_color, "Mesh");
                ImGui::Separator();

                if (ImGui::BeginTable("InspectorMeshIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Name");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%s", active_mesh.name.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Vertices");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%zu", active_mesh.vertices.size());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Triangles");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%zu", active_mesh.indices.size() / 3);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Bounds min");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", bounds_min[0], bounds_min[1], bounds_min[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Bounds max");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", bounds_max[0], bounds_max[1], bounds_max[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Playback");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, scene.bake.mode == ScenePlaybackMode::baked ? "Baked vertices" : "Live vertices");
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Render");
                const char* mode_label = settings.display_mode == MeshDisplayMode::surface ? "Surface" : "Wireframe";
                if (ImGui::BeginCombo("Mode", mode_label)) {
                    const bool surface_selected = settings.display_mode == MeshDisplayMode::surface;
                    if (ImGui::Selectable("Surface", surface_selected)) settings.display_mode = MeshDisplayMode::surface;
                    if (surface_selected) ImGui::SetItemDefaultFocus();

                    const bool wireframe_selected = settings.display_mode == MeshDisplayMode::wireframe;
                    if (ImGui::Selectable("Wireframe", wireframe_selected)) settings.display_mode = MeshDisplayMode::wireframe;
                    if (wireframe_selected) ImGui::SetItemDefaultFocus();
                    ImGui::EndCombo();
                }

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Vertex Format");
                if (ImGui::BeginTable("InspectorMeshFormat", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Position");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float3");

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Normal");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float3");

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Color");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float3");
                    ImGui::EndTable();
                }
            } else if (active_object.kind == SceneObjectKind::particles) {
                Particles& active_particles      = scene.particles.at(active_object.index);
                ParticleRenderSettings& settings = active_particles.render_settings;

                std::array<float, 3> bounds_min{};
                std::array<float, 3> bounds_max{};
                float radius_min = 0.0f;
                float radius_max = 0.0f;
                if (!active_particles.particles.empty()) {
                    bounds_min = active_particles.particles.front().position;
                    bounds_max = active_particles.particles.front().position;
                    radius_min = active_particles.particles.front().radius;
                    radius_max = active_particles.particles.front().radius;
                    for (const Particle& particle : active_particles.particles) {
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            if (particle.position[axis] < bounds_min[axis]) bounds_min[axis] = particle.position[axis];
                            if (particle.position[axis] > bounds_max[axis]) bounds_max[axis] = particle.position[axis];
                        }
                        if (particle.radius < radius_min) radius_min = particle.radius;
                        if (particle.radius > radius_max) radius_max = particle.radius;
                    }
                }

                ImGui::TextColored(accent_color, "Object Inspector");
                ImGui::SameLine();
                ImGui::TextColored(muted_color, "Particles");
                ImGui::Separator();

                if (ImGui::BeginTable("InspectorParticlesIdentity", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Name");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%s", active_particles.name.c_str());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Particles");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "%zu", active_particles.particles.size());

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Bounds min");
                    ImGui::TableNextColumn();
                    if (active_particles.particles.empty())
                        ImGui::TextColored(muted_color, "n/a");
                    else
                        ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", bounds_min[0], bounds_min[1], bounds_min[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Bounds max");
                    ImGui::TableNextColumn();
                    if (active_particles.particles.empty())
                        ImGui::TextColored(muted_color, "n/a");
                    else
                        ImGui::TextColored(value_color, "%.2f, %.2f, %.2f", bounds_max[0], bounds_max[1], bounds_max[2]);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Radius");
                    ImGui::TableNextColumn();
                    if (active_particles.particles.empty())
                        ImGui::TextColored(muted_color, "n/a");
                    else
                        ImGui::TextColored(value_color, "%.3f - %.3f", radius_min, radius_max);

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Playback");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, scene.bake.mode == ScenePlaybackMode::baked ? "Baked particles" : "Live particles");
                    ImGui::EndTable();
                }

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Render");
                ImGui::TextColored(value_color, "Billboard");
                ImGui::InputFloat("Radius Scale", &settings.radius_scale, 0.05f, 0.2f, "%.3f");

                ImGui::Separator();
                ImGui::TextColored(accent_color, "Particle Format");
                if (ImGui::BeginTable("InspectorParticleFormat", 2, ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Position");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float3");

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Radius");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float");

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(label_color, "Color");
                    ImGui::TableNextColumn();
                    ImGui::TextColored(value_color, "float3");
                    ImGui::EndTable();
                }
            } else {
                throw std::runtime_error("Object inspector received unsupported scene object kind");
            }

            ImGui::End();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(2);
        }

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

        const float timeline_height = this->timeline.height;
        if (main_viewport->WorkSize.x <= 320.0f || main_viewport->WorkSize.y <= timeline_height) throw std::runtime_error("Viewport is too small for fixed timeline");

        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(ImVec2{main_viewport->WorkPos.x, main_viewport->WorkPos.y + main_viewport->WorkSize.y - timeline_height}, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{main_viewport->WorkSize.x, timeline_height}, ImGuiCond_Always);
        constexpr ImGuiWindowFlags timeline_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground;

        TimelineSequence sequence{this->timeline.frame_min, this->timeline.frame_max};
        constexpr int sequence_options = ImSequencer::SEQUENCER_CHANGE_FRAME;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        ImGui::Begin("Spectra Timeline", nullptr, timeline_window_flags);
        ImSequencer::Sequencer(&sequence, &this->timeline.current_frame, nullptr, nullptr, &this->timeline.first_frame, sequence_options);
        this->timeline.current_frame = std::clamp(this->timeline.current_frame, this->timeline.frame_min, this->timeline.frame_max);
        ImGui::End();
        ImGui::PopStyleVar(2);

        ImGui::Render();
        const vk::RenderingAttachmentInfo imgui_color_attachment{
            *this->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo imgui_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &imgui_color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(imgui_rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, this->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame, const Scene& scene) {
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
        if (frame.recreate_after_present) this->recreate_swapchain(scene);

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }

    void Spectra::create_viewport_pipeline() {
        if (!*this->context.device) throw std::runtime_error("Cannot create viewport pipeline without a Vulkan device");
        if (this->swapchain.format == vk::Format::eUndefined || this->swapchain.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create viewport pipeline without swapchain formats");

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "viewport_grid.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "viewport_grid.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{this->context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{this->context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(float) * 16};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
        this->viewport.pipeline_layout = vk::raii::PipelineLayout{this->context.device, pipeline_layout_create_info};

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format color_format   = this->swapchain.format;
        const vk::Format stencil_format = static_cast<bool>(this->swapchain.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? this->swapchain.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = this->swapchain.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->viewport.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->viewport.pipeline                  = vk::raii::Pipeline{this->context.device, nullptr, pipeline_create_info};
    }

    void Spectra::destroy_viewport_pipeline() noexcept {
        this->viewport.pipeline        = nullptr;
        this->viewport.pipeline_layout = nullptr;
    }

    void Spectra::create_mesh_renderer(const Scene& scene) {
        if (*this->mesh_renderer.surface_pipeline || *this->mesh_renderer.wireframe_pipeline || this->mesh_renderer.descriptor_sets.size() != 0 || !this->mesh_renderer.frame_resources.empty()) throw std::runtime_error("Mesh renderer is already initialized");
        if (!*this->context.physical_device) throw std::runtime_error("Cannot create mesh renderer without a physical device");
        if (!*this->context.device) throw std::runtime_error("Cannot create mesh renderer without a Vulkan device");
        if (this->swapchain.format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a color format");
        if (this->swapchain.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create mesh renderer without a depth format");
        if (this->sync.frame_count == 0) throw std::runtime_error("Cannot create mesh renderer without frames in flight");
        if (scene.meshes.empty()) throw std::runtime_error("Cannot create mesh renderer for a scene without meshes");
        if (scene.meshes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / this->sync.frame_count)) throw std::runtime_error("Scene has too many meshes for frame resources");

        const std::uint32_t descriptor_set_count = static_cast<std::uint32_t>(scene.meshes.size() * this->sync.frame_count);
        this->mesh_renderer.frame_resources.resize(descriptor_set_count);
        if (descriptor_set_count > std::numeric_limits<std::uint32_t>::max() / 3) throw std::runtime_error("Mesh descriptor pool size is too large");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->mesh_renderer.descriptor_layout = vk::raii::DescriptorSetLayout{this->context.device, descriptor_layout_create_info};

        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, descriptor_set_count * 3};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, descriptor_set_count, 1, &pool_size};
        this->mesh_renderer.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};

        std::vector layouts(descriptor_set_count, *this->mesh_renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->mesh_renderer.descriptor_pool, descriptor_set_count, layouts.data()};
        this->mesh_renderer.descriptor_sets = vk::raii::DescriptorSets{this->context.device, allocate_info};
        if (this->mesh_renderer.descriptor_sets.size() != descriptor_set_count) throw std::runtime_error("Failed to allocate mesh descriptor sets");

        const vk::DescriptorSetLayout descriptor_layout = *this->mesh_renderer.descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout};
        this->mesh_renderer.pipeline_layout = vk::raii::PipelineLayout{this->context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "mesh.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{this->context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{this->context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format color_format   = this->swapchain.format;
        const vk::Format stencil_format = static_cast<bool>(this->swapchain.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? this->swapchain.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = this->swapchain.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->mesh_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->mesh_renderer.surface_pipeline     = vk::raii::Pipeline{this->context.device, nullptr, pipeline_create_info};
        input_assembly_state.topology            = vk::PrimitiveTopology::eLineList;
        this->mesh_renderer.wireframe_pipeline   = vk::raii::Pipeline{this->context.device, nullptr, pipeline_create_info};
    }

    void Spectra::destroy_mesh_renderer() noexcept {
        this->mesh_renderer.wireframe_pipeline = nullptr;
        this->mesh_renderer.surface_pipeline   = nullptr;
        this->mesh_renderer.pipeline_layout    = nullptr;
        this->mesh_renderer.descriptor_sets    = nullptr;
        this->mesh_renderer.descriptor_pool    = nullptr;
        this->mesh_renderer.descriptor_layout  = nullptr;
        this->mesh_renderer.frame_resources.clear();
    }

    void Spectra::create_particles_renderer(const Scene& scene) {
        if (*this->particles_renderer.pipeline || this->particles_renderer.descriptor_sets.size() != 0 || !this->particles_renderer.frame_resources.empty()) throw std::runtime_error("Particles renderer is already initialized");
        if (!*this->context.physical_device) throw std::runtime_error("Cannot create particles renderer without a physical device");
        if (!*this->context.device) throw std::runtime_error("Cannot create particles renderer without a Vulkan device");
        if (this->swapchain.format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a color format");
        if (this->swapchain.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create particles renderer without a depth format");
        if (this->sync.frame_count == 0) throw std::runtime_error("Cannot create particles renderer without frames in flight");
        if (scene.particles.empty()) throw std::runtime_error("Cannot create particles renderer for a scene without particles");
        if (scene.particles.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / this->sync.frame_count)) throw std::runtime_error("Scene has too many particles objects for frame resources");

        const std::uint32_t descriptor_set_count = static_cast<std::uint32_t>(scene.particles.size() * this->sync.frame_count);
        this->particles_renderer.frame_resources.resize(descriptor_set_count);
        if (descriptor_set_count > std::numeric_limits<std::uint32_t>::max() / 2) throw std::runtime_error("Particles descriptor pool size is too large");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->particles_renderer.descriptor_layout = vk::raii::DescriptorSetLayout{this->context.device, descriptor_layout_create_info};

        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, descriptor_set_count * 2};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, descriptor_set_count, 1, &pool_size};
        this->particles_renderer.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};

        std::vector layouts(descriptor_set_count, *this->particles_renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->particles_renderer.descriptor_pool, descriptor_set_count, layouts.data()};
        this->particles_renderer.descriptor_sets = vk::raii::DescriptorSets{this->context.device, allocate_info};
        if (this->particles_renderer.descriptor_sets.size() != descriptor_set_count) throw std::runtime_error("Failed to allocate particles descriptor sets");

        const vk::DescriptorSetLayout descriptor_layout = *this->particles_renderer.descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout};
        this->particles_renderer.pipeline_layout = vk::raii::PipelineLayout{this->context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "particles.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{this->context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{this->context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable    = VK_FALSE;
        color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format color_format   = this->swapchain.format;
        const vk::Format stencil_format = static_cast<bool>(this->swapchain.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? this->swapchain.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = this->swapchain.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->particles_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->particles_renderer.pipeline        = vk::raii::Pipeline{this->context.device, nullptr, pipeline_create_info};
    }

    void Spectra::destroy_particles_renderer() noexcept {
        this->particles_renderer.pipeline          = nullptr;
        this->particles_renderer.pipeline_layout   = nullptr;
        this->particles_renderer.descriptor_sets   = nullptr;
        this->particles_renderer.descriptor_pool   = nullptr;
        this->particles_renderer.descriptor_layout = nullptr;
        this->particles_renderer.frame_resources.clear();
    }

    void Spectra::create_volume_renderer(const Scene& scene) {
        if (*this->volume_renderer.pipeline || this->volume_renderer.descriptor_sets.size() != 0 || !this->volume_renderer.frame_resources.empty()) throw std::runtime_error("Volume renderer is already initialized");
        if (!*this->context.physical_device) throw std::runtime_error("Cannot create volume renderer without a physical device");
        if (!*this->context.device) throw std::runtime_error("Cannot create volume renderer without a Vulkan device");
        if (this->swapchain.format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a color format");
        if (this->swapchain.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create volume renderer without a depth format");
        if (this->sync.frame_count == 0) throw std::runtime_error("Cannot create volume renderer without frames in flight");
        if (scene.volumes.empty()) throw std::runtime_error("Cannot create volume renderer for a scene without volumes");
        if (scene.volumes.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() / this->sync.frame_count)) throw std::runtime_error("Scene has too many volumes for frame resources");

        const std::uint32_t descriptor_set_count = static_cast<std::uint32_t>(scene.volumes.size() * this->sync.frame_count);
        this->volume_renderer.frame_resources.resize(descriptor_set_count);
        if (descriptor_set_count > std::numeric_limits<std::uint32_t>::max() / 4) throw std::runtime_error("Volume descriptor pool size is too large");

        constexpr std::array bindings{
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_layout_create_info{{}, static_cast<std::uint32_t>(bindings.size()), bindings.data()};
        this->volume_renderer.descriptor_layout = vk::raii::DescriptorSetLayout{this->context.device, descriptor_layout_create_info};

        const vk::DescriptorPoolSize pool_size{vk::DescriptorType::eStorageBuffer, descriptor_set_count * 4};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, descriptor_set_count, 1, &pool_size};
        this->volume_renderer.descriptor_pool = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};

        std::vector layouts(descriptor_set_count, *this->volume_renderer.descriptor_layout);
        const vk::DescriptorSetAllocateInfo allocate_info{*this->volume_renderer.descriptor_pool, descriptor_set_count, layouts.data()};
        this->volume_renderer.descriptor_sets = vk::raii::DescriptorSets{this->context.device, allocate_info};
        if (this->volume_renderer.descriptor_sets.size() != descriptor_set_count) throw std::runtime_error("Failed to allocate volume descriptor sets");

        const vk::DescriptorSetLayout descriptor_layout = *this->volume_renderer.descriptor_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1, &descriptor_layout};
        this->volume_renderer.pipeline_layout = vk::raii::PipelineLayout{this->context.device, pipeline_layout_create_info};

        const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.vert.spv");
        const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "volume.frag.spv");
        const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
        const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
        const vk::raii::ShaderModule vertex_shader{this->context.device, vertex_module_create_info};
        const vk::raii::ShaderModule fragment_shader{this->context.device, fragment_module_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;

        constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{
            {},
            VK_FALSE,
            VK_FALSE,
            vk::PolygonMode::eFill,
            vk::CullModeFlagBits::eNone,
            vk::FrontFace::eCounterClockwise,
            VK_FALSE,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};

        vk::PipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.blendEnable         = VK_TRUE;
        color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
        color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
        color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
        color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
        color_blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        const vk::Format color_format   = this->swapchain.format;
        const vk::Format stencil_format = static_cast<bool>(this->swapchain.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? this->swapchain.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &color_format;
        rendering_create_info.depthAttachmentFormat   = this->swapchain.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;

        vk::GraphicsPipelineCreateInfo pipeline_create_info{};
        pipeline_create_info.pNext               = &rendering_create_info;
        pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
        pipeline_create_info.pStages             = shader_stages.data();
        pipeline_create_info.pVertexInputState   = &vertex_input_state;
        pipeline_create_info.pInputAssemblyState = &input_assembly_state;
        pipeline_create_info.pViewportState      = &viewport_state;
        pipeline_create_info.pRasterizationState = &rasterization_state;
        pipeline_create_info.pMultisampleState   = &multisample_state;
        pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
        pipeline_create_info.pColorBlendState    = &color_blend_state;
        pipeline_create_info.pDynamicState       = &dynamic_state;
        pipeline_create_info.layout              = *this->volume_renderer.pipeline_layout;
        pipeline_create_info.renderPass          = nullptr;
        pipeline_create_info.subpass             = 0;
        this->volume_renderer.pipeline           = vk::raii::Pipeline{this->context.device, nullptr, pipeline_create_info};
    }

    void Spectra::destroy_volume_renderer() noexcept {
        this->volume_renderer.pipeline          = nullptr;
        this->volume_renderer.pipeline_layout   = nullptr;
        this->volume_renderer.descriptor_sets   = nullptr;
        this->volume_renderer.descriptor_pool   = nullptr;
        this->volume_renderer.descriptor_layout = nullptr;
        this->volume_renderer.frame_resources.clear();
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

    void Spectra::recreate_swapchain(const Scene& scene) {
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

        const bool recreate_mesh_renderer      = static_cast<bool>(*this->mesh_renderer.surface_pipeline);
        const bool recreate_particles_renderer = static_cast<bool>(*this->particles_renderer.pipeline);
        const bool recreate_volume_renderer    = static_cast<bool>(*this->volume_renderer.pipeline);
        this->destroy_mesh_renderer();
        this->destroy_particles_renderer();
        this->destroy_volume_renderer();
        this->destroy_viewport_pipeline();
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
        this->create_viewport_pipeline();
        if (recreate_mesh_renderer) this->create_mesh_renderer(scene);
        if (recreate_particles_renderer) this->create_particles_renderer(scene);
        if (recreate_volume_renderer) this->create_volume_renderer(scene);
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
