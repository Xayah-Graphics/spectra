module spectra.render;

import std;

namespace spectra::render {
    Engine::Engine(runtime::VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, const std::optional<RendererKind> initial_renderer, const RasterDisplayMode initial_raster_display_mode) : context{runtime, gpu_scene, std::move(shader_directory), pathtracer_directory} {
        this->backends.raster_display_mode = initial_raster_display_mode;
        if (initial_renderer) this->backends.selected = *initial_renderer;
    }

    Engine::~Engine() {
        this->destroy();
    }

    void Engine::rebuild(const scene::ResolvedSceneView scene) {
        const RendererKind selected = this->backends.selected;
        this->destroy();
        this->activate(selected, scene);
    }

    void Engine::destroy() noexcept {
        this->backends.ready = false;
        this->backends.pathtracer.reset();
        this->backends.rasterizer.reset();
    }

    void Engine::activate(const RendererKind renderer, const scene::ResolvedSceneView scene) {
        if (renderer == RendererKind::Rasterizer) {
            if (!this->backends.rasterizer) this->backends.rasterizer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
            this->backends.rasterizer->set_display_mode(this->backends.raster_display_mode);
            this->backends.ready = true;
        } else {
            if (!this->context.runtime.device.ray_tracing_supported) throw std::runtime_error("The Path Tracer requires the complete Vulkan KHR ray-tracing profile");
            if (!this->pathtracer_resources) this->pathtracer_resources.emplace(this->context.runtime, this->context.pathtracer_directory);
            if (!this->backends.pathtracer) this->backends.pathtracer.emplace(this->context.runtime, this->context.gpu_scene, *this->pathtracer_resources, scene);
            this->backends.ready = false;
        }
        this->backends.selected = renderer;
    }

    void Engine::set_raster_display_mode(const RasterDisplayMode mode) noexcept {
        this->backends.raster_display_mode = mode;
        if (this->backends.rasterizer) this->backends.rasterizer->set_display_mode(mode);
    }

    RasterDisplayMode Engine::raster_display_mode() const noexcept {
        return this->backends.raster_display_mode;
    }

    void Engine::wait_for_pathtracer() {
        this->pathtracer_resources->wait_for_preparation();
        if (this->backends.pathtracer) this->backends.pathtracer->wait_for_preparation();
    }

    RendererDescriptor Engine::selected_descriptor() const noexcept {
        return renderer_descriptor(this->backends.selected);
    }

    std::optional<PathTracerPreparationProgress> Engine::pathtracer_preparation() const {
        if (this->backends.selected != RendererKind::PathTracer || this->backends.ready) return std::nullopt;
        const PathTracerPreparationProgress runtime_progress = this->pathtracer_resources->preparation_progress();
        if (runtime_progress.stage != PathTracerPreparationStage::Ready) return runtime_progress;
        return this->backends.pathtracer->preparation_progress();
    }

    std::optional<DepthBufferView> Engine::depth_buffer() noexcept {
        if (!this->backends.ready) return std::nullopt;
        if (this->backends.selected == RendererKind::Rasterizer) return this->backends.rasterizer->depth_buffer();
        return this->backends.pathtracer->depth_buffer();
    }

    void Engine::invalidate(const scene::SceneChange changes, const GpuSceneUpdate gpu_update) noexcept {
        if (this->backends.rasterizer) this->backends.rasterizer->invalidate(changes, gpu_update);
        if (this->backends.pathtracer) this->backends.pathtracer->invalidate(changes, gpu_update);
    }

    bool Engine::prepare(const scene::ResolvedSceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        if (this->backends.selected == RendererKind::PathTracer && !this->backends.ready) {
            if (!this->pathtracer_resources->complete_preparation()) return false;
            if (!this->backends.pathtracer->complete_preparation(scene, command_buffer)) return false;
            this->backends.ready = true;
        }
        if (!this->backends.ready) return false;
        if (this->backends.selected == RendererKind::Rasterizer) this->backends.rasterizer->prepare(scene, view, command_buffer);
        else this->backends.pathtracer->prepare(scene, view, command_buffer);
        return true;
    }

    void Engine::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (this->backends.selected == RendererKind::Rasterizer) this->backends.rasterizer->record(command_buffer, frame_slot_index);
        else this->backends.pathtracer->record(command_buffer, frame_slot_index);
    }

    RenderOutput Engine::output() const {
        if (this->backends.selected == RendererKind::Rasterizer) return this->backends.rasterizer->output();
        return this->backends.pathtracer->output();
    }

    std::optional<RenderProgress> Engine::progress() const noexcept {
        if (!this->backends.ready || this->backends.selected != RendererKind::PathTracer) return std::nullopt;
        return this->backends.pathtracer->progress();
    }

    void Engine::set_paused(const bool paused) {
        this->backends.pathtracer->set_paused(paused);
    }

    void Engine::reset() {
        this->backends.pathtracer->reset();
    }

    RenderGBufferReadback Engine::readback() {
        return this->backends.pathtracer->readback();
    }
} // namespace spectra::render
