export module xayah.projects.bouncing_ball;
import std;

namespace xayah::projects::bouncing_ball {
    export struct SolverConfiguration {
        float radius{0.35f};
        std::array<float, 3> start_position{0.0f, 3.0f, 0.0f};
        std::array<float, 3> initial_velocity{0.0f, 0.0f, 0.0f};
        std::array<float, 3> gravity{0.0f, -9.8f, 0.0f};
        float floor_height{0.0f};
        float restitution{0.82f};
    };

    export struct Solver {
        explicit Solver(const SolverConfiguration& configuration = {});

        void reset();
        void step(float delta_seconds);
        [[nodiscard]] const std::array<float, 3>& position() const;
        [[nodiscard]] const std::array<float, 3>& velocity() const;

    private:
        SolverConfiguration configuration{};
        std::array<float, 3> current_position{};
        std::array<float, 3> current_velocity{};
    };
} // namespace xayah::projects::bouncing_ball
