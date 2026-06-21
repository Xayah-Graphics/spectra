export module spectra.dynamic_scene.contracts;

export import spectra.scene;
import std;

namespace spectra::dynamic_scene {
    export enum class OptionKind {
        Text,
        DirectoryPath,
        FilePath,
        Choice,
        Bool,
        Float,
        UnsignedInteger,
    };

    export struct OptionChoice {
        std::string value{};
        std::string label{};
    };

    export struct OptionSchema {
        std::string key{};
        std::string label{};
        std::string description{};
        OptionKind kind{OptionKind::Text};
        bool required{};
        std::string default_value{};
        std::string group{};
        bool advanced{};
        std::int32_t priority{};
        std::vector<OptionChoice> choices{};
    };

    export struct Option {
        std::string key{};
        std::string value{};
    };

    export enum class GpuResourceHandleKind : std::uint32_t {
        OpaqueWin32 = 1u,
        OpaqueFileDescriptor = 2u,
    };

    export struct GpuDeviceIdentity {
        std::uint32_t vendor_id{};
        std::uint32_t device_id{};
        std::array<std::uint8_t, 16u> device_uuid{};
        std::array<std::uint8_t, 8u> device_luid{};
        std::uint32_t device_node_mask{};
    };

    export inline constexpr std::uint32_t GpuBufferKindVolumeChannel = 0u;
    export inline constexpr std::uint32_t GpuBufferKindViewportVoxelGrid = 1u;

    export struct GpuBufferRequest {
        std::uint32_t kind{};
        std::uint64_t byte_size{};
        std::string debug_name{};
    };

    export struct GpuBufferAllocation {
        std::uint64_t resource_id{};
        std::uint64_t byte_size{};
        std::uint32_t kind{};
        GpuResourceHandleKind handle_kind{GpuResourceHandleKind::OpaqueWin32};
        std::uintptr_t handle{};
        GpuDeviceIdentity device_identity{};
    };

    export inline constexpr std::uint32_t ControlPlacementViewportOverlay = 1u << 0u;
    export inline constexpr std::uint32_t ControlPlacementPanelSummary = 1u << 1u;
    export inline constexpr std::uint32_t ControlPlacementPanelDetail = 1u << 2u;
    export inline constexpr std::uint32_t ControlActionGroupRun = 0u;
    export inline constexpr std::uint32_t ControlActionGroupPreview = 1u;
    export inline constexpr std::uint32_t ControlActionGroupDebug = 2u;
    export inline constexpr std::uint32_t ControlActionGroupUtility = 3u;
    export inline constexpr std::uint32_t ControlActionStyleSecondary = 0u;
    export inline constexpr std::uint32_t ControlActionStylePrimary = 1u;
    export inline constexpr std::uint32_t ControlActionStyleDanger = 2u;

    export struct ControlAction {
        std::string id{};
        std::string label{};
        std::string description{};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint32_t style{};
        std::vector<OptionSchema> options{};
    };

    export struct ControlSettingValue {
        std::string key{};
        std::string value{};
    };

    export struct ControlMetric {
        std::string key{};
        std::string label{};
        std::string value{};
        std::uint32_t placement_flags{};
        std::int32_t priority{};
        bool has_color{};
        std::array<float, 4u> color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    export struct ControlActionState {
        std::string action_id{};
        bool enabled{};
        std::string disabled_reason{};
    };

    export struct ControlStatus {
        std::string phase{};
        std::string headline{};
        std::string detail{};
        std::vector<ControlMetric> metrics{};
        std::vector<ControlActionState> action_states{};
    };

    export struct ControlLogEntry {
        std::uint64_t sequence{};
        std::string level{};
        std::string message{};
    };

    export struct ControlImage {
        std::string id{};
        std::string label{};
        std::string description{};
        const std::uint8_t* rgba8{};
        std::uint64_t rgba8_size{};
        std::uint64_t revision{};
        std::uint32_t width{};
        std::uint32_t height{};
    };

    export struct ControlScalarSample {
        std::uint64_t step{};
        double time_seconds{};
        double value{};
    };

    export struct ControlScalarSeries {
        std::string id{};
        std::string label{};
        std::string description{};
        std::string unit{};
        std::array<float, 4u> color{1.0f, 1.0f, 1.0f, 1.0f};
        std::uint32_t group{};
        std::int32_t priority{};
        std::uint64_t revision{};
        std::vector<ControlScalarSample> samples{};
    };

    export enum class ControlTimelineMode : std::uint32_t {
        Live = 0u,
        Record = 1u,
        Playback = 2u,
    };

    export struct UpdateInfo {
        double wall_delta_seconds{};
        double scene_delta_seconds{};
        double time_seconds{};
        std::uint64_t frame_index{};
        ControlTimelineMode timeline_mode{ControlTimelineMode::Live};
        bool timeline_playing{};
    };

    export struct ControlSnapshot {
        ControlStatus status{};
        std::vector<ControlSettingValue> settings{};
        std::vector<ControlLogEntry> logs{};
        std::vector<ControlImage> images{};
        std::vector<ControlScalarSeries> scalar_series{};
    };

    export class SourceInstance {
    public:
        SourceInstance() = default;

        SourceInstance(const SourceInstance& other) = delete;
        SourceInstance(SourceInstance&& other) = delete;
        SourceInstance& operator=(const SourceInstance& other) = delete;
        SourceInstance& operator=(SourceInstance&& other) = delete;
        virtual ~SourceInstance() noexcept = default;

        virtual void reset() = 0;
        virtual void update(const UpdateInfo& update) = 0;
        [[nodiscard]] virtual std::uint64_t scene_revision() const = 0;
        virtual void execute_control_action(std::string_view action_id, std::span<const Option> options) = 0;
        virtual void update_control_setting(std::string_view key, std::string_view value) = 0;
        [[nodiscard]] virtual ControlSnapshot control_snapshot() const = 0;
        [[nodiscard]] virtual scene::Scene::Document create_scene_document() const = 0;
        [[nodiscard]] virtual scene::Scene::FrameSnapshot create_scene_frame(const scene::Scene::FrameInfo& frame) const = 0;
    };

} // namespace spectra::dynamic_scene
