export module spectra.scene;

export import :math;
import std;

namespace spectra::scene {
    export class Scene {
    public:
        struct Revision {
            std::uint64_t value{};

            friend auto operator<=>(const Revision&, const Revision&) = default;
        };

        enum class DirtyFlags : std::uint32_t {
            None     = 0,
            Timeline = 1u << 0u,
            Frame    = 1u << 1u,
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
            Vector3 emission_color{};
            float emission_strength{};
            float roughness{0.5f};
            float metallic{};
            float alpha_cutoff{0.5f};
            float volume_density_scale{0.08f};
            float volume_temperature_scale{0.035f};
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
            float intensity{1.0f};
            float cone_angle_degrees{45.0f};
            SourceLocation source{};
        };

        struct Camera {
            std::string name{};
            Transform transform{};
            Vector3 target{};
            Vector3 up{0.0f, 1.0f, 0.0f};
            float vertical_fov_degrees{45.0f};
            float near_plane{0.01f};
            float far_plane{200.0f};
            SourceLocation source{};
        };

        struct CameraState {
            Vector3 eye{};
            Vector3 target{};
            Vector3 up{0.0f, 1.0f, 0.0f};
            float vertical_fov_degrees{45.0f};
        };

        struct CameraSnapshot {
            Revision revision{};
            CameraState state{};
        };

        class CameraWorkspace {
        public:
            CameraWorkspace() = default;

            CameraWorkspace(const CameraWorkspace& other) = delete;
            CameraWorkspace(CameraWorkspace&& other) = delete;
            CameraWorkspace& operator=(const CameraWorkspace& other) = delete;
            CameraWorkspace& operator=(CameraWorkspace&& other) = delete;
            ~CameraWorkspace() = default;

            void ensure_camera(std::string scene_id, CameraState state);
            [[nodiscard]] CameraSnapshot snapshot(std::string_view scene_id) const;
            [[nodiscard]] CameraSnapshot commit(std::string_view scene_id, CameraState state);

        private:
            mutable std::mutex mutex{};
            std::map<std::string, CameraSnapshot> cameras{};
        };

        struct Mesh {
            std::string name{};
            std::vector<Vector3> positions{};
            std::vector<Vector3> normals{};
            std::vector<std::uint32_t> indices{};
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

        struct VolumeChannel {
            std::string name{};
            std::array<std::uint32_t, 3> dimensions{};
            std::vector<float> values{};
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

        struct Document {
            Revision revision{};
            std::string name{};
            std::string title{};
            std::string source{};
            double frames_per_second{24.0};
            bool timeline_enabled{true};
            std::optional<Camera> camera{};
            std::vector<PreviewMaterial> materials{};
            std::vector<PreviewLight> lights{};
            std::vector<Mesh> meshes{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
        };

        enum class TimelineMode {
            Live,
            Record,
            Playback,
        };

        struct FrameCursor {
            std::uint64_t frame_index{};
            double time_seconds{};
        };

        struct FrameSnapshot {
            FrameCursor cursor{};
            std::vector<Mesh> meshes{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
        };

        struct Timeline {
            TimelineMode mode{TimelineMode::Playback};
            double frames_per_second{24.0};
            bool playing{true};
            FrameCursor cursor{};
            std::uint64_t selected_frame_index{};
            std::uint64_t reset_request_serial{};
            std::uint64_t clear_recording_request_serial{};
            std::optional<FrameSnapshot> current_frame{};
            std::vector<FrameSnapshot> recorded_frames{};
        };

        struct ResolvedFrame {
            std::vector<Mesh> meshes{};
            std::vector<PointCloud> point_clouds{};
            std::vector<VolumeGrid> volumes{};
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
            void replace_timeline(Timeline timeline);
            void replace_frame(FrameSnapshot frame);

        private:
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

        explicit Scene(Document document);
        explicit Scene(ResolvedScene scene);
        Scene(ResolvedScene scene, Document preview_document);

        [[nodiscard]] Revision revision() const;
        [[nodiscard]] std::shared_ptr<const Document> document() const;
        [[nodiscard]] Timeline timeline() const;
        [[nodiscard]] ResolvedFrame resolved_frame() const;
        [[nodiscard]] ResolvedScene resolved_scene() const;
        [[nodiscard]] Info info() const;
        [[nodiscard]] Document make_preview_document() const;
        [[nodiscard]] DirtyFlags commit(Edit edit);

        [[nodiscard]] static Scene parse_pbrt(std::string_view scene_id);

        [[nodiscard]] static constexpr DirtyFlags combine_dirty_flags(const DirtyFlags lhs, const DirtyFlags rhs) {
            return static_cast<DirtyFlags>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
        }

        [[nodiscard]] static constexpr bool has_dirty_flag(const DirtyFlags flags, const DirtyFlags flag) {
            return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(flag)) != 0u;
        }

        [[nodiscard]] static FrameCursor make_frame_cursor(const FrameInfo& info);

    private:
        [[nodiscard]] const Document& preview_document() const;

        Revision current_revision{};
        mutable std::shared_ptr<const Document> current_document{};
        Timeline current_timeline{};
        std::optional<ResolvedScene> canonical_scene{};
    };
} // namespace spectra::scene
