export module spectra.runtime.shaders;

import std;

namespace spectra {
    export [[nodiscard]] std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path);
} // namespace spectra
