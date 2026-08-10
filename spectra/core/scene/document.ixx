export module spectra.scene.document;

import spectra.scene;
import std;

namespace spectra {
    export struct SceneDocument {
        struct {
            bool loaded{};
            bool modified{};
            scene::Scene source{};
            scene::Scene evaluated{};
            std::filesystem::path path{};
        } content;

        void save();
        void save_as(const std::filesystem::path& scene_path);

        void mark_change(scene::Scene& target_scene, scene::SceneChange changes) noexcept;
        void update_dynamic_system_parameters(scene::Scene& target_scene, std::size_t system_index, std::vector<scene::DynamicParameterSetting> parameters);
        void update_transform(scene::Scene& target_scene, scene::InstanceId instance_id, math::Transform transform);
        void update_camera_transform(scene::Scene& target_scene, scene::CameraId camera_id, math::Transform transform);
        void update_light_transform(scene::Scene& target_scene, scene::LightId light_id, math::Transform transform);
        void update_volume_transform(scene::Scene& target_scene, scene::VolumeId volume_id, math::Transform transform);
    };
} // namespace spectra
