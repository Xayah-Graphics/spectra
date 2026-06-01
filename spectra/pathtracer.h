#ifndef XAYAH_SPECTRA_PATHTRACER_H
#define XAYAH_SPECTRA_PATHTRACER_H

#include "spectra.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <spectra/pathtracer/integrator.h>
#include <string>
#include <string_view>

namespace xayah::pathtracer {
    struct RenderPipeline;
    struct SceneState;
} // namespace xayah::pathtracer

namespace xayah {
    class SpectraPathtracer final : public SpectraPlugin {
    public:
        explicit SpectraPathtracer(std::string scene_name);
        ~SpectraPathtracer() noexcept override;

        SpectraPathtracer(const SpectraPathtracer& other)                = delete;
        SpectraPathtracer(SpectraPathtracer&& other) noexcept            = delete;
        SpectraPathtracer& operator=(const SpectraPathtracer& other)     = delete;
        SpectraPathtracer& operator=(SpectraPathtracer&& other) noexcept = delete;

        [[nodiscard]] std::string_view name() const override;
        void attach(Spectra& spectra) override;
        void detach(Spectra& spectra) noexcept override;
        void before_imgui_shutdown(Spectra& spectra) noexcept override;
        void after_imgui_created(Spectra& spectra) override;
        [[nodiscard]] SpectraFrameResult begin_frame(Spectra& spectra, const SpectraFrameInfo& frame) override;
        void record_frame(const vk::raii::CommandBuffer& command_buffer) override;

    private:
        struct HostContext {
            const vk::raii::PhysicalDevice* physical_device{};
            const vk::raii::Device* device{};
            std::uint32_t frame_count{};
            vk::Extent2D swapchain_extent{};
        };

        struct PathtracerStatus {
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

        void register_panels(Spectra& spectra);
        void attach(HostContext host);
        void detach_noexcept() noexcept;
        void update_host(HostContext host);
        [[nodiscard]] std::string window_detail() const;

        void draw_viewport_window();
        void draw_camera_window();
        void draw_scene_browser_window();
        void draw_settings_window();
        void draw_inspector_window();
        void draw_environment_window();
        void draw_tonemapper_window();
        void draw_statistics_window();

        void unload_scene_noexcept() noexcept;
        void create_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_pathtracer_for_resolution(const std::array<int, 2>& resolution);
        void unload_pathtracer_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool pathtracer_ready() const;
        [[nodiscard]] std::array<int, 2> pathtracer_sample_range() const;
        void request_pathtracer_accumulation_reset();
        void initialize_camera_state();
        void set_camera_speed(float speed);
        void reset_camera();
        void clear_pathtracer_throughput_statistics();
        void update_frame_statistics(std::uint32_t frame_index, std::uint32_t image_index, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        [[nodiscard]] PathtracerStatus pathtracer_status() const;
        [[nodiscard]] bool process_camera_input();

        std::string scene_name{};
        HostContext host{};
        bool attached{false};

        struct {
            bool viewport_known{false};
            bool viewport_hovered{false};
            bool viewport_focused{false};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            std::array<int, 2> viewport_framebuffer_size{0, 0};
        } ui;

        std::unique_ptr<xayah::pathtracer::SceneState> scene_state{};
        std::unique_ptr<xayah::pathtracer::RenderPipeline> render_pipeline{};
        std::unique_ptr<spectra::pathtracer::GpuRuntime> gpu_runtime{};

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
} // namespace xayah

#endif
