module;
#include <vulkan/vulkan_raii.hpp>

export module spectra_scene;
export import render_context;
export import camera;
import std;

namespace xayah {
    export enum class SpectraSceneEntityKind : std::uint32_t {
        camera         = 0,
        geometry       = 1,
        material       = 2,
        texture        = 3,
        light          = 4,
        render_setting = 5,
        instance       = 6,
    };

    export enum class SpectraGeometryKind : std::uint32_t {
        triangle_mesh = 0,
        sphere        = 1,
        disk          = 2,
    };

    export enum class SpectraMaterialKind : std::uint32_t {
        diffuse    = 0,
        conductor  = 1,
        dielectric = 2,
    };

    export enum class SpectraLightKind : std::uint32_t {
        area     = 0,
        infinite = 1,
    };

    export enum class SpectraPathTraceBackend : std::uint32_t {
        optix = 0,
    };

    export struct SpectraSceneVertex {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 3> color{};
    };

    export struct SpectraSceneCamera {
        std::uint64_t entity_id{0};
        CameraState state{};
    };

    export struct SpectraSceneFilm {
        std::array<int, 2> resolution{1280, 720};
        float exposure{0.0f};
        float gamma{2.2f};
    };

    export struct SpectraSceneSampler {
        int samples_per_pixel{64};
    };

    export struct SpectraSceneGeometry {
        std::uint64_t entity_id{0};
        SpectraGeometryKind kind{SpectraGeometryKind::sphere};
        std::uint64_t material_id{0};
        float radius{1.0f};
        float height{0.0f};
        std::vector<SpectraSceneVertex> vertices{};
        std::vector<std::uint32_t> indices{};
    };

    export struct SpectraSceneMaterial {
        std::uint64_t entity_id{0};
        SpectraMaterialKind kind{SpectraMaterialKind::diffuse};
        std::array<float, 3> base_color{0.72f, 0.72f, 0.72f};
        float roughness{0.35f};
        float eta{1.5f};
    };

    export struct SpectraSceneLight {
        std::uint64_t entity_id{0};
        SpectraLightKind kind{SpectraLightKind::infinite};
        std::array<float, 3> color{3.0f, 3.0f, 3.0f};
        float intensity{1.0f};
    };

    export struct SpectraSceneEntity {
        std::uint64_t id{0};
        SpectraSceneEntityKind kind{SpectraSceneEntityKind::geometry};
        std::string name{};
        std::string detail{};
        std::uint32_t component_index{0};
        std::array<float, 16> transform{};
        BoundingBoxBounds local_bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
        bool visible{true};
    };

    export struct SpectraSceneSelection {
        std::uint64_t entity_id{0};
    };

    export struct SpectraSceneStats {
        std::size_t cameras{0};
        std::size_t geometries{0};
        std::size_t materials{0};
        std::size_t textures{0};
        std::size_t lights{0};
        std::size_t render_settings{0};
        std::size_t instances{0};
        std::size_t preview_triangles{0};
        std::uint64_t camera_revision{0};
        std::uint64_t geometry_revision{0};
        std::uint64_t material_revision{0};
        std::uint64_t light_revision{0};
    };

    export struct RenderSceneTriangle {
        std::array<float, 3> p0{};
        std::array<float, 3> p1{};
        std::array<float, 3> p2{};
        std::array<float, 3> n0{};
        std::array<float, 3> n1{};
        std::array<float, 3> n2{};
        std::uint32_t material_index{0};
    };

    export struct RenderSceneSphere {
        std::array<float, 16> transform{};
        float radius{1.0f};
        std::uint32_t material_index{0};
    };

    export struct RenderSceneDisk {
        std::array<float, 16> transform{};
        float radius{1.0f};
        float height{0.0f};
        std::uint32_t material_index{0};
    };

    export struct RenderSceneMaterial {
        SpectraMaterialKind kind{SpectraMaterialKind::diffuse};
        std::array<float, 3> base_color{0.72f, 0.72f, 0.72f};
        float roughness{0.35f};
        float eta{1.5f};
    };

    export struct RenderSceneLight {
        SpectraLightKind kind{SpectraLightKind::infinite};
        std::array<float, 16> transform{};
        std::array<float, 3> color{3.0f, 3.0f, 3.0f};
        float intensity{1.0f};
    };

    export struct RenderSceneSnapshot {
        CameraState camera{};
        SpectraSceneFilm film{};
        SpectraSceneSampler sampler{};
        std::vector<RenderSceneTriangle> triangles{};
        std::vector<RenderSceneSphere> spheres{};
        std::vector<RenderSceneDisk> disks{};
        std::vector<RenderSceneMaterial> materials{};
        std::vector<RenderSceneLight> lights{};
        std::uint64_t camera_revision{0};
        std::uint64_t film_revision{0};
        std::uint64_t geometry_revision{0};
        std::uint64_t material_revision{0};
        std::uint64_t light_revision{0};
    };

    export struct SpectraScenePreviewOverlay {
        std::array<float, 16> transform{};
        BoundingBoxBounds bounds{};
        std::array<float, 4> color{};
    };

    export class SpectraScenePreviewRenderer {
    public:
        SpectraScenePreviewRenderer();
        ~SpectraScenePreviewRenderer() noexcept;

        SpectraScenePreviewRenderer(const SpectraScenePreviewRenderer& other)                = delete;
        SpectraScenePreviewRenderer(SpectraScenePreviewRenderer&& other) noexcept            = delete;
        SpectraScenePreviewRenderer& operator=(const SpectraScenePreviewRenderer& other)     = delete;
        SpectraScenePreviewRenderer& operator=(SpectraScenePreviewRenderer&& other) noexcept = delete;

        void create(const RenderCreateContext& context);
        void destroy() noexcept;
        void render(const RenderFrameContext& context, std::span<const SpectraSceneVertex> vertices, std::span<const SpectraScenePreviewOverlay> overlays);
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

    export class SpectraScene {
    public:
        SpectraScene();
        ~SpectraScene() noexcept;

        SpectraScene(const SpectraScene& other)                = delete;
        SpectraScene(SpectraScene&& other) noexcept            = delete;
        SpectraScene& operator=(const SpectraScene& other)     = delete;
        SpectraScene& operator=(SpectraScene&& other) noexcept = delete;

        SpectraSceneSelection selection{};

        void create_default();
        void validate() const;
        void create_render_resources(const RenderCreateContext& context);
        void destroy_render_resources() noexcept;
        void recreate_render_resources(const RenderCreateContext& context);
        void render(const RenderFrameContext& context);

        [[nodiscard]] RenderSceneSnapshot create_render_snapshot() const;
        [[nodiscard]] const std::filesystem::path& path() const;
        [[nodiscard]] std::size_t entity_count() const;
        [[nodiscard]] std::size_t object_count() const;
        [[nodiscard]] SpectraSceneStats stats() const;
        [[nodiscard]] bool has_selection() const;
        void clear_selection();
        void select_entity(std::uint64_t entity_id);
        [[nodiscard]] SpectraSceneEntity& selected_entity();
        [[nodiscard]] const SpectraSceneEntity& selected_entity() const;
        [[nodiscard]] std::vector<std::uint64_t> entity_ids(SpectraSceneEntityKind kind) const;
        [[nodiscard]] SpectraSceneEntity& entity_by_id(std::uint64_t entity_id);
        [[nodiscard]] const SpectraSceneEntity& entity_by_id(std::uint64_t entity_id) const;
        [[nodiscard]] SpectraSceneCamera& camera_by_entity_id(std::uint64_t entity_id);
        [[nodiscard]] const SpectraSceneCamera& camera_by_entity_id(std::uint64_t entity_id) const;
        [[nodiscard]] SpectraSceneMaterial& material_by_entity_id(std::uint64_t entity_id);
        [[nodiscard]] const SpectraSceneMaterial& material_by_entity_id(std::uint64_t entity_id) const;
        [[nodiscard]] SpectraSceneLight& light_by_entity_id(std::uint64_t entity_id);
        [[nodiscard]] const SpectraSceneLight& light_by_entity_id(std::uint64_t entity_id) const;
        [[nodiscard]] BoundingBoxBounds world_bounds() const;
        [[nodiscard]] BoundingBoxBounds selected_world_bounds() const;

        void mark_entity_transform_edited(std::uint64_t entity_id);
        void mark_entity_visibility_edited(std::uint64_t entity_id);
        void mark_camera_edited(std::uint64_t entity_id);
        void mark_material_edited(std::uint64_t entity_id);
        void mark_light_edited(std::uint64_t entity_id);

        void draw_scene_browser_ui();
        void draw_selected_inspector_ui(bool editing_enabled);
        void draw_entity_transform_ui(std::uint64_t entity_id, bool editing_enabled);
        void draw_entity_parameters_ui(std::uint64_t entity_id, bool editing_enabled);

    private:
        std::filesystem::path source_path{};
        std::vector<SpectraSceneEntity> entities{};
        std::vector<SpectraSceneCamera> cameras{};
        std::vector<SpectraSceneGeometry> geometries{};
        std::vector<SpectraSceneMaterial> materials{};
        std::vector<SpectraSceneLight> lights{};
        SpectraSceneFilm film{};
        SpectraSceneSampler sampler{};
        SpectraScenePreviewRenderer preview_renderer{};
        std::uint64_t next_entity_id{1};
        std::uint64_t camera_revision{1};
        std::uint64_t film_revision{1};
        std::uint64_t geometry_revision{1};
        std::uint64_t material_revision{1};
        std::uint64_t light_revision{1};

        [[nodiscard]] std::uint64_t add_entity(SpectraSceneEntityKind kind, std::string name, std::string detail, BoundingBoxBounds bounds);
        [[nodiscard]] SpectraSceneEntity* find_entity(std::uint64_t entity_id);
        [[nodiscard]] const SpectraSceneEntity* find_entity(std::uint64_t entity_id) const;
        [[nodiscard]] BoundingBoxBounds entity_world_bounds(const SpectraSceneEntity& entity) const;
    };
} // namespace xayah
