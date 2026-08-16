module spectra.scene.document;

import spectra.scene.format;
import std;

namespace spectra {
    void SceneDocument::save() {
        scene::save_scene(this->content.source, this->content.path, this->content.path);
        this->content.modified = false;
    }

    void SceneDocument::save_as(const std::filesystem::path& scene_path) {
        struct ProviderReplacement {
            std::filesystem::path destination{};
            std::filesystem::path staged{};
            std::filesystem::path backup{};
            bool previous_moved{};
            bool installed{};
        };
        std::vector<ProviderReplacement> replacements{};
        if (!scene_path.parent_path().empty()) std::filesystem::create_directories(scene_path.parent_path());
        try {
            if (this->content.source.dynamic_setup && std::filesystem::absolute(scene_path.parent_path()).lexically_normal() != std::filesystem::absolute(this->content.path.parent_path()).lexically_normal()) {
                std::vector<std::filesystem::path> providers{};
                for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator{this->content.path.parent_path()}) {
#if defined(_WIN32)
                    if (entry.is_regular_file() && entry.path().filename().string().ends_with(".spectra-provider.dll")) providers.emplace_back(entry.path());
#else
                    if (entry.is_regular_file() && entry.path().filename().string().ends_with(".spectra-provider.so")) providers.emplace_back(entry.path());
#endif
                }
                std::ranges::sort(providers);
                const auto transaction = std::chrono::steady_clock::now().time_since_epoch().count();
                for (std::size_t index = 0; index != providers.size(); ++index) {
                    ProviderReplacement& replacement     = replacements.emplace_back();
                    replacement.destination              = scene_path.parent_path() / providers[index].filename();
                    replacement.staged                   = replacement.destination;
                    replacement.staged += std::format(".spectra-save-{}-{}.tmp", transaction, index);
                    replacement.backup = replacement.destination;
                    replacement.backup += std::format(".spectra-save-{}-{}.bak", transaction, index);
                    std::filesystem::copy_file(providers[index], replacement.staged);
                }
                for (ProviderReplacement& replacement : replacements) {
                    if (std::filesystem::exists(replacement.destination)) {
                        std::filesystem::rename(replacement.destination, replacement.backup);
                        replacement.previous_moved = true;
                    }
                    std::filesystem::rename(replacement.staged, replacement.destination);
                    replacement.installed = true;
                }
            }
            scene::save_scene(this->content.source, scene_path, this->content.path);
        } catch (...) {
            const std::exception_ptr original_error = std::current_exception();
            std::vector<std::string> rollback_errors{};
            for (auto replacement = replacements.rbegin(); replacement != replacements.rend(); ++replacement) {
                std::error_code cleanup_error{};
                if (replacement->installed) std::filesystem::remove(replacement->destination, cleanup_error);
                if (cleanup_error) rollback_errors.push_back(std::format("remove {}: {}", replacement->destination.string(), cleanup_error.message()));
                cleanup_error.clear();
                if (replacement->previous_moved) std::filesystem::rename(replacement->backup, replacement->destination, cleanup_error);
                if (cleanup_error) rollback_errors.push_back(std::format("restore {}: {}", replacement->destination.string(), cleanup_error.message()));
                cleanup_error.clear();
                std::filesystem::remove(replacement->staged, cleanup_error);
                if (cleanup_error) rollback_errors.push_back(std::format("remove {}: {}", replacement->staged.string(), cleanup_error.message()));
            }
            if (rollback_errors.empty()) std::rethrow_exception(original_error);
            try {
                std::rethrow_exception(original_error);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::format("{}; provider rollback failed: {}", error.what(), std::ranges::fold_left_first(rollback_errors, [](std::string left, const std::string& right) { return std::move(left) + "; " + right; }).value()));
            }
        }
        this->content.path     = scene_path;
        this->content.modified = false;
        std::vector<std::string> cleanup_errors{};
        for (const ProviderReplacement& replacement : replacements) {
            std::error_code cleanup_error{};
            if (replacement.previous_moved) std::filesystem::remove(replacement.backup, cleanup_error);
            if (cleanup_error) cleanup_errors.emplace_back(std::format("{}: {}", replacement.backup.string(), cleanup_error.message()));
        }
        if (!cleanup_errors.empty()) throw std::runtime_error(std::format("Saved the Scene but failed to remove provider backups: {}", std::ranges::fold_left_first(cleanup_errors, [](std::string left, const std::string& right) { return std::move(left) + "; " + right; }).value()));
    }

    void SceneDocument::mark_change(scene::Scene& target_scene, const scene::SceneChange changes) noexcept {
        if (&target_scene == &this->content.source) this->content.modified = true;
        target_scene.mark_changed(changes);
    }

    void SceneDocument::update_dynamic_system_parameters(scene::Scene& target_scene, const std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters) {
        target_scene.dynamic_setup->systems[system_index].parameters = std::move(parameters);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }

    void SceneDocument::update_transform(scene::Scene& target_scene, const scene::InstanceId instance_id, math::Transform transform) {
        scene::Instance& resource = *std::ranges::find(target_scene.resources.instances, instance_id, &scene::Instance::id);
        resource.transform        = std::move(transform);
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Transform);
    }

    void SceneDocument::update_camera_transform(scene::Scene& target_scene, const scene::CameraId camera_id, math::Transform transform) {
        scene::Camera& resource = *std::ranges::find(target_scene.resources.cameras, camera_id, &scene::Camera::id);
        resource.transform      = transform;
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Camera);
    }

    void SceneDocument::update_light_transform(scene::Scene& target_scene, const scene::LightId light_id, math::Transform transform) {
        scene::Light& resource = *std::ranges::find(target_scene.resources.lights, light_id, &scene::Light::id);
        std::visit(
            [&transform](auto& data) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(data)>, scene::PortalInfiniteLight>)
                    data.environment.transform = transform;
                else if constexpr (!std::same_as<std::remove_cvref_t<decltype(data)>, scene::DiffuseAreaLight>)
                    data.transform = transform;
            },
            resource.data);
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Light);
    }

    void SceneDocument::update_volume_transform(scene::Scene& target_scene, const scene::VolumeId volume_id, math::Transform transform) {
        scene::Volume& resource = *std::ranges::find(target_scene.resources.volumes, volume_id, &scene::Volume::id);
        resource.transform      = std::move(transform);
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Volume);
    }

    void SceneDocument::update_particle_set_transform(scene::Scene& target_scene, const scene::ParticleSetId particle_set_id, math::Transform transform) {
        scene::ParticleSet& resource = *std::ranges::find(target_scene.resources.particle_sets, particle_set_id, &scene::ParticleSet::id);
        resource.transform           = std::move(transform);
        ++resource.revision.content;
        this->mark_change(target_scene, scene::SceneChange::Particle);
    }

    void SceneDocument::update_volume_diagnostics(scene::Scene& target_scene, const scene::VolumeId volume_id, scene::VolumeDiagnostics diagnostics) {
        std::ranges::find(target_scene.resources.volumes, volume_id, &scene::Volume::id)->diagnostics = std::move(diagnostics);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }

    void SceneDocument::update_particle_set_visualization(scene::Scene& target_scene, const scene::ParticleSetId particle_set_id, scene::ParticleVisualization visualization) {
        std::ranges::find(target_scene.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->visualization = std::move(visualization);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }

    void SceneDocument::update_particle_set_diagnostics(scene::Scene& target_scene, const scene::ParticleSetId particle_set_id, scene::ParticleDiagnostics diagnostics) {
        std::ranges::find(target_scene.resources.particle_sets, particle_set_id, &scene::ParticleSet::id)->diagnostics = std::move(diagnostics);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }

    void SceneDocument::update_neural_field_diagnostics(scene::Scene& target_scene, const scene::NeuralFieldId neural_field_id, scene::NeuralFieldDiagnostics diagnostics) {
        std::ranges::find(target_scene.resources.neural_fields, neural_field_id, &scene::NeuralField::id)->diagnostics = std::move(diagnostics);
        this->mark_change(target_scene, scene::SceneChange::Metadata);
    }
} // namespace spectra
