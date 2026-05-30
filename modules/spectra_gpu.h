#ifndef XAYAH_MODULES_SPECTRA_GPU_H
#define XAYAH_MODULES_SPECTRA_GPU_H

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <src/scene.h>
#include <src/util/transform.h>
#include <src/util/vecmath.h>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <filesystem>
#include <memory>

namespace xayah {
    struct SpectraScene {
        std::filesystem::path scene_path{};
        std::array<int, 2> film_resolution{0, 0};
        spectra::Transform camera_from_world{};
        int sampler_sample_count{0};
        spectra::scene::SceneDescription description{};

        void load(const std::filesystem::path& path);
        void set_runtime_metadata(const std::array<int, 2>& resolution, int samples_per_pixel, const spectra::Transform& camera_transform);
        void unload_noexcept() noexcept;
    };

    struct SpectraGpuRuntimeState;

    class SpectraGpuRuntime {
    public:
        SpectraGpuRuntime();
        ~SpectraGpuRuntime() noexcept;

        SpectraGpuRuntime(const SpectraGpuRuntime& other)                = delete;
        SpectraGpuRuntime(SpectraGpuRuntime&& other) noexcept            = delete;
        SpectraGpuRuntime& operator=(const SpectraGpuRuntime& other)     = delete;
        SpectraGpuRuntime& operator=(SpectraGpuRuntime&& other) noexcept = delete;

        void reset_options_for_scene();
        void wait_gpu_noexcept() const noexcept;

    private:
        std::unique_ptr<SpectraGpuRuntimeState> state{};
    };

    struct SpectraGpuPathtracerState;

    class SpectraGpuPathtracer {
    public:
        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        SpectraGpuPathtracer(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count);
        ~SpectraGpuPathtracer() noexcept;

        SpectraGpuPathtracer(const SpectraGpuPathtracer& other)                = delete;
        SpectraGpuPathtracer(SpectraGpuPathtracer&& other) noexcept            = delete;
        SpectraGpuPathtracer& operator=(const SpectraGpuPathtracer& other)     = delete;
        SpectraGpuPathtracer& operator=(SpectraGpuPathtracer&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const;
        [[nodiscard]] int sampler_sample_count() const;
        [[nodiscard]] int target_sample_count() const;
        [[nodiscard]] float current_exposure() const;
        [[nodiscard]] float camera_initial_move_scale() const;
        [[nodiscard]] spectra::Bounds3f camera_initial_focus_bounds() const;
        [[nodiscard]] std::array<int, 2> film_resolution() const;
        [[nodiscard]] spectra::Transform camera_from_world_transform() const;
        [[nodiscard]] std::uint64_t film_pixel_count() const;
        [[nodiscard]] float completion_ratio() const;
        [[nodiscard]] VkDescriptorSet active_descriptor() const;
        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;
        void set_target_sample_count(int target_sample_count);
        void set_exposure(float value);
        void request_reset_accumulation();
        void release_viewport_descriptors_noexcept() noexcept;
        void create_viewport_descriptors();
        [[nodiscard]] RenderFrameResult render_frame(std::uint32_t frame_index, const spectra::Transform& moving_from_camera);
        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        std::unique_ptr<SpectraGpuPathtracerState> state{};
    };

} // namespace xayah

#endif
