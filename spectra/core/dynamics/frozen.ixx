export module spectra.dynamics.frozen;

import spectra.dynamics;
import spectra.runtime;
import std;

namespace spectra::dynamics {
    export enum class FrozenVisualizationKind : std::uint32_t {
        Points,
        Segments,
        Curves,
        Vectors,
        Field,
        Image,
        CameraObservations,
        Transforms,
        Surface,
    };

    export struct FrozenVisualization {
        FrozenVisualizationKind kind{};
        VisualizationStyle style{};
        math::UInt3 resolution{};
        math::Transform local_from_grid{};
        FieldChannelDescriptor channel{};
        ImageDataset image{};
        CameraObservationDataset camera_observations{};
        std::uint64_t primary_count{};
        std::uint64_t secondary_count{};
        std::vector<std::vector<std::byte>> buffers{};
    };

    export struct FrozenTelemetrySystem {
        std::string id{};
        std::string name{};
        std::string provider_id{};
        std::vector<TelemetryDescriptor> descriptors{};
        TelemetrySnapshot snapshot{};
    };

    export struct FrozenBounds {
        BoundsDomain domain{BoundsDomain::World};
        std::vector<SceneBound> values{};
    };

    export struct FrozenFrame {
        SimulationTimeline simulation{};
        PresentationTimeline presentation{};
        std::vector<FrozenBounds> bounds{};
        std::vector<FrozenVisualization> visualizations{};
        std::vector<FrozenTelemetrySystem> telemetry{};
    };

    export [[nodiscard]] std::vector<std::byte> serialize_frozen_frame(const FrozenFrame& frame);
    export [[nodiscard]] FrozenFrame deserialize_frozen_frame(std::span<const std::byte> payload);
    export void write_telemetry(const std::filesystem::path& path, const FrozenFrame& frame);

    export struct FrozenFrameRuntime {
        struct Buffer {
            GpuBuffer gpu{};
            DescriptorLease descriptor{};
        };

        explicit FrozenFrameRuntime(VulkanRuntime& runtime) noexcept;
        ~FrozenFrameRuntime();

        FrozenFrameRuntime(const FrozenFrameRuntime&)            = delete;
        FrozenFrameRuntime(FrozenFrameRuntime&&)                 = delete;
        FrozenFrameRuntime& operator=(const FrozenFrameRuntime&) = delete;
        FrozenFrameRuntime& operator=(FrozenFrameRuntime&&)      = delete;

        void initialize(std::span<const std::byte> payload);
        void destroy() noexcept;
        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] std::span<const GpuVisualization> visualizations() const noexcept;
        [[nodiscard]] const FrozenFrame& frame() const noexcept;

        VulkanRuntime& runtime;
        FrozenFrame data{};
        std::deque<Buffer> buffers{};
        DynamicFrame gpu_frame{};
        std::vector<GpuVisualization> gpu_visualizations{};
        bool ready{};
    };
} // namespace spectra::dynamics
