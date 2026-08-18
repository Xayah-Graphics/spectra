export module spectra.editor.document;

import spectra.scene;
import std;

namespace spectra::editor {
    export struct Document {
        bool loaded{};
        bool modified{};
        scene::Scene authored{};
        scene::Scene evaluated{};
        std::filesystem::path path{};

        void save();
        void save_as(const std::filesystem::path& scene_path);

        void update_simulation_parameters(std::size_t system_index, std::vector<scene::SimulationParameterSetting> parameters);
        void update_transform(scene::InstanceId instance_id, math::Transform transform);
        void update_camera_transform(scene::CameraId camera_id, math::Transform transform);
        void update_light_transform(scene::LightId light_id, math::Transform transform);
        void update_volume_transform(scene::VolumeId volume_id, math::Transform transform);
        void update_particle_set_transform(scene::ParticleSetId particle_set_id, math::Transform transform);
        void update_volume_diagnostics(scene::VolumeId volume_id, scene::VolumeDiagnostics diagnostics);
        void update_particle_set_visualization(scene::ParticleSetId particle_set_id, scene::ParticleVisualization visualization);
        void update_particle_set_diagnostics(scene::ParticleSetId particle_set_id, scene::ParticleDiagnostics diagnostics);
        void update_neural_field_diagnostics(scene::NeuralFieldId neural_field_id, scene::NeuralFieldDiagnostics diagnostics);

    private:
        void mark_change(scene::SceneChange changes) noexcept;
    };
} // namespace spectra::editor
