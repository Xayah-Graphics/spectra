module spectra.scene.spatial;

import std;

namespace spectra::scene {
    namespace {
        void validate_scene_id(const std::string_view scene_id) {
            if (scene_id.empty()) throw std::runtime_error("Scene camera workspace requires a non-empty scene id");
        }

        void validate_coordinate_convention(const CoordinateConvention& convention, const std::string_view context) {
            if (!is_finite(convention.right) || !is_finite(convention.up) || !is_finite(convention.forward)) throw std::runtime_error(std::format("{} coordinate axes must be finite", context));
            if (length_squared(convention.right) <= 1.0e-12f || length_squared(convention.up) <= 1.0e-12f || length_squared(convention.forward) <= 1.0e-12f) throw std::runtime_error(std::format("{} coordinate axes must not be zero", context));
            const Vector3 right = normalize(convention.right, std::format("{} right", context));
            const Vector3 up = normalize(convention.up, std::format("{} up", context));
            const Vector3 forward = normalize(convention.forward, std::format("{} forward", context));
            if (std::abs(dot(right, up)) > 1.0e-4f || std::abs(dot(right, forward)) > 1.0e-4f || std::abs(dot(up, forward)) > 1.0e-4f) throw std::runtime_error(std::format("{} coordinate axes must be orthogonal", context));
            const float handedness = dot(cross(right, up), forward);
            if (convention.handedness == CoordinateHandedness::Right && handedness <= 0.0f) throw std::runtime_error(std::format("{} is not right-handed", context));
            if (convention.handedness == CoordinateHandedness::Left && handedness >= 0.0f) throw std::runtime_error(std::format("{} is not left-handed", context));
        }

        void validate_coordinate_system(const CoordinateSystem& coordinate_system, const std::string_view context) {
            if (coordinate_system.name.empty()) throw std::runtime_error(std::format("{} coordinate system name must not be empty", context));
            validate_coordinate_convention(coordinate_system.convention, context);
            if (!std::isfinite(coordinate_system.unit_scale_to_meter) || coordinate_system.unit_scale_to_meter <= 0.0f) throw std::runtime_error(std::format("{} unit scale must be positive", context));
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
            validate_coordinate_convention(pose.local_convention, "Scene camera local convention");
            static_cast<void>(normalized_quaternion(pose.orientation, "Scene camera pose"));
        }

        void validate_camera_view_state(const CameraViewState& view) {
            validate_camera_pose(view.pose);
            if (!is_finite(view.focus)) throw std::runtime_error("Scene camera focus must be finite");
            if (!is_finite(view.navigation_up)) throw std::runtime_error("Scene camera navigation up must be finite");
            if (length_squared(view.focus - view.pose.position) <= 1.0e-12f) throw std::runtime_error("Scene camera position and focus must not overlap");
            if (length_squared(view.navigation_up) <= 1.0e-12f) throw std::runtime_error("Scene camera navigation up must not be zero");
            if (length_squared(cross(view.focus - view.pose.position, view.navigation_up)) <= 1.0e-12f) throw std::runtime_error("Scene camera navigation up must not be parallel to the view direction");
            validate_camera_projection(view.projection);
        }

        void validate_camera_state(const CameraState& state) {
            validate_camera_view_state(state.view);
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

        [[nodiscard]] Quaternion quaternion_from_frame(const Vector3 right, const Vector3 up, const Vector3 forward, const std::string_view context) {
            const float m00 = right.x;
            const float m01 = up.x;
            const float m02 = forward.x;
            const float m10 = right.y;
            const float m11 = up.y;
            const float m12 = forward.y;
            const float m20 = right.z;
            const float m21 = up.z;
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

        [[nodiscard]] std::array<float, 16> coordinate_basis_to_world_matrix(const CoordinateSystem& coordinate_system) {
            validate_coordinate_system(coordinate_system, std::format("Coordinate system \"{}\"", coordinate_system.name));
            const float scale = coordinate_system.unit_scale_to_meter;
            return std::array<float, 16>{
                coordinate_system.convention.right.x * scale, coordinate_system.convention.right.y * scale, coordinate_system.convention.right.z * scale, 0.0f,
                coordinate_system.convention.up.x * scale, coordinate_system.convention.up.y * scale, coordinate_system.convention.up.z * scale, 0.0f,
                coordinate_system.convention.forward.x * scale, coordinate_system.convention.forward.y * scale, coordinate_system.convention.forward.z * scale, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
        }

        [[nodiscard]] std::array<float, 16> coordinate_world_to_basis_matrix(const CoordinateSystem& coordinate_system) {
            validate_coordinate_system(coordinate_system, std::format("Coordinate system \"{}\"", coordinate_system.name));
            const float inverse_scale = 1.0f / coordinate_system.unit_scale_to_meter;
            return std::array<float, 16>{
                coordinate_system.convention.right.x * inverse_scale, coordinate_system.convention.up.x * inverse_scale, coordinate_system.convention.forward.x * inverse_scale, 0.0f,
                coordinate_system.convention.right.y * inverse_scale, coordinate_system.convention.up.y * inverse_scale, coordinate_system.convention.forward.y * inverse_scale, 0.0f,
                coordinate_system.convention.right.z * inverse_scale, coordinate_system.convention.up.z * inverse_scale, coordinate_system.convention.forward.z * inverse_scale, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
        }

        [[nodiscard]] Vector3 transform_vector_by_matrix(const std::array<float, 16>& matrix, const Vector3 vector, const float w) {
            return Vector3{
                vector.x * matrix.at(0u) + vector.y * matrix.at(4u) + vector.z * matrix.at(8u) + w * matrix.at(12u),
                vector.x * matrix.at(1u) + vector.y * matrix.at(5u) + vector.z * matrix.at(9u) + w * matrix.at(13u),
                vector.x * matrix.at(2u) + vector.y * matrix.at(6u) + vector.z * matrix.at(10u) + w * matrix.at(14u),
            };
        }

        [[nodiscard]] std::string axis_label(const Vector3 axis, const std::string_view context) {
            const Vector3 normalized_axis = normalize(axis, context);
            struct AxisCandidate {
                Vector3 direction{};
                std::string_view label{};
            };
            constexpr std::array candidates{
                AxisCandidate{.direction = Vector3{1.0f, 0.0f, 0.0f}, .label = "+X"},
                AxisCandidate{.direction = Vector3{-1.0f, 0.0f, 0.0f}, .label = "-X"},
                AxisCandidate{.direction = Vector3{0.0f, 1.0f, 0.0f}, .label = "+Y"},
                AxisCandidate{.direction = Vector3{0.0f, -1.0f, 0.0f}, .label = "-Y"},
                AxisCandidate{.direction = Vector3{0.0f, 0.0f, 1.0f}, .label = "+Z"},
                AxisCandidate{.direction = Vector3{0.0f, 0.0f, -1.0f}, .label = "-Z"},
            };
            for (const AxisCandidate& candidate : candidates) {
                if (std::abs(dot(normalized_axis, candidate.direction) - 1.0f) <= 1.0e-4f) return std::string{candidate.label};
            }
            return std::format("({:.4f}, {:.4f}, {:.4f})", normalized_axis.x, normalized_axis.y, normalized_axis.z);
        }

        [[nodiscard]] std::string handedness_label(const CoordinateHandedness handedness) {
            switch (handedness) {
            case CoordinateHandedness::Right:
                return "right-handed";
            case CoordinateHandedness::Left:
                return "left-handed";
            }
            throw std::runtime_error("Unknown coordinate handedness");
        }
    } // namespace

    CoordinateSystem coordinate_system(const std::string_view name) {
        if (name == "SpectraYUp" || name == "PBRT") {
            return CoordinateSystem{
                .name = std::string{name},
                .convention = CoordinateConvention{
                    .right = Vector3{1.0f, 0.0f, 0.0f},
                    .up = Vector3{0.0f, 1.0f, 0.0f},
                    .forward = Vector3{0.0f, 0.0f, 1.0f},
                    .handedness = CoordinateHandedness::Right,
                },
                .unit_scale_to_meter = 1.0f,
            };
        }
        if (name == "BlenderZUp") {
            return CoordinateSystem{
                .name = std::string{name},
                .convention = CoordinateConvention{
                    .right = Vector3{1.0f, 0.0f, 0.0f},
                    .up = Vector3{0.0f, 0.0f, 1.0f},
                    .forward = Vector3{0.0f, -1.0f, 0.0f},
                    .handedness = CoordinateHandedness::Right,
                },
                .unit_scale_to_meter = 1.0f,
            };
        }
        if (name == "OpenGL") {
            return CoordinateSystem{
                .name = "OpenGL",
                .convention = CoordinateConvention{
                    .right = Vector3{1.0f, 0.0f, 0.0f},
                    .up = Vector3{0.0f, 1.0f, 0.0f},
                    .forward = Vector3{0.0f, 0.0f, -1.0f},
                    .handedness = CoordinateHandedness::Left,
                },
                .unit_scale_to_meter = 1.0f,
            };
        }
        if (name == "OpenCV") {
            return CoordinateSystem{
                .name = std::string{name},
                .convention = CoordinateConvention{
                    .right = Vector3{1.0f, 0.0f, 0.0f},
                    .up = Vector3{0.0f, -1.0f, 0.0f},
                    .forward = Vector3{0.0f, 0.0f, 1.0f},
                    .handedness = CoordinateHandedness::Left,
                },
                .unit_scale_to_meter = 1.0f,
            };
        }
        throw std::runtime_error(std::format("Unknown Spectra coordinate system \"{}\"", name));
    }

    std::vector<std::string_view> coordinate_system_names() {
        return std::vector<std::string_view>{"SpectraYUp", "PBRT", "BlenderZUp", "OpenGL", "OpenCV"};
    }

    ClipConvention clip_convention(const std::string_view name) {
        if (name == "VulkanClip") {
            return ClipConvention{
                .name = "VulkanClip",
                .depth_range = ClipDepthRange::ZeroToOne,
                .y_axis = ClipYAxisDirection::Down,
            };
        }
        if (name == "DirectXClip") {
            return ClipConvention{
                .name = "DirectXClip",
                .depth_range = ClipDepthRange::ZeroToOne,
                .y_axis = ClipYAxisDirection::Up,
            };
        }
        throw std::runtime_error(std::format("Unknown Spectra clip convention \"{}\"", name));
    }

    std::vector<std::string_view> clip_convention_names() {
        return std::vector<std::string_view>{"VulkanClip", "DirectXClip"};
    }

    std::string coordinate_convention_label(const CoordinateConvention& convention) {
        validate_coordinate_convention(convention, "Coordinate convention label");
        return std::format(
            "right {}, up {}, forward {}, {}",
            axis_label(convention.right, "Coordinate convention label right axis"),
            axis_label(convention.up, "Coordinate convention label up axis"),
            axis_label(convention.forward, "Coordinate convention label forward axis"),
            handedness_label(convention.handedness)
        );
    }

    std::string coordinate_system_label(const CoordinateSystem& coordinate_system) {
        validate_coordinate_system(coordinate_system, std::format("Coordinate system \"{}\"", coordinate_system.name));
        if (std::abs(coordinate_system.unit_scale_to_meter - 1.0f) <= 1.0e-6f) return coordinate_convention_label(coordinate_system.convention);
        return std::format("{}, 1 unit = {:.6g} m", coordinate_convention_label(coordinate_system.convention), coordinate_system.unit_scale_to_meter);
    }

    FrameTransform frame_transform(const CoordinateSystem& from, const CoordinateSystem& to) {
        validate_coordinate_system(from, std::format("Source coordinate system \"{}\"", from.name));
        validate_coordinate_system(to, std::format("Destination coordinate system \"{}\"", to.name));
        return FrameTransform{
            .from = from.name,
            .to = to.name,
            .matrix = multiply_camera_matrix(coordinate_basis_to_world_matrix(from), coordinate_world_to_basis_matrix(to)),
            .inverse = multiply_camera_matrix(coordinate_basis_to_world_matrix(to), coordinate_world_to_basis_matrix(from)),
        };
    }

    Vector3 transform_point(const FrameTransform& transform, const Vector3 point) {
        if (transform.from.empty() || transform.to.empty()) throw std::runtime_error("Scene frame transform endpoints must not be empty");
        if (!is_finite(point)) throw std::runtime_error("Scene frame transform point must be finite");
        const Vector3 result = transform_vector_by_matrix(transform.matrix, point, 1.0f);
        if (!is_finite(result)) throw std::runtime_error("Scene frame transform produced a non-finite point");
        return result;
    }

    Vector3 transform_direction(const FrameTransform& transform, const Vector3 direction) {
        if (transform.from.empty() || transform.to.empty()) throw std::runtime_error("Scene frame transform endpoints must not be empty");
        if (!is_finite(direction)) throw std::runtime_error("Scene frame transform direction must be finite");
        const Vector3 result = transform_vector_by_matrix(transform.matrix, direction, 0.0f);
        if (!is_finite(result)) throw std::runtime_error("Scene frame transform produced a non-finite direction");
        return result;
    }

    CameraPose camera_pose_from_look_at(const Vector3 eye, const Vector3 target, const Vector3 up) {
        const Vector3 forward = normalize(target - eye, "Scene camera look-at forward");
        const Vector3 right = normalize(cross(normalize(up, "Scene camera look-at up"), forward), "Scene camera look-at right");
        const Vector3 camera_up = cross(forward, right);
        return CameraPose{
            .position = eye,
            .orientation = quaternion_from_frame(right, camera_up, forward, "Scene camera look-at orientation"),
            .local_convention = coordinate_system("SpectraYUp").convention,
        };
    }

    CameraViewState camera_view_from_look_at(const Vector3 eye, const Vector3 target, const Vector3 up, CameraProjection projection) {
        CameraViewState view{
            .pose = camera_pose_from_look_at(eye, target, up),
            .focus = target,
            .navigation_up = normalize(up, "Scene camera navigation up"),
            .projection = projection,
        };
        validate_camera_view_state(view);
        return view;
    }

    CameraFrame camera_frame(const CameraPose& pose) {
        validate_camera_pose(pose);
        const Quaternion rotation = normalized_quaternion(pose.orientation, "Scene camera pose");
        return CameraFrame{
            .position = pose.position,
            .right = normalize(rotate_vector(rotation, pose.local_convention.right), "Scene camera frame right"),
            .up = normalize(rotate_vector(rotation, pose.local_convention.up), "Scene camera frame up"),
            .forward = normalize(rotate_vector(rotation, pose.local_convention.forward), "Scene camera frame forward"),
        };
    }

    SceneTransform camera_world_from_camera(const CameraPose& pose) {
        const CameraFrame frame = camera_frame(pose);
        SceneTransform transform{};
        transform.matrix = {
            frame.right.x, frame.up.x, frame.forward.x, frame.position.x,
            frame.right.y, frame.up.y, frame.forward.y, frame.position.y,
            frame.right.z, frame.up.z, frame.forward.z, frame.position.z,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        transform.inverse = {
            frame.right.x, frame.right.y, frame.right.z, -dot(frame.right, frame.position),
            frame.up.x, frame.up.y, frame.up.z, -dot(frame.up, frame.position),
            frame.forward.x, frame.forward.y, frame.forward.z, -dot(frame.forward, frame.position),
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        return transform;
    }

    CameraPose camera_pose_from_world_from_camera(const SceneTransform& world_from_camera) {
        const Vector3 position{world_from_camera.matrix.at(3u), world_from_camera.matrix.at(7u), world_from_camera.matrix.at(11u)};
        const Vector3 right = normalize(Vector3{world_from_camera.matrix.at(0u), world_from_camera.matrix.at(4u), world_from_camera.matrix.at(8u)}, "Scene camera world transform right");
        const Vector3 up = normalize(Vector3{world_from_camera.matrix.at(1u), world_from_camera.matrix.at(5u), world_from_camera.matrix.at(9u)}, "Scene camera world transform up");
        const Vector3 forward = normalize(Vector3{world_from_camera.matrix.at(2u), world_from_camera.matrix.at(6u), world_from_camera.matrix.at(10u)}, "Scene camera world transform forward");
        return CameraPose{
            .position = position,
            .orientation = quaternion_from_frame(right, up, forward, "Scene camera world transform orientation"),
            .local_convention = coordinate_system("SpectraYUp").convention,
        };
    }

    float camera_projection_vertical_fov_degrees(const CameraProjection& projection) {
        return camera_projection_vertical_fov_radians(projection) * 180.0f / std::numbers::pi_v<float>;
    }

    VulkanCameraMatrices make_vulkan_camera_matrices(const CameraViewState& view, const float aspect, const float far_plane) {
        validate_camera_view_state(view);
        if (!std::isfinite(aspect) || aspect <= 0.0f) throw std::runtime_error("Scene Vulkan camera aspect ratio must be positive");
        if (!std::isfinite(far_plane) || far_plane <= view.projection.near_plane) throw std::runtime_error("Scene Vulkan camera far plane is invalid");
        const CameraFrame frame = camera_frame(view.pose);
        const std::array<float, 16> world_to_view{
            frame.right.x, frame.up.x, -frame.forward.x, 0.0f,
            frame.right.y, frame.up.y, -frame.forward.y, 0.0f,
            frame.right.z, frame.up.z, -frame.forward.z, 0.0f,
            -dot(frame.right, frame.position), -dot(frame.up, frame.position), dot(frame.forward, frame.position), 1.0f,
        };
        const std::array<float, 16> view_to_world{
            frame.right.x, frame.right.y, frame.right.z, 0.0f,
            frame.up.x, frame.up.y, frame.up.z, 0.0f,
            -frame.forward.x, -frame.forward.y, -frame.forward.z, 0.0f,
            frame.position.x, frame.position.y, frame.position.z, 1.0f,
        };

        std::array<float, 16> view_to_clip{};
        std::array<float, 16> clip_to_view{};
        if (view.projection.kind == CameraProjectionKind::Perspective || view.projection.kind == CameraProjectionKind::Pinhole) {
            const float f = 1.0f / std::tan(camera_projection_vertical_fov_radians(view.projection) * 0.5f);
            const float depth_scale = -(far_plane * view.projection.near_plane) / (far_plane - view.projection.near_plane);
            view_to_clip = {
                f / aspect, 0.0f, 0.0f, 0.0f,
                0.0f, -f, 0.0f, 0.0f,
                0.0f, 0.0f, far_plane / (view.projection.near_plane - far_plane), -1.0f,
                0.0f, 0.0f, depth_scale, 0.0f,
            };
            clip_to_view = {
                aspect / f, 0.0f, 0.0f, 0.0f,
                0.0f, -1.0f / f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f / depth_scale,
                0.0f, 0.0f, -1.0f, far_plane / (view.projection.near_plane - far_plane) / depth_scale,
            };
        } else if (view.projection.kind == CameraProjectionKind::Orthographic) {
            const float height = view.projection.orthographic_height;
            const float width = height * aspect;
            view_to_clip = {
                2.0f / width, 0.0f, 0.0f, 0.0f,
                0.0f, -2.0f / height, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f / (view.projection.near_plane - far_plane), 0.0f,
                0.0f, 0.0f, view.projection.near_plane / (view.projection.near_plane - far_plane), 1.0f,
            };
            clip_to_view = {
                width * 0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, height * -0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, view.projection.near_plane - far_plane, 0.0f,
                0.0f, 0.0f, -view.projection.near_plane, 1.0f,
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

    void CameraWorkspace::ensure_camera(std::string scene_id, CameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
        std::scoped_lock lock{this->mutex};
        if (this->cameras.contains(scene_id)) return;
        this->cameras.emplace(std::move(scene_id), CameraSnapshot{
                                                       .revision = CameraRevision{1},
                                                       .state    = std::move(state),
                                                   });
    }

    CameraSnapshot CameraWorkspace::reset_camera(std::string scene_id, CameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
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

    CameraSnapshot CameraWorkspace::commit(const std::string_view scene_id, CameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
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

    CameraState orbit_viewport_camera(CameraState state, const ViewportCameraDelta delta) {
        validate_camera_state(state);
        validate_viewport_camera_delta(delta);
        if (delta.x_pixels == 0.0f && delta.y_pixels == 0.0f) return state;
        constexpr float orbit_radians_per_pixel = 0.006f;
        const Vector3 offset = state.view.pose.position - state.view.focus;
        const float distance = length(offset);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera orbit distance must be positive");
        const Vector3 up_axis = normalize(state.view.navigation_up, "Scene viewport camera orbit up");
        const Vector3 offset_direction = offset / distance;
        const float current_pitch = std::asin(std::clamp(dot(offset_direction, up_axis), -1.0f, 1.0f));
        Vector3 horizontal = offset_direction - up_axis * dot(offset_direction, up_axis);
        if (length_squared(horizontal) <= 1.0e-12f) horizontal = -camera_frame(state.view.pose).forward;
        horizontal = normalize(horizontal, "Scene viewport camera orbit horizontal direction");
        const Quaternion yaw_rotation = axis_angle_quaternion(up_axis, delta.x_pixels * orbit_radians_per_pixel, "Scene viewport camera yaw axis");
        const Vector3 yawed_horizontal = normalize(rotate_vector(yaw_rotation, horizontal), "Scene viewport camera yaw direction");
        const float pitch = clamp_viewport_camera_pitch(current_pitch + delta.y_pixels * orbit_radians_per_pixel);
        const Vector3 direction = yawed_horizontal * std::cos(pitch) + up_axis * std::sin(pitch);
        state.view.pose.position = state.view.focus + direction * distance;
        state.view.pose = camera_pose_from_look_at(state.view.pose.position, state.view.focus, state.view.navigation_up);
        validate_camera_state(state);
        return state;
    }

    CameraState pan_viewport_camera(CameraState state, const ViewportCameraDelta delta, const ViewportCameraSize viewport) {
        validate_camera_state(state);
        validate_viewport_camera_delta(delta);
        validate_viewport_camera_size(viewport);
        if (delta.x_pixels == 0.0f && delta.y_pixels == 0.0f) return state;
        const CameraFrame frame = camera_frame(state.view.pose);
        const float distance = length(state.view.pose.position - state.view.focus);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera pan distance must be positive");
        const float pan_scale = state.view.projection.kind != CameraProjectionKind::Orthographic
                                    ? 2.0f * distance * std::tan(camera_projection_vertical_fov_radians(state.view.projection) * 0.5f) / viewport.height
                                    : state.view.projection.orthographic_height / viewport.height;
        if (!std::isfinite(pan_scale) || !(pan_scale > 0.0f)) throw std::runtime_error("Scene viewport camera pan scale must be positive");
        const float horizontal_offset = -delta.x_pixels * pan_scale;
        const float vertical_offset = delta.y_pixels * pan_scale;
        const Vector3 offset = frame.right * horizontal_offset + frame.up * vertical_offset;
        state.view.pose.position += offset;
        state.view.focus += offset;
        validate_camera_state(state);
        return state;
    }

    CameraState zoom_viewport_camera(CameraState state, const float steps) {
        validate_camera_state(state);
        if (!std::isfinite(steps)) throw std::runtime_error("Scene viewport camera zoom steps must be finite");
        if (steps == 0.0f) return state;
        constexpr float minimum_distance = 0.02f;
        constexpr float maximum_distance = 1000000.0f;
        const Vector3 target_to_eye = state.view.pose.position - state.view.focus;
        const float distance = length(target_to_eye);
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Scene viewport camera zoom distance must be positive");
        const float zoom_factor = std::pow(0.88f, steps);
        if (!std::isfinite(zoom_factor) || !(zoom_factor > 0.0f)) throw std::runtime_error("Scene viewport camera zoom factor must be positive");
        const float new_distance = std::clamp(distance * zoom_factor, minimum_distance, maximum_distance);
        state.view.pose.position = state.view.focus + target_to_eye / distance * new_distance;
        state.view.pose = camera_pose_from_look_at(state.view.pose.position, state.view.focus, state.view.navigation_up);
        validate_camera_state(state);
        return state;
    }
} // namespace spectra::scene
