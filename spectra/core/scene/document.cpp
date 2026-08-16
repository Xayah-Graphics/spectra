module spectra.scene.document;

import spectra.scene.format;
import std;

namespace spectra {
    void SceneDocument::save() {
        scene::save_scene(this->content.source, this->content.path, this->content.path);
        this->content.modified = false;
    }

    void SceneDocument::save_as(const std::filesystem::path& scene_path) {
        if (!scene_path.parent_path().empty()) std::filesystem::create_directories(scene_path.parent_path());
        scene::save_scene(this->content.source, scene_path, this->content.path);
        this->content.path     = scene_path;
        this->content.modified = false;
    }

    void SceneDocument::update_dynamic_system_parameters(const std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters) {
        this->content.source.dynamic_setup->systems[system_index].parameters    = parameters;
        this->content.evaluated.dynamic_setup->systems[system_index].parameters = std::move(parameters);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void SceneDocument::update_transform(const scene::InstanceId instance_id, math::Transform transform) {
        scene::Instance& source    = *std::ranges::find(this->content.source.resources.instances, instance_id, &scene::Instance::id);
        scene::Instance& evaluated = *std::ranges::find(this->content.evaluated.resources.instances, instance_id, &scene::Instance::id);
        source.transform           = transform;
        evaluated.transform        = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Transform);
    }

    void SceneDocument::update_camera_transform(const scene::CameraId camera_id, math::Transform transform) {
        scene::Camera& source    = *std::ranges::find(this->content.source.resources.cameras, camera_id, &scene::Camera::id);
        scene::Camera& evaluated = *std::ranges::find(this->content.evaluated.resources.cameras, camera_id, &scene::Camera::id);
        source.transform         = transform;
        evaluated.transform      = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Camera);
    }

    void SceneDocument::update_light_transform(const scene::LightId light_id, math::Transform transform) {
        const auto apply = [&transform](scene::Light& resource) {
        std::visit(
            [&transform](auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                    data.environment.transform = transform;
                else if constexpr (!std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                    data.transform = transform;
            },
            resource.data);
        ++resource.revision.content;
        };
        apply(*std::ranges::find(this->content.source.resources.lights, light_id, &scene::Light::id));
        apply(*std::ranges::find(this->content.evaluated.resources.lights, light_id, &scene::Light::id));
        this->mark_change(scene::SceneChange::Light);
    }

    void SceneDocument::update_volume_transform(const scene::VolumeId volume_id, math::Transform transform) {
        scene::Volume& source    = *std::ranges::find(this->content.source.resources.volumes, volume_id, &scene::Volume::id);
        scene::Volume& evaluated = *std::ranges::find(this->content.evaluated.resources.volumes, volume_id, &scene::Volume::id);
        source.transform         = transform;
        evaluated.transform      = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Volume);
    }

    void SceneDocument::update_particle_set_transform(const scene::ParticleSetId particle_set_id, math::Transform transform) {
        scene::ParticleSet& source    = *std::ranges::find(this->content.source.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        scene::ParticleSet& evaluated = *std::ranges::find(this->content.evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        source.transform              = transform;
        evaluated.transform           = std::move(transform);
        ++source.revision.content;
        ++evaluated.revision.content;
        this->mark_change(scene::SceneChange::Particle);
    }

    void SceneDocument::update_volume_diagnostics(const scene::VolumeId volume_id, scene::VolumeDiagnostics diagnostics) {
        std::ranges::find(this->content.source.resources.volumes, volume_id, &scene::Volume::id)->diagnostics    = diagnostics;
        std::ranges::find(this->content.evaluated.resources.volumes, volume_id, &scene::Volume::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void SceneDocument::update_particle_set_visualization(const scene::ParticleSetId particle_set_id, scene::ParticleVisualization visualization) {
        std::ranges::find(this->content.source.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->visualization    = visualization;
        std::ranges::find(this->content.evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->visualization = std::move(visualization);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void SceneDocument::update_particle_set_diagnostics(const scene::ParticleSetId particle_set_id, scene::ParticleDiagnostics diagnostics) {
        std::ranges::find(this->content.source.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->diagnostics    = diagnostics;
        std::ranges::find(this->content.evaluated.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void SceneDocument::update_neural_field_diagnostics(const scene::NeuralFieldId neural_field_id, scene::NeuralFieldDiagnostics diagnostics) {
        std::ranges::find(this->content.source.resources.neural_fields, neural_field_id, &scene::NeuralField::id)->diagnostics    = diagnostics;
        std::ranges::find(this->content.evaluated.resources.neural_fields, neural_field_id, &scene::NeuralField::id)->diagnostics = std::move(diagnostics);
        this->mark_change(scene::SceneChange::Metadata);
    }

    void SceneDocument::mark_change(const scene::SceneChange changes) noexcept {
        this->content.modified = true;
        this->content.source.mark_changed(changes);
        this->content.evaluated.mark_changed(changes);
    }
} // namespace spectra
