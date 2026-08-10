module spectra.runtime.shaders;

import std;

namespace spectra {
    std::vector<std::uint32_t> load_spirv(const std::filesystem::path& path) {
        std::ifstream input{path, std::ios::binary | std::ios::ate};
        if (!input) throw std::runtime_error(std::format("Cannot open Spectra shader: {}", path.string()));
        const std::streamsize byte_count = input.tellg();
        if (byte_count <= 0 || byte_count % static_cast<std::streamsize>(sizeof(std::uint32_t)) != 0) throw std::runtime_error(std::format("Spectra shader has an invalid SPIR-V size: {}", path.string()));
        std::vector<std::uint32_t> words(static_cast<std::size_t>(byte_count) / sizeof(std::uint32_t));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(words.data()), byte_count);
        if (!input) throw std::runtime_error(std::format("Cannot read Spectra shader: {}", path.string()));
        return words;
    }
} // namespace spectra
