module;

#include <spectra/plugin_api.h>

export module spectra.dynamics.runtime;

export import spectra.dynamics;
export import spectra.dynamics.gpu;

import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct DynamicsRuntime {
        DynamicsRuntime(VulkanRuntime& runtime) noexcept;
        ~DynamicsRuntime();

        DynamicsRuntime(const DynamicsRuntime&)            = delete;
        DynamicsRuntime(DynamicsRuntime&&)                 = delete;
        DynamicsRuntime& operator=(const DynamicsRuntime&) = delete;
        DynamicsRuntime& operator=(DynamicsRuntime&&)      = delete;

        void initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene);
        void destroy() noexcept;
        [[nodiscard]] bool initialized() const noexcept;

        [[nodiscard]] const dynamics::ProviderDescriptor& provider_descriptor(std::string_view provider_id) const;
        [[nodiscard]] const dynamics::TelemetrySnapshot& telemetry(std::size_t system_index) const;
        void write_telemetry(const std::filesystem::path& path) const;
        [[nodiscard]] std::span<const dynamics::MeshOutputBinding> mesh_bindings() const noexcept;
        [[nodiscard]] std::span<const dynamics::SphereSetOutputBinding> sphere_set_bindings() const noexcept;
        [[nodiscard]] std::span<const dynamics::GpuVisualization> visualizations() const noexcept;
        [[nodiscard]] bool controls(scene::InstanceId instance_id) const noexcept;
        [[nodiscard]] bool controls(scene::VolumeId volume_id) const noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] dynamics::SimulationTimeline timeline() const noexcept;
        void start();
        void pause();
        void step();
        void advance();
        void evaluate(std::uint64_t simulation_step);
        void evaluate_time(double simulation_seconds);
        void reset();
        void apply_parameter_changes(std::size_t system_index, std::span<const scene::DynamicParameterSetting> parameters, bool reset);

        [[nodiscard]] const dynamics::DynamicSnapshot* acquire_snapshot() noexcept;
        void consume_snapshot() noexcept;
        void record_telemetry(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void resolve_telemetry(std::uint32_t frame_slot_index);

    private:
        struct ProviderLibrary {
            ProviderLibrary(const std::filesystem::path& library_path, std::string_view expected_provider_id);
            ~ProviderLibrary();

            ProviderLibrary(const ProviderLibrary&)            = delete;
            ProviderLibrary(ProviderLibrary&&)                 = delete;
            ProviderLibrary& operator=(const ProviderLibrary&) = delete;
            ProviderLibrary& operator=(ProviderLibrary&&)      = delete;

            void* library_handle{};
            const SpectraPluginApi* plugin_api{};
            SpectraPluginProviderDescriptor descriptor{};
            dynamics::ProviderDescriptor provider{};
        };

        struct DynamicDatasetBuffer {
            SpectraPluginBufferSemantic semantic{};
            std::uint32_t channel_index{};
            GpuBuffer gpu_buffer{};
            DescriptorLease descriptor{};
            std::uint64_t byte_size{};
        };

        struct DynamicDatasetRuntime {
            dynamics::DatasetDescriptor descriptor{};
            std::optional<scene::DynamicSceneBinding> scene_binding{};
            std::vector<scene::DynamicVisualizationView> visualizations{};
            std::vector<std::vector<DynamicDatasetBuffer>> buffer_slots{};
            GpuExternalTimelineSemaphore timeline_semaphore{};
            std::uint32_t capacity{};
            std::uint32_t secondary_capacity{};
            std::uint64_t timeline_signal_value{};
            bool output_pending{};
        };

        struct TelemetryReadbackSlot {
            GpuBuffer buffer{};
            std::uint64_t simulation_step{};
            double simulation_seconds{};
            std::string phase{};
            std::string headline{};
            std::string message{};
            bool pending{};
        };

        struct DynamicTelemetryRuntime {
            std::vector<std::vector<DynamicDatasetBuffer>> buffer_slots{};
            GpuExternalTimelineSemaphore timeline_semaphore{};
            std::array<TelemetryReadbackSlot, VulkanFrames::frames_in_flight> readback_slots{};
            std::uint64_t timeline_signal_value{};
            std::uint64_t simulation_step{};
            double simulation_seconds{};
            std::uint32_t current_slot_index{};
            std::string phase{};
            std::string headline{};
            std::string message{};
            bool output_pending{};
        };

        struct DynamicSystemRuntime {
            std::size_t scene_system_index{};
            const dynamics::ProviderDescriptor* provider_descriptor{};
            const SpectraPluginApi* plugin_api{};
            void* provider_instance{};
            std::vector<scene::DynamicParameterValue> parameter_values{};
            std::vector<DynamicDatasetRuntime> datasets{};
            DynamicTelemetryRuntime telemetry_gpu{};
            dynamics::TelemetrySnapshot telemetry{};
        };

        struct PendingDatasetCommit {
            DynamicSystemRuntime* system{};
            DynamicDatasetRuntime* dataset{};
            SpectraPluginDatasetCommit commit{};
        };

        struct PendingTelemetryCommit {
            DynamicSystemRuntime* system{};
            std::uint32_t slot_index{};
            std::uint64_t signal_value{};
            std::string phase{};
            std::string headline{};
            std::string message{};
        };

        struct {
            VulkanRuntime& runtime;
        } context;

        struct {
            const scene::Scene* source_scene{};
            scene::DynamicSetup setup{};
        } configuration;

        struct {
            std::deque<ProviderLibrary> libraries{};
            std::unordered_map<std::string, ProviderLibrary*> by_id{};
        } providers;

        struct {
            std::vector<DynamicSystemRuntime> runtimes{};
        } systems;

        struct {
            std::uint64_t simulation_step{};
            bool playing{};
        } clock;

        struct {
            dynamics::DynamicSnapshot snapshot{};
            DynamicSystemRuntime* publishing_system{};
            std::vector<PendingDatasetCommit> dataset_commits{};
            std::vector<PendingTelemetryCommit> telemetry_commits{};
            std::string callback_error{};
            bool snapshot_pending{};
        } publication;

        struct {
            std::vector<dynamics::MeshOutputBinding> mesh_bindings{};
            std::vector<dynamics::SphereSetOutputBinding> sphere_set_bindings{};
        } outputs;

        [[nodiscard]] ProviderLibrary& provider_library(std::string_view provider_id) const;
        static SpectraPluginResult collect_dataset(void* context, std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit) noexcept;
        static SpectraPluginResult collect_telemetry(void* context, const SpectraPluginTelemetryCommit* commit) noexcept;
        void bind_dataset(DynamicDatasetRuntime& dataset, const scene::DynamicSystem& system) const;
        void declare_scene_output(const DynamicDatasetRuntime& dataset);
        void configure_dataset(DynamicSystemRuntime& system, std::size_t dataset_index);
        void configure_telemetry(DynamicSystemRuntime& system);
        [[nodiscard]] DynamicDatasetRuntime& dataset_runtime(DynamicSystemRuntime& system, std::uint64_t dataset_index);
        void consume_telemetry(DynamicSystemRuntime& system, const SpectraPluginTelemetryGpuValue* values, std::uint64_t simulation_step, double simulation_seconds, std::string phase, std::string headline, std::string message);
        void apply_parameters(DynamicSystemRuntime& system);
        void append_dataset(const PendingDatasetCommit& pending, dynamics::DynamicSnapshot& snapshot) const;
        void abort_publication();
        void commit_publication(dynamics::DynamicSnapshot& snapshot, std::uint64_t simulation_step);
        void discard_pending_snapshot();
        void publish_snapshot(std::uint64_t simulation_step);
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate_frame(std::uint64_t target_step);
        void reset_simulation();
        void advance_one_step();
    };
} // namespace spectra
