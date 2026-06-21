export module spectra.scene_runtime.controller;

export import spectra.scene_runtime.host_services;
import std;

namespace spectra::scene_runtime {
    export struct SceneEntry {
        SceneEntry(const SceneEntry& other) = delete;
        SceneEntry(SceneEntry&& other) noexcept = default;
        SceneEntry& operator=(const SceneEntry& other) = delete;
        SceneEntry& operator=(SceneEntry&& other) noexcept = default;
        ~SceneEntry() noexcept = default;

        std::string id{};
        std::string title{};
        SceneEntryKind kind{SceneEntryKind::Static};

    private:
        SceneEntry(std::string id, std::string title, SceneEntryKind kind, std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_dynamic_source);

        std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene{};
        std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_dynamic_source{};

        friend class SceneRegistry;
        friend class SceneController;
    };

    export class SceneRegistry final {
    public:
        SceneRegistry() = default;

        SceneRegistry(const SceneRegistry& other) = delete;
        SceneRegistry(SceneRegistry&& other) noexcept = default;
        SceneRegistry& operator=(const SceneRegistry& other) = delete;
        SceneRegistry& operator=(SceneRegistry&& other) noexcept = default;
        ~SceneRegistry() noexcept = default;

        [[nodiscard]] std::size_t upsert_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> create_scene);
        [[nodiscard]] std::size_t upsert_dynamic_source(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source);
        [[nodiscard]] const SceneEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;

    private:
        [[nodiscard]] std::optional<std::size_t> find_entry_index(std::string_view id) const;
        [[nodiscard]] std::unique_ptr<DynamicSceneSourceInstance> create_dynamic_source(std::size_t index);
        [[nodiscard]] std::shared_ptr<scene::Scene> create_static_scene(std::size_t index);

        std::vector<SceneEntry> entries{};

        friend class SceneController;
    };

    export class SceneController final {
    public:
        SceneController(SceneRegistry registry, std::shared_ptr<scene::Scene> empty_workspace, std::shared_ptr<DynamicSceneHostServices> host_services);

        SceneController(const SceneController& other) = delete;
        SceneController(SceneController&& other) = delete;
        SceneController& operator=(const SceneController& other) = delete;
        SceneController& operator=(SceneController&& other) = delete;
        ~SceneController() noexcept = default;

        [[nodiscard]] std::shared_ptr<scene::Scene> active_workspace();
        [[nodiscard]] const SceneEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] bool has_selected_entry() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] bool pending_switch() const;
        [[nodiscard]] bool has_activation_error() const;
        [[nodiscard]] const std::string& activation_error() const;
        [[nodiscard]] bool has_active_dynamic_scene_controls();
        [[nodiscard]] std::shared_ptr<DynamicSceneHostServices> dynamic_host_services() const;
        [[nodiscard]] bool active_scene_timeline_enabled();
        [[nodiscard]] bool active_scene_timeline_streaming_enabled();
        [[nodiscard]] DynamicSceneControlSnapshot active_dynamic_scene_control_snapshot();
        void activate_empty_workspace();
        void request_activate(std::size_t index);
        [[nodiscard]] bool activate_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene);
        [[nodiscard]] bool activate_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source);
        [[nodiscard]] bool apply_pending_scene();
        void toggle_active_scene_timeline_playback();
        void request_active_scene_timeline_reset();
        void update_active_scene(double delta_seconds);
        void execute_active_dynamic_scene_control_action(std::string_view action_id, std::span<const DynamicSceneOption> options);
        void update_active_dynamic_scene_control_setting(std::string_view key, std::string_view value);

    private:
        struct SceneSlot {
            std::unique_ptr<DynamicSceneSourceInstance> source{};
            std::shared_ptr<scene::Scene> workspace{};
            double frame_accumulator_seconds{};
            double stream_time_seconds{};
            std::uint64_t stream_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::uint64_t observed_scene_revision{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        void sync_slot_count();
        void set_static_slot(std::size_t index, std::shared_ptr<scene::Scene> scene);
        void set_dynamic_slot(std::size_t index, std::unique_ptr<DynamicSceneSourceInstance> source, std::shared_ptr<scene::Scene> workspace);
        void release_selected_dynamic_slot();
        void clear_activation_error();
        [[nodiscard]] SceneSlot& ensure_slot(std::size_t index);
        [[nodiscard]] DynamicSceneSourceInstance& active_dynamic_scene_control_source();
        [[nodiscard]] scene::Scene::Document create_dynamic_slot(std::size_t index, SceneSlot* slot);
        void reset_dynamic_scene(SceneSlot& slot, scene::Scene::Timeline timeline);
        void commit_dynamic_scene_revision(SceneSlot& slot, std::string_view context);

        SceneRegistry registry{};
        std::vector<SceneSlot> slots{};
        std::shared_ptr<scene::Scene> empty_workspace{};
        std::shared_ptr<DynamicSceneHostServices> host_services{};
        std::optional<std::size_t> selected_entry_index{};
        std::optional<std::size_t> pending_selected_entry_index{};
        std::string activation_error_message{};
    };

} // namespace spectra::scene_runtime
