export module spectra.scene_workspace.controller;

export import spectra.dynamic_scene.host;
import std;

namespace spectra::scene_workspace {
    export enum class EntryKind {
        Static,
        Dynamic,
    };

    export struct Entry {
        Entry(const Entry& other) = delete;
        Entry(Entry&& other) noexcept = default;
        Entry& operator=(const Entry& other) = delete;
        Entry& operator=(Entry&& other) noexcept = default;
        ~Entry() noexcept = default;

        std::string id{};
        std::string title{};
        EntryKind kind{EntryKind::Static};

    private:
        Entry(std::string id, std::string title, EntryKind kind, std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene, std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_dynamic_source);

        std::move_only_function<std::shared_ptr<scene::Scene>()> create_static_scene{};
        std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_dynamic_source{};

        friend class Registry;
        friend class Controller;
    };

    export class Registry final {
    public:
        Registry() = default;

        Registry(const Registry& other) = delete;
        Registry(Registry&& other) noexcept = default;
        Registry& operator=(const Registry& other) = delete;
        Registry& operator=(Registry&& other) noexcept = default;
        ~Registry() noexcept = default;

        [[nodiscard]] std::size_t upsert_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> create_scene);
        [[nodiscard]] std::size_t upsert_dynamic_source(std::string id, std::string title, std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_source);
        [[nodiscard]] const Entry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;

    private:
        [[nodiscard]] std::optional<std::size_t> find_entry_index(std::string_view id) const;
        [[nodiscard]] std::unique_ptr<dynamic_scene::SourceInstance> create_dynamic_source(std::size_t index);
        [[nodiscard]] std::shared_ptr<scene::Scene> create_static_scene(std::size_t index);

        std::vector<Entry> entries{};

        friend class Controller;
    };

    export class Controller final {
    public:
        Controller(Registry registry, std::shared_ptr<scene::Scene> empty_workspace, std::shared_ptr<dynamic_scene::HostServices> host);

        Controller(const Controller& other) = delete;
        Controller(Controller&& other) = delete;
        Controller& operator=(const Controller& other) = delete;
        Controller& operator=(Controller&& other) = delete;
        ~Controller() noexcept = default;

        [[nodiscard]] std::shared_ptr<scene::Scene> active_workspace();
        [[nodiscard]] const Entry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] bool has_selected_entry() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] bool pending_switch() const;
        [[nodiscard]] bool has_activation_error() const;
        [[nodiscard]] const std::string& activation_error() const;
        [[nodiscard]] bool has_active_dynamic_scene_controls();
        [[nodiscard]] std::shared_ptr<dynamic_scene::HostServices> dynamic_host() const;
        [[nodiscard]] bool active_scene_timeline_enabled();
        [[nodiscard]] bool active_scene_timeline_streaming_enabled();
        [[nodiscard]] dynamic_scene::ControlSnapshot active_dynamic_scene_control_snapshot();
        void activate_empty_workspace();
        void request_activate(std::size_t index);
        [[nodiscard]] bool activate_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene);
        [[nodiscard]] bool activate_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_source);
        [[nodiscard]] bool apply_pending_scene();
        void toggle_active_scene_timeline_playback();
        void request_active_scene_timeline_reset();
        void update_active_scene(double delta_seconds);
        void execute_active_dynamic_scene_control_action(std::string_view action_id, std::span<const dynamic_scene::Option> options);
        void update_active_dynamic_scene_control_setting(std::string_view key, std::string_view value);

    private:
        struct SceneSlot {
            std::unique_ptr<dynamic_scene::SourceInstance> source{};
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
        void set_dynamic_slot(std::size_t index, std::unique_ptr<dynamic_scene::SourceInstance> source, std::shared_ptr<scene::Scene> workspace);
        void release_selected_dynamic_slot();
        void clear_activation_error();
        [[nodiscard]] SceneSlot& ensure_slot(std::size_t index);
        [[nodiscard]] dynamic_scene::SourceInstance& active_dynamic_scene_control_source();
        [[nodiscard]] scene::Scene::Document create_dynamic_slot(std::size_t index, SceneSlot* slot);
        void reset_dynamic_scene(SceneSlot& slot, scene::Scene::Timeline timeline);
        void commit_dynamic_scene_revision(SceneSlot& slot, std::string_view context);

        Registry registry{};
        std::vector<SceneSlot> slots{};
        std::shared_ptr<scene::Scene> empty_workspace{};
        std::shared_ptr<dynamic_scene::HostServices> host{};
        std::optional<std::size_t> selected_entry_index{};
        std::optional<std::size_t> pending_selected_entry_index{};
        std::string activation_error_message{};
    };

} // namespace spectra::scene_workspace
