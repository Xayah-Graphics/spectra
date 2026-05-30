module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_mono.h>
#include <roboto/roboto_regular.h>
#include <vulkan/vulkan_raii.hpp>

#include "spectra_gpu.h"

module spectra;
import std;

namespace xayah {
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

    [[nodiscard]] std::string spectra_scene_title_text(const SpectraScene& scene) {
        const std::string title = scene.scene_path.filename().string();
        if (!title.empty()) return title;
        return scene.scene_path.string();
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type");
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

    void apply_imgui_style(const bool viewports) {
        ImGui::StyleColorsDark();
        ImGuiStyle& style                  = ImGui::GetStyle();
        style.WindowRounding               = 0.0f;
        style.WindowBorderSize             = 0.0f;
        style.ColorButtonPosition          = ImGuiDir_Right;
        style.FrameRounding                = 2.0f;
        style.FrameBorderSize              = 1.0f;
        style.GrabRounding                 = 4.0f;
        style.IndentSpacing                = 12.0f;
        style.Colors[ImGuiCol_WindowBg]    = ImVec4{0.2f, 0.2f, 0.2f, 1.0f};
        style.Colors[ImGuiCol_MenuBarBg]   = ImVec4{0.2f, 0.2f, 0.2f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4{0.2f, 0.2f, 0.2f, 1.0f};
        style.Colors[ImGuiCol_PopupBg]     = ImVec4{0.135f, 0.135f, 0.135f, 1.0f};
        style.Colors[ImGuiCol_Border]      = ImVec4{0.4f, 0.4f, 0.4f, 0.5f};
        style.Colors[ImGuiCol_FrameBg]     = ImVec4{0.05f, 0.05f, 0.05f, 0.5f};

        const ImVec4 normal_color = ImVec4{0.465f, 0.465f, 0.525f, 1.0f};
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

        const ImVec4 active_color = ImVec4{0.365f, 0.365f, 0.425f, 1.0f};
        constexpr std::array active_colors{
            ImGuiCol_HeaderActive,
            ImGuiCol_SliderGrabActive,
            ImGuiCol_ButtonActive,
            ImGuiCol_ResizeGripActive,
            ImGuiCol_SeparatorActive,
        };
        for (const ImGuiCol color_id : active_colors) style.Colors[color_id] = active_color;

        const ImVec4 hovered_color = ImVec4{0.565f, 0.565f, 0.625f, 1.0f};
        constexpr std::array hovered_colors{
            ImGuiCol_HeaderHovered,
            ImGuiCol_ButtonHovered,
            ImGuiCol_FrameBgHovered,
            ImGuiCol_ResizeGripHovered,
            ImGuiCol_SeparatorHovered,
        };
        for (const ImGuiCol color_id : hovered_colors) style.Colors[color_id] = hovered_color;

        style.Colors[ImGuiCol_TitleBgActive]    = ImVec4{0.465f, 0.465f, 0.465f, 1.0f};
        style.Colors[ImGuiCol_TitleBg]          = ImVec4{0.125f, 0.125f, 0.125f, 1.0f};
        style.Colors[ImGuiCol_Tab]              = ImVec4{0.05f, 0.05f, 0.05f, 0.5f};
        style.Colors[ImGuiCol_TabHovered]       = ImVec4{0.465f, 0.495f, 0.525f, 1.0f};
        style.Colors[ImGuiCol_TabActive]        = ImVec4{0.282f, 0.290f, 0.302f, 1.0f};
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4{0.465f, 0.465f, 0.465f, 0.350f};
        style.Colors[ImGuiCol_ButtonActive]     = static_cast<ImVec4>(ImColor::HSV(0.3F, 0.5F, 0.5F));
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
        if (viewports) {
            style.WindowRounding              = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
    }

    struct Spectra::SpectraState {
        struct {
            vk::raii::Context context;
            vk::raii::Instance instance{nullptr};
            vk::raii::DebugUtilsMessengerEXT debug_messenger{nullptr};
            vk::raii::PhysicalDevice physical_device{nullptr};
            vk::raii::Device device{nullptr};
            vk::raii::Queue graphics_queue{nullptr};
            std::uint32_t graphics_queue_index{0};
            vk::raii::CommandPool command_pool{nullptr};
        } context;

        struct {
            std::shared_ptr<GLFWwindow> window{nullptr};
            vk::raii::SurfaceKHR surface{nullptr};
            vk::Extent2D extent{};
            bool resize_requested{false};
            bool glfw_initialized{false};
        } surface;

        struct {
            std::string base{"Spectra"};
            float refresh_timer{0.0f};
            std::uint64_t frame_count{0};
        } window_title;

        struct {
            vk::raii::SwapchainKHR handle{nullptr};
            vk::Format format{};
            vk::ColorSpaceKHR color_space{};
            vk::Extent2D extent{};
            std::uint32_t image_count{0};
            vk::PresentModeKHR present_mode{};
            vk::ImageUsageFlags usage{};
            std::vector<vk::Image> images{};
            std::vector<vk::ImageLayout> image_layouts{};
            std::vector<vk::raii::ImageView> image_views{};
        } swapchain;

        struct {
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::Format color_format{vk::Format::eUndefined};
            std::uint32_t min_image_count{2};
            std::uint32_t image_count{2};
            bool docking{true};
            bool viewports{false};
            bool initialized{false};
        } imgui;

        struct {
            bool dock_layout_initialized{false};
            bool camera_visible{true};
            bool scene_browser_visible{true};
            bool inspector_visible{true};
            bool settings_visible{true};
            bool environment_visible{true};
            bool tonemapper_visible{true};
            bool statistics_visible{true};
            bool viewport_known{false};
            bool viewport_hovered{false};
            bool viewport_focused{false};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            std::array<int, 2> viewport_framebuffer_size{0, 0};
        } ui;

        std::unique_ptr<SpectraScene> spectra_scene{};
        std::unique_ptr<SpectraGpuPathtracer> gpu_pathtracer{};
        std::unique_ptr<SpectraGpuRuntime> gpu_runtime{};

        struct {
            bool candidate_known{false};
            bool pathtracer_created{false};
            bool rebuilding{false};
            float stable_seconds{0.0f};
            std::array<int, 2> candidate_resolution{0, 0};
            std::array<int, 2> active_resolution{0, 0};
        } render_resolution_sync;

        struct {
            bool initialized{false};
            bool input_enabled{false};
            float speed{1.0f};
            float fov_degrees{60.0f};
            float basis_handedness{1.0f};
            bool mouse_position_known{false};
            spectra::Point3f eye{0.0f, 0.0f, 0.0f};
            spectra::Point3f center{0.0f, 0.0f, 1.0f};
            spectra::Vector3f up{0.0f, 1.0f, 0.0f};
            std::array<float, 2> mouse_position{0.0f, 0.0f};
            spectra::Transform moving_from_camera{};
            spectra::Transform camera_from_world{};
        } camera;

        struct RollingFloatAverage {
            static constexpr std::size_t sample_count{100};

            std::array<float, sample_count> values{};
            std::size_t count{0};
            std::size_t cursor{0};
            float sum{0.0f};

            void clear() {
                this->values.fill(0.0f);
                this->count  = 0;
                this->cursor = 0;
                this->sum    = 0.0f;
            }

            void add(const float value) {
                if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("Rolling statistic value must be finite and non-negative");
                if (this->count < sample_count) {
                    this->values[this->cursor] = value;
                    this->sum += value;
                    ++this->count;
                } else {
                    this->sum -= this->values[this->cursor];
                    this->values[this->cursor] = value;
                    this->sum += value;
                }
                this->cursor = (this->cursor + 1) % sample_count;
            }

            [[nodiscard]] bool has_value() const {
                return this->count > 0;
            }

            [[nodiscard]] float average() const {
                if (this->count == 0) return 0.0f;
                return this->sum / static_cast<float>(this->count);
            }
        };

        struct {
            RollingFloatAverage frame_milliseconds{};
            RollingFloatAverage throughput_mspp{};
            std::uint64_t current_frame_id{0};
            std::uint32_t active_frame_index{0};
            std::uint32_t active_swapchain_image_index{0};
            float last_frame_milliseconds{0.0f};
            float last_valid_throughput_mspp{0.0f};
            bool has_throughput{false};
            bool last_frame_rendered_sample{false};
        } statistics;

        struct {
            std::uint32_t frame_count{2};
            std::uint32_t frame_index{0};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Semaphore> image_available_semaphores{};
            std::vector<vk::raii::Semaphore> render_finished_semaphores{};
            std::vector<std::uint32_t> image_in_flight_frame{};
            std::vector<vk::raii::Fence> in_flight_fences{};
        } sync;
    };

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try : state{std::make_unique<SpectraState>()} {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->state->surface.glfw_initialized = true;
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};
        this->state->window_title.base = app_name_string;

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

            const std::vector<vk::LayerProperties> available_layers = this->state->context.context.enumerateInstanceLayerProperties();
            for (const char* required_layer : enabled_instance_layers) {
                if (const auto found = std::ranges::find(available_layers, std::string_view{required_layer}, [](const vk::LayerProperties& layer) { return std::string_view{layer.layerName.data()}; }); found == available_layers.end()) throw std::runtime_error(std::string{"Required Vulkan layer not supported: "} + required_layer);
            }
            const std::vector<vk::ExtensionProperties> available_extensions = this->state->context.context.enumerateInstanceExtensionProperties();
            for (const char* required_extension : enabled_instance_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) throw std::runtime_error(std::string{"Required Vulkan instance extension not supported: "} + required_extension);
            }

            const vk::ApplicationInfo application_info{app_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), engine_name_string.c_str(), VK_MAKE_VERSION(1, 0, 0), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
            this->state->context.instance = vk::raii::Instance{this->state->context.context, instance_create_info};
        }
        {
            constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
                {},
                vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
                vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
                &debug_callback,
            };
            this->state->context.debug_messenger = this->state->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
        }
        {
            if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
            this->state->surface.window = std::shared_ptr<GLFWwindow>{glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr), [](GLFWwindow* window) { glfwDestroyWindow(window); }};
            if (this->state->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
            glfwSetWindowUserPointer(this->state->surface.window.get(), this);
            glfwSetFramebufferSizeCallback(this->state->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->state->surface.resize_requested = true; });
        }
        {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(*this->state->context.instance, this->state->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
            this->state->surface.surface = vk::raii::SurfaceKHR{this->state->context.instance, surface};
        }
        {
            int width  = 0;
            int height = 0;
            glfwGetFramebufferSize(this->state->surface.window.get(), &width, &height);
            if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
            this->state->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }
        {
            bool selected = false;
            for (const vk::raii::PhysicalDevice& physical_device : this->state->context.instance.enumeratePhysicalDevices()) {
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
                    if (!physical_device.getSurfaceSupportKHR(queue_family_index, this->state->surface.surface)) continue;
                    this->state->context.physical_device      = physical_device;
                    this->state->context.graphics_queue_index = queue_family_index;
                    selected                           = true;
                    break;
                }
                if (selected) break;
            }
            if (!selected) throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
        }
        {
            const auto supported_features = this->state->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
            if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

            vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}};
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = VK_TRUE;
            enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = VK_TRUE;

            constexpr std::array queue_priorities{1.0f};
            const vk::DeviceQueueCreateInfo queue_create_info{{}, this->state->context.graphics_queue_index, 1, queue_priorities.data()};
            const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
            this->state->context.device         = vk::raii::Device{this->state->context.physical_device, device_create_info};
            this->state->context.graphics_queue = vk::raii::Queue{this->state->context.device, this->state->context.graphics_queue_index, 0};
        }
        {
            const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->state->context.graphics_queue_index};
            this->state->context.command_pool = vk::raii::CommandPool{this->state->context.device, command_pool_create_info};
        }
        this->create_swapchain();
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
            const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->state->context.command_pool, vk::CommandBufferLevel::ePrimary, this->state->sync.frame_count};
            this->state->sync.command_buffers = vk::raii::CommandBuffers{this->state->context.device, command_buffer_allocate_info};
            if (this->state->sync.command_buffers.size() != this->state->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

            this->state->sync.image_available_semaphores.reserve(this->state->sync.frame_count);
            this->state->sync.in_flight_fences.reserve(this->state->sync.frame_count);
            for (std::uint32_t frame_index = 0; frame_index < this->state->sync.frame_count; ++frame_index) {
                this->state->sync.image_available_semaphores.emplace_back(this->state->context.device, semaphore_create_info);
                this->state->sync.in_flight_fences.emplace_back(this->state->context.device, fence_create_info);
            }
        }
        this->create_imgui();
        {
            if (this->state->gpu_runtime != nullptr) throw std::runtime_error("Spectra GPU runtime is already initialized");
            this->state->gpu_runtime = std::make_unique<SpectraGpuRuntime>();
        }
    } catch (...) {
        this->destroy_imgui();
        if (this->state->surface.glfw_initialized) glfwTerminate();
        throw;
    }

    Spectra::~Spectra() noexcept {
        try {
            if (*this->state->context.device) this->state->context.device.waitIdle();
        } catch (...) {
        }

        this->unload_pathtracer_noexcept();
        this->unload_spectra_scene_noexcept();
        this->state->gpu_runtime.reset();
        this->destroy_imgui();
        this->state->sync.command_buffers.clear();
        this->state->sync.in_flight_fences.clear();
        this->state->sync.image_in_flight_frame.clear();
        this->state->sync.render_finished_semaphores.clear();
        this->state->sync.image_available_semaphores.clear();
        this->state->context.command_pool = nullptr;
        this->state->swapchain.image_views.clear();
        this->state->swapchain.handle = nullptr;
        this->state->swapchain.image_layouts.clear();
        this->state->swapchain.images.clear();
        this->state->context.graphics_queue  = nullptr;
        this->state->context.device          = nullptr;
        this->state->surface.surface         = nullptr;
        this->state->surface.window          = nullptr;
        this->state->context.physical_device = nullptr;
        this->state->context.debug_messenger = nullptr;
        this->state->context.instance        = nullptr;
        if (this->state->surface.glfw_initialized) glfwTerminate();
        this->state->surface.glfw_initialized = false;
    }

    void Spectra::create_imgui() {
        if (this->state->imgui.initialized) throw std::runtime_error("ImGui is already initialized");
        if (this->state->surface.window.get() == nullptr) throw std::runtime_error("Cannot initialize ImGui without a GLFW window");
        if (this->state->swapchain.images.empty()) throw std::runtime_error("Cannot initialize ImGui without swapchain images");

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
            this->state->imgui.descriptor_pool = vk::raii::DescriptorPool{this->state->context.device, descriptor_pool_create_info};
            this->state->imgui.color_format    = this->state->swapchain.format;
            this->state->imgui.min_image_count = std::max(2u, this->state->sync.frame_count);
            this->state->imgui.image_count     = static_cast<std::uint32_t>(this->state->swapchain.images.size());
            if (this->state->imgui.image_count < this->state->imgui.min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            context_created = true;

            ImGuiIO& io = ImGui::GetIO();
            if (this->state->imgui.docking) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            if (this->state->imgui.viewports) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            load_imgui_fonts();
            apply_imgui_style(this->state->imgui.viewports);

            if (!ImGui_ImplGlfw_InitForVulkan(this->state->surface.window.get(), true)) throw std::runtime_error("ImGui_ImplGlfw_InitForVulkan failed");
            glfw_backend_initialized = true;

            auto color_attachment_format = static_cast<VkFormat>(this->state->imgui.color_format);
            VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info{};
            pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = VK_API_VERSION_1_4;
            init_info.Instance                                     = static_cast<VkInstance>(*this->state->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->state->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->state->context.device);
            init_info.QueueFamily                                  = this->state->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->state->context.graphics_queue);
            init_info.DescriptorPool                               = static_cast<VkDescriptorPool>(*this->state->imgui.descriptor_pool);
            init_info.MinImageCount                                = this->state->imgui.min_image_count;
            init_info.ImageCount                                   = this->state->imgui.image_count;
            init_info.PipelineInfoMain.MSAASamples                 = VK_SAMPLE_COUNT_1_BIT;
            init_info.UseDynamicRendering                          = true;
            init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering_create_info;
            if (!ImGui_ImplVulkan_Init(&init_info)) throw std::runtime_error("ImGui_ImplVulkan_Init failed");
            vulkan_backend_initialized = true;
            this->state->imgui.initialized    = true;
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->state->imgui.descriptor_pool = nullptr;
            this->state->imgui.color_format    = vk::Format::eUndefined;
            this->state->imgui.min_image_count = 2;
            this->state->imgui.image_count     = 2;
            this->state->imgui.initialized     = false;
            throw;
        }
    }

    void Spectra::destroy_imgui() noexcept {
        if (this->state->gpu_pathtracer != nullptr) this->state->gpu_pathtracer->release_viewport_descriptors_noexcept();
        if (this->state->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->state->imgui.descriptor_pool = nullptr;
        this->state->imgui.color_format    = vk::Format::eUndefined;
        this->state->imgui.min_image_count = 2;
        this->state->imgui.image_count     = 2;
        this->state->imgui.initialized     = false;
        this->state->ui.dock_layout_initialized = false;
    }

    void Spectra::unload_spectra_scene_noexcept() noexcept {
        if (this->state->spectra_scene != nullptr) {
            this->state->spectra_scene->unload_noexcept();
            this->state->spectra_scene.reset();
        }
    }

    void Spectra::create_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot create Spectra GPU pathtracer without a loaded Spectra scene");
        if (this->state->gpu_pathtracer != nullptr) throw std::runtime_error("Spectra GPU pathtracer is already loaded");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot create Spectra GPU pathtracer with a non-positive resolution");
        try {
            if (this->state->gpu_runtime == nullptr) throw std::runtime_error("Spectra GPU runtime is not initialized");
            this->state->gpu_runtime->reset_options_for_scene();
            this->state->gpu_pathtracer = std::make_unique<SpectraGpuPathtracer>(*this->state->spectra_scene, resolution, this->state->context.physical_device, this->state->context.device, this->state->sync.frame_count);
            this->state->spectra_scene->set_runtime_metadata(this->state->gpu_pathtracer->film_resolution(), this->state->gpu_pathtracer->sampler_sample_count(), this->state->gpu_pathtracer->camera_from_world_transform());
            this->state->render_resolution_sync.active_resolution = resolution;
            this->state->render_resolution_sync.pathtracer_created  = true;
        } catch (...) {
            if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            throw;
        }
    }

    void Spectra::rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution) {
        if (this->state->render_resolution_sync.rebuilding) throw std::runtime_error("Spectra GPU pathtracer resolution rebuild is already active");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot rebuild Spectra GPU pathtracer with a non-positive resolution");
        if (this->state->render_resolution_sync.pathtracer_created && this->state->render_resolution_sync.active_resolution == resolution) return;

        const bool preserve_camera = this->state->camera.initialized;
        const SpectraCameraPose preserved_pose{this->state->camera.eye, this->state->camera.center, this->state->camera.up, this->state->camera.basis_handedness};
        const float preserved_speed     = this->state->camera.speed;
        const int preserved_samples     = this->state->gpu_pathtracer == nullptr ? 0 : this->state->gpu_pathtracer->target_sample_count();
        const float preserved_exposure  = this->state->gpu_pathtracer == nullptr ? 1.0f : this->state->gpu_pathtracer->current_exposure();
        this->state->render_resolution_sync.rebuilding = true;
        try {
            this->state->context.device.waitIdle();
            if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
            this->unload_pathtracer_noexcept();
            this->create_pathtracer_for_resolution(resolution);
            if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU pathtracer was not created");
            if (preserved_samples > 0) this->state->gpu_pathtracer->set_target_sample_count(preserved_samples);
            this->state->gpu_pathtracer->set_exposure(preserved_exposure);
            if (preserve_camera) {
                this->state->camera.camera_from_world          = this->state->spectra_scene->camera_from_world;
                this->state->camera.eye                        = preserved_pose.eye;
                this->state->camera.center                     = preserved_pose.center;
                this->state->camera.up                         = preserved_pose.up;
                this->state->camera.basis_handedness           = preserved_pose.basis_handedness;
                this->state->camera.speed                      = preserved_speed;
                this->state->camera.fov_degrees                = spectra_camera_fov_degrees(*this->state->spectra_scene);
                this->state->camera.mouse_position_known       = false;
                this->state->camera.input_enabled              = false;
                this->state->camera.moving_from_camera         = moving_from_camera_from_pose(this->state->camera.camera_from_world, preserved_pose);
            } else
                this->initialize_camera_state();
            this->clear_pathtracer_throughput_statistics();
            this->state->statistics.last_frame_rendered_sample = false;
            this->state->render_resolution_sync.rebuilding     = false;
        } catch (...) {
            this->state->render_resolution_sync.rebuilding = false;
            throw;
        }
    }

    void Spectra::unload_pathtracer_noexcept() noexcept {
        if (this->state->gpu_runtime != nullptr) this->state->gpu_runtime->wait_gpu_noexcept();
        this->state->gpu_pathtracer.reset();
        this->state->render_resolution_sync.pathtracer_created  = false;
        this->state->render_resolution_sync.active_resolution = {0, 0};
    }

    void Spectra::observe_viewport_render_resolution(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid while tracking viewport resolution");
        if (!this->state->render_resolution_sync.candidate_known || this->state->render_resolution_sync.candidate_resolution != resolution) {
            this->state->render_resolution_sync.candidate_known     = true;
            this->state->render_resolution_sync.candidate_resolution = resolution;
            this->state->render_resolution_sync.stable_seconds      = 0.0f;
            return;
        }
        this->state->render_resolution_sync.stable_seconds += io.DeltaTime;
    }

    void Spectra::synchronize_render_resolution() {
        constexpr float resolution_stability_seconds = 0.3f;
        if (this->state->spectra_scene == nullptr) return;
        if (!this->state->render_resolution_sync.candidate_known) return;
        if (this->state->render_resolution_sync.stable_seconds < resolution_stability_seconds) return;
        if (this->state->render_resolution_sync.pathtracer_created && this->state->render_resolution_sync.active_resolution == this->state->render_resolution_sync.candidate_resolution) return;
        this->rebuild_pathtracer_for_resolution(this->state->render_resolution_sync.candidate_resolution);
    }

    [[nodiscard]] bool Spectra::pathtracer_ready() const {
        return this->state->render_resolution_sync.pathtracer_created && this->state->gpu_pathtracer != nullptr;
    }

    void Spectra::run_interactive_scene(const std::filesystem::path& scene_path) {
        if (this->state->spectra_scene != nullptr) throw std::runtime_error("Spectra scene is already active");
        if (this->state->gpu_pathtracer != nullptr) throw std::runtime_error("Spectra GPU pathtracer is already active");

        std::exception_ptr failure{};
        try {
            {
                std::unique_ptr<SpectraScene> loaded_scene = std::make_unique<SpectraScene>();
                try {
                    loaded_scene->load(scene_path);
                    this->state->spectra_scene = std::move(loaded_scene);
                } catch (...) {
                    loaded_scene->unload_noexcept();
                    throw;
                }
            }
            this->render_loop();
        } catch (...) {
            failure = std::current_exception();
        }

        try {
            this->state->context.device.waitIdle();
        } catch (...) {
            if (failure == nullptr) failure = std::current_exception();
        }
        this->unload_pathtracer_noexcept();
        this->unload_spectra_scene_noexcept();
        if (failure != nullptr) std::rethrow_exception(failure);
    }

    void Spectra::render_loop() {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot enter Spectra render loop without an active Spectra scene");
        while (!glfwWindowShouldClose(this->state->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
        this->state->context.device.waitIdle();
    }

    void Spectra::update_window_title(const float delta_seconds) {
        if (this->state->surface.window == nullptr) throw std::runtime_error("Cannot update window title without a GLFW window");

        ++this->state->window_title.frame_count;
        this->state->window_title.refresh_timer += delta_seconds;
        if (this->state->window_title.refresh_timer <= 1.0f) return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.Framerate <= 0.0f) return;

        std::uint32_t width  = this->state->swapchain.extent.width;
        std::uint32_t height = this->state->swapchain.extent.height;
        if (this->state->render_resolution_sync.pathtracer_created) {
            width  = static_cast<std::uint32_t>(this->state->render_resolution_sync.active_resolution[0]);
            height = static_cast<std::uint32_t>(this->state->render_resolution_sync.active_resolution[1]);
        } else if (this->state->ui.viewport_known && this->state->ui.viewport_framebuffer_size[0] > 0 && this->state->ui.viewport_framebuffer_size[1] > 0) {
            width  = static_cast<std::uint32_t>(this->state->ui.viewport_framebuffer_size[0]);
            height = static_cast<std::uint32_t>(this->state->ui.viewport_framebuffer_size[1]);
        }

        const std::string scene_title = this->state->spectra_scene == nullptr ? "No Scene" : spectra_scene_title_text(*this->state->spectra_scene);
        const std::array<int, 2> sample_range = this->state->spectra_scene == nullptr ? std::array<int, 2>{0, 0} : this->pathtracer_sample_range();
        const std::string title       = std::format("{} - {} | Spectra GPU Pathtracer | {}x{} | sample {}/{} | {:.0f} FPS / {:.3f}ms | frame {}", this->state->window_title.base, scene_title, width, height, sample_range[0], sample_range[1], io.Framerate, 1000.0f / io.Framerate, this->state->window_title.frame_count);
        glfwSetWindowTitle(this->state->surface.window.get(), title.c_str());
        this->state->window_title.refresh_timer = 0.0f;
    }

    void Spectra::clear_pathtracer_throughput_statistics() {
        this->state->statistics.throughput_mspp.clear();
        this->state->statistics.last_valid_throughput_mspp = 0.0f;
        this->state->statistics.has_throughput             = false;
    }

    void Spectra::update_frame_statistics(const FrameState& frame, const bool rendered_sample, const bool reset_accumulation, const std::uint64_t sample_pixels) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!std::isfinite(io.DeltaTime) || !(io.DeltaTime > 0.0f)) throw std::runtime_error("ImGui frame delta time must be finite and positive for statistics");
        if (!rendered_sample && sample_pixels != 0) throw std::runtime_error("Spectra GPU pathtracer frame statistics reported sample-pixels without rendering a sample");
        if (rendered_sample && sample_pixels == 0) throw std::runtime_error("Spectra GPU pathtracer frame statistics rendered a sample without sample-pixels");

        const float frame_milliseconds = io.DeltaTime * 1000.0f;
        this->state->statistics.current_frame_id             = this->state->window_title.frame_count + 1;
        this->state->statistics.active_frame_index           = frame.frame_index;
        this->state->statistics.active_swapchain_image_index = frame.image_index;
        this->state->statistics.last_frame_milliseconds      = frame_milliseconds;
        this->state->statistics.last_frame_rendered_sample   = rendered_sample;
        this->state->statistics.frame_milliseconds.add(frame_milliseconds);

        if (reset_accumulation) this->clear_pathtracer_throughput_statistics();
        if (rendered_sample) {
            const float throughput = (static_cast<float>(sample_pixels) / 1000000.0f) / io.DeltaTime;
            this->state->statistics.throughput_mspp.add(throughput);
            this->state->statistics.last_valid_throughput_mspp = throughput;
            this->state->statistics.has_throughput             = true;
        }
    }

    [[nodiscard]] Spectra::PathtracerStatus Spectra::pathtracer_status() const {
        PathtracerStatus status{};
        status.sample_range = this->pathtracer_sample_range();
        if (this->state->render_resolution_sync.rebuilding) {
            status.state = "Rebuilding";
            return status;
        }
        if (!this->state->render_resolution_sync.pathtracer_created) {
            status.state = this->state->render_resolution_sync.candidate_known ? "Pending Resolution" : "Waiting for Viewport";
            return status;
        }
        status.uses_external_completion = this->state->gpu_pathtracer != nullptr;
        if (this->state->gpu_pathtracer == nullptr) {
            status.state = "Unavailable";
            return status;
        }
        status.state = status.sample_range[0] >= status.sample_range[1] ? "Completed" : "Sampling";
        return status;
    }

    [[nodiscard]] VkDescriptorSet Spectra::pathtracer_viewport_descriptor() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU pathtracer viewport descriptor requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->active_descriptor();
    }

    [[nodiscard]] std::array<int, 2> Spectra::pathtracer_sample_range() const {
        if (this->state->gpu_pathtracer == nullptr) return {0, 0};
        return {this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count()};
    }

    [[nodiscard]] float Spectra::pathtracer_initial_move_scale() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU camera move scale requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->camera_initial_move_scale();
    }

    [[nodiscard]] vk::Semaphore Spectra::pathtracer_complete_semaphore() const {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU completion semaphore requested without an active Spectra GPU session");
        return this->state->gpu_pathtracer->active_cuda_complete_semaphore();
    }

    [[nodiscard]] Spectra::PathtracerFrameResult Spectra::render_pathtracer_frame(const FrameState& frame) {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot render Spectra GPU pathtracer without an active Spectra GPU session");
        const SpectraGpuPathtracer::RenderFrameResult render_result = this->state->gpu_pathtracer->render_frame(frame.frame_index, this->state->camera.moving_from_camera);
        return {render_result.sample_pixels, render_result.rendered_sample, render_result.reset_accumulation};
    }

    void Spectra::record_pathtracer_output(const vk::raii::CommandBuffer& command_buffer) {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot record Spectra GPU pathtracer output without an active Spectra GPU session");
        this->state->gpu_pathtracer->record_copy(command_buffer);
    }

    void Spectra::request_pathtracer_accumulation_reset() {
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Cannot reset Spectra GPU accumulation without an active Spectra GPU session");
        this->state->gpu_pathtracer->request_reset_accumulation();
        this->clear_pathtracer_throughput_statistics();
    }

    void Spectra::initialize_camera_state() {
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot initialize camera state without an active Spectra scene");
        const float initial_move_scale = this->pathtracer_initial_move_scale();
        if (!std::isfinite(initial_move_scale) || !(initial_move_scale > 0.0f)) throw std::runtime_error("Initial camera move scale must be finite and positive");
        this->state->camera.camera_from_world = this->state->spectra_scene->camera_from_world;
        if (this->state->gpu_pathtracer == nullptr) throw std::runtime_error("Spectra GPU camera focus bounds requested without an active Spectra GPU session");
        const SpectraCameraPose pose   = camera_pose_from_base_transform(this->state->camera.camera_from_world, this->state->gpu_pathtracer->camera_initial_focus_bounds());
        this->state->camera.initialized       = true;
        this->state->camera.input_enabled     = false;
        this->state->camera.speed             = initial_move_scale * 60.0f;
        this->state->camera.fov_degrees       = spectra_camera_fov_degrees(*this->state->spectra_scene);
        this->state->camera.basis_handedness  = pose.basis_handedness;
        this->state->camera.eye               = pose.eye;
        this->state->camera.center            = pose.center;
        this->state->camera.up                = pose.up;
        this->state->camera.mouse_position    = {0.0f, 0.0f};
        this->state->camera.mouse_position_known = false;
        this->state->camera.moving_from_camera   = spectra::Transform{};
    }

    void Spectra::set_camera_speed(const float speed) {
        if (!std::isfinite(speed) || !(speed > 0.0f)) throw std::runtime_error("Camera speed must be finite and positive");
        this->state->camera.speed = speed;
    }

    void Spectra::reset_camera() {
        if (!this->state->camera.initialized) throw std::runtime_error("Cannot reset camera before camera state is initialized");
        if (!this->pathtracer_ready()) throw std::runtime_error("Cannot reset camera without an active Spectra GPU pathtracer");
        const SpectraCameraPose pose  = camera_pose_from_base_transform(this->state->camera.camera_from_world, this->state->gpu_pathtracer->camera_initial_focus_bounds());
        this->state->camera.eye              = pose.eye;
        this->state->camera.center           = pose.center;
        this->state->camera.up               = pose.up;
        this->state->camera.basis_handedness = pose.basis_handedness;
        this->state->camera.mouse_position_known = false;
        this->state->camera.moving_from_camera   = spectra::Transform{};
        this->request_pathtracer_accumulation_reset();
    }

    void Spectra::process_camera_input(GLFWwindow* window) {
        if (window == nullptr) throw std::runtime_error("Cannot process camera input without a GLFW window");
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(window, GLFW_TRUE);

        const ImVec2 mouse_position = io.MousePos;
        const bool in_viewport_rect = this->state->ui.viewport_known && mouse_position.x >= this->state->ui.viewport_position[0] && mouse_position.x < this->state->ui.viewport_position[0] + this->state->ui.viewport_size[0] && mouse_position.y >= this->state->ui.viewport_position[1] && mouse_position.y < this->state->ui.viewport_position[1] + this->state->ui.viewport_size[1];
        this->state->camera.input_enabled  = in_viewport_rect && (this->state->ui.viewport_hovered || this->state->ui.viewport_focused) && !io.WantTextInput;
        if (!this->state->camera.input_enabled) {
            this->state->camera.mouse_position_known = false;
            return;
        }
        if (!this->state->camera.initialized) throw std::runtime_error("Cannot process camera input before camera state is initialized");

        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            this->reset_camera();
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false)) this->set_camera_speed(this->state->camera.speed * 2.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) this->set_camera_speed(this->state->camera.speed * 0.5f);

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl;
        const bool alt   = io.KeyAlt;
        SpectraCameraPose pose{this->state->camera.eye, this->state->camera.center, this->state->camera.up, this->state->camera.basis_handedness};
        bool camera_changed = false;
        if (!alt) {
            if (!std::isfinite(io.DeltaTime) || io.DeltaTime < 0.0f) throw std::runtime_error("ImGui delta time is invalid");
            float key_motion_factor = io.DeltaTime;
            if (shift) key_motion_factor *= 5.0f;
            if (ctrl) key_motion_factor *= 0.1f;
            if (key_motion_factor > 0.0f) {
                if (ImGui::IsKeyDown(ImGuiKey_W)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_S)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state->camera.speed, true) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) camera_changed = camera_key_motion(pose, {key_motion_factor, 0.0f}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow)) camera_changed = camera_key_motion(pose, {-key_motion_factor, 0.0f}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) camera_changed = camera_key_motion(pose, {0.0f, key_motion_factor}, this->state->camera.speed, false) || camera_changed;
                if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) camera_changed = camera_key_motion(pose, {0.0f, -key_motion_factor}, this->state->camera.speed, false) || camera_changed;
            }
        }

        const std::array<float, 2> viewport_size = this->state->ui.viewport_size;
        if (!std::isfinite(viewport_size[0]) || !std::isfinite(viewport_size[1]) || !(viewport_size[0] > 0.0f) || !(viewport_size[1] > 0.0f)) throw std::runtime_error("Camera viewport size must be finite and positive");
        const std::array<float, 2> current_mouse_position{mouse_position.x, mouse_position.y};
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle, false) || ImGui::IsMouseClicked(ImGuiMouseButton_Right, false)) {
            this->state->camera.mouse_position       = current_mouse_position;
            this->state->camera.mouse_position_known = true;
        }

        const bool left_dragging   = ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
        const bool middle_dragging = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 1.0f);
        const bool right_dragging  = ImGui::IsMouseDragging(ImGuiMouseButton_Right, 1.0f);
        if (left_dragging || middle_dragging || right_dragging) {
            if (!this->state->camera.mouse_position_known) {
                this->state->camera.mouse_position       = current_mouse_position;
                this->state->camera.mouse_position_known = true;
            }
            const std::array<float, 2> mouse_displacement{
                (current_mouse_position[0] - this->state->camera.mouse_position[0]) / viewport_size[0],
                (current_mouse_position[1] - this->state->camera.mouse_position[1]) / viewport_size[1],
            };
            if (left_dragging) {
                if ((ctrl && shift) || alt) camera_changed = camera_orbit(pose, {mouse_displacement[0], -mouse_displacement[1]}, true) || camera_changed;
                else if (shift) camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
                else if (ctrl) camera_changed = camera_pan(pose, mouse_displacement, this->state->camera.fov_degrees, viewport_size) || camera_changed;
                else camera_changed = camera_orbit(pose, mouse_displacement, false) || camera_changed;
            } else if (middle_dragging) {
                camera_changed = camera_pan(pose, mouse_displacement, this->state->camera.fov_degrees, viewport_size) || camera_changed;
            } else if (right_dragging) {
                camera_changed = camera_dolly(pose, mouse_displacement) || camera_changed;
            }
            this->state->camera.mouse_position = current_mouse_position;
        }

        if (io.MouseWheel != 0.0f && !shift) {
            constexpr float wheel_speed = 10.0f;
            const float wheel_value     = io.MouseWheel * wheel_speed;
            const float dolly_delta     = wheel_value * std::abs(wheel_value) / viewport_size[0];
            camera_changed              = camera_dolly(pose, {dolly_delta, 0.0f}) || camera_changed;
        }

        if (camera_changed) {
            this->state->camera.eye                  = pose.eye;
            this->state->camera.center               = pose.center;
            this->state->camera.up                   = pose.up;
            this->state->camera.basis_handedness     = pose.basis_handedness;
            this->state->camera.moving_from_camera   = moving_from_camera_from_pose(this->state->camera.camera_from_world, pose);
            this->request_pathtracer_accumulation_reset();
        }
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (this->state->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present = false;
        frame.frame_index            = this->state->sync.frame_index;
        if (this->state->context.device.waitForFences(*this->state->sync.in_flight_fences[frame.frame_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->state->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->state->sync.image_available_semaphores[frame.frame_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_index = this->state->sync.image_in_flight_frame.at(frame.image_index); previous_frame_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->state->context.device.waitForFences(*this->state->sync.in_flight_fences.at(previous_frame_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->state->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_index;
        this->state->context.device.resetFences(*this->state->sync.in_flight_fences[frame.frame_index]);
        if (!this->state->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (this->state->spectra_scene == nullptr) throw std::runtime_error("Cannot update Spectra GPU pathtracer frame without an active Spectra scene");
        this->synchronize_render_resolution();
        if (this->pathtracer_ready()) {
            this->process_camera_input(this->state->surface.window.get());
            const PathtracerFrameResult render_result = this->render_pathtracer_frame(frame);
            frame.wait_for_external_completion = true;
            frame.external_completion_semaphore = this->pathtracer_complete_semaphore();
            this->update_frame_statistics(frame, render_result.rendered_sample, render_result.reset_accumulation, render_result.sample_pixels);
        } else {
            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(this->state->surface.window.get(), GLFW_TRUE);
            this->update_frame_statistics(frame, false, false, 0);
        }
        return true;
    }

    void Spectra::record_frame(const FrameState& frame) {
        this->draw_main_menu();
        this->draw_dockspace();
        this->draw_viewport_window();
        this->draw_camera_window();
        this->draw_scene_browser_window();
        this->draw_inspector_window();
        this->draw_settings_window();
        this->draw_environment_window();
        this->draw_tonemapper_window();
        this->draw_statistics_window();

        const vk::raii::CommandBuffer& command_buffer = this->state->sync.command_buffers[frame.frame_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);
        if (this->pathtracer_ready()) this->record_pathtracer_output(command_buffer);

        {
            const vk::ImageMemoryBarrier2 color_barrier{
                vk::PipelineStageFlagBits2::eAllCommands,
                {},
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentWrite,
                this->state->swapchain.image_layouts[frame.image_index],
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED,
                VK_QUEUE_FAMILY_IGNORED,
                this->state->swapchain.images[frame.image_index],
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->state->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array<float, 4> clear_color{0.02f, 0.02f, 0.025f, 1.0f};
        const vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->state->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            color_clear_value,
        };
        const vk::RenderingInfo clear_rendering_info{{}, {{0, 0}, this->state->swapchain.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(clear_rendering_info);
        command_buffer.endRendering();

        ImGui::Render();
        const vk::RenderingAttachmentInfo imgui_color_attachment{
            *this->state->swapchain.image_views[frame.image_index],
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo imgui_rendering_info{{}, {{0, 0}, this->state->swapchain.extent}, 1, 0, 1, &imgui_color_attachment, nullptr, nullptr};
        command_buffer.beginRendering(imgui_rendering_info);
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), *command_buffer);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, this->state->swapchain.images[frame.image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eAllCommands, {});
        this->state->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::ePresentSrcKHR;
        command_buffer.end();
    }

    void Spectra::end_frame(FrameState& frame) {
        if (this->state->imgui.viewports) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        std::array<vk::SemaphoreSubmitInfo, 2> wait_semaphore_infos{
            vk::SemaphoreSubmitInfo{*this->state->sync.image_available_semaphores[frame.frame_index], 0, vk::PipelineStageFlagBits2::eAllCommands},
            vk::SemaphoreSubmitInfo{},
        };
        std::uint32_t wait_semaphore_count = 1;
        if (frame.wait_for_external_completion) {
            wait_semaphore_infos[1] = vk::SemaphoreSubmitInfo{frame.external_completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer};
            wait_semaphore_count    = 2;
        }
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->state->sync.command_buffers[frame.frame_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->state->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, wait_semaphore_count, wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->state->context.graphics_queue.submit2(submit_info, *this->state->sync.in_flight_fences[frame.frame_index]);

        const vk::Semaphore render_finished_semaphore = *this->state->sync.render_finished_semaphores[frame.image_index];
        const vk::SwapchainKHR swapchain              = *this->state->swapchain.handle;
        const vk::PresentInfoKHR present_info{1, &render_finished_semaphore, 1, &swapchain, &frame.image_index};
        bool frame_presented = true;
        try {
            if (const vk::Result present_result = this->state->context.graphics_queue.presentKHR(present_info); present_result == vk::Result::eSuboptimalKHR)
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

        this->state->sync.frame_index = (this->state->sync.frame_index + 1) % this->state->sync.frame_count;
    }
    void Spectra::create_swapchain(vk::raii::SwapchainKHR old_swapchain) {
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities   = this->state->context.physical_device.getSurfaceCapabilitiesKHR(this->state->surface.surface);
            const std::vector<vk::SurfaceFormatKHR> surface_formats = this->state->context.physical_device.getSurfaceFormatsKHR(this->state->surface.surface);
            const std::vector<vk::PresentModeKHR> present_modes     = this->state->context.physical_device.getSurfacePresentModesKHR(this->state->surface.surface);
            if (surface_formats.empty()) throw std::runtime_error("Surface has no formats");
            if (present_modes.empty()) throw std::runtime_error("Surface has no present modes");

            if (surface_formats.size() == 1 && surface_formats.front().format == vk::Format::eUndefined) {
                this->state->swapchain.format      = vk::Format::eB8G8R8A8Unorm;
                this->state->swapchain.color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
            } else {
                this->state->swapchain.format      = surface_formats.front().format;
                this->state->swapchain.color_space = surface_formats.front().colorSpace;

                bool selected = false;
                constexpr std::array preferred_surface_formats{
                    vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                    vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
                };
                for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_surface_formats) {
                    for (const vk::SurfaceFormatKHR& surface_format : surface_formats) {
                        if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) {
                            this->state->swapchain.format      = surface_format.format;
                            this->state->swapchain.color_space = surface_format.colorSpace;
                            selected                    = true;
                            break;
                        }
                    }
                    if (selected) break;
                }
            }

            this->state->swapchain.present_mode = vk::PresentModeKHR::eFifo;
            for (const vk::PresentModeKHR present_mode : present_modes) {
                if (present_mode == vk::PresentModeKHR::eMailbox) {
                    this->state->swapchain.present_mode = present_mode;
                    break;
                }
                if (present_mode == vk::PresentModeKHR::eImmediate) this->state->swapchain.present_mode = present_mode;
            }

            this->state->swapchain.extent = surface_capabilities.currentExtent.width == std::numeric_limits<std::uint32_t>::max() ? vk::Extent2D{std::clamp(this->state->surface.extent.width, surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width), std::clamp(this->state->surface.extent.height, surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height)} : surface_capabilities.currentExtent;
            if (this->state->swapchain.extent.width == 0 || this->state->swapchain.extent.height == 0) throw std::runtime_error("Cannot create swapchain with zero extent");

            this->state->swapchain.image_count = surface_capabilities.minImageCount + 1;
            if (this->state->swapchain.image_count < 2) this->state->swapchain.image_count = 2;
            if (surface_capabilities.maxImageCount != 0 && this->state->swapchain.image_count > surface_capabilities.maxImageCount) this->state->swapchain.image_count = surface_capabilities.maxImageCount;

            if ((surface_capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eColorAttachment) != vk::ImageUsageFlagBits::eColorAttachment) throw std::runtime_error("Swapchain must support color attachment usage");
            this->state->swapchain.usage = vk::ImageUsageFlagBits::eColorAttachment;
        }
        {
            const vk::SurfaceCapabilitiesKHR surface_capabilities = this->state->context.physical_device.getSurfaceCapabilitiesKHR(this->state->surface.surface);
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
            const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->state->surface.surface, this->state->swapchain.image_count, this->state->swapchain.format, this->state->swapchain.color_space, this->state->swapchain.extent, 1, this->state->swapchain.usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->state->swapchain.present_mode, VK_TRUE, *old_swapchain};
            this->state->swapchain.handle = vk::raii::SwapchainKHR{this->state->context.device, swapchain_create_info};
        }
        {
            this->state->swapchain.images = this->state->swapchain.handle.getImages();
            if (this->state->swapchain.images.empty()) throw std::runtime_error("Swapchain has no images");
            this->state->swapchain.image_layouts.assign(this->state->swapchain.images.size(), vk::ImageLayout::eUndefined);
        }
        {
            this->state->swapchain.image_views.clear();
            this->state->swapchain.image_views.reserve(this->state->swapchain.images.size());
            for (const vk::Image image : this->state->swapchain.images) {
                const vk::ImageViewCreateInfo image_view_create_info{{}, image, vk::ImageViewType::e2D, this->state->swapchain.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
                this->state->swapchain.image_views.emplace_back(this->state->context.device, image_view_create_info);
            }
            if (this->state->swapchain.image_views.size() != this->state->swapchain.images.size()) throw std::runtime_error("Failed to create all swapchain image views");
        }
        {
            constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
            this->state->sync.render_finished_semaphores.clear();
            this->state->sync.render_finished_semaphores.reserve(this->state->swapchain.images.size());
            for (std::uint32_t image_index = 0; image_index < this->state->swapchain.images.size(); ++image_index) this->state->sync.render_finished_semaphores.emplace_back(this->state->context.device, semaphore_create_info);
            this->state->sync.image_in_flight_frame.assign(this->state->swapchain.images.size(), std::numeric_limits<std::uint32_t>::max());
        }
    }

    void Spectra::recreate_swapchain() {
        {
            int width  = 0;
            int height = 0;
            while (width == 0 || height == 0) {
                glfwGetFramebufferSize(this->state->surface.window.get(), &width, &height);
                if (width == 0 || height == 0) glfwWaitEvents();
            }
            this->state->surface.extent = vk::Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
        }

        this->state->context.device.waitIdle();
        vk::raii::SwapchainKHR old_swapchain = std::move(this->state->swapchain.handle);
        this->state->swapchain.image_views.clear();
        this->state->sync.render_finished_semaphores.clear();
        this->state->sync.image_in_flight_frame.clear();
        this->state->swapchain.image_layouts.clear();
        this->state->swapchain.images.clear();
        this->create_swapchain(std::move(old_swapchain));

        const std::uint32_t image_count = static_cast<std::uint32_t>(this->state->swapchain.images.size());
        if (!this->state->imgui.initialized) throw std::runtime_error("ImGui is not initialized during swapchain recreation");
        if (this->state->imgui.color_format != this->state->swapchain.format || this->state->imgui.image_count != image_count) {
            const bool docking   = this->state->imgui.docking;
            const bool viewports = this->state->imgui.viewports;
            this->destroy_imgui();
            this->state->imgui.docking   = docking;
            this->state->imgui.viewports = viewports;
            this->create_imgui();
            if (this->state->gpu_pathtracer != nullptr) this->state->gpu_pathtracer->create_viewport_descriptors();
        }
        this->state->surface.resize_requested = false;
    }
} // namespace xayah

namespace {
    void draw_statistics_row(const char* label, const char* value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
    }

    void draw_statistics_row(const char* label, const std::string& value) {
        draw_statistics_row(label, value.c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location);

    [[nodiscard]] std::string optional_scene_text(const std::string& value) {
        if (value.empty()) return "None";
        return value;
    }

    [[nodiscard]] std::string spectra_parameter_count_text(const std::vector<spectra::scene::SceneDescriptionParameter>& parameters) {
        if (parameters.empty()) return "None";
        if (parameters.size() == 1u) return "1 parameter";
        return std::format("{} parameters", parameters.size());
    }

    [[nodiscard]] std::string scene_render_setting_text(const spectra::scene::SceneDescriptionRenderSetting& setting) {
        if (!setting.present) return "Not specified";
        if (!setting.type.empty() && !setting.name.empty()) return std::format("{} {}", setting.type, setting.name);
        if (!setting.type.empty()) return setting.type;
        if (!setting.name.empty()) return setting.name;
        return "Present";
    }

    [[nodiscard]] std::string resolution_text(const std::array<int, 2>& resolution) {
        if (resolution[0] <= 0 || resolution[1] <= 0) return "Pending";
        return std::format("{} x {}", resolution[0], resolution[1]);
    }

    [[nodiscard]] std::string positive_int_text(const int value) {
        if (value <= 0) return "Pending";
        return std::format("{}", value);
    }

    [[nodiscard]] const char* scene_texture_value_type_label(const spectra::scene::SceneDescriptionTextureValueType value_type) {
        switch (value_type) {
            case spectra::scene::SceneDescriptionTextureValueType::Unknown: return "Unknown";
            case spectra::scene::SceneDescriptionTextureValueType::Float: return "Float";
            case spectra::scene::SceneDescriptionTextureValueType::Spectrum: return "Spectrum";
        }
        throw std::runtime_error("Unknown Spectra scene texture value type");
    }

    void draw_scene_render_setting_row(const char* label, const spectra::scene::SceneDescriptionRenderSetting& setting) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const std::string setting_text = scene_render_setting_text(setting);
        if (setting.present) ImGui::TextWrapped("%s", setting_text.c_str());
        else ImGui::TextDisabled("%s", setting_text.c_str());
        ImGui::TableSetColumnIndex(2);
        if (setting.present) ImGui::TextWrapped("%s", scene_file_location_text(setting.location).c_str());
        else ImGui::TextDisabled("None");
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(spectra_parameter_count_text(setting.parameters).c_str());
    }

    [[nodiscard]] std::string scene_file_location_text(const spectra::scene::SceneDescriptionFileLocation& location) {
        if (location.filename.empty()) return "<unknown>";
        return std::format("{}:{}:{}", location.filename, location.line, location.column);
    }

} // namespace

namespace xayah {
    void Spectra::draw_main_menu() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F1, false)) this->state->ui.camera_visible = !this->state->ui.camera_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) this->state->ui.scene_browser_visible = !this->state->ui.scene_browser_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F3, false)) this->state->ui.settings_visible = !this->state->ui.settings_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F4, false)) this->state->ui.inspector_visible = !this->state->ui.inspector_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) this->state->ui.environment_visible = !this->state->ui.environment_visible;
            if (ImGui::IsKeyPressed(ImGuiKey_F6, false)) this->state->ui.tonemapper_visible = !this->state->ui.tonemapper_visible;
        }

        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem(ICON_MS_CLOSE " Exit", "Esc")) glfwSetWindowShouldClose(this->state->surface.window.get(), GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem(ICON_MS_PHOTO_CAMERA " Camera", "F1", &this->state->ui.camera_visible);
            ImGui::MenuItem(ICON_MS_ACCOUNT_TREE " Scene Browser", "F2", &this->state->ui.scene_browser_visible);
            ImGui::MenuItem(ICON_MS_SETTINGS " Settings", "F3", &this->state->ui.settings_visible);
            ImGui::MenuItem(ICON_MS_LIST_ALT " Inspector", "F4", &this->state->ui.inspector_visible);
            ImGui::MenuItem(ICON_MS_PUBLIC " Environment", "F5", &this->state->ui.environment_visible);
            ImGui::MenuItem(ICON_MS_TONALITY " Tonemapper", "F6", &this->state->ui.tonemapper_visible);
            ImGui::Separator();
            ImGui::MenuItem(ICON_MS_ANALYTICS " Statistics", nullptr, &this->state->ui.statistics_visible);
            ImGui::EndMenu();
        }
        this->draw_menu_toolbar();
        ImGui::EndMainMenuBar();
    }


    void Spectra::draw_menu_toolbar() {
        struct ToggleButton {
            const char* icon;
            const char* shortcut;
            bool* visible;
            const char* tooltip;
        };

        const std::array<ToggleButton, 6> toggles{{
            {ICON_MS_PHOTO_CAMERA, "F1", &this->state->ui.camera_visible, "Camera"},
            {ICON_MS_ACCOUNT_TREE, "F2", &this->state->ui.scene_browser_visible, "Scene Browser"},
            {ICON_MS_SETTINGS, "F3", &this->state->ui.settings_visible, "Settings"},
            {ICON_MS_LIST_ALT, "F4", &this->state->ui.inspector_visible, "Inspector"},
            {ICON_MS_PUBLIC, "F5", &this->state->ui.environment_visible, "Environment"},
            {ICON_MS_TONALITY, "F6", &this->state->ui.tonemapper_visible, "Tonemapper"},
        }};

        const float button_size  = ImGui::GetFrameHeight();
        const float total_width  = 2.0f + static_cast<float>(toggles.size()) * button_size + static_cast<float>(toggles.size() + 1) * 2.0f;
        const float window_width = ImGui::GetWindowWidth();
        if (window_width <= total_width + 180.0f) return;

        ImGui::SameLine(window_width * 0.5f - total_width * 0.5f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        for (const ToggleButton& toggle : toggles) {
            ImGui::PushStyleColor(ImGuiCol_Button, *toggle.visible ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_ChildBg]);
            if (ImGui::Button(toggle.icon, ImVec2{button_size, button_size})) *toggle.visible = !*toggle.visible;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && toggle.shortcut != nullptr) ImGui::SetTooltip("Toggle %s Window (%s)", toggle.tooltip, toggle.shortcut);
            if (ImGui::IsItemHovered() && toggle.shortcut == nullptr) ImGui::SetTooltip("Toggle %s Window", toggle.tooltip);
            ImGui::SameLine(0.0f, 2.0f);
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
        if (this->state->ui.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, main_viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, main_viewport->WorkSize);

        ImGuiID center_id = dockspace_id;
        ImGuiID left_id   = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Left, 0.25f, nullptr, &center_id);
        ImGuiID right_id  = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.25f, nullptr, &center_id);
        ImGuiID bottom_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Down, 0.35f, nullptr, &center_id);
        if (left_id == 0 || right_id == 0 || bottom_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra dock layout");

        ImGuiID left_bottom_id = ImGui::DockBuilderSplitNode(left_id, ImGuiDir_Down, 0.35f, nullptr, &left_id);
        ImGuiID inspector_id   = ImGui::DockBuilderSplitNode(right_id, ImGuiDir_Down, 0.35f, nullptr, &right_id);
        if (left_bottom_id == 0 || inspector_id == 0 || left_id == 0 || right_id == 0) throw std::runtime_error("Failed to build Spectra side panels");

        ImGui::DockBuilderDockWindow("Viewport", center_id);
        ImGui::DockBuilderDockWindow("Camera", left_id);
        ImGui::DockBuilderDockWindow("Settings", left_id);
        ImGui::DockBuilderDockWindow("Tonemapper", left_bottom_id);
        ImGui::DockBuilderDockWindow("Environment", left_bottom_id);
        ImGui::DockBuilderDockWindow("Scene Browser", right_id);
        ImGui::DockBuilderDockWindow("Inspector", inspector_id);
        ImGui::DockBuilderDockWindow("Statistics", bottom_id);
        ImGuiDockNode* central_node = ImGui::DockBuilderGetCentralNode(dockspace_id);
        if (central_node == nullptr) throw std::runtime_error("Failed to find Spectra central dock node");
        central_node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->state->ui.dock_layout_initialized = true;
    }


    void Spectra::draw_viewport_window() {
        constexpr ImGuiWindowFlags viewport_window_flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        if (ImGui::Begin("Viewport", nullptr, viewport_window_flags)) {
            const ImVec2 viewport_position = ImGui::GetCursorScreenPos();
            const ImVec2 viewport_size     = ImGui::GetContentRegionAvail();
            if (viewport_size.x <= 0.0f || viewport_size.y <= 0.0f) throw std::runtime_error("Viewport dock window has no drawable area");
            const ImGuiIO& io = ImGui::GetIO();
            if (!std::isfinite(io.DisplayFramebufferScale.x) || !std::isfinite(io.DisplayFramebufferScale.y) || !(io.DisplayFramebufferScale.x > 0.0f) || !(io.DisplayFramebufferScale.y > 0.0f)) throw std::runtime_error("ImGui framebuffer scale must be finite and positive");
            const std::array<int, 2> viewport_framebuffer_size{
                static_cast<int>(std::round(viewport_size.x * io.DisplayFramebufferScale.x)),
                static_cast<int>(std::round(viewport_size.y * io.DisplayFramebufferScale.y)),
            };
            if (viewport_framebuffer_size[0] <= 0 || viewport_framebuffer_size[1] <= 0) throw std::runtime_error("Viewport framebuffer resolution must be positive");
            this->state->ui.viewport_known    = true;
            this->state->ui.viewport_position = {viewport_position.x, viewport_position.y};
            this->state->ui.viewport_size     = {viewport_size.x, viewport_size.y};
            this->state->ui.viewport_framebuffer_size = viewport_framebuffer_size;
            this->state->ui.viewport_hovered  = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootWindow);
            this->state->ui.viewport_focused  = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootWindow);
            this->observe_viewport_render_resolution(viewport_framebuffer_size);
            if (this->pathtracer_ready()) {
                const VkDescriptorSet descriptor = this->pathtracer_viewport_descriptor();
                if (descriptor == VK_NULL_HANDLE) throw std::runtime_error("Spectra GPU pathtracer viewport descriptor is null");
                const ImTextureID texture_id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(descriptor));
                ImGui::Image(ImTextureRef{texture_id}, viewport_size, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
                ImGui::SetCursorScreenPos(viewport_position);
            } else if (this->state->spectra_scene != nullptr) {
                const char* pending_label = this->state->render_resolution_sync.rebuilding ? "Rebuilding pathtracer" : "Waiting for viewport resolution";
                const ImVec2 text_size = ImGui::CalcTextSize(pending_label);
                ImGui::SetCursorScreenPos(ImVec2{viewport_position.x + std::max(0.0f, (viewport_size.x - text_size.x) * 0.5f), viewport_position.y + std::max(0.0f, (viewport_size.y - text_size.y) * 0.5f)});
                ImGui::TextDisabled("%s", pending_label);
                ImGui::SetCursorScreenPos(viewport_position);
            }
            ImGui::InvisibleButton("ViewportInputSurface", viewport_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        } else {
            this->state->ui.viewport_known   = false;
            this->state->ui.viewport_hovered = false;
            this->state->ui.viewport_focused = false;
            this->state->ui.viewport_framebuffer_size = {0, 0};
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }


    void Spectra::draw_camera_window() {
        if (!this->state->ui.camera_visible) return;
        if (!ImGui::Begin("Camera", &this->state->ui.camera_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        if (ImGui::BeginTable("SpectraCameraControls", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            const PathtracerStatus pathtracer_status = this->pathtracer_status();

            draw_statistics_row("Path Tracer", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Camera Speed");
            ImGui::TableSetColumnIndex(1);
            float speed = this->state->camera.speed;
            const float drag_speed = std::max(std::abs(speed) * 0.01f, 0.000001f);
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##CameraSpeed", &speed, drag_speed, 0.0f, 0.0f, "%.6g")) this->set_camera_speed(speed);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Camera movement speed in world units per second. Changing this does not reset accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Actions");
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(!this->state->camera.initialized || !this->pathtracer_ready());
            if (ImGui::Button(ICON_MS_RESTART_ALT)) this->reset_camera();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset Camera");
            ImGui::EndDisabled();

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_scene_browser_window() {
        if (!this->state->ui.scene_browser_visible) return;
        if (!ImGui::Begin("Scene Browser", &this->state->ui.scene_browser_visible)) {
            ImGui::End();
            return;
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Asset Info");
        if (ImGui::BeginTable("SpectraSceneSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", spectra_scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (!ImGui::BeginTabBar("SpectraSceneBrowserTabs")) {
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags render_settings_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTabItem("Render Settings")) {
            if (ImGui::BeginTable("SpectraSceneRenderSettings", 4, render_settings_table_flags)) {
                ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableHeadersRow();
                draw_scene_render_setting_row("Pixel Filter", this->state->spectra_scene->description.pixelFilter);
                draw_scene_render_setting_row("Film", this->state->spectra_scene->description.film);
                draw_scene_render_setting_row("Sampler", this->state->spectra_scene->description.sampler);
                draw_scene_render_setting_row("Accelerator", this->state->spectra_scene->description.accelerator);
                draw_scene_render_setting_row("Integrator", this->state->spectra_scene->description.integrator);
                draw_scene_render_setting_row("Camera", this->state->spectra_scene->description.camera);
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Shapes")) {
            if (this->state->spectra_scene->description.shapes.empty()) {
                ImGui::TextDisabled("No Spectra GPU shapes recorded");
            } else if (ImGui::BeginTable("SpectraSceneShapes", 7, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Media", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Object", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Area Light", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionShape& shape : this->state->spectra_scene->description.shapes) {
                    const std::string material_text = !shape.materialName.empty() ? shape.materialName : shape.materialIndex >= 0 ? std::format("#{}", shape.materialIndex) : "None";
                    const std::string media_text    = shape.insideMedium.empty() && shape.outsideMedium.empty() ? "None" : std::format("{} / {}", optional_scene_text(shape.insideMedium), optional_scene_text(shape.outsideMedium));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", shape.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", material_text.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", media_text.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.objectDefinitionName).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextWrapped("%s", optional_scene_text(shape.areaLightType).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextWrapped("%s", scene_file_location_text(shape.location).c_str());
                    ImGui::TableSetColumnIndex(6);
                    ImGui::TextUnformatted(spectra_parameter_count_text(shape.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Materials")) {
            if (this->state->spectra_scene->description.materials.empty()) {
                ImGui::TextDisabled("No Spectra GPU materials recorded");
            } else if (ImGui::BeginTable("SpectraSceneMaterials", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMaterial& material : this->state->spectra_scene->description.materials) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(material.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(material.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(material.named ? "Named" : "Inline");
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(material.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(material.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Textures")) {
            if (this->state->spectra_scene->description.textures.empty()) {
                ImGui::TextDisabled("No Spectra GPU textures recorded");
            } else if (ImGui::BeginTable("SpectraSceneTextures", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Value Type", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Implementation", ImGuiTableColumnFlags_WidthFixed, 130.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionTexture& texture : this->state->spectra_scene->description.textures) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(scene_texture_value_type_label(texture.valueType));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(texture.implementation).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(texture.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(texture.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Media")) {
            if (this->state->spectra_scene->description.mediums.empty()) {
                ImGui::TextDisabled("No Spectra GPU media recorded");
            } else if (ImGui::BeginTable("SpectraSceneMedia", 4, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Medium Interfaces");
            if (this->state->spectra_scene->description.mediumBindings.empty()) {
                ImGui::TextDisabled("No Spectra GPU medium interfaces recorded");
            } else if (ImGui::BeginTable("SpectraSceneMediumInterfaces", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lights")) {
            if (this->state->spectra_scene->description.lights.empty()) {
                ImGui::TextDisabled("No Spectra GPU lights recorded");
            } else if (ImGui::BeginTable("SpectraSceneLights", 5, detail_table_flags)) {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 110.0f);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", light.type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(light.area ? "Area" : "Light");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Objects")) {
            ImGui::SeparatorText("Definitions");
            if (this->state->spectra_scene->description.objectDefinitions.empty()) {
                ImGui::TextDisabled("No Spectra GPU object definitions recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectDefinitions", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Shapes", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectDefinition& object_definition : this->state->spectra_scene->description.objectDefinitions) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_definition.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%zu", object_definition.shapeIndices.size());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_definition.location).c_str());
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Instances");
            if (this->state->spectra_scene->description.objectInstances.empty()) {
                ImGui::TextDisabled("No Spectra GPU object instances recorded");
            } else if (ImGui::BeginTable("SpectraSceneObjectInstances", 3, detail_table_flags)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Animated", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                for (const spectra::scene::SceneDescriptionObjectInstance& object_instance : this->state->spectra_scene->description.objectInstances) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextWrapped("%s", object_instance.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(object_instance.animatedTransform ? "Yes" : "No");
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", scene_file_location_text(object_instance.location).c_str());
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }


        ImGui::EndTabBar();
        ImGui::End();
    }


    void Spectra::draw_inspector_window() {
        if (!this->state->ui.inspector_visible) return;
        if (!ImGui::Begin("Inspector", &this->state->ui.inspector_visible)) {
            ImGui::End();
            return;
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra scene");
            ImGui::End();
            return;
        }

        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        const std::string viewport_resolution       = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraInspectorPathTracerState", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Scene");
        if (ImGui::BeginTable("SpectraInspectorScene", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Scene", spectra_scene_title_text(*this->state->spectra_scene));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Path");
            ImGui::TableSetColumnIndex(1);
            const std::string scene_path = this->state->spectra_scene->scene_path.string();
            ImGui::TextWrapped("%s", scene_path.c_str());
            draw_statistics_row("Film Resolution", resolution_text(this->state->spectra_scene->film_resolution));
            draw_statistics_row("Sampler SPP", positive_int_text(this->state->spectra_scene->sampler_sample_count));
            draw_statistics_row("Viewport", viewport_resolution);
            draw_statistics_row("Swapchain", std::format("{} x {}", this->state->swapchain.extent.width, this->state->swapchain.extent.height));
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Resources");
        if (ImGui::BeginTable("SpectraInspectorResources", 2, table_flags)) {
            ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Shapes", std::format("{}", this->state->spectra_scene->description.shapes.size()));
            draw_statistics_row("Materials", std::format("{}", this->state->spectra_scene->description.materials.size()));
            draw_statistics_row("Textures", std::format("{}", this->state->spectra_scene->description.textures.size()));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Object Definitions", std::format("{}", this->state->spectra_scene->description.objectDefinitions.size()));
            draw_statistics_row("Object Instances", std::format("{}", this->state->spectra_scene->description.objectInstances.size()));
            ImGui::EndTable();
        }

        if (this->state->gpu_pathtracer != nullptr) {
            ImGui::SeparatorText("Path Tracer");
            if (ImGui::BeginTable("SpectraInspectorPathTracer", 2, table_flags)) {
                ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                draw_statistics_row("Sample", std::format("{} / {}", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count()));
                draw_statistics_row("Completion", std::format("{:.1f}%", this->state->gpu_pathtracer->completion_ratio() * 100.0f));
                draw_statistics_row("Exposure", std::format("{:.3f}", this->state->gpu_pathtracer->current_exposure()));
                ImGui::EndTable();
            }
        }

        ImGui::End();
    }


    void Spectra::draw_settings_window() {
        if (!this->state->ui.settings_visible) return;
        if (!ImGui::Begin("Settings", &this->state->ui.settings_visible)) {
            ImGui::End();
            return;
        }

        const PathtracerStatus pathtracer_status = this->pathtracer_status();
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTable("SpectraPathTracerSettings", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Spectra GPU Sampler SPP");
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(positive_int_text(this->state->spectra_scene->sampler_sample_count).c_str());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Current Sample");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d / %d", this->state->gpu_pathtracer->current_sample(), this->state->gpu_pathtracer->target_sample_count());

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Max Iterations");
            ImGui::TableSetColumnIndex(1);
            const int previous_target_sample_count = this->state->gpu_pathtracer->target_sample_count();
            int target_sample_count                = previous_target_sample_count;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##MaxIterations", &target_sample_count, 1, this->state->spectra_scene->sampler_sample_count)) {
                this->state->gpu_pathtracer->set_target_sample_count(target_sample_count);
                if (target_sample_count != previous_target_sample_count) this->clear_pathtracer_throughput_statistics();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interactive stop sample count. Changing it resets accumulation.");

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Accumulation");
            ImGui::TableSetColumnIndex(1);
            if (ImGui::Button("Reset Accumulation")) this->request_pathtracer_accumulation_reset();

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_environment_window() {
        if (!this->state->ui.environment_visible) return;
        if (!ImGui::Begin("Environment", &this->state->ui.environment_visible)) {
            ImGui::End();
            return;
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU scene");
            ImGui::End();
            return;
        }

        std::size_t area_light_count = 0;
        std::size_t infinite_light_count = 0;
        for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
            if (light.area) ++area_light_count;
            if (light.type == "infinite") ++infinite_light_count;
        }

        constexpr ImGuiTableFlags summary_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        ImGui::SeparatorText("Summary");
        if (ImGui::BeginTable("SpectraEnvironmentSummary", 2, summary_table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 140.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Lights", std::format("{}", this->state->spectra_scene->description.lights.size()));
            draw_statistics_row("Area Lights", std::format("{}", area_light_count));
            draw_statistics_row("Infinite Lights", std::format("{}", infinite_light_count));
            draw_statistics_row("Media", std::format("{}", this->state->spectra_scene->description.mediums.size()));
            draw_statistics_row("Medium Bindings", std::format("{}", this->state->spectra_scene->description.mediumBindings.size()));
            ImGui::EndTable();
        }

        constexpr ImGuiTableFlags detail_table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable;
        ImGui::SeparatorText("Lights");
        if (this->state->spectra_scene->description.lights.empty()) {
            ImGui::TextDisabled("No Spectra GPU lights recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentLights", 5, detail_table_flags)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableSetupColumn("Outside Medium", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionLight& light : this->state->spectra_scene->description.lights) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", light.type.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(light.area ? "Area" : "Light");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", optional_scene_text(light.outsideMedium).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextWrapped("%s", scene_file_location_text(light.location).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(spectra_parameter_count_text(light.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Media");
        if (this->state->spectra_scene->description.mediums.empty()) {
            ImGui::TextDisabled("No Spectra GPU media recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMedia", 4, detail_table_flags)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Parameters", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMedium& medium : this->state->spectra_scene->description.mediums) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(medium.name).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(medium.type).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(medium.location).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(spectra_parameter_count_text(medium.parameters).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Medium Interfaces");
        if (this->state->spectra_scene->description.mediumBindings.empty()) {
            ImGui::TextDisabled("No Spectra GPU medium interfaces recorded");
        } else if (ImGui::BeginTable("SpectraEnvironmentMediumInterfaces", 3, detail_table_flags)) {
            ImGui::TableSetupColumn("Inside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Outside", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const spectra::scene::SceneDescriptionMediumBinding& binding : this->state->spectra_scene->description.mediumBindings) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextWrapped("%s", optional_scene_text(binding.inside).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextWrapped("%s", optional_scene_text(binding.outside).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextWrapped("%s", scene_file_location_text(binding.location).c_str());
            }
            ImGui::EndTable();
        }

        ImGui::End();
    }


    void Spectra::draw_tonemapper_window() {
        if (!this->state->ui.tonemapper_visible) return;
        if (!ImGui::Begin("Tonemapper", &this->state->ui.tonemapper_visible)) {
            ImGui::End();
            return;
        }
        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg;
        if (ImGui::BeginTable("SpectraTonemapperSettings", 2, table_flags)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Exposure");
            ImGui::TableSetColumnIndex(1);
            float exposure = this->state->gpu_pathtracer->current_exposure();
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::DragFloat("##TonemapperExposure", &exposure, 0.01f, 0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic)) this->state->gpu_pathtracer->set_exposure(exposure);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Viewport exposure multiplier. This does not reset accumulation.");

            ImGui::EndTable();
        }
        ImGui::End();
    }


    void Spectra::draw_statistics_window() {
        if (!this->state->ui.statistics_visible) return;
        if (!ImGui::Begin("Statistics", &this->state->ui.statistics_visible)) {
            ImGui::End();
            return;
        }
        constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV;
        const std::string viewport_resolution    = this->state->ui.viewport_known ? resolution_text(this->state->ui.viewport_framebuffer_size) : "Unknown";
        const PathtracerStatus pathtracer_status = this->pathtracer_status();

        ImGui::SeparatorText("Runtime");
        if (ImGui::BeginTable("SpectraRuntimeStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Path Tracer State", pathtracer_status.state);
            draw_statistics_row("External Completion", pathtracer_status.uses_external_completion ? "Yes" : "No");
            draw_statistics_row("Scene", this->state->spectra_scene == nullptr ? "No Scene" : spectra_scene_title_text(*this->state->spectra_scene));
            draw_statistics_row("Frame ID", std::format("{}", this->state->statistics.current_frame_id));
            draw_statistics_row("Frame Slot", std::format("{}", this->state->statistics.active_frame_index));
            draw_statistics_row("Swapchain Image", std::format("{}", this->state->statistics.active_swapchain_image_index));
            draw_statistics_row("Frames In Flight", std::format("{}", this->state->sync.frame_count));
            draw_statistics_row("Swapchain Resolution", std::format("{} x {}", this->state->swapchain.extent.width, this->state->swapchain.extent.height));
            draw_statistics_row("Viewport Resolution", viewport_resolution);
            ImGui::EndTable();
        }

        ImGui::SeparatorText("Performance");
        if (ImGui::BeginTable("SpectraPerformanceStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("Frame Time", std::format("{:.3f} ms", this->state->statistics.last_frame_milliseconds));
            if (this->state->statistics.frame_milliseconds.has_value()) {
                const float average_frame_milliseconds = this->state->statistics.frame_milliseconds.average();
                if (!(average_frame_milliseconds > 0.0f)) throw std::runtime_error("Average frame time must be positive after statistics are collected");
                draw_statistics_row("Frame Time Avg", std::format("{:.3f} ms over {} frames", average_frame_milliseconds, this->state->statistics.frame_milliseconds.count));
                draw_statistics_row("FPS Avg", std::format("{:.1f}", 1000.0f / average_frame_milliseconds));
            } else {
                draw_statistics_row("Frame Time Avg", "Collecting");
                draw_statistics_row("FPS Avg", "Collecting");
            }
            ImGui::EndTable();
        }

        if (this->state->spectra_scene == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU scene");
            ImGui::End();
            return;
        }

        if (this->state->gpu_pathtracer == nullptr) {
            ImGui::TextDisabled("No active Spectra GPU interactive session");
            ImGui::End();
            return;
        }

        const std::array<int, 2> film_resolution = this->state->spectra_scene->film_resolution;
        const int current_sample                 = this->state->gpu_pathtracer->current_sample();
        const int target_sample                  = this->state->gpu_pathtracer->target_sample_count();
        const float completion_ratio             = this->state->gpu_pathtracer->completion_ratio();
        const float completion_percent           = completion_ratio * 100.0f;
        const bool sampling_completed            = current_sample >= target_sample;
        const std::string sampling_state         = sampling_completed ? "Completed" : "Sampling";

        ImGui::SeparatorText("Path Tracer");
        if (ImGui::BeginTable("SpectraPathTracerStatistics", 2, table_flags)) {
            ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 170.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            draw_statistics_row("State", sampling_state);
            draw_statistics_row("Sample", std::format("{} / {}", current_sample, target_sample));
            draw_statistics_row("Completion", std::format("{:.1f}%", completion_percent));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted("Progress");
            ImGui::TableSetColumnIndex(1);
            const std::string progress_label = std::format("{:.1f}%", completion_percent);
            ImGui::ProgressBar(completion_ratio, ImVec2{-1.0f, 0.0f}, progress_label.c_str());

            draw_statistics_row("Film Resolution", resolution_text(film_resolution));
            if (this->state->statistics.throughput_mspp.has_value())
                draw_statistics_row("Throughput Avg", std::format("{:.2f} MSPP/s over {} sample frames", this->state->statistics.throughput_mspp.average(), this->state->statistics.throughput_mspp.count));
            else
                draw_statistics_row("Throughput Avg", sampling_completed ? "Completed" : "Collecting");
            draw_statistics_row("Last Sample Throughput", this->state->statistics.has_throughput ? std::format("{:.2f} MSPP/s", this->state->statistics.last_valid_throughput_mspp) : "No sample yet");
            draw_statistics_row("Current Frame Work", this->state->statistics.last_frame_rendered_sample ? "Rendered sample" : "No Spectra GPU sample");
            ImGui::EndTable();
        }

        ImGui::End();
    }
} // namespace xayah
