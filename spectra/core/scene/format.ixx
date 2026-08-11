export module spectra.scene.format;

import spectra.scene;
import std;

namespace spectra::scene {
    export enum class SceneSaveMode : std::uint8_t {
        PreserveSources,
        MaterializeAssets,
    };

    export [[nodiscard]] Scene load_scene(const std::filesystem::path& path);
    export void save_scene(const Scene& scene, const std::filesystem::path& path, const std::filesystem::path& source_scene_path = {}, SceneSaveMode mode = SceneSaveMode::PreserveSources);
} // namespace spectra::scene
