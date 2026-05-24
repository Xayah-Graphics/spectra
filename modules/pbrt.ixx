module;
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

        void load(const std::filesystem::path& path);
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

    struct SpectraPbrtPathtracerState;

    class SpectraPbrtPathtracer {
    public:
        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        SpectraPbrtPathtracer(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, std::uint32_t frame_count);
        ~SpectraPbrtPathtracer() noexcept;

        SpectraPbrtPathtracer(const SpectraPbrtPathtracer& other)                = delete;
        SpectraPbrtPathtracer(SpectraPbrtPathtracer&& other) noexcept            = delete;
        SpectraPbrtPathtracer& operator=(const SpectraPbrtPathtracer& other)     = delete;
        SpectraPbrtPathtracer& operator=(SpectraPbrtPathtracer&& other) noexcept = delete;

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
        void release_viewport_descriptors_noexcept() noexcept;
        void create_viewport_descriptors();
        [[nodiscard]] RenderFrameResult render_frame(std::uint32_t frame_index, const SpectraPbrtTransform& moving_from_camera);
        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        std::unique_ptr<SpectraPbrtPathtracerState> state{};
    };

    [[nodiscard]] float pbrt_camera_fov_degrees(const SpectraScene& scene);
    [[nodiscard]] SpectraCameraPose camera_pose_from_base_transform(const SpectraPbrtTransform& camera_from_world, const SpectraPbrtBounds3& focus_bounds);
    [[nodiscard]] SpectraPbrtTransform moving_from_camera_from_pose(const SpectraPbrtTransform& base_camera_from_world, const SpectraCameraPose& pose);
    bool camera_pan(SpectraCameraPose& pose, const std::array<float, 2>& displacement, float fov_degrees, const std::array<float, 2>& viewport_size);
    bool camera_dolly(SpectraCameraPose& pose, const std::array<float, 2>& displacement);
    bool camera_orbit(SpectraCameraPose& pose, std::array<float, 2> displacement, bool invert);
    bool camera_key_motion(SpectraCameraPose& pose, const std::array<float, 2>& delta, float speed, bool dolly);
} // namespace xayah
