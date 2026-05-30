#ifndef XAYAH_MODULES_SPECTRA_GPU_SESSION_H
#define XAYAH_MODULES_SPECTRA_GPU_SESSION_H

#include "backend.h"
#include "camera.h"

#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace xayah::spectra_pathtracer {
    struct HostContext {
        const vk::raii::PhysicalDevice* physical_device{};
        const vk::raii::Device* device{};
        std::uint32_t frame_count{};
        vk::Extent2D swapchain_extent{};
    };

    struct FrameInput {
        std::uint32_t frame_index{};
        std::uint32_t image_index{};
    };

    struct FrameOutput {
        vk::Semaphore completion_semaphore{};
        bool uses_external_completion{false};
    };

    [[nodiscard]] std::string scene_title_text(const SceneSession& scene);

    class InteractiveSession final {
    public:
        explicit InteractiveSession(std::filesystem::path scene_path);
        ~InteractiveSession() noexcept;

        InteractiveSession(const InteractiveSession& other)                = delete;
        InteractiveSession(InteractiveSession&& other) noexcept            = delete;
        InteractiveSession& operator=(const InteractiveSession& other)     = delete;
        InteractiveSession& operator=(InteractiveSession&& other) noexcept = delete;

        void attach(HostContext host);
        void detach() noexcept;
        void update_host(HostContext host);
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();
        [[nodiscard]] FrameOutput begin_frame(const FrameInput& frame);
        void record_frame(const vk::raii::CommandBuffer& command_buffer);
        [[nodiscard]] std::string window_detail() const;
        [[nodiscard]] bool close_requested() const;
        void clear_close_request();

        void draw_viewport_window();
        void draw_camera_window();
        void draw_scene_browser_window();
        void draw_inspector_window();
        void draw_settings_window();
        void draw_environment_window();
        void draw_tonemapper_window();
        void draw_statistics_window();

    private:
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

        struct RollingFloatAverage {
            static constexpr std::size_t sample_count{100};

            std::array<float, sample_count> values{};
            std::size_t count{0};
            std::size_t cursor{0};
            float sum{0.0f};

            void clear();
            void add(float value);
            [[nodiscard]] bool has_value() const;
            [[nodiscard]] float average() const;
        };

        struct SessionState {
            std::filesystem::path scene_path{};
            HostContext host{};
            bool attached{false};
            bool close_requested{false};

            struct {
                bool viewport_known{false};
                bool viewport_hovered{false};
                bool viewport_focused{false};
                std::array<float, 2> viewport_position{0.0f, 0.0f};
                std::array<float, 2> viewport_size{1280.0f, 720.0f};
                std::array<int, 2> viewport_framebuffer_size{0, 0};
            } ui;

            std::unique_ptr<SceneSession> spectra_scene{};
            std::unique_ptr<PathtracerSession> gpu_pathtracer{};
            std::unique_ptr<RuntimeSession> gpu_runtime{};

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
                spectra::Point3f eye{0.0f, 0.0f, 0.0f};
                spectra::Point3f center{0.0f, 0.0f, 1.0f};
                spectra::Vector3f up{0.0f, 1.0f, 0.0f};
                std::array<float, 2> mouse_position{0.0f, 0.0f};
                spectra::Transform moving_from_camera{};
                spectra::Transform camera_from_world{};
            } camera;

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
        };

        void unload_spectra_scene_noexcept() noexcept;
        void create_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void unload_pathtracer_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool pathtracer_ready() const;
        void clear_pathtracer_throughput_statistics();
        void update_frame_statistics(const FrameInput& frame, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        [[nodiscard]] PathtracerStatus pathtracer_status() const;
        [[nodiscard]] VkDescriptorSet pathtracer_viewport_descriptor() const;
        [[nodiscard]] std::array<int, 2> pathtracer_sample_range() const;
        [[nodiscard]] float pathtracer_initial_move_scale() const;
        [[nodiscard]] vk::Semaphore pathtracer_complete_semaphore() const;
        [[nodiscard]] PathtracerFrameResult render_pathtracer_frame(const FrameInput& frame);
        void record_pathtracer_output(const vk::raii::CommandBuffer& command_buffer);
        void request_pathtracer_accumulation_reset();
        void initialize_camera_state();
        void process_camera_input();
        void set_camera_speed(float speed);
        void reset_camera();

        std::unique_ptr<SessionState> state{};
    };
} // namespace xayah::spectra_pathtracer

#endif
