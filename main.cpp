import spectra;
import spectra.pathtracer;
import std;

int main(const int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: spectra [scene.pbrt]");
        xayah::Spectra spectra{"Spectra"};
        spectra.register_plugin(xayah::create_spectra_pathtracer_plugin(std::filesystem::path{argv[1]}));
        spectra.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
