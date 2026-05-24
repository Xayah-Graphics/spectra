module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

export module spectra;
import std;
import pbrt;

export namespace xayah {
    class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        void run_interactive_scene(const std::filesystem::path& scene_path);

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

    private:
        struct FrameState {
            std::uint32_t frame_index{0};
            std::uint32_t image_index{0};
            bool recreate_after_present{false};
            bool wait_for_external_completion{false};
            vk::Semaphore external_completion_semaphore{};
        };

        struct PathtracerFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        struct PathtracerStatus {
            std::array<int, 2> sample_range{0, 0};
            bool uses_external_completion{false};
            std::string state{};
        };

        void create_imgui();
        void destroy_imgui() noexcept;
        bool begin_frame(FrameState& frame);
        void record_frame(const FrameState& frame);
        void end_frame(FrameState& frame);
        void render_loop();
        void unload_spectra_scene_noexcept() noexcept;
        void create_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void unload_pathtracer_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool pathtracer_ready() const;
        void update_window_title(float delta_seconds);
        void update_frame_statistics(const FrameState& frame, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        void clear_pathtracer_throughput_statistics();
        void initialize_camera_state();
        void process_camera_input(GLFWwindow* window);
        void set_camera_speed(float speed);
        void reset_camera();
        [[nodiscard]] PathtracerStatus pathtracer_status() const;
        [[nodiscard]] VkDescriptorSet pathtracer_viewport_descriptor() const;
        [[nodiscard]] std::array<int, 2> pathtracer_sample_range() const;
        [[nodiscard]] float pathtracer_initial_move_scale() const;
        [[nodiscard]] SpectraPbrtBounds3 pathtracer_initial_focus_bounds() const;
        [[nodiscard]] vk::Semaphore pathtracer_complete_semaphore() const;
        [[nodiscard]] PathtracerFrameResult render_pathtracer_frame(const FrameState& frame);
        void record_pathtracer_output(const vk::raii::CommandBuffer& command_buffer);
        void request_pathtracer_accumulation_reset();
        void draw_main_menu();
        void draw_menu_toolbar();
        void draw_dockspace();
        void draw_viewport_window();
        void draw_camera_window();
        void draw_scene_browser_window();
        void draw_inspector_window();
        void draw_settings_window();
        void draw_environment_window();
        void draw_tonemapper_window();
        void draw_statistics_window();
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
            bool dock_layout_initialized{false};
            bool camera_visible{true};
            bool scene_browser_visible{true};
            bool inspector_visible{true};
            bool settings_visible{true};
            bool environment_visible{true};
            bool tonemapper_visible{true};
            bool statistics_visible{true};
            bool viewport_known{false};
            bool viewport_hovered{false};
            bool viewport_focused{false};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            std::array<int, 2> viewport_framebuffer_size{0, 0};
        } ui;

        std::unique_ptr<SpectraScene> spectra_scene{};
        std::unique_ptr<SpectraPbrtPathtracer> pbrt_pathtracer{};
        std::unique_ptr<SpectraPbrtRuntime> pbrt_runtime{};

        struct {
            bool candidate_known{false};
            bool pathtracer_created{false};
            bool rebuilding{false};
            float stable_seconds{0.0f};
            std::array<int, 2> candidate_resolution{0, 0};
            std::array<int, 2> active_resolution{0, 0};
        } render_resolution_sync;

        struct {
            bool initialized{false};
            bool input_enabled{false};
            float speed{1.0f};
            float fov_degrees{60.0f};
            float basis_handedness{1.0f};
            bool mouse_position_known{false};
            SpectraPbrtPoint3 eye{0.0f, 0.0f, 0.0f};
            SpectraPbrtPoint3 center{0.0f, 0.0f, 1.0f};
            SpectraPbrtVector3 up{0.0f, 1.0f, 0.0f};
            std::array<float, 2> mouse_position{0.0f, 0.0f};
            SpectraPbrtTransform moving_from_camera{};
            SpectraPbrtTransform camera_from_world{};
        } camera;

        struct RollingFloatAverage {
            static constexpr std::size_t sample_count{100};

            std::array<float, sample_count> values{};
            std::size_t count{0};
            std::size_t cursor{0};
            float sum{0.0f};

            void clear() {
                this->values.fill(0.0f);
                this->count  = 0;
                this->cursor = 0;
                this->sum    = 0.0f;
            }

            void add(const float value) {
                if (!std::isfinite(value) || value < 0.0f) throw std::runtime_error("Rolling statistic value must be finite and non-negative");
                if (this->count < sample_count) {
                    this->values[this->cursor] = value;
                    this->sum += value;
                    ++this->count;
                } else {
                    this->sum -= this->values[this->cursor];
                    this->values[this->cursor] = value;
                    this->sum += value;
                }
                this->cursor = (this->cursor + 1) % sample_count;
            }

            [[nodiscard]] bool has_value() const {
                return this->count > 0;
            }

            [[nodiscard]] float average() const {
                if (this->count == 0) return 0.0f;
                return this->sum / static_cast<float>(this->count);
            }
        };

        struct {
            RollingFloatAverage frame_milliseconds{};
            RollingFloatAverage throughput_mspp{};
            std::uint64_t current_frame_id{0};
            std::uint32_t active_frame_index{0};
            std::uint32_t active_swapchain_image_index{0};
            float last_frame_milliseconds{0.0f};
            float last_valid_throughput_mspp{0.0f};
            bool has_throughput{false};
            bool last_frame_rendered_sample{false};
        } statistics;

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
