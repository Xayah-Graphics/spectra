import spectra;
import std;

int main(const int argc, char** argv) {
    try {
        xayah::PbrtDocument document;
        if (argc > 1)
            document.load(std::filesystem::path{argv[1]});
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
