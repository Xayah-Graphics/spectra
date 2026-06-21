export module spectra.scene_session;

export import spectra.dynamic_scene.host;
import std;

namespace spectra::scene_session {
    export enum class SceneKind {
        Static,
        Dynamic,
    };

    export struct SceneDescriptor {
        std::string id{};
        std::string title{};
        SceneKind kind{SceneKind::Static};
    };

    export class Session final {
    public:
        Session(std::shared_ptr<scene::Scene> empty_scene, std::shared_ptr<dynamic_scene::HostServices> host);

        Session(const Session& other) = delete;
        Session(Session&& other) = delete;
        Session& operator=(const Session& other) = delete;
        Session& operator=(Session&& other) = delete;
        ~Session() noexcept = default;

        [[nodiscard]] std::shared_ptr<scene::Scene> active_scene();
        [[nodiscard]] bool has_active_scene() const;
        [[nodiscard]] const SceneDescriptor& active_scene_descriptor() const;
        [[nodiscard]] bool has_activation_error() const;
        [[nodiscard]] const std::string& activation_error() const;
        [[nodiscard]] bool has_active_dynamic_scene_controls() const;
        [[nodiscard]] std::shared_ptr<dynamic_scene::HostServices> dynamic_host() const;
        [[nodiscard]] bool active_scene_timeline_enabled() const;
        [[nodiscard]] bool active_scene_timeline_streaming_enabled() const;
        [[nodiscard]] dynamic_scene::ControlSnapshot active_dynamic_scene_control_snapshot() const;
        void close_scene();
        [[nodiscard]] bool open_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene);
        [[nodiscard]] bool open_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<dynamic_scene::SourceInstance>()> create_source);
        void toggle_active_scene_timeline_playback();
        void request_active_scene_timeline_reset();
        void tick(double delta_seconds);
        void execute_active_dynamic_scene_control_action(std::string_view action_id, std::span<const dynamic_scene::Option> options);
        void update_active_dynamic_scene_control_setting(std::string_view key, std::string_view value);

    private:
        struct DynamicState {
            std::unique_ptr<dynamic_scene::SourceInstance> source{};
            double frame_accumulator_seconds{};
            double stream_time_seconds{};
            std::uint64_t stream_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::uint64_t observed_scene_revision{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        void clear_activation_error();
        void reset_dynamic_state();
        [[nodiscard]] dynamic_scene::SourceInstance& active_dynamic_scene_control_source() const;
        void reset_dynamic_scene(scene::Scene::Timeline timeline);
        void commit_dynamic_scene_revision(std::string_view context);

        std::shared_ptr<scene::Scene> empty_scene{};
        std::shared_ptr<scene::Scene> active_scene_instance{};
        std::shared_ptr<dynamic_scene::HostServices> host{};
        std::optional<SceneDescriptor> descriptor{};
        DynamicState dynamic{};
        std::string activation_error_message{};
    };
} // namespace spectra::scene_session
