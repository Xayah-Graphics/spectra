module spectra.render;

import std;

namespace spectra {
    Renderers::Renderers(VulkanRuntime& runtime, GpuScene& gpu_scene, std::filesystem::path shader_directory, std::optional<std::string> initial_renderer) : context{runtime, gpu_scene, std::move(shader_directory)} {
        if (initial_renderer) {
            if (*initial_renderer != Rasterizer::descriptor.id && *initial_renderer != PathTracer::descriptor.id) throw std::runtime_error(std::format("Unknown Scene Renderer: {}", *initial_renderer));
            this->renderers.rasterizer_enabled  = *initial_renderer == Rasterizer::descriptor.id;
            this->renderers.path_tracer_enabled = *initial_renderer == PathTracer::descriptor.id;
            this->renderers.active_id           = std::move(*initial_renderer);
        }
        this->refresh_enabled_descriptors();
    }

    Renderers::~Renderers() {
        this->destroy();
    }

    void Renderers::rebuild(const scene::SceneView scene) {
        this->destroy();
        if (this->renderers.rasterizer_enabled) this->renderers.rasterizer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
        if (this->renderers.path_tracer_enabled) this->renderers.path_tracer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
        this->activate(this->renderers.active_id);
        this->refresh_enabled_descriptors();
    }

    void Renderers::destroy() noexcept {
        this->renderers.active = std::monostate{};
        this->renderers.path_tracer.reset();
        this->renderers.rasterizer.reset();
    }

    void Renderers::enable(const std::string_view id, const scene::SceneView scene) {
        if (id == Rasterizer::descriptor.id) {
            if (this->renderers.rasterizer) return;
            this->renderers.rasterizer_enabled = true;
            this->renderers.rasterizer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
        } else if (id == PathTracer::descriptor.id) {
            if (this->renderers.path_tracer) return;
            this->renderers.path_tracer_enabled = true;
            this->renderers.path_tracer.emplace(this->context.runtime, this->context.gpu_scene, scene, this->context.shader_directory);
        } else
            throw std::runtime_error(std::format("Unknown Scene Renderer: {}", id));
        if (std::holds_alternative<std::monostate>(this->renderers.active)) this->activate(id);
        this->refresh_enabled_descriptors();
    }

    void Renderers::disable(const std::string_view id, const std::string_view replacement_id) {
        if (this->renderers.enabled_count == 1) throw std::runtime_error("Spectra requires at least one enabled Scene Renderer");
        if (this->active_descriptor().id == id) {
            if (replacement_id.empty() || replacement_id == id) throw std::runtime_error("Disabling the active Scene Renderer requires an explicit enabled replacement");
            this->activate(replacement_id);
        }
        if (id == Rasterizer::descriptor.id) {
            this->renderers.rasterizer.reset();
            this->renderers.rasterizer_enabled = false;
        } else if (id == PathTracer::descriptor.id) {
            this->renderers.path_tracer.reset();
            this->renderers.path_tracer_enabled = false;
        } else
            throw std::runtime_error(std::format("Unknown Scene Renderer: {}", id));
        this->refresh_enabled_descriptors();
    }

    void Renderers::activate(const std::string_view id) {
        if (id == Rasterizer::descriptor.id && this->renderers.rasterizer)
            this->renderers.active = &*this->renderers.rasterizer;
        else if (id == PathTracer::descriptor.id && this->renderers.path_tracer)
            this->renderers.active = &*this->renderers.path_tracer;
        else
            throw std::runtime_error(std::format("Scene Renderer is not enabled: {}", id));
        this->renderers.active_id = id;
    }

    bool Renderers::enabled(const std::string_view id) const noexcept {
        if (id == Rasterizer::descriptor.id) return this->renderers.rasterizer.has_value();
        if (id == PathTracer::descriptor.id) return this->renderers.path_tracer.has_value();
        return false;
    }

    std::span<const RendererDescriptor> Renderers::enabled_renderers() const noexcept {
        return {this->renderers.enabled_descriptors.data(), this->renderers.enabled_count};
    }

    RendererDescriptor Renderers::active_descriptor() const {
        return std::visit(
            [](const auto active) -> RendererDescriptor {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else
                    return std::remove_pointer_t<decltype(active)>::descriptor;
            },
            this->renderers.active);
    }

    bool Renderers::active_renders_visualizations() const {
        return std::visit(
            [](const auto active) -> bool {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else
                    return std::remove_pointer_t<decltype(active)>::renders_visualizations;
            },
            this->renderers.active);
    }

    void Renderers::invalidate(const scene::SceneChange changes) noexcept {
        if (this->renderers.rasterizer) this->renderers.rasterizer->invalidate(changes);
        if (this->renderers.path_tracer) this->renderers.path_tracer->invalidate(changes);
    }

    void Renderers::prepare(const scene::SceneView scene, const RenderView& view, const vk::raii::CommandBuffer& command_buffer) {
        std::visit(
            [&](const auto active) {
                if constexpr (!std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    active->prepare(scene, view, command_buffer);
                else
                    throw std::runtime_error("No active Scene Renderer");
            },
            this->renderers.active);
    }

    void Renderers::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_slot_index) {
        std::visit(
            [&](const auto active) {
                if constexpr (!std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    active->record(command_buffer, frame_slot_index);
                else
                    throw std::runtime_error("No active Scene Renderer");
            },
            this->renderers.active);
    }

    RenderOutput Renderers::output() const {
        return std::visit(
            [](const auto active) -> RenderOutput {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else
                    return active->output();
            },
            this->renderers.active);
    }

    std::optional<RenderProgress> Renderers::progress() const noexcept {
        return std::visit(
            [](const auto active) -> std::optional<RenderProgress> {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    return std::nullopt;
                else if constexpr (ProgressiveSceneRenderer<std::remove_pointer_t<decltype(active)>>)
                    return active->progress();
                else
                    return std::nullopt;
            },
            this->renderers.active);
    }

    void Renderers::set_paused(const bool paused) {
        std::visit(
            [paused](const auto active) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else if constexpr (ProgressiveSceneRenderer<std::remove_pointer_t<decltype(active)>>)
                    active->set_paused(paused);
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            this->renderers.active);
    }

    void Renderers::reset() {
        std::visit(
            [](const auto active) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else if constexpr (ProgressiveSceneRenderer<std::remove_pointer_t<decltype(active)>>)
                    active->reset();
                else
                    throw std::runtime_error("The active Scene Renderer is not progressive");
            },
            this->renderers.active);
    }

    bool Renderers::gbuffer_available() const noexcept {
        return std::visit(
            [](const auto active) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    return false;
                else
                    return GBufferSceneRenderer<std::remove_pointer_t<decltype(active)>>;
            },
            this->renderers.active);
    }

    RenderGBufferReadback Renderers::readback() {
        return std::visit(
            [](const auto active) -> RenderGBufferReadback {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(active)>, std::monostate>)
                    throw std::runtime_error("No active Scene Renderer");
                else if constexpr (GBufferSceneRenderer<std::remove_pointer_t<decltype(active)>>)
                    return active->readback();
                else
                    throw std::runtime_error("The active Scene Renderer does not expose a GBuffer");
            },
            this->renderers.active);
    }

    void Renderers::refresh_enabled_descriptors() noexcept {
        this->renderers.enabled_count = 0;
        if (this->renderers.rasterizer_enabled) this->renderers.enabled_descriptors[this->renderers.enabled_count++] = Rasterizer::descriptor;
        if (this->renderers.path_tracer_enabled) this->renderers.enabled_descriptors[this->renderers.enabled_count++] = PathTracer::descriptor;
    }

} // namespace spectra
