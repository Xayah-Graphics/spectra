module;

#include <Windows.h>

#include <GLFW/glfw3.h>

export module spectra.editor.platform.window;

import std;
import vulkan;

namespace spectra {
    export struct WindowPlatform {
        explicit WindowPlatform(std::string_view application_name, vk::Extent2D initial_extent);
        ~WindowPlatform();

        WindowPlatform(const WindowPlatform&)            = delete;
        WindowPlatform(WindowPlatform&&)                 = delete;
        WindowPlatform& operator=(const WindowPlatform&) = delete;
        WindowPlatform& operator=(WindowPlatform&&)      = delete;

        void poll_events() noexcept;
        void wait_events() noexcept;
        void request_close() noexcept;
        [[nodiscard]] bool take_close_request() noexcept;
        [[nodiscard]] bool take_resize_completion() noexcept;
        [[nodiscard]] std::vector<std::filesystem::path> take_dropped_paths() noexcept;

        GLFWwindow* window{};
        HWND native_window{};
        std::array<std::array<float, 4>, 2> window_drag_regions{};

    private:
        struct GlfwLifetime {
            GlfwLifetime();
            ~GlfwLifetime();

            GlfwLifetime(const GlfwLifetime&)            = delete;
            GlfwLifetime(GlfwLifetime&&)                 = delete;
            GlfwLifetime& operator=(const GlfwLifetime&) = delete;
            GlfwLifetime& operator=(GlfwLifetime&&)      = delete;
        };

        static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

        struct {
            GlfwLifetime glfw{};
            std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> glfw_window{nullptr, glfwDestroyWindow};
            WNDPROC original_window_proc{};
            std::vector<std::filesystem::path> dropped_paths{};
            bool close_requested{};
            bool resize_completed{};
        } state;
    };
} // namespace spectra
