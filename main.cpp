import std;
import spectra;
import spectra.pathtracer.renderer;
import spectra.rasterizer.renderer;
import spectra.scene.ui;
import xayah.util.xcli;

namespace {
    static_assert(spectra::pathtracer::Host<spectra::Spectra>);
    static_assert(spectra::rasterizer::Host<spectra::Spectra>);
    static_assert(spectra::RendererFor<spectra::pathtracer::Renderer, spectra::Spectra>);
    static_assert(spectra::RendererFor<spectra::rasterizer::Renderer, spectra::Spectra>);
} // namespace

int main(const int argc, const char* const* const argv) {
    try {
        const std::span arguments{argv, static_cast<std::size_t>(argc)};
        std::optional<std::string> initial_scene_path{};
        xayah::util::Command command =
            xayah::util::Command{"Open the Spectra visualization workspace."}
            | xayah::util::option({.long_name = "scene", .value_name = "scene-file-or-plugin-path", .description = "PBRT scene file or scene plugin path", .show_default = false}, initial_scene_path)
            | xayah::util::example("--scene path/to/scene.pbrt")
            | xayah::util::example("--scene path/to/plugin.dll");
        const std::string usage = command.help(arguments);

        const auto cli_result = command.parse(arguments);
        if (!cli_result) {
            std::cerr << "error: " << cli_result.error() << '\n' << usage << std::endl;
            return 2;
        }
        if (cli_result->help_requested) {
            std::cout << usage << std::endl;
            return 0;
        }

        spectra::scene::SceneUi scene_ui{};
        spectra::Spectra app{"Spectra"};
        app.register_renderer(std::make_shared<spectra::rasterizer::Renderer>(scene_ui.scene(), scene_ui.camera_workspace()));
        app.register_renderer(std::make_shared<spectra::pathtracer::Renderer>(scene_ui.scene(), scene_ui.camera_workspace()));
        scene_ui.register_to(app);
        scene_ui.open_startup_file(app, initial_scene_path);
        app.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
    return 0;
}
