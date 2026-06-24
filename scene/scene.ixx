export module spectra.scene;

export import spectra.scene.math;
export import spectra.scene.spatial;
import std;

namespace spectra::scene {
    export class EmptySceneError final : public std::runtime_error {
    public:
        explicit EmptySceneError(const std::string& message) : std::runtime_error(message) {}
    };

    export enum class ControlOptionKind {
        Text,
        DirectoryPath,
        FilePath,
        Choice,
        Bool,
        Float,
        UnsignedInteger,
    };

    export struct ControlOptionChoice {
        std::string value{};
        std::string label{};
    };

    export inline constexpr std::uint32_t ControlOptionPresentationDefault = 0u;
    export inline constexpr std::uint32_t ControlOptionPresentationSlider = 1u;

    export struct ControlSection {
        std::string id{};
        std::string label{};
    };

    export struct ControlOptionSchema {
        std::string key{};
        std::string label{};
        std::string description{};
        ControlOptionKind kind{ControlOptionKind::Text};
        bool required{};
        std::string default_value{};
        std::string section_id{};
        std::vector<ControlOptionChoice> choices{};
        std::uint32_t presentation{ControlOptionPresentationDefault};
        bool has_numeric_range{};
        float numeric_min{};
        float numeric_max{};
        float numeric_step{};
    };

    export struct ControlOption {
        std::string key{};
        std::string value{};
    };

    export enum class GpuResourceHandleKind : std::uint32_t {
        OpaqueWin32 = 1u,
        OpaqueFileDescriptor = 2u,
    };

    export struct GpuDeviceIdentity {
        std::uint32_t vendor_id{};
        std::uint32_t device_id{};
        std::array<std::uint8_t, 16u> device_uuid{};
        std::array<std::uint8_t, 8u> device_luid{};
        std::uint32_t device_node_mask{};
    };

    export inline constexpr std::uint32_t GpuBufferKindVolumeChannel = 0u;
    export inline constexpr std::uint32_t GpuBufferKindViewportVoxelGrid = 1u;

    export struct GpuBufferRequest {
        std::uint32_t kind{};
        std::uint64_t byte_size{};
        std::string debug_name{};
    };

    export struct GpuBufferAllocation {
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        std::uint32_t kind{};
        GpuResourceHandleKind handle_kind{GpuResourceHandleKind::OpaqueWin32};
        std::uintptr_t handle{};
        GpuDeviceIdentity device_identity{};
    };

    export class HostServices {
    public:
        HostServices() = default;
        HostServices(const HostServices& other) = delete;
        HostServices(HostServices&& other) = delete;
        HostServices& operator=(const HostServices& other) = delete;
        HostServices& operator=(HostServices&& other) = delete;
        virtual ~HostServices() noexcept = default;

        [[nodiscard]] virtual GpuBufferAllocation request_gpu_buffer(const GpuBufferRequest& request) = 0;
        virtual void release_gpu_buffer(std::uint64_t resource_id) = 0;
        [[nodiscard]] virtual std::string_view last_error() const = 0;
    };

    export class HostServiceRouter final : public HostServices {
    public:
        HostServiceRouter() = default;
        HostServiceRouter(const HostServiceRouter& other) = delete;
        HostServiceRouter(HostServiceRouter&& other) = delete;
        HostServiceRouter& operator=(const HostServiceRouter& other) = delete;
        HostServiceRouter& operator=(HostServiceRouter&& other) = delete;
        ~HostServiceRouter() noexcept override = default;

        void set_gpu_buffer_backend(std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback);
        void clear_gpu_buffer_backend() noexcept;
        [[nodiscard]] GpuBufferAllocation request_gpu_buffer(const GpuBufferRequest& request) override;
        void release_gpu_buffer(std::uint64_t resource_id) override;
        [[nodiscard]] std::string_view last_error() const override;

    private:
        std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_gpu_buffer_callback{};
        std::move_only_function<void(std::uint64_t)> release_gpu_buffer_callback{};
        std::map<std::uint64_t, GpuBufferAllocation> gpu_buffer_allocations{};
        std::string last_error_message{};
    };

    export inline constexpr std::uint32_t ControlMetricDisplayPrimary = 1u << 0u;

    export struct ControlAction {
        std::string id{};
        std::string label{};
        std::string description{};
        std::string section_id{};
        std::vector<ControlOptionSchema> options{};
    };

    export struct ControlMetric {
        std::string key{};
        std::string label{};
        std::string value{};
        std::string section_id{};
        std::uint32_t display_flags{};
        bool has_color{};
        std::array<float, 4u> color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    export struct ControlActionState {
        std::string action_id{};
        bool enabled{};
        std::string disabled_reason{};
    };

    export struct ControlState {
        std::string phase{};
        std::string headline{};
        std::string detail{};
        std::vector<ControlMetric> metrics{};
        std::vector<ControlActionState> action_states{};
    };

    export struct UpdateInfo {
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        bool timeline_playing{};
    };

    export enum class Kind {
        Static,
        Dynamic,
    };

    export struct Descriptor {
        std::string id{};
        std::string title{};
        Kind kind{Kind::Static};
    };

    export struct PluginOpenRequest {
        std::filesystem::path plugin_path{};
        std::vector<ControlOption> options{};
    };

    export struct PluginInfo {
        std::string id{};
        std::string title{};
        std::string open_action_label{};
        std::filesystem::path path{};
        std::vector<ControlSection> sections{};
        std::vector<ControlOptionSchema> open_options{};
        std::vector<ControlAction> control_actions{};
        std::vector<ControlOptionSchema> control_settings{};
    };

    export class Scene {
    public:
        struct Revision {
            std::uint64_t value{};

            friend auto operator<=>(const Revision&, const Revision&) = default;
        };

        enum class DirtyFlags : std::uint32_t {
            None     = 0,
            Document = 1u << 0u,
            Timeline = 1u << 1u,
            Frame    = 1u << 2u,
        };

        struct SourceLocation {
            std::string filename{};
            int line{1};
            int column{1};
        };

        enum class ColorSpace { sRGB, DCI_P3, Rec2020, ACES2065_1 };

        struct Parameter {
            std::string type{};
            std::string name{};
            std::variant<std::vector<float>, std::vector<int>, std::vector<std::string>, std::vector<std::uint8_t>> values{std::vector<float>{}};
            bool may_be_unused{false};
            ColorSpace color_space{ColorSpace::sRGB};
            SourceLocation source{};
        };

        struct Entity {
            std::string type{};
            std::vector<Parameter> parameters{};
            ColorSpace color_space{ColorSpace::sRGB};
            SourceLocation source{};
        };

        struct Option {
            std::string name{};
            std::string value{};
            SourceLocation source{};
        };

        struct MediumInterface {
            std::string inside{};
            std::string outside{};
        };

        struct RenderSettings {
            Entity filter{.type = "gaussian"};
            Entity film{.type = "rgb"};
            Entity camera{.type = "perspective"};
            Entity sampler{.type = "zsobol"};
            Entity integrator{.type = "volpath"};
            Entity accelerator{.type = "bvh"};
            SceneTransformSet camera_transform{};
            std::string camera_medium{};
            std::vector<Option> options{};
        };

        struct Material {
            std::string name{};
            Entity entity{};
        };

        struct Texture {
            std::string name{};
            std::string kind{};
            Entity entity{};
            SceneTransformSet transform{};
        };

        struct Medium {
            std::string name{};
            Entity entity{};
            SceneTransformSet transform{};
        };

        struct Light {
            std::string name{};
            Entity entity{};
            SceneTransformSet transform{};
            std::string medium{};
        };

        struct AreaLight {
            Entity entity{};
        };

        struct Shape {
            std::string name{};
            Entity entity{};
            SceneTransformSet transform{};
            bool reverse_orientation{false};
            std::string material_name{};
            std::optional<AreaLight> area_light{};
            MediumInterface medium_interface{};
        };

        struct ObjectDefinition {
            std::string name{};
            std::vector<Shape> shapes{};
            SourceLocation source{};
        };

        struct ObjectInstance {
            std::string name{};
            std::string definition_name{};
            SceneTransformSet transform{};
            SourceLocation source{};
        };

        struct ResolvedScene {
            Revision revision{};
            std::string name{};
            std::string title{};
            std::string source{};
            RenderSettings render_settings{};
            std::vector<Material> materials{};
            std::vector<Texture> textures{};
            std::vector<Medium> media{};
            std::vector<Light> lights{};
            std::vector<Shape> shapes{};
            std::vector<ObjectDefinition> object_definitions{};
            std::vector<ObjectInstance> object_instances{};
        };

        struct Info {
            std::string name{};
            std::string title{};
            std::string camera{};
            std::string sampler{};
            std::string integrator{};
            std::string accelerator{};
            std::size_t shape_count{};
            std::size_t material_count{};
            std::size_t texture_count{};
            std::size_t medium_count{};
            std::size_t light_count{};
            std::size_t area_light_count{};
            std::size_t infinite_light_count{};
            std::size_t object_definition_count{};
            std::size_t object_instance_count{};
            float camera_fov_degrees{};
        };

        struct Diagnostic {
            SourceLocation source{};
            std::string message{};
        };

        enum class PreviewSurfaceKind : std::uint32_t {
            LitSurface      = 0u,
            UnlitSurface    = 1u,
            EmissiveSurface = 2u,
            Volume          = 3u,
            PointGlyph      = 4u,
        };

        enum class PreviewAlphaMode : std::uint32_t {
            Opaque = 0u,
            Masked = 1u,
            Blend  = 2u,
        };

        struct PreviewMaterial {
            std::string name{};
            PreviewSurfaceKind surface_kind{PreviewSurfaceKind::LitSurface};
            PreviewAlphaMode alpha_mode{PreviewAlphaMode::Opaque};
            Vector4 base_color{0.8f, 0.8f, 0.8f, 1.0f};
            std::string base_color_texture{};
            Vector3 emission_color{};
            std::string emission_texture{};
            float emission_strength{};
            float roughness{0.5f};
            std::string roughness_texture{};
            float metallic{};
            float alpha_cutoff{0.5f};
            std::string normal_texture{};
            float volume_density_scale{0.08f};
            float volume_temperature_scale{0.035f};
            Entity pathtracer_material{};
        };

        enum class PreviewLightKind {
            Directional,
            Point,
            Spot,
            Area,
            Environment,
        };

        struct PreviewLight {
            std::string name{};
            PreviewLightKind kind{PreviewLightKind::Directional};
            Transform transform{};
            Vector3 color{1.0f, 1.0f, 1.0f};
            float intensity{};
            float cone_angle_degrees{45.0f};
            SourceLocation source{};
        };

        struct Mesh {
            std::string name{};
            std::vector<Vector3> positions{};
            std::vector<Vector3> normals{};
            std::vector<std::uint32_t> indices{};
            std::vector<std::array<float, 2>> uvs{};
            std::string material_name{};
            Transform transform{};
            bool dynamic{false};
            SourceLocation source{};
        };

        struct Sphere {
            std::string name{};
            float radius{1.0f};
            std::string material_name{};
            Transform transform{};
            bool dynamic{false};
            SourceLocation source{};
        };

        struct PointCloud {
            std::string name{};
            std::vector<Vector3> positions{};
            std::vector<Vector3> normals{};
            std::vector<Vector4> colors{};
            std::vector<float> radii{};
            std::string material_name{};
            Transform transform{};
            bool dynamic{true};
            SourceLocation source{};
        };

        enum class VolumeChannelSourceKind : std::uint32_t {
            Values            = 0u,
            ExternalGpuBuffer = 1u,
        };

        enum class VolumeChannelIndexEncoding : std::uint32_t {
            Linear   = 0u,
            Morton3D = 1u,
        };

        enum class VolumeChannelFormat : std::uint32_t {
            Float32   = 0u,
            Float32x3 = 1u,
        };

        struct VolumeChannel {
            std::string name{};
            std::array<std::uint32_t, 3> dimensions{};
            std::vector<float> values{};
            VolumeChannelFormat format{VolumeChannelFormat::Float32};
            VolumeChannelSourceKind source_kind{VolumeChannelSourceKind::Values};
            VolumeChannelIndexEncoding index_encoding{VolumeChannelIndexEncoding::Linear};
            std::uint64_t buffer_id{};
            std::uintptr_t external_device_pointer{};
            std::uint64_t source_byte_size{};
            std::uint64_t revision{};
        };

        struct VolumeGrid {
            std::string name{};
            std::array<std::uint32_t, 3> dimensions{};
            Vector3 origin{};
            Vector3 voxel_size{1.0f, 1.0f, 1.0f};
            std::vector<VolumeChannel> channels{};
            std::string material_name{};
            bool dynamic{true};
            SourceLocation source{};
        };

        enum class SceneEntityKind : std::uint32_t {
            Mesh       = 0u,
            Sphere     = 1u,
            PointCloud = 2u,
            VolumeGrid = 3u,
            Camera     = 4u,
            Light      = 5u,
        };

        struct SceneEntityRef {
            SceneEntityKind kind{SceneEntityKind::Mesh};
            std::string name{};
        };

        struct ViewportSegment {
            Vector3 start{};
            Vector3 end{};
        };

        enum class ViewportSegmentWidthMode : std::uint32_t {
            Screen = 0u,
            World  = 1u,
        };

        enum class ViewportSegmentDepthMode : std::uint32_t {
            DepthTested   = 0u,
            AlwaysVisible = 1u,
        };

        enum class ViewportVoxelGridSourceKind : std::uint32_t {
            IndexList = 0u,
            Bitfield  = 1u,
        };

        enum class ViewportVoxelGridIndexEncoding : std::uint32_t {
            Linear   = 0u,
            Morton3D = 1u,
        };

        struct CameraImage {
            std::uint32_t width{};
            std::uint32_t height{};
            const std::uint8_t* rgba8{};
            std::uint64_t rgba8_size{};
            std::uint64_t revision{};
        };

        struct Camera {
            std::string name{};
            CameraPose pose{};
            CameraProjection projection{};
            std::optional<CameraImage> image{};
            SourceLocation source{};
        };

        struct ViewportSegmentSet {
            std::string name{};
            SceneEntityRef owner{};
            std::vector<ViewportSegment> segments{};
            std::vector<Vector4> colors{};
            std::vector<float> widths{};
            float width{2.0f};
            ViewportSegmentWidthMode width_mode{ViewportSegmentWidthMode::Screen};
            ViewportSegmentDepthMode depth_mode{ViewportSegmentDepthMode::DepthTested};
            Transform transform{};
            bool dynamic{true};
            SourceLocation source{};
        };

        struct ViewportVoxelGrid {
            std::string name{};
            SceneEntityRef owner{};
            std::array<std::uint32_t, 3> dimensions{};
            Vector3 origin{};
            Vector3 voxel_size{1.0f, 1.0f, 1.0f};
            Vector4 color{0.15f, 0.85f, 1.0f, 0.28f};
            float cell_scale{1.0f};
            ViewportSegmentDepthMode depth_mode{ViewportSegmentDepthMode::DepthTested};
            ViewportVoxelGridSourceKind source_kind{ViewportVoxelGridSourceKind::IndexList};
            ViewportVoxelGridIndexEncoding index_encoding{ViewportVoxelGridIndexEncoding::Linear};
            std::uint64_t buffer_id{};
            std::uint64_t source_byte_size{};
            std::uint64_t index_count{};
            std::uint64_t revision{};
            bool dynamic{true};
            SourceLocation source{};
        };

        struct DebugAttachmentSet {
            std::vector<ViewportSegmentSet> viewport_segment_sets{};
            std::vector<ViewportVoxelGrid> viewport_voxel_grids{};
        };

        enum class TimelineKind : std::uint32_t {
            Static  = 0u,
            Live    = 1u,
            Indexed = 2u,
        };

        struct TimelineDescriptor {
            TimelineKind kind{TimelineKind::Static};
            double frame_rate{24.0};
            std::uint64_t frame_count{};
        };

        struct Document {
            Revision revision{};
            std::string name{};
            std::string title{};
            std::string source{};
            TimelineDescriptor timeline{};
            std::vector<Camera> cameras{};
            std::string active_camera_name{};
            std::vector<PreviewMaterial> materials{};
            std::vector<Texture> textures{};
            std::vector<PreviewLight> lights{};
            std::vector<Mesh> meshes{};
            std::vector<Sphere> spheres{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
            DebugAttachmentSet debug_attachments{};
        };

        struct FrameCursor {
            std::uint64_t frame_index{};
            double time_seconds{};
        };

        struct FrameSnapshot {
            FrameCursor cursor{};
            std::vector<Mesh> meshes{};
            std::vector<Sphere> spheres{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
            std::vector<Camera> cameras{};
            DebugAttachmentSet debug_attachments{};
        };

        struct Timeline {
            TimelineDescriptor descriptor{};
            bool playing{true};
            bool loop{true};
            double playback_accumulator_seconds{};
            FrameCursor cursor{};
            std::optional<FrameSnapshot> current_frame{};
        };

        struct ResolvedFrame {
            std::vector<Mesh> meshes{};
            std::vector<Sphere> spheres{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
            std::vector<Camera> cameras{};
            DebugAttachmentSet debug_attachments{};
        };

        class Builder {
        public:
            Builder(std::string name, std::string title, std::string source);

            Builder(const Builder& other) = delete;
            Builder(Builder&& other) noexcept = default;
            Builder& operator=(const Builder& other) = delete;
            Builder& operator=(Builder&& other) noexcept = default;
            ~Builder() noexcept = default;

            void set_revision(Revision revision);
            void set_render_settings(RenderSettings render_settings);
            void add_material(Material material);
            void add_texture(Texture texture);
            void add_medium(Medium medium);
            void add_light(Light light);
            void add_shape(Shape shape);
            void add_object_definition(ObjectDefinition definition);
            void add_object_instance(ObjectInstance instance);

            [[nodiscard]] ResolvedScene resolved_scene() &&;
            [[nodiscard]] Scene build() &&;

        private:
            ResolvedScene scene{};
        };

        class Edit {
        public:
            void replace_document(Document document);
            void replace_timeline(Timeline timeline);
            void replace_frame(FrameSnapshot frame);

        private:
            std::optional<Document> document_replacement{};
            std::optional<Timeline> timeline_replacement{};
            std::optional<FrameSnapshot> frame_replacement{};
            DirtyFlags dirty{DirtyFlags::None};

            friend class Scene;
        };

        struct FrameInfo {
            double delta_seconds{};
            double time_seconds{};
            std::uint64_t frame_index{};
        };

        Scene();
        explicit Scene(Document document);
        explicit Scene(ResolvedScene scene);
        Scene(ResolvedScene scene, Document preview_document);
        Scene(const Scene& other) = delete;
        Scene(Scene&& other) noexcept;
        Scene& operator=(const Scene& other) = delete;
        Scene& operator=(Scene&& other) noexcept;
        ~Scene() noexcept;

        [[nodiscard]] Revision revision() const;
        [[nodiscard]] std::shared_ptr<const Document> document() const;
        [[nodiscard]] Timeline timeline() const;
        [[nodiscard]] ResolvedFrame resolved_frame() const;
        [[nodiscard]] ResolvedScene resolved_scene() const;
        [[nodiscard]] ResolvedScene resolved_scene(std::move_only_function<std::vector<float>(const VolumeGrid&, const VolumeChannel&)> external_volume_materializer) const;
        [[nodiscard]] Info info() const;
        [[nodiscard]] DirtyFlags commit(Edit edit);
        [[nodiscard]] Kind kind() const;
        [[nodiscard]] bool has_descriptor() const;
        [[nodiscard]] const Descriptor& descriptor() const;
        [[nodiscard]] bool has_controls() const;
        [[nodiscard]] bool has_plugin_info() const;
        [[nodiscard]] const PluginInfo& plugin_info() const;
        [[nodiscard]] std::shared_ptr<HostServiceRouter> host_services() const;
        [[nodiscard]] ControlState control_state() const;
        void close();
        void open_static_scene(std::string id, std::string title, Scene scene);
        void open_pbrt_file(const std::filesystem::path& scene_path);
        void open_plugin(PluginOpenRequest request);
        void advance(std::uint64_t frame_number, double delta_seconds);
        void set_timeline_playing(bool playing);
        void toggle_timeline_playing();
        void set_timeline_loop(bool loop);
        void seek_timeline_frame(std::uint64_t frame_index);
        void execute_control_action(std::string_view action_id, std::span<const ControlOption> options);
        void update_control_setting(std::string_view key, std::string_view value);

        [[nodiscard]] static Scene parse_pbrt(std::string_view scene_id);
        [[nodiscard]] static Scene parse_pbrt_file(const std::filesystem::path& scene_path);

        [[nodiscard]] static constexpr DirtyFlags combine_dirty_flags(const DirtyFlags lhs, const DirtyFlags rhs) {
            return static_cast<DirtyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
        }

        [[nodiscard]] static constexpr bool has_dirty_flag(const DirtyFlags flags, const DirtyFlags flag) {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0u;
        }

        [[nodiscard]] static FrameCursor make_frame_cursor(const FrameInfo& info);

    private:
        struct PluginRuntime;

        struct DriverRuntime {
            DriverRuntime();
            DriverRuntime(const DriverRuntime& other) = delete;
            DriverRuntime(DriverRuntime&& other) noexcept;
            DriverRuntime& operator=(const DriverRuntime& other) = delete;
            DriverRuntime& operator=(DriverRuntime&& other) noexcept;
            ~DriverRuntime() noexcept;

            std::unique_ptr<PluginRuntime> plugin{};
            std::uint64_t observed_scene_revision{};
            std::optional<std::uint64_t> updated_frame_number{};
        };

        [[nodiscard]] const Document& preview_document() const;
        void replace_with_scene(Scene scene);
        void reset_driver_runtime();
        [[nodiscard]] PluginRuntime& active_plugin_runtime() const;
        void commit_driver_revision(std::string_view context);
        void sync_driver_timeline_state(std::string_view context);

        Revision current_revision{};
        mutable std::shared_ptr<const Document> current_document{};
        Timeline current_timeline{};
        std::optional<ResolvedScene> canonical_scene{};
        Descriptor current_descriptor{};
        bool descriptor_valid{};
        PluginInfo current_plugin_info{};
        bool plugin_info_valid{};
        std::shared_ptr<HostServiceRouter> host{std::make_shared<HostServiceRouter>()};
        DriverRuntime driver_runtime{};
    };

    export [[nodiscard]] bool is_plugin_file(const std::filesystem::path& path);
    export [[nodiscard]] PluginInfo inspect_plugin(const std::filesystem::path& plugin_path);
    export void WritePbrtScene(const Scene::ResolvedScene& scene, const std::filesystem::path& path);
} // namespace spectra::scene
