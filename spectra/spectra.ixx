module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <vulkan/vulkan_raii.hpp>

export module spectra;

import std;

namespace spectra {
    export enum class DockSlot : std::uint8_t {
        Center,
        Floating,
    };

    export struct Panel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        DockSlot dock_slot{DockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    export struct SidebarTab {
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
        std::move_only_function<bool()> active{};
        std::move_only_function<void()> trigger{};
    };

    export struct FrameContext {
        std::uint32_t frame_slot_index{};
        std::uint32_t image_index{};
        std::uint64_t frame_number{};
        double delta_seconds{};
    };

    export struct FrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    export template <typename PanelContribution>
    concept PanelLike = requires(PanelContribution panel) {
        std::string{std::move(panel.id)};
        std::string{std::move(panel.title)};
        std::string{std::move(panel.icon)};
        std::string{std::move(panel.owner_renderer)};
        std::string{std::move(panel.shortcut_label)};
        static_cast<ImGuiKey>(panel.shortcut_key);
        static_cast<std::underlying_type_t<DockSlot>>(panel.dock_slot);
        static_cast<ImGuiWindowFlags>(panel.window_flags);
        { panel.visible } -> std::convertible_to<bool>;
        { panel.closable } -> std::convertible_to<bool>;
        { panel.zero_window_padding } -> std::convertible_to<bool>;
        std::move_only_function<void()>{std::move(panel.draw)};
    };

    export template <typename SidebarTabContribution>
    concept SidebarTabLike = requires(SidebarTabContribution tab) {
        std::string{std::move(tab.id)};
        std::string{std::move(tab.title)};
        std::string{std::move(tab.icon)};
        std::string{std::move(tab.owner_renderer)};
        std::string{std::move(tab.shortcut_label)};
        static_cast<ImGuiKey>(tab.shortcut_key);
        std::move_only_function<void()>{std::move(tab.draw)};
    };

    export template <typename ToolbarActionContribution>
    concept ToolbarActionLike = requires(ToolbarActionContribution action) {
        std::string{std::move(action.id)};
        std::string{std::move(action.title)};
        std::string{std::move(action.icon)};
        std::string{std::move(action.owner_renderer)};
        std::string{std::move(action.shortcut_label)};
        static_cast<ImGuiKey>(action.shortcut_key);
        std::move_only_function<bool()>{std::move(action.active)};
        std::move_only_function<void()>{std::move(action.trigger)};
    };

    export template <typename FrameResultContribution>
    concept FrameResultLike = requires(FrameResultContribution result) {
        std::optional<vk::Semaphore>{std::move(result.completion_semaphore)};
        { result.close_requested } -> std::convertible_to<bool>;
        std::optional<std::string>{std::move(result.window_detail)};
    };

    export template <typename Renderer, typename Host>
    concept RendererFor = std::movable<std::remove_cvref_t<Renderer>> && requires(std::remove_cvref_t<Renderer>& renderer, Host& host, const FrameContext& frame, const vk::raii::CommandBuffer& commandBuffer) {
        { std::remove_cvref_t<Renderer>::name() } -> std::convertible_to<std::string_view>;
        { renderer.attach(host) } -> std::same_as<void>;
        { renderer.detach() } noexcept -> std::same_as<void>;
        { renderer.before_imgui_shutdown() } noexcept -> std::same_as<void>;
        { renderer.after_imgui_created() } -> std::same_as<void>;
        { renderer.begin_frame(host, frame) } -> FrameResultLike;
        { renderer.record_frame(commandBuffer) } -> std::same_as<void>;
    };

    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

        void run();

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const;
        [[nodiscard]] const vk::raii::Device& device() const;
        [[nodiscard]] std::uint32_t frame_count() const;
        [[nodiscard]] vk::Extent2D swapchain_extent() const;

        template <typename Renderer>
            requires RendererFor<Renderer, Spectra>
        void register_renderer(Renderer renderer);
        template <typename PanelContribution>
            requires PanelLike<PanelContribution>
        void register_panel(PanelContribution panel);
        template <typename SidebarTabContribution>
            requires SidebarTabLike<SidebarTabContribution>
        void register_sidebar_tab(SidebarTabContribution tab);
        template <typename ToolbarActionContribution>
            requires ToolbarActionLike<ToolbarActionContribution>
        void register_toolbar_action(ToolbarActionContribution action);

    protected:
        struct FrameState;

        void initialize_glfw();
        void create_vulkan_instance(std::string_view app_name, std::string_view engine_name);
        void create_debug_messenger();
        void create_window(std::string_view app_name, std::uint32_t window_width, std::uint32_t window_height);
        void create_surface();
        void validate_initial_framebuffer_extent();
        void select_physical_device();
        void create_logical_device();
        void create_command_pool();
        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void create_frame_sync();
        void create_imgui();

        bool begin_frame(FrameState& frame);
        void record_frame(FrameState& frame);
        void end_frame(FrameState& frame);

        void recreate_swapchain();

        void shutdown_runtime() noexcept;
        void detach_renderers() noexcept;
        void notify_renderers_before_imgui_shutdown() noexcept;
        void wait_device_idle_for_cleanup() noexcept;
        void destroy_imgui() noexcept;
        void destroy_frame_sync() noexcept;
        void destroy_swapchain() noexcept;
        void destroy_surface_and_window() noexcept;
        void destroy_vulkan_context() noexcept;
        void terminate_glfw() noexcept;

    private:
        struct RendererSlot {
            template <typename Renderer>
                requires RendererFor<Renderer, Spectra>
            explicit RendererSlot(Renderer renderer) {
                auto instance               = std::make_shared<Renderer>(std::move(renderer));
                this->name                  = std::string{std::remove_cvref_t<Renderer>::name()};
                this->attach                = [instance](Spectra& spectra) { instance->attach(spectra); };
                this->detach                = [instance]() noexcept { instance->detach(); };
                this->before_imgui_shutdown = [instance]() noexcept { instance->before_imgui_shutdown(); };
                this->after_imgui_created   = [instance]() { instance->after_imgui_created(); };
                this->begin_frame           = [instance](Spectra& spectra, const FrameContext& frame) {
                    auto result = instance->begin_frame(spectra, frame);
                    return FrameResult{
                                  .completion_semaphore = std::optional<vk::Semaphore>{std::move(result.completion_semaphore)},
                                  .close_requested      = static_cast<bool>(result.close_requested),
                                  .window_detail        = std::optional<std::string>{std::move(result.window_detail)},
                    };
                };
                this->record_frame = [instance](const vk::raii::CommandBuffer& command_buffer) { instance->record_frame(command_buffer); };
            }

            std::string name{};
            std::move_only_function<void(Spectra&)> attach{};
            std::move_only_function<void() noexcept> detach{};
            std::move_only_function<void() noexcept> before_imgui_shutdown{};
            std::move_only_function<void()> after_imgui_created{};
            std::move_only_function<FrameResult(Spectra&, const FrameContext&)> begin_frame{};
            std::move_only_function<void(const vk::raii::CommandBuffer&)> record_frame{};
        };

        void store_renderer(RendererSlot renderer);
        void store_panel(Panel panel);
        void store_sidebar_tab(SidebarTab tab);
        void store_toolbar_action(ToolbarAction action);

        [[nodiscard]] std::string resolve_contribution_owner(std::string owner_renderer) const;
        [[nodiscard]] bool contribution_belongs_to_active_renderer(std::string_view owner_renderer) const;
        void sync_active_sidebar_tab();
        void activate_renderer(std::size_t renderer_index);

        void draw_command_bar();
        void draw_dockspace();
        void draw_sidebar();
        void draw_registered_panels();
        void update_window_title(float delta_seconds);

        struct {
            vk::raii::Context context{};
            vk::raii::Instance instance{nullptr};
            vk::raii::DebugUtilsMessengerEXT debug_messenger{nullptr};
            vk::raii::PhysicalDevice physical_device{nullptr};
            vk::raii::Device device{nullptr};
            vk::raii::Queue graphics_queue{nullptr};
            std::uint32_t graphics_queue_index{0};
            vk::raii::CommandPool command_pool{nullptr};
        } context;

        struct {
            std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> window{nullptr, glfwDestroyWindow};
            vk::raii::SurfaceKHR surface{nullptr};
            bool resize_requested{false};
            bool glfw_initialized{false};
        } surface;

        struct {
            vk::raii::SwapchainKHR handle{nullptr};
            std::vector<vk::Image> images{};
            std::vector<vk::raii::ImageView> image_views{};
            std::vector<vk::ImageLayout> image_layouts{};
            vk::Format format{vk::Format::eUndefined};
            vk::ColorSpaceKHR color_space{vk::ColorSpaceKHR::eSrgbNonlinear};
            vk::PresentModeKHR present_mode{vk::PresentModeKHR::eFifo};
            vk::Extent2D extent{};
        } swapchain;

        struct {
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            bool initialized{false};
            bool renderers_notified_before_shutdown{false};
        } imgui;

        struct {
            std::uint32_t frame_count{2};
            std::uint32_t frame_slot_index{0};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Semaphore> image_available_semaphores{};
            std::vector<vk::raii::Semaphore> render_finished_semaphores{};
            std::vector<std::uint32_t> image_in_flight_frame{};
            std::vector<vk::raii::Fence> in_flight_fences{};
        } sync;

        struct {
            std::string base{};
            std::string detail{};
            std::uint64_t frame_count{0};
            float refresh_timer{0.0f};
        } window_title;

        struct {
            std::uint64_t frame_number{0};
            std::chrono::steady_clock::time_point last_frame_time{};
            bool last_frame_time_valid{false};
        } timing;

        struct {
            std::vector<RendererSlot> slots{};
            std::optional<std::string> registering_name{};
            std::size_t active_index{0};
        } renderer_registry;

        struct {
            std::vector<Panel> panels{};
            std::vector<SidebarTab> sidebar_tabs{};
            std::vector<ToolbarAction> toolbar_actions{};
            bool dock_layout_initialized{false};
            bool sidebar_visible{true};
            std::string active_sidebar_tab_id{};
            bool sidebar_tab_selection_requested{false};
        } workspace;
    };

    template <typename Renderer>
        requires RendererFor<Renderer, Spectra>
    void Spectra::register_renderer(Renderer renderer) {
        this->store_renderer(RendererSlot{std::move(renderer)});
    }

    template <typename PanelContribution>
        requires PanelLike<PanelContribution>
    void Spectra::register_panel(PanelContribution panel) {
        this->store_panel(Panel{
            .id                  = std::string{std::move(panel.id)},
            .title               = std::string{std::move(panel.title)},
            .icon                = std::string{std::move(panel.icon)},
            .owner_renderer      = this->resolve_contribution_owner(std::string{std::move(panel.owner_renderer)}),
            .shortcut_label      = std::string{std::move(panel.shortcut_label)},
            .shortcut_key        = static_cast<ImGuiKey>(panel.shortcut_key),
            .dock_slot           = static_cast<DockSlot>(static_cast<std::underlying_type_t<DockSlot>>(panel.dock_slot)),
            .window_flags        = static_cast<ImGuiWindowFlags>(panel.window_flags),
            .visible             = static_cast<bool>(panel.visible),
            .closable            = static_cast<bool>(panel.closable),
            .zero_window_padding = static_cast<bool>(panel.zero_window_padding),
            .draw                = std::move(panel.draw),
        });
    }

    template <typename SidebarTabContribution>
        requires SidebarTabLike<SidebarTabContribution>
    void Spectra::register_sidebar_tab(SidebarTabContribution tab) {
        this->store_sidebar_tab(SidebarTab{
            .id             = std::string{std::move(tab.id)},
            .title          = std::string{std::move(tab.title)},
            .icon           = std::string{std::move(tab.icon)},
            .owner_renderer = this->resolve_contribution_owner(std::string{std::move(tab.owner_renderer)}),
            .shortcut_label = std::string{std::move(tab.shortcut_label)},
            .shortcut_key   = static_cast<ImGuiKey>(tab.shortcut_key),
            .draw           = std::move(tab.draw),
        });
    }

    template <typename ToolbarActionContribution>
        requires ToolbarActionLike<ToolbarActionContribution>
    void Spectra::register_toolbar_action(ToolbarActionContribution action) {
        this->store_toolbar_action(ToolbarAction{
            .id             = std::string{std::move(action.id)},
            .title          = std::string{std::move(action.title)},
            .icon           = std::string{std::move(action.icon)},
            .owner_renderer = this->resolve_contribution_owner(std::string{std::move(action.owner_renderer)}),
            .shortcut_label = std::string{std::move(action.shortcut_label)},
            .shortcut_key   = static_cast<ImGuiKey>(action.shortcut_key),
            .active         = std::move(action.active),
            .trigger        = std::move(action.trigger),
        });
    }
} // namespace spectra
