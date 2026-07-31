export module spectra.workspace;

import spectra;
import spectra.pathtracer;
import spectra.plugin;
import spectra.render.assets;
import spectra.render.output;
import spectra.scene;
import spectra.rasterizer;
import spectra.scene.format;
import std;
import vulkan;

namespace spectra::workspace {
    struct WorkspaceInteraction;

    export enum class RenderMode : std::uint8_t {
        Rasterizer,
        PathTracer,
    };

    export enum class SceneProvider : std::uint8_t {
        File,
        Plugin,
    };

    export enum class CameraSource : std::uint8_t {
        Scene,
        Viewport,
    };

    export struct SelectionState {
        std::vector<scene::InstanceId> selected_instances{};
        std::optional<scene::InstanceId> active{};
        std::optional<scene::InstanceId> hovered{};
    };

    export struct PlaybackControls {
        bool running{};
        bool can_start{};
        bool can_stop{};
        bool can_advance{};
    };

    export struct TimelineState {
        double seconds{};
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
        Workspace(GpuDevice& gpu, const std::filesystem::path& shader_directory, const std::filesystem::path& scene_path, const std::optional<std::filesystem::path>& plugin_path, std::uint32_t frames_in_flight);
        ~Workspace();

        Workspace(const Workspace&)            = delete;
        Workspace(Workspace&&)                 = delete;
        Workspace& operator=(const Workspace&) = delete;
        Workspace& operator=(Workspace&&)      = delete;

        void begin_frame(std::uint32_t frame_index);
        void update(double seconds);
        void prepare(const vk::raii::CommandBuffer& command_buffer, vk::Extent2D extent);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_index);
        void record_overlays(const vk::raii::CommandBuffer& command_buffer, vk::Image target_image, vk::ImageView target_view, vk::Extent2D extent, bool show_axes);

        [[nodiscard]] render::RenderOutput output() const noexcept;
        [[nodiscard]] std::string_view provider_name() const noexcept;
        [[nodiscard]] bool dirty() const noexcept;

        void open_scene(const std::filesystem::path& path);
        void open_plugin(const std::filesystem::path& path);
        void save();
        void save_as(const std::filesystem::path& path);

        void reset_accumulation() noexcept;
        [[nodiscard]] std::uint32_t accumulated_path_samples() const noexcept;
        [[nodiscard]] render::RenderReadback readback();

        [[nodiscard]] PlaybackControls playback_controls() const;
        [[nodiscard]] TimelineState timeline() const;
        void start_playback();
        void stop_playback();
        void advance_playback(double seconds);

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

        void destroy_renderers() noexcept;
        void rebuild_renderers();
        void camera_changed() noexcept;
        void select_instance(scene::InstanceId instance, bool additive);
        void prune_selection() noexcept;
        void apply_edit(const TransformEdit& edit, bool before);
        void push_edit(TransformEdit edit);
        [[nodiscard]] std::optional<std::uint32_t> instance_index(scene::InstanceId id) const noexcept;

        GpuDevice* gpu{};
        std::filesystem::path shader_directory{};
        scene::Scene scene_storage{};
        std::unique_ptr<plugin::PluginHost> scene_plugin{};
        std::unique_ptr<render::GpuAssetCache> gpu_assets{};
        std::unique_ptr<rasterizer::RasterScene> raster_scene{};
        std::unique_ptr<rasterizer::Rasterizer> rasterizer_renderer{};
        std::unique_ptr<pathtracer::PathTracer> pathtracer_renderer{};
        std::unique_ptr<WorkspaceInteraction> interaction{};
        AxesPlane axes_plane{AxesPlane::Xz};
        scene::CameraResource display_camera{};
        float viewport_aspect{1.0f};
        std::uint64_t synchronized_revision{};
        CameraSource synchronized_camera_source{CameraSource::Viewport};
        std::uint64_t synchronized_viewport_camera_revision{};
        scene::ResourceRevision synchronized_scene_camera_revision{};
        scene::SceneChange pending_pathtracer_changes{scene::SceneChange::None};
        std::uint32_t frame_count{};
        std::vector<TransformEdit> undo_history{};
        std::vector<TransformEdit> redo_history{};
        std::optional<TransformEdit> transform_edit{};
        std::uint64_t current_edit_serial{};
        std::uint64_t saved_edit_serial{};
        std::uint64_t next_edit_serial{1};

    public:
        const scene::Scene& scene;
        std::filesystem::path source_path{};
        SceneProvider provider{SceneProvider::File};
        RenderMode mode{RenderMode::Rasterizer};
        SelectionState selection{};
        bool overlays_visible{true};
        ViewportCamera viewport_camera{};
        CameraSource camera_source{CameraSource::Viewport};
        bool pathtracer_paused{};
        float exposure{};
    };
} // namespace spectra::workspace
