export module spectra.dynamics.frozen;

import spectra.dynamics.gpu;
import spectra.math;
import spectra.runtime;
import std;

namespace spectra::dynamics {
    export struct FrozenElements {
        std::vector<std::byte> elements{};
        std::uint32_t count{};
    };

    export struct FrozenField {
        math::UInt3 resolution{};
        math::Transform local_from_grid{};
        FieldChannelDescriptor channel{};
        std::vector<std::byte> values{};
    };

    export struct FrozenImage {
        ImageDataset image{};
        std::vector<std::byte> pixels{};
    };

    export struct FrozenCameraObservations {
        CameraObservationDataset dataset{};
        std::vector<std::byte> observations{};
        std::vector<std::byte> images{};
        std::uint32_t count{};
    };

    export struct FrozenSurface {
        std::vector<std::byte> positions{};
        std::optional<std::vector<std::byte>> indices{};
        std::optional<std::vector<std::byte>> scalars{};
        std::uint32_t vertex_count{};
        std::uint32_t index_count{};
    };

    export struct FrozenVisualization {
        VisualizationStyle style{};
        std::variant<FrozenElements, FrozenField, FrozenImage, FrozenCameraObservations, FrozenSurface> data{};
    };

    export struct FrozenTelemetrySystem {
        std::string id{};
        std::string name{};
        std::string provider_id{};
        std::vector<TelemetryDescriptor> descriptors{};
        TelemetrySnapshot snapshot{};
    };

    export struct FrozenSnapshot {
        SimulationTimeline simulation{};
        std::vector<FrozenVisualization> visualizations{};
        std::vector<FrozenTelemetrySystem> telemetry{};
    };

    export [[nodiscard]] std::vector<std::byte> serialize_frozen_snapshot(const FrozenSnapshot& snapshot);
    export [[nodiscard]] FrozenSnapshot deserialize_frozen_snapshot(std::span<const std::byte> payload);
    export void write_telemetry(const std::filesystem::path& path, const FrozenSnapshot& snapshot);

    export struct FrozenSnapshotRuntime {
        explicit FrozenSnapshotRuntime(VulkanRuntime& runtime) noexcept;
        ~FrozenSnapshotRuntime();

        FrozenSnapshotRuntime(const FrozenSnapshotRuntime&)            = delete;
        FrozenSnapshotRuntime(FrozenSnapshotRuntime&&)                 = delete;
        FrozenSnapshotRuntime& operator=(const FrozenSnapshotRuntime&) = delete;
        FrozenSnapshotRuntime& operator=(FrozenSnapshotRuntime&&)      = delete;

        void initialize(std::span<const std::byte> payload);
        void destroy() noexcept;
        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] std::span<const GpuVisualization> visualizations() const noexcept;
        [[nodiscard]] const FrozenSnapshot& snapshot() const noexcept;
        [[nodiscard]] const DynamicSnapshot& pending_snapshot() const noexcept;

    private:
        struct Buffer {
            GpuBuffer gpu{};
            DescriptorLease descriptor{};
        };
        VulkanRuntime& runtime;
        FrozenSnapshot data{};
        std::deque<Buffer> buffers{};
        DynamicSnapshot gpu_snapshot{};
        std::vector<GpuVisualization> gpu_visualizations{};
        bool ready{};
    };
} // namespace spectra::dynamics
