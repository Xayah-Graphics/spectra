module;
#include <GLFW/glfw3.h>

#include <cuda_runtime_api.h>
#include <driver_types.h>
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

export module spectra:runtime;
import std;

export namespace xayah {
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
    struct SpectraPbrtInteractiveState;
    struct SpectraPbrtSessionDriver;

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
        [[nodiscard]] void* native_basic_scene();
    };

    struct SpectraPbrtInteractiveSession;
    struct SpectraVulkanRasterizer;

    struct SpectraState {
    public:
        explicit SpectraState(const std::string_view& app_name = "Spectra", const std::string_view& engine_name = "Spectra Engine", std::uint32_t window_width = 1920, std::uint32_t window_height = 1080);
        ~SpectraState() noexcept;

        void run_interactive_scene(const std::filesystem::path& scene_path);

        SpectraState(const SpectraState& other)                = delete;
        SpectraState(SpectraState&& other) noexcept            = delete;
        SpectraState& operator=(const SpectraState& other)     = delete;
        SpectraState& operator=(SpectraState&& other) noexcept = delete;

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
        void unload_spectra_scene_noexcept() noexcept;
        void load_pbrt_backend_scene(const std::array<int, 2>& resolution);
        void unload_pbrt_backend_scene_noexcept() noexcept;
        void unload_raster_scene_noexcept() noexcept;
        void load_vulkan_rasterizer();
        void unload_vulkan_rasterizer_noexcept() noexcept;
        void create_renderers_for_resolution(const std::array<int, 2>& resolution);
        void rebuild_renderers_for_resolution(const std::array<int, 2>& resolution);
        void unload_renderer_sessions_noexcept() noexcept;
        void observe_viewport_render_resolution(const std::array<int, 2>& resolution);
        void synchronize_render_resolution();
        [[nodiscard]] bool renderers_ready() const;
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

    struct SpectraScene;
    struct SpectraRasterScene;

    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout, vk::ImageAspectFlags aspect, vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access, vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access);
    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, std::uint32_t memory_type_bits, vk::MemoryPropertyFlags required_properties);
    VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callback_data, void*);
    [[nodiscard]] std::array<float, 16> identity_matrix_array();
    void validate_matrix_array(const std::array<float, 16>& values);
    [[nodiscard]] std::array<float, 16> multiply_matrix_arrays(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs);
    [[nodiscard]] std::array<float, 16> transpose_matrix_array(const std::array<float, 16>& matrix);
    [[nodiscard]] std::array<float, 16> inverse_matrix_array(const std::array<float, 16>& matrix);
    [[nodiscard]] std::array<float, 16> normal_from_local_matrix_array(const std::array<float, 16>& object_from_local);
    [[nodiscard]] std::array<float, 3> transform_point_array(const std::array<float, 16>& matrix, const std::array<float, 3>& point);
    [[nodiscard]] std::array<float, 16> raster_perspective_matrix(float fov_degrees, float aspect);
    [[nodiscard]] float raster_camera_fov_degrees(const SpectraScene& scene);
    [[nodiscard]] std::array<float, 16> raster_view_projection_matrix(const SpectraScene& scene, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera);
    [[nodiscard]] ImVec4 imgui_srgb(float red, float green, float blue, float alpha);
    void load_imgui_fonts();
    void apply_imgui_style(bool viewports);

    struct SpectraPbrtInteractiveSession {
        friend struct SpectraPbrtSessionDriver;

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

        struct RenderFrameResult {
            std::uint64_t sample_pixels{0};
            bool rendered_sample{false};
            bool reset_accumulation{false};
        };

        std::filesystem::path scene_path{};
        std::unique_ptr<SpectraPbrtInteractiveState> pbrt_state{};
        vk::Format display_format{vk::Format::eR32G32B32A32Sfloat};
        float exposure{1.0f};
        float initial_move_scale{1.0f};
        std::array<float, 6> initial_focus_bounds{};
        int sample_index{0};
        int max_samples{0};
        int target_samples{0};
        bool reset_requested{false};
        std::uint32_t active_frame_index{0};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        std::uint32_t frame_count{0};
        std::vector<FrameResource> frames{};

        SpectraPbrtInteractiveSession(const SpectraScene& spectra_scene, SpectraPbrtBackendScene& backend_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count);

        ~SpectraPbrtInteractiveSession() noexcept;

        void destroy_resources_noexcept() noexcept;

        SpectraPbrtInteractiveSession(const SpectraPbrtInteractiveSession& other)                = delete;
        SpectraPbrtInteractiveSession(SpectraPbrtInteractiveSession&& other) noexcept            = delete;
        SpectraPbrtInteractiveSession& operator=(const SpectraPbrtInteractiveSession& other)     = delete;
        SpectraPbrtInteractiveSession& operator=(SpectraPbrtInteractiveSession&& other) noexcept = delete;

        [[nodiscard]] int current_sample() const;

        [[nodiscard]] int sampler_sample_count() const;

        [[nodiscard]] int target_sample_count() const;

        [[nodiscard]] float current_exposure() const;

        [[nodiscard]] float camera_initial_move_scale() const;

        [[nodiscard]] std::array<float, 6> camera_initial_focus_bounds() const;

        [[nodiscard]] std::array<int, 2> film_resolution() const;

        [[nodiscard]] std::array<float, 16> camera_from_world_matrix() const;

        [[nodiscard]] std::uint64_t film_pixel_count() const;

        [[nodiscard]] float completion_ratio() const;

        [[nodiscard]] VkDescriptorSet active_descriptor() const;

        [[nodiscard]] vk::Semaphore active_cuda_complete_semaphore() const;

        void set_target_sample_count(const int target_sample_count);

        void set_exposure(const float value);

        void request_reset_accumulation();

        void release_imgui_descriptors() noexcept;

        void create_imgui_descriptors();

        void destroy_frame_resources_noexcept() noexcept;

        [[nodiscard]] RenderFrameResult render_frame(const std::uint32_t frame_index, const std::array<float, 16>& moving_from_camera_matrix);

        void record_copy(const vk::raii::CommandBuffer& command_buffer);

    private:
        void validate_cuda_vulkan_device(const vk::raii::PhysicalDevice& physical_device) const;

        void create_frame_resources(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count);

        void create_interop_buffer(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::DeviceSize rgba_bytes);

        void create_cuda_complete_semaphore(const vk::raii::Device& device, FrameResource& frame);

        void create_display_image(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, FrameResource& frame, const vk::Format display_format);

    };

    struct SpectraVulkanRasterizer {

        struct FrameResource {
            vk::raii::DeviceMemory color_memory{nullptr};
            vk::raii::Image color_image{nullptr};
            vk::raii::ImageView color_image_view{nullptr};
            vk::raii::Sampler color_sampler{nullptr};
            vk::ImageLayout color_layout{vk::ImageLayout::eUndefined};
            vk::raii::DeviceMemory depth_memory{nullptr};
            vk::raii::Image depth_image{nullptr};
            vk::raii::ImageView depth_image_view{nullptr};
            vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
            VkDescriptorSet imgui_descriptor{VK_NULL_HANDLE};
        };

        struct BufferResource {
            vk::raii::Buffer buffer{nullptr};
            vk::raii::DeviceMemory memory{nullptr};
            vk::DeviceSize size{0};
        };

        struct SpectraRasterDrawGpu {
            std::array<float, 16> object_from_local{};
            std::array<float, 16> normal_from_local{};
            std::uint32_t material_index{0};
            std::array<std::uint32_t, 7> padding{};
        };

        struct SpectraRasterMaterialGpu {
            std::array<float, 4> base_color_roughness{};
        };

        struct SpectraRasterPushConstants {
            std::array<float, 16> view_projection{};
            std::uint32_t draw_index{0};
            std::array<std::uint32_t, 7> padding{};
        };
        static_assert(sizeof(SpectraRasterDrawGpu) == 160);
        static_assert(sizeof(SpectraRasterPushConstants) == 96);

        const SpectraScene* scene{nullptr};
        const SpectraRasterScene* raster_scene{nullptr};
        const vk::raii::PhysicalDevice* physical_device{nullptr};
        const vk::raii::Device* device{nullptr};
        const vk::raii::Queue* graphics_queue{nullptr};
        const vk::raii::CommandPool* command_pool{nullptr};
        vk::Extent2D extent{};
        vk::Format color_format{vk::Format::eR8G8B8A8Unorm};
        vk::Format depth_format{vk::Format::eD32Sfloat};
        std::uint32_t active_frame_index{0};
        std::size_t draw_count{0};
        std::size_t triangle_count{0};
        float initial_move_scale{1.0f};
        std::array<float, 6> initial_focus_bounds{};
        bool has_initial_focus_bounds{false};
        BufferResource vertex_buffer{};
        BufferResource index_buffer{};
        BufferResource draw_buffer{};
        BufferResource material_buffer{};
        vk::raii::DescriptorSetLayout descriptor_set_layout{nullptr};
        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::DescriptorSets descriptor_sets{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::ShaderModule vertex_shader{nullptr};
        vk::raii::ShaderModule fragment_shader{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
        std::vector<FrameResource> frames{};

        SpectraVulkanRasterizer(const SpectraScene& scene, const SpectraRasterScene& raster_scene, const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const vk::raii::Queue& graphics_queue, const vk::raii::CommandPool& command_pool, const std::uint32_t frame_count);

        ~SpectraVulkanRasterizer() noexcept;

        SpectraVulkanRasterizer(const SpectraVulkanRasterizer& other)                = delete;
        SpectraVulkanRasterizer(SpectraVulkanRasterizer&& other) noexcept            = delete;
        SpectraVulkanRasterizer& operator=(const SpectraVulkanRasterizer& other)     = delete;
        SpectraVulkanRasterizer& operator=(SpectraVulkanRasterizer&& other) noexcept = delete;

        [[nodiscard]] VkDescriptorSet active_descriptor() const;

        [[nodiscard]] float camera_initial_move_scale() const;

        [[nodiscard]] std::array<float, 6> camera_initial_focus_bounds() const;

        void render_frame(const std::uint32_t frame_index);

        void record_draw(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera);

        void release_imgui_descriptors() noexcept;

        void create_imgui_descriptors();

        void destroy_resources_noexcept() noexcept;

        [[nodiscard]] std::size_t vertex_count() const;

        [[nodiscard]] std::size_t index_count() const;

        [[nodiscard]] std::size_t material_count() const;

        [[nodiscard]] std::size_t diagnostic_count() const;

    private:
        void validate_formats() const;

        [[nodiscard]] BufferResource create_buffer(const vk::DeviceSize size, const vk::BufferUsageFlags usage, const vk::MemoryPropertyFlags memory_properties) const;

        void submit_upload(const vk::raii::Buffer& staging_buffer, const vk::raii::Buffer& destination_buffer, const vk::DeviceSize size) const;

        template <typename T>
        [[nodiscard]] BufferResource upload_vector_buffer(const std::vector<T>& values, const vk::BufferUsageFlags usage) const;

        [[nodiscard]] std::vector<SpectraRasterMaterialGpu> build_gpu_materials() const;

        [[nodiscard]] std::vector<SpectraRasterDrawGpu> build_gpu_draws();

        void create_scene_buffers();

        void create_image_resource(FrameResource& frame, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect, vk::raii::DeviceMemory& memory, vk::raii::Image& image, vk::raii::ImageView& image_view) const;

        void create_frame_resources(const std::uint32_t frame_count);

        void create_descriptors();

        void create_pipeline();

        void record_geometry(const vk::raii::CommandBuffer& command_buffer, const std::array<float, 16>& camera_from_world, const std::array<float, 16>& moving_from_camera) const;

    };

} // namespace xayah
