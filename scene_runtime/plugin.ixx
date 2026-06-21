export module spectra.scene_runtime.plugin;

export import spectra.scene_runtime.host_services;
import std;

namespace spectra::scene_runtime {
    export struct DynamicSceneOpenRequest {
        std::filesystem::path plugin_path{};
        std::vector<DynamicSceneOption> options{};
        std::shared_ptr<DynamicSceneHostServices> host_services{};
    };

    export struct DynamicScenePluginSource {
        std::string id{};
        std::string title{};
        std::filesystem::path path{};
        std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source{};
    };

    export struct DynamicScenePluginInfo {
        std::string id{};
        std::string title{};
        std::string controls_panel_title{};
        std::string open_action_label{};
        std::string open_action_description{};
        std::filesystem::path path{};
        std::vector<DynamicSceneOptionSchema> open_options{};
        std::vector<DynamicSceneControlAction> control_actions{};
        std::vector<DynamicSceneOptionSchema> control_settings{};
    };

    export [[nodiscard]] bool is_dynamic_scene_plugin_file(const std::filesystem::path& path);
    export [[nodiscard]] DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path);
    export [[nodiscard]] DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request);
} // namespace spectra::scene_runtime
