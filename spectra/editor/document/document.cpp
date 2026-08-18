module spectra.editor.document;

import spectra.scene.io;
import std;

namespace spectra::editor {
    void Document::save() {
        scene::save_scene(this->authored, this->path, this->path);
        this->modified = false;
    }

    void Document::save_as(const std::filesystem::path& scene_path) {
        if (!scene_path.parent_path().empty()) std::filesystem::create_directories(scene_path.parent_path());
        scene::save_scene(this->authored, scene_path, this->path);
        this->path     = scene_path;
        this->modified = false;
    }

    void Document::update_simulation_parameters(const std::size_t system_index, std::vector<scene::SimulationParameterSetting> parameters) {
        this->authored.simulation->systems[system_index].parameters  = parameters;
        this->evaluated.simulation->systems[system_index].parameters = std::move(parameters);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void Document::update_transform(const scene::InstanceId instance_id, math::Transform transform) {
        scene::Instance& source    = *std::ranges::find(this->authored.resources.instances, instance_id, &scene::Instance::id);
        scene::Instance& evaluated = *std::ranges::find(this->evaluated.resources.instances, instance_id, &scene::Instance::id);
        source.transform           = transform;
        evaluated.transform        = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Transform);
    }

    void Document::update_camera_transform(const scene::CameraId camera_id, math::Transform transform) {
        scene::Camera& source    = *std::ranges::find(this->authored.resources.cameras, camera_id, &scene::Camera::id);
        scene::Camera& evaluated = *std::ranges::find(this->evaluated.resources.cameras, camera_id, &scene::Camera::id);
        source.transform         = transform;
        evaluated.transform      = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Camera);
    }

    void Document::update_light_transform(const scene::LightId light_id, math::Transform transform) {
        const auto apply = [&transform](scene::Light& resource) {
            std::visit(
                [&transform](auto& data) {
                    if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>) data.environment.transform = transform;
                    else if constexpr (!std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>) data.transform = transform;
                },
                resource.data);
            ++resource.revision.content;
        };
        apply(*std::ranges::find(this->authored.resources.lights, light_id, &scene::Light::id));
        apply(*std::ranges::find(this->evaluated.resources.lights, light_id, &scene::Light::id));
        this->mark_change(scene::SceneChange::Light);
    }

    void Document::update_volume_transform(const scene::VolumeId volume_id, math::Transform transform) {
        scene::Volume& source    = *std::ranges::find(this->authored.resources.volumes, volume_id, &scene::Volume::id);
        scene::Volume& evaluated = *std::ranges::find(this->evaluated.resources.volumes, volume_id, &scene::Volume::id);
        source.transform         = transform;
        evaluated.transform      = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Volume);
    }

    void Document::update_particle_set_transform(const scene::ParticleSetId particle_set_id, math::Transform transform) {
        scene::ParticleSet& source    = *std::ranges::find(this->authored.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        scene::ParticleSet& evaluated = *std::ranges::find(this->evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        source.transform              = transform;
        evaluated.transform           = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Particle);
    }

    void Document::update_volume_diagnostics(const scene::VolumeId volume_id, scene::VolumeDiagnostics diagnostics) {
        std::ranges::find(this->authored.resources.volumes, volume_id, &scene::Volume::id)->diagnostics  = diagnostics;
        std::ranges::find(this->evaluated.resources.volumes, volume_id, &scene::Volume::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void Document::update_particle_set_visualization(const scene::ParticleSetId particle_set_id, scene::ParticleVisualization visualization) {
        std::ranges::find(this->authored.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->visualization  = visualization;
        std::ranges::find(this->evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->visualization = std::move(visualization);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void Document::update_particle_set_diagnostics(const scene::ParticleSetId particle_set_id, scene::ParticleDiagnostics diagnostics) {
        std::ranges::find(this->authored.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->diagnostics  = diagnostics;
        std::ranges::find(this->evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void Document::update_neural_field_diagnostics(const scene::NeuralFieldId neural_field_id, scene::NeuralFieldDiagnostics diagnostics) {
        std::ranges::find(this->authored.resources.neural_fields, neural_field_id, &scene::NeuralField::id)->diagnostics  = diagnostics;
        std::ranges::find(this->evaluated.resources.neural_fields, neural_field_id, &scene::NeuralField::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void Document::mark_change(const scene::SceneChange changes) noexcept {
        this->modified = true;
        this->authored.mark_changed(changes);
        this->evaluated.mark_changed(changes);
    }
} // namespace spectra::editor
