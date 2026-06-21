module spectra.scene_runtime.plugin;

import std;
import spectra.scene_runtime.plugin_conversion;
import spectra.scene_runtime.plugin_library;

namespace spectra::scene_runtime {
    bool is_dynamic_scene_plugin_file(const std::filesystem::path& path) {
#if defined(_WIN32)
        return path_extension_is(path, ".dll");
#elif defined(__APPLE__)
        return path_extension_is(path, ".dylib");
#else
        return path_extension_is(path, ".so");
#endif
    }

    DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path) {
        DynamicScenePluginLibrary plugin{plugin_path};
        return DynamicScenePluginInfo{
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

    DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request) {
        std::shared_ptr<DynamicScenePluginLibrary> plugin = std::make_shared<DynamicScenePluginLibrary>(std::move(request.plugin_path), std::move(request.options), std::move(request.host_services));
        return DynamicScenePluginSource{
            .id = plugin->source_id(),
            .title = plugin->title(),
            .path = plugin->path(),
            .create_source = [plugin = std::move(plugin)] {
                return make_dynamic_scene_plugin_source_instance(plugin);
            },
        };
    }
} // namespace spectra::scene_runtime
