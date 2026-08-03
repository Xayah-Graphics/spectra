export module spectra.render;

export import :common;
export import :rasterizer;
export import :pathtracer;

import std;
import vulkan;

namespace spectra {
    export struct Renderers {
        Renderers(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, std::optional<std::string> initial_renderer = std::nullopt);
        ~Renderers();

        Renderers(const Renderers&)            = delete;
        Renderers(Renderers&&)                 = delete;
        Renderers& operator=(const Renderers&) = delete;
        Renderers& operator=(Renderers&&)      = delete;

        void rebuild(scene::SceneView scene);
        void destroy() noexcept;
        void enable(std::string_view id, scene::SceneView scene);
        void disable(std::string_view id, std::string_view replacement_id);
        void activate(std::string_view id);
        [[nodiscard]] bool enabled(std::string_view id) const noexcept;
        [[nodiscard]] std::span<const RendererDescriptor> enabled_renderers() const noexcept;
        [[nodiscard]] RendererDescriptor active_descriptor() const;
        [[nodiscard]] bool active_renders_visualizations() const;
        void invalidate(scene::SceneChange changes) noexcept;
        void prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
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

        struct {
            std::optional<Rasterizer> rasterizer{};
            std::optional<PathTracer> path_tracer{};
            std::variant<std::monostate, Rasterizer*, PathTracer*> active{};
            std::array<RendererDescriptor, 2> enabled_descriptors{};
            std::size_t enabled_count{};
            bool rasterizer_enabled{true};
            bool path_tracer_enabled{true};
            std::string active_id{"rasterizer"};
        } renderers;

    private:
        void refresh_enabled_descriptors() noexcept;
    };
} // namespace spectra
