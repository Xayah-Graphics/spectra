export module camera;
import std;

namespace xayah {
    export struct CameraInput {
        float delta_seconds{0.0f};
        float mouse_delta_x{0.0f};
        float mouse_delta_y{0.0f};
        float mouse_wheel{0.0f};
        bool orbit{false};
        bool pan{false};
        bool fly{false};
        bool move_forward{false};
        bool move_backward{false};
        bool move_left{false};
        bool move_right{false};
        bool move_up{false};
        bool move_down{false};
    };

    export class Camera {
    public:
        void reset_home() {
            this->target   = {0.0f, 0.0f, 0.0f};
            this->yaw      = 0.7853982f;
            this->pitch    = -0.5235988f;
            this->distance = 8.0f;
        }

        void update(const CameraInput& input) {
            constexpr float rotation_speed = 0.005f;
            constexpr float pan_speed      = 0.0015f;
            constexpr float zoom_speed     = 0.12f;
            constexpr float fly_speed      = 5.0f;

            if (input.orbit || input.fly) {
                this->yaw += input.mouse_delta_x * rotation_speed;
                this->pitch = std::clamp(this->pitch - input.mouse_delta_y * rotation_speed, -1.553343f, 1.553343f);
            }

            const std::array<float, 3> forward = this->forward();
            const std::array<float, 3> right   = normalize(cross(std::array<float, 3>{0.0f, 1.0f, 0.0f}, forward));
            const std::array<float, 3> up      = normalize(cross(forward, right));

            if (input.pan) {
                const float scale = this->distance * pan_speed;
                this->target      = add(this->target, multiply(right, -input.mouse_delta_x * scale));
                this->target      = add(this->target, multiply(up, input.mouse_delta_y * scale));
            }

            if (input.mouse_wheel != 0.0f) this->distance = std::clamp(this->distance * std::exp(-input.mouse_wheel * zoom_speed), 0.25f, 200.0f);

            if (input.fly) {
                std::array<float, 3> direction{0.0f, 0.0f, 0.0f};
                if (input.move_forward) direction = add(direction, forward);
                if (input.move_backward) direction = subtract(direction, forward);
                if (input.move_right) direction = add(direction, right);
                if (input.move_left) direction = subtract(direction, right);
                if (input.move_up) direction = add(direction, std::array<float, 3>{0.0f, 1.0f, 0.0f});
                if (input.move_down) direction = subtract(direction, std::array<float, 3>{0.0f, 1.0f, 0.0f});
                if (length(direction) > 0.0f) this->target = add(this->target, multiply(normalize(direction), fly_speed * input.delta_seconds));
            }
        }

        [[nodiscard]] std::array<float, 16> view_projection(const float aspect) const {
            if (aspect <= 0.0f) throw std::runtime_error("Camera aspect ratio must be positive");
            return multiply(projection(aspect), view());
        }

        [[nodiscard]] std::array<float, 3> position() const {
            return subtract(this->target, multiply(this->forward(), this->distance));
        }

        [[nodiscard]] std::array<float, 3> right() const {
            return normalize(cross(std::array<float, 3>{0.0f, 1.0f, 0.0f}, this->forward()));
        }

        [[nodiscard]] std::array<float, 3> up() const {
            return normalize(cross(this->forward(), this->right()));
        }

    private:
        std::array<float, 3> target{0.0f, 0.0f, 0.0f};
        float yaw{0.7853982f};
        float pitch{-0.5235988f};
        float distance{8.0f};
        float fov_y{1.0471976f};
        float near_z{0.05f};
        float far_z{500.0f};

        [[nodiscard]] std::array<float, 3> forward() const {
            return normalize(std::array<float, 3>{
                std::cos(this->pitch) * std::sin(this->yaw),
                std::sin(this->pitch),
                std::cos(this->pitch) * std::cos(this->yaw),
            });
        }

        [[nodiscard]] std::array<float, 16> view() const {
            const std::array<float, 3> forward = this->forward();
            const std::array<float, 3> eye     = subtract(this->target, multiply(forward, this->distance));
            const std::array<float, 3> right   = normalize(cross(std::array<float, 3>{0.0f, 1.0f, 0.0f}, forward));
            const std::array<float, 3> up      = cross(forward, right);

            return {
                right[0],
                up[0],
                -forward[0],
                0.0f,
                right[1],
                up[1],
                -forward[1],
                0.0f,
                right[2],
                up[2],
                -forward[2],
                0.0f,
                -dot(right, eye),
                -dot(up, eye),
                dot(forward, eye),
                1.0f,
            };
        }

        [[nodiscard]] std::array<float, 16> projection(const float aspect) const {
            const float focal = 1.0f / std::tan(this->fov_y * 0.5f);
            return {
                focal / aspect,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                -focal,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                this->far_z / (this->near_z - this->far_z),
                -1.0f,
                0.0f,
                0.0f,
                (this->far_z * this->near_z) / (this->near_z - this->far_z),
                0.0f,
            };
        }

        [[nodiscard]] static std::array<float, 3> add(const std::array<float, 3>& left, const std::array<float, 3>& right) {
            return {left[0] + right[0], left[1] + right[1], left[2] + right[2]};
        }

        [[nodiscard]] static std::array<float, 3> subtract(const std::array<float, 3>& left, const std::array<float, 3>& right) {
            return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
        }

        [[nodiscard]] static std::array<float, 3> multiply(const std::array<float, 3>& vector, const float value) {
            return {vector[0] * value, vector[1] * value, vector[2] * value};
        }

        [[nodiscard]] static float dot(const std::array<float, 3>& left, const std::array<float, 3>& right) {
            return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
        }

        [[nodiscard]] static std::array<float, 3> cross(const std::array<float, 3>& left, const std::array<float, 3>& right) {
            return {
                left[1] * right[2] - left[2] * right[1],
                left[2] * right[0] - left[0] * right[2],
                left[0] * right[1] - left[1] * right[0],
            };
        }

        [[nodiscard]] static float length(const std::array<float, 3>& vector) {
            return std::sqrt(dot(vector, vector));
        }

        [[nodiscard]] static std::array<float, 3> normalize(const std::array<float, 3>& vector) {
            const float vector_length = length(vector);
            if (vector_length <= 0.0f) throw std::runtime_error("Cannot normalize zero-length camera vector");
            return multiply(vector, 1.0f / vector_length);
        }

        [[nodiscard]] static std::array<float, 16> multiply(const std::array<float, 16>& left, const std::array<float, 16>& right) {
            std::array<float, 16> result{};
            for (std::uint32_t column = 0; column < 4; ++column) {
                for (std::uint32_t row = 0; row < 4; ++row) {
                    result[column * 4 + row] = left[0 * 4 + row] * right[column * 4 + 0] + left[1 * 4 + row] * right[column * 4 + 1] + left[2 * 4 + row] * right[column * 4 + 2] + left[3 * 4 + row] * right[column * 4 + 3];
                }
            }
            return result;
        }
    };
} // namespace xayah
