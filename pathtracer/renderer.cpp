module spectra.pathtracer;

import std;

namespace spectra::pathtracer {
    PathTracer::PathTracer(GpuDevice& gpu, const render::GpuAssetCache& assets, const scene::SceneView canonical_scene, const std::filesystem::path& shader_directory, const std::uint32_t frames_in_flight) : gpu(&gpu), shader_directory(shader_directory), path_scene(gpu, assets, canonical_scene), render_session(gpu, frames_in_flight) {}

    PathTracer::~PathTracer() = default;

    void PathTracer::prepare(const vk::Extent2D extent) {
        if (!this->integrator) this->integrator = std::make_unique<WavefrontIntegrator>(*this->gpu, this->shader_directory);
        this->render_session.resize(extent, this->path_scene.texture_stack_size, this->path_scene.material_texture_values);
    }

    void PathTracer::synchronize(const scene::SceneView scene, const vk::raii::CommandBuffer& command_buffer) {
        this->path_scene.synchronize(scene, command_buffer);
    }

    void PathTracer::record(const vk::raii::CommandBuffer& command_buffer, const std::uint32_t frame_index) {
        this->integrator->record(this->path_scene, this->render_session, command_buffer, frame_index);
    }

    void PathTracer::reset_accumulation() noexcept {
        this->render_session.reset();
    }

    void PathTracer::change_camera(const scene::CameraResource& camera) noexcept {
        this->path_scene.scene_camera = camera;
        this->render_session.reset();
    }

    render::RenderReadback PathTracer::readback() {
        return this->render_session.readback();
    }

    render::RenderOutput PathTracer::output() const noexcept {
        return this->render_session.output();
    }

    std::uint32_t PathTracer::accumulated_samples() const noexcept {
        return this->render_session.sample_index;
    }
} // namespace spectra::pathtracer
