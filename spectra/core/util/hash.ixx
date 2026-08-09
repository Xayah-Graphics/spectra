export module spectra.util.hash;

import std;

namespace spectra {
    export [[nodiscard]] std::string sha256_hex(std::span<const std::span<const std::byte>> blocks);
    export [[nodiscard]] std::string sha256_file(const std::filesystem::path& path);
} // namespace spectra
