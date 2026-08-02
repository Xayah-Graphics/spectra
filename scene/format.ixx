export module spectra.scene.format;

import spectra.scene;
import std;

namespace spectra::scene {
    export struct SceneSummary {
        std::filesystem::path path{};
        std::string name{};
        bool dynamic{};
        std::vector<std::string> providers{};
    };

    export [[nodiscard]] SceneSummary inspect_scene(const std::filesystem::path& path);
    export [[nodiscard]] Scene load_scene(const std::filesystem::path& path);
    export void save_scene(Scene scene, const std::filesystem::path& path, const std::filesystem::path& source_path = {});
} // namespace spectra::scene
