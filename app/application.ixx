export module spectra.application;

import std;

namespace spectra {
    export void run_application(
        const std::filesystem::path& scene_path,
        const std::optional<std::filesystem::path>& plugin_path,
        const std::filesystem::path& shader_directory,
        std::optional<std::uint64_t> maximum_frame_count = std::nullopt,
        bool start_pathtracer = false);
} // namespace spectra
