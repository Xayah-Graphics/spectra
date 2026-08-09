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
        SpectraPluginPortDescriptor{text("initial_volume"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::Volume, SpectraPluginMemoryDomain::Host, pyro_voxel_count, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Density)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)), SpectraPluginMeshUpdateMode::Deformable, {64, 96, 64}},
        SpectraPluginPortDescriptor{text("volume"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::Volume, SpectraPluginMemoryDomain::CudaExternal, pyro_voxel_count, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Density)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Velocity)), SpectraPluginMeshUpdateMode::Deformable, {64, 96, 64}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("pressure_iterations"), text("Pressure iterations"), {}, SpectraPluginParameterKind::Integer, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Integer, 64, {}}, {SpectraPluginParameterKind::Integer, 1, {}}, {SpectraPluginParameterKind::Integer, 256, {}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("density_buoyancy"), text("Density buoyancy"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.15, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {5.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("temperature_buoyancy"), text("Temperature buoyancy"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {1.2, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {10.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("vorticity"), text("Vorticity"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.22, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {5.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("source_density"), text("Source density"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::Live, {SpectraPluginParameterKind::Float, 0, {18.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {100.0, 0.0, 0.0}}, nullptr, 0},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.volume.pyro"),
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
    };

    struct Provider {
        struct OutputSlot {
            xayah::projects::cuda_interop::ImportedBuffer density{};
            xayah::projects::cuda_interop::ImportedBuffer temperature{};
            xayah::projects::cuda_interop::ImportedBuffer velocity{};
        };

        xayah::projects::pyro::SolverConfiguration configuration{};
        xayah::projects::pyro::PlumeSource source{};
        std::optional<xayah::projects::pyro::Solver> solver{std::in_place, this->configuration};
        std::array<OutputSlot, 2> output_slots{};
        const float* initial_density{};
        const float* initial_temperature{};
        std::uint64_t initial_voxel_count{};
        xayah::projects::cuda_interop::ImportedTimelineSemaphore timeline_semaphore{};
        std::uint64_t ready_timeline_value{};
        std::uint32_t next_output_slot_index{};
        Provider() {
            this->solver->set_plume_source(this->source);
        }

        ~Provider() {
            for (OutputSlot& output_slot : this->output_slots) {
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.density);
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.temperature);
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.velocity);
            }
            xayah::projects::cuda_interop::destroy_imported_timeline_semaphore(this->timeline_semaphore);
        }

        void configure_output(const SpectraPluginPortConfiguration& configuration) {
            for (OutputSlot& output_slot : this->output_slots) {
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.density);
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.temperature);
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.velocity);
            }
            xayah::projects::cuda_interop::destroy_imported_timeline_semaphore(this->timeline_semaphore);
            xayah::projects::cuda_interop::select_matching_device(configuration.vulkan_device_uuid, configuration.vulkan_device_luid, configuration.vulkan_device_node_mask);
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index) {
                const SpectraPluginPortSlot& source_slot = configuration.slots[slot_index];
                OutputSlot& destination                  = this->output_slots[source_slot.slot_index];
                for (std::uint64_t index = 0; index < source_slot.buffer_count; ++index) {
                    const SpectraPluginBuffer& buffer = source_slot.buffers[index];
                    if (buffer.attribute == SpectraPluginAttribute::Density)
                        destination.density = xayah::projects::cuda_interop::import_buffer(buffer.external_memory_handle, buffer.byte_size);
                    else if (buffer.attribute == SpectraPluginAttribute::Temperature)
                        destination.temperature = xayah::projects::cuda_interop::import_buffer(buffer.external_memory_handle, buffer.byte_size);
                    else if (buffer.attribute == SpectraPluginAttribute::Velocity)
                        destination.velocity = xayah::projects::cuda_interop::import_buffer(buffer.external_memory_handle, buffer.byte_size);
                }
            }
            this->timeline_semaphore     = xayah::projects::cuda_interop::import_timeline_semaphore(configuration.timeline_semaphore_handle);
            this->ready_timeline_value   = 0;
            this->next_output_slot_index = 0;
        }

        void configure_input(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index)
                for (std::uint64_t buffer_index = 0; buffer_index < configuration.slots[slot_index].buffer_count; ++buffer_index) {
                    const SpectraPluginBuffer& buffer = configuration.slots[slot_index].buffers[buffer_index];
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
            if (count != parameters.size()) throw std::runtime_error("Pyro Provider parameter count mismatch");
            this->configuration.pressure_iterations         = static_cast<std::int32_t>(values[0].integer);
            this->configuration.buoyancy_density_factor     = static_cast<float>(values[1].floating[0]);
            this->configuration.buoyancy_temperature_factor = static_cast<float>(values[2].floating[0]);
            this->configuration.vorticity_confinement       = static_cast<float>(values[3].floating[0]);
            this->source.density                            = static_cast<float>(values[4].floating[0]);
            this->solver->set_runtime_configuration(this->configuration);
            this->solver->set_plume_source(this->source);
        }

        void reset() {
            this->solver.emplace(this->configuration);
            this->solver->set_initial_fields(std::span<const float>{this->initial_density, this->initial_voxel_count}, std::span<const float>{this->initial_temperature, this->initial_voxel_count});
            this->solver->set_plume_source(this->source);
        }

        void advance_simulation(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
            xayah::projects::cuda_interop::synchronize_stream(this->solver->cuda_volume().cuda_stream);
        }

        void publish(const SpectraPluginFrameSink& sink) {
            const xayah::projects::pyro::CudaVolumeView volume = this->solver->cuda_volume();
            if (this->ready_timeline_value != 0) xayah::projects::cuda_interop::wait_timeline(volume.cuda_stream, this->timeline_semaphore, this->ready_timeline_value + 1);
            OutputSlot& output_slot = this->output_slots[this->next_output_slot_index];
            xayah::projects::cuda_interop::copy_float_buffer(volume.cuda_stream, output_slot.density.device_pointer, volume.density, volume.cell_count);
            xayah::projects::cuda_interop::copy_float_buffer(volume.cuda_stream, output_slot.temperature.device_pointer, volume.temperature, volume.cell_count);
            xayah::projects::cuda_interop::pack_float3_buffer(volume.cuda_stream, output_slot.velocity.device_pointer, volume.velocity_x, volume.velocity_y, volume.velocity_z, volume.cell_count);
            this->ready_timeline_value += this->ready_timeline_value == 0 ? 1 : 2;
            xayah::projects::cuda_interop::signal_timeline(volume.cuda_stream, this->timeline_semaphore, this->ready_timeline_value);
            const SpectraPluginOutputCommit commit{
                this->next_output_slot_index,
                volume.cell_count,
                0,
                this->ready_timeline_value,
                {},
                {
                    this->configuration.resolution[0],
                    this->configuration.resolution[1],
                    this->configuration.resolution[2],
                },
                0,
            };
            sink.commit_output(sink.context, 1, &commit);
            this->next_output_slot_index = (this->next_output_slot_index + 1) % this->output_slots.size();
        }
    };

    SpectraPluginProviderDescriptor describe_provider() {
        return provider;
    }

    void* create_provider() {
        return new Provider{};
    }

    void destroy_provider(void* instance) {
        delete static_cast<Provider*>(instance);
    }

    void configure_port(void* instance, const SpectraPluginPortConfiguration* configuration) {
        if (configuration->direction == SpectraPluginPortDirection::Input)
            static_cast<Provider*>(instance)->configure_input(*configuration);
        else
            static_cast<Provider*>(instance)->configure_output(*configuration);
    }
    void set_input_frame(void* instance, const SpectraPluginInputFrame* frame) {
        static_cast<Provider*>(instance)->set_input(*frame);
    }

    void apply_parameters(void* instance, const SpectraPluginParameterValue* values, const std::uint64_t count) {
        static_cast<Provider*>(instance)->apply_parameters(values, count);
    }

    void reset(void* instance, std::uint64_t) {
        static_cast<Provider*>(instance)->reset();
    }

    void step(void* instance, const double step_seconds, const std::uint64_t count) {
        static_cast<Provider*>(instance)->advance_simulation(step_seconds, count);
    }

    void publish_frame(void* instance, std::uint64_t, const SpectraPluginFrameSink* sink) {
        static_cast<Provider*>(instance)->publish(*sink);
    }

} // namespace

extern "C" SPECTRA_PLUGIN_EXPORT const SpectraPluginApi* spectra_plugin_api_14() {
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
        &publish_frame,
    };
    return &api;
}
