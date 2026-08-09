export module spectra.render;

export import :common;
export import :rasterizer;
export import :pathtracer;

import std;
import vulkan;

namespace spectra {
    export struct Renderers {
        Renderers(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, std::optional<std::string> initial_renderer = std::nullopt);
        ~Renderers();

        Renderers(const Renderers&)            = delete;
        Renderers(Renderers&&)                 = delete;
        Renderers& operator=(const Renderers&) = delete;
        Renderers& operator=(Renderers&&)      = delete;

        void rebuild(scene::SceneView scene);
        void destroy() noexcept;
        void activate(std::string_view id, scene::SceneView scene);
        void wait_for_pathtracer();
        [[nodiscard]] RendererDescriptor active_descriptor() const;
        [[nodiscard]] RendererDescriptor selected_descriptor() const noexcept;
        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] std::optional<PathTracerPreparationProgress> pathtracer_preparation() const;
        [[nodiscard]] bool active_renders_visualizations() const;
        void invalidate(scene::SceneChange changes) noexcept;
        [[nodiscard]] bool prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const;
        [[nodiscard]] std::optional<RenderProgress> progress() const noexcept;
        void set_paused(bool paused);
        void reset();
        [[nodiscard]] bool gbuffer_available() const noexcept;
        [[nodiscard]] RenderGBufferReadback readback();

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        PathTracerRuntime pathtracer_runtime;

        struct {
            std::optional<Rasterizer> rasterizer{};
            std::optional<PathTracer> pathtracer{};
            std::optional<std::variant<std::reference_wrapper<Rasterizer>, std::reference_wrapper<PathTracer>>> active{};
            std::string selected_id{"rasterizer"};
        } renderers;
    };
} // namespace spectra
