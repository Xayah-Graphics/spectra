export module camera;
import std;

namespace xayah {
    export enum class CameraMode : std::uint32_t {
        examine = 0,
        fly     = 1,
        walk    = 2,
    };

    export enum class CameraAction : std::uint32_t {
        none        = 0,
        orbit       = 1,
        dolly       = 2,
        pan         = 3,
        look_around = 4,
    };

    export enum class CameraProjection : std::uint32_t {
        perspective  = 0,
        orthographic = 1,
    };

    export struct CameraInputs {
        bool left_mouse{false};
        bool middle_mouse{false};
        bool right_mouse{false};
        bool shift{false};
        bool ctrl{false};
        bool alt{false};
    };

    export struct CameraState {
        std::array<float, 3> eye{10.0f, 10.0f, 10.0f};
        std::array<float, 3> center{0.0f, 0.0f, 0.0f};
        std::array<float, 3> up{0.0f, 1.0f, 0.0f};
        float fov_degrees{60.0f};
        std::array<float, 2> near_far{0.001f, 100000.0f};
        std::array<float, 2> orthographic_magnitudes{5.0f, 5.0f};
        CameraProjection projection{CameraProjection::perspective};

        [[nodiscard]] bool operator==(const CameraState&) const = default;
    };

    export class Camera {
    public:
        Camera() {
            this->update_view_matrix();
        }

        void reset_home() {
            CameraState camera = this->current;
            camera.eye         = {10.0f, 10.0f, 10.0f};
            camera.center      = {0.0f, 0.0f, 0.0f};
            camera.up          = {0.0f, 1.0f, 0.0f};
            this->set_camera(camera, true);
        }

        void update_animation(const float delta_seconds) {
            if (delta_seconds < 0.0f) throw std::runtime_error("Camera animation delta must be non-negative");
            if (!this->animating) return;
            if (!this->snapshot) throw std::runtime_error("Camera animation snapshot is missing");

            this->animation_elapsed += delta_seconds;
            float t = std::min(this->animation_elapsed / this->animation_duration_seconds, 1.0f);
            t       = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            if (t >= 1.0f) {
                this->current   = this->goal;
                this->animating = false;
                this->snapshot.reset();
                this->update_view_matrix();
                return;
            }

            this->current.eye                     = bezier(t, this->bezier_points[0], this->bezier_points[1], this->bezier_points[2]);
            this->current.center                  = mix(this->snapshot->center, this->goal.center, t);
            this->current.up                      = normalize(mix(this->snapshot->up, this->goal.up, t));
            this->current.fov_degrees             = mix(this->snapshot->fov_degrees, this->goal.fov_degrees, t);
            this->current.near_far                = mix(this->snapshot->near_far, this->goal.near_far, t);
            this->current.orthographic_magnitudes = mix(this->snapshot->orthographic_magnitudes, this->goal.orthographic_magnitudes, t);
            this->current.projection              = this->goal.projection;
            this->update_view_matrix();
        }

        void set_window_size(const std::array<std::uint32_t, 2>& size) {
            if (size[0] == 0 || size[1] == 0) throw std::runtime_error("Camera window size must be positive");
            this->window_size = size;
            this->adjust_orthographic_aspect();
        }

        [[nodiscard]] const CameraState& state() const {
            return this->current;
        }

        void set_camera(CameraState camera, const bool instant) {
            camera.up = normalize(camera.up);
            this->validate(camera);
            if (instant || this->animation_duration_seconds == 0.0f || camera.projection != this->current.projection) {
                this->current   = camera;
                this->animating = false;
                this->snapshot.reset();
                this->update_view_matrix();
                return;
            }
            if (camera == this->current) return;
            this->goal              = camera;
            this->snapshot          = this->current;
            this->animation_elapsed = 0.0f;
            this->animating         = true;
            this->find_bezier_points();
        }

        void set_lookat(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up, const bool instant) {
            CameraState camera = this->current;
            camera.eye         = eye;
            camera.center      = center;
            camera.up          = up;
            this->set_camera(camera, instant);
        }

        void fit_bounds(const std::array<float, 3>& minimum, const std::array<float, 3>& maximum, const bool instant, const bool tight, const float aspect) {
            if (aspect <= 0.0f) throw std::runtime_error("Camera fit aspect ratio must be positive");
            for (std::uint32_t axis = 0; axis < 3; ++axis) {
                if (maximum[axis] < minimum[axis]) throw std::runtime_error("Camera fit bounds are invalid");
            }

            const std::array<float, 3> half_size = multiply(subtract(maximum, minimum), 0.5f);
            const std::array<float, 3> box_center = multiply(add(minimum, maximum), 0.5f);
            if (length(half_size) <= epsilon) throw std::runtime_error("Camera fit bounds must have non-zero extent");
            const float y_fov = std::tan(radians(this->current.fov_degrees * 0.5f));
            const float x_fov = y_fov * aspect;
            float distance   = 0.0f;

            if (tight) {
                const std::array<float, 16> fit_view = lookat_matrix(this->current.eye, box_center, effective_up(this->current.eye, box_center, this->current.up));
                for (std::uint32_t corner = 0; corner < 8; ++corner) {
                    std::array<float, 3> point{
                        (corner & 1u) != 0u ? half_size[0] : -half_size[0],
                        (corner & 2u) != 0u ? half_size[1] : -half_size[1],
                        (corner & 4u) != 0u ? half_size[2] : -half_size[2],
                    };
                    point = transform_vector(fit_view, point);
                    if (point[2] < 0.0f) {
                        distance = std::max(std::abs(point[1]) / y_fov + std::abs(point[2]), distance);
                        distance = std::max(std::abs(point[0]) / x_fov + std::abs(point[2]), distance);
                    }
                }
            } else {
                const float radius = length(half_size);
                distance           = std::max(radius / x_fov, radius / y_fov);
            }

            if (distance <= epsilon) throw std::runtime_error("Camera fit distance must be positive");
            const std::array<float, 3> direction = normalize(subtract(box_center, this->current.eye));
            this->set_lookat(subtract(box_center, multiply(direction, distance)), box_center, this->current.up, instant);
        }

        void set_mode(const CameraMode value) {
            this->mode = value;
        }

        [[nodiscard]] CameraMode get_mode() const {
            return this->mode;
        }

        void set_speed(const float value) {
            if (value <= 0.0f) throw std::runtime_error("Camera speed must be positive");
            this->speed = value;
        }

        [[nodiscard]] float get_speed() const {
            return this->speed;
        }

        void set_animation_duration(const float value) {
            if (value < 0.0f) throw std::runtime_error("Camera animation duration must be non-negative");
            this->animation_duration_seconds = value;
        }

        [[nodiscard]] float get_animation_duration() const {
            return this->animation_duration_seconds;
        }

        [[nodiscard]] bool is_animating() const {
            return this->animating;
        }

        void set_mouse_position(const std::array<float, 2>& value) {
            this->mouse_position = value;
        }

        [[nodiscard]] CameraAction mouse_move(const std::array<float, 2>& screen_position, const CameraInputs& inputs) {
            if (!inputs.left_mouse && !inputs.middle_mouse && !inputs.right_mouse) {
                this->set_mouse_position(screen_position);
                return CameraAction::none;
            }

            CameraAction action = CameraAction::none;
            if (inputs.left_mouse) {
                if ((inputs.ctrl && inputs.shift) || inputs.alt)
                    action = this->mode == CameraMode::examine ? CameraAction::look_around : CameraAction::orbit;
                else if (inputs.shift)
                    action = CameraAction::dolly;
                else if (inputs.ctrl)
                    action = CameraAction::pan;
                else
                    action = this->mode == CameraMode::examine ? CameraAction::orbit : CameraAction::look_around;
            } else if (inputs.middle_mouse) {
                action = CameraAction::pan;
            } else if (inputs.right_mouse) {
                action = CameraAction::dolly;
            }

            if (action != CameraAction::none) this->motion(screen_position, action);
            return action;
        }

        void wheel(const float value, const CameraInputs& inputs) {
            if (value == 0.0f) return;
            const float delta = value * std::abs(value) / static_cast<float>(this->window_size[0]);
            if (inputs.shift) {
                if (this->current.projection == CameraProjection::orthographic)
                    this->zoom_orthographic(1.0f + delta);
                else
                    this->set_fov(this->current.fov_degrees + value);
                this->apply_user_change(this->current.projection == CameraProjection::orthographic);
                return;
            }
            this->dolly({delta, delta}, inputs.ctrl);
            this->apply_user_change(true);
        }

        void key_motion(const std::array<float, 2>& delta, const CameraAction action) {
            if (delta[0] == 0.0f && delta[1] == 0.0f) return;
            const CameraFrame frame = this->camera_frame();
            const std::array<float, 2> scaled{delta[0] * this->speed, delta[1] * this->speed};
            std::array<float, 3> movement{0.0f, 0.0f, 0.0f};
            if (action == CameraAction::dolly) {
                movement = multiply(frame.forward, scaled[0]);
                if (this->mode == CameraMode::walk) movement = this->project_to_ground_plane(movement);
            } else if (action == CameraAction::pan) {
                movement = add(multiply(frame.right, scaled[0]), multiply(frame.up, scaled[1]));
            }
            this->current.eye    = add(this->current.eye, movement);
            this->current.center = add(this->current.center, movement);
            this->apply_user_change(true);
        }

        void set_projection(const CameraProjection value) {
            if (value == this->current.projection) return;
            if (value == CameraProjection::perspective)
                this->convert_to_perspective();
            else
                this->convert_to_orthographic();
            this->apply_user_change(false);
        }

        [[nodiscard]] CameraProjection projection() const {
            return this->current.projection;
        }

        [[nodiscard]] bool orthographic() const {
            return this->current.projection == CameraProjection::orthographic;
        }

        void set_fov(const float value) {
            this->current.fov_degrees = std::clamp(value, min_fov, max_fov);
            this->apply_user_change(false);
        }

        void set_clip_planes(const std::array<float, 2>& value) {
            if (value[0] <= 0.0f || value[1] <= value[0]) throw std::runtime_error("Camera clip planes are invalid");
            this->current.near_far = value;
            this->apply_user_change(false);
        }

        void set_orthographic_magnitudes(const std::array<float, 2>& value) {
            if (value[0] <= 0.0f || value[1] <= 0.0f) throw std::runtime_error("Camera orthographic magnitudes must be positive");
            this->current.orthographic_magnitudes = value;
            this->apply_user_change(false);
        }

        [[nodiscard]] std::array<float, 16> view_projection(const float aspect) const {
            if (aspect <= 0.0f) throw std::runtime_error("Camera aspect ratio must be positive");
            return multiply(projection_matrix(aspect, this->current.near_far[1], true), this->view);
        }

        [[nodiscard]] std::array<float, 16> view_projection(const float aspect, const float far_clip) const {
            if (aspect <= 0.0f) throw std::runtime_error("Camera aspect ratio must be positive");
            if (far_clip <= this->current.near_far[0]) throw std::runtime_error("Camera far clip must be greater than near clip");
            return multiply(projection_matrix(aspect, far_clip, true), this->view);
        }

        [[nodiscard]] std::array<float, 16> view_matrix() const {
            return this->view;
        }

        [[nodiscard]] std::array<float, 16> gizmo_projection_matrix(const float aspect) const {
            if (aspect <= 0.0f) throw std::runtime_error("Camera aspect ratio must be positive");
            return projection_matrix(aspect, this->current.near_far[1], false);
        }

        [[nodiscard]] std::array<float, 3> position() const {
            return this->current.eye;
        }

        [[nodiscard]] std::array<float, 3> center() const {
            return this->current.center;
        }

        [[nodiscard]] std::array<float, 3> forward() const {
            return normalize(subtract(this->current.center, this->current.eye));
        }

        [[nodiscard]] std::array<float, 3> right() const {
            return this->camera_frame().right;
        }

        [[nodiscard]] std::array<float, 3> up() const {
            return this->camera_frame().up;
        }

        [[nodiscard]] float distance_to_center() const {
            return length(subtract(this->current.center, this->current.eye));
        }

        [[nodiscard]] float aspect_ratio() const {
            return static_cast<float>(this->window_size[0]) / static_cast<float>(this->window_size[1]);
        }

    private:
        struct CameraFrame {
            std::array<float, 3> forward{};
            std::array<float, 3> right{};
            std::array<float, 3> up{};
        };

        struct ViewDimensions {
            float width{0.0f};
            float height{0.0f};
        };

        static constexpr float epsilon{0.000001f};
        static constexpr float min_distance{0.000001f};
        static constexpr float min_fov{0.01f};
        static constexpr float max_fov{179.0f};
        static constexpr float min_orthographic_size{0.01f};
        static constexpr float max_dolly_displacement{0.99f};
        static constexpr float default_animation_duration{0.5f};

        CameraState current{};
        CameraState goal{};
        std::optional<CameraState> snapshot{};
        std::array<std::array<float, 3>, 3> bezier_points{};
        std::array<float, 16> view{
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        std::array<std::uint32_t, 2> window_size{1920, 1080};
        std::array<float, 2> mouse_position{0.0f, 0.0f};
        CameraMode mode{CameraMode::examine};
        float speed{3.0f};
        float animation_duration_seconds{default_animation_duration};
        float animation_elapsed{0.0f};
        bool animating{false};

        void update_view_matrix() {
            this->view = lookat_matrix(this->current.eye, this->current.center, effective_up(this->current.eye, this->current.center, this->current.up));
        }

        void motion(const std::array<float, 2>& screen_position, const CameraAction action) {
            const std::array<float, 2> displacement{
                (screen_position[0] - this->mouse_position[0]) / static_cast<float>(this->window_size[0]),
                (screen_position[1] - this->mouse_position[1]) / static_cast<float>(this->window_size[1]),
            };
            if (action == CameraAction::orbit) this->orbit(displacement, false);
            if (action == CameraAction::dolly) this->dolly(displacement, false);
            if (action == CameraAction::pan) this->pan(displacement);
            if (action == CameraAction::look_around) this->orbit({displacement[0], -displacement[1]}, true);
            this->mouse_position = screen_position;
            this->apply_user_change(true);
        }

        void pan(std::array<float, 2> displacement) {
            if (displacement[0] == 0.0f && displacement[1] == 0.0f) return;
            if (this->mode == CameraMode::fly) displacement = {-displacement[0], -displacement[1]};
            const CameraFrame frame      = this->camera_frame();
            const ViewDimensions viewbox = this->view_dimensions();
            const std::array<float, 3> offset = add(multiply(frame.right, -displacement[0] * viewbox.width), multiply(frame.up, displacement[1] * viewbox.height));
            this->current.eye               = add(this->current.eye, offset);
            this->current.center            = add(this->current.center, offset);
        }

        void orbit(std::array<float, 2> displacement, const bool invert) {
            if (displacement[0] == 0.0f && displacement[1] == 0.0f) return;
            displacement = {displacement[0] * two_pi(), displacement[1] * two_pi()};

            const std::array<float, 3> origin   = invert ? this->current.eye : this->current.center;
            const std::array<float, 3> position = invert ? this->current.center : this->current.eye;
            std::array<float, 3> center_to_eye  = subtract(position, origin);
            const float radius                  = length(center_to_eye);
            if (radius < epsilon) return;
            center_to_eye = normalize(center_to_eye);

            constexpr float pole_padding = 0.001f;
            const float cos_elevation    = dot(center_to_eye, this->current.up);
            std::array<float, 3> horizontal = subtract(center_to_eye, multiply(this->current.up, cos_elevation));
            const float sin_elevation       = length(horizontal);
            const float elevation           = std::atan2(sin_elevation, cos_elevation);

            if (sin_elevation < epsilon) {
                const std::array<float, 3> reference = std::abs(this->current.up[0]) < 0.9f ? std::array<float, 3>{1.0f, 0.0f, 0.0f} : std::array<float, 3>{0.0f, 0.0f, 1.0f};
                horizontal                          = normalize(subtract(reference, multiply(this->current.up, dot(reference, this->current.up))));
            } else {
                horizontal = multiply(horizontal, 1.0f / sin_elevation);
            }

            const float yaw_cos = std::cos(-displacement[0]);
            const float yaw_sin = std::sin(-displacement[0]);
            horizontal          = add(multiply(horizontal, yaw_cos), multiply(cross(this->current.up, horizontal), yaw_sin));
            const float new_elevation = std::clamp(elevation - displacement[1], pole_padding, std::numbers::pi_v<float> - pole_padding);
            center_to_eye             = multiply(add(multiply(this->current.up, std::cos(new_elevation)), multiply(horizontal, std::sin(new_elevation))), radius);
            const std::array<float, 3> new_position = add(center_to_eye, origin);

            if (!invert)
                this->current.eye = new_position;
            else
                this->current.center = new_position;
        }

        void dolly(const std::array<float, 2>& displacement, const bool keep_center_fixed) {
            const float larger = std::abs(displacement[0]) > std::abs(displacement[1]) ? displacement[0] : -displacement[1];
            if (this->current.projection == CameraProjection::orthographic) {
                this->zoom_orthographic(1.0f - larger);
                return;
            }

            std::array<float, 3> direction = subtract(this->current.center, this->current.eye);
            if (length(direction) < min_distance) return;
            if (larger >= max_dolly_displacement) return;
            direction = multiply(direction, larger);
            if (this->mode == CameraMode::walk) direction = this->project_to_ground_plane(direction);
            this->current.eye = add(this->current.eye, direction);
            if ((this->mode == CameraMode::fly || this->mode == CameraMode::walk) && !keep_center_fixed) this->current.center = add(this->current.center, direction);
        }

        void zoom_orthographic(const float factor) {
            this->current.orthographic_magnitudes[0] = std::max(this->current.orthographic_magnitudes[0] * factor, min_orthographic_size);
            this->current.orthographic_magnitudes[1] = std::max(this->current.orthographic_magnitudes[1] * factor, min_orthographic_size);
        }

        void convert_to_perspective() {
            const float distance = this->distance_to_center();
            if (distance <= epsilon || this->current.orthographic_magnitudes[1] <= 0.0f) throw std::runtime_error("Cannot convert camera to perspective with invalid orthographic size");
            this->current.fov_degrees = std::clamp(degrees(2.0f * std::atan(this->current.orthographic_magnitudes[1] / distance)), min_fov, max_fov);
            this->current.projection  = CameraProjection::perspective;
        }

        void convert_to_orthographic() {
            const float distance = this->distance_to_center();
            if (distance <= epsilon) throw std::runtime_error("Cannot convert camera to orthographic with invalid distance");
            this->current.orthographic_magnitudes[1] = distance * std::tan(radians(this->current.fov_degrees * 0.5f));
            this->current.orthographic_magnitudes[0] = this->current.orthographic_magnitudes[1] * this->aspect_ratio();
            this->current.projection                 = CameraProjection::orthographic;
        }

        void adjust_orthographic_aspect() {
            if (this->current.projection != CameraProjection::orthographic) return;
            this->current.orthographic_magnitudes[0] = this->current.orthographic_magnitudes[1] * this->aspect_ratio();
        }

        void apply_user_change(const bool update_matrix) {
            this->animating = false;
            this->snapshot.reset();
            if (update_matrix) this->update_view_matrix();
        }

        void find_bezier_points() {
            if (!this->snapshot) throw std::runtime_error("Camera bezier snapshot is missing");
            const std::array<float, 3> p0         = this->current.eye;
            const std::array<float, 3> p2         = this->goal.eye;
            const std::array<float, 3> interest   = multiply(add(this->goal.center, this->snapshot->center), 0.5f);
            const std::array<float, 3> midpoint   = multiply(add(p0, p2), 0.5f);
            const float radius                    = 0.5f * (length(subtract(p0, interest)) + length(subtract(p2, interest)));
            std::array<float, 3> to_midpoint      = subtract(midpoint, interest);
            if (dot(to_midpoint, to_midpoint) < epsilon) to_midpoint = {0.0f, 0.0f, 1.0f};
            const std::array<float, 3> pass_point = add(interest, multiply(normalize(to_midpoint), radius));
            std::array<float, 3> p1               = subtract(multiply(pass_point, 2.0f), multiply(add(p0, p2), 0.5f));
            const std::array<float, 3> average_up = normalize(add(this->snapshot->up, this->goal.up));
            p1                                    = add(p1, multiply(average_up, dot(subtract(midpoint, p1), average_up)));
            this->bezier_points                   = {p0, p1, p2};
        }

        [[nodiscard]] ViewDimensions view_dimensions() const {
            if (this->current.projection == CameraProjection::orthographic) return {this->current.orthographic_magnitudes[0] * 2.0f, this->current.orthographic_magnitudes[1] * 2.0f};
            const float distance = this->distance_to_center();
            const float height   = 2.0f * distance * std::tan(radians(this->current.fov_degrees * 0.5f));
            return {height * this->aspect_ratio(), height};
        }

        [[nodiscard]] CameraFrame camera_frame() const {
            const std::array<float, 3> forward = this->forward();
            const std::array<float, 3> right   = normalize(cross(forward, effective_up(this->current.eye, this->current.center, this->current.up)));
            return {forward, right, cross(right, forward)};
        }

        [[nodiscard]] std::array<float, 3> project_to_ground_plane(const std::array<float, 3>& value) const {
            const float up_length = dot(this->current.up, this->current.up);
            if (up_length < epsilon) throw std::runtime_error("Camera up vector is invalid for ground projection");
            return subtract(value, multiply(this->current.up, dot(value, this->current.up) / up_length));
        }

        [[nodiscard]] std::array<float, 16> projection_matrix(const float aspect, const float far_clip, const bool vulkan_y_flip) const {
            if (this->current.projection == CameraProjection::orthographic) {
                const float half_width  = this->current.orthographic_magnitudes[0];
                const float half_height = this->current.orthographic_magnitudes[1];
                const float y_scale     = vulkan_y_flip ? -1.0f / half_height : 1.0f / half_height;
                return {
                    1.0f / half_width, 0.0f, 0.0f, 0.0f,
                    0.0f, y_scale, 0.0f, 0.0f,
                    0.0f, 0.0f, -1.0f / (far_clip - this->current.near_far[0]), 0.0f,
                    0.0f, 0.0f, -this->current.near_far[0] / (far_clip - this->current.near_far[0]), 1.0f,
                };
            }

            const float focal  = 1.0f / std::tan(radians(this->current.fov_degrees * 0.5f));
            const float y_axis = vulkan_y_flip ? -focal : focal;
            return {
                focal / aspect, 0.0f, 0.0f, 0.0f,
                0.0f, y_axis, 0.0f, 0.0f,
                0.0f, 0.0f, far_clip / (this->current.near_far[0] - far_clip), -1.0f,
                0.0f, 0.0f, (far_clip * this->current.near_far[0]) / (this->current.near_far[0] - far_clip), 0.0f,
            };
        }

        static void validate(const CameraState& camera) {
            if (!finite(camera.eye) || !finite(camera.center) || !finite(camera.up)) throw std::runtime_error("Camera vectors must be finite");
            if (length(camera.up) <= epsilon) throw std::runtime_error("Camera up vector must be non-zero");
            if (length(subtract(camera.eye, camera.center)) < min_distance) throw std::runtime_error("Camera eye and center must not overlap");
            if (camera.fov_degrees < min_fov || camera.fov_degrees > max_fov) throw std::runtime_error("Camera FOV must stay within 0.01 and 179 degrees");
            if (camera.near_far[0] <= 0.0f || camera.near_far[1] <= camera.near_far[0]) throw std::runtime_error("Camera clip planes are invalid");
            if (camera.orthographic_magnitudes[0] <= 0.0f || camera.orthographic_magnitudes[1] <= 0.0f) throw std::runtime_error("Camera orthographic magnitudes must be positive");
        }

        [[nodiscard]] static bool finite(const std::array<float, 3>& value) {
            return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
        }

        [[nodiscard]] static float radians(const float degrees_value) {
            return degrees_value * std::numbers::pi_v<float> / 180.0f;
        }

        [[nodiscard]] static float degrees(const float radians_value) {
            return radians_value * 180.0f / std::numbers::pi_v<float>;
        }

        [[nodiscard]] static float two_pi() {
            return std::numbers::pi_v<float> * 2.0f;
        }

        [[nodiscard]] static float mix(const float left, const float right, const float value) {
            return left * (1.0f - value) + right * value;
        }

        template <std::size_t Count>
        [[nodiscard]] static std::array<float, Count> mix(const std::array<float, Count>& left, const std::array<float, Count>& right, const float value) {
            std::array<float, Count> result{};
            for (std::size_t index = 0; index < Count; ++index) result[index] = mix(left[index], right[index], value);
            return result;
        }

        [[nodiscard]] static std::array<float, 3> bezier(const float value, const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2) {
            const float inverse = 1.0f - value;
            return add(add(multiply(p0, inverse * inverse), multiply(p1, 2.0f * inverse * value)), multiply(p2, value * value));
        }

        [[nodiscard]] static std::array<float, 3> effective_up(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up) {
            const std::array<float, 3> right = cross(subtract(center, eye), up);
            if (dot(right, right) > epsilon) return up;
            return std::abs(up[1]) < 0.9f ? std::array<float, 3>{0.0f, 1.0f, 0.0f} : std::array<float, 3>{1.0f, 0.0f, 0.0f};
        }

        [[nodiscard]] static std::array<float, 16> lookat_matrix(const std::array<float, 3>& eye, const std::array<float, 3>& center, const std::array<float, 3>& up) {
            const std::array<float, 3> forward = normalize(subtract(center, eye));
            const std::array<float, 3> right   = normalize(cross(forward, up));
            const std::array<float, 3> view_up = cross(right, forward);
            return {
                right[0], view_up[0], -forward[0], 0.0f,
                right[1], view_up[1], -forward[1], 0.0f,
                right[2], view_up[2], -forward[2], 0.0f,
                -dot(right, eye), -dot(view_up, eye), dot(forward, eye), 1.0f,
            };
        }

        [[nodiscard]] static std::array<float, 3> transform_vector(const std::array<float, 16>& matrix, const std::array<float, 3>& vector) {
            return {
                matrix[0] * vector[0] + matrix[4] * vector[1] + matrix[8] * vector[2],
                matrix[1] * vector[0] + matrix[5] * vector[1] + matrix[9] * vector[2],
                matrix[2] * vector[0] + matrix[6] * vector[1] + matrix[10] * vector[2],
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
            const float value = length(vector);
            if (value <= epsilon) throw std::runtime_error("Cannot normalize zero-length camera vector");
            return multiply(vector, 1.0f / value);
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
