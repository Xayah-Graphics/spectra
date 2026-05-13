module;
#include <vulkan/vulkan_raii.hpp>

export module particles;
export import scene_object;
import std;

namespace xayah {
    export struct Particle {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        float radius{0.03f};
        std::array<float, 3> color{0.35f, 0.70f, 1.0f};
    };

    export struct ParticleRenderSettings {
        float radius_scale{1.0f};
        bool show_bounding_box{false};
    };

    export struct ParticlesSnapshot {
        std::uint64_t object_id{0};
        std::vector<Particle> particles{};
    };

    export class ParticlesRenderer {
    public:
        ParticlesRenderer();
        ~ParticlesRenderer() noexcept;

        ParticlesRenderer(const ParticlesRenderer& other)                = delete;
        ParticlesRenderer(ParticlesRenderer&& other) noexcept            = delete;
        ParticlesRenderer& operator=(const ParticlesRenderer& other)     = delete;
        ParticlesRenderer& operator=(ParticlesRenderer&& other) noexcept = delete;

        void create(const SceneRenderCreateContext& context);
        void destroy() noexcept;
        [[nodiscard]] bool active() const;

        vk::raii::PipelineLayout pipeline_layout{nullptr};
        vk::raii::Pipeline pipeline{nullptr};
    };

    export class Particles {
    public:
        std::uint64_t id{0};
        std::string name{};
        bool visible{true};
        Transform transform{};
        std::vector<Particle> particles{};
        ParticleRenderSettings render_settings{};

        Particles();
        ~Particles() noexcept;

        Particles(const Particles& other)                = delete;
        Particles(Particles&& other) noexcept            = default;
        Particles& operator=(const Particles& other)     = delete;
        Particles& operator=(Particles&& other) noexcept = default;

        [[nodiscard]] SceneObjectKind kind() const;
        void validate() const;
        void create_render_resources(const SceneRenderCreateContext& context, const ParticlesRenderer& renderer);
        void destroy_render_resources() noexcept;
        void render(const SceneRenderFrameContext& context, const ParticlesRenderer& renderer);
        void draw_inspector_ui();
        [[nodiscard]] BoundingBoxBounds bounds() const;
        [[nodiscard]] ParticlesSnapshot make_snapshot() const;
        void apply_snapshot(const ParticlesSnapshot& snapshot);

    private:
        struct ParticleDrawResources {
            vk::raii::Buffer vertex_buffer{nullptr};
            vk::raii::DeviceMemory vertex_memory{nullptr};
            vk::DeviceSize vertex_size{0};
        };

        std::vector<ParticleDrawResources> frame_resources{};
    };

    static_assert(SceneObject<Particles>);
} // namespace xayah
