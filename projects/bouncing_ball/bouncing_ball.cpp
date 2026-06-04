module xayah.projects.bouncing_ball;
import std;

namespace {
    void validate_config(const xayah::projects::bouncing_ball::BouncingBallConfig& config) {
        if (config.radius <= 0.0f) throw std::runtime_error("BouncingBall radius must be positive");
        if (config.restitution < 0.0f || config.restitution > 1.0f) throw std::runtime_error("BouncingBall restitution must be in [0, 1]");
        if (config.latitude_segments < 3u) throw std::runtime_error("BouncingBall latitude_segments must be at least 3");
        if (config.longitude_segments < 3u) throw std::runtime_error("BouncingBall longitude_segments must be at least 3");
        if (config.start_position[1] < config.floor_y + config.radius) throw std::runtime_error("BouncingBall start_position penetrates the floor");
    }

    [[nodiscard]] xayah::projects::bouncing_ball::BouncingBallVertex sphere_vertex(const float radius, const float theta, const float phi) {
        const float sin_phi = std::sin(phi);
        const std::array normal{std::cos(theta) * sin_phi, std::cos(phi), std::sin(theta) * sin_phi};
        return xayah::projects::bouncing_ball::BouncingBallVertex{
            {normal[0] * radius, normal[1] * radius, normal[2] * radius},
            normal,
        };
    }
} // namespace

namespace xayah::projects::bouncing_ball {
    BouncingBallSolver::BouncingBallSolver(const BouncingBallConfig& config) : config{config} {
        validate_config(this->config);
        const std::uint32_t latitude_count  = this->config.latitude_segments + 1u;
        const std::uint32_t longitude_count = this->config.longitude_segments + 1u;
        if (static_cast<std::uint64_t>(latitude_count) * static_cast<std::uint64_t>(longitude_count) > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("BouncingBall sphere mesh is too large");

        this->vertices.reserve(static_cast<std::size_t>(latitude_count) * static_cast<std::size_t>(longitude_count));
        for (std::uint32_t latitude = 0; latitude <= this->config.latitude_segments; ++latitude) {
            const float v   = static_cast<float>(latitude) / static_cast<float>(this->config.latitude_segments);
            const float phi = v * std::numbers::pi_v<float>;
            for (std::uint32_t longitude = 0; longitude <= this->config.longitude_segments; ++longitude) {
                const float u     = static_cast<float>(longitude) / static_cast<float>(this->config.longitude_segments);
                const float theta = u * std::numbers::pi_v<float> * 2.0f;
                this->vertices.emplace_back(sphere_vertex(this->config.radius, theta, phi));
            }
        }

        this->indices.reserve(static_cast<std::size_t>(this->config.latitude_segments) * static_cast<std::size_t>(this->config.longitude_segments) * 6u);
        for (std::uint32_t latitude = 0; latitude < this->config.latitude_segments; ++latitude) {
            for (std::uint32_t longitude = 0; longitude < this->config.longitude_segments; ++longitude) {
                const std::uint32_t current = latitude * longitude_count + longitude;
                const std::uint32_t next    = current + longitude_count;
                if (latitude != 0u) {
                    this->indices.emplace_back(current);
                    this->indices.emplace_back(next);
                    this->indices.emplace_back(current + 1u);
                }
                if (latitude + 1u != this->config.latitude_segments) {
                    this->indices.emplace_back(current + 1u);
                    this->indices.emplace_back(next);
                    this->indices.emplace_back(next + 1u);
                }
            }
        }

        this->reset();
    }

    void BouncingBallSolver::reset() {
        this->position = this->config.start_position;
        this->velocity = this->config.initial_velocity;
    }

    void BouncingBallSolver::step(const float delta_seconds) {
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

    const std::array<float, 3>& BouncingBallSolver::current_position() const {
        return this->position;
    }

    const std::vector<BouncingBallVertex>& BouncingBallSolver::mesh_vertices() const {
        return this->vertices;
    }

    const std::vector<std::uint32_t>& BouncingBallSolver::mesh_indices() const {
        return this->indices;
    }
} // namespace xayah::projects::bouncing_ball
