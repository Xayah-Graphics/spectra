module spectra;
import std;
import :runtime;

namespace xayah {
    Spectra::Spectra(const std::string_view& app_name, const std::string_view& engine_name, const std::uint32_t window_width, const std::uint32_t window_height) : state{std::make_unique<SpectraState>(app_name, engine_name, window_width, window_height)} {}

    Spectra::~Spectra() noexcept = default;

    void Spectra::run_interactive_scene(const std::filesystem::path& scene_path) {
        if (this->state == nullptr) throw std::runtime_error("Spectra state is unavailable");
        this->state->run_interactive_scene(scene_path);
    }
} // namespace xayah
