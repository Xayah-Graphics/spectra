#include <spectra/plugin_api.h>

import std;
import xayah.projects.sparkles;

namespace {
    template <std::size_t Size>
    constexpr SpectraPluginString text(const char (&value)[Size]) noexcept {
        return {value, Size - 1};
    }

    constexpr std::array ports{
        SpectraPluginPortDescriptor{text("particles"), SpectraPluginPortDirection::Output, SpectraPluginResourceKind::ParticleSet, SpectraPluginMemoryDomain::Host, 1024, 0, (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Position)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Radius)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Color)) | (1ull << static_cast<std::uint32_t>(SpectraPluginAttribute::Temperature)), SpectraPluginMeshUpdateMode::TopologyChanging, {}},
    };
    constexpr std::array parameters{
        SpectraPluginParameterDescriptor{text("automatic_relaunch"), text("Automatic relaunch"), {}, SpectraPluginParameterKind::Boolean, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Boolean, 1, {}}, {SpectraPluginParameterKind::Boolean, 0, {}}, {SpectraPluginParameterKind::Boolean, 1, {}}, nullptr, 0},
        SpectraPluginParameterDescriptor{text("gravity"), text("Gravity"), text("m/s²"), SpectraPluginParameterKind::Float, SpectraPluginParameterApplication::ResetRequired, {SpectraPluginParameterKind::Float, 0, {3.65, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {0.0, 0.0, 0.0}}, {SpectraPluginParameterKind::Float, 0, {20.0, 0.0, 0.0}}, nullptr, 0},
    };
    constexpr SpectraPluginProviderDescriptor provider{
        text("xayah.particles.sparkles"),
        ports.data(),
        ports.size(),
        parameters.data(),
        parameters.size(),
    };

    struct Provider {
        struct OutputSlot {
            SpectraPluginFloat3* positions{};
            float* radii{};
            SpectraPluginFloat3* colors{};
            float* temperatures{};
        };

        xayah::projects::sparkles::SolverConfiguration configuration{};
        std::optional<xayah::projects::sparkles::Solver> solver{std::in_place, this->configuration};
        std::vector<SpectraPluginFloat3> positions{};
        std::vector<float> radii{};
        std::vector<SpectraPluginFloat3> colors{};
        std::vector<float> temperatures{};
        std::uint64_t capacity{ports[0].capacity};
        std::array<OutputSlot, 1> output_slots{};
        Provider() {
            this->prepare_frame();
        }

        void reset() {
            this->solver.emplace(this->configuration);
            this->prepare_frame();
        }

        void advance_simulation(const double step_seconds, const std::uint64_t count) {
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

        void configure_output(const SpectraPluginPortConfiguration& configuration) {
            for (std::uint64_t slot_index = 0; slot_index < configuration.slot_count; ++slot_index) {
                OutputSlot& destination = this->output_slots[configuration.slots[slot_index].slot_index];
                for (std::uint64_t buffer_index = 0; buffer_index < configuration.slots[slot_index].buffer_count; ++buffer_index) {
                    const SpectraPluginBuffer& source = configuration.slots[slot_index].buffers[buffer_index];
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
                sink.request_capacity(sink.context, 0, std::bit_ceil(static_cast<std::uint64_t>(this->positions.size())), 0);
                return;
            }
            OutputSlot& output_slot = this->output_slots[0];
            std::ranges::copy(this->positions, output_slot.positions);
            std::ranges::copy(this->radii, output_slot.radii);
            std::ranges::copy(this->colors, output_slot.colors);
            std::ranges::copy(this->temperatures, output_slot.temperatures);
            const SpectraPluginOutputCommit commit{0, this->positions.size(), 0, 0, {}, {}, 0};
            sink.commit_output(sink.context, 0, &commit);
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
        static_cast<Provider*>(instance)->configure_output(*configuration);
    }
    void set_input_frame(void*, const SpectraPluginInputFrame*) {}

    void apply_parameters(void* instance, const SpectraPluginParameterValue* values, const std::uint64_t count) {
        Provider& provider = *static_cast<Provider*>(instance);
        if (count != parameters.size()) throw std::runtime_error("Sparkles parameter count mismatch");
        provider.configuration.automatic_relaunch = values[0].integer != 0;
        provider.configuration.gravity            = static_cast<float>(values[1].floating[0]);
        provider.solver->set_configuration(provider.configuration);
    }

    void reset(void* instance, const std::uint64_t seed) {
        Provider& provider          = *static_cast<Provider*>(instance);
        provider.configuration.seed = static_cast<std::uint32_t>(seed);
        provider.reset();
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
