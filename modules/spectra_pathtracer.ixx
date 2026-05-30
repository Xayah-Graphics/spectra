export module spectra.pathtracer;
import std;
import spectra;

export namespace xayah {
    [[nodiscard]] std::unique_ptr<SpectraPlugin> create_spectra_pathtracer_plugin(std::filesystem::path scene_path);
} // namespace xayah
