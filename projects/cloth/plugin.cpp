#include "cuda_interop.h"
#include <spectra/plugin_api.h>

import std;
import xayah.projects.cloth;

namespace {
    template <std::size_t Size>
    constexpr SpectraPluginString text(const char (&value)[Size]) noexcept {
        return {value, Size - 1};
    }

    constexpr std::uint64_t cloth_vertex_count = 45ull * 37ull;
    constexpr std::uint64_t cloth_index_count  = 44ull * 36ull * 6ull;
    constexpr std::array ports{
        SpectraPluginPortDescriptor{text("collider"), text("Sphere Collider"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::InstanceTransform, SpectraPluginMemoryDomain::Host, 1, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Transform)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Bounds)), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("initial_mesh"), text("Initial Cloth Mesh"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::TriangleMesh, SpectraPluginMemoryDomain::Host, cloth_vertex_count, 0, 1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Position), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("mesh"), text("Cloth Mesh"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::TriangleMesh, SpectraPluginMemoryDomain::CudaExternal, cloth_vertex_count, cloth_index_count, 1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Position), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("debug"), text("Constraint Debug"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::DebugDraw, SpectraPluginMemoryDomain::Host, 8, 0, 0, SpectraPluginMeshUpdateMode::Deformable, {}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("gravity"), text("Gravity"), text("m/s²"), SpectraPluginParameterKind::Float3, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float3, 0, {0.0, -9.8, 0.0}}, {SpectraPluginParameterKind::Float3, 0, {-100.0, -100.0, -100.0}}, {SpectraPluginParameterKind::Float3, 0, {100.0, 100.0, 100.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("solver_iterations"), text("Solver iterations"), {}, SpectraPluginParameterKind::Integer, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Integer, 10, {}}, {SpectraPluginParameterKind::Integer, 1, {}}, {SpectraPluginParameterKind::Integer, 64, {}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("stretch_compliance"), text("Stretch compliance"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.000001, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.01, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("bend_compliance"), text("Bend compliance"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.00045, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.1, 0.0, 0.0}}, nullptr, 0},
    };
    constexpr std::array telemetry_descriptors{
        SpectraPluginTelemetryDescriptor{text("vertex_count"), text("Vertices"), {}},
        SpectraPluginTelemetryDescriptor{text("triangle_count"), text("Triangles"), {}},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.cloth.xpbd"),
        text("XPBD Cloth"),
        text("spectra.dynamic.deformable-surface"),
        1,
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
        telemetry_descriptors.data(),
        telemetry_descriptors.size(),
    };

    struct Provider {
        struct OutputSlot {
            xayah::projects::cuda_interop::ImportedBuffer positions{};
        };

        struct InputSlot {
            const SpectraPluginTransform* transform{};
            const SpectraPluginFloat3* bounds{};
        };

        struct InitialMeshInput {
            const SpectraPluginFloat3* positions{};
        };

        xayah::projects::cloth::SolverConfiguration configuration{};
        xayah::projects::cloth::SphereCollider collider{};
        std::optional<xayah::projects::cloth::Solver> solver{std::in_place, this->configuration, this->collider};
        std::array<OutputSlot, 2> output_slots{};
        InputSlot collider_input{};
        InitialMeshInput initial_mesh_input{};
        xayah::projects::cuda_interop::ImportedTimelineSemaphore timeline_semaphore{};
        std::uint64_t ready_timeline_value{};
        std::uint32_t next_output_slot_index{};
        ~Provider() {
            for (OutputSlot& output_slot : this->output_slots) {
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.positions);
            }
            xayah::projects::cuda_interop::destroy_imported_timeline_semaphore(this->timeline_semaphore);
        }

        void configure_output(const SpectraPluginPortConfiguration& configuration) {
            for (OutputSlot& output_slot : this->output_slots) {
                xayah::projects::cuda_interop::destroy_imported_buffer(output_slot.positions);
            }
            xayah::projects::cuda_interop::destroy_imported_timeline_semaphore(this->timeline_semaphore);
            xayah::projects::cuda_interop::select_matching_device(configuration.vulkan_device_uuid, configuration.vulkan_device_luid, configuration.vulkan_device_node_mask);
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index) {
                const SpectraPluginPortSlot& source = configuration.slots[slot_index];
                OutputSlot& destination             = this->output_slots[source.slot_index];
                for (std::uint64_t index = 0; index < source.buffer_count; ++index) {
                    const SpectraPluginBuffer& buffer = source.buffers[index];
                    if (buffer.attribute == SpectraPluginAttribute::Position) destination.positions = xayah::projects::cuda_interop::import_buffer(buffer.external_memory_handle, buffer.byte_size);
                }
            }
            this->timeline_semaphore     = xayah::projects::cuda_interop::import_timeline_semaphore(configuration.timeline_semaphore_handle);
            this->ready_timeline_value   = 0;
            this->next_output_slot_index = 0;
        }

        void configure_input(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index)
                for (std::uint64_t buffer_index = 0; buffer_index < configuration.slots[slot_index].buffer_count; ++buffer_index) {
                    const SpectraPluginBuffer& source = configuration.slots[slot_index].buffers[buffer_index];
                    if (source.attribute == SpectraPluginAttribute::Transform)
                        this->collider_input.transform = static_cast<const SpectraPluginTransform*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Bounds)
                        this->collider_input.bounds = static_cast<const SpectraPluginFloat3*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Position)
                        this->initial_mesh_input.positions = static_cast<const SpectraPluginFloat3*>(source.host_address);
                }
        }

        void set_input(const SpectraPluginInputFrame& frame) {
            if (frame.port_index == 0) {
                this->collider.center = {this->collider_input.transform->matrix[3], this->collider_input.transform->matrix[7], this->collider_input.transform->matrix[11]};
                this->collider.radius = std::max({(this->collider_input.bounds[1].x - this->collider_input.bounds[0].x) * 0.5f, (this->collider_input.bounds[1].y - this->collider_input.bounds[0].y) * 0.5f, (this->collider_input.bounds[1].z - this->collider_input.bounds[0].z) * 0.5f});
            } else {
                const SpectraPluginFloat3& origin     = this->initial_mesh_input.positions[0];
                const SpectraPluginFloat3& column_end = this->initial_mesh_input.positions[this->configuration.columns - 1];
                const SpectraPluginFloat3& row_end    = this->initial_mesh_input.positions[(this->configuration.rows - 1) * this->configuration.columns];
                this->configuration.origin            = {origin.x, origin.y, origin.z};
                this->configuration.width             = std::hypot(column_end.x - origin.x, column_end.z - origin.z);
                this->configuration.depth             = std::hypot(row_end.x - origin.x, row_end.z - origin.z);
            }
        }

        void apply_parameters(const SpectraPluginParameterValue* values, const std::uint64_t count) {
            if (count != parameters.size()) throw std::runtime_error("Cloth Provider parameter count mismatch");
            this->configuration.gravity = {
                static_cast<float>(values[0].floating[0]),
                static_cast<float>(values[0].floating[1]),
                static_cast<float>(values[0].floating[2]),
            };
            this->configuration.solver_iterations  = static_cast<std::uint32_t>(values[1].integer);
            this->configuration.stretch_compliance = static_cast<float>(values[2].floating[0]);
            this->configuration.bend_compliance    = static_cast<float>(values[3].floating[0]);
        }

        void reset() {
            this->solver.emplace(this->configuration, this->collider);
        }

        void advance_simulation(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
            xayah::projects::cuda_interop::synchronize_stream(this->solver->cuda_mesh().cuda_stream);
        }

        void publish(const SpectraPluginFrameSink& sink) {
            const xayah::projects::cloth::CudaMeshView mesh = this->solver->cuda_mesh();
            if (this->ready_timeline_value != 0) xayah::projects::cuda_interop::wait_timeline(mesh.cuda_stream, this->timeline_semaphore, this->ready_timeline_value + 1);
            OutputSlot& output_slot = this->output_slots[this->next_output_slot_index];
            xayah::projects::cuda_interop::pack_float3_buffer(mesh.cuda_stream, output_slot.positions.device_pointer, mesh.position_x, mesh.position_y, mesh.position_z, mesh.vertex_count);
            this->ready_timeline_value += this->ready_timeline_value == 0 ? 1 : 2;
            xayah::projects::cuda_interop::signal_timeline(mesh.cuda_stream, this->timeline_semaphore, this->ready_timeline_value);
            const SpectraPluginOutputCommit commit{
                this->next_output_slot_index,
                mesh.vertex_count,
                ports[2].secondary_capacity,
                this->ready_timeline_value,
                {},
                {},
                0,
            };
            sink.commit_output(sink.context, 2, &commit);
            this->next_output_slot_index = (this->next_output_slot_index + 1) % this->output_slots.size();

            const std::array debug{
                SpectraPluginDebugPrimitive{
                    SpectraPluginDebugPrimitiveKind::AxisAlignedBox,
                    SpectraPluginDebugDepthMode::Tested,
                    {
                        this->collider.center[0] - this->collider.radius,
                        this->collider.center[1] - this->collider.radius,
                        this->collider.center[2] - this->collider.radius,
                    },
                    {
                        this->collider.center[0] + this->collider.radius,
                        this->collider.center[1] + this->collider.radius,
                        this->collider.center[2] + this->collider.radius,
                    },
                    {0.9f, 0.65f, 0.2f},
                    1.0f,
                    1,
                },
                SpectraPluginDebugPrimitive{
                    SpectraPluginDebugPrimitiveKind::Point,
                    SpectraPluginDebugDepthMode::XRay,
                    {this->configuration.origin[0], this->configuration.origin[1], this->configuration.origin[2]},
                    {},
                    {0.18f, 0.72f, 0.98f},
                    0.06f,
                    2,
                },
                SpectraPluginDebugPrimitive{
                    SpectraPluginDebugPrimitiveKind::Point,
                    SpectraPluginDebugDepthMode::XRay,
                    {this->configuration.origin[0] + this->configuration.width, this->configuration.origin[1], this->configuration.origin[2]},
                    {},
                    {0.18f, 0.72f, 0.98f},
                    0.06f,
                    3,
                },
            };
            sink.write_debug_draw(sink.context, 3, debug.data(), debug.size());
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

    double read_telemetry(const void* instance, const std::uint64_t index) {
        const Provider& provider = *static_cast<const Provider*>(instance);
        if (index == 0) return static_cast<double>(provider.configuration.columns * provider.configuration.rows);
        return static_cast<double>((provider.configuration.columns - 1) * (provider.configuration.rows - 1) * 2);
    }

    void publish_frame(void* instance, std::uint64_t, const SpectraPluginFrameSink* sink) {
        static_cast<Provider*>(instance)->publish(*sink);
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
        &read_telemetry,
        &publish_frame,
    };
    return &api;
}
