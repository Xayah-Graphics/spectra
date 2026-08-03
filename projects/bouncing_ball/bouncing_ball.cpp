module xayah.projects.bouncing_ball;
import std;

namespace xayah::projects::bouncing_ball {
    namespace {
        void validate_config(const SolverConfiguration& configuration) {
            if (configuration.radius <= 0.0f) throw std::runtime_error("BouncingBall radius must be positive");
            if (configuration.restitution < 0.0f || configuration.restitution > 1.0f) throw std::runtime_error("BouncingBall restitution must be in [0, 1]");
            if (configuration.start_position[1] < configuration.floor_height + configuration.radius) throw std::runtime_error("BouncingBall start_position penetrates the floor");
        }
    } // namespace

    Solver::Solver(const SolverConfiguration& configuration) : configuration{configuration} {
        validate_config(this->configuration);
        this->reset();
    }

    void Solver::reset() {
        this->current_position = this->configuration.start_position;
        this->current_velocity = this->configuration.initial_velocity;
    }

    void Solver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("BouncingBall delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            this->current_velocity[axis] += this->configuration.gravity[axis] * delta_seconds;
            this->current_position[axis] += this->current_velocity[axis] * delta_seconds;
        }

        const float minimum_center_y = this->configuration.floor_height + this->configuration.radius;
        if (this->current_position[1] < minimum_center_y) {
            this->current_position[1] = minimum_center_y;
            if (this->current_velocity[1] < 0.0f) this->current_velocity[1] = -this->current_velocity[1] * this->configuration.restitution;
        }
    }

    const std::array<float, 3>& Solver::position() const {
        return this->current_position;
    }

    const std::array<float, 3>& Solver::velocity() const {
        return this->current_velocity;
    }

} // namespace xayah::projects::bouncing_ball
