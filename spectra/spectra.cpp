module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_mono.h>
#include <roboto/roboto_regular.h>

#include <vulkan/vulkan_raii.hpp>

module spectra;

import spectra.contract;
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

    void load_imgui_fonts() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts == nullptr) throw std::runtime_error("ImGui font atlas is unavailable");

        ImFontConfig font_config{};
        font_config.OversampleH   = 3;
        font_config.OversampleV   = 3;
        constexpr float font_size = 15.0f;
        ImFont* default_font      = io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_regular_compressed_data, g_roboto_regular_compressed_size, font_size, &font_config);
        if (default_font == nullptr) throw std::runtime_error("Failed to load Roboto regular font");

        ImFontConfig icon_config{};
        icon_config.MergeMode     = true;
        icon_config.PixelSnapH    = true;
        icon_config.OversampleH   = 3;
        icon_config.OversampleV   = 3;
        constexpr float icon_size = 1.28571429f * font_size;
        icon_config.GlyphOffset.x = icon_size * 0.01f;
        icon_config.GlyphOffset.y = icon_size * 0.2f;
        constexpr std::array<ImWchar, 3> icon_ranges{ICON_MIN_MS, ICON_MAX_MS, 0};
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_materialSymbolsRounded_compressed_data, g_materialSymbolsRounded_compressed_size, icon_size, &icon_config, icon_ranges.data()) == nullptr) throw std::runtime_error("Failed to load Material Symbols icon font");

        ImFontConfig mono_config{};
        mono_config.OversampleH = 3;
        mono_config.OversampleV = 3;
        if (io.Fonts->AddFontFromMemoryCompressedTTF(g_roboto_mono_compressed_data, g_roboto_mono_compressed_size, font_size, &mono_config) == nullptr) throw std::runtime_error("Failed to load Roboto mono font");
        io.FontDefault = default_font;
    }

    void apply_imgui_style() {
        ImGui::StyleColorsDark();
        ImGuiStyle& style                  = ImGui::GetStyle();
        style.WindowRounding               = 6.0f;
        style.WindowBorderSize             = 1.0f;
        style.ColorButtonPosition          = ImGuiDir_Right;
        style.FrameRounding                = 4.0f;
        style.FrameBorderSize              = 1.0f;
        style.GrabRounding                 = 4.0f;
        style.ScrollbarRounding            = 6.0f;
        style.TabRounding                  = 4.0f;
        style.IndentSpacing                = 14.0f;
        style.ItemSpacing                  = ImVec2{8.0f, 6.0f};
        style.FramePadding                 = ImVec2{8.0f, 5.0f};
        style.WindowPadding                = ImVec2{10.0f, 10.0f};
        style.Colors[ImGuiCol_WindowBg]    = ImVec4{0.075f, 0.080f, 0.090f, 1.0f};
        style.Colors[ImGuiCol_ChildBg]     = ImVec4{0.060f, 0.065f, 0.074f, 1.0f};
        style.Colors[ImGuiCol_MenuBarBg]   = ImVec4{0.050f, 0.055f, 0.064f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4{0.050f, 0.055f, 0.064f, 1.0f};
        style.Colors[ImGuiCol_PopupBg]     = ImVec4{0.070f, 0.075f, 0.085f, 1.0f};
        style.Colors[ImGuiCol_Border]      = ImVec4{0.180f, 0.195f, 0.220f, 1.0f};
        style.Colors[ImGuiCol_FrameBg]     = ImVec4{0.105f, 0.115f, 0.130f, 1.0f};

        const ImVec4 normal_color = ImVec4{0.165f, 0.345f, 0.650f, 1.0f};
        constexpr std::array normal_colors{
            ImGuiCol_Header,
            ImGuiCol_SliderGrab,
            ImGuiCol_Button,
            ImGuiCol_CheckMark,
            ImGuiCol_ResizeGrip,
            ImGuiCol_TextSelectedBg,
            ImGuiCol_Separator,
            ImGuiCol_FrameBgActive,
        };
        for (const ImGuiCol color_id : normal_colors) style.Colors[color_id] = normal_color;

        const ImVec4 active_color = ImVec4{0.105f, 0.285f, 0.560f, 1.0f};
        constexpr std::array active_colors{
            ImGuiCol_HeaderActive,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_ButtonActive,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SeparatorActive,
        };
        for (const ImGuiCol color_id : active_colors) style.Colors[color_id] = active_color;

        const ImVec4 hovered_color = ImVec4{0.235f, 0.450f, 0.760f, 1.0f};
        constexpr std::array hovered_colors{
            ImGuiCol_HeaderHovered,
            ImGuiCol_ButtonHovered,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_SeparatorHovered,
        };
        for (const ImGuiCol color_id : hovered_colors) style.Colors[color_id] = hovered_color;

        style.Colors[ImGuiCol_TitleBgActive]    = ImVec4{0.090f, 0.100f, 0.115f, 1.0f};
        style.Colors[ImGuiCol_TitleBg]          = ImVec4{0.050f, 0.055f, 0.064f, 1.0f};
        style.Colors[ImGuiCol_Tab]              = ImVec4{0.080f, 0.090f, 0.105f, 1.0f};
        style.Colors[ImGuiCol_TabHovered]       = ImVec4{0.235f, 0.450f, 0.760f, 1.0f};
        style.Colors[ImGuiCol_TabActive]        = ImVec4{0.125f, 0.150f, 0.180f, 1.0f};
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4{0.020f, 0.025f, 0.032f, 0.650f};
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
    }

} // namespace

namespace spectra {
    struct Spectra::FrameState {
        std::uint32_t frame_index{0};
        std::uint32_t image_index{0};
        bool recreate_after_present{false};
        std::vector<vk::SemaphoreSubmitInfo> external_waits{};
    };

    const vk::raii::PhysicalDevice& Spectra::physical_device() const {
        return this->context.physical_device;
    }

    const vk::raii::Device& Spectra::device() const {
        return this->context.device;
    }

    std::uint32_t Spectra::frame_count() const {
        return this->sync.frame_count;
    }

    vk::Extent2D Spectra::swapchain_extent() const {
        return this->swapchain.extent;
    }

    void Spectra::activate_renderer(const std::size_t renderer_index) {
        if (renderer_index >= this->renderers.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        const SpectraRendererAvailability availability = this->renderer_availability(this->renderers[renderer_index].name);
        if (!availability.available) throw std::runtime_error(availability.detail.empty() ? std::format("Renderer \"{}\" is not available for the active scene", this->renderers[renderer_index].name) : availability.detail);
        this->active_renderer_index = renderer_index;
        if (this->renderer_activation_callback) this->renderer_activation_callback(this->renderers[renderer_index].name);
    }

    void Spectra::store_panel(SpectraPanel panel) {
        if (panel.id.empty()) throw std::runtime_error("Spectra panel id must not be empty");
        if (panel.title.empty()) throw std::runtime_error("Spectra panel title must not be empty");
        if (!panel.draw) throw std::runtime_error("Spectra panel draw callback must not be empty");
        for (const SpectraPanel& existing_panel : this->panels) {
            if (existing_panel.id == panel.id) throw std::runtime_error(std::string{"Duplicate Spectra panel id: "} + panel.id);
            if (existing_panel.title == panel.title) throw std::runtime_error(std::string{"Duplicate Spectra panel title: "} + panel.title);
        }
        this->panels.push_back(std::move(panel));
        this->dock_layout_initialized = false;
    }

    void Spectra::set_window_detail(std::string detail) {
        this->window_title.detail = std::move(detail);
    }

    void Spectra::set_renderer_availability_callback(std::move_only_function<SpectraRendererAvailability(std::string_view)> callback) {
        this->renderer_availability_callback = std::move(callback);
    }

    void Spectra::set_renderer_activation_callback(std::move_only_function<void(std::string_view)> callback) {
        this->renderer_activation_callback = std::move(callback);
    }

    SpectraRendererAvailability Spectra::renderer_availability(const std::string_view renderer_name) {
        if (!this->renderer_availability_callback) return SpectraRendererAvailability{};
        SpectraRendererAvailability availability = this->renderer_availability_callback(renderer_name);
        if (!availability.available && availability.detail.empty()) availability.detail = std::format("Renderer \"{}\" is not available for the active scene", renderer_name);
        return availability;
    }

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};
        this->window_title.base = app_name_string;

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
        std::vector<const char*> enabled_device_extensions{vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName};
#if defined(_WIN32)
        enabled_device_extensions.push_back(vk::KHRExternalMemoryWin32ExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreWin32ExtensionName);
#else
        enabled_device_extensions.push_back(vk::KHRExternalMemoryFdExtensionName);
        enabled_device_extensions.push_back(vk::KHRExternalSemaphoreFdExtensionName);
#endif
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

            const vk::ApplicationInfo application_info{app_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), engine_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
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
            this->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
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
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
                if (physical_device.getProperties().apiVersion < VK_API_VERSION_1_4) continue;

                const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
                bool required_extensions_available                              = true;
                for (const char* required_extension : enabled_device_extensions) {
                    if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) required_extensions_available = false;
                }
                if (!required_extensions_available) continue;

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
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
        }
        {
            const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
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
        this->create_imgui();
    } catch (...) {
        this->destroy_imgui();
        if (this->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        this->detach_renderers_noexcept();

        this->destroy_imgui();
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
        this->context.command_pool = nullptr;
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

    void Spectra::register_renderer(RegisteredRenderer renderer) {
        const std::string_view renderer_name = renderer.name;
        if (renderer_name.empty()) throw std::runtime_error("Spectra renderer name must not be empty");
        for (const RegisteredRenderer& existing_renderer : this->renderers) {
            if (existing_renderer.name == renderer_name) throw std::runtime_error(std::string{"Duplicate Spectra renderer name: "} + std::string{renderer_name});
        }
        const bool first_renderer = this->renderers.empty();
        if (first_renderer) {
            const SpectraRendererAvailability availability = this->renderer_availability(renderer.name);
            if (!availability.available) throw std::runtime_error(availability.detail);
        }
        renderer.attach(*this);
        if (this->imgui.initialized) renderer.after_imgui_created(*this);
        this->renderers.push_back(std::move(renderer));
        if (first_renderer) {
            this->active_renderer_index = 0;
            if (this->renderer_activation_callback) this->renderer_activation_callback(this->renderers.front().name);
        }
    }

    void Spectra::run() {
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
    }

    void Spectra::create_imgui() {
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
            const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1000u * static_cast<std::uint32_t>(descriptor_pool_sizes.size()), static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
            this->imgui.descriptor_pool               = vk::raii::DescriptorPool{this->context.device, descriptor_pool_create_info};
            const vk::Format imgui_color_format       = this->swapchain.format;
            const std::uint32_t imgui_min_image_count = std::max(2u, this->sync.frame_count);
            const std::uint32_t imgui_image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (imgui_image_count < imgui_min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created = true;

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            load_imgui_fonts();
            apply_imgui_style();

            if (!ImGui_ImplGlfw_InitForVulkan(this->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
            glfw_backend_initialized = true;

            auto color_attachment_format = static_cast<VkFormat>(imgui_color_format);
            VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
            pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = VK_API_VERSION_1_4;
            init_info.Instance                                     = static_cast<VkInstance>(*this->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->context.device);
            init_info.QueueFamily                                  = this->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->context.graphics_queue);
            init_info.DescriptorPool                               = static_cast<VkDescriptorPool>(*this->imgui.descriptor_pool);
            init_info.MinImageCount                                = imgui_min_image_count;
            init_info.ImageCount                                   = imgui_image_count;
            init_info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering                          = true;
            init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
            if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
            vulkan_backend_initialized    = true;
            this->imgui.initialized       = true;
            this->imgui_shutdown_notified = false;
            for (RegisteredRenderer& renderer : this->renderers) renderer.after_imgui_created(*this);
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->imgui.descriptor_pool   = nullptr;
            this->imgui.initialized       = false;
            this->imgui_shutdown_notified = false;
            throw;
        }
    }

    void Spectra::wait_device_idle_noexcept() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }
    }

    void Spectra::notify_renderers_before_imgui_shutdown() noexcept {
        if (this->imgui_shutdown_notified) return;
        this->wait_device_idle_noexcept();
        for (auto renderer = this->renderers.rbegin(); renderer != this->renderers.rend(); ++renderer) {
            try {
                renderer->before_imgui_shutdown(*this);
            } catch (...) {
            }
        }
        this->imgui_shutdown_notified = true;
    }

    void Spectra::destroy_imgui() noexcept {
        this->notify_renderers_before_imgui_shutdown();
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool   = nullptr;
        this->imgui.initialized       = false;
        this->dock_layout_initialized = false;
    }

    void Spectra::detach_renderers_noexcept() noexcept {
        this->notify_renderers_before_imgui_shutdown();
        for (auto renderer = this->renderers.rbegin(); renderer != this->renderers.rend(); ++renderer) {
            try {
                renderer->detach(*this);
            } catch (...) {
            }
        }
        this->renderers.clear();
        this->panels.clear();
        this->active_renderer_index   = 0;
        this->dock_layout_initialized = false;
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (glfwWindowShouldClose(this->surface.window.get())) return false;
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

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);

        if (this->renderers.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->active_renderer_index >= this->renderers.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        const SpectraFrameInfo frame_info{frame.frame_index, frame.image_index};
        SpectraFrameResult frame_result = this->renderers[this->active_renderer_index].begin_frame(*this, frame_info);
        if (frame_result.completion_semaphore.has_value()) {
            if (*frame_result.completion_semaphore == VK_NULL_HANDLE) throw std::runtime_error("External completion semaphore must not be null");
            frame.external_waits.emplace_back(*frame_result.completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer);
        }
        if (frame_result.close_requested) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
        if (frame_result.window_detail.has_value()) this->set_window_detail(std::move(*frame_result.window_detail));
        return true;
    }

    void Spectra::record_frame(FrameState& frame) {
        this->draw_main_menu();
        this->draw_dockspace();
        this->draw_registered_panels();

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);

        if (this->renderers.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->active_renderer_index >= this->renderers.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        this->renderers[this->active_renderer_index].record_frame(command_buffer);

        {
            const vk::ImageMemoryBarrier2 color_barrier{
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
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.02f, 0.02f, 0.025f, 1.0f};
        const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
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
        const vk::RenderingInfo clear_rendering_info{{}, {{0, 0}, this->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(clear_rendering_info);
        command_buffer.endRendering();

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

    void Spectra::end_frame(FrameState& frame) {
        std::vector<vk::SemaphoreSubmitInfo> wait_semaphore_infos{};
        wait_semaphore_infos.reserve(frame.external_waits.size() + 1u);
        wait_semaphore_infos.emplace_back(*this->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands);
        wait_semaphore_infos.insert(wait_semaphore_infos.end(), frame.external_waits.begin(), frame.external_waits.end());
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, static_cast<std::uint32_t>(wait_semaphore_infos.size()), wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        bool frame_presented = true;
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
                frame.recreate_after_present = true;
            else if (present_result == vk::Result::eErrorSurfaceLostKHR) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (present_result != vk::Result::eSuccess)
                throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
            frame_presented              = false;
        } catch (const vk::SystemError& error) {
            if (error.code().value() == static_cast<int>(vk::Result::eErrorOutOfDateKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else if (error.code().value() == static_cast<int>(vk::Result::eSuboptimalKHR))
                frame.recreate_after_present = true;
            else if (error.code().value() == static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) {
                frame.recreate_after_present = true;
                frame_presented              = false;
            } else
                throw;
        }
        if (frame.recreate_after_present) this->recreate_swapchain();
        if (frame_presented) this->update_window_title(ImGui::GetIO().DeltaTime);

        this->sync.frame_index = (this->sync.frame_index + 1) % this->sync.frame_count;
    }

    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            for (SpectraPanel& panel : this->panels) {
                if (panel.shortcut_key != ImGuiKey_None && ImGui::IsKeyPressed(panel.shortcut_key, false)) panel.visible = !panel.visible;
            }
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            for (SpectraPanel& panel : this->panels) {
                if (!panel.show_in_menu) continue;
                const std::string label = panel.icon.empty() ? panel.title : panel.icon + " " + panel.title;
                const char* shortcut    = panel.shortcut_label.empty() ? nullptr : panel.shortcut_label.c_str();
                ImGui::MenuItem(label.c_str(), shortcut, &panel.visible);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Renderer")) {
            for (std::size_t renderer_index = 0; renderer_index < this->renderers.size(); ++renderer_index) {
                const bool selected = renderer_index == this->active_renderer_index;
                const SpectraRendererAvailability availability = this->renderer_availability(this->renderers[renderer_index].name);
                ImGui::BeginDisabled(!availability.available);
                if (ImGui::MenuItem(this->renderers[renderer_index].name.c_str(), nullptr, selected)) this->activate_renderer(renderer_index);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !availability.detail.empty()) ImGui::SetTooltip("%s", availability.detail.c_str());
            }
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }

    void Spectra::draw_menu_toolbar() {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (std::size_t renderer_index = 0; renderer_index < this->renderers.size(); ++renderer_index) {
            const bool selected = renderer_index == this->active_renderer_index;
            const SpectraRendererAvailability availability = this->renderer_availability(this->renderers[renderer_index].name);
            ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            ImGui::BeginDisabled(!availability.available);
            if (ImGui::SmallButton(this->renderers[renderer_index].name.c_str())) this->activate_renderer(renderer_index);
            ImGui::EndDisabled();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !availability.detail.empty()) ImGui::SetTooltip("%s", availability.detail.c_str());
            if (renderer_index + 1 < this->renderers.size()) ImGui::SameLine(0.0f, 4.0f);
        }

        std::vector<SpectraPanel*> toolbar_panels{};
        for (SpectraPanel& panel : this->panels) {
            if (panel.show_in_toolbar) toolbar_panels.push_back(&panel);
        }
        if (toolbar_panels.empty()) return;

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toolbar_panels.size()) * button_size + static_cast<float>(toolbar_panels.size() + 1) * 4.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 520.0f) return;

        ImGui::SameLine(window_width - total_width - 16.0f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (SpectraPanel* panel : toolbar_panels) {
            const char* label = panel->icon.empty() ? panel->title.c_str() : panel->icon.c_str();
            ImGui::PushStyleColor(ImGuiCol_Button, panel->visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            if (ImGui::Button(label, ImVec2{button_size, button_size})) panel->visible = !panel->visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && !panel->shortcut_label.empty()) ImGui::SetTooltip("Toggle %s Window (%s)", panel->title.c_str(), panel->shortcut_label.c_str());
            if (ImGui::IsItemHovered() && panel->shortcut_label.empty()) ImGui::SetTooltip("Toggle %s Window", panel->title.c_str());
            ImGui::SameLine(0.0f, 4.0f);
        }
    }

    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (main_viewport->WorkSize.x <= 640.0f || main_viewport->WorkSize.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        const ImVec4 dockspace_window_background     = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{dockspace_window_background.x, dockspace_window_background.y, dockspace_window_background.z, 0.0f});
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, main_viewport, dockspace_flags);
        ImGui::PopStyleColor();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID left_id   = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.30f, nullptr, &center_id);
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.27f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.30f, nullptr, &center_id);
        if (left_id == 0 || right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGuiID left_bottom_id  = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Down, 0.30f, nullptr, &left_id);
        ImGuiID right_bottom_id = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.45f, nullptr, &right_id);
        if (left_bottom_id == 0 || right_bottom_id == 0 || left_id == 0 || right_id == 0) throw std::runtime_error("Failed to build Spectra side panels");

        for (const SpectraPanel& panel : this->panels) {
            switch (panel.dock_slot) {
            case SpectraDockSlot::Center: ImGui::DockBuilderDockWindow(panel.title.c_str(), center_id); break;
            case SpectraDockSlot::Left: ImGui::DockBuilderDockWindow(panel.title.c_str(), left_id); break;
            case SpectraDockSlot::LeftBottom: ImGui::DockBuilderDockWindow(panel.title.c_str(), left_bottom_id); break;
            case SpectraDockSlot::Right: ImGui::DockBuilderDockWindow(panel.title.c_str(), right_id); break;
            case SpectraDockSlot::RightBottom: ImGui::DockBuilderDockWindow(panel.title.c_str(), right_bottom_id); break;
            case SpectraDockSlot::Bottom: ImGui::DockBuilderDockWindow(panel.title.c_str(), bottom_id); break;
            case SpectraDockSlot::Floating: break;
            }
        }
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->dock_layout_initialized = true;
    }

    void Spectra::draw_registered_panels() {
        for (SpectraPanel& panel : this->panels) {
            if (!panel.visible) continue;
            if (panel.zero_window_padding) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
            bool open        = panel.visible;
            const bool began = panel.closable ? ImGui::Begin(panel.title.c_str(), &open, panel.window_flags) : ImGui::Begin(panel.title.c_str(), nullptr, panel.window_flags);
            panel.visible    = open;
            if (began) panel.draw();
            ImGui::End();
            if (panel.zero_window_padding) ImGui::PopStyleVar();
        }
    }

    void Spectra::update_window_title(const float delta_seconds) {
        if (this->surface.window == nullptr) throw std::runtime_error("Cannot update window title without a GLFW window");

        ++this->window_title.frame_count;
        this->window_title.refresh_timer += delta_seconds;
        if (this->window_title.refresh_timer <= 1.0f) return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.Framerate <= 0.0f) return;

        const std::string title = this->window_title.detail.empty() ? std::format("{} | {:.0f} FPS / {:.3f}ms | frame {}", this->window_title.base, io.Framerate, 1000.0f / io.Framerate, this->window_title.frame_count) : std::format("{} - {} | {:.0f} FPS / {:.3f}ms | frame {}", this->window_title.base, this->window_title.detail, io.Framerate, 1000.0f / io.Framerate, this->window_title.frame_count);
        glfwSetWindowTitle(this->surface.window.get(), title.c_str());
        this->window_title.refresh_timer = 0.0f;
    }

    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        const std::vector<vk::SurfaceFormatKHR> surface_formats = this->context.physical_device.getSurfaceFormatsKHR(this->surface.surface);
        if (surface_formats.empty()) throw std::runtime_error("Vulkan surface has no supported formats");
        const std::vector<vk::PresentModeKHR> present_modes = this->context.physical_device.getSurfacePresentModesKHR(this->surface.surface);
        if (present_modes.empty()) throw std::runtime_error("Vulkan surface has no supported present modes");
        const vk::SurfaceCapabilitiesKHR surface_capabilities = this->context.physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);

        if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
            this->swapchain.format      = vk::Format::eB8G8R8A8Unorm;
            this->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
        } else {
            this->swapchain.format      = surface_formats.front().format;
            this->swapchain.color_space = surface_formats.front().colorSpace;
            bool selected               = false;
            constexpr std::array preferred_surface_formats{
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

        int framebuffer_width  = 0;
        int framebuffer_height = 0;
        glfwGetFramebufferSize(this->surface.window.get(), &framebuffer_width, &framebuffer_height);
        if (framebuffer_width <= 0 || framebuffer_height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size during swapchain creation");
        if (surface_capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
            this->swapchain.extent = surface_capabilities.currentExtent;
        } else {
            const std::uint32_t width  = std::clamp(static_cast<std::uint32_t>(framebuffer_width), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
            const std::uint32_t height = std::clamp(static_cast<std::uint32_t>(framebuffer_height), surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
            this->swapchain.extent     = vk::Extent2D{width, height};
        }
        if (this->swapchain.extent.width == 0 || this->swapchain.extent.height == 0) throw std::runtime_error("Swapchain extent must be positive");

        std::uint32_t image_count = surface_capabilities.minImageCount + 1;
        if (surface_capabilities.maxImageCount > 0) image_count = std::min(image_count, surface_capabilities.maxImageCount);
        if (image_count < 2) throw std::runtime_error("Swapchain requires at least two images");
        const vk::ImageUsageFlags swapchain_usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        if ((surface_capabilities.supportedUsageFlags & swapchain_usage) != swapchain_usage) throw std::runtime_error("Vulkan surface does not support required swapchain image usage");

        const vk::SurfaceTransformFlagBitsKHR pre_transform     = surface_capabilities.currentTransform;
        constexpr vk::CompositeAlphaFlagBitsKHR composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
        const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->surface.surface, image_count, this->swapchain.format, this->swapchain.color_space, this->swapchain.extent, 1, swapchain_usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->swapchain.present_mode, VK_TRUE, *old_swapchain};
        this->swapchain.handle = vk::raii::SwapchainKHR{this->context.device, swapchain_create_info};

        this->swapchain.images = this->swapchain.handle.getImages();
        if (this->swapchain.images.empty()) throw std::runtime_error("Swapchain returned no images");
        this->swapchain.image_views.clear();
        this->swapchain.image_views.reserve(this->swapchain.images.size());
        this->swapchain.image_layouts.assign(this->swapchain.images.size(), vk::ImageLayout::eUndefined);
        for (const vk::Image image : this->swapchain.images) {
            const vk::ImageViewCreateInfo image_view_create_info{{}, image, vk::ImageViewType::e2D, this->swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
            this->swapchain.image_views.emplace_back(this->context.device, image_view_create_info);
        }

        this->sync.image_in_flight_frame.assign(this->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        this->sync.render_finished_semaphores.clear();
        constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
        this->sync.render_finished_semaphores.reserve(this->swapchain.images.size());
        for (std::uint32_t image_index = 0; image_index < this->swapchain.images.size(); ++image_index) this->sync.render_finished_semaphores.emplace_back(this->context.device, semaphore_create_info);
    }

    void Spectra::recreate_swapchain() {
        int width  = 0;
        int height = 0;
        while (width == 0 || height == 0) {
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (width == 0 || height == 0) {
                glfwWaitEvents();
                if (glfwWindowShouldClose(this->surface.window.get())) return;
            }
        }

        this->context.device.waitIdle();
        this->destroy_imgui();
        vk::raii::SwapchainKHR old_swapchain = std::move(this->swapchain.handle);
        this->swapchain.image_views.clear();
        this->swapchain.images.clear();
        this->swapchain.image_layouts.clear();
        this->create_swapchain(std::move(old_swapchain));
        this->surface.resize_requested = false;
        this->create_imgui();
    }
} // namespace spectra
