import std;
import spectra.scene;
import xayah.spectra;
import xayah.spectra.pathtracer;
import xayah.spectra.rasterizer;

int main(const int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: spectra <scene-name>");
        spectra::scene::SceneWorkspace scene_workspace = spectra::scene::BuildScene(argv[1]);
        xayah::Spectra spectra{std::move(scene_workspace), "Spectra"};
        spectra.register_renderer(xayah::SpectraPathtracer{});
        spectra.register_renderer(xayah::SpectraRasterizer{});
        spectra.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
