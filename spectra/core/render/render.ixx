export module spectra.render;

export import spectra.render.contract;

import spectra.render.pathtracer;
import spectra.render.pathtracer.resources;
import spectra.render.rasterizer;
import spectra.render.scene;
import spectra.runtime;
import spectra.scene;

import std;
import vulkan;

namespace spectra {
    export struct RenderEngine {
        RenderEngine(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, std::optional<std::string> initial_renderer = std::nullopt, RasterDisplayMode initial_raster_display_mode = RasterDisplayMode::Material);
        ~RenderEngine();

        RenderEngine(const RenderEngine&)            = delete;
        RenderEngine(RenderEngine&&)                 = delete;
        RenderEngine& operator=(const RenderEngine&) = delete;
        RenderEngine& operator=(RenderEngine&&)      = delete;

        void rebuild(scene::SceneView scene);
        void destroy() noexcept;
        void activate(std::string_view id, scene::SceneView scene);
        void set_raster_display_mode(RasterDisplayMode mode) noexcept;
        void wait_for_pathtracer();
        [[nodiscard]] RendererDescriptor active_descriptor() const;
        [[nodiscard]] RendererDescriptor selected_descriptor() const noexcept;
        [[nodiscard]] bool ready() const noexcept;
        [[nodiscard]] std::optional<PathTracerPreparationProgress> pathtracer_preparation() const;
        [[nodiscard]] std::optional<DepthBufferView> depth_buffer() noexcept;
        void invalidate(scene::SceneChange changes, GpuSceneUpdate gpu_update = {}) noexcept;
        [[nodiscard]] bool prepare(scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer);
        void record(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] RenderOutput output() const;
        [[nodiscard]] std::optional<RenderProgress> progress() const noexcept;
        void set_paused(bool paused);
        void reset();
        [[nodiscard]] bool gbuffer_available() const noexcept;
        [[nodiscard]] RenderGBufferReadback readback();

    private:
        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            std::filesystem::path shader_directory{};
        } context;

        PathTracerResources pathtracer_resources;

        struct {
            std::optional<Rasterizer> rasterizer{};
            std::optional<PathTracer> pathtracer{};
            std::optional<std::variant<std::reference_wrapper<Rasterizer>, std::reference_wrapper<PathTracer>>> active{};
            std::string selected_id{"rasterizer"};
            RasterDisplayMode raster_display_mode{RasterDisplayMode::Material};
        } backends;
    };
} // namespace spectra
