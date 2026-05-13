module;
#include <vulkan/vulkan_raii.hpp>

export module scene;
export import volume;
export import mesh;
export import particles;
import std;

namespace xayah {
    export struct SceneFrameSnapshot {
        int frame_index{0};
        std::vector<std::variant<VolumeSnapshot, MeshSnapshot, ParticlesSnapshot>> objects{};
    };

    export struct SceneSelection {
        std::uint64_t object_id{0};
    };

    export class BoundingBoxRenderer {
    public:
        BoundingBoxRenderer();
        ~BoundingBoxRenderer() noexcept;

        BoundingBoxRenderer(const BoundingBoxRenderer& other)                = delete;
        BoundingBoxRenderer(BoundingBoxRenderer&& other) noexcept            = delete;
        BoundingBoxRenderer& operator=(const BoundingBoxRenderer& other)     = delete;
        BoundingBoxRenderer& operator=(BoundingBoxRenderer&& other) noexcept = delete;

        void create(const SceneRenderCreateContext& context);
        void destroy() noexcept;
        void render(const SceneRenderFrameContext& context, const Transform& transform, const BoundingBoxBounds& bounds, const std::array<float, 4>& color);
        [[nodiscard]] bool active() const;

    private:
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
    };

    export class Scene {
    public:
        Scene();
        ~Scene() noexcept;

        Scene(const Scene& other)                = delete;
        Scene(Scene&& other) noexcept            = delete;
        Scene& operator=(const Scene& other)     = delete;
        Scene& operator=(Scene&& other) noexcept = delete;

        SceneSelection selection{};

        void add(Volume&& object);
        void add(Mesh&& object);
        void add(Particles&& object);
        [[nodiscard]] std::size_t object_count() const;
        [[nodiscard]] std::size_t volume_count() const;
        [[nodiscard]] std::size_t mesh_count() const;
        [[nodiscard]] std::size_t particles_count() const;
        [[nodiscard]] SceneObjectRef object_ref(std::uint64_t object_id) const;
        [[nodiscard]] SceneObjectRef selected_object_ref() const;
        void select_object(std::uint64_t object_id);
        [[nodiscard]] bool has_selection() const;
        [[nodiscard]] bool selected_object_visible() const;
        [[nodiscard]] Transform& object_transform(std::uint64_t object_id);
        [[nodiscard]] const Transform& object_transform(std::uint64_t object_id) const;
        [[nodiscard]] Transform& selected_transform();
        [[nodiscard]] const Transform& selected_transform() const;
        [[nodiscard]] const char* selected_kind_label() const;
        void validate() const;
        void initialize_objects();
        [[nodiscard]] SceneFrameSnapshot make_snapshot(int frame_index) const;
        void apply_snapshot(const std::variant<VolumeSnapshot, MeshSnapshot, ParticlesSnapshot>& snapshot);
        void apply_snapshot(const SceneFrameSnapshot& snapshot);
        void create_render_resources(const SceneRenderCreateContext& context);
        void destroy_render_resources() noexcept;
        void recreate_render_resources(const SceneRenderCreateContext& context);
        void render(const SceneRenderFrameContext& context);
        void draw_hierarchy_ui();
        void draw_selected_inspector_ui();

    private:
        std::vector<std::variant<Volume, Mesh, Particles>> objects{};
        VolumeRenderer volume_renderer{};
        MeshRenderer mesh_renderer{};
        ParticlesRenderer particles_renderer{};
        BoundingBoxRenderer bounding_box_renderer{};
    };
} // namespace xayah
