module spectra.dynamic_scene.loader;

import std;
import spectra.dynamic_scene.plugin_decode;
import spectra.dynamic_scene.plugin_library;

namespace spectra::dynamic_scene {
    bool is_plugin_file(const std::filesystem::path& path) {
#if defined(_WIN32)
        return path_extension_is(path, ".dll");
#elif defined(__APPLE__)
        return path_extension_is(path, ".dylib");
#else
        return path_extension_is(path, ".so");
#endif
    }

    PluginInfo inspect_plugin(const std::filesystem::path& plugin_path) {
        PluginLibrary plugin{plugin_path};
        return PluginInfo{
            .id = plugin.id(),
            .title = plugin.title(),
            .controls_panel_title = plugin.controls_panel_title(),
            .open_action_label = plugin.open_action_label(),
            .open_action_description = plugin.open_action_description(),
            .path = plugin.path(),
            .open_options = plugin.open_options(),
            .control_actions = plugin.control_actions(),
            .control_settings = plugin.control_settings(),
        };
    }

    PluginSource load_plugin(OpenRequest request) {
        std::shared_ptr<PluginLibrary> plugin = std::make_shared<PluginLibrary>(std::move(request.plugin_path), std::move(request.options), std::move(request.host));
        return PluginSource{
            .id = plugin->source_id(),
            .title = plugin->title(),
            .path = plugin->path(),
            .create_source = [plugin = std::move(plugin)] {
                return make_plugin_source_instance(plugin);
            },
        };
    }
} // namespace spectra::dynamic_scene
