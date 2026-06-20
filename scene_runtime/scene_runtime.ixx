export module spectra.scene_runtime;

export import spectra.scene;
import std;

namespace spectra::scene_runtime {
    export enum class SceneEntryKind {
        Static,
        Dynamic,
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
        std::string group{};
        bool advanced{};
        std::int32_t priority{};
        std::vector<DynamicSceneOpenOptionChoice> choices{};
    };

    export struct DynamicSceneOpenOption {
        std::string key{};
        std::string value{};
    };

    export enum class DynamicSceneGpuResourceHandleKind : std::uint32_t {
        OpaqueWin32 = 1u,
        OpaqueFileDescriptor = 2u,
    };

    export struct DynamicSceneGpuDeviceIdentity {
        std::uint32_t vendor_id{};
        std::uint32_t device_id{};
        std::array<std::uint8_t, 16u> device_uuid{};
        std::array<std::uint8_t, 8u> device_luid{};
        std::uint32_t device_node_mask{};
    };

    export struct DynamicSceneViewportVoxelBufferRequest {
        std::uint64_t byte_size{};
        std::string debug_name{};
    };

    export struct DynamicSceneViewportVoxelBufferAllocation {
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        DynamicSceneGpuResourceHandleKind handle_kind{DynamicSceneGpuResourceHandleKind::OpaqueWin32};
        std::uintptr_t handle{};
        DynamicSceneGpuDeviceIdentity device_identity{};
    };

    export struct DynamicSceneVolumeBufferRequest {
        std::uint64_t byte_size{};
        std::string debug_name{};
    };

    export struct DynamicSceneVolumeBufferAllocation {
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        DynamicSceneGpuResourceHandleKind handle_kind{DynamicSceneGpuResourceHandleKind::OpaqueWin32};
        std::uintptr_t handle{};
        DynamicSceneGpuDeviceIdentity device_identity{};
    };

    export class DynamicSceneHostServices {
    public:
        DynamicSceneHostServices() = default;
        DynamicSceneHostServices(const DynamicSceneHostServices& other) = delete;
        DynamicSceneHostServices(DynamicSceneHostServices&& other) = delete;
        DynamicSceneHostServices& operator=(const DynamicSceneHostServices& other) = delete;
        DynamicSceneHostServices& operator=(DynamicSceneHostServices&& other) = delete;
        virtual ~DynamicSceneHostServices() noexcept = default;

        [[nodiscard]] virtual DynamicSceneViewportVoxelBufferAllocation request_viewport_voxel_buffer(const DynamicSceneViewportVoxelBufferRequest& request) = 0;
        virtual void release_viewport_voxel_buffer(std::uint64_t resource_id) = 0;
        [[nodiscard]] virtual DynamicSceneVolumeBufferAllocation request_volume_buffer(const DynamicSceneVolumeBufferRequest& request) = 0;
        virtual void release_volume_buffer(std::uint64_t resource_id) = 0;
        [[nodiscard]] virtual std::string_view last_error() const = 0;
    };

    export class DynamicSceneHostServiceRouter final : public DynamicSceneHostServices {
    public:
        DynamicSceneHostServiceRouter() = default;
        DynamicSceneHostServiceRouter(const DynamicSceneHostServiceRouter& other) = delete;
        DynamicSceneHostServiceRouter(DynamicSceneHostServiceRouter&& other) = delete;
        DynamicSceneHostServiceRouter& operator=(const DynamicSceneHostServiceRouter& other) = delete;
        DynamicSceneHostServiceRouter& operator=(DynamicSceneHostServiceRouter&& other) = delete;
        ~DynamicSceneHostServiceRouter() noexcept override = default;

        void set_viewport_voxel_buffer_backend(std::move_only_function<DynamicSceneViewportVoxelBufferAllocation(const DynamicSceneViewportVoxelBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback);
        void clear_viewport_voxel_buffer_backend() noexcept;
        void set_volume_buffer_backend(std::move_only_function<DynamicSceneVolumeBufferAllocation(const DynamicSceneVolumeBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback);
        void clear_volume_buffer_backend() noexcept;
        [[nodiscard]] DynamicSceneViewportVoxelBufferAllocation request_viewport_voxel_buffer(const DynamicSceneViewportVoxelBufferRequest& request) override;
        void release_viewport_voxel_buffer(std::uint64_t resource_id) override;
        [[nodiscard]] DynamicSceneVolumeBufferAllocation request_volume_buffer(const DynamicSceneVolumeBufferRequest& request) override;
        void release_volume_buffer(std::uint64_t resource_id) override;
        [[nodiscard]] std::string_view last_error() const override;

    private:
        std::move_only_function<DynamicSceneViewportVoxelBufferAllocation(const DynamicSceneViewportVoxelBufferRequest&)> request_viewport_voxel_buffer_callback{};
        std::move_only_function<void(std::uint64_t)> release_viewport_voxel_buffer_callback{};
        std::move_only_function<DynamicSceneVolumeBufferAllocation(const DynamicSceneVolumeBufferRequest&)> request_volume_buffer_callback{};
        std::move_only_function<void(std::uint64_t)> release_volume_buffer_callback{};
        std::map<std::uint64_t, DynamicSceneVolumeBufferAllocation> volume_buffer_allocations{};
        std::string last_error_message{};
    };

    export struct DynamicSceneOpenRequest {
        std::filesystem::path plugin_path{};
        std::vector<DynamicSceneOpenOption> options{};
        std::shared_ptr<DynamicSceneHostServices> host_services{};
    };

    export inline constexpr std::uint32_t DynamicSceneControlPlacementViewportOverlay = 1u << 0u;
    export inline constexpr std::uint32_t DynamicSceneControlPlacementPanelSummary = 1u << 1u;
    export inline constexpr std::uint32_t DynamicSceneControlPlacementPanelDetail = 1u << 2u;
    export inline constexpr std::uint32_t DynamicSceneControlActionGroupRun = 0u;
    export inline constexpr std::uint32_t DynamicSceneControlActionGroupPreview = 1u;
    export inline constexpr std::uint32_t DynamicSceneControlActionGroupDebug = 2u;
    export inline constexpr std::uint32_t DynamicSceneControlActionGroupUtility = 3u;
    export inline constexpr std::uint32_t DynamicSceneControlActionStyleSecondary = 0u;
    export inline constexpr std::uint32_t DynamicSceneControlActionStylePrimary = 1u;
    export inline constexpr std::uint32_t DynamicSceneControlActionStyleDanger = 2u;

    export struct DynamicSceneControlAction {
        std::string id{};
        std::string label{};
        std::string description{};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint32_t style{};
        std::vector<DynamicSceneOpenOptionSchema> options{};
    };

    export struct DynamicSceneControlSetting {
        std::string key{};
        std::string label{};
        std::string description{};
        DynamicSceneOpenOptionKind kind{DynamicSceneOpenOptionKind::Bool};
        std::string value{};
        std::string group{};
        bool advanced{};
        std::int32_t priority{};
        std::vector<DynamicSceneOpenOptionChoice> choices{};
    };

    export struct DynamicSceneControlMetric {
        std::string key{};
        std::string label{};
        std::string value{};
        std::uint32_t placement_flags{};
        std::int32_t priority{};
        bool has_color{};
        std::array<float, 4u> color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    export struct DynamicSceneControlDisabledAction {
        std::string action_id{};
        std::string reason{};
    };

    export struct DynamicSceneControlStatus {
        std::string phase{};
        std::string headline{};
        std::string detail{};
        std::vector<DynamicSceneControlMetric> metrics{};
        std::vector<std::string> enabled_action_ids{};
        std::vector<DynamicSceneControlDisabledAction> disabled_actions{};
    };

    export struct DynamicSceneControlLogEntry {
        std::uint64_t sequence{};
        std::string level{};
        std::string message{};
    };

    export struct DynamicSceneControlImage {
        std::string id{};
        std::string label{};
        std::string description{};
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    export struct DynamicSceneControlScalarSample {
        std::uint64_t step{};
        double time_seconds{};
        double value{};
    };

    export struct DynamicSceneControlScalarSeries {
        std::string id{};
        std::string label{};
        std::string description{};
        std::string unit{};
        std::array<float, 4u> color{1.0f, 1.0f, 1.0f, 1.0f};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint64_t revision{};
        std::vector<DynamicSceneControlScalarSample> samples{};
    };

    export enum class DynamicSceneControlTimelineMode : std::uint32_t {
        Live = 0u,
        Record = 1u,
        Playback = 2u,
    };

    export struct DynamicSceneControlUpdateInfo {
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        DynamicSceneControlTimelineMode timeline_mode{DynamicSceneControlTimelineMode::Live};
        bool timeline_playing{};
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
        virtual void update_controls(const DynamicSceneControlUpdateInfo& update) = 0;
        [[nodiscard]] virtual std::uint64_t scene_revision() const = 0;
        virtual void execute_control_action(std::string_view action_id, std::span<const DynamicSceneOpenOption> options) = 0;
        [[nodiscard]] virtual std::vector<DynamicSceneControlSetting> control_settings() const = 0;
        virtual void update_control_setting(std::string_view key, std::string_view value) = 0;
        [[nodiscard]] virtual DynamicSceneControlStatus control_status() const = 0;
        [[nodiscard]] virtual std::vector<DynamicSceneControlLogEntry> control_logs() const = 0;
        [[nodiscard]] virtual std::vector<DynamicSceneControlImage> control_images() const = 0;
        [[nodiscard]] virtual std::vector<DynamicSceneControlScalarSeries> control_scalar_series() const = 0;
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

    export struct DynamicScenePluginSource {
        std::string id{};
        std::string title{};
        std::filesystem::path path{};
        std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source{};
    };

    export struct DynamicScenePluginInfo {
        std::string id{};
        std::string title{};
        std::string controls_panel_title{};
        std::string open_action_label{};
        std::string open_action_description{};
        std::filesystem::path path{};
        std::vector<DynamicSceneOpenOptionSchema> open_options{};
        std::vector<DynamicSceneControlAction> control_actions{};
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
        [[nodiscard]] DynamicSceneControlStatus active_dynamic_scene_control_status();
        [[nodiscard]] std::vector<DynamicSceneControlLogEntry> active_dynamic_scene_control_logs();
        [[nodiscard]] std::vector<DynamicSceneControlImage> active_dynamic_scene_control_images();
        [[nodiscard]] std::vector<DynamicSceneControlScalarSeries> active_dynamic_scene_control_scalar_series();
        [[nodiscard]] std::vector<DynamicSceneControlSetting> active_dynamic_scene_control_settings();
        void activate_empty_workspace();
        void request_activate(std::size_t index);
        [[nodiscard]] bool activate_static_scene(std::string id, std::string title, std::move_only_function<std::shared_ptr<scene::Scene>()> load_scene);
        [[nodiscard]] bool activate_dynamic_scene(std::string id, std::string title, std::move_only_function<std::unique_ptr<DynamicSceneSourceInstance>()> create_source);
        [[nodiscard]] bool apply_pending_scene();
        void toggle_active_scene_timeline_playback();
        void request_active_scene_timeline_reset();
        void update_active_scene_controls(double delta_seconds);
        void update_active_scene(double delta_seconds);
        void execute_active_dynamic_scene_control_action(std::string_view action_id, std::span<const DynamicSceneOpenOption> options);
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

    export [[nodiscard]] bool is_dynamic_scene_plugin_file(const std::filesystem::path& path);
    export [[nodiscard]] DynamicScenePluginInfo inspect_dynamic_scene_plugin(const std::filesystem::path& plugin_path);
    export [[nodiscard]] DynamicScenePluginSource load_dynamic_scene_plugin(DynamicSceneOpenRequest request);
} // namespace spectra::scene_runtime
