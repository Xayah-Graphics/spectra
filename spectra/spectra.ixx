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

export import spectra.contract;
import std;

export namespace spectra {
    class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

        template <typename Renderer>
            requires SpectraRendererForHost<Renderer, Spectra>
        void register_renderer(Renderer renderer);
        void run();

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const;
        [[nodiscard]] const vk::raii::Device& device() const;
        [[nodiscard]] std::uint32_t frame_count() const;
        [[nodiscard]] vk::Extent2D swapchain_extent() const;
        void activate_renderer(std::size_t renderer_index);
        template <typename Panel>
            requires SpectraPanelLike<Panel>
        void register_panel(Panel panel);
        void set_window_detail(std::string detail);
        void set_renderer_availability_callback(std::move_only_function<SpectraRendererAvailability(std::string_view)> callback);
        void set_renderer_activation_callback(std::move_only_function<void(std::string_view)> callback);

    private:
        struct FrameState;
        struct RegisteredRenderer {
            template <typename Renderer>
                requires SpectraRendererForHost<Renderer, Spectra>
            explicit RegisteredRenderer(Renderer renderer) {
                auto instance               = std::make_shared<Renderer>(std::move(renderer));
                this->name                  = std::string{instance->name()};
                this->attach                = [instance](Spectra& spectra) { instance->attach(spectra); };
                this->detach                = [instance](Spectra& spectra) { instance->detach(spectra); };
                this->before_imgui_shutdown = [instance](Spectra& spectra) { instance->before_imgui_shutdown(spectra); };
                this->after_imgui_created   = [instance](Spectra& spectra) { instance->after_imgui_created(spectra); };
                this->begin_frame           = [instance](Spectra& spectra, const SpectraFrameInfo& frame) {
                    auto result = instance->begin_frame(spectra, frame);
                    return SpectraFrameResult{
                        .completion_semaphore = std::optional<vk::Semaphore>{std::move(result.completion_semaphore)},
                        .close_requested      = static_cast<bool>(result.close_requested),
                        .window_detail        = std::optional<std::string>{std::move(result.window_detail)},
                    };
                };
                this->record_frame = [instance](const vk::raii::CommandBuffer& command_buffer) { instance->record_frame(command_buffer); };
            }

            std::string name{};
            std::move_only_function<void(Spectra&)> attach{};
            std::move_only_function<void(Spectra&)> detach{};
            std::move_only_function<void(Spectra&)> before_imgui_shutdown{};
            std::move_only_function<void(Spectra&)> after_imgui_created{};
            std::move_only_function<SpectraFrameResult(Spectra&, const SpectraFrameInfo&)> begin_frame{};
            std::move_only_function<void(const vk::raii::CommandBuffer&)> record_frame{};
        };

        void register_renderer(RegisteredRenderer renderer);
        void store_panel(SpectraPanel panel);

        void create_imgui();
        void notify_renderers_before_imgui_shutdown() noexcept;
        void wait_device_idle_noexcept() noexcept;
        void destroy_imgui() noexcept;
        void detach_renderers_noexcept() noexcept;

        bool begin_frame(FrameState& frame);
        void record_frame(FrameState& frame);
        void end_frame(FrameState& frame);

        void draw_main_menu();
        void draw_menu_toolbar();
        void draw_dockspace();
        void draw_registered_panels();
        void update_window_title(float delta_seconds);
        [[nodiscard]] SpectraRendererAvailability renderer_availability(std::string_view renderer_name);

        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain();

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
            std::shared_ptr<GLFWwindow> window{};
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
        } imgui;

        struct {
            std::uint32_t frame_count{2};
            std::uint32_t frame_index{0};
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

        bool dock_layout_initialized{false};
        bool imgui_shutdown_notified{false};
        std::size_t active_renderer_index{0};
        std::vector<SpectraPanel> panels{};
        std::vector<RegisteredRenderer> renderers{};
        std::move_only_function<SpectraRendererAvailability(std::string_view)> renderer_availability_callback{};
        std::move_only_function<void(std::string_view)> renderer_activation_callback{};
    };

    template <typename Panel>
        requires SpectraPanelLike<Panel>
    void Spectra::register_panel(Panel panel) {
        this->store_panel(SpectraPanel{
            .id                  = std::string{std::move(panel.id)},
            .title               = std::string{std::move(panel.title)},
            .icon                = std::string{std::move(panel.icon)},
            .shortcut_label      = std::string{std::move(panel.shortcut_label)},
            .shortcut_key        = static_cast<ImGuiKey>(panel.shortcut_key),
            .dock_slot           = static_cast<SpectraDockSlot>(static_cast<std::underlying_type_t<SpectraDockSlot>>(panel.dock_slot)),
            .window_flags        = static_cast<ImGuiWindowFlags>(panel.window_flags),
            .visible             = static_cast<bool>(panel.visible),
            .closable            = static_cast<bool>(panel.closable),
            .show_in_menu        = static_cast<bool>(panel.show_in_menu),
            .show_in_toolbar     = static_cast<bool>(panel.show_in_toolbar),
            .zero_window_padding = static_cast<bool>(panel.zero_window_padding),
            .draw                = std::move(panel.draw),
        });
    }

    template <typename Renderer>
        requires SpectraRendererForHost<Renderer, Spectra>
    void Spectra::register_renderer(Renderer renderer) {
        this->register_renderer(RegisteredRenderer{std::move(renderer)});
    }
} // namespace spectra
