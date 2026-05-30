module;
#include <vulkan/vulkan_raii.hpp>

export module spectra_gpu;
import std;

export namespace xayah {
    struct SpectraGpuPoint3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    struct SpectraGpuVector3 {
        float x{0.0f};
        float y{0.0f};
        float z{0.0f};
    };

    struct SpectraGpuBounds3 {
        SpectraGpuPoint3 minimum{};
        SpectraGpuPoint3 maximum{};
    };

    struct SpectraGpuTransform {
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

    struct SpectraGpuFileLocation {
        std::string filename{};
        int line{0};
        int column{0};
    };

    struct SpectraGpuParameter {
        std::string type{};
        std::string name{};
        SpectraGpuFileLocation location{};
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
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
        std::vector<SpectraGpuParameter> parameters{};
    };

    struct SpectraSceneTexture {
        std::string name{};
        SpectraSceneTextureValueType value_type{SpectraSceneTextureValueType::Unknown};
        std::string implementation{};
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
        std::vector<SpectraGpuParameter> parameters{};
    };

    struct SpectraSceneMaterial {
        std::string name{};
        std::string type{};
        bool named{false};
        SpectraGpuFileLocation location{};
        std::vector<SpectraGpuParameter> parameters{};
    };

    struct SpectraSceneMedium {
        std::string name{};
        std::string type{};
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
        std::vector<SpectraGpuParameter> parameters{};
    };

    struct SpectraSceneMediumBinding {
        std::string inside{};
        std::string outside{};
        SpectraGpuFileLocation location{};
    };

    struct SpectraSceneLight {
        std::string type{};
        bool area{false};
        std::string outside_medium{};
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
        std::vector<SpectraGpuParameter> parameters{};
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
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
        std::vector<SpectraGpuParameter> parameters{};
    };

    struct SpectraSceneObjectDefinition {
        std::string name{};
        SpectraGpuFileLocation location{};
        std::vector<std::size_t> shape_indices{};
    };

    struct SpectraSceneObjectInstance {
        std::string name{};
        bool animated_transform{false};
        SpectraGpuFileLocation location{};
        SpectraGpuTransform transform{};
    };

    struct SpectraScene {
        std::filesystem::path scene_path{};
        std::string scene_label{"No Scene"};
        std::string scene_path_text{};
        std::array<int, 2> film_resolution{0, 0};
        SpectraGpuTransform camera_from_world{};
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

        void load(const std::filesystem::path& path);
        void set_runtime_metadata(const std::array<int, 2>& resolution, int samples_per_pixel, const SpectraGpuTransform& camera_transform);
        void unload_noexcept() noexcept;
    };

    struct SpectraCameraPose {
        SpectraGpuPoint3 eye{};
        SpectraGpuPoint3 center{};
        SpectraGpuVector3 up{};
        float basis_handedness{1.0f};
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
        [[nodiscard]] SpectraGpuBounds3 camera_initial_focus_bounds() const;
        [[nodiscard]] std::array<int, 2> film_resolution() const;
        [[nodiscard]] SpectraGpuTransform camera_from_world_transform() const;
        [[nodiscard]] std::uint64_t film_pixel_count() const;
        [[nodiscard]] float completion_ratio() const;
        [[nodiscard]] VkDescriptorSet active_descriptor() const;
        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;
        void set_target_sample_count(int target_sample_count);
        void set_exposure(float value);
        void request_reset_accumulation();
        void release_viewport_descriptors_noexcept() noexcept;
        void create_viewport_descriptors();
        [[nodiscard]] RenderFrameResult render_frame(std::uint32_t frame_index, const SpectraGpuTransform& moving_from_camera);
        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        std::unique_ptr<SpectraGpuPathtracerState> state{};
    };

    [[nodiscard]] float spectra_camera_fov_degrees(const SpectraScene& scene);
    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const SpectraGpuTransform& camera_from_world, const SpectraGpuBounds3& focus_bounds);
    [[nodiscard]] SpectraGpuTransform moving_from_camera_from_pose(const SpectraGpuTransform& base_camera_from_world, const SpectraCameraPose& pose);
    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, float fov_degrees, const std::array<float, 2>& viewport_size);
    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement);
    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, bool invert);
    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, float speed, bool dolly);
} // namespace xayah
