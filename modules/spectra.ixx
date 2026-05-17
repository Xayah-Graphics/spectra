module;
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
export module spectra;
export import pbrt_document;
import camera;
import std;

namespace xayah {
    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;
        void render(PbrtDocument& document);

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

        bool begin_frame(FrameState& frame, PbrtDocument& document);
        void record_frame(const FrameState& frame, PbrtDocument& document);
        void end_frame(FrameState& frame, PbrtDocument& document);

    private:
        void render_loop(PbrtDocument& document);
        void update_window_title(float delta_seconds);
        void draw_main_menu(PbrtDocument& document);
        void draw_menu_toolbar(PbrtDocument& document);
        void draw_dockspace();
        void draw_viewport_window();
        void draw_camera_window();
        void draw_camera_quick_actions(CameraState& camera);
        void draw_camera_navigation();
        bool draw_camera_projection(CameraState& camera);
        bool draw_camera_position(CameraState& camera);
        bool draw_camera_other(CameraState& camera);
        void draw_scene_browser(PbrtDocument& document);
        void draw_settings_window();
        void draw_grid_settings_window();
        void draw_render_output(PbrtDocument& document);
        void draw_object_inspector(PbrtDocument& document);
        void draw_environment_window();
        void draw_tonemapper_window();
        void draw_statistics_window(PbrtDocument& document);
        void draw_timeline_window();
        void draw_transform_gizmo(PbrtDocument& document);
        void create_viewport_pipeline();
        void destroy_viewport_pipeline() noexcept;
        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain(PbrtDocument& document);

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
            std::string base{"Spectra"};
            float refresh_timer{0.0f};
            std::uint64_t frame_count{0};
        } window_title;

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
            vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
            vk::raii::DescriptorPool descriptor_pool{nullptr};
            vk::raii::DescriptorSets descriptor_sets{nullptr};
            struct FrameResources {
                vk::raii::Buffer parameter_buffer{nullptr};
                vk::raii::DeviceMemory parameter_memory{nullptr};
                vk::DeviceSize parameter_size{0};
            };
            std::vector<FrameResources> frame_resources{};
            vk::raii::PipelineLayout pipeline_layout{nullptr};
            vk::raii::Pipeline grid_pipeline{nullptr};
            vk::raii::Pipeline axis_pipeline{nullptr};
            std::uint32_t grid_num_lines{151};
            float grid_unit{1.0f};
            bool show_grid{false};
        } viewport;

        enum class GizmoOperation : std::uint32_t {
            translate = 0,
            rotate    = 1,
            scale     = 2,
        };

        enum class GizmoMode : std::uint32_t {
            local = 0,
            world = 1,
        };

        struct {
            GizmoOperation operation{GizmoOperation::translate};
            GizmoMode mode{GizmoMode::local};
            bool using_gizmo{false};
        } gizmo;

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
            bool dock_layout_initialized{false};
            bool camera_visible{true};
            bool scene_browser_visible{true};
            bool settings_visible{true};
            bool inspector_visible{true};
            bool environment_visible{true};
            bool tonemapper_visible{true};
            bool statistics_visible{true};
            bool render_output_visible{true};
            bool timeline_visible{true};
            bool axis_visible{true};
            bool gizmo_visible{true};
            bool snap_enabled{false};
            bool grid_settings_visible{false};
            bool viewport_known{false};
            bool viewport_hovered{false};
            bool viewport_focused{false};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            int environment_type{0};
            bool solid_background{false};
            std::array<float, 3> background_color{0.02f, 0.02f, 0.025f};
            float environment_intensity{1.0f};
            float environment_rotation_degrees{0.0f};
            float tonemap_exposure{0.0f};
            float tonemap_gamma{2.2f};
            bool tonemap_aces{true};
            float snap_rotation_degrees{45.0f};
            float snap_scale{0.1f};
        } ui;

        struct {
            int frame_min{0};
            int frame_max{0};
            int available_frame_max{0};
            int current_frame{0};
            int first_frame{0};
            float height{64.0f};
            bool visible{false};
        } timeline;

        struct {
            bool space_pressed{false};
            bool shift_down{false};
        } input;

        enum class RendererMode : std::uint32_t {
            preview     = 0,
            path_tracer = 1,
        };

        struct {
            RendererMode mode{RendererMode::preview};
            std::array<int, 2> resolution{1280, 720};
            int samples_per_pixel{64};
            int thread_count{30};
            PbrtPathTraceBackend backend{PbrtPathTraceBackend::cpu};
            std::array<char, 256> output_path{"render-output.exr"};
        } renderer;

        struct {
            std::string status{"Idle"};
            std::string error{"Ready"};
        } render_output;

        struct {
            std::string mode_label{"Idle"};
        } session;

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
