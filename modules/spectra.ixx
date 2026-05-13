module;
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>
export module spectra;
export import scene_frame;
import camera;
import std;

namespace xayah {
    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;
        void render(Scene& scene);

        template <SceneFrameProducer Producer>
        void render(Scene& scene, Producer&& producer) {
            std::function<SceneFrameSnapshot(const SceneFrameRequest&)> frame_producer = [producer = std::forward<Producer>(producer)](const SceneFrameRequest& request) mutable { return producer(request); };
            this->render_loop(scene, frame_producer);
        }

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

        bool begin_frame(FrameState& frame, Scene& scene);
        void record_frame(const FrameState& frame, Scene& scene);
        void end_frame(FrameState& frame, Scene& scene);

    private:
        enum class SceneFrameSessionMode : std::uint32_t {
            idle            = 0,
            preview_running = 1,
            record_running  = 2,
            record_stopping = 3,
            playback        = 4,
        };

        void render_loop(Scene& scene, const std::function<SceneFrameSnapshot(const SceneFrameRequest&)>& frame_producer);
        void update_scene_frame_session(Scene& scene, const std::function<SceneFrameSnapshot(const SceneFrameRequest&)>& frame_producer, float delta_seconds);
        void draw_stats_panel(Scene& scene);
        void draw_object_inspector(Scene& scene);
        void draw_transform_gizmo(Scene& scene);
        void create_viewport_pipeline();
        void destroy_viewport_pipeline() noexcept;
        void create_swapchain(vk::raii::SwapchainKHR old_swapchain = nullptr);
        void recreate_swapchain(Scene& scene);

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

        struct {
            std::string mode_label{"Idle"};
            SceneFrameSessionMode mode{SceneFrameSessionMode::idle};
            bool show_record_stats{false};
            int next_frame_index{0};
            int simulated_frames{0};
            int written_frames{0};
            std::uint64_t cache_bytes{0};
            std::uint64_t max_cache_bytes{0};
        } session;

        SceneFrameRecorder recorder{};
        int applied_record_frame{-1};

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
