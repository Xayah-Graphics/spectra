module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module xayah.spectra.pathtracer;

import std;

export namespace xayah {
    enum class PathtracerDockSlot {
        Center,
        Left,
        LeftBottom,
        Right,
        RightBottom,
        Bottom,
        Floating,
    };

    struct PathtracerPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        PathtracerDockSlot dock_slot{PathtracerDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool show_in_menu{true};
        bool show_in_toolbar{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct PathtracerFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct PathtracerFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Frame>
    concept PathtracerFrameInfoLike = requires(const Frame& frame) {
        { frame.frame_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
    };

    template <typename Host>
    concept PathtracerHost = requires(Host& host, PathtracerPanel panel, std::string detail) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.set_window_detail(std::move(detail)) } -> std::same_as<void>;
    };

    class PathtracerHostView {
    public:
        template <PathtracerHost Host>
        explicit PathtracerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](PathtracerPanel panel) { host.register_panel(std::move(panel)); }), setWindowDetailCallback([&host](std::string detail) { host.set_window_detail(std::move(detail)); }) {}

        PathtracerHostView(const PathtracerHostView& other)                = delete;
        PathtracerHostView(PathtracerHostView&& other) noexcept            = default;
        PathtracerHostView& operator=(const PathtracerHostView& other)     = delete;
        PathtracerHostView& operator=(PathtracerHostView&& other) noexcept = default;
        ~PathtracerHostView() noexcept                                     = default;

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

        void register_panel(PathtracerPanel panel) {
            this->registerPanelCallback(std::move(panel));
        }

        void set_window_detail(std::string detail) {
            this->setWindowDetailCallback(std::move(detail));
        }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physicalDeviceCallback{};
        std::move_only_function<const vk::raii::Device&()> deviceCallback{};
        std::move_only_function<std::uint32_t()> frameCountCallback{};
        std::move_only_function<vk::Extent2D()> swapchainExtentCallback{};
        std::move_only_function<void(PathtracerPanel)> registerPanelCallback{};
        std::move_only_function<void(std::string)> setWindowDetailCallback{};
    };

    class SpectraPathtracer final {
    public:
        explicit SpectraPathtracer(std::string scene_name);
        ~SpectraPathtracer() noexcept;

        SpectraPathtracer(const SpectraPathtracer& other) = delete;
        SpectraPathtracer(SpectraPathtracer&& other) noexcept;
        SpectraPathtracer& operator=(const SpectraPathtracer& other) = delete;
        SpectraPathtracer& operator=(SpectraPathtracer&& other) noexcept;

        [[nodiscard]] std::string_view name() const;

        template <PathtracerHost Host>
        void attach(Host& host) {
            this->attach(PathtracerHostView{host});
        }

        template <PathtracerHost Host>
        void detach(Host&) noexcept {
            this->detach();
        }

        template <PathtracerHost Host>
        void before_imgui_shutdown(Host&) noexcept {
            this->before_imgui_shutdown();
        }

        template <PathtracerHost Host>
        void after_imgui_created(Host&) {
            this->after_imgui_created();
        }

        template <PathtracerHost Host, typename Frame>
            requires PathtracerFrameInfoLike<Frame>
        [[nodiscard]] PathtracerFrameResult begin_frame(Host& host, const Frame& frame) {
            return this->begin_frame(PathtracerHostView{host}, PathtracerFrameInfo{
                                                                   .frame_index = static_cast<std::uint32_t>(frame.frame_index),
                                                                   .image_index = static_cast<std::uint32_t>(frame.image_index),
                                                               });
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        void attach(PathtracerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] PathtracerFrameResult begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame);
    };
} // namespace xayah
