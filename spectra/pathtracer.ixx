module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vulkan/vulkan_raii.hpp>

export module spectra.pathtracer;

import std;
export import spectra.scene;
export import spectra.contract;

export namespace spectra::pathtracer {
    template <typename Host>
    concept PathtracerHost = requires(Host& host, SpectraPanel panel, std::string detail) {
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
        explicit PathtracerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](SpectraPanel panel) { host.register_panel(std::move(panel)); }), setWindowDetailCallback([&host](std::string detail) { host.set_window_detail(std::move(detail)); }) {}

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

        void register_panel(SpectraPanel panel) {
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
        std::move_only_function<void(SpectraPanel)> registerPanelCallback{};
        std::move_only_function<void(std::string)> setWindowDetailCallback{};
    };

    class PathtracerRenderer final {
    public:
        explicit PathtracerRenderer(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace);
        ~PathtracerRenderer() noexcept;

        PathtracerRenderer(const PathtracerRenderer& other) = delete;
        PathtracerRenderer(PathtracerRenderer&& other) noexcept;
        PathtracerRenderer& operator=(const PathtracerRenderer& other) = delete;
        PathtracerRenderer& operator=(PathtracerRenderer&& other) noexcept;

        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] static spectra::scene::SceneTranslationTarget translation_target();
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
            requires SpectraFrameInfoLike<Frame>
        [[nodiscard]] SpectraFrameResult begin_frame(Host& host, const Frame& frame) {
            return this->begin_frame(PathtracerHostView{host}, SpectraFrameInfo{
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
        [[nodiscard]] SpectraFrameResult begin_frame(PathtracerHostView host, const SpectraFrameInfo& frame);
    };
} // namespace spectra::pathtracer
