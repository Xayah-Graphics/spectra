export module spectra.scene_runtime.plugin_library;

export import spectra.scene_runtime.host_services;
import std;

namespace spectra::scene_runtime {
    class DynamicScenePluginSourceInstance;

    export class DynamicScenePluginLibrary final {
    public:
        explicit DynamicScenePluginLibrary(std::filesystem::path plugin_path);
        DynamicScenePluginLibrary(std::filesystem::path plugin_path, std::vector<DynamicSceneOption> options, std::shared_ptr<DynamicSceneHostServices> host_services);
        DynamicScenePluginLibrary(const DynamicScenePluginLibrary& other) = delete;
        DynamicScenePluginLibrary(DynamicScenePluginLibrary&& other) = delete;
        DynamicScenePluginLibrary& operator=(const DynamicScenePluginLibrary& other) = delete;
        DynamicScenePluginLibrary& operator=(DynamicScenePluginLibrary&& other) = delete;
        ~DynamicScenePluginLibrary() noexcept;

        [[nodiscard]] std::string id() const;
        [[nodiscard]] std::string title() const;
        [[nodiscard]] std::string controls_panel_title() const;
        [[nodiscard]] std::string open_action_label() const;
        [[nodiscard]] std::string open_action_description() const;
        [[nodiscard]] std::string source_id() const;
        [[nodiscard]] const std::filesystem::path& path() const;
        [[nodiscard]] std::vector<DynamicSceneOptionSchema> open_options() const;
        [[nodiscard]] std::vector<DynamicSceneControlAction> control_actions() const;
        [[nodiscard]] std::vector<DynamicSceneOptionSchema> control_settings() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl{};

        friend class DynamicScenePluginSourceInstance;
    };

    export [[nodiscard]] std::unique_ptr<DynamicSceneSourceInstance> make_dynamic_scene_plugin_source_instance(std::shared_ptr<DynamicScenePluginLibrary> plugin);
} // namespace spectra::scene_runtime
