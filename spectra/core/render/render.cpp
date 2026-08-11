module spectra.render;

import std;

namespace spectra {
    RenderEngine::RenderEngine(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, std::optional<std::string> initial_renderer, const RasterDisplayMode initial_raster_display_mode) : context{runtime, gpu_scene, std::move(shader_directory), pathtracer_directory} {
        this->backends.raster_display_mode = initial_raster_display_mode;
        if (initial_renderer) {
            if (*initial_renderer != rasterizer_descriptor.id && *initial_renderer != pathtracer_descriptor.id) throw std::runtime_error(std::format("Unknown Scene Renderer: {}", *initial_renderer));
            this->backends.selected_id = std::move(*initial_renderer);
        }
    }

    RenderEngine::~RenderEngine() {
        this->destroy();
    }

    void RenderEngine::rebuild(const scene::SceneView scene) {
        const std::string selected_id = this->backends.selected_id;
        this->destroy();
        this->activate(selected_id, scene);
    }

    void RenderEngine::destroy() noexcept {
        this->backends.active.reset();
        this->backends.pathtracer.reset();
        this->backends.rasterizer.reset();
    }

    void RenderEngine::activate(const std::string_view id, const scene::SceneView scene) {
        if (id == rasterizer_descriptor.id) {
            if (!this->backends.rasterizer) this->backends.rasterizer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
            this->backends.rasterizer->set_display_mode(this->backends.raster_display_mode);
            this->backends.active = std::ref(*this->backends.rasterizer);
        } else if (id == pathtracer_descriptor.id) {
            if (!this->context.runtime.graphics.ray_tracing_supported) throw std::runtime_error("The Path Tracer requires the complete Vulkan KHR ray-tracing profile");
            if (!this->pathtracer_resources) this->pathtracer_resources.emplace(this->context.runtime, this->context.pathtracer_directory);
            if (!this->backends.pathtracer) this->backends.pathtracer.emplace(this->context.runtime, this->context.gpu_scene, *this->pathtracer_resources, scene);
            this->backends.active.reset();
        } else
            throw std::runtime_error(std::format("Unknown Scene Renderer: {}", id));
        this->backends.selected_id = id;
    }

    void RenderEngine::set_raster_display_mode(const RasterDisplayMode mode) noexcept {
        this->backends.raster_display_mode = mode;
        if (this->backends.rasterizer) this->backends.rasterizer->set_display_mode(mode);
    }

    RasterDisplayMode RenderEngine::raster_display_mode() const noexcept {
        return this->backends.raster_display_mode;
    }

    void RenderEngine::wait_for_pathtracer() {
        this->pathtracer_resources->wait_for_preparation();
        if (this->backends.pathtracer) this->backends.pathtracer->wait_for_preparation();
    }

    RendererDescriptor RenderEngine::active_descriptor() const {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit([](const auto active) { return std::remove_cvref_t<decltype(active.get())>::descriptor; }, *this->backends.active);
    }

    RendererDescriptor RenderEngine::selected_descriptor() const noexcept {
        return this->backends.selected_id == pathtracer_descriptor.id ? pathtracer_descriptor : rasterizer_descriptor;
    }

    bool RenderEngine::ready() const noexcept {
        return this->backends.active.has_value();
    }

    std::optional<PathTracerPreparationProgress> RenderEngine::pathtracer_preparation() const {
        if (this->backends.selected_id != pathtracer_descriptor.id || this->backends.active) return std::nullopt;
        const PathTracerPreparationProgress runtime_progress = this->pathtracer_resources->preparation_progress();
        if (runtime_progress.stage != PathTracerPreparationStage::Ready) return runtime_progress;
        return this->backends.pathtracer->preparation_progress();
    }

    std::optional<DepthBufferView> RenderEngine::depth_buffer() noexcept {
        if (!this->backends.active) return std::nullopt;
        return std::visit(
            [](const auto active) -> DepthBufferView {
                auto& renderer = active.get();
                if constexpr (std::same_as<std::remove_cvref_t<decltype(renderer)>, Rasterizer>)
                    return {renderer.renderer.depth_image, renderer.renderer.sampled_depth_descriptor, renderer.renderer.depth_layout};
                else
                    return renderer.depth_buffer();
            },
            *this->backends.active);
    }

    void RenderEngine::invalidate(const scene::SceneChange changes, const GpuSceneUpdate gpu_update) noexcept {
        if (this->backends.rasterizer) this->backends.rasterizer->invalidate(changes, gpu_update);
        if (this->backends.pathtracer) this->backends.pathtracer->invalidate(changes, gpu_update);
    }

    bool RenderEngine::prepare(const scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        if (this->backends.selected_id == pathtracer_descriptor.id && !this->backends.active) {
            if (!this->pathtracer_resources->complete_preparation()) return false;
            if (!this->backends.pathtracer->complete_preparation(scene, command_buffer)) return false;
            this->backends.active = std::ref(*this->backends.pathtracer);
        }
        if (!this->backends.active) return false;
        std::visit([&](const auto active) { active.get().prepare(scene, view, command_buffer); }, *this->backends.active);
        return true;
    }

    void RenderEngine::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        std::visit([&](const auto active) { active.get().record(command_buffer, frame_slot_index); }, *this->backends.active);
    }

    RenderOutput RenderEngine::output() const {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit([](const auto active) { return active.get().output(); }, *this->backends.active);
    }

    std::optional<RenderProgress> RenderEngine::progress() const noexcept {
        if (!this->backends.active) return std::nullopt;
        return std::visit(
            [](const auto active_reference) -> std::optional<RenderProgress> {
                const auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    return active.progress();
                else
                    return std::nullopt;
            },
            *this->backends.active);
    }

    void RenderEngine::set_paused(const bool paused) {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        std::visit(
            [paused](const auto active_reference) {
                auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    active.set_paused(paused);
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            *this->backends.active);
    }

    void RenderEngine::reset() {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        std::visit(
            [](const auto active_reference) {
                auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    active.reset();
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            *this->backends.active);
    }

    bool RenderEngine::gbuffer_available() const noexcept {
        if (!this->backends.active) return false;
        return std::visit([](const auto active) { return GBufferSceneRenderer<std::remove_cvref_t<decltype(active.get())>>; }, *this->backends.active);
    }

    void RenderEngine::record_gbuffer_readback(const vk::raii::CommandBuffer& command_buffer, RenderGBufferSnapshot& snapshot) {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        std::visit(
            [&](const auto active_reference) {
                auto& active = active_reference.get();
                if constexpr (GBufferSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    active.record_readback(command_buffer, snapshot);
                else
                    throw std::runtime_error("The active Scene Renderer does not expose a GBuffer");
            },
            *this->backends.active);
    }

    RenderGBufferReadback RenderEngine::readback() {
        if (!this->backends.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit(
            [](const auto active_reference) -> RenderGBufferReadback {
                auto& active = active_reference.get();
                if constexpr (GBufferSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    return active.readback();
                else
                    throw std::runtime_error("The active Scene Renderer does not expose a GBuffer");
            },
            *this->backends.active);
    }
} // namespace spectra
