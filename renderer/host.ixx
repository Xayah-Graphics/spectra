module;

export module spectra.renderer.host;

export import imgui;
export import vulkan;
import std;

namespace spectra {
    export struct Panel {
        std::string id{};
        std::string title{};
        std::string owner_renderer{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    export struct CommandPopover {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string owner_renderer{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        std::move_only_function<void()> draw{};
    };

    export struct ViewportOverlay {
        std::string id{};
        std::string owner_renderer{};
        std::int32_t priority{};
        std::move_only_function<void(ImVec2, ImVec2)> draw{};
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
        std::uint64_t frame_number{};
        double delta_seconds{};
    };

    export struct FrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
    };

    export template <typename PanelContribution>
    concept PanelLike = requires(PanelContribution panel) {
        std::string{std::move(panel.id)};
        std::string{std::move(panel.title)};
        std::string{std::move(panel.owner_renderer)};
        static_cast<ImGuiKey>(panel.shortcut_key);
        static_cast<ImGuiWindowFlags>(panel.window_flags);
        { panel.visible } -> std::convertible_to<bool>;
        { panel.closable } -> std::convertible_to<bool>;
        { panel.zero_window_padding } -> std::convertible_to<bool>;
        std::move_only_function<void()>{std::move(panel.draw)};
    };

    export template <typename CommandPopoverContribution>
    concept CommandPopoverLike = requires(CommandPopoverContribution popover) {
        std::string{std::move(popover.id)};
        std::string{std::move(popover.title)};
        std::string{std::move(popover.icon)};
        std::string{std::move(popover.owner_renderer)};
        std::string{std::move(popover.shortcut_label)};
        static_cast<ImGuiKey>(popover.shortcut_key);
        std::move_only_function<void()>{std::move(popover.draw)};
    };

    export template <typename ViewportOverlayContribution>
    concept ViewportOverlayLike = requires(ViewportOverlayContribution overlay) {
        std::string{std::move(overlay.id)};
        std::string{std::move(overlay.owner_renderer)};
        static_cast<std::int32_t>(overlay.priority);
        std::move_only_function<void(ImVec2, ImVec2)>{std::move(overlay.draw)};
    };

    export template <typename ToolbarActionContribution>
    concept ToolbarActionLike = requires(ToolbarActionContribution action) {
        std::string{std::move(action.id)};
        std::string{std::move(action.title)};
        std::string{std::move(action.icon)};
        std::string{std::move(action.owner_renderer)};
        std::string{std::move(action.shortcut_label)};
        static_cast<ImGuiKey>(action.shortcut_key);
        std::move_only_function<bool()>{std::move(action.enabled)};
        std::move_only_function<bool()>{std::move(action.active)};
        std::move_only_function<void()>{std::move(action.trigger)};
    };

    export template <typename FrameResultContribution>
    concept FrameResultLike = requires(FrameResultContribution result) {
        std::optional<vk::Semaphore>{std::move(result.completion_semaphore)};
        { result.close_requested } -> std::convertible_to<bool>;
    };

    export template <typename HostType>
    concept Host = requires(HostType& host, Panel panel, CommandPopover popover, ViewportOverlay overlay, ToolbarAction action, ImVec2 viewport_position, ImVec2 viewport_size) {
        { host.physical_device() } -> std::same_as<const vk::raii::PhysicalDevice&>;
        { host.device() } -> std::same_as<const vk::raii::Device&>;
        { host.frame_count() } -> std::same_as<std::uint32_t>;
        { host.swapchain_extent() } -> std::same_as<vk::Extent2D>;
        { host.register_panel(std::move(panel)) } -> std::same_as<void>;
        { host.register_command_popover(std::move(popover)) } -> std::same_as<void>;
        { host.register_viewport_overlay(std::move(overlay)) } -> std::same_as<void>;
        { host.register_toolbar_action(std::move(action)) } -> std::same_as<void>;
        { host.draw_viewport_overlays(viewport_position, viewport_size) } -> std::same_as<void>;
    };

    export class HostView {
    public:
        template <Host HostType>
        explicit HostView(HostType& host)
            : physical_device_callback([&host]() -> const vk::raii::PhysicalDevice& { return host.physical_device(); }),
              device_callback([&host]() -> const vk::raii::Device& { return host.device(); }),
              frame_count_callback([&host]() -> std::uint32_t { return host.frame_count(); }),
              swapchain_extent_callback([&host]() -> vk::Extent2D { return host.swapchain_extent(); }),
              register_panel_callback([&host](Panel panel) { host.register_panel(std::move(panel)); }),
              register_command_popover_callback([&host](CommandPopover popover) { host.register_command_popover(std::move(popover)); }),
              register_viewport_overlay_callback([&host](ViewportOverlay overlay) { host.register_viewport_overlay(std::move(overlay)); }),
              register_toolbar_action_callback([&host](ToolbarAction action) { host.register_toolbar_action(std::move(action)); }),
              draw_viewport_overlays_callback([&host](const ImVec2 position, const ImVec2 size) { host.draw_viewport_overlays(position, size); }) {}

        HostView(const HostView& other)                = delete;
        HostView(HostView&& other) noexcept            = default;
        HostView& operator=(const HostView& other)     = delete;
        HostView& operator=(HostView&& other) noexcept = default;
        ~HostView() noexcept                           = default;

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() { return this->physical_device_callback(); }
        [[nodiscard]] const vk::raii::Device& device() { return this->device_callback(); }
        [[nodiscard]] std::uint32_t frame_count() { return this->frame_count_callback(); }
        [[nodiscard]] vk::Extent2D swapchain_extent() { return this->swapchain_extent_callback(); }
        void register_panel(Panel panel) { this->register_panel_callback(std::move(panel)); }
        void register_command_popover(CommandPopover popover) { this->register_command_popover_callback(std::move(popover)); }
        void register_viewport_overlay(ViewportOverlay overlay) { this->register_viewport_overlay_callback(std::move(overlay)); }
        void register_toolbar_action(ToolbarAction action) { this->register_toolbar_action_callback(std::move(action)); }
        [[nodiscard]] std::move_only_function<void(ImVec2, ImVec2)> take_viewport_overlay_draw_callback() { return std::move(this->draw_viewport_overlays_callback); }

    private:
        std::move_only_function<const vk::raii::PhysicalDevice&()> physical_device_callback{};
        std::move_only_function<const vk::raii::Device&()> device_callback{};
        std::move_only_function<std::uint32_t()> frame_count_callback{};
        std::move_only_function<vk::Extent2D()> swapchain_extent_callback{};
        std::move_only_function<void(Panel)> register_panel_callback{};
        std::move_only_function<void(CommandPopover)> register_command_popover_callback{};
        std::move_only_function<void(ViewportOverlay)> register_viewport_overlay_callback{};
        std::move_only_function<void(ToolbarAction)> register_toolbar_action_callback{};
        std::move_only_function<void(ImVec2, ImVec2)> draw_viewport_overlays_callback{};
    };
} // namespace spectra
