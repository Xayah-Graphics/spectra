module sparkles;
import std;

namespace {
    void validate_float(const float value, const char* label) {
        if (!std::isfinite(value)) throw std::runtime_error(std::string{label} + " must be finite");
    }

    void validate_positive(const float value, const char* label) {
        validate_float(value, label);
        if (value <= 0.0f) throw std::runtime_error(std::string{label} + " must be positive");
    }

    void validate_config(const xayah::SparklesConfig& config) {
        for (const float value : config.origin) validate_float(value, "Sparkles origin");
        validate_positive(config.launch_speed, "Sparkles launch_speed");
        if (config.launch_speed_jitter < 0.0f || !std::isfinite(config.launch_speed_jitter)) throw std::runtime_error("Sparkles launch_speed_jitter must be finite and non-negative");
        if (config.lateral_launch_speed < 0.0f || !std::isfinite(config.lateral_launch_speed)) throw std::runtime_error("Sparkles lateral_launch_speed must be finite and non-negative");
        validate_positive(config.rocket_lifetime, "Sparkles rocket_lifetime");
        if (config.restart_delay < 0.0f || !std::isfinite(config.restart_delay)) throw std::runtime_error("Sparkles restart_delay must be finite and non-negative");
        validate_positive(config.gravity, "Sparkles gravity");
        validate_positive(config.max_step_seconds, "Sparkles max_step_seconds");
        if (config.explosion_particles == 0u) throw std::runtime_error("Sparkles explosion_particles must be positive");
        if (config.ring_particles == 0u) throw std::runtime_error("Sparkles ring_particles must be positive");
        if (config.glitter_particles == 0u) throw std::runtime_error("Sparkles glitter_particles must be positive");
        validate_positive(config.trail_particles_per_second, "Sparkles trail_particles_per_second");
    }

    std::array<float, 3> add(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
    }

    std::array<float, 3> scale(const std::array<float, 3>& value, const float scalar) {
        return {value[0] * scalar, value[1] * scalar, value[2] * scalar};
    }

    std::array<float, 3> color_from_hue(const float hue) {
        const float h      = std::fmod(std::fmod(hue, 1.0f) + 1.0f, 1.0f) * 6.0f;
        const int sector   = static_cast<int>(std::floor(h));
        const float factor = h - static_cast<float>(sector);
        const float q      = 1.0f - factor;
        if (sector == 0) return {1.0f, factor, 0.0f};
        if (sector == 1) return {q, 1.0f, 0.0f};
        if (sector == 2) return {0.0f, 1.0f, factor};
        if (sector == 3) return {0.0f, q, 1.0f};
        if (sector == 4) return {factor, 0.0f, 1.0f};
        return {1.0f, 0.0f, q};
    }

    std::array<float, 3> mix_color(const std::array<float, 3>& first, const std::array<float, 3>& second, const float amount) {
        return {
            std::lerp(first[0], second[0], amount),
            std::lerp(first[1], second[1], amount),
            std::lerp(first[2], second[2], amount),
        };
    }

} // namespace

namespace xayah {
    SparklesSolver::SparklesSolver(const SparklesConfig& config) : config{config} {
        validate_config(this->config);
        this->states.reserve(static_cast<std::size_t>(this->config.explosion_particles + this->config.ring_particles + this->config.glitter_particles + 512u));
        this->visible_particles.reserve(this->states.capacity() + 1u);
        this->reset();
    }

    void SparklesSolver::reset() {
        validate_config(this->config);
        this->random.seed(this->config.seed);
        this->states.clear();
        this->visible_particles.clear();
        this->cooldown          = 0.0f;
        this->trail_accumulator = 0.0f;
        this->start_rocket();
    }

    void SparklesSolver::step(const float delta_seconds) {
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0f) throw std::runtime_error("Sparkles delta_seconds must be finite and non-negative");
        if (delta_seconds == 0.0f) return;

        float remaining_seconds = delta_seconds;
        while (remaining_seconds > 0.0f) {
            const float step_seconds = std::min(remaining_seconds, this->config.max_step_seconds);
            remaining_seconds -= step_seconds;

            if (this->phase == Phase::launch) {
                this->rocket_age += step_seconds;
                this->rocket_velocity[1] -= this->config.gravity * step_seconds;
                this->rocket_position = add(this->rocket_position, scale(this->rocket_velocity, step_seconds));
                this->emit_trail(step_seconds);
                if (this->rocket_age >= this->config.rocket_lifetime || this->rocket_velocity[1] <= 0.18f) this->explode();
            } else if (this->states.empty()) {
                this->cooldown += step_seconds;
                if (this->config.automatic_relaunch && this->cooldown >= this->config.restart_delay) this->start_rocket();
            }

            this->update_particles(step_seconds);
        }
        this->rebuild_visible_particles();
    }

    std::span<const SparklesParticle> SparklesSolver::particles() const {
        return this->visible_particles;
    }

    void SparklesSolver::start_rocket() {
        this->phase           = Phase::launch;
        this->rocket_age      = 0.0f;
        this->cooldown        = 0.0f;
        this->rocket_position = this->config.origin;
        this->rocket_velocity = {
            this->random_range(-this->config.lateral_launch_speed, this->config.lateral_launch_speed),
            this->config.launch_speed + this->random_range(-this->config.launch_speed_jitter, this->config.launch_speed_jitter),
            this->random_range(-this->config.lateral_launch_speed, this->config.lateral_launch_speed),
        };
        this->rocket_color = mix_color(color_from_hue(this->random_range(0.0f, 1.0f)), {1.0f, 0.78f, 0.25f}, 0.38f);
        this->rebuild_visible_particles();
    }

    void SparklesSolver::emit_trail(const float delta_seconds) {
        this->trail_accumulator += this->config.trail_particles_per_second * delta_seconds;
        const auto spawn_count = static_cast<std::uint32_t>(std::floor(this->trail_accumulator));
        this->trail_accumulator -= static_cast<float>(spawn_count);

        for (std::uint32_t index = 0; index < spawn_count; ++index) {
            ParticleState state;
            state.kind          = ParticleKind::trail;
            state.position      = add(this->rocket_position, {this->random_range(-0.025f, 0.025f), this->random_range(-0.035f, 0.02f), this->random_range(-0.025f, 0.025f)});
            state.velocity      = {this->random_range(-0.42f, 0.42f), this->random_range(-1.15f, -0.25f), this->random_range(-0.42f, 0.42f)};
            state.color         = mix_color({1.0f, 0.62f, 0.16f}, {1.0f, 0.96f, 0.58f}, this->random_range(0.0f, 1.0f));
            state.radius        = this->random_range(0.018f, 0.034f);
            state.lifetime      = this->random_range(0.45f, 0.82f);
            state.drag          = 2.35f;
            state.gravity_scale = 0.32f;
            this->states.emplace_back(state);
        }
    }

    void SparklesSolver::explode() {
        this->phase    = Phase::fade;
        this->cooldown = 0.0f;

        const float hue                 = this->random_range(0.0f, 1.0f);
        const std::array main_color     = color_from_hue(hue);
        const std::array accent_color   = color_from_hue(hue + 0.12f);
        const std::array contrast_color = color_from_hue(hue + 0.52f);

        for (std::uint32_t index = 0; index < this->config.explosion_particles; ++index) {
            const std::array direction = this->random_sphere_direction();
            const float speed          = this->random_range(2.1f, 5.9f);
            ParticleState state;
            state.kind          = ParticleKind::burst;
            state.position      = this->rocket_position;
            state.velocity      = add(scale(direction, speed), scale(this->rocket_velocity, 0.16f));
            state.color         = mix_color(main_color, accent_color, this->random_range(0.0f, 1.0f));
            state.radius        = this->random_range(0.026f, 0.052f);
            state.lifetime      = this->random_range(2.2f, 3.6f);
            state.drag          = this->random_range(0.56f, 0.86f);
            state.gravity_scale = 0.82f;
            this->states.emplace_back(state);
        }

        for (std::uint32_t index = 0; index < this->config.explosion_particles / 4u; ++index) {
            const std::array direction = this->random_sphere_direction();
            ParticleState state;
            state.kind          = ParticleKind::core;
            state.position      = this->rocket_position;
            state.velocity      = add(scale(direction, this->random_range(0.45f, 2.15f)), scale(this->rocket_velocity, 0.08f));
            state.color         = mix_color({1.0f, 0.96f, 0.74f}, main_color, this->random_range(0.0f, 0.35f));
            state.radius        = this->random_range(0.036f, 0.074f);
            state.lifetime      = this->random_range(1.05f, 1.75f);
            state.drag          = this->random_range(1.05f, 1.55f);
            state.gravity_scale = 0.46f;
            this->states.emplace_back(state);
        }

        for (std::uint32_t index = 0; index < this->config.ring_particles; ++index) {
            const float angle          = static_cast<float>(index) / static_cast<float>(this->config.ring_particles) * std::numbers::pi_v<float> * 2.0f;
            const float vertical_noise = this->random_range(-0.16f, 0.16f);
            const std::array direction{std::cos(angle), vertical_noise, std::sin(angle)};
            ParticleState state;
            state.kind          = ParticleKind::ring;
            state.position      = this->rocket_position;
            state.velocity      = add(scale(direction, this->random_range(3.05f, 4.2f)), scale(this->rocket_velocity, 0.12f));
            state.color         = mix_color(contrast_color, {1.0f, 0.86f, 0.36f}, this->random_range(0.0f, 0.45f));
            state.radius        = this->random_range(0.024f, 0.044f);
            state.lifetime      = this->random_range(2.35f, 3.35f);
            state.drag          = this->random_range(0.42f, 0.68f);
            state.gravity_scale = 0.64f;
            this->states.emplace_back(state);
        }

        for (std::uint32_t index = 0; index < this->config.glitter_particles; ++index) {
            const std::array direction = this->random_sphere_direction();
            ParticleState state;
            state.kind          = ParticleKind::glitter;
            state.position      = this->rocket_position;
            state.velocity      = add(scale(direction, this->random_range(1.1f, 5.4f)), scale(this->rocket_velocity, 0.1f));
            state.color         = mix_color({1.0f, 0.84f, 0.28f}, {0.92f, 0.98f, 1.0f}, this->random_range(0.0f, 1.0f));
            state.radius        = this->random_range(0.014f, 0.028f);
            state.lifetime      = this->random_range(1.4f, 2.7f);
            state.drag          = this->random_range(0.7f, 1.25f);
            state.gravity_scale = 0.72f;
            this->states.emplace_back(state);
        }
    }

    void SparklesSolver::update_particles(const float delta_seconds) {
        for (ParticleState& state : this->states) {
            state.age += delta_seconds;
            state.velocity[1] -= this->config.gravity * state.gravity_scale * delta_seconds;
            const float damping = std::exp(-state.drag * delta_seconds);
            state.velocity      = scale(state.velocity, damping);
            state.position      = add(state.position, scale(state.velocity, delta_seconds));
        }
        std::erase_if(this->states, [](const ParticleState& state) { return state.age >= state.lifetime; });
    }

    void SparklesSolver::rebuild_visible_particles() {
        this->visible_particles.clear();
        if (this->phase == Phase::launch) {
            this->visible_particles.emplace_back(SparklesParticle{this->rocket_position, 0.07f, mix_color(this->rocket_color, {1.0f, 0.95f, 0.72f}, 0.45f)});
        }

        for (const ParticleState& state : this->states) {
            const float normalized_age = state.age / state.lifetime;
            float fade                 = std::clamp(1.0f - normalized_age, 0.0f, 1.0f);
            if (state.kind == ParticleKind::glitter) fade *= 0.42f + 0.58f * std::abs(std::sin(normalized_age * std::numbers::pi_v<float> * 18.0f));
            if (fade <= 0.0f) continue;
            const float radius = state.radius * std::lerp(0.18f, 1.0f, fade);
            this->visible_particles.emplace_back(SparklesParticle{
                state.position,
                radius,
                {
                    state.color[0] * fade,
                    state.color[1] * fade,
                    state.color[2] * fade,
                },
            });
        }
    }

    float SparklesSolver::random_range(const float minimum, const float maximum) {
        std::uniform_real_distribution<float> distribution{minimum, maximum};
        return distribution(this->random);
    }

    std::array<float, 3> SparklesSolver::random_sphere_direction() {
        const float z     = this->random_range(-1.0f, 1.0f);
        const float theta = this->random_range(0.0f, std::numbers::pi_v<float> * 2.0f);
        const float r     = std::sqrt(std::max(0.0f, 1.0f - z * z));
        return {r * std::cos(theta), z, r * std::sin(theta)};
    }
} // namespace xayah
