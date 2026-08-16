module;

#include "../../../sdk/internal/abi.h"

export module spectra.dynamics.runtime;

export import spectra.dynamics;
export import spectra.dynamics.gpu;

import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra {
    export struct DynamicsRuntime {
        explicit DynamicsRuntime(VulkanRuntime& runtime) noexcept;
        ~DynamicsRuntime();

        DynamicsRuntime(const DynamicsRuntime&)            = delete;
        DynamicsRuntime(DynamicsRuntime&&)                 = delete;
        DynamicsRuntime& operator=(const DynamicsRuntime&) = delete;
        DynamicsRuntime& operator=(DynamicsRuntime&&)      = delete;

        void initialize(const std::filesystem::path& scene_path, const scene::Scene& source_scene, scene::Scene& evaluated_scene);
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
        [[nodiscard]] bool controls(scene::ParticleSetId particle_set_id) const noexcept;
        [[nodiscard]] bool controls(scene::CameraId camera_id) const noexcept;
        [[nodiscard]] const dynamics::CameraReferenceImage* camera_reference(scene::CameraId camera_id) const noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] bool faulted() const noexcept;
        [[nodiscard]] dynamics::SimulationTimeline timeline() const noexcept;
        void start();
        void pause();
        void step();
        void advance();
        void evaluate(std::uint64_t simulation_step);
        void evaluate_time(double simulation_seconds);
        void reset();
        [[nodiscard]] bool apply_parameter_changes(std::size_t system_index, std::span<const scene::DynamicParameterSetting> parameters, bool reset);

        [[nodiscard]] const dynamics::DynamicSnapshot* acquire_snapshot();
        void consume_snapshot();
        void record_telemetry(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void resolve_telemetry(std::uint32_t frame_slot_index);

    private:
        struct ProviderLibrary {
            explicit ProviderLibrary(const std::filesystem::path& library_path);
            ~ProviderLibrary();

            ProviderLibrary(const ProviderLibrary&)            = delete;
            ProviderLibrary(ProviderLibrary&&)                 = delete;
            ProviderLibrary& operator=(const ProviderLibrary&) = delete;
            ProviderLibrary& operator=(ProviderLibrary&&)      = delete;

            void* library_handle{};
            const SpectraSdkApi* api{};
            SpectraSdkProviderDescriptor descriptor{};
            dynamics::ProviderDescriptor provider{};
        };

        struct OutputBuffer {
            GpuBuffer gpu_buffer{};
            DescriptorLease descriptor{};
            std::uint64_t byte_size{};
        };

        struct OutputRuntime {
            dynamics::DatasetDescriptor descriptor{};
            std::optional<scene::DynamicSceneBinding> scene_binding{};
            std::vector<scene::DynamicVisualizationView> visualizations{};
            std::vector<OutputBuffer> static_buffers{};
            std::vector<std::vector<OutputBuffer>> slots{};
            std::uint32_t capacity{};
            std::uint32_t secondary_capacity{};
            math::UInt3 resolution{};
            SpectraSdkOutputKind kind{};
        };

        struct TelemetryReadbackSlot {
            GpuBuffer buffer{};
            std::uint64_t simulation_step{};
            double simulation_seconds{};
            bool pending{};
        };

        struct SystemRuntime {
            std::size_t scene_system_index{};
            const dynamics::ProviderDescriptor* provider_descriptor{};
            const SpectraSdkApi* api{};
            void* provider_instance{};
            std::vector<scene::DynamicParameterValue> parameter_values{};
            std::vector<OutputRuntime> outputs{};
            GpuExternalTimelineSemaphore timeline{};
            std::uint64_t signal_value{};
            std::uint32_t current_slot{};
            bool output_pending{};
            std::array<TelemetryReadbackSlot, VulkanFrames::frames_in_flight> telemetry_readback{};
            dynamics::TelemetrySnapshot telemetry{};
        };

        struct {
            VulkanRuntime& runtime;
        } context;

        struct {
            const scene::Scene* source_scene{};
            scene::Scene* evaluated_scene{};
            std::filesystem::path assets{};
            scene::DynamicSetup setup{};
            std::uint64_t next_camera_id{};
            bool initialized{};
            bool faulted{};
        } configuration;

        struct {
            std::deque<ProviderLibrary> libraries{};
            std::unordered_map<std::string, ProviderLibrary*> by_id{};
        } providers;

        struct {
            std::vector<SystemRuntime> values{};
        } systems;

        struct {
            std::uint64_t simulation_step{};
            bool playing{};
        } clock;

        struct {
            dynamics::DynamicSnapshot snapshot{};
            SystemRuntime* configuring_system{};
            bool snapshot_pending{};
            bool snapshot_acquired{};
            std::string callback_error{};
        } publication;

        struct {
            std::vector<dynamics::MeshOutputBinding> mesh_bindings{};
            std::vector<dynamics::SphereSetOutputBinding> sphere_set_bindings{};
            std::vector<dynamics::CameraReferenceImage> camera_references{};
        } outputs;

        [[nodiscard]] ProviderLibrary& provider_library(std::string_view provider_id) const;
        static SpectraSdkResult configure_output(void* context, const SpectraSdkOutputLayout* layout, SpectraSdkOutputRequest* request) noexcept;
        static void release_output(void* lifetime) noexcept;
        void bind_output(OutputRuntime& output, const scene::DynamicSystem& system) const;
        void declare_outputs();
        void declare_scene_output(OutputRuntime& output);
        void create_system(SystemRuntime& system, const scene::DynamicSystem& declared);
        void apply_parameters(SystemRuntime& system, std::span<const scene::DynamicParameterValue> values);
        void append_output(const SystemRuntime& system, const OutputRuntime& output, const SpectraSdkOutputCommit& commit, dynamics::DynamicSnapshot& snapshot) const;
        void discard_pending_snapshot();
        void publish_frame();
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate_frame(std::uint64_t target_step);
        void reset_simulation();
        void advance_one_step();
    };
}
