export module spectra.workspace;

import spectra;
import spectra.pathtracer;
import spectra.scene.dynamics;
import spectra.render;
import spectra.scene;
import spectra.rasterizer;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra::workspace {
    struct PickRequest {
        float x{};
        float y{};
        bool select{};
        bool additive{};
        std::optional<std::uint64_t> debug_object{};
        bool debug_xray{};
    };

    struct PickResult {
        bool available{};
        std::optional<std::uint32_t> instance_index{};
        std::optional<std::uint64_t> debug_object{};
        bool debug_xray{};
        bool select{};
        bool additive{};
    };

    struct Picker {
        Picker(Spectra& runtime, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory, std::uint32_t frames_in_flight);
        ~Picker();

        Picker(const Picker&)            = delete;
        Picker(Picker&&)                 = delete;
        Picker& operator=(const Picker&) = delete;
        Picker& operator=(Picker&&)      = delete;

        void request(PickRequest request) noexcept;
        [[nodiscard]] PickResult consume(std::uint32_t frame_index) noexcept;
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index);

    private:
        struct Slot {
            GpuBuffer result{};
            DescriptorHandle descriptor{};
            std::optional<PickRequest> submitted{};
        };

        Spectra* runtime{};
        const rasterizer::RasterScene* scene{};
        vk::raii::ShaderEXT shader{nullptr};
        std::vector<Slot> slots{};
        std::optional<PickRequest> pending{};
    };

    struct OverlayRenderer {
        OverlayRenderer(Spectra& runtime, const rasterizer::RasterScene& scene, const std::filesystem::path& shader_directory);
        ~OverlayRenderer();

        OverlayRenderer(const OverlayRenderer&)            = delete;
        OverlayRenderer(OverlayRenderer&&)                 = delete;
        OverlayRenderer& operator=(const OverlayRenderer&) = delete;
        OverlayRenderer& operator=(OverlayRenderer&&)      = delete;

        void record(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, vk::Rect2D render_region, std::span<const std::uint32_t> selected_instances, std::span<const std::uint32_t> active_instances, std::span<const std::uint32_t> hovered_instances, std::uint32_t axes_plane, bool axes_visible, bool outline_visible, bool raster_visualizations, std::span<const scene::dynamics::DebugPrimitive> debug_primitives, std::span<const render::VolumeVectorField> volume_vector_fields);

    private:
        void create_images(vk::Extent2D extent);

        Spectra* runtime{};
        const rasterizer::RasterScene* scene{};
        vk::raii::ShaderEXTs mask_shaders{nullptr};
        vk::raii::ShaderEXTs axes_shaders{nullptr};
        vk::raii::ShaderEXTs outline_shaders{nullptr};
        vk::raii::ShaderEXTs debug_shaders{nullptr};
        vk::raii::ShaderEXTs volume_vector_shaders{nullptr};
        GpuImage mask{};
        GpuImage depth{};
        GpuBuffer debug_buffer{};
        DescriptorHandle mask_descriptor{};
        DescriptorHandle sampler_descriptor{};
        DescriptorHandle debug_descriptor{};
        std::uint64_t debug_capacity{};
        vk::ImageLayout mask_layout{vk::ImageLayout::eUndefined};
        vk::ImageLayout depth_layout{vk::ImageLayout::eUndefined};
    };

    export enum class RenderMode : std::uint8_t {
        Rasterizer,
        PathTracer,
    };

    export enum class CameraSource : std::uint8_t {
        Scene,
        Viewport,
    };

    export struct SelectionState {
        std::vector<scene::InstanceId> selected_instances{};
        std::optional<scene::InstanceId> active{};
        std::optional<scene::InstanceId> hovered{};
        std::optional<std::uint64_t> debug_object{};
        std::optional<std::uint64_t> hovered_debug_object{};
    };

    export struct FrozenExportResult {
        std::filesystem::path path{};
        std::string error{};
    };

    export struct ViewportCamera {
        scene::CameraResource camera{};
        scene::Float3 focus{};
        scene::Float3 navigation_up{0.0f, 1.0f, 0.0f};
        std::uint64_t revision{1};

        ViewportCamera() = default;
        ViewportCamera(const scene::CameraResource& camera, scene::Bounds3 bounds) noexcept;

        void orbit(float x_pixels, float y_pixels) noexcept;
        void pan(float x_pixels, float y_pixels, float viewport_height) noexcept;
        void zoom(float steps) noexcept;
        void frame(scene::Bounds3 bounds, float aspect) noexcept;
        void align(scene::Float3 direction, scene::Bounds3 bounds, float aspect) noexcept;
    };

    export struct Workspace {
        Workspace(Spectra& runtime, const std::filesystem::path& shader_directory, const std::filesystem::path& scene_path, std::uint32_t frames_in_flight);
        ~Workspace();

        Workspace(const Workspace&)            = delete;
        Workspace(Workspace&&)                 = delete;
        Workspace& operator=(const Workspace&) = delete;
        Workspace& operator=(Workspace&&)      = delete;

        void begin_frame(std::uint32_t frame_index);
        void update(std::chrono::duration<double> elapsed);
        void prepare(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index);
        void record_overlays(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, bool show_axes);

        [[nodiscard]] render::RenderOutput output() const noexcept;
        [[nodiscard]] bool dirty() const noexcept;
        [[nodiscard]] bool has_dynamic_setup() const noexcept;

        void open_scene(const std::filesystem::path& path);
        void save();
        void save_as(const std::filesystem::path& path);
        void export_frozen(const std::filesystem::path& path);
        [[nodiscard]] bool export_in_progress() const noexcept;
        [[nodiscard]] std::optional<FrozenExportResult> take_export_result();
        void wait_for_export();

        void reset_accumulation() noexcept;
        [[nodiscard]] std::uint32_t accumulated_path_samples() const noexcept;
        [[nodiscard]] render::RenderReadback readback();

        [[nodiscard]] bool playback_running() const noexcept;
        [[nodiscard]] scene::dynamics::TimelineState timeline() const noexcept;
        void start_playback();
        void stop_playback();
        void advance_playback();
        void reset_playback();
        void set_simulation_step(std::uint64_t step);
        void set_export_frame(std::uint64_t frame, bool reset);
        void set_dynamic_parameters(std::size_t system, std::vector<scene::DynamicParameterSetting> parameters, bool reset);
        [[nodiscard]] std::vector<scene::dynamics::SystemState>& dynamic_systems() noexcept;
        [[nodiscard]] std::span<const scene::dynamics::ProviderDescriptor> dynamic_providers() const noexcept;
        [[nodiscard]] const std::optional<scene::DynamicSetup>& dynamic_setup() const noexcept;
        void set_dynamic_clock(scene::DynamicClock clock);
        void set_dynamic_seed(std::uint64_t seed);
        void set_dynamic_system_enabled(std::size_t system, bool enabled);
        void set_dynamic_system_visible(std::size_t system, bool visible);
        void set_dynamic_system_provider(std::size_t system, const scene::dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings);
        void add_dynamic_system(const scene::dynamics::ProviderDescriptor& provider, std::vector<scene::DynamicPortBinding> bindings);
        void remove_dynamic_system(std::size_t system);
        void set_dynamic_port_binding(std::size_t system, std::string port, scene::DynamicPortBinding binding);

        [[nodiscard]] const scene::CameraResource& active_camera() const noexcept;
        void orbit_viewport_camera(float x_pixels, float y_pixels) noexcept;
        void pan_viewport_camera(float x_pixels, float y_pixels, float viewport_height) noexcept;
        void zoom_viewport_camera(float steps) noexcept;
        void frame_scene(float aspect) noexcept;
        void frame_selection(float aspect) noexcept;
        void view_axis(scene::Float3 direction, float aspect) noexcept;

        void clear_selection() noexcept;
        void clear_hover() noexcept;
        void request_pick(float x, float y, bool select, bool additive) noexcept;

        void begin_transform_edit(scene::InstanceId instance);
        void update_transform_edit(scene::Transform transform);
        void finish_transform_edit();
        [[nodiscard]] bool can_undo() const noexcept;
        [[nodiscard]] bool can_redo() const noexcept;
        void undo();
        void redo();

        scene::Scene scene{};
        std::filesystem::path source_path{};
        RenderMode mode{RenderMode::Rasterizer};
        SelectionState selection{};
        bool overlays_visible{true};
        ViewportCamera viewport_camera{};
        CameraSource camera_source{CameraSource::Viewport};
        bool pathtracer_paused{};
        float exposure{};

    private:
        enum class AxesPlane : std::uint8_t {
            Xz,
            Xy,
            Yz,
        };

        struct TransformEdit {
            std::uint64_t before_serial{};
            std::uint64_t after_serial{};
            scene::InstanceId instance{};
            scene::Transform before{};
            scene::Transform after{};
        };

        struct DynamicSetupEdit {
            std::uint64_t before_serial{};
            std::uint64_t after_serial{};
            std::optional<scene::DynamicSetup> before{};
            std::optional<scene::DynamicSetup> after{};
            bool preserve_simulation{};
        };

        struct FrozenExportSlot {
            std::optional<render::FrozenScene> snapshot{};
            std::filesystem::path path{};
            std::filesystem::path source_path{};
        };

        void destroy_renderers() noexcept;
        void rebuild_renderers(scene::Scene& source, scene::dynamics::Runtime* dynamics);
        void camera_changed() noexcept;
        void select_instance(scene::InstanceId instance, bool additive);
        [[nodiscard]] std::pair<std::optional<std::uint64_t>, bool> debug_object_at(float x, float y) const noexcept;
        void prune_selection() noexcept;
        void apply_edit(const std::variant<TransformEdit, DynamicSetupEdit>& edit, bool before);
        void push_edit(std::variant<TransformEdit, DynamicSetupEdit> edit);
        void commit_dynamic_setup(std::optional<scene::DynamicSetup> setup, bool preserve_simulation = false);
        [[nodiscard]] std::optional<std::uint32_t> instance_index(scene::InstanceId id) const noexcept;

        Spectra* runtime{};
        std::filesystem::path shader_directory{};
        std::unique_ptr<scene::dynamics::Runtime> dynamics{};
        std::unique_ptr<render::GpuScene> gpu_scene{};
        std::unique_ptr<rasterizer::RasterScene> raster_scene{};
        std::unique_ptr<rasterizer::Rasterizer> rasterizer_renderer{};
        std::unique_ptr<pathtracer::PathTracer> pathtracer_renderer{};
        std::unique_ptr<Picker> picker{};
        std::unique_ptr<OverlayRenderer> overlay_renderer{};
        AxesPlane axes_plane{AxesPlane::Xz};
        scene::CameraResource display_camera{};
        float viewport_aspect{1.0f};
        std::uint64_t synchronized_revision{};
        CameraSource synchronized_camera_source{CameraSource::Viewport};
        std::uint64_t synchronized_viewport_camera_revision{};
        scene::ResourceRevision synchronized_scene_camera_revision{};
        scene::SceneChange pending_pathtracer_changes{scene::SceneChange::None};
        std::uint32_t frame_count{};
        std::vector<std::variant<TransformEdit, DynamicSetupEdit>> undo_history{};
        std::vector<std::variant<TransformEdit, DynamicSetupEdit>> redo_history{};
        std::optional<TransformEdit> transform_edit{};
        std::uint64_t current_edit_serial{};
        std::uint64_t saved_edit_serial{};
        std::uint64_t next_edit_serial{1};
        std::vector<FrozenExportSlot> frozen_export_slots{};
        std::optional<std::filesystem::path> frozen_export_request{};
        std::future<FrozenExportResult> frozen_export_writer{};
        std::optional<FrozenExportResult> frozen_export_result{};
    };
} // namespace spectra::workspace
