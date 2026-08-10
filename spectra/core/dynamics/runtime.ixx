module;

#include <spectra/plugin_api.h>

export module spectra.dynamics.runtime;

export import spectra.dynamics;

import spectra.dynamics.frozen;
import spectra.runtime;
import spectra.scene;
import spectra.scene.document;
import std;
import vulkan;

namespace spectra {
    export struct DynamicsRuntime {
        struct ProviderLibrary {
            ProviderLibrary(const std::filesystem::path& library_path, std::string_view expected_provider_id);
            ~ProviderLibrary();

            ProviderLibrary(const ProviderLibrary&)            = delete;
            ProviderLibrary(ProviderLibrary&&)                 = delete;
            ProviderLibrary& operator=(const ProviderLibrary&) = delete;
            ProviderLibrary& operator=(ProviderLibrary&&)      = delete;

            std::filesystem::path library_path{};
            void* library_handle{};
            const SpectraPluginApi* plugin_api{};
        };

        struct DynamicDatasetBuffer {
            SpectraPluginBufferSemantic semantic{};
            std::uint32_t channel_index{};
            GpuBuffer gpu_buffer{};
            DescriptorLease descriptor{};
            std::uint64_t byte_size{};
        };

        struct DynamicDatasetRuntime {
            std::size_t dataset_index{};
            dynamics::DatasetDescriptor descriptor{};
            std::optional<scene::DynamicSceneBinding> scene_binding{};
            std::vector<scene::DynamicVisualizationView> visualizations{};
            std::vector<std::vector<DynamicDatasetBuffer>> buffer_slots{};
            GpuExternalTimelineSemaphore timeline_semaphore{};
            std::uint64_t capacity{};
            std::uint64_t secondary_capacity{};
            std::uint64_t requested_capacity{};
            std::uint64_t requested_secondary_capacity{};
            std::uint64_t active_count{};
            std::uint64_t secondary_count{};
            std::uint64_t timeline_signal_value{};
            std::uint32_t current_slot_index{};
            std::optional<scene::VolumeRegion> dirty_region{};
            bool output_pending{};
        };

        struct TelemetryReadbackSlot {
            GpuBuffer buffer{};
            std::uint64_t simulation_step{};
            std::uint64_t sequence{};
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
            GpuBuffer immediate_readback{};
            std::uint64_t timeline_signal_value{};
            std::uint64_t simulation_step{};
            std::uint64_t sequence{};
            std::uint64_t next_sequence{};
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

        DynamicsRuntime(VulkanRuntime& runtime, SceneDocument& document) noexcept;
        ~DynamicsRuntime();

        DynamicsRuntime(const DynamicsRuntime&)            = delete;
        DynamicsRuntime(DynamicsRuntime&&)                 = delete;
        DynamicsRuntime& operator=(const DynamicsRuntime&) = delete;
        DynamicsRuntime& operator=(DynamicsRuntime&&)      = delete;

        void initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene);
        void destroy() noexcept;
        void advance(std::chrono::duration<double> elapsed);
        [[nodiscard]] const dynamics::DynamicFrame* pending_frame() noexcept;
        void consume_frame() noexcept;
        void resolve_telemetry(std::uint32_t frame_slot_index);
        void record_telemetry(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        [[nodiscard]] bool controls(scene::InstanceId instance_id) const noexcept;
        [[nodiscard]] bool controls(scene::VolumeId volume_id) const noexcept;
        [[nodiscard]] const dynamics::ProviderDescriptor& provider_descriptor(std::string_view provider_id) const;
        [[nodiscard]] const dynamics::TelemetrySnapshot& telemetry(std::size_t system_index) const;
        [[nodiscard]] bool initialized() const noexcept;
        [[nodiscard]] std::span<const dynamics::MeshOutputBinding> mesh_bindings() const noexcept;
        [[nodiscard]] std::span<const dynamics::SphereSetOutputBinding> sphere_set_bindings() const noexcept;
        [[nodiscard]] std::span<const dynamics::GpuVisualization> visualizations() const noexcept;
        [[nodiscard]] const dynamics::DynamicFrame& published_frame() const noexcept;
        [[nodiscard]] const dynamics::FrozenFrame* frozen_frame() const noexcept;
        [[nodiscard]] dynamics::FrozenFrame telemetry_frame() const;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] dynamics::SimulationTimeline timeline() const noexcept;
        [[nodiscard]] dynamics::PresentationTimeline presentation_timeline() const noexcept;
        void start();
        void pause();
        void step();
        void evaluate(std::uint64_t simulation_step);
        void evaluate_time(double simulation_seconds);
        void reset();
        void apply_parameter_changes(std::size_t system_index, std::span<const scene::DynamicParameterSetting> parameters, bool reset);

    private:
        struct {
            VulkanRuntime& runtime;
            SceneDocument& document;
        } context;

        dynamics::FrozenFrameRuntime frozen;

        struct {
            bool initialized{};
            const scene::Scene* source_scene{};
            scene::DynamicSetup setup{};
        } configuration;

        struct {
            std::deque<ProviderLibrary> libraries{};
            std::unordered_map<std::string, ProviderLibrary*> by_id{};
            std::vector<dynamics::ProviderDescriptor> descriptors{};
        } providers;

        struct {
            std::vector<DynamicSystemRuntime> runtimes{};
        } systems;

        struct {
            std::uint64_t simulation_step{};
            std::uint64_t presentation_frame{};
            double presentation_seconds{};
            std::chrono::duration<double> accumulator{};
            bool playing{};
        } clock;

        struct {
            dynamics::DynamicFrame frame{};
            bool frame_pending{};
            DynamicSystemRuntime* publishing_system{};
            std::vector<PendingDatasetCommit> dataset_commits{};
            std::vector<PendingTelemetryCommit> telemetry_commits{};
            std::string callback_error{};
            bool frozen_frame_pending{};
        } publication;

        struct {
            std::vector<dynamics::MeshOutputBinding> mesh_bindings{};
            std::vector<dynamics::SphereSetOutputBinding> sphere_set_bindings{};
        } outputs;

        [[nodiscard]] ProviderLibrary& provider_library(std::string_view provider_id) const;
        void bind_dataset(DynamicDatasetRuntime& dataset, const scene::DynamicSystem& system) const;
        void declare_scene_output(const DynamicDatasetRuntime& dataset);
        void configure_dataset(DynamicSystemRuntime& system, std::size_t dataset_index);
        void configure_telemetry(DynamicSystemRuntime& system);
        void consume_telemetry(DynamicSystemRuntime& system, const SpectraPluginTelemetryGpuValue* values, std::uint64_t simulation_step, double simulation_seconds, std::string phase, std::string headline, std::string message);
        void flush_telemetry(DynamicSystemRuntime& system);
        [[nodiscard]] DynamicDatasetRuntime& dataset_runtime(DynamicSystemRuntime& system, std::uint64_t dataset_index);
        void apply_parameters(DynamicSystemRuntime& system);
        void append_dataset(const PendingDatasetCommit& pending, dynamics::DynamicFrame& frame) const;
        void abort_publication(std::size_t first_dataset_commit = 0, std::size_t first_telemetry_commit = 0);
        void commit_publication(dynamics::DynamicFrame& frame);
        void publish_frame(std::uint64_t simulation_step);
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate_frame(std::uint64_t target_step);
        void reset_simulation();
        void advance_one_step();
        static SpectraPluginResult collect_dataset(void* context, std::uint64_t dataset_index, const SpectraPluginDatasetCommit* commit) noexcept;
        static SpectraPluginResult collect_capacity(void* context, std::uint64_t dataset_index, std::uint64_t capacity, std::uint64_t secondary_capacity) noexcept;
        static SpectraPluginResult collect_telemetry(void* context, const SpectraPluginTelemetryCommit* commit) noexcept;
    };
} // namespace spectra
