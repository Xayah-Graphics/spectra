import std;
import xayah.spectra;
import xayah.scene.library;
import xayah.renderer.pathtracer;
import xayah.renderer.rasterizer;

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra");

        std::shared_ptr<xayah::SceneLibrary> scene_library = std::make_shared<xayah::SceneLibrary>();
        scene_library->register_translation_target(xayah::PathtracerRenderer::translation_target());
        scene_library->register_translation_target(xayah::RasterizerRenderer::translation_target());
        scene_library->load_first_supported_scene(xayah::PathtracerRenderer::target_name());

        std::shared_ptr<xayah::scene::SceneWorkspace> document_workspace = scene_library->document_workspace();
        xayah::Spectra spectra{"Spectra"};
        spectra.set_renderer_availability_callback([scene_library](const std::string_view renderer_name) { return scene_library->renderer_availability(renderer_name); });
        spectra.set_renderer_activation_callback([scene_library](const std::string_view renderer_name) { scene_library->set_active_renderer(renderer_name); });
        scene_library->attach(spectra);
        spectra.register_renderer(xayah::PathtracerRenderer{document_workspace});
        spectra.register_renderer(xayah::RasterizerRenderer{std::move(document_workspace)});
        spectra.run();
        scene_library->detach();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
