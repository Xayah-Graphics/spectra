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
        ImGuiStyle& style          = ImGui::GetStyle();
        style.Alpha                = 1.0f;
        style.DisabledAlpha        = 0.45f;
        style.WindowRounding       = 7.0f;
        style.WindowBorderSize     = 1.0f;
        style.ChildRounding        = 6.0f;
        style.ChildBorderSize      = 1.0f;
        style.PopupRounding        = 7.0f;
        style.PopupBorderSize      = 1.0f;
        style.ColorButtonPosition  = ImGuiDir_Right;
        style.FrameRounding        = 5.0f;
        style.FrameBorderSize      = 1.0f;
        style.GrabRounding         = 5.0f;
        style.ScrollbarRounding    = 8.0f;
        style.TabRounding          = 6.0f;
        style.TabBorderSize        = 0.0f;
        style.IndentSpacing        = 14.0f;
        style.ItemSpacing          = ImVec2{8.0f, 7.0f};
        style.ItemInnerSpacing     = ImVec2{7.0f, 5.0f};
        style.FramePadding         = ImVec2{9.0f, 5.0f};
        style.CellPadding          = ImVec2{8.0f, 5.0f};
        style.WindowPadding        = ImVec2{11.0f, 10.0f};
        style.SeparatorTextPadding = ImVec2{8.0f, 4.0f};
        style.SelectableTextAlign  = ImVec2{0.0f, 0.5f};

        style.Colors[ImGuiCol_Text]                 = ImVec4{232.0f / 255.0f, 236.0f / 255.0f, 243.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TextDisabled]         = ImVec4{132.0f / 255.0f, 143.0f / 255.0f, 156.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_WindowBg]             = ImVec4{16.0f / 255.0f, 18.0f / 255.0f, 21.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ChildBg]              = ImVec4{20.0f / 255.0f, 23.0f / 255.0f, 27.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_PopupBg]              = ImVec4{24.0f / 255.0f, 28.0f / 255.0f, 33.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Border]               = ImVec4{48.0f / 255.0f, 55.0f / 255.0f, 63.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_BorderShadow]         = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
        style.Colors[ImGuiCol_FrameBg]              = ImVec4{29.0f / 255.0f, 34.0f / 255.0f, 40.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4{38.0f / 255.0f, 47.0f / 255.0f, 55.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_FrameBgActive]        = ImVec4{47.0f / 255.0f, 62.0f / 255.0f, 72.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TitleBg]              = ImVec4{15.0f / 255.0f, 17.0f / 255.0f, 20.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TitleBgActive]        = ImVec4{21.0f / 255.0f, 25.0f / 255.0f, 30.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TitleBgCollapsed]     = ImVec4{15.0f / 255.0f, 17.0f / 255.0f, 20.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_MenuBarBg]            = ImVec4{16.0f / 255.0f, 18.0f / 255.0f, 21.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarBg]          = ImVec4{17.0f / 255.0f, 20.0f / 255.0f, 23.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarGrab]        = ImVec4{49.0f / 255.0f, 57.0f / 255.0f, 66.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4{67.0f / 255.0f, 78.0f / 255.0f, 90.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4{83.0f / 255.0f, 98.0f / 255.0f, 114.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_CheckMark]            = ImVec4{91.0f / 255.0f, 197.0f / 255.0f, 184.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_SliderGrab]           = ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_SliderGrabActive]     = ImVec4{118.0f / 255.0f, 195.0f / 255.0f, 245.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Button]               = ImVec4{31.0f / 255.0f, 37.0f / 255.0f, 43.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ButtonHovered]        = ImVec4{43.0f / 255.0f, 55.0f / 255.0f, 64.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ButtonActive]         = ImVec4{47.0f / 255.0f, 78.0f / 255.0f, 95.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Header]               = ImVec4{31.0f / 255.0f, 38.0f / 255.0f, 45.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_HeaderHovered]        = ImVec4{43.0f / 255.0f, 58.0f / 255.0f, 70.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_HeaderActive]         = ImVec4{48.0f / 255.0f, 79.0f / 255.0f, 96.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Separator]            = ImVec4{47.0f / 255.0f, 54.0f / 255.0f, 62.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4{78.0f / 255.0f, 112.0f / 255.0f, 132.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_SeparatorActive]      = ImVec4{92.0f / 255.0f, 145.0f / 255.0f, 169.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ResizeGrip]           = ImVec4{47.0f / 255.0f, 54.0f / 255.0f, 62.0f / 255.0f, 0.7f};
        style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4{78.0f / 255.0f, 112.0f / 255.0f, 132.0f / 255.0f, 0.8f};
        style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4{92.0f / 255.0f, 145.0f / 255.0f, 169.0f / 255.0f, 0.95f};
        style.Colors[ImGuiCol_Tab]                  = ImVec4{22.0f / 255.0f, 25.0f / 255.0f, 29.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabHovered]           = ImVec4{42.0f / 255.0f, 57.0f / 255.0f, 69.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabActive]            = ImVec4{35.0f / 255.0f, 43.0f / 255.0f, 51.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabUnfocused]         = ImVec4{18.0f / 255.0f, 21.0f / 255.0f, 24.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabUnfocusedActive]   = ImVec4{27.0f / 255.0f, 32.0f / 255.0f, 38.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_DockingPreview]       = ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.42f};
        style.Colors[ImGuiCol_DockingEmptyBg]       = ImVec4{14.0f / 255.0f, 16.0f / 255.0f, 19.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableHeaderBg]        = ImVec4{26.0f / 255.0f, 31.0f / 255.0f, 36.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableBorderStrong]    = ImVec4{50.0f / 255.0f, 58.0f / 255.0f, 67.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableBorderLight]     = ImVec4{39.0f / 255.0f, 46.0f / 255.0f, 53.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableRowBg]           = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
        style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4{24.0f / 255.0f, 28.0f / 255.0f, 33.0f / 255.0f, 0.58f};
        style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4{64.0f / 255.0f, 124.0f / 255.0f, 163.0f / 255.0f, 0.45f};
        style.Colors[ImGuiCol_DragDropTarget]       = ImVec4{91.0f / 255.0f, 197.0f / 255.0f, 184.0f / 255.0f, 0.9f};
        style.Colors[ImGuiCol_NavHighlight]         = ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.7f};
        style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4{5.0f / 255.0f, 7.0f / 255.0f, 10.0f / 255.0f, 0.68f};
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
    }

    [[nodiscard]] float command_bar_height() {
        const ImGuiStyle& style = ImGui::GetStyle();
        return std::max(42.0f, ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f);
    }

    void push_toolbar_button_style(const bool active) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? style.Colors[ImGuiCol_HeaderActive] : style.Colors[ImGuiCol_Button]);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4{59.0f / 255.0f, 96.0f / 255.0f, 116.0f / 255.0f, 1.0f} : style.Colors[ImGuiCol_ButtonHovered]);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_ButtonActive]);
        ImGui::PushStyleColor(ImGuiCol_Border, active ? ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.95f} : style.Colors[ImGuiCol_Border]);
    }

    void pop_toolbar_button_style() {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    void push_renderer_button_style(const bool active) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4{49.0f / 255.0f, 78.0f / 255.0f, 95.0f / 255.0f, 1.0f} : ImVec4{28.0f / 255.0f, 33.0f / 255.0f, 39.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4{60.0f / 255.0f, 97.0f / 255.0f, 116.0f / 255.0f, 1.0f} : ImVec4{39.0f / 255.0f, 49.0f / 255.0f, 57.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_ButtonActive]);
        ImGui::PushStyleColor(ImGuiCol_Border, active ? ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.85f} : ImVec4{49.0f / 255.0f, 57.0f / 255.0f, 65.0f / 255.0f, 1.0f});
    }

    void pop_renderer_button_style() {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    [[nodiscard]] bool owner_scopes_overlap(const std::string& left, const std::string& right) {
        return left.empty() || right.empty() || left == right;
    }

    [[nodiscard]] constexpr std::array<const char*, 5> required_device_extensions() {
#if defined(_WIN32)
        return {vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName, vk::KHRExternalMemoryWin32ExtensionName, vk::KHRExternalSemaphoreWin32ExtensionName};
#else
        return {vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName, vk::KHRExternalMemoryFdExtensionName, vk::KHRExternalSemaphoreFdExtensionName};
#endif
    }

} // namespace

namespace spectra {
    struct Spectra::FrameState {
        std::uint32_t frame_slot_index{0};
        std::uint32_t image_index{0};
        std::uint64_t frame_number{0};
        double delta_seconds{0.0};
        bool recreate_after_present{false};
        std::vector<vk::SemaphoreSubmitInfo> external_waits{};
    };

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
        this->window_title.base = std::string{app_name};
        this->initialize_glfw();
        this->create_vulkan_instance(app_name, engine_name);
        this->create_debug_messenger();
        this->create_window(app_name, window_width, window_height);
        this->create_surface();
        this->validate_initial_framebuffer_extent();
        this->select_physical_device();
        this->create_logical_device();
        this->create_command_pool();
        this->create_swapchain();
        this->create_frame_sync();
        this->create_imgui();
    } catch (...) {
        this->shutdown_runtime();
        throw;
    }

    Spectra::~Spectra() noexcept {
        this->shutdown_runtime();
    }

    void Spectra::run() {
        while (!glfwWindowShouldClose(this->surface.window.get())) {
            FrameState frame{};
            if (!this->begin_frame(frame)) continue;
            this->record_frame(frame);
            this->end_frame(frame);
        }
    }

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

    void Spectra::initialize_glfw() {
        if (!glfwInit()) throw std::runtime_error("Failed to initialize GLFW");
        this->surface.glfw_initialized = true;
    }

    void Spectra::create_vulkan_instance(const std::string_view app_name, const std::string_view engine_name) {
        const std::string app_name_string{app_name};
        const std::string engine_name_string{engine_name};

        constexpr std::array<const char*, 1> enabled_instance_layers{"VK_LAYER_KHRONOS_validation"};
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
    }

    void Spectra::create_debug_messenger() {
        constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
            {},
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
            &debug_callback,
        };
        this->context.debug_messenger = this->context.instance.createDebugUtilsMessengerEXT(debug_messenger_create_info);
    }

    void Spectra::create_window(const std::string_view app_name, const std::uint32_t window_width, const std::uint32_t window_height) {
        if (window_width == 0 || window_height == 0 || window_width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) || window_height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error("Invalid GLFW window resolution");
        const std::string app_name_string{app_name};
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        this->surface.window.reset(glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr));
        if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
        glfwSetWindowSizeLimits(this->surface.window.get(), 800, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(this->surface.window.get(), this);
        glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
    }

    void Spectra::create_surface() {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != VK_SUCCESS) throw std::runtime_error("Failed to create Vulkan surface");
        this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
    }

    void Spectra::validate_initial_framebuffer_extent() {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
        if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
    }

    void Spectra::select_physical_device() {
        constexpr std::array<const char*, 5> enabled_device_extensions = required_device_extensions();
        bool selected                                                  = false;
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

    void Spectra::create_logical_device() {
        const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features>();
        if (!supported_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) throw std::runtime_error("Device does not support shaderDrawParameters");
        if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
        if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}, {}};
        enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = VK_TRUE;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2     = VK_TRUE;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering     = VK_TRUE;

        constexpr std::array<const char*, 5> enabled_device_extensions = required_device_extensions();
        constexpr std::array queue_priorities{1.0f};
        const vk::DeviceQueueCreateInfo queue_create_info{{}, this->context.graphics_queue_index, 1, queue_priorities.data()};
        const vk::DeviceCreateInfo device_create_info{{}, 1, &queue_create_info, 0, nullptr, static_cast<std::uint32_t>(enabled_device_extensions.size()), enabled_device_extensions.data(), nullptr, &enabled_features.get<vk::PhysicalDeviceFeatures2>()};
        this->context.device         = vk::raii::Device{this->context.physical_device, device_create_info};
        this->context.graphics_queue = vk::raii::Queue{this->context.device, this->context.graphics_queue_index, 0};
    }

    void Spectra::create_command_pool() {
        const vk::CommandPoolCreateInfo command_pool_create_info{vk::CommandPoolCreateFlagBits::eResetCommandBuffer, this->context.graphics_queue_index};
        this->context.command_pool = vk::raii::CommandPool{this->context.device, command_pool_create_info};
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

    void Spectra::create_frame_sync() {
        constexpr vk::SemaphoreCreateInfo semaphore_create_info{};
        constexpr vk::FenceCreateInfo fence_create_info{vk::FenceCreateFlagBits::eSignaled};
        const vk::CommandBufferAllocateInfo command_buffer_allocate_info{*this->context.command_pool, vk::CommandBufferLevel::ePrimary, this->sync.frame_count};
        this->sync.command_buffers = vk::raii::CommandBuffers{this->context.device, command_buffer_allocate_info};
        if (this->sync.command_buffers.size() != this->sync.frame_count) throw std::runtime_error("Failed to allocate per-frame command buffers");

        this->sync.image_available_semaphores.reserve(this->sync.frame_count);
        this->sync.in_flight_fences.reserve(this->sync.frame_count);
        for (std::uint32_t frame_slot_index = 0; frame_slot_index < this->sync.frame_count; ++frame_slot_index) {
            this->sync.image_available_semaphores.emplace_back(this->context.device, semaphore_create_info);
            this->sync.in_flight_fences.emplace_back(this->context.device, fence_create_info);
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
            vulkan_backend_initialized                     = true;
            this->imgui.initialized                        = true;
            this->imgui.renderers_notified_before_shutdown = false;
            for (RendererSlot& renderer : this->renderer_registry.slots) renderer.after_imgui_created();
        } catch (...) {
            if (vulkan_backend_initialized) ImGui_ImplVulkan_Shutdown();
            if (glfw_backend_initialized) ImGui_ImplGlfw_Shutdown();
            if (context_created) ImGui::DestroyContext();
            this->imgui.descriptor_pool                    = nullptr;
            this->imgui.initialized                        = false;
            this->imgui.renderers_notified_before_shutdown = false;
            throw;
        }
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
        if (glfwWindowShouldClose(this->surface.window.get())) return false;
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }

        frame.recreate_after_present                    = false;
        frame.frame_slot_index                          = this->sync.frame_slot_index;
        frame.frame_number                              = this->timing.frame_number;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (this->timing.last_frame_time_valid) frame.delta_seconds = std::chrono::duration<double>(now - this->timing.last_frame_time).count();
        this->timing.last_frame_time       = now;
        this->timing.last_frame_time_valid = true;
        if (!std::isfinite(frame.delta_seconds) || frame.delta_seconds < 0.0) throw std::runtime_error("Spectra frame delta time is invalid");
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_slot_index], VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_slot_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_slot_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_slot_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_slot_index), VK_TRUE, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_slot_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_slot_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);

        if (this->renderer_registry.slots.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->renderer_registry.active_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        const FrameContext frame_info{
            .frame_slot_index = frame.frame_slot_index,
            .image_index      = frame.image_index,
            .frame_number     = frame.frame_number,
            .delta_seconds    = frame.delta_seconds,
        };
        FrameResult frame_result = this->renderer_registry.slots[this->renderer_registry.active_index].begin_frame(*this, frame_info);
        if (frame_result.completion_semaphore.has_value()) {
            if (*frame_result.completion_semaphore == VK_NULL_HANDLE) throw std::runtime_error("External completion semaphore must not be null");
            frame.external_waits.emplace_back(*frame_result.completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer);
        }
        if (frame_result.close_requested) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
        if (frame_result.window_detail.has_value()) this->window_title.detail = std::move(*frame_result.window_detail);
        return true;
    }

    void Spectra::record_frame(FrameState& frame) {
        this->draw_command_bar();
        this->draw_dockspace();
        this->draw_sidebar();
        this->draw_registered_panels();

        const vk::raii::CommandBuffer& command_buffer = this->sync.command_buffers[frame.frame_slot_index];
        command_buffer.reset();
        constexpr vk::CommandBufferBeginInfo command_buffer_begin_info{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        command_buffer.begin(command_buffer_begin_info);

        if (this->renderer_registry.slots.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->renderer_registry.active_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        this->renderer_registry.slots[this->renderer_registry.active_index].record_frame(command_buffer);

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
        wait_semaphore_infos.emplace_back(*this->sync.image_available_semaphores[frame.frame_slot_index], 0, vk::PipelineStageFlagBits2::eAllCommands);
        wait_semaphore_infos.insert(wait_semaphore_infos.end(), frame.external_waits.begin(), frame.external_waits.end());
        const vk::CommandBufferSubmitInfo command_buffer_submit_info{*this->sync.command_buffers[frame.frame_slot_index]};
        const vk::SemaphoreSubmitInfo signal_semaphore_info{*this->sync.render_finished_semaphores[frame.image_index], 0, vk::PipelineStageFlagBits2::eAllCommands};
        const vk::SubmitInfo2 submit_info{{}, static_cast<std::uint32_t>(wait_semaphore_infos.size()), wait_semaphore_infos.data(), 1, &command_buffer_submit_info, 1, &signal_semaphore_info};
        this->context.graphics_queue.submit2(submit_info, *this->sync.in_flight_fences[frame.frame_slot_index]);

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

        this->sync.frame_slot_index = (this->sync.frame_slot_index + 1) % this->sync.frame_count;
        ++this->timing.frame_number;
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

    void Spectra::shutdown_runtime() noexcept {
        this->detach_renderers();
        this->destroy_imgui();
        this->destroy_frame_sync();
        this->destroy_swapchain();
        this->destroy_surface_and_window();
        this->destroy_vulkan_context();
        this->terminate_glfw();
    }

    void Spectra::detach_renderers() noexcept {
        this->notify_renderers_before_imgui_shutdown();
        for (auto renderer = this->renderer_registry.slots.rbegin(); renderer != this->renderer_registry.slots.rend(); ++renderer) {
            renderer->detach();
        }
        this->renderer_registry.slots.clear();
        this->workspace.panels.clear();
        this->workspace.sidebar_tabs.clear();
        this->workspace.toolbar_actions.clear();
        this->renderer_registry.active_index    = 0;
        this->workspace.dock_layout_initialized = false;
        this->workspace.sidebar_visible         = true;
        this->workspace.active_sidebar_tab_id.clear();
        this->workspace.sidebar_tab_selection_requested = false;
    }

    void Spectra::notify_renderers_before_imgui_shutdown() noexcept {
        if (this->imgui.renderers_notified_before_shutdown) return;
        this->wait_device_idle_for_cleanup();
        for (auto renderer = this->renderer_registry.slots.rbegin(); renderer != this->renderer_registry.slots.rend(); ++renderer) {
            renderer->before_imgui_shutdown();
        }
        this->imgui.renderers_notified_before_shutdown = true;
    }

    void Spectra::wait_device_idle_for_cleanup() noexcept {
        try {
            if (*this->context.device) this->context.device.waitIdle();
        } catch (...) {
        }
    }

    void Spectra::destroy_imgui() noexcept {
        this->notify_renderers_before_imgui_shutdown();
        if (this->imgui.initialized) {
            ImGui_ImplVulkan_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
        this->imgui.descriptor_pool             = nullptr;
        this->imgui.initialized                 = false;
        this->workspace.dock_layout_initialized = false;
    }

    void Spectra::destroy_frame_sync() noexcept {
        this->sync.command_buffers.clear();
        this->sync.in_flight_fences.clear();
        this->sync.image_in_flight_frame.clear();
        this->sync.render_finished_semaphores.clear();
        this->sync.image_available_semaphores.clear();
    }

    void Spectra::destroy_swapchain() noexcept {
        this->swapchain.image_views.clear();
        this->swapchain.handle = nullptr;
        this->swapchain.image_layouts.clear();
        this->swapchain.images.clear();
    }

    void Spectra::destroy_surface_and_window() noexcept {
        this->surface.surface = nullptr;
        this->surface.window  = nullptr;
    }

    void Spectra::destroy_vulkan_context() noexcept {
        this->context.command_pool    = nullptr;
        this->context.graphics_queue  = nullptr;
        this->context.device          = nullptr;
        this->context.physical_device = nullptr;
        this->context.debug_messenger = nullptr;
        this->context.instance        = nullptr;
    }

    void Spectra::terminate_glfw() noexcept {
        if (this->surface.glfw_initialized) glfwTerminate();
        this->surface.glfw_initialized = false;
    }

    void Spectra::store_renderer(RendererSlot renderer) {
        const std::string_view renderer_name = renderer.name;
        if (renderer_name.empty()) throw std::runtime_error("Spectra renderer name must not be empty");
        for (const RendererSlot& existing_renderer : this->renderer_registry.slots) {
            if (existing_renderer.name == renderer_name) throw std::runtime_error(std::string{"Duplicate Spectra renderer name: "} + std::string{renderer_name});
        }
        const bool first_renderer = this->renderer_registry.slots.empty();
        if (this->renderer_registry.registering_name.has_value()) throw std::runtime_error("Nested Spectra renderer registration is not supported");
        const std::size_t panel_count              = this->workspace.panels.size();
        const std::size_t sidebar_tab_count        = this->workspace.sidebar_tabs.size();
        const std::size_t toolbar_action_count     = this->workspace.toolbar_actions.size();
        const bool dock_layout_initialized         = this->workspace.dock_layout_initialized;
        const bool sidebar_visible                 = this->workspace.sidebar_visible;
        const std::string active_sidebar_tab_id    = this->workspace.active_sidebar_tab_id;
        const bool sidebar_tab_selection_requested = this->workspace.sidebar_tab_selection_requested;
        this->renderer_registry.registering_name = std::string{renderer_name};
        try {
            renderer.attach(*this);
            if (this->imgui.initialized) renderer.after_imgui_created();
        } catch (...) {
            this->workspace.panels.resize(panel_count);
            this->workspace.sidebar_tabs.resize(sidebar_tab_count);
            this->workspace.toolbar_actions.resize(toolbar_action_count);
            this->workspace.dock_layout_initialized         = dock_layout_initialized;
            this->workspace.sidebar_visible                 = sidebar_visible;
            this->workspace.active_sidebar_tab_id           = active_sidebar_tab_id;
            this->workspace.sidebar_tab_selection_requested = sidebar_tab_selection_requested;
            this->renderer_registry.registering_name.reset();
            throw;
        }
        this->renderer_registry.registering_name.reset();
        this->renderer_registry.slots.push_back(std::move(renderer));
        if (first_renderer) this->renderer_registry.active_index = 0;
    }

    void Spectra::store_panel(Panel panel) {
        if (panel.id.empty()) throw std::runtime_error("Spectra panel id must not be empty");
        if (panel.title.empty()) throw std::runtime_error("Spectra panel title must not be empty");
        if (!panel.draw) throw std::runtime_error("Spectra panel draw callback must not be empty");
        switch (panel.dock_slot) {
        case DockSlot::Center:
        case DockSlot::Floating: break;
        default: throw std::runtime_error("Spectra panel dock slot is invalid");
        }
        for (const Panel& existing_panel : this->workspace.panels) {
            if (!owner_scopes_overlap(existing_panel.owner_renderer, panel.owner_renderer)) continue;
            if (existing_panel.id == panel.id) throw std::runtime_error(std::string{"Duplicate Spectra panel id: "} + panel.id);
            if (existing_panel.title == panel.title) throw std::runtime_error(std::string{"Duplicate Spectra panel title: "} + panel.title);
        }
        this->workspace.panels.push_back(std::move(panel));
        this->workspace.dock_layout_initialized = false;
    }

    void Spectra::store_sidebar_tab(SidebarTab tab) {
        if (tab.id.empty()) throw std::runtime_error("Spectra sidebar tab id must not be empty");
        if (tab.title.empty()) throw std::runtime_error("Spectra sidebar tab title must not be empty");
        if (!tab.draw) throw std::runtime_error("Spectra sidebar tab draw callback must not be empty");
        for (const SidebarTab& existing_tab : this->workspace.sidebar_tabs) {
            if (!owner_scopes_overlap(existing_tab.owner_renderer, tab.owner_renderer)) continue;
            if (existing_tab.id == tab.id) throw std::runtime_error(std::string{"Duplicate Spectra sidebar tab id: "} + tab.id);
            if (existing_tab.title == tab.title) throw std::runtime_error(std::string{"Duplicate Spectra sidebar tab title: "} + tab.title);
        }
        const bool initialize_active_sidebar_tab = this->workspace.active_sidebar_tab_id.empty();
        this->workspace.sidebar_tabs.push_back(std::move(tab));
        if (initialize_active_sidebar_tab) {
            this->workspace.active_sidebar_tab_id           = this->workspace.sidebar_tabs.back().id;
            this->workspace.sidebar_tab_selection_requested = true;
        }
        this->workspace.dock_layout_initialized = false;
    }

    void Spectra::store_toolbar_action(ToolbarAction action) {
        if (action.id.empty()) throw std::runtime_error("Spectra toolbar action id must not be empty");
        if (action.title.empty()) throw std::runtime_error("Spectra toolbar action title must not be empty");
        if (action.icon.empty()) throw std::runtime_error("Spectra toolbar action icon must not be empty");
        if (!action.active) throw std::runtime_error("Spectra toolbar action active callback must not be empty");
        if (!action.trigger) throw std::runtime_error("Spectra toolbar action trigger callback must not be empty");
        for (const ToolbarAction& existing_action : this->workspace.toolbar_actions) {
            if (!owner_scopes_overlap(existing_action.owner_renderer, action.owner_renderer)) continue;
            if (existing_action.id == action.id) throw std::runtime_error(std::string{"Duplicate Spectra toolbar action id: "} + action.id);
            if (existing_action.title == action.title) throw std::runtime_error(std::string{"Duplicate Spectra toolbar action title: "} + action.title);
        }
        this->workspace.toolbar_actions.push_back(std::move(action));
    }

    std::string Spectra::resolve_contribution_owner(std::string owner_renderer) const {
        if (!owner_renderer.empty()) {
            if (this->renderer_registry.registering_name.has_value() && owner_renderer != *this->renderer_registry.registering_name) throw std::runtime_error(std::format("Spectra UI owner \"{}\" does not match renderer registration scope \"{}\"", owner_renderer, *this->renderer_registry.registering_name));
            if (!this->renderer_registry.registering_name.has_value()) {
                const bool owner_found = std::ranges::any_of(this->renderer_registry.slots, [&owner_renderer](const RendererSlot& renderer) { return renderer.name == owner_renderer; });
                if (!owner_found) throw std::runtime_error(std::format("Spectra UI owner \"{}\" does not match a registered renderer", owner_renderer));
            }
            return owner_renderer;
        }
        if (this->renderer_registry.registering_name.has_value()) return *this->renderer_registry.registering_name;
        return {};
    }

    bool Spectra::contribution_belongs_to_active_renderer(const std::string_view owner_renderer) const {
        if (this->renderer_registry.slots.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->renderer_registry.active_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        if (owner_renderer.empty()) return true;
        return owner_renderer == this->renderer_registry.slots[this->renderer_registry.active_index].name;
    }

    void Spectra::sync_active_sidebar_tab() {
        for (const SidebarTab& tab : this->workspace.sidebar_tabs) {
            if (!this->contribution_belongs_to_active_renderer(tab.owner_renderer)) continue;
            if (tab.id == this->workspace.active_sidebar_tab_id) return;
        }
        for (const SidebarTab& tab : this->workspace.sidebar_tabs) {
            if (!this->contribution_belongs_to_active_renderer(tab.owner_renderer)) continue;
            this->workspace.active_sidebar_tab_id           = tab.id;
            this->workspace.sidebar_visible                 = true;
            this->workspace.sidebar_tab_selection_requested = true;
            return;
        }
        this->workspace.active_sidebar_tab_id.clear();
        this->workspace.sidebar_visible = false;
    }

    void Spectra::activate_renderer(const std::size_t renderer_index) {
        if (renderer_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        this->renderer_registry.active_index    = renderer_index;
        this->workspace.dock_layout_initialized = false;
        this->sync_active_sidebar_tab();
    }

    void Spectra::draw_command_bar() {
        ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            for (Panel& panel : this->workspace.panels) {
                if (!this->contribution_belongs_to_active_renderer(panel.owner_renderer)) continue;
                if (panel.shortcut_key != ImGuiKey_None && ImGui::IsKeyPressed(panel.shortcut_key, false)) panel.visible = !panel.visible;
            }
            this->sync_active_sidebar_tab();
            for (SidebarTab& tab : this->workspace.sidebar_tabs) {
                if (!this->contribution_belongs_to_active_renderer(tab.owner_renderer)) continue;
                if (tab.shortcut_key == ImGuiKey_None || !ImGui::IsKeyPressed(tab.shortcut_key, false)) continue;
                const bool selected                             = this->workspace.sidebar_visible && tab.id == this->workspace.active_sidebar_tab_id;
                this->workspace.active_sidebar_tab_id           = tab.id;
                this->workspace.sidebar_visible                 = !selected;
                this->workspace.sidebar_tab_selection_requested = true;
                this->workspace.dock_layout_initialized         = false;
            }
            for (ToolbarAction& action : this->workspace.toolbar_actions) {
                if (!this->contribution_belongs_to_active_renderer(action.owner_renderer)) continue;
                if (action.shortcut_key != ImGuiKey_None && ImGui::IsKeyPressed(action.shortcut_key, false)) action.trigger();
            }
        }
        this->sync_active_sidebar_tab();

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(ImVec2{main_viewport->WorkSize.x, command_bar_height()});

        constexpr ImGuiWindowFlags command_bar_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 7.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{13.0f / 255.0f, 15.0f / 255.0f, 18.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{42.0f / 255.0f, 48.0f / 255.0f, 55.0f / 255.0f, 1.0f});
        const bool began = ImGui::Begin("SpectraCommandBar", nullptr, command_bar_flags);
        if (!began) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4{232.0f / 255.0f, 236.0f / 255.0f, 243.0f / 255.0f, 1.0f}, "Spectra");
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, 10.0f);
        for (std::size_t renderer_index = 0; renderer_index < this->renderer_registry.slots.size(); ++renderer_index) {
            const bool selected = renderer_index == this->renderer_registry.active_index;
            push_renderer_button_style(selected);
            if (ImGui::Button(this->renderer_registry.slots[renderer_index].name.c_str(), ImVec2{0.0f, ImGui::GetFrameHeight()})) this->activate_renderer(renderer_index);
            pop_renderer_button_style();
            if (renderer_index + 1 < this->renderer_registry.slots.size()) ImGui::SameLine(0.0f, 6.0f);
        }

        std::vector<SidebarTab*> visible_tabs{};
        for (SidebarTab& tab : this->workspace.sidebar_tabs) {
            if (this->contribution_belongs_to_active_renderer(tab.owner_renderer)) visible_tabs.push_back(&tab);
        }

        std::vector<ToolbarAction*> visible_actions{};
        for (ToolbarAction& action : this->workspace.toolbar_actions) {
            if (this->contribution_belongs_to_active_renderer(action.owner_renderer)) visible_actions.push_back(&action);
        }

        if (visible_tabs.empty() && visible_actions.empty()) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return;
        }

        const std::size_t button_count = visible_tabs.size() + visible_actions.size();
        const float button_size        = std::max(30.0f, ImGui::GetFrameHeight());
        const float total_width        = static_cast<float>(button_count) * button_size + static_cast<float>(button_count + 1) * 6.0f + 8.0f;
        const float window_width       = ImGui::GetWindowWidth();
        if (window_width > total_width + ImGui::GetCursorPosX() + 24.0f) ImGui::SameLine(window_width - total_width - ImGui::GetStyle().WindowPadding.x);
        else ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, 6.0f);
        std::size_t button_index = 0;
        for (SidebarTab* tab : visible_tabs) {
            const bool selected = this->workspace.sidebar_visible && tab->id == this->workspace.active_sidebar_tab_id;
            const char* label   = tab->icon.empty() ? tab->title.c_str() : tab->icon.c_str();
            push_toolbar_button_style(selected);
            if (ImGui::Button(label, ImVec2{button_size, button_size})) {
                this->workspace.active_sidebar_tab_id           = tab->id;
                this->workspace.sidebar_visible                 = !selected;
                this->workspace.sidebar_tab_selection_requested = true;
                this->workspace.dock_layout_initialized         = false;
            }
            pop_toolbar_button_style();
            if (ImGui::IsItemHovered() && !tab->shortcut_label.empty()) ImGui::SetTooltip("%s (%s)", tab->title.c_str(), tab->shortcut_label.c_str());
            if (ImGui::IsItemHovered() && tab->shortcut_label.empty()) ImGui::SetTooltip("%s", tab->title.c_str());
            ++button_index;
            if (button_index < button_count) ImGui::SameLine(0.0f, 6.0f);
        }
        for (ToolbarAction* action : visible_actions) {
            const bool active = action->active();
            push_toolbar_button_style(active);
            if (ImGui::Button(action->icon.c_str(), ImVec2{button_size, button_size})) action->trigger();
            pop_toolbar_button_style();
            if (ImGui::IsItemHovered() && !action->shortcut_label.empty()) ImGui::SetTooltip("%s (%s)", action->title.c_str(), action->shortcut_label.c_str());
            if (ImGui::IsItemHovered() && action->shortcut_label.empty()) ImGui::SetTooltip("%s", action->title.c_str());
            ++button_index;
            if (button_index < button_count) ImGui::SameLine(0.0f, 6.0f);
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        const float bar_height = command_bar_height();
        const ImVec2 dock_pos{main_viewport->WorkPos.x, main_viewport->WorkPos.y + bar_height};
        const ImVec2 dock_size{main_viewport->WorkSize.x, std::max(1.0f, main_viewport->WorkSize.y - bar_height)};
        if (dock_size.x <= 640.0f || dock_size.y <= 360.0f) throw std::runtime_error("Viewport is too small for docked workspace");

        constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode;
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(dock_pos);
        ImGui::SetNextWindowSize(dock_size);
        constexpr ImGuiWindowFlags dockspace_window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{0.0f, 0.0f});
        const bool began = ImGui::Begin("SpectraDockspaceHost", nullptr, dockspace_window_flags);
        ImGui::PopStyleVar(3);
        if (!began) {
            ImGui::End();
            return;
        }
        const ImGuiID dockspace_id = ImGui::GetID("SpectraDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2{0.0f, 0.0f}, dockspace_flags);
        ImGui::End();
        if (dockspace_id == 0) throw std::runtime_error("Failed to create Spectra dockspace");
        if (this->workspace.dock_layout_initialized) return;

        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace | dockspace_flags);
        ImGui::DockBuilderSetNodePos(dockspace_id, dock_pos);
        ImGui::DockBuilderSetNodeSize(dockspace_id, dock_size);

        ImGuiID center_id = dockspace_id;
        ImGuiID right_id  = 0;
        if (this->workspace.sidebar_visible && !this->workspace.active_sidebar_tab_id.empty()) {
            right_id = ImGui::DockBuilderSplitNode(center_id, ImGuiDir_Right, 0.28f, nullptr, &center_id);
            if (right_id == 0 || center_id == 0) throw std::runtime_error("Failed to build Spectra sidebar dock layout");
            ImGui::DockBuilderDockWindow("Sidebar", right_id);
        }

        for (const Panel& panel : this->workspace.panels) {
            if (!this->contribution_belongs_to_active_renderer(panel.owner_renderer)) continue;
            switch (panel.dock_slot) {
            case DockSlot::Center: ImGui::DockBuilderDockWindow(panel.title.c_str(), center_id); break;
            case DockSlot::Floating: break;
            default: throw std::runtime_error("Spectra panel dock slot is invalid");
            }
        }
        const auto hide_tab_bar = [](const ImGuiID node_id) {
            ImGuiDockNode* node = ImGui::DockBuilderGetNode(node_id);
            if (node == nullptr) throw std::runtime_error("Failed to find Spectra dock node");
            node->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;
        };
        hide_tab_bar(center_id);
        if (right_id != 0) hide_tab_bar(right_id);
        ImGui::DockBuilderFinish(dockspace_id);
        this->workspace.dock_layout_initialized = true;
    }

    void Spectra::draw_sidebar() {
        if (!this->workspace.sidebar_visible || this->workspace.active_sidebar_tab_id.empty()) return;

        std::vector<SidebarTab*> visible_tabs{};
        for (SidebarTab& tab : this->workspace.sidebar_tabs) {
            if (this->contribution_belongs_to_active_renderer(tab.owner_renderer)) visible_tabs.push_back(&tab);
        }
        if (visible_tabs.empty()) return;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 10.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 8.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{18.0f / 255.0f, 21.0f / 255.0f, 25.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4{24.0f / 255.0f, 28.0f / 255.0f, 33.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4{38.0f / 255.0f, 47.0f / 255.0f, 56.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4{48.0f / 255.0f, 66.0f / 255.0f, 78.0f / 255.0f, 1.0f});
        const bool began = ImGui::Begin("Sidebar", nullptr, ImGuiWindowFlags_NoCollapse);
        if (!began) {
            ImGui::End();
            ImGui::PopStyleColor(4);
            ImGui::PopStyleVar(2);
            return;
        }
        if (ImGui::BeginTabBar("SidebarTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            for (SidebarTab* tab : visible_tabs) {
                const ImGuiTabItemFlags tab_flags = this->workspace.sidebar_tab_selection_requested && tab->id == this->workspace.active_sidebar_tab_id ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
                const std::string label           = tab->icon.empty() ? tab->title : std::format("{} {}", tab->icon, tab->title);
                if (ImGui::BeginTabItem(label.c_str(), nullptr, tab_flags)) {
                    this->workspace.active_sidebar_tab_id = tab->id;
                    ImGui::Spacing();
                    tab->draw();
                    ImGui::EndTabItem();
                }
            }
            this->workspace.sidebar_tab_selection_requested = false;
            ImGui::EndTabBar();
        }
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }

    void Spectra::draw_registered_panels() {
        for (Panel& panel : this->workspace.panels) {
            if (!this->contribution_belongs_to_active_renderer(panel.owner_renderer)) continue;
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

} // namespace spectra
