module spectra.scene.spatial;

import std;

namespace spectra::scene {
    namespace {
        void validate_scene_id(const std::string_view scene_id) {
            if (scene_id.empty()) throw std::runtime_error("Scene camera workspace requires a non-empty scene id");
        }

        void validate_camera_projection(const CameraProjection& projection) {
            switch (projection.kind) {
            case CameraProjectionKind::Perspective:
                if (!std::isfinite(projection.vertical_fov_degrees) || !(projection.vertical_fov_degrees > 0.0f) || !(projection.vertical_fov_degrees < 180.0f)) throw std::runtime_error("Scene camera vertical FOV must be inside (0, 180)");
                break;
            case CameraProjectionKind::Orthographic:
                if (!std::isfinite(projection.orthographic_height) || !(projection.orthographic_height > 0.0f)) throw std::runtime_error("Scene orthographic camera height must be positive");
                break;
            case CameraProjectionKind::Pinhole:
                if (projection.image_width == 0u || projection.image_height == 0u) throw std::runtime_error("Scene pinhole camera image size must be non-zero");
                if (!std::isfinite(projection.fx) || !std::isfinite(projection.fy) || !(projection.fx > 0.0f) || !(projection.fy > 0.0f)) throw std::runtime_error("Scene pinhole camera focal length must be positive");
                if (!std::isfinite(projection.cx) || !std::isfinite(projection.cy)) throw std::runtime_error("Scene pinhole camera principal point must be finite");
                break;
            }
            if (!std::isfinite(projection.near_plane) || !std::isfinite(projection.far_plane) || !(projection.near_plane > 0.0f) || !(projection.far_plane > projection.near_plane)) throw std::runtime_error("Scene camera clipping planes are invalid");
        }

        [[nodiscard]] float camera_projection_vertical_fov_radians(const CameraProjection& projection) {
            validate_camera_projection(projection);
            switch (projection.kind) {
            case CameraProjectionKind::Perspective:
                return projection.vertical_fov_degrees * std::numbers::pi_v<float> / 180.0f;
            case CameraProjectionKind::Pinhole:
                return 2.0f * std::atan(static_cast<float>(projection.image_height) * 0.5f / projection.fy);
            case CameraProjectionKind::Orthographic:
                throw std::runtime_error("Scene orthographic camera does not have a perspective vertical FOV");
            }
            throw std::runtime_error("Unknown scene camera projection kind");
        }

        void validate_camera_pose(const CameraPose& pose) {
            if (!is_finite(pose.position)) throw std::runtime_error("Scene camera pose position must be finite");
            static_cast<void>(normalized_quaternion(pose.orientation, "Scene camera pose"));
        }

        void validate_viewport_camera(const ViewportCamera& camera) {
            validate_camera_pose(camera.pose);
            if (!is_finite(camera.focus)) throw std::runtime_error("Scene viewport camera focus must be finite");
            if (!is_finite(camera.navigation_up)) throw std::runtime_error("Scene viewport camera navigation up must be finite");
            if (length_squared(camera.focus - camera.pose.position) <= 1.0e-12f) throw std::runtime_error("Scene viewport camera position and focus must not overlap");
            if (length_squared(camera.navigation_up) <= 1.0e-12f) throw std::runtime_error("Scene viewport camera navigation up must not be zero");
            if (length_squared(cross(camera.focus - camera.pose.position, camera.navigation_up)) <= 1.0e-12f) throw std::runtime_error("Scene viewport camera navigation up must not be parallel to the view direction");
            validate_camera_projection(camera.projection);
        }

        void validate_viewport_camera_delta(const ViewportCameraDelta delta) {
            if (!std::isfinite(delta.x_pixels) || !std::isfinite(delta.y_pixels)) throw std::runtime_error("Scene viewport camera delta must be finite");
        }

        void validate_viewport_camera_size(const ViewportCameraSize viewport) {
            if (!std::isfinite(viewport.width) || !std::isfinite(viewport.height) || !(viewport.width > 0.0f) || !(viewport.height > 0.0f)) throw std::runtime_error("Scene viewport camera size must be finite and positive");
        }

        [[nodiscard]] float clamp_viewport_camera_pitch(const float pitch) {
            constexpr float limit = std::numbers::pi_v<float> * 0.49f;
            return std::clamp(pitch, -limit, limit);
        }

        [[nodiscard]] Quaternion quaternion_from_frame(const Vector3 right, const Vector3 y_axis, const Vector3 forward, const std::string_view context) {
            const float m00 = right.x;
            const float m01 = y_axis.x;
            const float m02 = forward.x;
            const float m10 = right.y;
            const float m11 = y_axis.y;
            const float m12 = forward.y;
            const float m20 = right.z;
            const float m21 = y_axis.z;
            const float m22 = forward.z;
            Quaternion result{};
            const float trace = m00 + m11 + m22;
            if (trace > 0.0f) {
                const float s = std::sqrt(trace + 1.0f) * 2.0f;
                result.w = 0.25f * s;
                result.x = (m21 - m12) / s;
                result.y = (m02 - m20) / s;
                result.z = (m10 - m01) / s;
                return normalized_quaternion(result, context);
            }
            if (m00 > m11 && m00 > m22) {
                const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
                result.w = (m21 - m12) / s;
                result.x = 0.25f * s;
                result.y = (m01 + m10) / s;
                result.z = (m02 + m20) / s;
                return normalized_quaternion(result, context);
            }
            if (m11 > m22) {
                const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
                result.w = (m02 - m20) / s;
                result.x = (m01 + m10) / s;
                result.y = 0.25f * s;
                result.z = (m12 + m21) / s;
                return normalized_quaternion(result, context);
            }
            const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
            result.w = (m10 - m01) / s;
            result.x = (m02 + m20) / s;
            result.y = (m12 + m21) / s;
            result.z = 0.25f * s;
            return normalized_quaternion(result, context);
        }

        [[nodiscard]] Quaternion axis_angle_quaternion(const Vector3 axis, const float radians, const std::string_view context) {
            const Vector3 normalized_axis = normalize(axis, context);
            const float half = radians * 0.5f;
            const float s = std::sin(half);
            return Quaternion{normalized_axis.x * s, normalized_axis.y * s, normalized_axis.z * s, std::cos(half)};
        }

        [[nodiscard]] float camera_matrix_value(const std::array<float, 16>& matrix, const std::size_t row, const std::size_t column) {
            return matrix.at(row * 4u + column);
        }

        [[nodiscard]] std::array<float, 16> multiply_camera_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
            std::array<float, 16> result{};
            for (std::size_t row = 0u; row < 4u; ++row)
                for (std::size_t column = 0u; column < 4u; ++column)
                    result.at(row * 4u + column) =
                        camera_matrix_value(left, row, 0u) * camera_matrix_value(right, 0u, column) +
                        camera_matrix_value(left, row, 1u) * camera_matrix_value(right, 1u, column) +
                        camera_matrix_value(left, row, 2u) * camera_matrix_value(right, 2u, column) +
                        camera_matrix_value(left, row, 3u) * camera_matrix_value(right, 3u, column);
            return result;
        }

        [[nodiscard]] std::array<Vector3, 3> validated_camera_basis(Vector3 right, Vector3 down, Vector3 forward, const std::string_view context) {
            right = normalize(right, std::format("{} right", context));
            down = normalize(down, std::format("{} down", context));
            forward = normalize(forward, std::format("{} forward", context));
            if (std::abs(dot(right, down)) > 1.0e-4f || std::abs(dot(right, forward)) > 1.0e-4f || std::abs(dot(down, forward)) > 1.0e-4f) throw std::runtime_error(std::format("{} basis must be orthogonal", context));
            if (dot(cross(right, down), forward) <= 0.0f) throw std::runtime_error(std::format("{} basis must satisfy cross(right, down) == forward in Spectra image-down camera space", context));
            return {right, down, forward};
        }
    } // namespace

    CameraPose camera_pose_from_look_at(const Vector3 eye, const Vector3 target, const Vector3 navigation_up) {
        const Vector3 forward = normalize(target - eye, "Scene camera look-at forward");
        const Vector3 down = -normalize(navigation_up, "Scene camera look-at navigation up");
        const Vector3 right = normalize(cross(down, forward), "Scene camera look-at right");
        const Vector3 camera_down = cross(forward, right);
        return CameraPose{
            .position = eye,
            .orientation = quaternion_from_frame(right, camera_down, forward, "Scene camera look-at orientation"),
        };
    }

    CameraPose camera_pose_from_frame(const Vector3 position, const Vector3 right, const Vector3 down, const Vector3 forward) {
        if (!is_finite(position)) throw std::runtime_error("Scene camera frame position must be finite");
        const std::array<Vector3, 3> basis = validated_camera_basis(right, down, forward, "Scene camera frame");
        return CameraPose{
            .position = position,
            .orientation = quaternion_from_frame(basis[0], basis[1], basis[2], "Scene camera frame orientation"),
        };
    }

    ViewportCamera viewport_camera_from_look_at(const Vector3 eye, const Vector3 target, const Vector3 navigation_up, CameraProjection projection) {
        ViewportCamera view{
            .pose = camera_pose_from_look_at(eye, target, navigation_up),
            .focus = target,
            .navigation_up = normalize(navigation_up, "Scene camera navigation up"),
            .projection = projection,
        };
        validate_viewport_camera(view);
        return view;
    }

    CameraFrame camera_frame(const CameraPose& pose) {
        validate_camera_pose(pose);
        const Quaternion rotation = normalized_quaternion(pose.orientation, "Scene camera pose");
        return CameraFrame{
            .position = pose.position,
            .right = normalize(rotate_vector(rotation, Vector3{1.0f, 0.0f, 0.0f}), "Scene camera frame right"),
            .down = normalize(rotate_vector(rotation, Vector3{0.0f, 1.0f, 0.0f}), "Scene camera frame down"),
            .forward = normalize(rotate_vector(rotation, Vector3{0.0f, 0.0f, 1.0f}), "Scene camera frame forward"),
        };
    }

    SceneTransform camera_world_from_camera(const CameraPose& pose) {
        const CameraFrame frame = camera_frame(pose);
        SceneTransform transform{};
        transform.matrix = {
            frame.right.x, frame.down.x, frame.forward.x, frame.position.x,
            frame.right.y, frame.down.y, frame.forward.y, frame.position.y,
            frame.right.z, frame.down.z, frame.forward.z, frame.position.z,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        transform.inverse = {
            frame.right.x, frame.right.y, frame.right.z, -dot(frame.right, frame.position),
            frame.down.x, frame.down.y, frame.down.z, -dot(frame.down, frame.position),
            frame.forward.x, frame.forward.y, frame.forward.z, -dot(frame.forward, frame.position),
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        return transform;
    }

    CameraPose camera_pose_from_world_from_camera(const SceneTransform& world_from_camera) {
        const Vector3 position{world_from_camera.matrix.at(3u), world_from_camera.matrix.at(7u), world_from_camera.matrix.at(11u)};
        const Vector3 right = normalize(Vector3{world_from_camera.matrix.at(0u), world_from_camera.matrix.at(4u), world_from_camera.matrix.at(8u)}, "Scene camera world transform right");
        const Vector3 down = normalize(Vector3{world_from_camera.matrix.at(1u), world_from_camera.matrix.at(5u), world_from_camera.matrix.at(9u)}, "Scene camera world transform down");
        const Vector3 forward = normalize(Vector3{world_from_camera.matrix.at(2u), world_from_camera.matrix.at(6u), world_from_camera.matrix.at(10u)}, "Scene camera world transform forward");
        return camera_pose_from_frame(position, right, down, forward);
    }

    float camera_projection_vertical_fov_degrees(const CameraProjection& projection) {
        return camera_projection_vertical_fov_radians(projection) * 180.0f / std::numbers::pi_v<float>;
    }

    VulkanCameraMatrices make_vulkan_camera_matrices(const CameraPose& pose, const CameraProjection& projection, const float aspect, const float far_plane) {
        validate_camera_pose(pose);
        validate_camera_projection(projection);
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Scene Vulkan camera aspect ratio must be positive");
        if (!std::isfinite(far_plane) || far_plane <= projection.near_plane) throw std::runtime_error("Scene Vulkan camera far plane is invalid");
        const CameraFrame frame = camera_frame(pose);
        const std::array<float, 16> world_to_view{
            frame.right.x, frame.down.x, -frame.forward.x, 0.0f,
            frame.right.y, frame.down.y, -frame.forward.y, 0.0f,
            frame.right.z, frame.down.z, -frame.forward.z, 0.0f,
            -dot(frame.right, frame.position), -dot(frame.down, frame.position), dot(frame.forward, frame.position), 1.0f,
        };
        const std::array<float, 16> view_to_world{
            frame.right.x, frame.right.y, frame.right.z, 0.0f,
            frame.down.x, frame.down.y, frame.down.z, 0.0f,
            -frame.forward.x, -frame.forward.y, -frame.forward.z, 0.0f,
            frame.position.x, frame.position.y, frame.position.z, 1.0f,
        };

        std::array<float, 16> view_to_clip{};
        std::array<float, 16> clip_to_view{};
        if (projection.kind == CameraProjectionKind::Perspective || projection.kind == CameraProjectionKind::Pinhole) {
            const float f = 1.0f / std::tan(camera_projection_vertical_fov_radians(projection) * 0.5f);
            const float depth_scale = -(far_plane * projection.near_plane) / (far_plane - projection.near_plane);
            view_to_clip = {
                f / aspect, 0.0f, 0.0f, 0.0f,
                0.0f, f, 0.0f, 0.0f,
                0.0f, 0.0f, far_plane / (projection.near_plane - far_plane), -1.0f,
                0.0f, 0.0f, depth_scale, 0.0f,
            };
            clip_to_view = {
                aspect / f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f / f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f / depth_scale,
                0.0f, 0.0f, -1.0f, far_plane / (projection.near_plane - far_plane) / depth_scale,
            };
        } else if (projection.kind == CameraProjectionKind::Orthographic) {
            const float height = projection.orthographic_height;
            const float width = height * aspect;
            view_to_clip = {
                2.0f / width, 0.0f, 0.0f, 0.0f,
                0.0f, 2.0f / height, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f / (projection.near_plane - far_plane), 0.0f,
                0.0f, 0.0f, projection.near_plane / (projection.near_plane - far_plane), 1.0f,
            };
            clip_to_view = {
                width * 0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, height * 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, projection.near_plane - far_plane, 0.0f,
                0.0f, 0.0f, -projection.near_plane, 1.0f,
            };
        } else {
            throw std::runtime_error("Unknown scene camera projection kind");
        }

        return VulkanCameraMatrices{
            .world_to_clip = multiply_camera_matrix(world_to_view, view_to_clip),
            .clip_to_world = multiply_camera_matrix(clip_to_view, view_to_world),
            .frame = frame,
            .far_plane = far_plane,
        };
    }

    void CameraWorkspace::ensure_camera(std::string scene_id, ViewportCamera state) {
        validate_scene_id(scene_id);
        validate_viewport_camera(state);
        std::scoped_lock lock{this->mutex};
        if (this->cameras.contains(scene_id)) return;
        this->cameras.emplace(std::move(scene_id), CameraSnapshot{
                                                       .revision = CameraRevision{1},
                                                       .state    = std::move(state),
                                                   });
    }

    CameraSnapshot CameraWorkspace::reset_camera(std::string scene_id, ViewportCamera state) {
        validate_scene_id(scene_id);
        validate_viewport_camera(state);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, CameraSnapshot>::iterator found = this->cameras.find(scene_id);
        const CameraRevision revision = found == this->cameras.end() ? CameraRevision{1u} : CameraRevision{found->second.revision.value + 1u};
        CameraSnapshot snapshot{
            .revision = revision,
            .state    = std::move(state),
        };
        this->cameras.insert_or_assign(std::move(scene_id), snapshot);
        return snapshot;
    }

    CameraSnapshot CameraWorkspace::snapshot(const std::string_view scene_id) const {
        validate_scene_id(scene_id);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, CameraSnapshot>::const_iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        return found->second;
    }

    CameraSnapshot CameraWorkspace::commit(const std::string_view scene_id, ViewportCamera state) {
        validate_scene_id(scene_id);
        validate_viewport_camera(state);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, CameraSnapshot>::iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        found->second = CameraSnapshot{
            .revision = CameraRevision{found->second.revision.value + 1u},
            .state    = std::move(state),
        };
        return found->second;
    }

    float viewport_drag_zoom_steps(const ViewportCameraDelta delta) {
        validate_viewport_camera_delta(delta);
        return -delta.y_pixels * 0.035f;
    }

    ViewportCamera orbit_viewport_camera(ViewportCamera state, const ViewportCameraDelta delta) {
        validate_viewport_camera(state);
        validate_viewport_camera_delta(delta);
        if (delta.x_pixels == 0.0f && delta.y_pixels == 0.0f) return state;
        constexpr float orbit_radians_per_pixel = 0.006f;
        const Vector3 offset = state.pose.position - state.focus;
        const float distance = length(offset);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera orbit distance must be positive");
        const Vector3 up_axis = normalize(state.navigation_up, "Scene viewport camera orbit up");
        const Vector3 offset_direction = offset / distance;
        const float current_pitch = std::asin(std::clamp(dot(offset_direction, up_axis), -1.0f, 1.0f));
        Vector3 horizontal = offset_direction - up_axis * dot(offset_direction, up_axis);
        if (length_squared(horizontal) <= 1.0e-12f) horizontal = -camera_frame(state.pose).forward;
        horizontal = normalize(horizontal, "Scene viewport camera orbit horizontal direction");
        const Quaternion yaw_rotation = axis_angle_quaternion(up_axis, -delta.x_pixels * orbit_radians_per_pixel, "Scene viewport camera yaw axis");
        const Vector3 yawed_horizontal = normalize(rotate_vector(yaw_rotation, horizontal), "Scene viewport camera yaw direction");
        const float pitch = clamp_viewport_camera_pitch(current_pitch + delta.y_pixels * orbit_radians_per_pixel);
        const Vector3 direction = yawed_horizontal * std::cos(pitch) + up_axis * std::sin(pitch);
        state.pose.position = state.focus + direction * distance;
        state.pose = camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        validate_viewport_camera(state);
        return state;
    }

    ViewportCamera pan_viewport_camera(ViewportCamera state, const ViewportCameraDelta delta, const ViewportCameraSize viewport) {
        validate_viewport_camera(state);
        validate_viewport_camera_delta(delta);
        validate_viewport_camera_size(viewport);
        if (delta.x_pixels == 0.0f && delta.y_pixels == 0.0f) return state;
        const CameraFrame frame = camera_frame(state.pose);
        const float distance = length(state.pose.position - state.focus);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera pan distance must be positive");
        const float pan_scale = state.projection.kind != CameraProjectionKind::Orthographic
                                    ? 2.0f * distance * std::tan(camera_projection_vertical_fov_radians(state.projection) * 0.5f) / viewport.height
                                    : state.projection.orthographic_height / viewport.height;
        if (!std::isfinite(pan_scale) || !(pan_scale > 0.0f)) throw std::runtime_error("Scene viewport camera pan scale must be positive");
        const float horizontal_offset = delta.x_pixels * pan_scale;
        const float vertical_offset = delta.y_pixels * pan_scale;
        const Vector3 offset = frame.right * horizontal_offset - frame.down * vertical_offset;
        state.pose.position += offset;
        state.focus += offset;
        validate_viewport_camera(state);
        return state;
    }

    ViewportCamera zoom_viewport_camera(ViewportCamera state, const float steps) {
        validate_viewport_camera(state);
        if (!std::isfinite(steps)) throw std::runtime_error("Scene viewport camera zoom steps must be finite");
        if (steps == 0.0f) return state;
        constexpr float minimum_distance = 0.02f;
        constexpr float maximum_distance = 1000000.0f;
        const Vector3 target_to_eye = state.pose.position - state.focus;
        const float distance = length(target_to_eye);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera zoom distance must be positive");
        const float zoom_factor = std::pow(0.88f, steps);
        if (!std::isfinite(zoom_factor) || !(zoom_factor > 0.0f)) throw std::runtime_error("Scene viewport camera zoom factor must be positive");
        const float new_distance = std::clamp(distance * zoom_factor, minimum_distance, maximum_distance);
        state.pose.position = state.focus + target_to_eye / distance * new_distance;
        state.pose = camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        validate_viewport_camera(state);
        return state;
    }
} // namespace spectra::scene
