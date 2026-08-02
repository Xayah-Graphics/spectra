#include <spectra/plugin_api.h>

import std;
import xayah.projects.sparkles;

namespace {
    template <std::size_t Size>
    constexpr SpectraPluginString text(const char (&value)[Size]) noexcept {
        return {value, Size - 1};
    }

    constexpr std::array ports{
        SpectraPluginPortDescriptor{text("particles"), text("Particles"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::ParticleSet, SpectraPluginMemoryDomain::Host, 1024, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Position)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Radius)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Color)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)), SpectraPluginMeshUpdateMode::TopologyChanging, {}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("automatic_relaunch"), text("Automatic relaunch"), {}, SpectraPluginParameterKind::Boolean, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Boolean, 1, {}}, {SpectraPluginParameterKind::Boolean, 0, {}}, {SpectraPluginParameterKind::Boolean, 1, {}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("gravity"), text("Gravity"), text("m/s²"), SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {3.65, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {20.0, 0.0, 0.0}}, nullptr, 0},
    };
    constexpr std::array telemetry{
        SpectraPluginTelemetryDescriptor{text("particle_count"), text("Particles"), {}},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.particles.sparkles"),
        text("Sparkles"),
        text("spectra.dynamic.particle-set"),
        1,
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
        telemetry.data(),
        telemetry.size(),
    };

    struct Plugin {
        struct Slot {
            SpectraPluginFloat3* positions{};
            float* radii{};
            SpectraPluginFloat3* colors{};
            float* temperatures{};
        };

        xayah::projects::sparkles::Config config{};
        std::optional<xayah::projects::sparkles::Solver> solver{std::in_place, this->config};
        std::vector<SpectraPluginFloat3> positions{};
        std::vector<float> radii{};
        std::vector<SpectraPluginFloat3> colors{};
        std::vector<float> temperatures{};
        std::uint64_t capacity{ports[0].capacity};
        std::array<Slot, 1> slots{};
        Plugin() {
            this->prepare_frame();
        }

        void reset() {
            this->solver.emplace(this->config);
            this->prepare_frame();
        }

        void iterate(const double step_seconds, const std::uint64_t count) {
            for (std::uint64_t step = 0; step < count; ++step) this->solver->step(static_cast<float>(step_seconds));
            this->prepare_frame();
        }

        void prepare_frame() {
            const std::span<const xayah::projects::sparkles::Particle> particles = this->solver->particles();
            this->positions.resize(particles.size());
            this->radii.resize(particles.size());
            this->colors.resize(particles.size());
            this->temperatures.resize(particles.size());
            for (std::size_t index = 0; index < particles.size(); ++index) {
                this->positions[index]    = {particles[index].position[0], particles[index].position[1], particles[index].position[2]};
                this->radii[index]        = particles[index].radius;
                this->colors[index]       = {particles[index].color[0], particles[index].color[1], particles[index].color[2]};
                this->temperatures[index] = 6500.0f;
            }
        }

        void configure(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot = 0; slot < configuration.slot_count; ++slot) {
                Slot& destination = this->slots[configuration.slots[slot].index];
                for (std::uint64_t buffer = 0; buffer < configuration.slots[slot].buffer_count; ++buffer) {
                    const SpectraPluginBuffer& source = configuration.slots[slot].buffers[buffer];
                    if (source.attribute == SpectraPluginAttribute::Position)
                        destination.positions = static_cast<SpectraPluginFloat3*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Radius)
                        destination.radii = static_cast<float*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Color)
                        destination.colors = static_cast<SpectraPluginFloat3*>(source.host_address);
                    else if (source.attribute == SpectraPluginAttribute::Temperature)
                        destination.temperatures = static_cast<float*>(source.host_address);
                }
            }
            this->capacity = configuration.slots[0].buffers[0].byte_size / sizeof(SpectraPluginFloat3);
        }

        void publish(const SpectraPluginFrameSink& sink) {
            if (this->positions.size() > this->capacity) {
                sink.request_capacity(sink.state, 0, std::bit_ceil(static_cast<std::uint64_t>(this->positions.size())), 0);
                return;
            }
            Slot& slot = this->slots[0];
            std::ranges::copy(this->positions, slot.positions);
            std::ranges::copy(this->radii, slot.radii);
            std::ranges::copy(this->colors, slot.colors);
            std::ranges::copy(this->temperatures, slot.temperatures);
            const SpectraPluginOutputCommit commit{0, this->positions.size(), 0, 0, {}, {}, 0};
            sink.commit_output(sink.state, 0, &commit);
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
        static_cast<Plugin*>(instance)->configure(*configuration);
    }
    void set_input_frame(void*, const SpectraPluginInputFrame*) {}

    void apply_parameters(void* instance, const SpectraPluginParameterValue* values, const std::uint64_t count) {
        Plugin& plugin = *static_cast<Plugin*>(instance);
        if (count != parameters.size()) throw std::runtime_error("Sparkles parameter count mismatch");
        plugin.config.automatic_relaunch = values[0].integer != 0;
        plugin.config.gravity            = static_cast<float>(values[1].floating[0]);
    }

    void reset(void* instance, const std::uint64_t seed) {
        Plugin& plugin     = *static_cast<Plugin*>(instance);
        plugin.config.seed = static_cast<std::uint32_t>(seed);
        plugin.reset();
    }
    void step(void* instance, const double step_seconds, const std::uint64_t count) {
        static_cast<Plugin*>(instance)->iterate(step_seconds, count);
    }
    double telemetry_value(const void* instance, std::uint64_t) {
        const Plugin& plugin = *static_cast<const Plugin*>(instance);
        return static_cast<double>(plugin.positions.size());
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
