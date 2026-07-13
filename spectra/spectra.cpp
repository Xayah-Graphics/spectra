module;

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#if defined(_WIN32)
#include <GLFW/glfw3native.h>
#endif
#include <material_symbols/IconsMaterialSymbols.h>
#include <material_symbols/material_symbols_rounded_regular.h>
#include <roboto/roboto_regular.h>

#include <ranges>

module spectra;

import imgui_impl_glfw;
import imgui_impl_vulkan;
import imgui_internal;
import std;
import vulkan;

namespace {
    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::ImageAspectFlags aspect, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            vk::QueueFamilyIgnored,
            vk::QueueFamilyIgnored,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(const vk::DebugUtilsMessageSeverityFlagBitsEXT severity, const vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*) {
        if (vk::DebugUtilsMessageSeverityFlagsEXT{severity} & (vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)) std::cerr << "validation layer: type " << vk::to_string(type) << " msg: " << callback_data->pMessage << std::endl;
        return vk::False;
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
        style.ChildBorderSize      = 0.0f;
        style.PopupRounding        = 7.0f;
        style.PopupBorderSize      = 1.0f;
        style.ColorButtonPosition  = ImGuiDir_Right;
        style.FrameRounding        = 5.0f;
        style.FrameBorderSize      = 0.0f;
        style.GrabRounding         = 5.0f;
        style.ScrollbarRounding    = 8.0f;
        style.TabRounding          = 6.0f;
        style.TabBorderSize        = 0.0f;
        style.IndentSpacing        = 14.0f;
        style.ItemSpacing          = ImVec2{8.0f, 6.0f};
        style.ItemInnerSpacing     = ImVec2{7.0f, 5.0f};
        style.FramePadding         = ImVec2{8.0f, 4.0f};
        style.CellPadding          = ImVec2{7.0f, 4.0f};
        style.WindowPadding        = ImVec2{10.0f, 9.0f};
        style.SeparatorTextPadding = ImVec2{6.0f, 3.0f};
        style.SelectableTextAlign  = ImVec2{0.0f, 0.5f};

        style.Colors[ImGuiCol_Text]                 = ImVec4{232.0f / 255.0f, 236.0f / 255.0f, 243.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TextDisabled]         = ImVec4{126.0f / 255.0f, 137.0f / 255.0f, 149.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_WindowBg]             = ImVec4{14.0f / 255.0f, 16.0f / 255.0f, 19.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ChildBg]              = ImVec4{17.0f / 255.0f, 20.0f / 255.0f, 24.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_PopupBg]              = ImVec4{19.0f / 255.0f, 23.0f / 255.0f, 27.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Border]               = ImVec4{50.0f / 255.0f, 58.0f / 255.0f, 66.0f / 255.0f, 0.58f};
        style.Colors[ImGuiCol_BorderShadow]         = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
        style.Colors[ImGuiCol_FrameBg]              = ImVec4{25.0f / 255.0f, 30.0f / 255.0f, 35.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_FrameBgHovered]       = ImVec4{34.0f / 255.0f, 43.0f / 255.0f, 50.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_FrameBgActive]        = ImVec4{38.0f / 255.0f, 57.0f / 255.0f, 64.0f / 255.0f, 1.0f};
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
        style.Colors[ImGuiCol_Button]               = ImVec4{27.0f / 255.0f, 33.0f / 255.0f, 38.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ButtonHovered]        = ImVec4{36.0f / 255.0f, 47.0f / 255.0f, 54.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_ButtonActive]         = ImVec4{38.0f / 255.0f, 68.0f / 255.0f, 76.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Header]               = ImVec4{26.0f / 255.0f, 33.0f / 255.0f, 38.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_HeaderHovered]        = ImVec4{36.0f / 255.0f, 50.0f / 255.0f, 58.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_HeaderActive]         = ImVec4{39.0f / 255.0f, 70.0f / 255.0f, 78.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_Separator]            = ImVec4{55.0f / 255.0f, 64.0f / 255.0f, 72.0f / 255.0f, 0.38f};
        style.Colors[ImGuiCol_SeparatorHovered]     = ImVec4{72.0f / 255.0f, 125.0f / 255.0f, 134.0f / 255.0f, 0.65f};
        style.Colors[ImGuiCol_SeparatorActive]      = ImVec4{86.0f / 255.0f, 169.0f / 255.0f, 174.0f / 255.0f, 0.85f};
        style.Colors[ImGuiCol_ResizeGrip]           = ImVec4{47.0f / 255.0f, 54.0f / 255.0f, 62.0f / 255.0f, 0.7f};
        style.Colors[ImGuiCol_ResizeGripHovered]    = ImVec4{78.0f / 255.0f, 112.0f / 255.0f, 132.0f / 255.0f, 0.8f};
        style.Colors[ImGuiCol_ResizeGripActive]     = ImVec4{92.0f / 255.0f, 145.0f / 255.0f, 169.0f / 255.0f, 0.95f};
        style.Colors[ImGuiCol_Tab]                  = ImVec4{22.0f / 255.0f, 27.0f / 255.0f, 31.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabHovered]           = ImVec4{35.0f / 255.0f, 50.0f / 255.0f, 57.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabActive]            = ImVec4{31.0f / 255.0f, 45.0f / 255.0f, 51.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabUnfocused]         = ImVec4{18.0f / 255.0f, 21.0f / 255.0f, 24.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TabUnfocusedActive]   = ImVec4{27.0f / 255.0f, 32.0f / 255.0f, 38.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_DockingPreview]       = ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.42f};
        style.Colors[ImGuiCol_DockingEmptyBg]       = ImVec4{14.0f / 255.0f, 16.0f / 255.0f, 19.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableHeaderBg]        = ImVec4{26.0f / 255.0f, 31.0f / 255.0f, 36.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableBorderStrong]    = ImVec4{50.0f / 255.0f, 58.0f / 255.0f, 67.0f / 255.0f, 1.0f};
        style.Colors[ImGuiCol_TableBorderLight]     = ImVec4{39.0f / 255.0f, 46.0f / 255.0f, 53.0f / 255.0f, 0.55f};
        style.Colors[ImGuiCol_TableRowBg]           = ImVec4{0.0f, 0.0f, 0.0f, 0.0f};
        style.Colors[ImGuiCol_TableRowBgAlt]        = ImVec4{24.0f / 255.0f, 28.0f / 255.0f, 33.0f / 255.0f, 0.58f};
        style.Colors[ImGuiCol_TextSelectedBg]       = ImVec4{64.0f / 255.0f, 124.0f / 255.0f, 163.0f / 255.0f, 0.45f};
        style.Colors[ImGuiCol_DragDropTarget]       = ImVec4{91.0f / 255.0f, 197.0f / 255.0f, 184.0f / 255.0f, 0.9f};
        style.Colors[ImGuiCol_NavHighlight]         = ImVec4{91.0f / 255.0f, 166.0f / 255.0f, 230.0f / 255.0f, 0.7f};
        style.Colors[ImGuiCol_ModalWindowDimBg]     = ImVec4{5.0f / 255.0f, 7.0f / 255.0f, 10.0f / 255.0f, 0.68f};
        ImGui::SetColorEditOptions(ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel);
    }

#if defined(_WIN32)
    [[nodiscard]] float window_chrome_caption_height(GLFWwindow* window);
    void set_window_chrome_top_margin(GLFWwindow* window, int top_margin);
#endif

    [[nodiscard]] float command_bar_height(GLFWwindow* window) {
        const ImGuiStyle& style = ImGui::GetStyle();
        float height            = std::max(42.0f, ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f);
#if defined(_WIN32)
        height = std::max(height, window_chrome_caption_height(window));
        set_window_chrome_top_margin(window, static_cast<int>(std::ceil(height)));
#else
        static_cast<void>(window);
#endif
        return height;
    }

    [[nodiscard]] bool draw_ellipsized_command_bar_text(const std::string_view text, const float width, const ImVec4 color) {
        if (text.empty() || width <= 0.0f) return false;
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::Dummy(ImVec2{width, ImGui::GetFrameHeight()});
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        const ImVec2 text_min{item_min.x, item_min.y + style.FramePadding.y};
        const ImVec2 text_max{item_max.x, text_min.y + ImGui::GetTextLineHeight()};
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), text_min, text_max, text_max.x, text.data(), text.data() + text.size(), nullptr);
        ImGui::PopStyleColor();
        return ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
    }

    void push_toolbar_button_style(const bool active) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4{35.0f / 255.0f, 64.0f / 255.0f, 72.0f / 255.0f, 1.0f} : ImVec4{24.0f / 255.0f, 29.0f / 255.0f, 34.0f / 255.0f, 0.92f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4{45.0f / 255.0f, 83.0f / 255.0f, 92.0f / 255.0f, 1.0f} : ImVec4{34.0f / 255.0f, 42.0f / 255.0f, 48.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_ButtonActive]);
        ImGui::PushStyleColor(ImGuiCol_Border, active ? ImVec4{93.0f / 255.0f, 207.0f / 255.0f, 199.0f / 255.0f, 0.72f} : style.Colors[ImGuiCol_Border]);
    }

    void pop_toolbar_button_style() {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    void push_renderer_button_style(const bool active) {
        const ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4{35.0f / 255.0f, 65.0f / 255.0f, 73.0f / 255.0f, 1.0f} : ImVec4{24.0f / 255.0f, 29.0f / 255.0f, 34.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? ImVec4{44.0f / 255.0f, 82.0f / 255.0f, 91.0f / 255.0f, 1.0f} : ImVec4{34.0f / 255.0f, 42.0f / 255.0f, 48.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_ButtonActive]);
        ImGui::PushStyleColor(ImGuiCol_Border, active ? ImVec4{93.0f / 255.0f, 207.0f / 255.0f, 199.0f / 255.0f, 0.68f} : ImVec4{47.0f / 255.0f, 55.0f / 255.0f, 62.0f / 255.0f, 0.62f});
    }

    void pop_renderer_button_style() {
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar();
    }

    [[nodiscard]] bool owner_scopes_overlap(const std::string& left, const std::string& right) {
        return left.empty() || right.empty() || left == right;
    }

#if defined(_WIN32)
    constexpr UINT_PTR window_chrome_subclass_id = 1u;

    struct WindowChromeState {
        RECT drag_region{};
        LONG caption_buttons_left{};
        LONG caption_buttons_bottom{};
        int frame_top_margin{};
        int minimum_width{};
        int minimum_height{};
        const char* deferred_operation{};
        std::uint32_t deferred_error{};
        bool deferred_error_is_hresult{};
        bool drag_region_valid{};
    };

    LRESULT CALLBACK window_chrome_subclass_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR subclass_id, DWORD_PTR reference_data);

    [[nodiscard]] HWND native_window_handle(GLFWwindow* window) {
        if (window == nullptr) throw std::runtime_error("Cannot access a Win32 handle from a null GLFW window");
        HWND const native_window = glfwGetWin32Window(window);
        if (native_window == nullptr) throw std::runtime_error("Failed to get Spectra Win32 window handle");
        return native_window;
    }

    void remember_window_chrome_error(WindowChromeState& state, const char* operation, const std::uint32_t error, const bool hresult) noexcept {
        if (state.deferred_operation != nullptr) return;
        state.deferred_operation        = operation;
        state.deferred_error            = error;
        state.deferred_error_is_hresult = hresult;
    }

    void remember_window_chrome_last_error(WindowChromeState& state, const char* operation) noexcept {
        const DWORD error = GetLastError();
        remember_window_chrome_error(state, operation, error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error, false);
    }

    [[nodiscard]] WindowChromeState& window_chrome_state(GLFWwindow* window) {
        const HWND native_window = native_window_handle(window);
        DWORD_PTR reference_data = 0u;
        if (!GetWindowSubclass(native_window, window_chrome_subclass_proc, window_chrome_subclass_id, &reference_data) || reference_data == 0u) throw std::runtime_error("Spectra Win32 window chrome is not installed");
        return *reinterpret_cast<WindowChromeState*>(reference_data);
    }

    void validate_window_chrome(GLFWwindow* window) {
        const WindowChromeState& state = window_chrome_state(window);
        if (state.deferred_operation == nullptr) return;
        if (state.deferred_error_is_hresult) throw std::runtime_error(std::format("{} failed with HRESULT 0x{:08X}", state.deferred_operation, state.deferred_error));
        throw std::system_error(static_cast<int>(state.deferred_error), std::system_category(), state.deferred_operation);
    }

    void extend_window_frame(HWND window, WindowChromeState& state, const int top_margin) {
        if (top_margin <= 0) throw std::runtime_error("Spectra Win32 frame top margin must be positive");
        const MARGINS margins{0, 0, top_margin, 0};
        const HRESULT result = DwmExtendFrameIntoClientArea(window, &margins);
        if (FAILED(result)) throw std::runtime_error(std::format("DwmExtendFrameIntoClientArea failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(result)));
        state.frame_top_margin = top_margin;
    }

    void extend_window_frame_noexcept(HWND window, WindowChromeState& state) noexcept {
        const MARGINS margins{0, 0, std::max(1, state.frame_top_margin), 0};
        const HRESULT result = DwmExtendFrameIntoClientArea(window, &margins);
        if (FAILED(result)) remember_window_chrome_error(state, "DwmExtendFrameIntoClientArea", static_cast<std::uint32_t>(result), true);
    }

    LRESULT CALLBACK window_chrome_subclass_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam, const UINT_PTR subclass_id, const DWORD_PTR reference_data) {
        if (subclass_id != window_chrome_subclass_id || reference_data == 0u) return DefSubclassProc(window, message, wparam, lparam);
        auto& state = *reinterpret_cast<WindowChromeState*>(reference_data);

        LRESULT dwm_result     = 0;
        const bool dwm_handled = DwmDefWindowProc(window, message, wparam, lparam, &dwm_result) == TRUE;

        if (message == WM_NCCALCSIZE && wparam == TRUE) return 0;

        if (message == WM_GETMINMAXINFO) {
            const LRESULT result    = DefSubclassProc(window, message, wparam, lparam);
            auto* const minmax_info = reinterpret_cast<MINMAXINFO*>(lparam);
            const HMONITOR monitor  = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
            MONITORINFO monitor_info{};
            monitor_info.cbSize = sizeof(monitor_info);
            if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) {
                remember_window_chrome_last_error(state, "GetMonitorInfoW");
                return result;
            }
            minmax_info->ptMaxPosition.x  = monitor_info.rcWork.left - monitor_info.rcMonitor.left;
            minmax_info->ptMaxPosition.y  = monitor_info.rcWork.top - monitor_info.rcMonitor.top;
            minmax_info->ptMaxSize.x      = monitor_info.rcWork.right - monitor_info.rcWork.left;
            minmax_info->ptMaxSize.y      = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
            minmax_info->ptMinTrackSize.x = state.minimum_width;
            minmax_info->ptMinTrackSize.y = state.minimum_height;
            return result;
        }

        if (message == WM_NCHITTEST) {
            if (dwm_handled) return dwm_result;
            const POINT screen_point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            if (!IsZoomed(window)) {
                RECT window_rect{};
                if (!GetWindowRect(window, &window_rect)) {
                    remember_window_chrome_last_error(state, "GetWindowRect");
                    return HTCLIENT;
                }
                const UINT dpi = GetDpiForWindow(window);
                if (dpi == 0u) {
                    remember_window_chrome_last_error(state, "GetDpiForWindow");
                    return HTCLIENT;
                }
                const int horizontal_border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const int vertical_border   = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                if (horizontal_border <= 0 || vertical_border <= 0) {
                    remember_window_chrome_error(state, "GetSystemMetricsForDpi", ERROR_INVALID_DATA, false);
                    return HTCLIENT;
                }
                const bool left   = screen_point.x >= window_rect.left && screen_point.x < window_rect.left + horizontal_border;
                const bool right  = screen_point.x < window_rect.right && screen_point.x >= window_rect.right - horizontal_border;
                const bool top    = screen_point.y >= window_rect.top && screen_point.y < window_rect.top + vertical_border;
                const bool bottom = screen_point.y < window_rect.bottom && screen_point.y >= window_rect.bottom - vertical_border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
            }
            POINT client_point = screen_point;
            if (!ScreenToClient(window, &client_point)) {
                remember_window_chrome_last_error(state, "ScreenToClient");
                return HTCLIENT;
            }
            if (state.drag_region_valid && client_point.x >= state.drag_region.left && client_point.x < state.drag_region.right && client_point.y >= state.drag_region.top && client_point.y < state.drag_region.bottom) return HTCAPTION;
            return HTCLIENT;
        }

        if (message == WM_DPICHANGED) {
            const LRESULT result = DefSubclassProc(window, message, wparam, lparam);
            extend_window_frame_noexcept(window, state);
            return result;
        }
        if (message == WM_DWMCOMPOSITIONCHANGED || message == WM_ACTIVATE) {
            extend_window_frame_noexcept(window, state);
            return DefSubclassProc(window, message, wparam, lparam);
        }
        if (dwm_handled) return dwm_result;
        return DefSubclassProc(window, message, wparam, lparam);
    }

    void install_window_chrome(GLFWwindow* window, const int width, const int height, const int minimum_width, const int minimum_height) {
        if (width <= 0 || height <= 0 || minimum_width <= 0 || minimum_height <= 0) throw std::runtime_error("Invalid Spectra Win32 window chrome dimensions");
        const HWND native_window = native_window_handle(window);
        auto state               = std::make_unique<WindowChromeState>();
        state->minimum_width     = minimum_width;
        state->minimum_height    = minimum_height;
        if (!SetWindowSubclass(native_window, window_chrome_subclass_proc, window_chrome_subclass_id, reinterpret_cast<DWORD_PTR>(state.get()))) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "SetWindowSubclass");
        }
        WindowChromeState* const installed_state = state.release();
        const BOOL dark_mode           = TRUE;
        const HRESULT dark_mode_result = DwmSetWindowAttribute(native_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
        if (FAILED(dark_mode_result)) throw std::runtime_error(std::format("DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE) failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(dark_mode_result)));
        constexpr DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_DEFAULT;
        const HRESULT corner_result                              = DwmSetWindowAttribute(native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference, sizeof(corner_preference));
        if (FAILED(corner_result)) throw std::runtime_error(std::format("DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE) failed with HRESULT 0x{:08X}; Spectra requires Windows 11", static_cast<std::uint32_t>(corner_result)));
        extend_window_frame(native_window, *installed_state, 42);

        RECT window_rect{};
        if (!GetWindowRect(native_window, &window_rect)) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "GetWindowRect");
        }
        const int centered_x = window_rect.left + ((window_rect.right - window_rect.left) - width) / 2;
        const int centered_y = window_rect.top + ((window_rect.bottom - window_rect.top) - height) / 2;
        if (!SetWindowPos(native_window, nullptr, centered_x, centered_y, width, height, SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOZORDER)) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "SetWindowPos(SWP_FRAMECHANGED)");
        }
        validate_window_chrome(window);
    }

    void destroy_window_chrome(GLFWwindow* window) noexcept {
        if (window == nullptr) return;
        const HWND native_window = glfwGetWin32Window(window);
        if (native_window == nullptr) return;
        DWORD_PTR reference_data = 0u;
        if (!GetWindowSubclass(native_window, window_chrome_subclass_proc, window_chrome_subclass_id, &reference_data) || reference_data == 0u) return;
        if (!RemoveWindowSubclass(native_window, window_chrome_subclass_proc, window_chrome_subclass_id)) return;
        delete reinterpret_cast<WindowChromeState*>(reference_data);
    }

    [[nodiscard]] bool refresh_window_chrome_metrics(GLFWwindow* window) {
        validate_window_chrome(window);
        const HWND native_window = native_window_handle(window);
        if (!IsWindowVisible(native_window) || IsIconic(native_window)) return false;
        WindowChromeState& state     = window_chrome_state(window);
        DWORD cloaked                = 0u;
        const HRESULT cloaked_result = DwmGetWindowAttribute(native_window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (FAILED(cloaked_result)) throw std::runtime_error(std::format("DwmGetWindowAttribute(DWMWA_CLOAKED) failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(cloaked_result)));
        if (cloaked != 0u) return false;

        RECT caption_bounds{};
        const HRESULT caption_result = DwmGetWindowAttribute(native_window, DWMWA_CAPTION_BUTTON_BOUNDS, &caption_bounds, sizeof(caption_bounds));
        if (FAILED(caption_result)) throw std::runtime_error(std::format("DwmGetWindowAttribute(DWMWA_CAPTION_BUTTON_BOUNDS) failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(caption_result)));
        RECT window_rect{};
        RECT client_rect{};
        POINT client_origin{};
        if (!GetWindowRect(native_window, &window_rect)) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "GetWindowRect");
        }
        if (!GetClientRect(native_window, &client_rect)) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "GetClientRect");
        }
        if (!ClientToScreen(native_window, &client_origin)) {
            const DWORD error = GetLastError();
            throw std::system_error(static_cast<int>(error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error), std::system_category(), "ClientToScreen");
        }

        const int client_offset_x = client_origin.x - window_rect.left;
        const int client_offset_y = client_origin.y - window_rect.top;
        const int caption_left    = caption_bounds.left - client_offset_x;
        const int caption_width   = caption_bounds.right - caption_bounds.left;
        const int caption_bottom  = caption_bounds.bottom - client_offset_y;
        if (caption_left <= client_rect.left || caption_left >= client_rect.right || caption_width <= 0 || caption_bottom <= client_rect.top || caption_bottom > client_rect.bottom) {
            if (state.caption_buttons_left > 0 && state.caption_buttons_bottom > 0) return true;
            throw std::runtime_error("DWM returned invalid Spectra caption button bounds");
        }
        state.caption_buttons_left   = caption_left;
        state.caption_buttons_bottom = caption_bottom;
        return true;
    }

    [[nodiscard]] float window_chrome_caption_buttons_left(GLFWwindow* window) {
        validate_window_chrome(window);
        const LONG left = window_chrome_state(window).caption_buttons_left;
        if (left <= 0) throw std::runtime_error("Spectra caption button bounds are unavailable");
        return static_cast<float>(left);
    }

    [[nodiscard]] float window_chrome_caption_height(GLFWwindow* window) {
        validate_window_chrome(window);
        const LONG height = window_chrome_state(window).caption_buttons_bottom;
        if (height <= 0) throw std::runtime_error("Spectra caption button height is unavailable");
        return static_cast<float>(height);
    }

    [[nodiscard]] vk::ClearRect window_chrome_caption_clear_rect(GLFWwindow* window, const vk::Extent2D swapchain_extent) {
        validate_window_chrome(window);
        const WindowChromeState& state = window_chrome_state(window);
        if (state.caption_buttons_left <= 0 || state.caption_buttons_bottom <= 0) throw std::runtime_error("Spectra caption button bounds are unavailable");
        const auto caption_left   = static_cast<std::uint32_t>(state.caption_buttons_left);
        const auto caption_bottom = static_cast<std::uint32_t>(state.caption_buttons_bottom);
        if (caption_left >= swapchain_extent.width || caption_bottom > swapchain_extent.height) throw std::runtime_error("Spectra caption button bounds exceed the Vulkan swapchain extent");
        return vk::ClearRect{vk::Rect2D{{static_cast<std::int32_t>(caption_left), 0}, {swapchain_extent.width - caption_left, caption_bottom}}, 0u, 1u};
    }

    void set_window_chrome_top_margin(GLFWwindow* window, const int top_margin) {
        WindowChromeState& state = window_chrome_state(window);
        if (state.frame_top_margin == top_margin) return;
        extend_window_frame(native_window_handle(window), state, top_margin);
    }

    void clear_window_chrome_drag_region(GLFWwindow* window) {
        WindowChromeState& state = window_chrome_state(window);
        state.drag_region        = {};
        state.drag_region_valid  = false;
    }

    void set_window_chrome_drag_region(GLFWwindow* window, const ImVec2 minimum, const ImVec2 maximum, const ImGuiViewport& viewport) {
        if (!(maximum.x > minimum.x) || !(maximum.y > minimum.y)) return;
        WindowChromeState& state = window_chrome_state(window);
        state.drag_region        = RECT{
            static_cast<LONG>(std::floor(minimum.x - viewport.Pos.x)),
            static_cast<LONG>(std::floor(minimum.y - viewport.Pos.y)),
            static_cast<LONG>(std::ceil(maximum.x - viewport.Pos.x)),
            static_cast<LONG>(std::ceil(maximum.y - viewport.Pos.y)),
        };
        state.drag_region_valid = state.drag_region.right > state.drag_region.left && state.drag_region.bottom > state.drag_region.top;
    }

    [[nodiscard]] HICON load_spectra_window_icon(const int size) {
        HINSTANCE__* const instance = GetModuleHandleW(nullptr);
        if (instance == nullptr) throw std::runtime_error("Failed to get Spectra executable module for window icon");
        const auto icon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED));
        if (icon == nullptr) throw std::runtime_error("Failed to load Spectra window icon resource");
        return icon;
    }

    void apply_spectra_window_icon(GLFWwindow* window) {
        const HWND native_window = native_window_handle(window);
        SendMessageW(native_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(load_spectra_window_icon(GetSystemMetrics(SM_CXSMICON))));
        SendMessageW(native_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(load_spectra_window_icon(GetSystemMetrics(SM_CXICON))));
    }
#endif

    [[nodiscard]] constexpr std::array<const char*, 5> required_device_extensions() {
#if defined(_WIN32)
        return {vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName, vk::KHRExternalMemoryWin32ExtensionName, vk::KHRExternalSemaphoreWin32ExtensionName};
#else
        return {vk::KHRSwapchainExtensionName, vk::KHRExternalMemoryExtensionName, vk::KHRExternalSemaphoreExtensionName, vk::KHRExternalMemoryFdExtensionName, vk::KHRExternalSemaphoreFdExtensionName};
#endif
    }

    constexpr std::array preferred_swapchain_surface_formats{
        vk::SurfaceFormatKHR{vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
        vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear},
    };

    [[nodiscard]] std::optional<vk::SurfaceFormatKHR> select_preferred_swapchain_surface_format(const std::span<const vk::SurfaceFormatKHR> surface_formats) {
        if (surface_formats.size() == 1u && surface_formats.front().format == vk::Format::eUndefined) return preferred_swapchain_surface_formats.front();
        for (const vk::SurfaceFormatKHR preferred_surface_format : preferred_swapchain_surface_formats) {
            for (const vk::SurfaceFormatKHR surface_format : surface_formats) {
                if (surface_format.format == preferred_surface_format.format && surface_format.colorSpace == preferred_surface_format.colorSpace) return preferred_surface_format;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr vk::CompositeAlphaFlagBitsKHR required_swapchain_composite_alpha() {
#if defined(_WIN32)
        return vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
#else
        return vk::CompositeAlphaFlagBitsKHR::eOpaque;
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
        bool surface_lost{false};
        std::vector<vk::SemaphoreSubmitInfo> external_waits{};
    };

    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) try {
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
#if defined(_WIN32)
        glfwShowWindow(this->surface.window.get());
        glfwPollEvents();
        const HRESULT flush_result = DwmFlush();
        if (FAILED(flush_result)) throw std::runtime_error(std::format("DwmFlush failed with HRESULT 0x{:08X}", static_cast<std::uint32_t>(flush_result)));
        while (!refresh_window_chrome_metrics(this->surface.window.get())) {
            if (glfwWindowShouldClose(this->surface.window.get())) return;
            glfwWaitEvents();
        }
        set_window_chrome_top_margin(this->surface.window.get(), static_cast<int>(std::ceil(std::max(42.0f, window_chrome_caption_height(this->surface.window.get())))));
#endif
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

            const vk::ApplicationInfo application_info{app_name_string.c_str(), vk::makeApiVersion(0u, 1u, 0u, 0u), engine_name_string.c_str(), vk::makeApiVersion(0u, 1u, 0u, 0u), vk::ApiVersion14};
            const vk::InstanceCreateInfo instance_create_info{{}, &application_info, static_cast<std::uint32_t>(enabled_instance_layers.size()), enabled_instance_layers.data(), static_cast<std::uint32_t>(enabled_instance_extensions.size()), enabled_instance_extensions.data()};
            this->context.instance = vk::raii::Instance{this->context.context, instance_create_info};
        }
    }

    void Spectra::create_debug_messenger() {
        constexpr vk::DebugUtilsMessengerCreateInfoEXT debug_messenger_create_info{
            {},
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
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
#if defined(_WIN32)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#endif
        GLFWwindow* const window = glfwCreateWindow(static_cast<int>(window_width), static_cast<int>(window_height), app_name_string.c_str(), nullptr, nullptr);
#if defined(_WIN32)
        glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
#endif
        this->surface.window.reset(window);
        if (this->surface.window == nullptr) throw std::runtime_error("Failed to create GLFW window");
        glfwSetWindowSizeLimits(this->surface.window.get(), 800, 480, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(this->surface.window.get(), this);
        glfwSetFramebufferSizeCallback(this->surface.window.get(), [](GLFWwindow* window, int, int) { static_cast<Spectra*>(glfwGetWindowUserPointer(window))->surface.resize_requested = true; });
        glfwSetDropCallback(this->surface.window.get(), [](GLFWwindow* window, const int path_count, const char** paths) {
            const auto spectra = static_cast<Spectra*>(glfwGetWindowUserPointer(window));
            if (spectra == nullptr) return;
            spectra->queue_file_drop(path_count, paths);
        });
#if defined(_WIN32)
        apply_spectra_window_icon(this->surface.window.get());
        install_window_chrome(this->surface.window.get(), static_cast<int>(window_width), static_cast<int>(window_height), 800, 480);
        this->surface.resize_requested = false;
#endif
    }

    void Spectra::create_surface() {
        VkSurfaceKHR surface{};
        if (glfwCreateWindowSurface(*this->context.instance, this->surface.window.get(), nullptr, &surface) != static_cast<VkResult>(vk::Result::eSuccess)) throw std::runtime_error("Failed to create Vulkan surface");
        this->surface.surface = vk::raii::SurfaceKHR{this->context.instance, surface};
    }

    void Spectra::validate_initial_framebuffer_extent() const {
        int width  = 0;
        int height = 0;
        glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
        if (width <= 0 || height <= 0) throw std::runtime_error("Invalid GLFW framebuffer size");
    }

    void Spectra::select_physical_device() {
        constexpr std::array<const char*, 5> enabled_device_extensions = required_device_extensions();
        bool selected                                                  = false;
        for (const vk::raii::PhysicalDevice& physical_device : this->context.instance.enumeratePhysicalDevices()) {
            if (physical_device.getProperties().apiVersion < vk::ApiVersion14) continue;

            const std::vector<vk::ExtensionProperties> available_extensions = physical_device.enumerateDeviceExtensionProperties();
            bool required_extensions_available                              = true;
            for (const char* required_extension : enabled_device_extensions) {
                if (const auto found = std::ranges::find(available_extensions, std::string_view{required_extension}, [](const vk::ExtensionProperties& extension) { return std::string_view{extension.extensionName.data()}; }); found == available_extensions.end()) required_extensions_available = false;
            }
            if (!required_extensions_available) continue;

            const vk::SurfaceCapabilitiesKHR surface_capabilities = physical_device.getSurfaceCapabilitiesKHR(this->surface.surface);
            if (!static_cast<bool>(surface_capabilities.supportedCompositeAlpha & required_swapchain_composite_alpha())) continue;
#if defined(_WIN32)
            if (surface_capabilities.currentTransform != vk::SurfaceTransformFlagBitsKHR::eIdentity) continue;
#endif
            const std::vector<vk::SurfaceFormatKHR> surface_formats = physical_device.getSurfaceFormatsKHR(this->surface.surface);
            if (!select_preferred_swapchain_surface_format(surface_formats).has_value()) continue;

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
        if (!selected) {
#if defined(_WIN32)
            throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with BGRA8/RGBA8 sRGB presentation, identity surface transform, premultiplied composite alpha, external memory, external semaphore, and graphics-present queue support");
#else
            throw std::runtime_error("Failed to find a Vulkan 1.4 physical device with swapchain, external memory, external semaphore, and graphics-present queue support");
#endif
        }
    }

    void Spectra::create_logical_device() {
        const auto supported_features = this->context.physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features>();
        if (!supported_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters) throw std::runtime_error("Device does not support shaderDrawParameters");
        if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2) throw std::runtime_error("Device does not support synchronization2");
        if (!supported_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering) throw std::runtime_error("Device does not support dynamicRendering");

        vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan13Features> enabled_features{{}, {}, {}};
        enabled_features.get<vk::PhysicalDeviceVulkan11Features>().shaderDrawParameters = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().synchronization2     = vk::True;
        enabled_features.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering     = vk::True;

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

        const std::optional<vk::SurfaceFormatKHR> preferred_surface_format = select_preferred_swapchain_surface_format(surface_formats);
        if (!preferred_surface_format.has_value()) throw std::runtime_error("Spectra requires a BGRA8 or RGBA8 UNORM surface format with the sRGB nonlinear color space");
        this->swapchain.format      = preferred_surface_format->format;
        this->swapchain.color_space = preferred_surface_format->colorSpace;

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
        constexpr vk::ImageUsageFlags swapchain_usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst;
        if ((surface_capabilities.supportedUsageFlags & swapchain_usage) != swapchain_usage) throw std::runtime_error("Vulkan surface does not support required swapchain image usage");

        const vk::SurfaceTransformFlagBitsKHR pre_transform = surface_capabilities.currentTransform;
#if defined(_WIN32)
        if (pre_transform != vk::SurfaceTransformFlagBitsKHR::eIdentity) throw std::runtime_error("Spectra Win32 title bar requires an identity Vulkan surface transform");
#endif
        constexpr vk::CompositeAlphaFlagBitsKHR composite_alpha = required_swapchain_composite_alpha();
        if (!static_cast<bool>(surface_capabilities.supportedCompositeAlpha & composite_alpha)) throw std::runtime_error("Vulkan surface does not support the required swapchain composite alpha mode");
        const vk::SwapchainCreateInfoKHR swapchain_create_info{{}, *this->surface.surface, image_count, this->swapchain.format, this->swapchain.color_space, this->swapchain.extent, 1, swapchain_usage, vk::SharingMode::eExclusive, 0, nullptr, pre_transform, composite_alpha, this->swapchain.present_mode, vk::True, *old_swapchain};
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
            const vk::Format imgui_color_format       = this->swapchain.format;
            const std::uint32_t imgui_min_image_count = std::max(2u, this->sync.frame_count);
            const auto imgui_image_count     = static_cast<std::uint32_t>(this->swapchain.images.size());
            if (imgui_image_count < imgui_min_image_count) throw std::runtime_error("ImGui image count is smaller than minimum image count");

            ImGui::DebugCheckVersionAndDataLayout("1.92.8", sizeof(ImGuiIO), sizeof(ImGuiStyle), sizeof(ImVec2), sizeof(ImVec4), sizeof(ImDrawVert), sizeof(ImDrawIdx));
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
            pipeline_rendering_create_info.sType                   = static_cast<VkStructureType>(vk::StructureType::ePipelineRenderingCreateInfo);
            pipeline_rendering_create_info.colorAttachmentCount    = 1;
            pipeline_rendering_create_info.pColorAttachmentFormats = &color_attachment_format;

            ImGui_ImplVulkan_InitInfo init_info{};
            init_info.ApiVersion                                   = vk::ApiVersion14;
            init_info.Instance                                     = static_cast<VkInstance>(*this->context.instance);
            init_info.PhysicalDevice                               = static_cast<VkPhysicalDevice>(*this->context.physical_device);
            init_info.Device                                       = static_cast<VkDevice>(*this->context.device);
            init_info.QueueFamily                                  = this->context.graphics_queue_index;
            init_info.Queue                                        = static_cast<VkQueue>(*this->context.graphics_queue);
            init_info.DescriptorPoolSize                           = 16u;
            init_info.MinImageCount                                = imgui_min_image_count;
            init_info.ImageCount                                   = imgui_image_count;
            init_info.PipelineInfoMain.MSAASamples                 = static_cast<VkSampleCountFlagBits>(vk::SampleCountFlagBits::e1);
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
            this->imgui.initialized                        = false;
            this->imgui.renderers_notified_before_shutdown = false;
            throw;
        }
    }

    bool Spectra::begin_frame(FrameState& frame) {
        glfwPollEvents();
#if defined(_WIN32)
        validate_window_chrome(this->surface.window.get());
#endif
        if (glfwWindowShouldClose(this->surface.window.get())) return false;
        if (glfwGetWindowAttrib(this->surface.window.get(), GLFW_ICONIFIED) == GLFW_TRUE) {
            this->timing.last_frame_time_valid = false;
            glfwWaitEvents();
            return false;
        }
        if (this->surface.resize_requested) {
            this->recreate_swapchain();
            return false;
        }
#if defined(_WIN32)
        static_cast<void>(refresh_window_chrome_metrics(this->surface.window.get()));
#endif

        frame.recreate_after_present                    = false;
        frame.frame_slot_index                          = this->sync.frame_slot_index;
        frame.frame_number                              = this->timing.frame_number;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (this->timing.last_frame_time_valid) frame.delta_seconds = std::chrono::duration<double>(now - this->timing.last_frame_time).count();
        this->timing.last_frame_time       = now;
        this->timing.last_frame_time_valid = true;
        if (!std::isfinite(frame.delta_seconds) || frame.delta_seconds < 0.0) throw std::runtime_error("Spectra frame delta time is invalid");
        if (this->context.device.waitForFences(*this->sync.in_flight_fences[frame.frame_slot_index], vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for frame fence");

        try {
            const vk::ResultValue<std::uint32_t> acquired_image = this->swapchain.handle.acquireNextImage(std::numeric_limits<std::uint64_t>::max(), *this->sync.image_available_semaphores[frame.frame_slot_index], nullptr);
            if (acquired_image.result != vk::Result::eSuccess && acquired_image.result != vk::Result::eSuboptimalKHR) throw std::runtime_error(std::string{"Failed to acquire swapchain image: "} + vk::to_string(acquired_image.result));
            frame.recreate_after_present = acquired_image.result == vk::Result::eSuboptimalKHR;
            frame.image_index            = acquired_image.value;
        } catch (const vk::OutOfDateKHRError&) {
            this->recreate_swapchain();
            return false;
        } catch (const vk::SystemError& error) {
            if (error.code().value() != static_cast<int>(vk::Result::eErrorSurfaceLostKHR)) throw;
            this->recreate_surface_and_swapchain();
            return false;
        }

        if (const std::uint32_t previous_frame_slot_index = this->sync.image_in_flight_frame.at(frame.image_index); previous_frame_slot_index != std::numeric_limits<std::uint32_t>::max()) {
            if (this->context.device.waitForFences(*this->sync.in_flight_fences.at(previous_frame_slot_index), vk::True, std::numeric_limits<std::uint64_t>::max()) != vk::Result::eSuccess) throw std::runtime_error("Failed to wait for swapchain image fence");
        }
        this->sync.image_in_flight_frame.at(frame.image_index) = frame.frame_slot_index;
        this->context.device.resetFences(*this->sync.in_flight_fences[frame.frame_slot_index]);
        if (!this->imgui.initialized) throw std::runtime_error("ImGui is not initialized");
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::GetMainViewport() == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
        this->dispatch_file_drops();

        if (this->renderer_registry.slots.empty()) throw std::runtime_error("Spectra requires at least one registered renderer");
        if (this->renderer_registry.active_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        const FrameContext frame_info{
            .frame_index      = frame.frame_slot_index,
            .image_index      = frame.image_index,
            .frame_number     = frame.frame_number,
            .delta_seconds    = frame.delta_seconds,
        };
        const FrameResult frame_result = this->renderer_registry.slots[this->renderer_registry.active_index].begin_frame(*this, frame_info);
        if (frame_result.completion_semaphore.has_value()) {
            if (*frame_result.completion_semaphore == vk::Semaphore{}) throw std::runtime_error("External completion semaphore must not be null");
            frame.external_waits.emplace_back(*frame_result.completion_semaphore, 0, vk::PipelineStageFlagBits2::eTransfer);
        }
        if (frame_result.close_requested) glfwSetWindowShouldClose(this->surface.window.get(), GLFW_TRUE);
        return true;
    }

    void Spectra::record_frame(FrameState& frame) {
        this->process_command_bar_shortcuts();
        this->draw_dockspace();
        this->draw_registered_panels();
        this->draw_command_popover();
        this->draw_command_bar();

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
                vk::QueueFamilyIgnored,
                vk::QueueFamilyIgnored,
                this->swapchain.images[frame.image_index],
                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
            };
            const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &color_barrier};
            command_buffer.pipelineBarrier2(dependency_info);
        }
        this->swapchain.image_layouts[frame.image_index] = vk::ImageLayout::eColorAttachmentOptimal;

        constexpr std::array clear_color{0.02f, 0.02f, 0.025f, 1.0f};
        constexpr vk::ClearValue color_clear_value{vk::ClearColorValue{clear_color}};
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
#if defined(_WIN32)
        constexpr vk::ClearAttachment transparent_caption_attachment{vk::ImageAspectFlagBits::eColor, 0u, vk::ClearValue{vk::ClearColorValue{std::array{0.0f, 0.0f, 0.0f, 0.0f}}}};
        const std::array transparent_caption_rects{window_chrome_caption_clear_rect(this->surface.window.get(), this->swapchain.extent)};
        command_buffer.clearAttachments(std::array{transparent_caption_attachment}, transparent_caption_rects);
#endif
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
        const auto set_present_result = [&frame](const vk::Result present_result) {
            if (present_result == vk::Result::eSuboptimalKHR || present_result == vk::Result::eErrorOutOfDateKHR) {
                frame.recreate_after_present = true;
                return;
            }
            if (present_result == vk::Result::eErrorSurfaceLostKHR) {
                frame.surface_lost = true;
                return;
            }
            throw std::runtime_error(std::string{"Failed to present swapchain image: "} + vk::to_string(present_result));
        };
        try {
            if (const vk::Result present_result = this->context.graphics_queue.presentKHR(present_info); present_result != vk::Result::eSuccess) set_present_result(present_result);
        } catch (const vk::OutOfDateKHRError&) {
            frame.recreate_after_present = true;
        } catch (const vk::SystemError& error) {
            if (error.code().value() != static_cast<int>(vk::Result::eErrorSurfaceLostKHR) && error.code().value() != static_cast<int>(vk::Result::eSuboptimalKHR) && error.code().value() != static_cast<int>(vk::Result::eErrorOutOfDateKHR)) throw;
            set_present_result(static_cast<vk::Result>(error.code().value()));
        }
        if (frame.surface_lost) this->recreate_surface_and_swapchain();
        else if (frame.recreate_after_present) this->recreate_swapchain();

        this->sync.frame_slot_index = (this->sync.frame_slot_index + 1) % this->sync.frame_count;
        ++this->timing.frame_number;
    }

    void Spectra::recreate_swapchain() {
        int width  = 0;
        int height = 0;
        while (true) {
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (glfwWindowShouldClose(this->surface.window.get())) return;
            if (glfwGetWindowAttrib(this->surface.window.get(), GLFW_ICONIFIED) == GLFW_FALSE && width > 0 && height > 0) break;
            this->timing.last_frame_time_valid = false;
            glfwWaitEvents();
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

    void Spectra::recreate_surface_and_swapchain() {
        int width  = 0;
        int height = 0;
        while (true) {
            glfwGetFramebufferSize(this->surface.window.get(), &width, &height);
            if (glfwWindowShouldClose(this->surface.window.get())) return;
            if (glfwGetWindowAttrib(this->surface.window.get(), GLFW_ICONIFIED) == GLFW_FALSE && width > 0 && height > 0) break;
            this->timing.last_frame_time_valid = false;
            glfwWaitEvents();
        }

        this->context.device.waitIdle();
        this->destroy_imgui();
        this->destroy_swapchain();
        this->surface.surface = nullptr;
        this->create_surface();
        if (!this->context.physical_device.getSurfaceSupportKHR(this->context.graphics_queue_index, this->surface.surface)) throw std::runtime_error("Recreated Vulkan surface is unsupported by the selected graphics queue");
        this->create_swapchain();
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
        for (auto & slot : std::views::reverse(this->renderer_registry.slots)) {
            slot.detach();
        }
        this->renderer_registry.slots.clear();
        this->workspace.panels.clear();
        this->workspace.command_popovers.clear();
        this->workspace.viewport_overlays.clear();
        this->workspace.toolbar_actions.clear();
        this->workspace.title_provider = nullptr;
        this->file_drop.handlers.clear();
        this->file_drop.pending_batches.clear();
        this->renderer_registry.active_index    = 0;
        this->workspace.dock_layout_initialized = false;
        this->workspace.command_popover_open    = false;
        this->workspace.active_command_popover_id.clear();
    }

    void Spectra::notify_renderers_before_imgui_shutdown() noexcept {
        if (this->imgui.renderers_notified_before_shutdown) return;
        this->wait_device_idle_for_cleanup();
        for (auto & slot : std::views::reverse(this->renderer_registry.slots)) {
            slot.before_imgui_shutdown();
        }
        this->imgui.renderers_notified_before_shutdown = true;
    }

    void Spectra::wait_device_idle_for_cleanup() const noexcept {
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
#if defined(_WIN32)
        destroy_window_chrome(this->surface.window.get());
#endif
        this->surface.window = nullptr;
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
        if (this->renderer_registry.registering_name.has_value()) throw std::runtime_error("Nested Spectra renderer registration is not supported");
        const std::size_t panel_count                                = this->workspace.panels.size();
        const std::size_t command_popover_count                      = this->workspace.command_popovers.size();
        const std::size_t viewport_overlay_count                     = this->workspace.viewport_overlays.size();
        const std::size_t toolbar_action_count                       = this->workspace.toolbar_actions.size();
        const std::size_t file_drop_handler_count                    = this->file_drop.handlers.size();
        const bool dock_layout_initialized                           = this->workspace.dock_layout_initialized;
        const bool command_popover_open                              = this->workspace.command_popover_open;
        const std::string active_command_popover_id                  = this->workspace.active_command_popover_id;
        std::move_only_function<WorkspaceTitle()> title_provider     = std::move(this->workspace.title_provider);
        this->renderer_registry.registering_name = std::string{renderer_name};
        try {
            renderer.attach(*this);
            if (this->imgui.initialized) renderer.after_imgui_created();
        } catch (...) {
            this->workspace.panels.resize(panel_count);
            this->workspace.command_popovers.resize(command_popover_count);
            this->workspace.viewport_overlays.resize(viewport_overlay_count);
            this->workspace.toolbar_actions.resize(toolbar_action_count);
            this->file_drop.handlers.resize(file_drop_handler_count);
            this->workspace.dock_layout_initialized = dock_layout_initialized;
            this->workspace.command_popover_open    = command_popover_open;
            this->workspace.active_command_popover_id = active_command_popover_id;
            this->workspace.title_provider = std::move(title_provider);
            this->renderer_registry.registering_name.reset();
            throw;
        }
        if (!this->workspace.title_provider) this->workspace.title_provider = std::move(title_provider);
        this->renderer_registry.registering_name.reset();
        this->renderer_registry.slots.push_back(std::move(renderer));
    }

    void Spectra::store_panel(Panel panel) {
        if (panel.id.empty()) throw std::runtime_error("Spectra panel id must not be empty");
        if (panel.title.empty()) throw std::runtime_error("Spectra panel title must not be empty");
        if (!panel.draw) throw std::runtime_error("Spectra panel draw callback must not be empty");
        for (const Panel& existing_panel : this->workspace.panels) {
            if (!owner_scopes_overlap(existing_panel.owner_renderer, panel.owner_renderer)) continue;
            if (existing_panel.id == panel.id) throw std::runtime_error(std::string{"Duplicate Spectra panel id: "} + panel.id);
            if (existing_panel.title == panel.title) throw std::runtime_error(std::string{"Duplicate Spectra panel title: "} + panel.title);
        }
        this->workspace.panels.push_back(std::move(panel));
        this->workspace.dock_layout_initialized = false;
    }

    void Spectra::store_command_popover(CommandPopover popover) {
        if (popover.id.empty()) throw std::runtime_error("Spectra command popover id must not be empty");
        if (popover.title.empty()) throw std::runtime_error("Spectra command popover title must not be empty");
        if (popover.icon.empty()) throw std::runtime_error("Spectra command popover icon must not be empty");
        if (!popover.draw) throw std::runtime_error("Spectra command popover draw callback must not be empty");
        for (const CommandPopover& existing_popover : this->workspace.command_popovers) {
            if (!owner_scopes_overlap(existing_popover.owner_renderer, popover.owner_renderer)) continue;
            if (existing_popover.id == popover.id) throw std::runtime_error(std::string{"Duplicate Spectra command popover id: "} + popover.id);
            if (existing_popover.title == popover.title) throw std::runtime_error(std::string{"Duplicate Spectra command popover title: "} + popover.title);
        }
        const bool initialize_active_command_popover = this->workspace.active_command_popover_id.empty();
        this->workspace.command_popovers.push_back(std::move(popover));
        if (initialize_active_command_popover) this->workspace.active_command_popover_id = this->workspace.command_popovers.back().id;
    }

    void Spectra::store_viewport_overlay(ViewportOverlay overlay) {
        if (overlay.id.empty()) throw std::runtime_error("Spectra viewport overlay id must not be empty");
        if (!overlay.draw) throw std::runtime_error("Spectra viewport overlay draw callback must not be empty");
        for (const ViewportOverlay& existing_overlay : this->workspace.viewport_overlays) {
            if (!owner_scopes_overlap(existing_overlay.owner_renderer, overlay.owner_renderer)) continue;
            if (existing_overlay.id == overlay.id) throw std::runtime_error(std::string{"Duplicate Spectra viewport overlay id: "} + overlay.id);
        }
        this->workspace.viewport_overlays.push_back(std::move(overlay));
    }

    void Spectra::store_toolbar_action(ToolbarAction action) {
        if (action.id.empty()) throw std::runtime_error("Spectra toolbar action id must not be empty");
        if (action.title.empty()) throw std::runtime_error("Spectra toolbar action title must not be empty");
        if (action.icon.empty()) throw std::runtime_error("Spectra toolbar action icon must not be empty");
        if (!action.enabled) throw std::runtime_error("Spectra toolbar action enabled callback must not be empty");
        if (!action.active) throw std::runtime_error("Spectra toolbar action active callback must not be empty");
        if (!action.trigger) throw std::runtime_error("Spectra toolbar action trigger callback must not be empty");
        for (const ToolbarAction& existing_action : this->workspace.toolbar_actions) {
            if (!owner_scopes_overlap(existing_action.owner_renderer, action.owner_renderer)) continue;
            if (existing_action.id == action.id) throw std::runtime_error(std::string{"Duplicate Spectra toolbar action id: "} + action.id);
            if (existing_action.title == action.title) throw std::runtime_error(std::string{"Duplicate Spectra toolbar action title: "} + action.title);
        }
        this->workspace.toolbar_actions.push_back(std::move(action));
    }

    void Spectra::set_workspace_title_provider(std::move_only_function<WorkspaceTitle()> provider) {
        if (!provider) throw std::runtime_error("Spectra workspace title provider must not be empty");
        this->workspace.title_provider = std::move(provider);
    }

    void Spectra::open_command_popover(std::string id) {
        if (id.empty()) throw std::runtime_error("Spectra command popover id must not be empty");
        for (const CommandPopover& popover : this->workspace.command_popovers) {
            if (popover.id != id) continue;
            this->workspace.active_command_popover_id = std::move(id);
            this->workspace.command_popover_open      = true;
            return;
        }
        throw std::runtime_error(std::string{"Unknown Spectra command popover id: "} + id);
    }

    void Spectra::close_command_popover(const std::string& id) {
        if (id.empty()) throw std::runtime_error("Spectra command popover id must not be empty");
        if (this->workspace.active_command_popover_id == id) this->workspace.command_popover_open = false;
    }

    void Spectra::draw_viewport_overlays(const ImVec2 viewport_position, const ImVec2 viewport_size) {
        if (!(viewport_size.x > 1.0f) || !(viewport_size.y > 1.0f)) return;
        std::vector<ViewportOverlay*> visible_overlays{};
        for (ViewportOverlay& overlay : this->workspace.viewport_overlays) {
            if (this->contribution_belongs_to_active_renderer(overlay.owner_renderer)) visible_overlays.push_back(&overlay);
        }
        std::ranges::stable_sort(visible_overlays, [](const ViewportOverlay* left, const ViewportOverlay* right) {
            if (left->priority != right->priority) return left->priority < right->priority;
            return left->id < right->id;
        });
        for (ViewportOverlay* overlay : visible_overlays) overlay->draw(viewport_position, viewport_size);
    }

    void Spectra::store_file_drop_handler(FileDropHandler handler) {
        if (handler.id.empty()) throw std::runtime_error("Spectra file drop handler id must not be empty");
        if (!handler.handle) throw std::runtime_error("Spectra file drop handler callback must not be empty");
        for (const FileDropHandler& existing_handler : this->file_drop.handlers) {
            if (!owner_scopes_overlap(existing_handler.owner_renderer, handler.owner_renderer)) continue;
            if (existing_handler.id == handler.id) throw std::runtime_error(std::string{"Duplicate Spectra file drop handler id: "} + handler.id);
        }
        this->file_drop.handlers.push_back(std::move(handler));
    }

    void Spectra::queue_file_drop(const int path_count, const char** paths) noexcept {
        if (path_count <= 0 || paths == nullptr) return;
        std::vector<std::filesystem::path> batch{};
        batch.reserve(static_cast<std::size_t>(path_count));
        for (int path_index = 0; path_index < path_count; ++path_index) {
            if (paths[path_index] == nullptr) continue;
            batch.push_back(std::filesystem::path{paths[path_index]}.lexically_normal());
        }
        if (!batch.empty()) this->file_drop.pending_batches.push_back(std::move(batch));
    }

    void Spectra::dispatch_file_drops() {
        if (this->file_drop.pending_batches.empty()) return;
        const std::vector<std::vector<std::filesystem::path>> pending_batches = std::exchange(this->file_drop.pending_batches, {});
        for (const std::vector<std::filesystem::path>& batch : pending_batches) {
            const std::span paths{batch.data(), batch.size()};
            for (FileDropHandler& handler : this->file_drop.handlers) {
                if (!this->contribution_belongs_to_active_renderer(handler.owner_renderer)) continue;
                if (handler.handle(paths)) break;
            }
        }
    }

    std::string Spectra::resolve_contribution_owner(std::string owner_renderer) const {
        if (!owner_renderer.empty()) {
            if (this->renderer_registry.registering_name.has_value() && owner_renderer != *this->renderer_registry.registering_name) throw std::runtime_error(std::format(R"(Spectra UI owner "{}" does not match renderer registration scope "{}")", owner_renderer, *this->renderer_registry.registering_name));
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

    void Spectra::sync_active_command_popover() {
        for (const CommandPopover& popover : this->workspace.command_popovers) {
            if (!this->contribution_belongs_to_active_renderer(popover.owner_renderer)) continue;
            if (popover.id == this->workspace.active_command_popover_id) return;
        }
        for (const CommandPopover& popover : this->workspace.command_popovers) {
            if (!this->contribution_belongs_to_active_renderer(popover.owner_renderer)) continue;
            this->workspace.active_command_popover_id = popover.id;
            this->workspace.command_popover_open      = false;
            return;
        }
        this->workspace.active_command_popover_id.clear();
        this->workspace.command_popover_open = false;
    }

    void Spectra::activate_renderer(const std::size_t renderer_index) {
        if (renderer_index >= this->renderer_registry.slots.size()) throw std::runtime_error("Spectra active renderer index is out of range");
        this->renderer_registry.active_index    = renderer_index;
        this->workspace.dock_layout_initialized = false;
        this->sync_active_command_popover();
    }

    void Spectra::process_command_bar_shortcuts() {
        const ImGuiIO& io = ImGui::GetIO();
        if (!io.WantTextInput) {
            for (Panel& panel : this->workspace.panels) {
                if (!this->contribution_belongs_to_active_renderer(panel.owner_renderer)) continue;
                if (panel.shortcut_key != ImGuiKey_None && ImGui::IsKeyPressed(panel.shortcut_key, false)) panel.visible = !panel.visible;
            }
            this->sync_active_command_popover();
            for (CommandPopover& popover : this->workspace.command_popovers) {
                if (!this->contribution_belongs_to_active_renderer(popover.owner_renderer)) continue;
                if (popover.shortcut_key == ImGuiKey_None || !ImGui::IsKeyPressed(popover.shortcut_key, false)) continue;
                const bool selected = this->workspace.command_popover_open && popover.id == this->workspace.active_command_popover_id;
                this->workspace.active_command_popover_id = popover.id;
                this->workspace.command_popover_open      = !selected;
            }
            for (ToolbarAction& action : this->workspace.toolbar_actions) {
                if (!this->contribution_belongs_to_active_renderer(action.owner_renderer)) continue;
                if (!action.enabled()) continue;
                if (action.shortcut_key != ImGuiKey_None && ImGui::IsKeyPressed(action.shortcut_key, false)) action.trigger();
            }
        }
        this->sync_active_command_popover();
    }

    void Spectra::draw_command_bar() {
        this->sync_active_command_popover();
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
#if defined(_WIN32)
        clear_window_chrome_drag_region(this->surface.window.get());
#endif
        const float bar_height = command_bar_height(this->surface.window.get());
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(main_viewport->WorkPos);
        ImGui::SetNextWindowSize(ImVec2{main_viewport->WorkSize.x, bar_height});

        constexpr ImGuiWindowFlags command_bar_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{12.0f, 7.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f, 0.0f});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{12.0f / 255.0f, 14.0f / 255.0f, 17.0f / 255.0f, 1.0f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{42.0f / 255.0f, 50.0f / 255.0f, 58.0f / 255.0f, 0.72f});
        const bool began = ImGui::Begin("SpectraCommandBar", nullptr, command_bar_flags);
        if (!began) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return;
        }

        WorkspaceTitle workspace_title{};
        if (this->workspace.title_provider) workspace_title = this->workspace.title_provider();

        std::vector<CommandPopover*> visible_popovers{};
        for (CommandPopover& popover : this->workspace.command_popovers) {
            if (this->contribution_belongs_to_active_renderer(popover.owner_renderer)) visible_popovers.push_back(&popover);
        }
        std::ranges::stable_sort(visible_popovers, [](const CommandPopover* left, const CommandPopover* right) { return left->owner_renderer.empty() && !right->owner_renderer.empty(); });

        std::vector<ToolbarAction*> visible_actions{};
        for (ToolbarAction& action : this->workspace.toolbar_actions) {
            if (this->contribution_belongs_to_active_renderer(action.owner_renderer)) visible_actions.push_back(&action);
        }

        const ImGuiStyle& style        = ImGui::GetStyle();
        const std::size_t button_count = visible_popovers.size() + visible_actions.size();
        const float button_size        = std::max(30.0f, ImGui::GetFrameHeight());
        const float toolbar_width      = button_count == 0u ? 0.0f : 1.0f + 6.0f + static_cast<float>(button_count) * button_size + static_cast<float>(button_count - 1u) * 6.0f;
        const float window_width       = ImGui::GetWindowWidth();
        float caption_buttons_left     = window_width;
#if defined(_WIN32)
        caption_buttons_left = window_chrome_caption_buttons_left(this->surface.window.get()) - (main_viewport->WorkPos.x - main_viewport->Pos.x);
#endif
        const float right_edge      = std::min(window_width - style.WindowPadding.x, caption_buttons_left - 8.0f);
        const float toolbar_start_x = right_edge - toolbar_width;
        if (toolbar_start_x <= style.WindowPadding.x) throw std::runtime_error("Spectra command bar has no room for its right-side controls");

        float renderer_buttons_width = 0.0f;
        for (const RendererSlot& renderer : this->renderer_registry.slots) renderer_buttons_width += ImGui::CalcTextSize(renderer.name.c_str()).x + style.FramePadding.x * 2.0f;
        if (!this->renderer_registry.slots.empty()) renderer_buttons_width += static_cast<float>(this->renderer_registry.slots.size() - 1u) * 6.0f;
        constexpr std::string_view command_bar_title   = "Spectra";
        const ImGuiIO& io                              = ImGui::GetIO();
        const bool performance_ready                   = std::isfinite(io.Framerate) && io.Framerate > 0.0f;
        constexpr float minimum_drag_width             = 64.0f;
        constexpr float left_separator_width           = 10.0f + 1.0f + 10.0f;
        constexpr float performance_spacing            = 8.0f;
        constexpr float performance_horizontal_padding = 10.0f;
        const float minimum_title_right                = style.WindowPadding.x + ImGui::CalcTextSize(command_bar_title.data()).x;
        const auto left_text_boundary                  = [&](const float performance_width) {
            const float performance_reservation = performance_width > 0.0f ? performance_width + performance_spacing : 0.0f;
            return toolbar_start_x - minimum_drag_width - performance_reservation - left_separator_width - renderer_buttons_width;
        };

        std::string performance_text = performance_ready ? std::format("{:.0f} FPS \xC2\xB7 {:.2f} ms", io.Framerate, 1000.0f / io.Framerate) : std::string{"Collecting"};
        float performance_width      = ImGui::CalcTextSize(performance_text.c_str()).x + performance_horizontal_padding * 2.0f;
        if (left_text_boundary(performance_width) <= minimum_title_right && performance_ready) {
            performance_text  = std::format("{:.0f} FPS", io.Framerate);
            performance_width = ImGui::CalcTextSize(performance_text.c_str()).x + performance_horizontal_padding * 2.0f;
        }
        if (left_text_boundary(performance_width) <= minimum_title_right) {
            performance_text.clear();
            performance_width = 0.0f;
        }
        const float left_text_right = left_text_boundary(performance_width);

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4{232.0f / 255.0f, 236.0f / 255.0f, 243.0f / 255.0f, 1.0f}, "%s", command_bar_title.data());
        bool workspace_title_hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        const float title_right      = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        if (left_text_right <= title_right) throw std::runtime_error("Spectra command bar has no room for its title and renderer controls");

        float remaining_text_width     = left_text_right - title_right;
        float detail_width             = 0.0f;
        const float text_line_height   = ImGui::GetTextLineHeight();
        const float status_reservation = workspace_title.status_text.empty() ? 0.0f : 10.0f + text_line_height;
        if (!workspace_title.detail.empty()) {
            const float detail_prefix_width = 6.0f + ImGui::CalcTextSize("/").x + 6.0f;
            const float detail_available    = remaining_text_width - detail_prefix_width - status_reservation;
            if (detail_available >= text_line_height) {
                detail_width = std::min(ImGui::CalcTextSize(workspace_title.detail.c_str()).x, detail_available);
                remaining_text_width -= detail_prefix_width + detail_width;
            }
        }
        float status_width = 0.0f;
        if (!workspace_title.status_text.empty() && remaining_text_width > 10.0f + text_line_height) status_width = std::min(260.0f, remaining_text_width - 10.0f);

        if (detail_width > 0.0f) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(ImVec4{94.0f / 255.0f, 105.0f / 255.0f, 116.0f / 255.0f, 1.0f}, "/");
            ImGui::SameLine(0.0f, 6.0f);
            const bool detail_hovered = draw_ellipsized_command_bar_text(workspace_title.detail, detail_width, ImVec4{188.0f / 255.0f, 197.0f / 255.0f, 208.0f / 255.0f, 1.0f});
            workspace_title_hovered   = workspace_title_hovered || detail_hovered;
        }
        if (status_width > 0.0f) {
            ImGui::SameLine(0.0f, 10.0f);
            const ImVec4 status_color = workspace_title.status_error ? ImVec4{1.0f, 0.42f, 0.36f, 1.0f} : ImVec4{111.0f / 255.0f, 207.0f / 255.0f, 185.0f / 255.0f, 1.0f};
            if (draw_ellipsized_command_bar_text(workspace_title.status_text, status_width, status_color)) ImGui::SetTooltip("%s", workspace_title.status_text.c_str());
        }
        if (workspace_title_hovered && !workspace_title.tooltip.empty()) ImGui::SetTooltip("%s", workspace_title.tooltip.c_str());
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine(0.0f, 10.0f);
        std::optional<std::size_t> requested_renderer_index{};
        for (std::size_t renderer_index = 0; renderer_index < this->renderer_registry.slots.size(); ++renderer_index) {
            const bool selected = renderer_index == this->renderer_registry.active_index;
            push_renderer_button_style(selected);
            if (ImGui::Button(this->renderer_registry.slots[renderer_index].name.c_str(), ImVec2{0.0f, ImGui::GetFrameHeight()})) requested_renderer_index = renderer_index;
            pop_renderer_button_style();
            if (renderer_index + 1 < this->renderer_registry.slots.size()) ImGui::SameLine(0.0f, 6.0f);
        }

        const float left_controls_end     = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
        const ImVec2 command_bar_position = ImGui::GetWindowPos();
        if (performance_width > 0.0f) {
            const float performance_height = ImGui::GetFrameHeight();
            const float performance_right  = toolbar_start_x - performance_spacing;
            const ImVec2 performance_min{command_bar_position.x + performance_right - performance_width, command_bar_position.y + (bar_height - performance_height) * 0.5f};
            const ImVec2 performance_max{command_bar_position.x + performance_right, performance_min.y + performance_height};
            const ImVec2 performance_text_size = ImGui::CalcTextSize(performance_text.c_str());
            ImDrawList* draw_list              = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(performance_min, performance_max, ImGui::GetColorU32(ImVec4{26.0f / 255.0f, 31.0f / 255.0f, 36.0f / 255.0f, 0.92f}), performance_height * 0.5f);
            draw_list->AddRect(performance_min, performance_max, ImGui::GetColorU32(ImVec4{57.0f / 255.0f, 67.0f / 255.0f, 76.0f / 255.0f, 0.62f}), performance_height * 0.5f);
            draw_list->AddText(ImVec2{performance_min.x + performance_horizontal_padding, performance_min.y + (performance_height - performance_text_size.y) * 0.5f}, ImGui::GetColorU32(ImVec4{188.0f / 255.0f, 198.0f / 255.0f, 209.0f / 255.0f, 1.0f}), performance_text.c_str());
        }
#if defined(_WIN32)
        const float drag_region_left  = left_controls_end + 8.0f;
        const float drag_region_right = toolbar_start_x - 8.0f;
        if (drag_region_right > drag_region_left) {
            set_window_chrome_drag_region(this->surface.window.get(), ImVec2{command_bar_position.x + drag_region_left, command_bar_position.y}, ImVec2{command_bar_position.x + drag_region_right, command_bar_position.y + bar_height}, *main_viewport);
        }
#endif

        std::size_t button_index = 0;
        if (button_count > 0u) {
            ImGui::SameLine(toolbar_start_x);
            ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
            ImGui::SameLine(0.0f, 6.0f);
            for (const CommandPopover* popover : visible_popovers) {
                const bool selected = this->workspace.command_popover_open && popover->id == this->workspace.active_command_popover_id;
                const char* label   = popover->icon.c_str();
                push_toolbar_button_style(selected);
                if (ImGui::Button(label, ImVec2{button_size, button_size})) {
                    this->workspace.active_command_popover_id = popover->id;
                    this->workspace.command_popover_open      = !selected;
                }
                pop_toolbar_button_style();
                if (ImGui::IsItemHovered() && !popover->shortcut_label.empty()) ImGui::SetTooltip("%s (%s)", popover->title.c_str(), popover->shortcut_label.c_str());
                if (ImGui::IsItemHovered() && popover->shortcut_label.empty()) ImGui::SetTooltip("%s", popover->title.c_str());
                ++button_index;
                if (button_index < button_count) ImGui::SameLine(0.0f, 6.0f);
            }
            for (ToolbarAction* action : visible_actions) {
                const bool active  = action->active();
                const bool enabled = action->enabled();
                push_toolbar_button_style(active);
                ImGui::BeginDisabled(!enabled);
                if (ImGui::Button(action->icon.c_str(), ImVec2{button_size, button_size})) action->trigger();
                ImGui::EndDisabled();
                pop_toolbar_button_style();
                if (ImGui::IsItemHovered() && !action->shortcut_label.empty()) ImGui::SetTooltip("%s (%s)", action->title.c_str(), action->shortcut_label.c_str());
                if (ImGui::IsItemHovered() && action->shortcut_label.empty()) ImGui::SetTooltip("%s", action->title.c_str());
                ++button_index;
                if (button_index < button_count) ImGui::SameLine(0.0f, 6.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        if (requested_renderer_index.has_value()) this->activate_renderer(*requested_renderer_index);
    }

    void Spectra::draw_dockspace() {
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        const float bar_height = command_bar_height(this->surface.window.get());
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

        for (const Panel& panel : this->workspace.panels) {
            if (!this->contribution_belongs_to_active_renderer(panel.owner_renderer)) continue;
            ImGui::DockBuilderDockWindow(panel.title.c_str(), dockspace_id);
        }
        ImGuiDockNode* dock_node = ImGui::DockBuilderGetNode(dockspace_id);
        if (dock_node == nullptr) throw std::runtime_error("Failed to find Spectra dock node");
        dock_node->LocalFlags |= ImGuiDockNodeFlags_HiddenTabBar;
        ImGui::DockBuilderFinish(dockspace_id);
        this->workspace.dock_layout_initialized = true;
    }

    void Spectra::draw_command_popover() {
        if (!this->workspace.command_popover_open || this->workspace.active_command_popover_id.empty()) return;

        CommandPopover* active_popover = nullptr;
        for (CommandPopover& popover : this->workspace.command_popovers) {
            if (!this->contribution_belongs_to_active_renderer(popover.owner_renderer)) continue;
            if (popover.id != this->workspace.active_command_popover_id) continue;
            active_popover = &popover;
            break;
        }
        if (active_popover == nullptr) {
            this->sync_active_command_popover();
            return;
        }

        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        if (main_viewport == nullptr) throw std::runtime_error("ImGui main viewport is unavailable");
        constexpr float margin = 12.0f;
        const float bar_height = command_bar_height(this->surface.window.get());
        const float popover_width = std::min(460.0f, std::max(360.0f, main_viewport->WorkSize.x * 0.28f));
        const float popover_height = main_viewport->WorkSize.y - bar_height - margin * 2.0f;
        if (popover_height <= 120.0f) throw std::runtime_error("Viewport is too small for the Spectra command popover");
        const ImVec2 popover_position{main_viewport->WorkPos.x + main_viewport->WorkSize.x - popover_width - margin, main_viewport->WorkPos.y + bar_height + margin};
        ImGui::SetNextWindowViewport(main_viewport->ID);
        ImGui::SetNextWindowPos(popover_position, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2{popover_width, popover_height}, ImGuiCond_Always);

        constexpr ImGuiWindowFlags popover_flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{15.0f, 13.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{8.0f, 6.0f});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 7.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{17.0f / 255.0f, 20.0f / 255.0f, 24.0f / 255.0f, 0.985f});
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{66.0f / 255.0f, 77.0f / 255.0f, 86.0f / 255.0f, 0.48f});
        const bool began = ImGui::Begin("SpectraCommandPopover", nullptr, popover_flags);
        if (!began) {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);
            return;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4{93.0f / 255.0f, 207.0f / 255.0f, 199.0f / 255.0f, 0.9f}, "%s", active_popover->icon.c_str());
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(active_popover->title.c_str());
        const ImVec2 header_line_min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2{0.0f, 5.0f});
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddLine(ImVec2{header_line_min.x, header_line_min.y + 2.0f}, ImVec2{header_line_min.x + ImGui::GetContentRegionAvail().x, header_line_min.y + 2.0f}, ImGui::GetColorU32(ImVec4{62.0f / 255.0f, 72.0f / 255.0f, 81.0f / 255.0f, 0.45f}), 1.0f);
        ImGui::Spacing();
        active_popover->draw();
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
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

} // namespace spectra
