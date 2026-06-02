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

export module xayah.spectra;

import std;

export namespace xayah {
    class Spectra;

    enum class SpectraDockSlot {
        Center,
        Left,
        LeftBottom,
        Right,
        RightBottom,
        Bottom,
        Floating,
    };

    struct SpectraPanel {
        std::string id{};
        std::string title{};
        std::string icon{};
        std::string shortcut_label{};
        ImGuiKey shortcut_key{ImGuiKey_None};
        SpectraDockSlot dock_slot{SpectraDockSlot::Floating};
        ImGuiWindowFlags window_flags{0};
        bool visible{true};
        bool closable{true};
        bool show_in_menu{true};
        bool show_in_toolbar{true};
        bool zero_window_padding{false};
        std::move_only_function<void()> draw{};
    };

    struct SpectraFrameInfo {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct SpectraFrameResult {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    template <typename Plugin>
    concept SpectraPlugin = std::movable<std::remove_cvref_t<Plugin>> && requires(std::remove_cvref_t<Plugin>& plugin, const std::remove_cvref_t<Plugin>& const_plugin, Spectra& spectra, const SpectraFrameInfo& frame, const vk::raii::CommandBuffer& command_buffer) {
        { const_plugin.name() } -> std::convertible_to<std::string_view>;
        { plugin.attach(spectra) } -> std::same_as<void>;
        { plugin.detach(spectra) } noexcept -> std::same_as<void>;
        { plugin.before_imgui_shutdown(spectra) } noexcept -> std::same_as<void>;
        { plugin.after_imgui_created(spectra) } -> std::same_as<void>;
        { plugin.begin_frame(spectra, frame) } -> std::same_as<SpectraFrameResult>;
        { plugin.record_frame(command_buffer) } -> std::same_as<void>;
    };

    class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

        template <SpectraPlugin Plugin>
        void register_plugin(Plugin plugin);
        void run();

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const;
        [[nodiscard]] const vk::raii::Device& device() const;
        [[nodiscard]] std::uint32_t frame_count() const;
        [[nodiscard]] vk::Extent2D swapchain_extent() const;
        void register_panel(SpectraPanel panel);
        void set_window_detail(std::string detail);

    private:
        struct FrameState;
        struct RegisteredPlugin {
            template <SpectraPlugin Plugin>
            explicit RegisteredPlugin(Plugin plugin) {
                auto instance               = std::make_shared<Plugin>(std::move(plugin));
                this->name                  = std::string{instance->name()};
                this->attach                = [instance](Spectra& spectra) { instance->attach(spectra); };
                this->detach                = [instance](Spectra& spectra) { instance->detach(spectra); };
                this->before_imgui_shutdown = [instance](Spectra& spectra) { instance->before_imgui_shutdown(spectra); };
                this->after_imgui_created   = [instance](Spectra& spectra) { instance->after_imgui_created(spectra); };
                this->begin_frame           = [instance](Spectra& spectra, const SpectraFrameInfo& frame) { return instance->begin_frame(spectra, frame); };
                this->record_frame          = [instance](const vk::raii::CommandBuffer& command_buffer) { instance->record_frame(command_buffer); };
            }

            std::string name{};
            std::move_only_function<void(Spectra&)> attach{};
            std::move_only_function<void(Spectra&)> detach{};
            std::move_only_function<void(Spectra&)> before_imgui_shutdown{};
            std::move_only_function<void(Spectra&)> after_imgui_created{};
            std::move_only_function<SpectraFrameResult(Spectra&, const SpectraFrameInfo&)> begin_frame{};
            std::move_only_function<void(const vk::raii::CommandBuffer&)> record_frame{};
        };

        void register_plugin(RegisteredPlugin plugin);

        void create_imgui();
        void notify_plugins_before_imgui_shutdown() noexcept;
        void destroy_imgui() noexcept;
        void detach_plugins_noexcept() noexcept;

        bool begin_frame(FrameState& frame);
        void record_frame(FrameState& frame);
        void end_frame(FrameState& frame);

        void draw_main_menu();
        void draw_menu_toolbar();
        void draw_dockspace();
        void draw_registered_panels();
        void update_window_title(float delta_seconds);

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
        std::vector<SpectraPanel> panels{};
        std::vector<RegisteredPlugin> plugins{};
    };

    template <SpectraPlugin Plugin>
    void Spectra::register_plugin(Plugin plugin) {
        this->register_plugin(RegisteredPlugin{std::move(plugin)});
    }
} // namespace xayah
