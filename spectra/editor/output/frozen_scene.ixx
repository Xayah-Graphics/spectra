export module spectra.editor:output.frozen_scene;

import spectra.render.scene;
import spectra.dynamics.frozen;
import spectra.dynamics.runtime;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export enum class FrozenSceneReadbackKind : std::uint8_t {
        GeometryPosition,
        GeometryNormal,
        GeometryTangent,
        GeometryTextureCoordinate,
        GeometryIndex,
        SpherePosition,
        SphereRadius,
        InstanceTransform,
        VolumeField,
        VisualizationBuffer,
        TelemetryValues,
        SceneBounds,
    };

    export struct FrozenSceneReadbackRegion {
        FrozenSceneReadbackKind kind{};
        std::uint32_t resource_index{};
        std::uint32_t primitive_index{};
        GpuVolumeField volume_field{};
        vk::DeviceSize offset{};
        std::uint64_t element_count{};
    };

    export struct FrozenSceneSnapshot {
        scene::Scene frozen_scene{};
        GpuBuffer readback_buffer{};
        std::vector<FrozenSceneReadbackRegion> readback_regions{};
        std::optional<dynamics::FrozenFrame> frozen_frame{};

        void materialize();
    };

    export struct FrozenSceneExporter {
        struct FrameSlot {
            std::optional<FrozenSceneSnapshot> snapshot{};
            std::filesystem::path output_path{};
            std::filesystem::path source_scene_path{};
        };

        FrozenSceneExporter(VulkanRuntime& runtime, GpuScene& gpu_scene, DynamicsRuntime& dynamics) noexcept;
        ~FrozenSceneExporter();

        FrozenSceneExporter(const FrozenSceneExporter&)            = delete;
        FrozenSceneExporter(FrozenSceneExporter&&)                 = delete;
        FrozenSceneExporter& operator=(const FrozenSceneExporter&) = delete;
        FrozenSceneExporter& operator=(FrozenSceneExporter&&)      = delete;

        void request(const std::filesystem::path& path);
        [[nodiscard]] bool in_progress() const noexcept;
        [[nodiscard]] std::optional<std::expected<std::filesystem::path, std::string>> begin_frame(std::uint32_t frame_slot_index);
        void record_snapshot(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index, const scene::Scene& scene, const scene::Camera& camera, vk::Extent2D extent, float exposure, const std::filesystem::path& source_scene_path);
        void wait();

        struct {
            VulkanRuntime& runtime;
            GpuScene& gpu_scene;
            DynamicsRuntime& dynamics;
        } context;

        struct {
            std::vector<FrameSlot> slots{};
            std::optional<std::filesystem::path> pending_request{};
            std::future<std::expected<std::filesystem::path, std::string>> task{};
            std::optional<std::expected<std::filesystem::path, std::string>> completed_result{};
        } export_state;

    private:
        [[nodiscard]] std::optional<std::expected<std::filesystem::path, std::string>> take_result();
    };
} // namespace spectra
