export module spectra.scene.format;

import spectra.scene;
import std;

namespace spectra::scene {
    export [[nodiscard]] Scene load_scene(const std::filesystem::path& path);
    export void save_scene(
        Scene scene,
        const std::filesystem::path& path,
        const std::filesystem::path& source_path = {});
} // namespace spectra::scene
