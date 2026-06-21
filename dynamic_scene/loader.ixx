export module spectra.dynamic_scene.loader;

export import spectra.dynamic_scene.host;
import std;

namespace spectra::dynamic_scene {
    export struct OpenRequest {
        std::filesystem::path plugin_path{};
        std::vector<Option> options{};
        std::shared_ptr<HostServices> host{};
    };

    export struct PluginSource {
        std::string id{};
        std::string title{};
        std::filesystem::path path{};
        std::move_only_function<std::unique_ptr<SourceInstance>()> create_source{};
    };

    export struct PluginInfo {
        std::string id{};
        std::string title{};
        std::string controls_panel_title{};
        std::string open_action_label{};
        std::string open_action_description{};
        std::filesystem::path path{};
        std::vector<OptionSchema> open_options{};
        std::vector<ControlAction> control_actions{};
        std::vector<OptionSchema> control_settings{};
    };

    export [[nodiscard]] bool is_plugin_file(const std::filesystem::path& path);
    export [[nodiscard]] PluginInfo inspect_plugin(const std::filesystem::path& plugin_path);
    export [[nodiscard]] PluginSource load_plugin(OpenRequest request);
} // namespace spectra::dynamic_scene
