module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

export module spectra;
import std;

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

        struct SpectraState;
        std::unique_ptr<SpectraState> state{};
    };
} // namespace xayah
