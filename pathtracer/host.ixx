module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

export module spectra.pathtracer.host;

import std;

namespace spectra::pathtracer {
    export struct Panel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    export struct RendererPopoverTab {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    export struct ToolbarAction {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<bool()> enabled{};
        std::move_only_function<bool()> active{};
        std::move_only_function<void()> trigger{};
    };

    export struct FrameContext {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    export struct FrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    export template <typename HostType>
    concept Host = requires(HostType& host, Panel panel, RendererPopoverTab tab, ToolbarAction action) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.register_renderer_popover_tab(std::move(tab)) } -> std::same_as<void>;
        { host.register_toolbar_action(std::move(action)) } -> std::same_as<void>;
    };

    export class HostView {
    public:
        template <Host HostType>
        explicit HostView(HostType& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](Panel panel) { host.register_panel(std::move(panel)); }), registerRendererPopoverTabCallback([&host](RendererPopoverTab tab) { host.register_renderer_popover_tab(std::move(tab)); }), registerToolbarActionCallback([&host](ToolbarAction action) { host.register_toolbar_action(std::move(action)); }) {}

        HostView(const HostView& other)                = delete;
        HostView(HostView&& other) noexcept            = default;
        HostView& operator=(const HostView& other)     = delete;
        HostView& operator=(HostView&& other) noexcept = default;
        ~HostView() noexcept                           = default;

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() {
            return this->physicalDeviceCallback();
        }

        [[nodiscard]] const vk::raii::Device& device() {
            return this->deviceCallback();
        }

        [[nodiscard]] std::uint32_t frame_count() {
            return this->frameCountCallback();
        }

        [[nodiscard]] vk::Extent2D swapchain_extent() {
            return this->swapchainExtentCallback();
        }

        void register_panel(Panel panel) {
            this->registerPanelCallback(std::move(panel));
        }

        void register_renderer_popover_tab(RendererPopoverTab tab) {
            this->registerRendererPopoverTabCallback(std::move(tab));
        }

        void register_toolbar_action(ToolbarAction action) {
            this->registerToolbarActionCallback(std::move(action));
        }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physicalDeviceCallback{};
        std::move_only_function<const vk::raii::Device&()> deviceCallback{};
        std::move_only_function<std::uint32_t()> frameCountCallback{};
        std::move_only_function<vk::Extent2D()> swapchainExtentCallback{};
        std::move_only_function<void(Panel)> registerPanelCallback{};
        std::move_only_function<void(RendererPopoverTab)> registerRendererPopoverTabCallback{};
        std::move_only_function<void(ToolbarAction)> registerToolbarActionCallback{};
    };
} // namespace spectra::pathtracer
