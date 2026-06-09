export module spectra.scene.session;

export import spectra.scene;
import std;

namespace spectra::scene {
    export enum class SceneSourceKind {
        Static,
        Simulation,
    };

    export struct SceneFrameInfo {
        double delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
    };

    export [[nodiscard]] FrameCursor MakeFrameCursor(const SceneFrameInfo& info);

    export template <typename Adapter>
    concept SceneSimulationAdapter = std::default_initializable<Adapter> && requires(Adapter& adapter, const Adapter& const_adapter, const SceneFrameInfo& frame) {
        { Adapter::project_id() } -> std::convertible_to<std::string_view>;
        { Adapter::project_title() } -> std::convertible_to<std::string_view>;
        { const_adapter.create_document() } -> std::same_as<SceneDocument>;
        { adapter.reset() } -> std::same_as<SceneFrameSnapshot>;
        { adapter.step(frame) } -> std::same_as<SceneFrameSnapshot>;
    };

    export class SceneSourceRuntime {
    public:
        SceneSourceRuntime() = default;

        SceneSourceRuntime(const SceneSourceRuntime& other) = delete;
        SceneSourceRuntime(SceneSourceRuntime&& other) = delete;
        SceneSourceRuntime& operator=(const SceneSourceRuntime& other) = delete;
        SceneSourceRuntime& operator=(SceneSourceRuntime&& other) = delete;
        virtual ~SceneSourceRuntime() noexcept = default;

        [[nodiscard]] virtual std::string_view id() const = 0;
        [[nodiscard]] virtual std::string_view title() const = 0;
        [[nodiscard]] virtual SceneDocument create_document() const = 0;
        [[nodiscard]] virtual SceneFrameSnapshot reset() = 0;
        [[nodiscard]] virtual SceneFrameSnapshot step(const SceneFrameInfo& frame) = 0;
    };

    export template <SceneSimulationAdapter Adapter>
    class SceneSourceRuntimeModel final : public SceneSourceRuntime {
    public:
        SceneSourceRuntimeModel() = default;

        [[nodiscard]] std::string_view id() const override {
            return Adapter::project_id();
        }

        [[nodiscard]] std::string_view title() const override {
            return Adapter::project_title();
        }

        [[nodiscard]] SceneDocument create_document() const override {
            return this->adapter.create_document();
        }

        [[nodiscard]] SceneFrameSnapshot reset() override {
            return this->adapter.reset();
        }

        [[nodiscard]] SceneFrameSnapshot step(const SceneFrameInfo& frame) override {
            return this->adapter.step(frame);
        }

    private:
        Adapter adapter{};
    };

    export struct SceneSourceEntry {
        std::string id{};
        std::string title{};
        SceneSourceKind kind{SceneSourceKind::Static};
        std::move_only_function<SceneDocument()> create_static_document{};
        std::move_only_function<std::unique_ptr<SceneSourceRuntime>()> create_simulation_runtime{};
    };

    export class SceneSourceRegistry final {
    public:
        SceneSourceRegistry() = default;

        SceneSourceRegistry(const SceneSourceRegistry& other) = delete;
        SceneSourceRegistry(SceneSourceRegistry&& other) noexcept = default;
        SceneSourceRegistry& operator=(const SceneSourceRegistry& other) = delete;
        SceneSourceRegistry& operator=(SceneSourceRegistry&& other) noexcept = default;
        ~SceneSourceRegistry() noexcept = default;

        void register_static_scene(std::string id, std::string title, std::move_only_function<SceneDocument()> create_document);

        template <SceneSimulationAdapter Adapter>
        void register_simulation() {
            const std::string id{Adapter::project_id()};
            this->ensure_unique_scene_id(id);
            this->entries.push_back(SceneSourceEntry{
                .id                        = id,
                .title                     = std::string{Adapter::project_title()},
                .kind                      = SceneSourceKind::Simulation,
                .create_simulation_runtime = [] { return std::make_unique<SceneSourceRuntimeModel<Adapter>>(); },
            });
        }

        [[nodiscard]] std::unique_ptr<SceneSourceRuntime> create_simulation_runtime(std::size_t index);
        [[nodiscard]] SceneDocument create_static_document(std::size_t index);
        [[nodiscard]] const SceneSourceEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;

    private:
        void ensure_unique_scene_id(const std::string& id) const;

        std::vector<SceneSourceEntry> entries{};

        friend class SceneSession;
    };

    export class SceneSession final {
    public:
        explicit SceneSession(SceneSourceRegistry registry);

        SceneSession(const SceneSession& other) = delete;
        SceneSession(SceneSession&& other) = delete;
        SceneSession& operator=(const SceneSession& other) = delete;
        SceneSession& operator=(SceneSession&& other) = delete;
        ~SceneSession() noexcept = default;

        [[nodiscard]] std::shared_ptr<SceneWorkspace> active_workspace();
        [[nodiscard]] const SceneSourceEntry& entry(std::size_t index) const;
        [[nodiscard]] std::size_t size() const;
        [[nodiscard]] std::size_t active_index() const;
        [[nodiscard]] std::size_t selected_index() const;
        [[nodiscard]] bool pending_switch() const;

        void request_activate(std::size_t index);
        [[nodiscard]] bool apply_pending_scene();
        void update_active_scene(double delta_seconds);

    private:
        struct SceneSlot {
            std::unique_ptr<SceneSourceRuntime> runtime{};
            std::shared_ptr<SceneWorkspace> workspace{};
            double simulation_accumulator_seconds{};
            double simulation_time_seconds{};
            std::uint64_t simulation_frame_index{};
            std::uint64_t observed_reset_request_serial{};
            std::uint64_t observed_clear_recording_request_serial{};
            std::optional<std::uint64_t> committed_playback_frame_index{};
        };

        [[nodiscard]] SceneSlot& ensure_slot(std::size_t index);
        [[nodiscard]] SceneDocument create_simulation_slot(std::size_t index, SceneSlot* slot);
        void reset_simulation(SceneSlot& slot, SimulationTimeline timeline);

        SceneSourceRegistry registry{};
        std::vector<SceneSlot> slots{};
        std::size_t currentActiveIndex{};
        std::optional<std::size_t> pendingActiveIndex{};
    };
} // namespace spectra::scene
