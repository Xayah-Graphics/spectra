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
        try {
            if (this->content.source.dynamic_setup && std::filesystem::absolute(scene_path.parent_path()).lexically_normal() != std::filesystem::absolute(this->content.path.parent_path()).lexically_normal()) {
                std::vector<std::string> providers{};
                for (const scene::DynamicSystem& system : this->content.source.dynamic_setup->systems)
                    if (!std::ranges::contains(providers, system.provider_id)) providers.emplace_back(system.provider_id);
                const auto transaction = std::chrono::steady_clock::now().time_since_epoch().count();
                for (std::size_t index = 0; index != providers.size(); ++index) {
                    const std::string& provider           = providers[index];
                    const std::filesystem::path filename = scene::provider_library_filename(provider);
                    ProviderReplacement& replacement     = replacements.emplace_back();
                    replacement.destination              = scene_path.parent_path() / filename;
                    replacement.staged                   = replacement.destination;
                    replacement.staged += std::format(".spectra-save-{}-{}.tmp", transaction, index);
                    replacement.backup = replacement.destination;
                    replacement.backup += std::format(".spectra-save-{}-{}.bak", transaction, index);
                    std::filesystem::copy_file(this->content.path.parent_path() / filename, replacement.staged);
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
            for (auto replacement = replacements.rbegin(); replacement != replacements.rend(); ++replacement) {
                std::error_code cleanup_error{};
                if (replacement->installed) std::filesystem::remove(replacement->destination, cleanup_error);
                if (replacement->previous_moved) std::filesystem::rename(replacement->backup, replacement->destination, cleanup_error);
                std::filesystem::remove(replacement->staged, cleanup_error);
            }
            throw;
        }
        for (const ProviderReplacement& replacement : replacements) {
            std::error_code cleanup_error{};
            if (replacement.previous_moved) std::filesystem::remove(replacement.backup, cleanup_error);
        }
        this->content.path     = scene_path;
        this->content.modified = false;
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
        resource.transform      = std::move(transform);
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
} // namespace spectra
