module xayah.projects.bouncing_ball;
import std;

namespace xayah::projects::bouncing_ball {
namespace {
    void validate_config(const Config& config) {
        if (config.radius <= 0.0f) throw std::runtime_error("BouncingBall radius must be positive");
        if (config.restitution < 0.0f || config.restitution > 1.0f) throw std::runtime_error("BouncingBall restitution must be in [0, 1]");
        if (config.start_position[1] < config.floor_y + config.radius) throw std::runtime_error("BouncingBall start_position penetrates the floor");
    }
} // namespace

    Solver::Solver(const Config& config) : config{config} {
        validate_config(this->config);
        this->reset();
    }

    void Solver::reset() {
        this->position = this->config.start_position;
        this->velocity = this->config.initial_velocity;
    }

    void Solver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("BouncingBall delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            this->velocity[axis] += this->config.gravity[axis] * delta_seconds;
            this->position[axis] += this->velocity[axis] * delta_seconds;
        }

        const float minimum_center_y = this->config.floor_y + this->config.radius;
        if (this->position[1] < minimum_center_y) {
            this->position[1] = minimum_center_y;
            if (this->velocity[1] < 0.0f) this->velocity[1] = -this->velocity[1] * this->config.restitution;
        }
    }

    const std::array<float, 3>& Solver::current_position() const {
        return this->position;
    }

    const std::array<float, 3>& Solver::current_velocity() const {
        return this->velocity;
    }

} // namespace xayah::projects::bouncing_ball
