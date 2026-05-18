import spectra;
import std;

int main(const int argc, char** argv) {
    try {
        xayah::SpectraScene document;
        if (argc > 1)
            throw std::runtime_error(std::format("PBRT import into SpectraScene is planned for stage 2: {}", std::filesystem::path{argv[1]}.string()));
        else
            document.create_default();

        xayah::Spectra spectra{"Spectra"};
        spectra.render(document);
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
