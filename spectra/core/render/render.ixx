export module spectra.render;

export import spectra.render.types;

import spectra.render.pathtracer;
import spectra.render.pathtracer.resources;
import spectra.render.rasterizer;
import spectra.render.gpu_scene;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

namespace spectra::render {
    export struct Engine {
        Engine(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, std::optional<RendererKind> initial_renderer = std::nullopt, RasterDisplayMode initial_raster_display_mode = RasterDisplayMode::Material);
        ~Engine();

        Engine(const Engine&)            = delete;
        Engine(Engine&&)                 = delete;
        Engine& operator=(const Engine&) = delete;
        Engine& operator=(Engine&&)      = delete;

        void rebuild(scene::ResolvedSceneView scene);
        void destroy() noexcept;
        void activate(RendererKind renderer, scene::ResolvedSceneView scene);
        void set_raster_display_mode(RasterDisplayMode mode) noexcept;
        [[nodiscard]] RasterDisplayMode raster_display_mode() const noexcept;
        void wait_for_pathtracer();
        [[nodiscard]] RendererDescriptor selected_descriptor() const noexcept;
        [[nodiscard]] std::optional<PathTracerPreparationProgress> pathtracer_preparation() const;
        [[nodiscard]] std::optional<DepthBufferView> depth_buffer() noexcept;
        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update = {}) noexcept;
        [[nodiscard]] bool prepare(scene::ResolvedSceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const;
        [[nodiscard]] std::optional<RenderProgress> progress() const noexcept;
        void set_paused(bool paused);
        void reset();
        [[nodiscard]] RenderGBufferReadback readback();

    private:
        struct {
            runtime::VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
            std::filesystem::path pathtracer_directory{};
        } context;

        std::optional<PathTracerResources> pathtracer_resources{};

        struct {
            std::optional<Rasterizer> rasterizer{};
            std::optional<PathTracer> pathtracer{};
            RendererKind selected{RendererKind::Rasterizer};
            RasterDisplayMode raster_display_mode{RasterDisplayMode::Material};
            bool ready{};
        } backends;
    };
} // namespace spectra::render
