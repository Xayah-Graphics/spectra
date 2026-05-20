import spectra;
import std;

int main(const int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: spectra [scene.pbrt]");
        xayah::Spectra spectra{"Spectra"};
        spectra.run_interactive_scene(std::filesystem::path{argv[1]});
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
