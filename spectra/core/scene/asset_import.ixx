export module spectra.scene.asset_import;

import spectra.scene;
import std;

namespace spectra::scene {
    export void load_scene_sources(Scene& scene, const std::filesystem::path& root);
} // namespace spectra::scene
