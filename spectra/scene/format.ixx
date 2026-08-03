export module spectra.scene:format;

import :model;
import std;

namespace spectra::scene {
    export struct SceneSummary {
        std::filesystem::path scene_path{};
        std::string name{};
        bool has_dynamics{};
        std::vector<std::string> provider_ids{};
    };

    export [[nodiscard]] SceneSummary inspect_scene(const std::filesystem::path& path);
    export [[nodiscard]] Scene load_scene(const std::filesystem::path& path);
    export void save_scene(Scene scene, const std::filesystem::path& path, const std::filesystem::path& source_scene_path = {});
} // namespace spectra::scene
