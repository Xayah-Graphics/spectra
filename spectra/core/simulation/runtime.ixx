module;

#include <abi.h>

export module spectra.simulation.runtime;

export import spectra.simulation;
export import spectra.simulation.frame;

import spectra.simulation.provider;
import spectra.runtime;
import spectra.scene;
import std;
import vulkan;

namespace spectra::simulation {
    export struct Runtime {
        explicit Runtime(runtime::VulkanRuntime& runtime) noexcept;
        ~Runtime();

        Runtime(const Runtime&)            = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime& operator=(Runtime&&)      = delete;

        void initialize(const std::filesystem::path& scene_path, const scene::Scene& authored_scene, scene::Scene& evaluated_scene);
        void destroy() noexcept;
        [[nodiscard]] bool initialized() const noexcept;

        [[nodiscard]] const ProviderDescriptor& provider_descriptor(std::string_view provider_id) const;
        [[nodiscard]] const TelemetrySnapshot& telemetry(std::size_t system_index) const;
        void write_telemetry(const std::filesystem::path& path) const;
        [[nodiscard]] std::span<const MeshOutputBinding> mesh_bindings() const noexcept;
        [[nodiscard]] std::span<const SphereSetOutputBinding> sphere_set_bindings() const noexcept;
        [[nodiscard]] std::span<const GpuVisualization> visualizations() const noexcept;
        [[nodiscard]] bool controls(scene::InstanceId instance_id) const noexcept;
        [[nodiscard]] bool controls(scene::VolumeId volume_id) const noexcept;
        [[nodiscard]] bool controls(scene::ParticleSetId particle_set_id) const noexcept;
        [[nodiscard]] bool controls(scene::CameraId camera_id) const noexcept;
        [[nodiscard]] const CameraReferenceImage* camera_reference(scene::CameraId camera_id) const noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] bool faulted() const noexcept;
        [[nodiscard]] SimulationTimeline timeline() const noexcept;
        void start();
        void pause();
        void step();
        void advance();
        void evaluate(std::uint64_t simulation_step);
        void evaluate_time(double simulation_seconds);
        void reset();
        [[nodiscard]] bool apply_parameter_changes(std::size_t system_index, std::span<const scene::SimulationParameterSetting> parameters, bool reset);

        [[nodiscard]] const SimulationFrame* acquire_frame();
        void consume_frame();
        void record_telemetry(const vk::raii::CommandBuffer& command_buffer, std::uint32_t frame_slot_index);
        void resolve_telemetry(std::uint32_t frame_slot_index);

    private:
        struct OutputBuffer {
            runtime::GpuBuffer gpu_buffer{};
            runtime::DescriptorLease descriptor{};
        };

        struct OutputStorage {
            std::vector<OutputBuffer> static_buffers{};
            std::vector<std::vector<OutputBuffer>> slots{};
        };

        struct OutputRuntime {
            OutputDescriptor descriptor;
            std::optional<scene::SimulationOutputBinding> scene_binding{};
            std::vector<scene::SimulationVisualization> visualizations{};
            OutputStorage storage{};

            explicit OutputRuntime(OutputDescriptor descriptor) : descriptor(std::move(descriptor)) {}
        };

        struct MetricRuntime {
            OutputStorage storage{};
        };

        struct TelemetryReadbackSlot {
            runtime::GpuBuffer buffer{};
            std::uint64_t simulation_step{};
            double simulation_seconds{};
            bool pending{};
        };

        struct SystemRuntime {
            std::size_t scene_system_index{};
            const ProviderDescriptor* provider_descriptor{};
            const SpectraSdkApi* api{};
            void* provider_instance{};
            std::vector<scene::SimulationParameterValue> parameter_values{};
            std::vector<OutputRuntime> outputs{};
            MetricRuntime metrics{};
            runtime::GpuExternalTimelineSemaphore timeline{};
            std::uint64_t signal_value{};
            std::uint32_t current_slot{};
            bool output_pending{};
            std::array<TelemetryReadbackSlot, runtime::VulkanFrames::frames_in_flight> telemetry_readback{};
            TelemetrySnapshot telemetry{};
        };

        runtime::VulkanRuntime& vulkan;

        struct {
            const scene::Scene* authored_scene{};
            scene::Scene* evaluated_scene{};
            std::filesystem::path assets{};
            scene::SimulationSetup setup{};
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
            SimulationFrame frame{};
            SystemRuntime* configuring_system{};
            bool frame_pending{};
            bool frame_acquired{};
            std::string callback_error{};
        } publication;

        struct {
            std::vector<MeshOutputBinding> mesh_bindings{};
            std::vector<SphereSetOutputBinding> sphere_set_bindings{};
            std::vector<CameraReferenceImage> camera_references{};
        } outputs;

        [[nodiscard]] ProviderLibrary& provider_library(std::string_view provider_id) const;
        static SpectraSdkResult configure_output(void* context, const SpectraSdkOutputLayout* layout, SpectraSdkOutputRequest* request) noexcept;
        static void release_output(void* lifetime) noexcept;
        void bind_output(OutputRuntime& output, const scene::SimulationSystem& system) const;
        void declare_outputs();
        void declare_scene_output(OutputRuntime& output);
        void create_system(SystemRuntime& system, const scene::SimulationSystem& declared);
        void apply_parameters(SystemRuntime& system, std::span<const scene::SimulationParameterValue> values);
        void append_output(const SystemRuntime& system, const OutputRuntime& output, const SpectraSdkOutputCommit& commit, SimulationFrame& frame) const;
        void discard_pending_frame();
        void publish_frame();
        void step_to(std::uint64_t target_step);
        void reset_systems();
        void evaluate_frame(std::uint64_t target_step);
        void reset_simulation();
        void advance_one_step();
    };
} // namespace spectra::simulation
