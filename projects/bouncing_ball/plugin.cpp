#include <spectra/plugin_api.h>

import std;
import xayah.projects.bouncing_ball;

namespace {
    template <std::size_t Size>
    constexpr SpectraPluginString text(const char (&value)[Size]) noexcept {
        return {value, Size - 1};
    }

    constexpr std::array ports{
        SpectraPluginPortDescriptor{text("collider"), text("Floor Collider"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::InstanceTransform, SpectraPluginMemoryDomain::Host, 1, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Transform)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Bounds)), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("initial_ball"), text("Initial Ball"), SpectraPluginPortDirection::Input, SpectraPluginResourceKind::InstanceTransform, SpectraPluginMemoryDomain::Host, 1, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Transform)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Bounds)), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("ball_transform"), text("Ball Transform"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::InstanceTransform, SpectraPluginMemoryDomain::Host, 1, 0, 1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Transform), SpectraPluginMeshUpdateMode::Deformable, {}},
        SpectraPluginPortDescriptor{text("debug"), text("Collision Debug"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::DebugDraw, SpectraPluginMemoryDomain::Host, 4, 0, 0, SpectraPluginMeshUpdateMode::Deformable, {}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("restitution"), text("Restitution"), {}, SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {0.82, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {1.0, 0.0, 0.0}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("gravity"), text("Gravity"), text("m/s²"), SpectraPluginParameterKind::Float3, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float3, 0, {0.0, -9.8, 0.0}}, {SpectraPluginParameterKind::Float3, 0, {-100.0, -100.0, -100.0}}, {SpectraPluginParameterKind::Float3, 0, {100.0, 100.0, 100.0}}, nullptr, 0},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.rigid.bouncing-ball"),
        text("Bouncing Ball"),
        text("spectra.dynamic.rigid-body"),
        1,
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
        nullptr,
        0,
    };

    struct Provider {
        struct OutputSlot {
            SpectraPluginTransform* transform{};
        };

        struct InputSlot {
            const SpectraPluginTransform* transform{};
            const SpectraPluginFloat3* bounds{};
        };

        xayah::projects::bouncing_ball::SolverConfiguration configuration{};
        std::optional<xayah::projects::bouncing_ball::Solver> solver{std::in_place, this->configuration};
        std::array<OutputSlot, 1> output_slots{};
        InputSlot collider_input{};
        InputSlot initial_ball_input{};
        void reset() {
            this->solver.emplace(this->configuration);
        }

        void advance_simulation(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
        }

        void configure_output(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index)
                for (std::uint64_t buffer_index = 0; buffer_index < configuration.slots[slot_index].buffer_count; ++buffer_index)
                    if (configuration.slots[slot_index].buffers[buffer_index].attribute == SpectraPluginAttribute::Transform) this->output_slots[configuration.slots[slot_index].slot_index].transform = static_cast<SpectraPluginTransform*>(configuration.slots[slot_index].buffers[buffer_index].host_address);
        }

        void configure_input(const SpectraPluginPortConfiguration& configuration) {
            InputSlot& destination = configuration.port_index == 0 ? this->collider_input : this->initial_ball_input;
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index)
                for (std::uint64_t buffer_index = 0; buffer_index < configuration.slots[slot_index].buffer_count; ++buffer_index) {
                    const SpectraPluginBuffer& source = configuration.slots[slot_index].buffers[buffer_index];
                    if (source.attribute == SpectraPluginAttribute::Transform)
                        destination.transform = static_cast<const SpectraPluginTransform*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Bounds)
                        destination.bounds = static_cast<const SpectraPluginFloat3*>(source.host_address);
                }
        }

        void set_input(const SpectraPluginInputFrame& frame) {
            if (frame.port_index == 0)
                this->configuration.floor_height = this->collider_input.transform->matrix[7] + this->collider_input.bounds[1].y;
            else {
                this->configuration.start_position = {this->initial_ball_input.transform->matrix[3], this->initial_ball_input.transform->matrix[7], this->initial_ball_input.transform->matrix[11]};
                this->configuration.radius         = std::max({(this->initial_ball_input.bounds[1].x - this->initial_ball_input.bounds[0].x) * 0.5f, (this->initial_ball_input.bounds[1].y - this->initial_ball_input.bounds[0].y) * 0.5f, (this->initial_ball_input.bounds[1].z - this->initial_ball_input.bounds[0].z) * 0.5f});
            }
        }

        void publish(const SpectraPluginFrameSink& sink) {
            *this->output_slots[0].transform = SpectraPluginTransform{
                1.0f,
                0.0f,
                0.0f,
                this->solver->position()[0],
                0.0f,
                1.0f,
                0.0f,
                this->solver->position()[1],
                0.0f,
                0.0f,
                1.0f,
                this->solver->position()[2],
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            const SpectraPluginOutputCommit commit{0, 1, 0, 0, {}, {}, 0};
            sink.commit_output(sink.context, 2, &commit);
            const SpectraPluginFloat3 position{this->solver->position()[0], this->solver->position()[1], this->solver->position()[2]};
            const SpectraPluginFloat3 velocity_end{
                position.x + this->solver->velocity()[0] * 0.15f,
                position.y + this->solver->velocity()[1] * 0.15f,
                position.z + this->solver->velocity()[2] * 0.15f,
            };
            const SpectraPluginFloat3 center{position.x, this->configuration.floor_height, position.z};
            const std::array debug{
                SpectraPluginDebugPrimitive{SpectraPluginDebugPrimitiveKind::Arrow, SpectraPluginDebugDepthMode::XRay, position, velocity_end, {0.18f, 0.72f, 0.98f}, 0.025f, 1},
                SpectraPluginDebugPrimitive{SpectraPluginDebugPrimitiveKind::Contact, SpectraPluginDebugDepthMode::XRay, center, {center.x, center.y + 0.55f, center.z}, {0.95f, 0.72f, 0.22f}, 0.035f, 1},
            };
            const bool contact = std::abs(position.y - (this->configuration.floor_height + this->configuration.radius)) <= 0.0001f;
            sink.write_debug_draw(sink.context, 3, debug.data(), contact ? debug.size() : 1);
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
        Provider& provider = *static_cast<Provider*>(instance);
        if (count != parameters.size()) throw std::runtime_error("Bouncing Ball parameter count mismatch");
        provider.configuration.restitution = static_cast<float>(values[0].floating[0]);
        provider.configuration.gravity     = {static_cast<float>(values[1].floating[0]), static_cast<float>(values[1].floating[1]), static_cast<float>(values[1].floating[2])};
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
        nullptr,
        &publish_frame,
    };
    return &api;
}
