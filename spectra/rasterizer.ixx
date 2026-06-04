module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module xayah.spectra.rasterizer;

import std;
export import spectra.scene;

export namespace xayah {
    enum class RasterizerDockSlot {
        Center,
        Left,
        LeftBottom,
        Right,
        RightBottom,
        Bottom,
        Floating,
    };

    struct RasterizerPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        RasterizerDockSlot dock_slot{RasterizerDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool show_in_menu{true};
        bool show_in_toolbar{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct RasterizerFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct RasterizerFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Frame>
    concept RasterizerFrameInfoLike = requires(const Frame& frame) {
        { frame.frame_index } -> std::convertible_to<std::uint32_t>;
        { frame.image_index } -> std::convertible_to<std::uint32_t>;
    };

    template <typename Host>
    concept RasterizerHost = requires(Host& host, RasterizerPanel panel, std::string detail) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.set_window_detail(std::move(detail)) } -> std::same_as<void>;
    };

    class RasterizerHostView {
    public:
        template <RasterizerHost Host>
        explicit RasterizerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](RasterizerPanel panel) { host.register_panel(std::move(panel)); }), setWindowDetailCallback([&host](std::string detail) { host.set_window_detail(std::move(detail)); }) {}

        RasterizerHostView(const RasterizerHostView& other)                = delete;
        RasterizerHostView(RasterizerHostView&& other) noexcept            = default;
        RasterizerHostView& operator=(const RasterizerHostView& other)     = delete;
        RasterizerHostView& operator=(RasterizerHostView&& other) noexcept = default;
        ~RasterizerHostView() noexcept                                     = default;

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

        void register_panel(RasterizerPanel panel) {
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
        std::move_only_function<void(RasterizerPanel)> registerPanelCallback{};
        std::move_only_function<void(std::string)> setWindowDetailCallback{};
    };

    class SpectraRasterizer final {
    public:
        explicit SpectraRasterizer(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace);
        ~SpectraRasterizer() noexcept;

        SpectraRasterizer(const SpectraRasterizer& other) = delete;
        SpectraRasterizer(SpectraRasterizer&& other) noexcept;
        SpectraRasterizer& operator=(const SpectraRasterizer& other) = delete;
        SpectraRasterizer& operator=(SpectraRasterizer&& other) noexcept;

        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] static spectra::scene::SceneTranslationTarget translation_target();
        [[nodiscard]] std::string_view name() const;

        template <RasterizerHost Host>
        void attach(Host& host) {
            this->attach(RasterizerHostView{host});
        }

        template <RasterizerHost Host>
        void detach(Host&) noexcept {
            this->detach();
        }

        template <RasterizerHost Host>
        void before_imgui_shutdown(Host&) noexcept {
            this->before_imgui_shutdown();
        }

        template <RasterizerHost Host>
        void after_imgui_created(Host&) {
            this->after_imgui_created();
        }

        template <RasterizerHost Host, typename Frame>
            requires RasterizerFrameInfoLike<Frame>
        [[nodiscard]] RasterizerFrameResult begin_frame(Host& host, const Frame& frame) {
            return this->begin_frame(RasterizerHostView{host}, RasterizerFrameInfo{
                                                                   .frame_index = static_cast<std::uint32_t>(frame.frame_index),
                                                                   .image_index = static_cast<std::uint32_t>(frame.image_index),
                                                               });
        }

        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;

        void attach(RasterizerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] RasterizerFrameResult begin_frame(RasterizerHostView host, const RasterizerFrameInfo& frame);
    };
} // namespace xayah
