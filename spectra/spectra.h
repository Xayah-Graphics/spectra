#ifndef XAYAH_SPECTRA_SPECTRA_H
#define XAYAH_SPECTRA_SPECTRA_H

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace xayah
{
    class Spectra;

    enum class SpectraDockSlot
    {
        Center,
        Left,
        LeftBottom,
        Right,
        RightBottom,
        Bottom,
        Floating,
    };

    struct SpectraPanel
    {
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

    struct SpectraFrameInfo
    {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct SpectraFrameResult
    {
        std::optional<vk::Semaphore> completion_semaphore{};
        bool close_requested{false};
        std::optional<std::string> window_detail{};
    };

    class SpectraPlugin
    {
    public:
        virtual ~SpectraPlugin() = default;

        [[nodiscard]] virtual std::string_view name() const = 0;
        virtual void attach(Spectra& spectra) = 0;
        virtual void detach(Spectra& spectra) noexcept = 0;
        virtual void before_imgui_shutdown(Spectra& spectra) noexcept = 0;
        virtual void after_imgui_created(Spectra& spectra) = 0;
        [[nodiscard]] virtual SpectraFrameResult begin_frame(Spectra& spectra, const SpectraFrameInfo& frame) = 0;
        virtual void record_frame(const vk::raii::CommandBuffer& command_buffer) = 0;
    };

    class Spectra
    {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        Spectra(const Spectra& other) = delete;
        Spectra(Spectra&& other) noexcept = delete;
        Spectra& operator=(const Spectra& other) = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

        void register_plugin(std::unique_ptr<SpectraPlugin> plugin);
        void run();

        [[nodiscard]] const vk::raii::PhysicalDevice& physical_device() const;
        [[nodiscard]] const vk::raii::Device& device() const;
        [[nodiscard]] std::uint32_t frame_count() const;
        [[nodiscard]] vk::Extent2D swapchain_extent() const;
        void register_panel(SpectraPanel panel);
        void set_window_detail(std::string detail);

    private:
        struct FrameState;

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

        struct
        {
            vk::raii::Context context{};
            vk::raii::Instance instance{nullptr};
            vk::raii::DebugUtilsMessengerEXT debug_messenger{nullptr};
            vk::raii::PhysicalDevice physical_device{nullptr};
            vk::raii::Device device{nullptr};
            vk::raii::Queue graphics_queue{nullptr};
            std::uint32_t graphics_queue_index{0};
            vk::raii::CommandPool command_pool{nullptr};
        } context;

        struct
        {
            std::shared_ptr<GLFWwindow> window{};
            vk::raii::SurfaceKHR surface{nullptr};
            bool resize_requested{false};
            bool glfw_initialized{false};
        } surface;

        struct
        {
            vk::raii::SwapchainKHR handle{nullptr};
            std::vector<vk::Image> images{};
            std::vector<vk::raii::ImageView> image_views{};
            std::vector<vk::ImageLayout> image_layouts{};
            vk::Format format{vk::Format::eUndefined};
            vk::ColorSpaceKHR color_space{vk::ColorSpaceKHR::eSrgbNonlinear};
            vk::PresentModeKHR present_mode{vk::PresentModeKHR::eFifo};
            vk::Extent2D extent{};
        } swapchain;

        struct
        {
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            bool initialized{false};
        } imgui;

        struct
        {
            std::uint32_t frame_count{2};
            std::uint32_t frame_index{0};
            vk::raii::CommandBuffers command_buffers{nullptr};
            std::vector<vk::raii::Semaphore> image_available_semaphores{};
            std::vector<vk::raii::Semaphore> render_finished_semaphores{};
            std::vector<std::uint32_t> image_in_flight_frame{};
            std::vector<vk::raii::Fence> in_flight_fences{};
        } sync;

        struct
        {
            std::string base{};
            std::string detail{};
            std::uint64_t frame_count{0};
            float refresh_timer{0.0f};
        } window_title;

        bool dock_layout_initialized{false};
        bool imgui_shutdown_notified{false};
        std::vector<SpectraPanel> panels{};
        std::vector<std::unique_ptr<SpectraPlugin>> plugins{};
    };
} // namespace xayah

#endif
