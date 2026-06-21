export module spectra.scene.plugin_library;

import spectra.scene;
import std;

namespace spectra::scene {
    class PluginSceneDriver;

    export class PluginLibrary final {
    public:
        explicit PluginLibrary(std::filesystem::path plugin_path);
        PluginLibrary(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host);
        PluginLibrary(const PluginLibrary& other) = delete;
        PluginLibrary(PluginLibrary&& other) = delete;
        PluginLibrary& operator=(const PluginLibrary& other) = delete;
        PluginLibrary& operator=(PluginLibrary&& other) = delete;
        ~PluginLibrary() noexcept;

        [[nodiscard]] std::string id() const;
        [[nodiscard]] std::string title() const;
        [[nodiscard]] std::string controls_panel_title() const;
        [[nodiscard]] std::string open_action_label() const;
        [[nodiscard]] std::string open_action_description() const;
        [[nodiscard]] std::string scene_id() const;
        [[nodiscard]] const std::filesystem::path& path() const;
        [[nodiscard]] std::vector<ControlOptionSchema> open_options() const;
        [[nodiscard]] std::vector<ControlAction> control_actions() const;
        [[nodiscard]] std::vector<ControlOptionSchema> control_settings() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl{};

        friend class PluginSceneDriver;
    };

    export [[nodiscard]] std::unique_ptr<SceneDriver> make_plugin_driver(std::shared_ptr<PluginLibrary> plugin);
} // namespace spectra::scene
