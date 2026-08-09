module spectra.render;

import std;

namespace spectra {
    Renderers::Renderers(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, const std::filesystem::path& pathtracer_directory, std::optional<std::string> initial_renderer) : context{runtime, gpu_scene, std::move(shader_directory)}, pathtracer_runtime{runtime, pathtracer_directory} {
        if (initial_renderer) {
            if (*initial_renderer != Rasterizer::descriptor.id && *initial_renderer != PathTracer::descriptor.id) throw std::runtime_error(std::format("Unknown Scene Renderer: {}", *initial_renderer));
            this->renderers.selected_id = std::move(*initial_renderer);
        }
    }

    Renderers::~Renderers() {
        this->destroy();
    }

    void Renderers::rebuild(const scene::SceneView scene) {
        const std::string selected_id = this->renderers.selected_id;
        this->destroy();
        this->activate(selected_id, scene);
    }

    void Renderers::destroy() noexcept {
        this->renderers.active.reset();
        this->renderers.pathtracer.reset();
        this->renderers.rasterizer.reset();
    }

    void Renderers::activate(const std::string_view id, const scene::SceneView scene) {
        if (id == Rasterizer::descriptor.id) {
            if (!this->renderers.rasterizer) this->renderers.rasterizer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
            this->renderers.active = std::ref(*this->renderers.rasterizer);
        } else if (id == PathTracer::descriptor.id) {
            if (!this->renderers.pathtracer) this->renderers.pathtracer.emplace(this->context.runtime, this->context.gpu_scene, this->pathtracer_runtime, scene);
            this->renderers.active.reset();
        } else
            throw std::runtime_error(std::format("Unknown Scene Renderer: {}", id));
        this->renderers.selected_id = id;
    }

    void Renderers::wait_for_pathtracer() {
        this->pathtracer_runtime.wait_for_preparation();
        if (this->renderers.pathtracer) this->renderers.pathtracer->wait_for_preparation();
    }

    RendererDescriptor Renderers::active_descriptor() const {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit([](const auto active) { return std::remove_cvref_t<decltype(active.get())>::descriptor; }, *this->renderers.active);
    }

    RendererDescriptor Renderers::selected_descriptor() const noexcept {
        return this->renderers.selected_id == PathTracer::descriptor.id ? PathTracer::descriptor : Rasterizer::descriptor;
    }

    bool Renderers::ready() const noexcept {
        return this->renderers.active.has_value();
    }

    std::optional<PathTracerPreparationProgress> Renderers::pathtracer_preparation() const {
        if (this->renderers.selected_id != PathTracer::descriptor.id || this->renderers.active) return std::nullopt;
        const PathTracerPreparationProgress runtime_progress = this->pathtracer_runtime.preparation_progress();
        if (runtime_progress.stage != PathTracerPreparationStage::Ready) return runtime_progress;
        return this->renderers.pathtracer->preparation_progress();
    }

    bool Renderers::active_renders_visualizations() const {
        if (!this->renderers.active) return false;
        return std::visit([](const auto active) { return std::remove_cvref_t<decltype(active.get())>::renders_visualizations; }, *this->renderers.active);
    }

    void Renderers::invalidate(const scene::SceneChange changes) noexcept {
        if (this->renderers.rasterizer) this->renderers.rasterizer->invalidate(changes);
        if (this->renderers.pathtracer) this->renderers.pathtracer->invalidate(changes);
    }

    bool Renderers::prepare(const scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        if (this->renderers.selected_id == PathTracer::descriptor.id && !this->renderers.active) {
            if (!this->pathtracer_runtime.complete_preparation()) return false;
            if (!this->renderers.pathtracer->complete_preparation(scene, command_buffer)) return false;
            this->renderers.active = std::ref(*this->renderers.pathtracer);
        }
        if (!this->renderers.active) return false;
        std::visit([&](const auto active) { active.get().prepare(scene, view, command_buffer); }, *this->renderers.active);
        return true;
    }

    void Renderers::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        std::visit([&](const auto active) { active.get().record(command_buffer, frame_slot_index); }, *this->renderers.active);
    }

    RenderOutput Renderers::output() const {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit([](const auto active) { return active.get().output(); }, *this->renderers.active);
    }

    std::optional<RenderProgress> Renderers::progress() const noexcept {
        if (!this->renderers.active) return std::nullopt;
        return std::visit(
            [](const auto active_reference) -> std::optional<RenderProgress> {
                const auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    return active.progress();
                else
                    return std::nullopt;
            },
            *this->renderers.active);
    }

    void Renderers::set_paused(const bool paused) {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        std::visit(
            [paused](const auto active_reference) {
                auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    active.set_paused(paused);
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            *this->renderers.active);
    }

    void Renderers::reset() {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        std::visit(
            [](const auto active_reference) {
                auto& active = active_reference.get();
                if constexpr (ProgressiveSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    active.reset();
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            *this->renderers.active);
    }

    bool Renderers::gbuffer_available() const noexcept {
        if (!this->renderers.active) return false;
        return std::visit([](const auto active) { return GBufferSceneRenderer<std::remove_cvref_t<decltype(active.get())>>; }, *this->renderers.active);
    }

    RenderGBufferReadback Renderers::readback() {
        if (!this->renderers.active) throw std::runtime_error("No active Scene Renderer");
        return std::visit(
            [](const auto active_reference) -> RenderGBufferReadback {
                auto& active = active_reference.get();
                if constexpr (GBufferSceneRenderer<std::remove_cvref_t<decltype(active)>>)
                    return active.readback();
                else
                    throw std::runtime_error("The active Scene Renderer does not expose a GBuffer");
            },
            *this->renderers.active);
    }
} // namespace spectra
