module;

#include <filesystem>

export module spectra.scene.usd;

import spectra.scene;

namespace spectra::scene {
    export [[nodiscard]] Scene load_usd(const std::filesystem::path& path);
    export void save_usd(const Scene& scene, const std::filesystem::path& path);
    export void save_physica_usd(const Scene& scene, const std::filesystem::path& path, const std::filesystem::path& base_scene_path);
} // namespace spectra::scene
