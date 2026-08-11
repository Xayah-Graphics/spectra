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

    export struct FrozenFrame {
        SimulationTimeline simulation{};
        PresentationTimeline presentation{};
        std::vector<FrozenVisualization> visualizations{};
        std::vector<FrozenTelemetrySystem> telemetry{};
    };

    export [[nodiscard]] std::vector<std::byte> serialize_frozen_frame(const FrozenFrame& frame);
    export [[nodiscard]] FrozenFrame deserialize_frozen_frame(std::span<const std::byte> payload);
    export void write_telemetry(const std::filesystem::path& path, const FrozenFrame& frame);

    export struct FrozenFrameRuntime {
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
        [[nodiscard]] const DynamicFrame& pending_frame() const noexcept;

    private:
        struct Buffer {
            GpuBuffer gpu{};
            DescriptorLease descriptor{};
        };
        VulkanRuntime& runtime;
        FrozenFrame data{};
        std::deque<Buffer> buffers{};
        DynamicFrame gpu_frame{};
        std::vector<GpuVisualization> gpu_visualizations{};
        bool ready{};
    };
} // namespace spectra::dynamics
