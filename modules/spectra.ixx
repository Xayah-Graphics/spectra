module;
#include <GLFW/glfw3.h>

#include <vulkan/vulkan_raii.hpp>

#include "spectra_pbrt_fwd.h"

export module spectra;
import std;

namespace xayah {
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

    enum class SpectraPbrtDirectiveKind {
        Option,
        Identity,
        Translate,
        Rotate,
        Scale,
        LookAt,
        ConcatTransform,
        Transform,
        CoordinateSystem,
        CoordSysTransform,
        ActiveTransformAll,
        ActiveTransformEndTime,
        ActiveTransformStartTime,
        TransformTimes,
        ColorSpace,
        PixelFilter,
        Film,
        Sampler,
        Accelerator,
        Integrator,
        Camera,
        MakeNamedMedium,
        MediumInterface,
        WorldBegin,
        AttributeBegin,
        AttributeEnd,
        Attribute,
        Texture,
        Material,
        MakeNamedMaterial,
        NamedMaterial,
        LightSource,
        AreaLightSource,
        Shape,
        ReverseOrientation,
        ObjectBegin,
        ObjectEnd,
        ObjectInstance,
        EndOfFiles,
    };

    struct SpectraPbrtDirective {
        SpectraPbrtDirectiveKind kind{SpectraPbrtDirectiveKind::EndOfFiles};
        std::string name{};
        std::string type{};
        std::string value{};
        std::string target{};
        SpectraPbrtFileLocation location{};
        std::array<float, 16> transform{};
        std::array<float, 9> look_at{};
        std::array<float, 4> vector{};
        std::array<float, 2> times{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    enum class SpectraSceneTextureValueType {
        Unknown,
        Float,
        Spectrum,
    };

    enum class SpectraSceneUnsupportedFeatureKind {
        AnimatedTransform,
        ParticipatingMedium,
        VdbMedium,
        ProceduralTexture,
        AreaLightInObjectDefinition,
        ParserAttribute,
    };

    struct SpectraSceneRenderSetting {
        bool present{false};
        std::string type{};
        std::string name{};
        SpectraPbrtFileLocation location{};
        std::array<float, 16> transform{};
        std::vector<SpectraPbrtParameter> parameters{};
    };

    struct SpectraSceneTexture {
        std::string name{};
        SpectraSceneTextureValueType value_type{SpectraSceneTextureValueType::Unknown};
        std::string implementation{};
        SpectraPbrtFileLocation location{};
        std::array<float, 16> transform{};
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
        std::array<float, 16> transform{};
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
        std::array<float, 16> transform{};
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
        std::array<float, 16> transform{};
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
        std::array<float, 16> transform{};
    };

    struct SpectraSceneUnsupportedFeature {
        SpectraSceneUnsupportedFeatureKind kind{SpectraSceneUnsupportedFeatureKind::AnimatedTransform};
        std::string source_type{};
        std::string source_name{};
        std::string message{};
        SpectraPbrtFileLocation location{};
    };

    struct SpectraSceneBuildChunk {
        std::vector<SpectraPbrtDirective> pbrt_directives{};
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
        std::vector<SpectraSceneUnsupportedFeature> unsupported_features{};
    };

    struct SpectraScene {
        std::filesystem::path scene_path{};
        std::string scene_label{"No Scene"};
        std::string scene_path_text{};
        std::array<int, 2> film_resolution{0, 0};
        std::array<float, 16> camera_from_world{};
        int sampler_sample_count{0};
        std::vector<SpectraPbrtDirective> pbrt_directives{};
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
        std::vector<SpectraSceneUnsupportedFeature> unsupported_features{};
        mutable std::mutex scene_mutex{};

        void load(const std::filesystem::path& path);
        void append_build_chunk(SpectraSceneBuildChunk chunk);
        void set_runtime_metadata(const std::array<int, 2>& resolution, int samples_per_pixel, const std::array<float, 16>& camera_transform);
        void unload_noexcept() noexcept;
    };

    enum class SpectraRasterDiagnosticKind {
        UnsupportedShape,
        UnsupportedMaterial,
        UnsupportedTexture,
        MissingMaterial,
        InvalidMesh,
        MissingPlyFile,
        UnsupportedAnimatedTransform,
        UnsupportedObjectInstance,
        UnsupportedAreaLight,
        UnsupportedMediumBinding,
    };

    struct SpectraRasterDiagnostic {
        SpectraRasterDiagnosticKind kind{SpectraRasterDiagnosticKind::UnsupportedShape};
        std::string source_type{};
        std::string source_name{};
        std::string message{};
        SpectraPbrtFileLocation location{};
    };

    struct SpectraRasterVertex {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 2> uv{};
    };

    struct SpectraRasterMaterial {
        std::string name{};
        std::string source_type{};
        std::array<float, 3> base_color{};
        float roughness{1.0f};
        float metallic{0.0f};
        std::size_t source_material_index{std::numeric_limits<std::size_t>::max()};
    };

    struct SpectraRasterGeometry {
        std::size_t source_shape_index{std::numeric_limits<std::size_t>::max()};
        std::string source_type{};
        std::filesystem::path source_path{};
        std::size_t first_vertex{0};
        std::size_t vertex_count{0};
        std::size_t first_index{0};
        std::size_t index_count{0};
    };

    struct SpectraRasterDraw {
        std::size_t geometry_index{std::numeric_limits<std::size_t>::max()};
        std::size_t material_index{std::numeric_limits<std::size_t>::max()};
        std::size_t source_shape_index{std::numeric_limits<std::size_t>::max()};
        std::size_t source_instance_index{std::numeric_limits<std::size_t>::max()};
        std::array<float, 16> transform{};
        bool reverse_orientation{false};
    };

    struct SpectraRasterScene {
        std::filesystem::path scene_path{};
        std::string scene_label{"No Scene"};
        std::vector<SpectraRasterVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::vector<SpectraRasterMaterial> materials{};
        std::vector<SpectraRasterGeometry> geometries{};
        std::vector<SpectraRasterDraw> draws{};
        std::vector<SpectraRasterDiagnostic> diagnostics{};

        void build(const SpectraScene& scene);
        void unload_noexcept() noexcept;
    };

    struct SpectraPbrtBackendSceneState;
    struct SpectraPbrtRuntimeState;

    struct SpectraPbrtBackendScene {
        std::unique_ptr<SpectraPbrtBackendSceneState> state{};

        SpectraPbrtBackendScene();
        ~SpectraPbrtBackendScene() noexcept;

        SpectraPbrtBackendScene(const SpectraPbrtBackendScene& other)                = delete;
        SpectraPbrtBackendScene(SpectraPbrtBackendScene&& other) noexcept            = delete;
        SpectraPbrtBackendScene& operator=(const SpectraPbrtBackendScene& other)     = delete;
        SpectraPbrtBackendScene& operator=(SpectraPbrtBackendScene&& other) noexcept = delete;

        void load(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution);
        void unload_noexcept() noexcept;
        [[nodiscard]] pbrt::BasicScene& basic_scene();
    };

    struct SpectraPbrtInteractiveSession;
    struct SpectraVulkanRasterizer;

    export class Spectra {
    public:
        explicit Spectra(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~Spectra() noexcept;

        void run_interactive_scene(const std::filesystem::path& scene_path);

        Spectra(const Spectra& other)                = delete;
        Spectra(Spectra&& other) noexcept            = delete;
        Spectra& operator=(const Spectra& other)     = delete;
        Spectra& operator=(Spectra&& other) noexcept = delete;

    private:
        enum class SpectraRenderMode {
            PbrtPathtracer,
            VulkanRasterizer,
        };

        struct FrameState {
            std::uint32_t frame_index{0};
            std::uint32_t image_index{0};
            bool recreate_after_present{false};
            SpectraRenderMode render_mode{SpectraRenderMode::PbrtPathtracer};
            bool wait_for_external_completion{false};
            vk::Semaphore external_completion_semaphore{};
        };

        struct ActiveRendererFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        struct ActiveRendererStatus {
            const char* label{""};
            std::array<int, 2> sample_range{0, 0};
            bool has_accumulation{false};
            bool pathtracer_accumulation_dirty{false};
            bool uses_external_completion{false};
            std::string state{};
        };

        void create_imgui();
        void destroy_imgui() noexcept;
        bool begin_frame(FrameState& frame);
        void record_frame(const FrameState& frame);
        void end_frame(FrameState& frame);
        void render_loop();
        void load_spectra_scene(const std::filesystem::path& scene_path);
        void unload_spectra_scene_noexcept() noexcept;
        void load_pbrt_backend_scene(const std::array<int, 2>& resolution);
        void unload_pbrt_backend_scene_noexcept() noexcept;
        void load_raster_scene();
        void unload_raster_scene_noexcept() noexcept;
        void load_vulkan_rasterizer();
        void unload_vulkan_rasterizer_noexcept() noexcept;
        void create_renderers_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_renderers_for_resolution(const std::array<int, 2>& resolution);
        void unload_renderer_sessions_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool renderers_ready() const;
        void initialize_pbrt_runtime();
        void reset_pbrt_runtime_options_for_scene();
        void wait_pbrt_gpu_noexcept() const noexcept;
        void update_window_title(float delta_seconds);
        void update_frame_statistics(const FrameState& frame, bool rendered_sample, bool reset_accumulation, std::uint64_t sample_pixels);
        void clear_pathtracer_throughput_statistics();
        void initialize_camera_state();
        void process_camera_input(GLFWwindow* window);
        void set_camera_speed(float speed);
        void reset_camera();
        [[nodiscard]] const char* active_renderer_label() const;
        [[nodiscard]] ActiveRendererStatus active_renderer_status() const;
        [[nodiscard]] VkDescriptorSet active_viewport_descriptor() const;
        [[nodiscard]] std::array<int, 2> active_renderer_sample_range() const;
        [[nodiscard]] float active_renderer_initial_move_scale() const;
        [[nodiscard]] std::array<float, 6> active_renderer_initial_focus_bounds() const;
        [[nodiscard]] bool active_renderer_uses_external_completion_semaphore() const;
        [[nodiscard]] vk::Semaphore active_renderer_complete_semaphore() const;
        [[nodiscard]] ActiveRendererFrameResult render_active_renderer_frame(const FrameState& frame);
        void record_renderer_output(SpectraRenderMode render_mode, const vk::raii::CommandBuffer& command_buffer);
        void reset_active_renderer_accumulation();
        void request_pathtracer_accumulation_reset();
        void mark_pathtracer_accumulation_dirty();
        void set_active_render_mode(SpectraRenderMode render_mode);
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
            SpectraRenderMode active_render_mode{SpectraRenderMode::PbrtPathtracer};
            std::array<float, 2> viewport_position{0.0f, 0.0f};
            std::array<float, 2> viewport_size{1280.0f, 720.0f};
            std::array<int, 2> viewport_framebuffer_size{0, 0};
        } ui;

        std::unique_ptr<SpectraScene> spectra_scene{};
        std::unique_ptr<SpectraRasterScene> raster_scene{};
        std::unique_ptr<SpectraPbrtBackendScene> pbrt_backend_scene{};
        std::unique_ptr<SpectraPbrtInteractiveSession> pbrt_interactive{};
        std::unique_ptr<SpectraVulkanRasterizer> vulkan_rasterizer{};
        std::unique_ptr<SpectraPbrtRuntimeState> pbrt_runtime{};

        struct {
            bool candidate_known{false};
            bool renderer_created{false};
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
            std::array<float, 3> eye{0.0f, 0.0f, 0.0f};
            std::array<float, 3> center{0.0f, 0.0f, 1.0f};
            std::array<float, 3> up{0.0f, 1.0f, 0.0f};
            std::array<float, 2> mouse_position{0.0f, 0.0f};
            std::array<float, 16> moving_from_camera{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            std::array<float, 16> camera_from_world{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            bool pathtracer_accumulation_dirty{false};
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
