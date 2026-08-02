#include "cuda_interop.h"
#include <spectra/plugin_api.h>

import std;
import xayah.projects.pyro;

namespace {
    template <std::size_t Size>
    constexpr SpectraPluginString text(const char (&value)[Size]) noexcept {
        return {value, Size - 1};
    }

    constexpr std::uint64_t pyro_voxel_count = 64ull * 96ull * 64ull;
    constexpr std::array ports{
        SpectraPluginPortDescriptor{text("initial_volume"), text("Initial Volume"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::Volume, SpectraPluginMemoryDomain::Host, pyro_voxel_count, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Density)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)), SpectraPluginMeshUpdateMode::Deformable, {64, 96, 64}},
        SpectraPluginPortDescriptor{text("volume"), text("Pyro Volume"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::Volume, SpectraPluginMemoryDomain::CudaExternal, pyro_voxel_count, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Density)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Velocity)), SpectraPluginMeshUpdateMode::Deformable, {64, 96, 64}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("pressure_iterations"), text("Pressure iterations"), {}, SpectraPluginParameterKind::Integer, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Integer, 64, {}}, {SpectraPluginParameterKind::Integer, 1, {}}, {SpectraPluginParameterKind::Integer, 256, {}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("density_buoyancy"), text("Density buoyancy"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.15, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {5.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("temperature_buoyancy"), text("Temperature buoyancy"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {1.2, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {10.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("vorticity"), text("Vorticity"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.22, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {5.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("source_density"), text("Source density"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::Live, {SpectraPluginParameterKind::Float, 0, {18.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {100.0, 0.0, 0.0}}, nullptr, 0},
    };
    constexpr std::array telemetry_descriptors{
        SpectraPluginTelemetryDescriptor{text("voxel_count"), text("Voxels"), {}},
        SpectraPluginTelemetryDescriptor{text("resolution"), text("Resolution"), {}},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.volume.pyro"),
        text("Pyro"),
        text("spectra.dynamic.volume"),
        1,
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
        telemetry_descriptors.data(),
        telemetry_descriptors.size(),
    };

    struct Plugin {
        struct Slot {
            xayah::projects::cuda_interop::Buffer density{};
            xayah::projects::cuda_interop::Buffer temperature{};
            xayah::projects::cuda_interop::Buffer velocity{};
        };

        xayah::projects::pyro::Config config{};
        xayah::projects::pyro::PlumeSource source{};
        std::optional<xayah::projects::pyro::Solver> solver{std::in_place, this->config};
        std::array<Slot, 2> slots{};
        const float* initial_density{};
        const float* initial_temperature{};
        std::uint64_t initial_voxel_count{};
        xayah::projects::cuda_interop::Timeline timeline{};
        std::uint64_t ready_value{};
        std::uint32_t next_slot{};
        Plugin() {
            this->solver->set_plume_source(this->source);
        }

        ~Plugin() {
            for (Slot& slot : this->slots) {
                xayah::projects::cuda_interop::destroy(slot.density);
                xayah::projects::cuda_interop::destroy(slot.temperature);
                xayah::projects::cuda_interop::destroy(slot.velocity);
            }
            xayah::projects::cuda_interop::destroy(this->timeline);
        }

        void configure(const SpectraPluginPortConfiguration& configuration) {
            for (Slot& slot : this->slots) {
                xayah::projects::cuda_interop::destroy(slot.density);
                xayah::projects::cuda_interop::destroy(slot.temperature);
                xayah::projects::cuda_interop::destroy(slot.velocity);
            }
            xayah::projects::cuda_interop::destroy(this->timeline);
            xayah::projects::cuda_interop::select_device(configuration.vulkan_device_uuid, configuration.vulkan_device_luid, configuration.vulkan_device_node_mask);
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index) {
                const SpectraPluginPortSlot& source_slot = configuration.slots[slot_index];
                Slot& destination                        = this->slots[source_slot.index];
                for (std::uint64_t index = 0; index < source_slot.buffer_count; ++index) {
                    const SpectraPluginBuffer& buffer = source_slot.buffers[index];
                    if (buffer.attribute == SpectraPluginAttribute::Density)
                        destination.density = xayah::projects::cuda_interop::import_buffer(buffer.memory_handle, buffer.byte_size);
                    else if (buffer.attribute == SpectraPluginAttribute::Temperature)
                        destination.temperature = xayah::projects::cuda_interop::import_buffer(buffer.memory_handle, buffer.byte_size);
                    else if (buffer.attribute == SpectraPluginAttribute::Velocity)
                        destination.velocity = xayah::projects::cuda_interop::import_buffer(buffer.memory_handle, buffer.byte_size);
                }
            }
            this->timeline    = xayah::projects::cuda_interop::import_timeline(configuration.timeline_semaphore_handle);
            this->ready_value = 0;
            this->next_slot   = 0;
        }

        void configure_input(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot = 0; slot < configuration.slot_count; ++slot)
                for (std::uint64_t index = 0; index < configuration.slots[slot].buffer_count; ++index) {
                    const SpectraPluginBuffer& buffer = configuration.slots[slot].buffers[index];
                    if (buffer.attribute == SpectraPluginAttribute::Density)
                        this->initial_density = static_cast<const float*>(buffer.host_address);
                    else if (buffer.attribute == SpectraPluginAttribute::Temperature)
                        this->initial_temperature = static_cast<const float*>(buffer.host_address);
                }
        }

        void set_input(const SpectraPluginInputFrame& frame) {
            this->initial_voxel_count = frame.active_count;
        }

        void apply_parameters(const SpectraPluginParameterValue* values, const std::uint64_t count) {
            if (count != parameters.size()) throw std::runtime_error("Pyro Plugin parameter count mismatch");
            this->config.pressure_iterations         = static_cast<std::int32_t>(values[0].integer);
            this->config.buoyancy_density_factor     = static_cast<float>(values[1].floating[0]);
            this->config.buoyancy_temperature_factor = static_cast<float>(values[2].floating[0]);
            this->config.vorticity_confinement       = static_cast<float>(values[3].floating[0]);
            this->source.density                     = static_cast<float>(values[4].floating[0]);
            this->solver->set_plume_source(this->source);
        }

        void reset() {
            this->solver.emplace(this->config);
            this->solver->set_initial_fields(std::span<const float>{this->initial_density, this->initial_voxel_count}, std::span<const float>{this->initial_temperature, this->initial_voxel_count});
            this->solver->set_plume_source(this->source);
        }

        void iterate(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
            xayah::projects::cuda_interop::synchronize(this->solver->device_frame().stream);
        }

        void publish(const SpectraPluginFrameSink& sink) {
            const xayah::projects::pyro::DeviceFrame frame = this->solver->device_frame();
            if (this->ready_value != 0) xayah::projects::cuda_interop::wait(frame.stream, this->timeline, this->ready_value + 1);
            Slot& slot = this->slots[this->next_slot];
            xayah::projects::cuda_interop::copy_float(frame.stream, slot.density.pointer, frame.density, frame.cell_count);
            xayah::projects::cuda_interop::copy_float(frame.stream, slot.temperature.pointer, frame.temperature, frame.cell_count);
            xayah::projects::cuda_interop::pack_float3(frame.stream, slot.velocity.pointer, frame.velocity_x, frame.velocity_y, frame.velocity_z, frame.cell_count);
            this->ready_value += this->ready_value == 0 ? 1 : 2;
            xayah::projects::cuda_interop::signal(frame.stream, this->timeline, this->ready_value);
            const SpectraPluginOutputCommit commit{
                this->next_slot,
                frame.cell_count,
                0,
                this->ready_value,
                {},
                {
                    this->config.resolution[0],
                    this->config.resolution[1],
                    this->config.resolution[2],
                },
                0,
            };
            sink.commit_output(sink.state, 1, &commit);
            this->next_slot = (this->next_slot + 1) % this->slots.size();
        }
    };

    SpectraPluginProviderDescriptor describe_provider() {
        return provider;
    }

    void* create_provider() {
        return new Plugin{};
    }

    void destroy_provider(void* instance) {
        delete static_cast<Plugin*>(instance);
    }

    void configure_port(void* instance, const SpectraPluginPortConfiguration* configuration) {
        if (configuration->direction == SpectraPluginPortDirection::Input)
            static_cast<Plugin*>(instance)->configure_input(*configuration);
        else
            static_cast<Plugin*>(instance)->configure(*configuration);
    }
    void set_input_frame(void* instance, const SpectraPluginInputFrame* frame) {
        static_cast<Plugin*>(instance)->set_input(*frame);
    }

    void apply_parameters(void* instance, const SpectraPluginParameterValue* values, const std::uint64_t count) {
        static_cast<Plugin*>(instance)->apply_parameters(values, count);
    }

    void reset(void* instance, std::uint64_t) {
        static_cast<Plugin*>(instance)->reset();
    }

    void step(void* instance, const double step_seconds, const std::uint64_t count) {
        static_cast<Plugin*>(instance)->iterate(step_seconds, count);
    }

    double telemetry_value(const void* instance, const std::uint64_t index) {
        const Plugin& plugin = *static_cast<const Plugin*>(instance);
        if (index == 0) return static_cast<double>(ports[0].capacity);
        return static_cast<double>(plugin.config.resolution[0]);
    }

    void publish_frame(void* instance, std::uint64_t, const SpectraPluginFrameSink* sink) {
        static_cast<Plugin*>(instance)->publish(*sink);
    }
} // namespace

extern "C" __declspec(dllexport) const SpectraPluginApi* spectra_plugin_api_11() {
    static constexpr SpectraPluginApi api{
        SPECTRA_PLUGIN_API_VERSION,
        sizeof(SpectraPluginApi),
        &describe_provider,
        &create_provider,
        &destroy_provider,
        &configure_port,
        &set_input_frame,
        &apply_parameters,
        &reset,
        &step,
        &telemetry_value,
        &publish_frame,
    };
    return &api;
}
