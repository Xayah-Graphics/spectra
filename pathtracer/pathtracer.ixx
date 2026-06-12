module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module spectra.pathtracer;

export import spectra.scene;

import std;

extern "C++" {
    namespace pstd::pmr {
        class memory_resource;
    }

    namespace spectra::pathtracer {
        class CompiledPathtracerScene;
        struct RenderConfig;
    } // namespace spectra::pathtracer
}

export namespace spectra::pathtracer {
    enum class PathtracerDockSlot {
        Center = 0,
        Floating = 1,
    };

    struct PathtracerPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        PathtracerDockSlot dock_slot{PathtracerDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct PathtracerSidebarTab {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    struct PathtracerToolbarAction {
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

    struct PathtracerFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct PathtracerFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Host>
    concept PathtracerHost = requires(Host& host, PathtracerPanel panel, PathtracerSidebarTab tab, PathtracerToolbarAction action) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.register_sidebar_tab(std::move(tab)) } -> std::same_as<void>;
        { host.register_toolbar_action(std::move(action)) } -> std::same_as<void>;
    };

    class PathtracerHostView {
    public:
        template <PathtracerHost Host>
        explicit PathtracerHostView(Host& host) : physicalDeviceCallback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }), deviceCallback([&host]() -> const vk::raii::Device& { return host.device(); }), frameCountCallback([&host]() -> std::uint32_t { return host.frame_count(); }), swapchainExtentCallback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }), registerPanelCallback([&host](PathtracerPanel panel) { host.register_panel(std::move(panel)); }), registerSidebarTabCallback([&host](PathtracerSidebarTab tab) { host.register_sidebar_tab(std::move(tab)); }), registerToolbarActionCallback([&host](PathtracerToolbarAction action) { host.register_toolbar_action(std::move(action)); }) {}

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

        void register_sidebar_tab(PathtracerSidebarTab tab) {
            this->registerSidebarTabCallback(std::move(tab));
        }

        void register_toolbar_action(PathtracerToolbarAction action) {
            this->registerToolbarActionCallback(std::move(action));
        }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physicalDeviceCallback{};
        std::move_only_function<const vk::raii::Device&()> deviceCallback{};
        std::move_only_function<std::uint32_t()> frameCountCallback{};
        std::move_only_function<vk::Extent2D()> swapchainExtentCallback{};
        std::move_only_function<void(PathtracerPanel)> registerPanelCallback{};
        std::move_only_function<void(PathtracerSidebarTab)> registerSidebarTabCallback{};
        std::move_only_function<void(PathtracerToolbarAction)> registerToolbarActionCallback{};
    };

    struct PathtracerSceneSupportReport {
        std::string target{};
        bool supported{true};
        std::vector<scene::Scene::Diagnostic> diagnostics{};
    };

    [[nodiscard]] PathtracerSceneSupportReport AnalyzePathtracerSceneSupport(const scene::Scene::ResolvedScene& scene);
    [[nodiscard]] std::unique_ptr<CompiledPathtracerScene> CompilePathtracerScene(const scene::Scene::ResolvedScene& scene, const RenderConfig& config, pstd::pmr::memory_resource* memoryResource);

    class PathtracerRenderer final {
    public:
        PathtracerRenderer(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace);
        ~PathtracerRenderer() noexcept;

        PathtracerRenderer(const PathtracerRenderer& other) = delete;
        PathtracerRenderer(PathtracerRenderer&& other) noexcept;
        PathtracerRenderer& operator=(const PathtracerRenderer& other) = delete;
        PathtracerRenderer& operator=(PathtracerRenderer&& other) noexcept;

        [[nodiscard]] static std::string_view name();
        void set_scene_workspace(std::shared_ptr<const scene::Scene> source_scene, std::shared_ptr<scene::Scene::CameraWorkspace> camera_workspace);
        void attach(PathtracerHostView host);
        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] PathtracerFrameResult begin_frame(PathtracerHostView host, const PathtracerFrameInfo& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);

    private:
        class Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace spectra::pathtracer
