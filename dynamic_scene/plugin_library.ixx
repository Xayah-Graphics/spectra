export module spectra.dynamic_scene.plugin_library;

export import spectra.dynamic_scene.host;
import std;

namespace spectra::dynamic_scene {
    class PluginSourceInstance;

    export class PluginLibrary final {
    public:
        explicit PluginLibrary(std::filesystem::path plugin_path);
        PluginLibrary(std::filesystem::path plugin_path, std::vector<Option> options, std::shared_ptr<HostServices> host);
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
        [[nodiscard]] std::string source_id() const;
        [[nodiscard]] const std::filesystem::path& path() const;
        [[nodiscard]] std::vector<OptionSchema> open_options() const;
        [[nodiscard]] std::vector<ControlAction> control_actions() const;
        [[nodiscard]] std::vector<OptionSchema> control_settings() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl{};

        friend class PluginSourceInstance;
    };

    export [[nodiscard]] std::unique_ptr<SourceInstance> make_plugin_source_instance(std::shared_ptr<PluginLibrary> plugin);
} // namespace spectra::dynamic_scene
