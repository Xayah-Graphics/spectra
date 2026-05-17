module;
#include <vulkan/vulkan_raii.hpp>

export module pbrt_document;
export import scene_object;
import std;

namespace xayah {
    export enum class PbrtElementKind : std::uint32_t {
        camera         = 0,
        shape          = 1,
        material       = 2,
        texture        = 3,
        light          = 4,
        medium         = 5,
        render_setting = 6,
        instance       = 7,
    };

    export enum class PbrtPathTraceBackend : std::uint32_t {
        cpu       = 0,
        wavefront = 1,
        gpu       = 2,
    };

    export struct PbrtRenderSettings {
        std::array<int, 2> resolution{1280, 720};
        int samples_per_pixel{64};
        int thread_count{30};
        PbrtPathTraceBackend backend{PbrtPathTraceBackend::cpu};
        std::array<char, 256> output_path{"render-output.exr"};
    };

    export struct PbrtRenderResult {
        bool success{false};
        double seconds{0.0};
        std::string output_path{};
        std::string message{};
    };

    export struct PbrtParameter {
        std::string type{};
        std::string name{};
        std::vector<float> floats{};
        std::vector<int> ints{};
        std::vector<std::string> strings{};
        std::vector<bool> bools{};
    };

    export enum class PbrtPreviewState : std::uint32_t {
        supported   = 0,
        unsupported = 1,
        prototype   = 2,
        none        = 3,
    };

    export struct PbrtElement {
        std::uint64_t id{0};
        PbrtElementKind kind{PbrtElementKind::shape};
        std::string type{};
        std::string name{};
        std::string detail{};
        std::string prototype_name{};
        std::size_t command_index{0};
        std::vector<PbrtParameter> parameters{};
        std::array<float, 16> transform{};
        bool transform_override{false};
        bool visible{true};
        BoundingBoxBounds local_bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
        PbrtPreviewState preview_state{PbrtPreviewState::none};
        std::string preview_message{};
        std::size_t preview_triangle_count{0};
    };

    export struct PbrtSelection {
        std::uint64_t element_id{0};
    };

    export struct PbrtDocumentStats {
        std::size_t cameras{0};
        std::size_t shapes{0};
        std::size_t materials{0};
        std::size_t textures{0};
        std::size_t lights{0};
        std::size_t media{0};
        std::size_t instances{0};
        std::size_t render_settings{0};
        std::size_t commands{0};
        std::size_t preview_instances{0};
        std::size_t preview_triangles{0};
        std::size_t unsupported_preview{0};
    };

    export struct PbrtPreviewVertex {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 3> color{};
    };

    export struct PbrtPreviewOverlay {
        std::array<float, 16> transform{};
        BoundingBoxBounds bounds{};
        std::array<float, 4> color{};
    };

    export class PbrtPreviewRenderer {
    public:
        PbrtPreviewRenderer();
        ~PbrtPreviewRenderer() noexcept;

        PbrtPreviewRenderer(const PbrtPreviewRenderer& other)                = delete;
        PbrtPreviewRenderer(PbrtPreviewRenderer&& other) noexcept            = delete;
        PbrtPreviewRenderer& operator=(const PbrtPreviewRenderer& other)     = delete;
        PbrtPreviewRenderer& operator=(PbrtPreviewRenderer&& other) noexcept = delete;

        void create(const SceneRenderCreateContext& context);
        void destroy() noexcept;
        void render(const SceneRenderFrameContext& context, std::span<const PbrtPreviewVertex> vertices, std::span<const PbrtPreviewOverlay> overlays);
        [[nodiscard]] bool active() const;

    private:
        struct FrameResources {
            vk::raii::Buffer vertex_buffer{nullptr};
            vk::raii::DeviceMemory vertex_memory{nullptr};
            vk::DeviceSize vertex_size{0};
        };

        vk::raii::PipelineLayout surface_pipeline_layout{nullptr};
        vk::raii::Pipeline surface_pipeline{nullptr};
        vk::raii::PipelineLayout overlay_pipeline_layout{nullptr};
        vk::raii::Pipeline overlay_pipeline{nullptr};
        std::vector<FrameResources> frame_resources{};
    };

    export class PbrtDocument {
    public:
        PbrtDocument();
        ~PbrtDocument() noexcept;

        PbrtDocument(const PbrtDocument& other)                = delete;
        PbrtDocument(PbrtDocument&& other) noexcept            = delete;
        PbrtDocument& operator=(const PbrtDocument& other)     = delete;
        PbrtDocument& operator=(PbrtDocument&& other) noexcept = delete;

        PbrtSelection selection{};

        void load(const std::filesystem::path& path);
        void create_default();
        void validate() const;
        void create_render_resources(const SceneRenderCreateContext& context);
        void destroy_render_resources() noexcept;
        void recreate_render_resources(const SceneRenderCreateContext& context);
        void render(const SceneRenderFrameContext& context);
        [[nodiscard]] PbrtRenderResult render_final(const PbrtRenderSettings& settings);

        [[nodiscard]] const std::filesystem::path& path() const;
        [[nodiscard]] std::size_t element_count() const;
        [[nodiscard]] std::size_t object_count() const;
        [[nodiscard]] PbrtDocumentStats stats() const;
        [[nodiscard]] bool has_selection() const;
        void clear_selection();
        void select_element(std::uint64_t element_id);
        [[nodiscard]] PbrtElement& selected_element();
        [[nodiscard]] const PbrtElement& selected_element() const;
        [[nodiscard]] BoundingBoxBounds world_bounds() const;
        [[nodiscard]] BoundingBoxBounds selected_world_bounds() const;

        void draw_scene_browser_ui();
        void draw_selected_inspector_ui();

    private:
        friend class PbrtDocumentLoader;

        enum class PbrtCommandKind : std::uint32_t {
            option,
            identity,
            translate,
            rotate,
            scale,
            look_at,
            concat_transform,
            transform,
            coordinate_system,
            coord_sys_transform,
            active_transform_all,
            active_transform_end_time,
            active_transform_start_time,
            transform_times,
            color_space,
            pixel_filter,
            film,
            sampler,
            accelerator,
            integrator,
            camera,
            make_named_medium,
            medium_interface,
            world_begin,
            attribute_begin,
            attribute_end,
            attribute,
            texture,
            material,
            make_named_material,
            named_material,
            light_source,
            area_light_source,
            reverse_orientation,
            shape,
            object_begin,
            object_end,
            object_instance,
        };

        struct PbrtCommand {
            PbrtCommandKind kind{PbrtCommandKind::identity};
            std::array<std::string, 3> text{};
            std::vector<float> values{};
            std::vector<PbrtParameter> parameters{};
            std::uint64_t element_id{0};
        };

        struct PbrtPreviewMesh {
            std::uint64_t source_element_id{0};
            std::vector<PbrtPreviewVertex> vertices{};
            BoundingBoxBounds local_bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
        };

        struct PbrtPreviewInstance {
            std::uint64_t element_id{0};
            std::uint64_t source_element_id{0};
            std::size_t mesh_index{0};
            bool unsupported{false};
            BoundingBoxBounds local_bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
        };

        std::filesystem::path source_path{};
        std::vector<PbrtElement> elements{};
        PbrtPreviewRenderer preview_renderer{};
        std::vector<PbrtCommand> commands{};
        std::vector<PbrtPreviewMesh> preview_meshes{};
        std::vector<PbrtPreviewInstance> preview_instances{};
        std::uint64_t next_element_id{1};
        bool dirty{false};

        [[nodiscard]] PbrtElement* find_element(std::uint64_t element_id);
        [[nodiscard]] const PbrtElement* find_element(std::uint64_t element_id) const;
        void mark_document_dirty();
        void mark_transform_edited(PbrtElement& element);
        void mark_parameters_edited(PbrtElement& element);
        void rebuild_preview_cache();
        [[nodiscard]] std::array<float, 16> preview_instance_transform(const PbrtPreviewInstance& instance) const;
        [[nodiscard]] BoundingBoxBounds preview_instance_world_bounds(const PbrtPreviewInstance& instance) const;
    };
} // namespace xayah
