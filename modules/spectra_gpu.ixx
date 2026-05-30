export module spectra.gpu;
import std;
import spectra;

export namespace xayah {
    [[nodiscard]] std::unique_ptr<SpectraPlugin> create_spectra_gpu_interactive_plugin(std::filesystem::path scene_path);
} // namespace xayah
