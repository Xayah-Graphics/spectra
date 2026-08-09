export module xayah.projects.sparkles;
import std;

namespace xayah::projects::sparkles {
    export struct Particle {
        std::array<float, 3> position{0.0f, 0.0f, 0.0f};
        float radius{0.02f};
        std::array<float, 3> color{1.0f, 0.82f, 0.30f};
    };

    export struct SolverConfiguration {
        std::uint32_t seed{20260514u};
        std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
        float launch_speed{5.8f};
        float launch_speed_jitter{0.55f};
        float lateral_launch_speed{0.36f};
        float rocket_lifetime{1.35f};
        float restart_delay{1.05f};
        float gravity{3.65f};
        float maximum_step_seconds{1.0f / 120.0f};
        std::uint32_t explosion_particles{520u};
        std::uint32_t ring_particles{180u};
        std::uint32_t glitter_particles{150u};
        float trail_particles_per_second{135.0f};
        bool automatic_relaunch{true};
    };

    export struct Solver {
        explicit Solver(const SolverConfiguration& configuration = {});

        void set_configuration(const SolverConfiguration& configuration) noexcept;
        void reset();
        void step(float delta_seconds);
        [[nodiscard]] std::span<const Particle> particles() const;

    private:
        enum class Phase : std::uint32_t {
            Launch = 0,
            Fade   = 1,
        };

        enum class ParticleKind : std::uint32_t {
            Trail   = 0,
            Burst   = 1,
            Core    = 2,
            Ring    = 3,
            Glitter = 4,
        };

        struct ParticleState {
            std::array<float, 3> position{};
            std::array<float, 3> velocity{};
            std::array<float, 3> color{};
            float radius{0.02f};
            float age{0.0f};
            float lifetime{1.0f};
            float drag{0.0f};
            float gravity_scale{1.0f};
            ParticleKind kind{ParticleKind::Burst};
        };

        SolverConfiguration configuration{};
        std::mt19937 random_engine{};
        Phase phase{Phase::Launch};
        std::array<float, 3> rocket_position{};
        std::array<float, 3> rocket_velocity{};
        std::array<float, 3> rocket_color{1.0f, 0.88f, 0.36f};
        float rocket_age{0.0f};
        float relaunch_wait_seconds{0.0f};
        float trail_accumulator{0.0f};
        std::vector<ParticleState> particle_states{};
        std::vector<Particle> render_particles{};

        void start_rocket();
        void emit_trail(float delta_seconds);
        void explode();
        void update_particles(float delta_seconds);
        void rebuild_render_particles();
        [[nodiscard]] float random_range(float minimum, float maximum);
        [[nodiscard]] std::array<float, 3> random_sphere_direction();
    };
} // namespace xayah::projects::sparkles
