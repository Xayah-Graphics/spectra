module;
#include <vulkan/vulkan_raii.hpp>

export module mesh;
export import scene_object;
import std;

namespace xayah {
    export struct MeshVertex {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        std::array<float, 3> normal{0.0f, 1.0f, 0.0f};
        std::array<float, 3> color{0.8f, 0.8f, 0.8f};
    };

    export enum class MeshDisplayMode : std::uint32_t {
        surface   = 0,
        wireframe = 1,
    };

    export struct MeshRenderSettings {
        MeshDisplayMode display_mode{MeshDisplayMode::surface};
        bool show_bounding_box{false};
    };

    export struct MeshSnapshot {
        std::uint64_t object_id{0};
        Transform transform{};
        std::vector<MeshVertex> vertices{};
    };

    export class MeshRenderer {
    public:
        MeshRenderer();
        ~MeshRenderer() noexcept;

        MeshRenderer(const MeshRenderer& other)                = delete;
        MeshRenderer(MeshRenderer&& other) noexcept            = delete;
        MeshRenderer& operator=(const MeshRenderer& other)     = delete;
        MeshRenderer& operator=(MeshRenderer&& other) noexcept = delete;

        void create(const SceneRenderCreateContext& context);
        void destroy() noexcept;
        [[nodiscard]] bool active() const;

        vk::raii::DescriptorSetLayout descriptor_layout{nullptr};
        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::Pipeline surface_pipeline{nullptr};
        vk::raii::Pipeline wireframe_pipeline{nullptr};
    };

    export class Mesh {
    public:
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::vector<MeshVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        MeshRenderSettings render_settings{};

        Mesh();
        ~Mesh() noexcept;

        Mesh(const Mesh& other)                = delete;
        Mesh(Mesh&& other) noexcept            = default;
        Mesh& operator=(const Mesh& other)     = delete;
        Mesh& operator=(Mesh&& other) noexcept = default;

        [[nodiscard]] SceneObjectKind kind() const;
        void validate() const;
        void create_render_resources(const SceneRenderCreateContext& context, const MeshRenderer& renderer);
        void destroy_render_resources() noexcept;
        void render(const SceneRenderFrameContext& context, const MeshRenderer& renderer);
        void draw_inspector_ui();
        [[nodiscard]] BoundingBoxBounds bounds() const;
        [[nodiscard]] MeshSnapshot make_snapshot() const;
        void apply_snapshot(const MeshSnapshot& snapshot);

    private:
        struct MeshDrawResources {
            vk::raii::Buffer vertex_buffer{nullptr};
            vk::raii::DeviceMemory vertex_memory{nullptr};
            vk::DeviceSize vertex_size{0};
            vk::raii::Buffer parameters_buffer{nullptr};
            vk::raii::DeviceMemory parameters_memory{nullptr};
            vk::DeviceSize parameters_size{0};
        };

        vk::raii::DescriptorPool descriptor_pool{nullptr};
        vk::raii::DescriptorSets descriptor_sets{nullptr};
        std::vector<MeshDrawResources> frame_resources{};
    };

    static_assert(SceneObject<Mesh>);
} // namespace xayah
