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

        [[nodiscard]] ScenePluginInfo info() const;
        [[nodiscard]] std::string scene_id() const;

    private:
        struct State;
        std::unique_ptr<State> state{};

        friend class PluginSceneDriver;
    };

    export [[nodiscard]] std::unique_ptr<SceneDriver> make_plugin_driver(std::shared_ptr<PluginLibrary> plugin);
} // namespace spectra::scene
