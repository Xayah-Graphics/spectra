export module spectra.scene.plugin_host;

import spectra.scene;
import std;

namespace spectra::scene {
    class PluginSceneDriver;

    export class PluginHost final : public std::enable_shared_from_this<PluginHost> {
    public:
        explicit PluginHost(std::filesystem::path plugin_path);
        PluginHost(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host);
        PluginHost(const PluginHost& other) = delete;
        PluginHost(PluginHost&& other) = delete;
        PluginHost& operator=(const PluginHost& other) = delete;
        PluginHost& operator=(PluginHost&& other) = delete;
        ~PluginHost() noexcept;

        [[nodiscard]] PluginInfo info() const;
        [[nodiscard]] std::string scene_id() const;
        [[nodiscard]] std::unique_ptr<SceneDriver> create_driver();
        [[nodiscard]] static bool accepts_path(const std::filesystem::path& path);

    private:
        struct State;
        std::unique_ptr<State> state{};

        friend class PluginSceneDriver;
    };
} // namespace spectra::scene
