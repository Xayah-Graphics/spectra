export module spectra.scene.plugin_host;

import spectra.scene;
import std;

namespace spectra::scene {
    export class PluginHost final : public std::enable_shared_from_this<PluginHost> {
    public:
        class Instance;

        explicit PluginHost(std::filesystem::path plugin_path);
        PluginHost(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host);
        PluginHost(const PluginHost& other) = delete;
        PluginHost(PluginHost&& other) = delete;
        PluginHost& operator=(const PluginHost& other) = delete;
        PluginHost& operator=(PluginHost&& other) = delete;
        ~PluginHost() noexcept;

        [[nodiscard]] PluginInfo info() const;
        [[nodiscard]] std::string scene_id() const;
        [[nodiscard]] std::shared_ptr<Instance> create_instance();
        void update(Instance& instance, const UpdateInfo& update) const;
        [[nodiscard]] std::uint64_t scene_revision(Instance& instance) const;
        void execute_control_action(Instance& instance, std::string_view action_id, std::span<const ControlOption> options) const;
        void update_control_setting(Instance& instance, std::string_view key, std::string_view value) const;
        [[nodiscard]] ControlState control_state(Instance& instance) const;
        [[nodiscard]] Scene::Document create_scene_document(Instance& instance) const;
        [[nodiscard]] Scene::FrameSnapshot create_scene_frame(Instance& instance, const Scene::FrameInfo& frame) const;

    private:
        struct State;
        std::unique_ptr<State> state{};
    };
} // namespace spectra::scene
