import std;
import spectra;
import spectra.scene.library;
import spectra.pathtracer;
import spectra.rasterizer;

int main(const int argc, char**) {
    try {
        if (argc != 1) throw std::runtime_error("usage: spectra");

        std::shared_ptr<spectra::scene::SceneLibrary> scene_library = std::make_shared<spectra::scene::SceneLibrary>();
        scene_library->register_translation_target(spectra::pathtracer::PathtracerRenderer::translation_target());
        scene_library->register_translation_target(spectra::rasterizer::RasterizerRenderer::translation_target());
        scene_library->load_first_supported_scene(spectra::pathtracer::PathtracerRenderer::target_name());

        std::shared_ptr<spectra::scene::SceneWorkspace> document_workspace = scene_library->document_workspace();
        spectra::Spectra app{"Spectra"};
        app.set_renderer_availability_callback([scene_library](const std::string_view renderer_name) { return scene_library->renderer_availability(renderer_name); });
        app.set_renderer_activation_callback([scene_library](const std::string_view renderer_name) { scene_library->set_active_renderer(renderer_name); });
        scene_library->attach(app);
        app.register_renderer(spectra::pathtracer::PathtracerRenderer{document_workspace});
        app.register_renderer(spectra::rasterizer::RasterizerRenderer{std::move(document_workspace)});
        app.run();
        scene_library->detach();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
