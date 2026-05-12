module;
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
export module spectra;
import std;

namespace xayah {
    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name, const std::string_view& engine_name, std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;
        void run();

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

    protected:
        struct FrameState {
            std::uint32_t frame_index{0};
            std::uint32_t image_index{0};
            bool recreate_after_present{false};
        };

        bool begin_frame(FrameState& frame);
        void record_frame(const FrameState& frame);
        void draw_imgui() const;
        void render_imgui(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index);
        void begin_rendering(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index);
        void end_rendering(const vk::raii::CommandBuffer& command_buffer, std::uint32_t image_index);
        void end_frame(FrameState& frame);
        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain();

    private:
        struct {
            vk::raii::Context context;
            vk::raii::Instance instance{nullptr};
            vk::raii::DebugUtilsMessengerEXT debug_messenger{nullptr};
            vk::raii::PhysicalDevice physical_device{nullptr};
            vk::raii::Device device{nullptr};
            vk::raii::Queue graphics_queue{nullptr};
            std::uint32_t graphics_queue_index{0};
            vk::raii::CommandPool command_pool{nullptr};
        } context;

        struct {
            std::shared_ptr<GLFWwindow> window{nullptr};
            vk::raii::SurfaceKHR surface{nullptr};
            vk::Extent2D extent{};
            bool resize_requested{false};
            bool glfw_initialized{false};
        } surface;

        struct {
            vk::raii::SwapchainKHR handle{nullptr};
            vk::Format format{};
            vk::ColorSpaceKHR color_space{};
            vk::Extent2D extent{};
            std::uint32_t image_count{0};
            vk::PresentModeKHR present_mode{};
            vk::ImageUsageFlags usage{};
            std::vector<vk::Image> images{};
            std::vector<vk::ImageLayout> image_layouts{};
            std::vector<vk::raii::ImageView> image_views{};
            vk::raii::Image depth_image{nullptr};
            vk::raii::DeviceMemory depth_memory{nullptr};
            vk::raii::ImageView depth_view{nullptr};
            vk::Format depth_format{};
            vk::ImageAspectFlags depth_aspect{};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
        } swapchain;

        struct {
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::Format color_format{vk::Format::eUndefined};
            std::uint32_t min_image_count{2};
            std::uint32_t image_count{2};
            bool docking{true};
            bool viewports{false};
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
    };
} // namespace xayah
