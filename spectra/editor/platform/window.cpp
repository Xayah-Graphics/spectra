module;

#include <Windows.h>

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <dwmapi.h>
#include <windowsx.h>

module spectra.editor;

import :platform.window;

import std;
import vulkan;

namespace spectra {
    WindowPlatform::GlfwLifetime::GlfwLifetime() {
        if (glfwInit() != GLFW_TRUE) throw std::runtime_error("GLFW initialization failed");
    }

    WindowPlatform::GlfwLifetime::~GlfwLifetime() {
        glfwTerminate();
    }

    WindowPlatform::WindowPlatform(const std::string_view application_name, const vk::Extent2D initial_extent) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        this->state.glfw_window.reset(glfwCreateWindow(static_cast<int>(initial_extent.width), static_cast<int>(initial_extent.height), std::string{application_name}.c_str(), nullptr, nullptr));
        if (!this->state.glfw_window) throw std::runtime_error("Spectra window creation failed");
        this->window             = this->state.glfw_window.get();
        this->native_window      = glfwGetWin32Window(this->window);
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        const auto small_icon    = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR | LR_SHARED));
        const auto large_icon    = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
        SendMessageW(this->native_window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
        SendMessageW(this->native_window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
        glfwSetWindowSizeLimits(this->window, 960, 600, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetWindowUserPointer(this->window, this);
        glfwSetDropCallback(this->window, [](GLFWwindow* window, const int count, const char** paths) {
            WindowPlatform& platform = *static_cast<WindowPlatform*>(glfwGetWindowUserPointer(window));
            platform.state.dropped_paths.clear();
            for (int index = 0; index < count; ++index) platform.state.dropped_paths.emplace_back(paths[index]);
        });
        glfwSetWindowCloseCallback(this->window, [](GLFWwindow* window) {
            WindowPlatform& platform       = *static_cast<WindowPlatform*>(glfwGetWindowUserPointer(window));
            platform.state.close_requested = true;
            glfwSetWindowShouldClose(window, GLFW_FALSE);
        });

        SetPropW(this->native_window, L"SpectraWindow", this);
        this->state.original_window_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(this->native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowPlatform::window_proc)));
        constexpr LONG_PTR style         = WS_POPUP | WS_VISIBLE | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtrW(this->native_window, GWL_STYLE, style);
        constexpr BOOL dark_mode = TRUE;
        DwmSetWindowAttribute(this->native_window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark_mode, sizeof(dark_mode));
        constexpr DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(this->native_window, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        SetWindowPos(this->native_window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    WindowPlatform::~WindowPlatform() {
        SetWindowLongPtrW(this->native_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(this->state.original_window_proc));
        RemovePropW(this->native_window, L"SpectraWindow");
    }

    LRESULT CALLBACK WindowPlatform::window_proc(HWND window, const UINT message, const WPARAM wparam, const LPARAM lparam) {
        WindowPlatform* platform = static_cast<WindowPlatform*>(GetPropW(window, L"SpectraWindow"));
        if (platform == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        switch (message) {
        case WM_NCCALCSIZE:
            if (wparam != 0) return 0;
            break;
        case WM_NCHITTEST:
            {
                POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &point);
                RECT client{};
                GetClientRect(window, &client);
                if (!IsZoomed(window)) {
                    const UINT dpi    = GetDpiForWindow(window);
                    const int border  = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                    const bool left   = point.x < border;
                    const bool right  = point.x >= client.right - border;
                    const bool top    = point.y < border;
                    const bool bottom = point.y >= client.bottom - border;
                    if (top && left) return HTTOPLEFT;
                    if (top && right) return HTTOPRIGHT;
                    if (bottom && left) return HTBOTTOMLEFT;
                    if (bottom && right) return HTBOTTOMRIGHT;
                    if (left) return HTLEFT;
                    if (right) return HTRIGHT;
                    if (top) return HTTOP;
                    if (bottom) return HTBOTTOM;
                }
                for (const std::array<float, 4>& region : platform->window_drag_regions)
                    if (static_cast<float>(point.x) >= region[0] && static_cast<float>(point.y) >= region[1] && static_cast<float>(point.x) < region[2] && static_cast<float>(point.y) < region[3]) return HTCAPTION;
                return HTCLIENT;
            }
        case WM_GETMINMAXINFO:
            {
                MONITORINFO monitor_info{sizeof(MONITORINFO)};
                GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor_info);
                MINMAXINFO& minmax    = *reinterpret_cast<MINMAXINFO*>(lparam);
                minmax.ptMaxPosition  = {monitor_info.rcWork.left - monitor_info.rcMonitor.left, monitor_info.rcWork.top - monitor_info.rcMonitor.top};
                minmax.ptMaxSize      = {monitor_info.rcWork.right - monitor_info.rcWork.left, monitor_info.rcWork.bottom - monitor_info.rcWork.top};
                minmax.ptMinTrackSize = {960, 600};
                return 0;
            }
        case WM_DPICHANGED:
            {
                const RECT& suggested = *reinterpret_cast<const RECT*>(lparam);
                SetWindowPos(window, nullptr, suggested.left, suggested.top, suggested.right - suggested.left, suggested.bottom - suggested.top, SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
        }
        return CallWindowProcW(platform->state.original_window_proc, window, message, wparam, lparam);
    }

    void WindowPlatform::poll_events() noexcept {
        glfwPollEvents();
    }

    void WindowPlatform::wait_events() noexcept {
        int width{};
        int height{};
        glfwGetFramebufferSize(this->window, &width, &height);
        if (width == 0 || height == 0)
            glfwWaitEvents();
        else
            glfwPollEvents();
    }

    void WindowPlatform::request_close() noexcept {
        this->state.close_requested = true;
    }

    bool WindowPlatform::take_close_request() noexcept {
        return std::exchange(this->state.close_requested, false);
    }

    std::vector<std::filesystem::path> WindowPlatform::take_dropped_paths() noexcept {
        return std::exchange(this->state.dropped_paths, {});
    }
} // namespace spectra
