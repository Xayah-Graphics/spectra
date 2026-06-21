export module spectra.scene_runtime.contracts;

export import spectra.scene;
import std;

namespace spectra::scene_runtime {
    export enum class SceneEntryKind {
        Static,
        Dynamic,
    };

    export enum class DynamicSceneOptionKind {
        Text,
        DirectoryPath,
        FilePath,
        Choice,
        Bool,
        Float,
        UnsignedInteger,
    };

    export struct DynamicSceneOptionChoice {
        std::string value{};
        std::string label{};
    };

    export struct DynamicSceneOptionSchema {
        std::string key{};
        std::string label{};
        std::string description{};
        DynamicSceneOptionKind kind{DynamicSceneOptionKind::Text};
        bool required{};
        std::string default_value{};
        std::string group{};
        bool advanced{};
        std::int32_t priority{};
        std::vector<DynamicSceneOptionChoice> choices{};
    };

    export struct DynamicSceneOption {
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

    export inline constexpr std::uint32_t DynamicSceneGpuBufferKindVolumeChannel = 0u;
    export inline constexpr std::uint32_t DynamicSceneGpuBufferKindViewportVoxelGrid = 1u;

    export struct DynamicSceneGpuBufferRequest {
        std::uint32_t kind{};
        std::uint64_t byte_size{};
        std::string debug_name{};
    };

    export struct DynamicSceneGpuBufferAllocation {
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        std::uint32_t kind{};
        DynamicSceneGpuResourceHandleKind handle_kind{DynamicSceneGpuResourceHandleKind::OpaqueWin32};
        std::uintptr_t handle{};
        DynamicSceneGpuDeviceIdentity device_identity{};
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
        std::vector<DynamicSceneOptionSchema> options{};
    };

    export struct DynamicSceneControlSettingValue {
        std::string key{};
        std::string value{};
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

    export struct DynamicSceneControlActionState {
        std::string action_id{};
        bool enabled{};
        std::string disabled_reason{};
    };

    export struct DynamicSceneControlStatus {
        std::string phase{};
        std::string headline{};
        std::string detail{};
        std::vector<DynamicSceneControlMetric> metrics{};
        std::vector<DynamicSceneControlActionState> action_states{};
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

    export struct DynamicSceneUpdateInfo {
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        DynamicSceneControlTimelineMode timeline_mode{DynamicSceneControlTimelineMode::Live};
        bool timeline_playing{};
    };

    export struct DynamicSceneControlSnapshot {
        DynamicSceneControlStatus status{};
        std::vector<DynamicSceneControlSettingValue> settings{};
        std::vector<DynamicSceneControlLogEntry> logs{};
        std::vector<DynamicSceneControlImage> images{};
        std::vector<DynamicSceneControlScalarSeries> scalar_series{};
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
        virtual void update(const DynamicSceneUpdateInfo& update) = 0;
        [[nodiscard]] virtual std::uint64_t scene_revision() const = 0;
        virtual void execute_control_action(std::string_view action_id, std::span<const DynamicSceneOption> options) = 0;
        virtual void update_control_setting(std::string_view key, std::string_view value) = 0;
        [[nodiscard]] virtual DynamicSceneControlSnapshot control_snapshot() const = 0;
        [[nodiscard]] virtual scene::Scene::Document create_scene_document() const = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const = 0;
    };

} // namespace spectra::scene_runtime
