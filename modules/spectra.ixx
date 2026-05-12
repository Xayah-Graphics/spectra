module;
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
export module spectra;
export import scene;
import camera;
import std;

namespace xayah {
    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;
        void render(Scene& scene);

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
        void record_frame(const FrameState& frame, Scene& scene);
        void end_frame(FrameState& frame);

    private:
        void create_viewport_pipeline();
        void destroy_viewport_pipeline() noexcept;
        void create_volume_renderer();
        void destroy_volume_renderer() noexcept;
        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain();

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
            Camera camera{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::uint32_t vertex_count{170};
            bool grid_visible{true};
        } viewport;

        struct VolumeDrawResources {
            vk::raii::Buffer x_data_buffer{nullptr};
            vk::raii::DeviceMemory x_data_memory{nullptr};
            vk::DeviceSize x_data_size{0};
            vk::raii::Buffer y_data_buffer{nullptr};
            vk::raii::DeviceMemory y_data_memory{nullptr};
            vk::DeviceSize y_data_size{0};
            vk::raii::Buffer z_data_buffer{nullptr};
            vk::raii::DeviceMemory z_data_memory{nullptr};
            vk::DeviceSize z_data_size{0};
            vk::raii::Buffer parameters_buffer{nullptr};
            vk::raii::DeviceMemory parameters_memory{nullptr};
            vk::DeviceSize parameters_size{0};
        };

        struct {
            vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline pipeline{nullptr};
            std::vector<VolumeDrawResources> frame_resources{};
        } volume_renderer{};

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
            int frame_min{0};
            int frame_max{240};
            int current_frame{0};
            int first_frame{0};
            float height{64.0f};
        } timeline;

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
