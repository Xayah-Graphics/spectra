import std;
import xayah.spectra;
import xayah.spectra.scene_session;
import xayah.spectra.pathtracer;
import xayah.spectra.rasterizer;

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra");

        std::shared_ptr<xayah::SpectraSceneSession> scene_session = std::make_shared<xayah::SpectraSceneSession>();
        scene_session->register_translation_target(xayah::SpectraPathtracer::translation_target());
        scene_session->register_translation_target(xayah::SpectraRasterizer::translation_target());
        scene_session->load_first_supported_scene(xayah::PathtracerSceneTranslator::target_name());

        std::shared_ptr<spectra::scene::SceneWorkspace> document_workspace = scene_session->document_workspace();
        xayah::Spectra spectra{"Spectra"};
        spectra.set_renderer_availability_callback([scene_session](const std::string_view renderer_name) { return scene_session->renderer_availability(renderer_name); });
        spectra.set_renderer_activation_callback([scene_session](const std::string_view renderer_name) { scene_session->set_active_renderer(renderer_name); });
        scene_session->attach(spectra);
        spectra.register_renderer(xayah::SpectraPathtracer{document_workspace});
        spectra.register_renderer(xayah::SpectraRasterizer{std::move(document_workspace)});
        spectra.run();
        scene_session->detach();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
