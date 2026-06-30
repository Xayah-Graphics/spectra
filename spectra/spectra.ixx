module;

#include <GLFW/glfw3.h>

export module spectra;

export import imgui;
export import vulkan;
import std;

namespace spectra {
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
        std::string title{};
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

    export struct WorkspaceTitle {
        std::string detail{};
        std::string tooltip{};
        std::string status_text{};
        bool status_error{};
    };

    export struct Rgba8ImageSource {
        const std::uint8_t* data{};
        std::uint64_t byte_size{};
        std::uint32_t width{};
        std::uint32_t height{};
        std::uint64_t revision{};
    };

    export struct FileDropHandler {
        std::string id{};
        std::string title{};
        std::string owner_renderer{};
        std::move_only_function<bool(std::span<const std::filesystem::path>)> handle{};
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
        std::string{std::move(overlay.title)};
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

    export template <typename FileDropHandlerContribution>
    concept FileDropHandlerLike = requires(FileDropHandlerContribution handler) {
        std::string{std::move(handler.id)};
        std::string{std::move(handler.title)};
        std::string{std::move(handler.owner_renderer)};
        std::move_only_function<bool(std::span<const std::filesystem::path>)>{std::move(handler.handle)};
    };

    export template <typename FrameResultContribution>
    concept FrameResultLike = requires(FrameResultContribution result) {
        std::optional<vk::Semaphore>{std::move(result.completion_semaphore)};
        { result.close_requested } -> std::convertible_to<bool>;
        std::optional<std::string>{std::move(result.window_detail)};
    };

    export template <typename Renderer, typename Host>
    concept RendererFor = requires(std::remove_cvref_t<Renderer>& renderer, Host& host, const FrameContext& frame, const vk::raii::CommandBuffer& commandBuffer) {
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
            requires std::movable<std::remove_cvref_t<Renderer>> && RendererFor<Renderer, Spectra>
        void register_renderer(Renderer renderer);
        template <typename Renderer>
            requires RendererFor<Renderer, Spectra>
        void register_renderer(std::shared_ptr<Renderer> renderer);
        template <typename PanelContribution>
            requires PanelLike<PanelContribution>
        void register_panel(PanelContribution panel);
        template <typename CommandPopoverContribution>
            requires CommandPopoverLike<CommandPopoverContribution>
        void register_command_popover(CommandPopoverContribution popover);
        template <typename ViewportOverlayContribution>
            requires ViewportOverlayLike<ViewportOverlayContribution>
        void register_viewport_overlay(ViewportOverlayContribution overlay);
        template <typename ToolbarActionContribution>
            requires ToolbarActionLike<ToolbarActionContribution>
        void register_toolbar_action(ToolbarActionContribution action);
        void open_command_popover(std::string id);
        void close_command_popover(const std::string& id);
        void draw_viewport_overlays(ImVec2 viewport_position, ImVec2 viewport_size);
        void draw_imgui_rgba8_image(std::string_view cache_key, const Rgba8ImageSource& source, ImVec2 display_size);
        void clear_imgui_rgba8_images(std::string_view cache_key_prefix = {});
        void set_workspace_title_provider(std::move_only_function<WorkspaceTitle()> provider);
        template <typename FileDropHandlerContribution>
            requires FileDropHandlerLike<FileDropHandlerContribution>
        void register_file_drop_handler(FileDropHandlerContribution handler);

    protected:
        struct FrameState;

        void initialize_glfw();
        void create_vulkan_instance(std::string_view app_name, std::string_view engine_name);
        void create_debug_messenger();
        void create_window(std::string_view app_name, std::uint32_t window_width, std::uint32_t window_height);
        void create_surface();
        void validate_initial_framebuffer_extent() const;
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
        void wait_device_idle_for_cleanup() const noexcept;
        void destroy_imgui() noexcept;
        void destroy_frame_sync() noexcept;
        void destroy_swapchain() noexcept;
        void destroy_surface_and_window() noexcept;
        void destroy_vulkan_context() noexcept;
        void terminate_glfw() noexcept;

    private:
        struct RendererSlot {
            template <typename Renderer>
                requires std::movable<std::remove_cvref_t<Renderer>> && RendererFor<Renderer, Spectra>
            explicit RendererSlot(Renderer renderer) : RendererSlot(std::make_shared<std::remove_cvref_t<Renderer>>(std::move(renderer))) {}

            template <typename Renderer>
                requires RendererFor<Renderer, Spectra>
            explicit RendererSlot(std::shared_ptr<Renderer> instance) {
                if (instance == nullptr) throw std::runtime_error("Spectra renderer instance must not be null");
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
        void store_command_popover(CommandPopover popover);
        void store_viewport_overlay(ViewportOverlay overlay);
        void store_toolbar_action(ToolbarAction action);
        void store_file_drop_handler(FileDropHandler handler);

        [[nodiscard]] std::string resolve_contribution_owner(std::string owner_renderer) const;
        [[nodiscard]] bool contribution_belongs_to_active_renderer(std::string_view owner_renderer) const;
        void sync_active_command_popover();
        void activate_renderer(std::size_t renderer_index);

        void queue_file_drop(int path_count, const char** paths) noexcept;
        void dispatch_file_drops();
        void process_command_bar_shortcuts();
        void draw_command_bar();
        void draw_dockspace();
        void draw_command_popover();
        void draw_registered_panels();
        void update_window_title(float delta_seconds);

        struct ImGuiRgba8TextureSource {
            std::uintptr_t data{};
            std::uint64_t byte_size{};
            std::uint32_t width{};
            std::uint32_t height{};
            std::uint64_t revision{};

            friend auto operator<=>(const ImGuiRgba8TextureSource&, const ImGuiRgba8TextureSource&) = default;
        };

        struct ImGuiRgba8Texture {
            ImGuiRgba8TextureSource source{};
            vk::raii::Image image{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::raii::ImageView view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            ImTextureID descriptor{};
            vk::ImageLayout layout{vk::ImageLayout::eUndefined};
        };

        void destroy_imgui_rgba8_texture(ImGuiRgba8Texture& texture) const noexcept;
        void destroy_imgui_rgba8_textures() noexcept;
        [[nodiscard]] ImGuiRgba8Texture& ensure_imgui_rgba8_texture(std::string_view cache_key, const Rgba8ImageSource& source);
        void upload_imgui_rgba8_texture(ImGuiRgba8Texture& texture, const std::uint8_t* data) const;

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
            std::map<std::string, ImGuiRgba8Texture> rgba8_textures{};
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
            std::vector<FileDropHandler> handlers{};
            std::vector<std::vector<std::filesystem::path>> pending_batches{};
        } file_drop;

        struct {
            std::vector<RendererSlot> slots{};
            std::optional<std::string> registering_name{};
            std::size_t active_index{0};
        } renderer_registry;

        struct {
            std::vector<Panel> panels{};
            std::vector<CommandPopover> command_popovers{};
            std::vector<ViewportOverlay> viewport_overlays{};
            std::vector<ToolbarAction> toolbar_actions{};
            std::move_only_function<WorkspaceTitle()> title_provider{};
            bool dock_layout_initialized{false};
            bool command_popover_open{false};
            std::string active_command_popover_id{};
        } workspace;
    };

    template <typename Renderer>
        requires std::movable<std::remove_cvref_t<Renderer>> && RendererFor<Renderer, Spectra>
    void Spectra::register_renderer(Renderer renderer) {
        this->store_renderer(RendererSlot{std::move(renderer)});
    }

    template <typename Renderer>
        requires RendererFor<Renderer, Spectra>
    void Spectra::register_renderer(std::shared_ptr<Renderer> renderer) {
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
            .window_flags        = static_cast<ImGuiWindowFlags>(panel.window_flags),
            .visible             = static_cast<bool>(panel.visible),
            .closable            = static_cast<bool>(panel.closable),
            .zero_window_padding = static_cast<bool>(panel.zero_window_padding),
            .draw                = std::move(panel.draw),
        });
    }

    template <typename CommandPopoverContribution>
        requires CommandPopoverLike<CommandPopoverContribution>
    void Spectra::register_command_popover(CommandPopoverContribution popover) {
        this->store_command_popover(CommandPopover{
            .id             = std::string{std::move(popover.id)},
            .title          = std::string{std::move(popover.title)},
            .icon           = std::string{std::move(popover.icon)},
            .owner_renderer = this->resolve_contribution_owner(std::string{std::move(popover.owner_renderer)}),
            .shortcut_label = std::string{std::move(popover.shortcut_label)},
            .shortcut_key   = static_cast<ImGuiKey>(popover.shortcut_key),
            .draw           = std::move(popover.draw),
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
            .enabled        = std::move(action.enabled),
            .active         = std::move(action.active),
            .trigger        = std::move(action.trigger),
        });
    }

    template <typename FileDropHandlerContribution>
        requires FileDropHandlerLike<FileDropHandlerContribution>
    void Spectra::register_file_drop_handler(FileDropHandlerContribution handler) {
        this->store_file_drop_handler(FileDropHandler{
            .id             = std::string{std::move(handler.id)},
            .title          = std::string{std::move(handler.title)},
            .owner_renderer = this->resolve_contribution_owner(std::string{std::move(handler.owner_renderer)}),
            .handle         = std::move(handler.handle),
        });
    }

    template <typename ViewportOverlayContribution>
        requires ViewportOverlayLike<ViewportOverlayContribution>
    void Spectra::register_viewport_overlay(ViewportOverlayContribution overlay) {
        this->store_viewport_overlay(ViewportOverlay{
            .id             = std::string{std::move(overlay.id)},
            .title          = std::string{std::move(overlay.title)},
            .owner_renderer = this->resolve_contribution_owner(std::string{std::move(overlay.owner_renderer)}),
            .priority       = static_cast<std::int32_t>(overlay.priority),
            .draw           = std::move(overlay.draw),
        });
    }

} // namespace spectra
