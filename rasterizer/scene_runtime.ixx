export module spectra.rasterizer.scene_runtime;

export import spectra.scene;
import std;

namespace spectra::rasterizer {
    export enum class SceneEntryKind {
        Static,
        Dynamic,
    };

    export class DynamicSceneSourceInstance {
    public:
        DynamicSceneSourceInstance() = default;

        DynamicSceneSourceInstance(const DynamicSceneSourceInstance& other) = delete;
        DynamicSceneSourceInstance(DynamicSceneSourceInstance&& other) = delete;
        DynamicSceneSourceInstance& operator=(const DynamicSceneSourceInstance& other) = delete;
        DynamicSceneSourceInstance& operator=(DynamicSceneSourceInstance&& other) = delete;
        virtual ~DynamicSceneSourceInstance() noexcept = default;

        virtual void reset() = 0;
        virtual void step(float delta_seconds) = 0;
        [[nodiscard]] virtual scene::Scene::Document create_scene_document() const = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const = 0;
    };

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
        SceneController(SceneRegistry registry, std::shared_ptr<scene::Scene> empty_workspace);

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
        void activate_empty_workspace();
        void request_activate(std::size_t index);
        [[nodiscard]] bool activate_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene);
        [[nodiscard]] bool activate_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source);
        [[nodiscard]] bool apply_pending_scene();
        void update_active_scene(double delta_seconds);

    private:
        struct SceneSlot {
            std::unique_ptr<DynamicSceneSourceInstance> source{};
            std::shared_ptr<scene::Scene> workspace{};
            double frame_accumulator_seconds{};
            double stream_time_seconds{};
            std::uint64_t stream_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        void sync_slot_count();
        void set_static_slot(std::size_t index, std::shared_ptr<scene::Scene> scene);
        void set_dynamic_slot(std::size_t index, std::unique_ptr<DynamicSceneSourceInstance> source, std::shared_ptr<scene::Scene> workspace);
        void clear_activation_error();
        [[nodiscard]] SceneSlot& ensure_slot(std::size_t index);
        [[nodiscard]] scene::Scene::Document create_dynamic_slot(std::size_t index, SceneSlot* slot);
        void reset_dynamic_scene(SceneSlot& slot, scene::Scene::Timeline timeline);

        SceneRegistry registry{};
        std::vector<SceneSlot> slots{};
        std::shared_ptr<scene::Scene> empty_workspace{};
        std::optional<std::size_t> selected_entry_index{};
        std::optional<std::size_t> pending_selected_entry_index{};
        std::string activation_error_message{};
    };

    export struct DynamicScenePluginSource {
        std::string id{};
        std::string title{};
        std::filesystem::path path{};
        std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source{};
    };

    export enum class DynamicSceneOpenOptionKind {
        Text,
        DirectoryPath,
        FilePath,
        Choice,
        Bool,
        Float,
        UnsignedInteger,
    };

    export struct DynamicSceneOpenOptionChoice {
        std::string value{};
        std::string label{};
    };

    export struct DynamicSceneOpenOptionSchema {
        std::string key{};
        std::string label{};
        std::string description{};
        DynamicSceneOpenOptionKind kind{DynamicSceneOpenOptionKind::Text};
        bool required{};
        std::string default_value{};
        std::vector<DynamicSceneOpenOptionChoice> choices{};
    };

    export struct DynamicScenePluginInfo {
        std::string id{};
        std::string title{};
        std::string project_panel_title{};
        std::string open_action_label{};
        std::string open_action_description{};
        std::filesystem::path path{};
        std::vector<DynamicSceneOpenOptionSchema> open_options{};
    };

    export struct DynamicSceneOpenOption {
        std::string key{};
        std::string value{};
    };

    export struct DynamicSceneOpenRequest {
        std::filesystem::path plugin_path{};
        std::vector<DynamicSceneOpenOption> options{};
    };

    export [[nodiscard]] bool is_dynamic_scene_plugin_file(const std::filesystem::path& path);
    export [[nodiscard]] DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path);
    export [[nodiscard]] DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request);
} // namespace spectra::rasterizer
