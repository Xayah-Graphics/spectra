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

    struct Plugin {
        struct Slot {
            SpectraPluginTransform* transform{};
        };

        struct InputSlot {
            const SpectraPluginTransform* transform{};
            const SpectraPluginFloat3* bounds{};
        };

        xayah::projects::bouncing_ball::Config config{};
        std::optional<xayah::projects::bouncing_ball::Solver> solver{std::in_place, this->config};
        std::array<Slot, 1> slots{};
        InputSlot collider_input{};
        InputSlot initial_ball_input{};
        void reset() {
            this->solver.emplace(this->config);
        }

        void iterate(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
        }

        void configure_output(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot = 0; slot < configuration.slot_count; ++slot)
                for (std::uint64_t buffer = 0; buffer < configuration.slots[slot].buffer_count; ++buffer)
                    if (configuration.slots[slot].buffers[buffer].attribute == SpectraPluginAttribute::Transform) this->slots[configuration.slots[slot].index].transform = static_cast<SpectraPluginTransform*>(configuration.slots[slot].buffers[buffer].host_address);
        }

        void configure_input(const SpectraPluginPortConfiguration& configuration) {
            InputSlot& destination = configuration.port == 0 ? this->collider_input : this->initial_ball_input;
            for (std::uint64_t slot = 0; slot < configuration.slot_count; ++slot)
                for (std::uint64_t buffer = 0; buffer < configuration.slots[slot].buffer_count; ++buffer) {
                    const SpectraPluginBuffer& source = configuration.slots[slot].buffers[buffer];
                    if (source.attribute == SpectraPluginAttribute::Transform)
                        destination.transform = static_cast<const SpectraPluginTransform*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Bounds)
                        destination.bounds = static_cast<const SpectraPluginFloat3*>(source.host_address);
                }
        }

        void set_input(const SpectraPluginInputFrame& frame) {
            if (frame.port == 0)
                this->config.floor_y = this->collider_input.transform->matrix[7] + this->collider_input.bounds[1].y;
            else {
                this->config.start_position = {this->initial_ball_input.transform->matrix[3], this->initial_ball_input.transform->matrix[7], this->initial_ball_input.transform->matrix[11]};
                this->config.radius         = std::max({(this->initial_ball_input.bounds[1].x - this->initial_ball_input.bounds[0].x) * 0.5f, (this->initial_ball_input.bounds[1].y - this->initial_ball_input.bounds[0].y) * 0.5f, (this->initial_ball_input.bounds[1].z - this->initial_ball_input.bounds[0].z) * 0.5f});
            }
        }

        void publish(const SpectraPluginFrameSink& sink) {
            *this->slots[0].transform = SpectraPluginTransform{
                1.0f,
                0.0f,
                0.0f,
                this->solver->current_position()[0],
                0.0f,
                1.0f,
                0.0f,
                this->solver->current_position()[1],
                0.0f,
                0.0f,
                1.0f,
                this->solver->current_position()[2],
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            const SpectraPluginOutputCommit commit{0, 1, 0, 0, {}, {}, 0};
            sink.commit_output(sink.state, 2, &commit);
            const SpectraPluginFloat3 position{this->solver->current_position()[0], this->solver->current_position()[1], this->solver->current_position()[2]};
            const SpectraPluginFloat3 velocity_end{
                position.x + this->solver->current_velocity()[0] * 0.15f,
                position.y + this->solver->current_velocity()[1] * 0.15f,
                position.z + this->solver->current_velocity()[2] * 0.15f,
            };
            const SpectraPluginFloat3 center{position.x, this->config.floor_y, position.z};
            const std::array debug{
                SpectraPluginDebugPrimitive{SpectraPluginDebugPrimitiveKind::Arrow, SpectraPluginDebugDepthMode::XRay, position, velocity_end, {0.18f, 0.72f, 0.98f}, 0.025f, 1},
                SpectraPluginDebugPrimitive{SpectraPluginDebugPrimitiveKind::Contact, SpectraPluginDebugDepthMode::XRay, center, {center.x, center.y + 0.55f, center.z}, {0.95f, 0.72f, 0.22f}, 0.035f, 1},
            };
            const bool contact = std::abs(position.y - (this->config.floor_y + this->config.radius)) <= 0.0001f;
            sink.write_debug_draw(sink.state, 3, debug.data(), contact ? debug.size() : 1);
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
            static_cast<Plugin*>(instance)->configure_output(*configuration);
    }
    void set_input_frame(void* instance, const SpectraPluginInputFrame* frame) {
        static_cast<Plugin*>(instance)->set_input(*frame);
    }

    void apply_parameters(void* instance, const SpectraPluginParameterValue* values, const std::uint64_t count) {
        Plugin& plugin = *static_cast<Plugin*>(instance);
        if (count != parameters.size()) throw std::runtime_error("Bouncing Ball parameter count mismatch");
        plugin.config.restitution = static_cast<float>(values[0].floating[0]);
        plugin.config.gravity     = {static_cast<float>(values[1].floating[0]), static_cast<float>(values[1].floating[1]), static_cast<float>(values[1].floating[2])};
    }

    void reset(void* instance, std::uint64_t) {
        static_cast<Plugin*>(instance)->reset();
    }
    void step(void* instance, const double step_seconds, const std::uint64_t count) {
        static_cast<Plugin*>(instance)->iterate(step_seconds, count);
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
        nullptr,
        &publish_frame,
    };
    return &api;
}
