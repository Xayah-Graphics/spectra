module;
#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <vulkan/vulkan_raii.hpp>

export module pbrt;
import std;

export namespace xayah {
    struct SpectraPbrtPoint3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    struct SpectraPbrtVector3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    struct SpectraPbrtBounds3 {
        SpectraPbrtPoint3 minimum{};
        SpectraPbrtPoint3 maximum{};
    };

    struct SpectraPbrtTransform {
        std::array<float, 16> matrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        std::array<float, 16> inverse_matrix{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    };

    struct SpectraPbrtFileLocation {
        std::string filename{};
        int line{0};
        int column{0};
    };

    struct SpectraPbrtParameter {
        std::string type{};
        std::string name{};
        SpectraPbrtFileLocation location{};
        std::vector<float> floats{};
        std::vector<int> ints{};
        std::vector<std::string> strings{};
        std::vector<std::uint8_t> bools{};
        bool may_be_unused{false};
    };

    enum class SpectraSceneTextureValueType {
        Unknown,
        Float,
        Spectrum,
    };

    struct SpectraSceneRenderSetting {
        bool present{false};
        std::string type{};
        std::string name{};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneTexture {
        std::string name{};
        SpectraSceneTextureValueType value_type{SpectraSceneTextureValueType::Unknown};
        std::string implementation{};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneMaterial {
        std::string name{};
        std::string type{};
        bool named{false};
        SpectraPbrtFileLocation location{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneMedium {
        std::string name{};
        std::string type{};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneMediumBinding {
        std::string inside{};
        std::string outside{};
        SpectraPbrtFileLocation location{};
    };

    struct SpectraSceneLight {
        std::string type{};
        bool area{false};
        std::string outside_medium{};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneShape {
        std::string type{};
        std::string material_name{};
        int material_index{-1};
        std::string inside_medium{};
        std::string outside_medium{};
        std::string object_definition_name{};
        std::string area_light_type{};
        bool reverse_orientation{false};
        bool animated_transform{false};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneObjectDefinition {
        std::string name{};
        SpectraPbrtFileLocation location{};
        std::vector<std::size_t> shape_indices{};
    };

    struct SpectraSceneObjectInstance {
        std::string name{};
        bool animated_transform{false};
        SpectraPbrtFileLocation location{};
        SpectraPbrtTransform transform{};
    };

    struct SpectraSceneBuildChunk {
        SpectraSceneRenderSetting pixel_filter{};
        SpectraSceneRenderSetting film{};
        SpectraSceneRenderSetting sampler{};
        SpectraSceneRenderSetting accelerator{};
        SpectraSceneRenderSetting integrator{};
        SpectraSceneRenderSetting camera{};
        std::vector<SpectraSceneTexture> textures{};
        std::vector<SpectraSceneMaterial> materials{};
        std::vector<SpectraSceneMedium> mediums{};
        std::vector<SpectraSceneMediumBinding> medium_bindings{};
        std::vector<SpectraSceneLight> lights{};
        std::vector<SpectraSceneShape> shapes{};
        std::vector<SpectraSceneObjectDefinition> object_definitions{};
        std::vector<SpectraSceneObjectInstance> object_instances{};
    };

    struct SpectraScene {
        std::filesystem::path scene_path{};
        std::string scene_label{"No Scene"};
        std::string scene_path_text{};
        std::array<int, 2> film_resolution{0, 0};
        SpectraPbrtTransform camera_from_world{};
        int sampler_sample_count{0};
        bool parsed{false};
        SpectraSceneRenderSetting pixel_filter{};
        SpectraSceneRenderSetting film{};
        SpectraSceneRenderSetting sampler{};
        SpectraSceneRenderSetting accelerator{};
        SpectraSceneRenderSetting integrator{};
        SpectraSceneRenderSetting camera{};
        std::vector<SpectraSceneTexture> textures{};
        std::vector<SpectraSceneMaterial> materials{};
        std::vector<SpectraSceneMedium> mediums{};
        std::vector<SpectraSceneMediumBinding> medium_bindings{};
        std::vector<SpectraSceneLight> lights{};
        std::vector<SpectraSceneShape> shapes{};
        std::vector<SpectraSceneObjectDefinition> object_definitions{};
        std::vector<SpectraSceneObjectInstance> object_instances{};
        mutable std::mutex scene_mutex{};

        void load(const std::filesystem::path& path);
        void append_build_chunk(SpectraSceneBuildChunk chunk);
        void set_runtime_metadata(const std::array<int, 2>& resolution, int samples_per_pixel, const SpectraPbrtTransform& camera_transform);
        void unload_noexcept() noexcept;
    };

    struct SpectraCameraPose {
        SpectraPbrtPoint3 eye{};
        SpectraPbrtPoint3 center{};
        SpectraPbrtVector3 up{};
        float basis_handedness{1.0f};
    };

    struct SpectraPbrtRuntimeState;

    class SpectraPbrtRuntime {
    public:
        SpectraPbrtRuntime();
        ~SpectraPbrtRuntime() noexcept;

        SpectraPbrtRuntime(const SpectraPbrtRuntime& other)                = delete;
        SpectraPbrtRuntime(SpectraPbrtRuntime&& other) noexcept            = delete;
        SpectraPbrtRuntime& operator=(const SpectraPbrtRuntime& other)     = delete;
        SpectraPbrtRuntime& operator=(SpectraPbrtRuntime&& other) noexcept = delete;

        void reset_options_for_scene();
        void wait_gpu_noexcept() const noexcept;

    private:
        std::unique_ptr<SpectraPbrtRuntimeState> state{};
    };

    struct SpectraPbrtBackendSceneState;

    class SpectraPbrtBackendScene {
    public:
        SpectraPbrtBackendScene();
        ~SpectraPbrtBackendScene() noexcept;

        SpectraPbrtBackendScene(const SpectraPbrtBackendScene& other)                = delete;
        SpectraPbrtBackendScene(SpectraPbrtBackendScene&& other) noexcept            = delete;
        SpectraPbrtBackendScene& operator=(const SpectraPbrtBackendScene& other)     = delete;
        SpectraPbrtBackendScene& operator=(SpectraPbrtBackendScene&& other) noexcept = delete;

        void load(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution);
        void unload_noexcept() noexcept;
        [[nodiscard]] void* native_basic_scene();

    private:
        std::unique_ptr<SpectraPbrtBackendSceneState> state{};
    };

    struct SpectraPbrtInteractiveState;

    class SpectraPbrtInteractiveSession {
    public:
        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        SpectraPbrtInteractiveSession(const SpectraScene& spectra_scene, SpectraPbrtBackendScene& backend_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count);
        ~SpectraPbrtInteractiveSession() noexcept;

        SpectraPbrtInteractiveSession(const SpectraPbrtInteractiveSession& other)                = delete;
        SpectraPbrtInteractiveSession(SpectraPbrtInteractiveSession&& other) noexcept            = delete;
        SpectraPbrtInteractiveSession& operator=(const SpectraPbrtInteractiveSession& other)     = delete;
        SpectraPbrtInteractiveSession& operator=(SpectraPbrtInteractiveSession&& other) noexcept = delete;

        void destroy_resources_noexcept() noexcept;
        [[nodiscard]] int current_sample() const;
        [[nodiscard]] int sampler_sample_count() const;
        [[nodiscard]] int target_sample_count() const;
        [[nodiscard]] float current_exposure() const;
        [[nodiscard]] float camera_initial_move_scale() const;
        [[nodiscard]] SpectraPbrtBounds3 camera_initial_focus_bounds() const;
        [[nodiscard]] std::array<int, 2> film_resolution() const;
        [[nodiscard]] SpectraPbrtTransform camera_from_world_transform() const;
        [[nodiscard]] std::uint64_t film_pixel_count() const;
        [[nodiscard]] float completion_ratio() const;
        [[nodiscard]] VkDescriptorSet active_descriptor() const;
        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;
        void set_target_sample_count(int target_sample_count);
        void set_exposure(float value);
        void request_reset_accumulation();
        void release_imgui_descriptors() noexcept;
        void create_imgui_descriptors();
        void destroy_frame_resources_noexcept() noexcept;
        [[nodiscard]] RenderFrameResult render_frame(std::uint32_t frame_index, const SpectraPbrtTransform& moving_from_camera);
        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        struct FrameResource {
            vk::raii::Buffer interop_buffer{nullptr};
            vk::raii::DeviceMemory interop_memory{nullptr};
            vk::DeviceSize interop_allocation_size{0};
            vk::DeviceSize interop_buffer_size{0};
            vk::raii::Semaphore cuda_complete_semaphore{nullptr};
            cudaExternalMemory_t cuda_external_memory{};
            cudaExternalSemaphore_t cuda_external_semaphore{};
            float* cuda_pixels{nullptr};

            vk::raii::DeviceMemory image_memory{nullptr};
            vk::raii::Image image{nullptr};
            vk::raii::ImageView image_view{nullptr};
            vk::raii::Sampler sampler{nullptr};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
            vk::ImageLayout image_layout{vk::ImageLayout::eUndefined};
        };

        std::filesystem::path scene_path{};
        std::unique_ptr<SpectraPbrtInteractiveState> pbrt_state{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        float exposure{1.0f};
        float initial_move_scale{1.0f};
        SpectraPbrtBounds3 initial_focus_bounds{};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};
        void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const;
        void create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count);
        void create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, vk::DeviceSize rgba_bytes);
        void create_cuda_complete_semaphore(const vk::raii::Device& device, FrameResource& frame);
        void create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, vk::Format display_format);
    };

    [[nodiscard]] float pbrt_camera_fov_degrees(const SpectraScene& scene);
    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const SpectraPbrtTransform& camera_from_world, const SpectraPbrtBounds3& focus_bounds);
    [[nodiscard]] SpectraPbrtTransform moving_from_camera_from_pose(const SpectraPbrtTransform& base_camera_from_world, const SpectraCameraPose& pose);
    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, float fov_degrees, const std::array<float, 2>& viewport_size);
    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement);
    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, bool invert);
    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, float speed, bool dolly);
} // namespace xayah
