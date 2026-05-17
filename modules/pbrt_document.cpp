module;
#include <cstring>
#include <imgui.h>
#include <pbrt/cpu/render.h>
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/mesh.h>
#include <pbrt/wavefront/wavefront.h>

#include <vulkan/vulkan_raii.hpp>

module pbrt_document;
import std;

#if !defined(SPECTRA_SHADER_DIR)
#error "SPECTRA_SHADER_DIR must be defined by CMake"
#endif

namespace {
    struct PbrtPreviewSurfaceShaderParameters {
        std::array<float, 16> view_projection{};
        std::array<float, 4> light_direction{};
    };

    struct PbrtPreviewOverlayShaderParameters {
        std::array<float, 16> model_view_projection{};
        std::array<float, 4> bounds_min{};
        std::array<float, 4> bounds_max{};
        std::array<float, 4> color{};
    };

    [[nodiscard]] std::array<float, 16> identity_matrix() {
        return {
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> pbrt_file_matrix_to_spectra_matrix(const std::vector<float>& values) {
        if (values.size() != 16) throw std::runtime_error("PBRT transform command must have 16 values");
        std::array<float, 16> result{};
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t column = 0; column < 4; ++column) result[column * 4u + row] = values[row * 4u + column];
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> spectra_matrix_to_pbrt_file_matrix(const std::array<float, 16>& matrix) {
        std::array<float, 16> result{};
        for (std::uint32_t row = 0; row < 4; ++row) {
            for (std::uint32_t column = 0; column < 4; ++column) result[row * 4u + column] = matrix[column * 4u + row];
        }
        return result;
    }

    [[nodiscard]] std::array<float, 16> translation_matrix(const float x, const float y, const float z) {
        std::array<float, 16> result = identity_matrix();
        result[12]                   = x;
        result[13]                   = y;
        result[14]                   = z;
        return result;
    }

    [[nodiscard]] std::array<float, 16> scale_matrix(const float x, const float y, const float z) {
        std::array<float, 16> result = identity_matrix();
        result[0]                    = x;
        result[5]                    = y;
        result[10]                   = z;
        return result;
    }

    [[nodiscard]] std::array<float, 16> rotation_matrix(const float angle_degrees, const float x, const float y, const float z) {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (length <= 0.000001f) throw std::runtime_error("PBRT Rotate axis must be non-zero");
        const float axis_x = x / length;
        const float axis_y = y / length;
        const float axis_z = z / length;
        const float angle  = angle_degrees * 0.017453292519943295769f;
        const float c      = std::cos(angle);
        const float s      = std::sin(angle);
        const float t      = 1.0f - c;
        return {
            t * axis_x * axis_x + c,
            t * axis_x * axis_y + s * axis_z,
            t * axis_x * axis_z - s * axis_y,
            0.0f,
            t * axis_x * axis_y - s * axis_z,
            t * axis_y * axis_y + c,
            t * axis_y * axis_z + s * axis_x,
            0.0f,
            t * axis_x * axis_z + s * axis_y,
            t * axis_y * axis_z - s * axis_x,
            t * axis_z * axis_z + c,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
        };
    }

    [[nodiscard]] std::array<float, 16> multiply_preview_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        return xayah::multiply_matrix(left, right);
    }

    [[nodiscard]] float parameter_float(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name, const float default_value) {
        for (const xayah::PbrtParameter& parameter : parameters) {
            if (parameter.name == name && !parameter.floats.empty()) return parameter.floats.front();
            if (parameter.name == name && !parameter.ints.empty()) return static_cast<float>(parameter.ints.front());
        }
        return default_value;
    }

    [[nodiscard]] const xayah::PbrtParameter* find_parameter(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name) {
        for (const xayah::PbrtParameter& parameter : parameters) {
            if (parameter.name == name) return &parameter;
        }
        return nullptr;
    }

    [[nodiscard]] std::string parameter_string(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name, const std::string_view default_value) {
        const xayah::PbrtParameter* parameter = find_parameter(parameters, name);
        if (parameter == nullptr || parameter->strings.empty()) return std::string{default_value};
        return parameter->strings.front();
    }

    [[nodiscard]] std::vector<float> parameter_floats(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name) {
        const xayah::PbrtParameter* parameter = find_parameter(parameters, name);
        if (parameter == nullptr) return {};
        std::vector<float> result = parameter->floats;
        result.reserve(result.size() + parameter->ints.size());
        for (const int value : parameter->ints) result.push_back(static_cast<float>(value));
        return result;
    }

    [[nodiscard]] std::vector<int> parameter_ints(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name) {
        const xayah::PbrtParameter* parameter = find_parameter(parameters, name);
        if (parameter == nullptr) return {};
        std::vector<int> result = parameter->ints;
        result.reserve(result.size() + parameter->floats.size());
        for (const float value : parameter->floats) result.push_back(static_cast<int>(value));
        return result;
    }

    [[nodiscard]] bool parameter_present(const std::vector<xayah::PbrtParameter>& parameters, const std::string_view name) {
        return find_parameter(parameters, name) != nullptr;
    }

    [[nodiscard]] xayah::BoundingBoxBounds preview_bounds_for_shape(const std::string& type, const std::vector<xayah::PbrtParameter>& parameters) {
        if (type == "sphere") {
            const float radius = parameter_float(parameters, "radius", 1.0f);
            return {{-radius, -radius, -radius}, {radius, radius, radius}};
        }
        if (type == "disk") {
            const float radius = parameter_float(parameters, "radius", 1.0f);
            return {{-radius, -radius, -0.001f}, {radius, radius, 0.001f}};
        }
        if (type == "trianglemesh") {
            bool initialized{};
            xayah::BoundingBoxBounds result{};
            for (const xayah::PbrtParameter& parameter : parameters) {
                if (parameter.name != "P" || parameter.floats.size() < 3) continue;
                for (std::size_t index = 0; index + 2 < parameter.floats.size(); index += 3) {
                    const std::array<float, 3> point{parameter.floats[index], parameter.floats[index + 1], parameter.floats[index + 2]};
                    if (!initialized) {
                        result      = {point, point};
                        initialized = true;
                    } else {
                        for (std::uint32_t axis = 0; axis < 3; ++axis) {
                            if (point[axis] < result.minimum[axis]) result.minimum[axis] = point[axis];
                            if (point[axis] > result.maximum[axis]) result.maximum[axis] = point[axis];
                        }
                    }
                }
            }
            if (initialized) return result;
        }
        return {{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    }

    [[nodiscard]] float clamp_float(const float value, const float minimum, const float maximum) {
        return std::max(minimum, std::min(maximum, value));
    }

    [[nodiscard]] std::array<float, 3> subtract_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    }

    [[nodiscard]] std::array<float, 3> cross_vector(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0],
        };
    }

    [[nodiscard]] std::array<float, 3> normalize_vector(const std::array<float, 3>& value) {
        const float length = std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        if (length <= 0.000001f) return {0.0f, 1.0f, 0.0f};
        return {value[0] / length, value[1] / length, value[2] / length};
    }

    [[nodiscard]] std::array<float, 3> transform_direction(const std::array<float, 16>& matrix, const std::array<float, 3>& vector) {
        return {
            matrix[0] * vector[0] + matrix[4] * vector[1] + matrix[8] * vector[2],
            matrix[1] * vector[0] + matrix[5] * vector[1] + matrix[9] * vector[2],
            matrix[2] * vector[0] + matrix[6] * vector[1] + matrix[10] * vector[2],
        };
    }

    [[nodiscard]] std::array<float, 16> camera_to_world_look_at_matrix(const float eye_x, const float eye_y, const float eye_z, const float look_x, const float look_y, const float look_z, const float up_x, const float up_y, const float up_z) {
        const std::array<float, 3> eye{eye_x, eye_y, eye_z};
        const std::array<float, 3> look{look_x, look_y, look_z};
        const std::array<float, 3> up = normalize_vector({up_x, up_y, up_z});
        const std::array<float, 3> direction = normalize_vector(subtract_vector(look, eye));
        const std::array<float, 3> raw_right = cross_vector(up, direction);
        const float right_length = std::sqrt(raw_right[0] * raw_right[0] + raw_right[1] * raw_right[1] + raw_right[2] * raw_right[2]);
        if (right_length <= 0.000001f) throw std::runtime_error("PBRT LookAt up vector is parallel to the view direction");
        const std::array<float, 3> right = {raw_right[0] / right_length, raw_right[1] / right_length, raw_right[2] / right_length};
        const std::array<float, 3> corrected_up = cross_vector(direction, right);
        return {
            right[0],
            right[1],
            right[2],
            0.0f,
            corrected_up[0],
            corrected_up[1],
            corrected_up[2],
            0.0f,
            direction[0],
            direction[1],
            direction[2],
            0.0f,
            eye[0],
            eye[1],
            eye[2],
            1.0f,
        };
    }

    [[nodiscard]] xayah::BoundingBoxBounds bounds_for_vertices(const std::vector<xayah::PbrtPreviewVertex>& vertices) {
        if (vertices.empty()) throw std::runtime_error("Cannot compute bounds for empty PBRT preview mesh");
        xayah::BoundingBoxBounds result{vertices.front().position, vertices.front().position};
        for (const xayah::PbrtPreviewVertex& vertex : vertices) {
            for (std::uint32_t axis = 0; axis < 3; ++axis) {
                if (vertex.position[axis] < result.minimum[axis]) result.minimum[axis] = vertex.position[axis];
                if (vertex.position[axis] > result.maximum[axis]) result.maximum[axis] = vertex.position[axis];
            }
        }
        return result;
    }

    void append_preview_triangle(std::vector<xayah::PbrtPreviewVertex>& vertices, const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2, const std::array<float, 3>& n0, const std::array<float, 3>& n1, const std::array<float, 3>& n2) {
        vertices.push_back({p0, n0, {}});
        vertices.push_back({p1, n1, {}});
        vertices.push_back({p2, n2, {}});
    }

    void append_preview_triangle_flat(std::vector<xayah::PbrtPreviewVertex>& vertices, const std::array<float, 3>& p0, const std::array<float, 3>& p1, const std::array<float, 3>& p2) {
        const std::array<float, 3> normal = normalize_vector(cross_vector(subtract_vector(p1, p0), subtract_vector(p2, p0)));
        append_preview_triangle(vertices, p0, p1, p2, normal, normal, normal);
    }

    struct PreviewMeshBuildResult {
        bool supported{false};
        std::string message{};
        std::vector<xayah::PbrtPreviewVertex> vertices{};
        xayah::BoundingBoxBounds local_bounds{{-0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}};
    };

    [[nodiscard]] PreviewMeshBuildResult make_supported_preview_mesh(std::vector<xayah::PbrtPreviewVertex>&& vertices) {
        if (vertices.empty()) throw std::runtime_error("Supported PBRT preview mesh must not be empty");
        PreviewMeshBuildResult result{};
        result.supported    = true;
        result.message      = "Preview ready";
        result.vertices     = std::move(vertices);
        result.local_bounds = bounds_for_vertices(result.vertices);
        return result;
    }

    [[nodiscard]] PreviewMeshBuildResult make_unsupported_preview_mesh(const std::string& message, const xayah::BoundingBoxBounds& bounds) {
        PreviewMeshBuildResult result{};
        result.supported    = false;
        result.message      = message;
        result.local_bounds = bounds;
        return result;
    }

    [[nodiscard]] std::array<float, 3> point_from_values(const std::vector<float>& values, const std::size_t point_index) {
        const std::size_t offset = point_index * 3u;
        return {values[offset], values[offset + 1u], values[offset + 2u]};
    }

    [[nodiscard]] PreviewMeshBuildResult build_trianglemesh_preview_mesh(const std::vector<xayah::PbrtParameter>& parameters) {
        const std::vector<float> point_values = parameter_floats(parameters, "P");
        if (point_values.empty()) return make_unsupported_preview_mesh("trianglemesh preview requires point3 P", preview_bounds_for_shape("trianglemesh", parameters));
        if (point_values.size() % 3u != 0u) return make_unsupported_preview_mesh("trianglemesh P value count is not divisible by 3", preview_bounds_for_shape("trianglemesh", parameters));

        const std::size_t point_count = point_values.size() / 3u;
        std::vector<int> indices      = parameter_ints(parameters, "indices");
        if (indices.empty()) {
            if (point_count != 3u) return make_unsupported_preview_mesh("trianglemesh preview requires integer indices unless P has exactly one triangle", preview_bounds_for_shape("trianglemesh", parameters));
            indices = {0, 1, 2};
        }
        if (indices.size() % 3u != 0u) return make_unsupported_preview_mesh("trianglemesh index count is not divisible by 3", preview_bounds_for_shape("trianglemesh", parameters));

        const std::vector<float> normal_values = parameter_floats(parameters, "N");
        const bool has_normals                 = normal_values.size() == point_values.size();
        std::vector<xayah::PbrtPreviewVertex> vertices{};
        vertices.reserve(indices.size());
        for (std::size_t index = 0; index < indices.size(); index += 3u) {
            const int i0 = indices[index + 0u];
            const int i1 = indices[index + 1u];
            const int i2 = indices[index + 2u];
            if (i0 < 0 || i1 < 0 || i2 < 0) return make_unsupported_preview_mesh("trianglemesh has negative vertex index", preview_bounds_for_shape("trianglemesh", parameters));
            if (static_cast<std::size_t>(i0) >= point_count || static_cast<std::size_t>(i1) >= point_count || static_cast<std::size_t>(i2) >= point_count) return make_unsupported_preview_mesh("trianglemesh index is outside P range", preview_bounds_for_shape("trianglemesh", parameters));
            const std::array<float, 3> p0 = point_from_values(point_values, static_cast<std::size_t>(i0));
            const std::array<float, 3> p1 = point_from_values(point_values, static_cast<std::size_t>(i1));
            const std::array<float, 3> p2 = point_from_values(point_values, static_cast<std::size_t>(i2));
            if (has_normals) {
                const std::array<float, 3> n0 = normalize_vector(point_from_values(normal_values, static_cast<std::size_t>(i0)));
                const std::array<float, 3> n1 = normalize_vector(point_from_values(normal_values, static_cast<std::size_t>(i1)));
                const std::array<float, 3> n2 = normalize_vector(point_from_values(normal_values, static_cast<std::size_t>(i2)));
                append_preview_triangle(vertices, p0, p1, p2, n0, n1, n2);
            } else {
                append_preview_triangle_flat(vertices, p0, p1, p2);
            }
        }
        return make_supported_preview_mesh(std::move(vertices));
    }

    [[nodiscard]] std::array<float, 3> sphere_point(const float radius, const float theta, const float phi) {
        const float sin_theta = std::sin(theta);
        return {radius * sin_theta * std::cos(phi), radius * sin_theta * std::sin(phi), radius * std::cos(theta)};
    }

    [[nodiscard]] PreviewMeshBuildResult build_sphere_preview_mesh(const std::vector<xayah::PbrtParameter>& parameters) {
        constexpr float pi = 3.14159265358979323846f;
        const float radius = parameter_float(parameters, "radius", 1.0f);
        if (radius <= 0.0f) return make_unsupported_preview_mesh("sphere radius must be positive", preview_bounds_for_shape("sphere", parameters));
        const float zmin                      = clamp_float(std::min(parameter_float(parameters, "zmin", -radius), parameter_float(parameters, "zmax", radius)), -radius, radius);
        const float zmax                      = clamp_float(std::max(parameter_float(parameters, "zmin", -radius), parameter_float(parameters, "zmax", radius)), -radius, radius);
        const float phi_max                   = clamp_float(parameter_float(parameters, "phimax", 360.0f), 0.0f, 360.0f) * pi / 180.0f;
        const float theta_min                 = std::acos(clamp_float(zmax / radius, -1.0f, 1.0f));
        const float theta_max                 = std::acos(clamp_float(zmin / radius, -1.0f, 1.0f));
        constexpr std::uint32_t segment_count = 48;
        constexpr std::uint32_t stack_count   = 24;
        if (phi_max <= 0.0f || theta_max <= theta_min) return make_unsupported_preview_mesh("sphere clipped range has no previewable area", preview_bounds_for_shape("sphere", parameters));

        std::vector<xayah::PbrtPreviewVertex> vertices{};
        vertices.reserve(static_cast<std::size_t>(segment_count) * stack_count * 6u);
        for (std::uint32_t stack = 0; stack < stack_count; ++stack) {
            const float t0     = static_cast<float>(stack) / static_cast<float>(stack_count);
            const float t1     = static_cast<float>(stack + 1u) / static_cast<float>(stack_count);
            const float theta0 = theta_min + (theta_max - theta_min) * t0;
            const float theta1 = theta_min + (theta_max - theta_min) * t1;
            for (std::uint32_t segment = 0; segment < segment_count; ++segment) {
                const float p0                 = static_cast<float>(segment) / static_cast<float>(segment_count);
                const float p1                 = static_cast<float>(segment + 1u) / static_cast<float>(segment_count);
                const float phi0               = phi_max * p0;
                const float phi1               = phi_max * p1;
                const std::array<float, 3> v00 = sphere_point(radius, theta0, phi0);
                const std::array<float, 3> v10 = sphere_point(radius, theta0, phi1);
                const std::array<float, 3> v01 = sphere_point(radius, theta1, phi0);
                const std::array<float, 3> v11 = sphere_point(radius, theta1, phi1);
                append_preview_triangle(vertices, v00, v01, v11, normalize_vector(v00), normalize_vector(v01), normalize_vector(v11));
                append_preview_triangle(vertices, v00, v11, v10, normalize_vector(v00), normalize_vector(v11), normalize_vector(v10));
            }
        }
        return make_supported_preview_mesh(std::move(vertices));
    }

    [[nodiscard]] std::array<float, 3> disk_point(const float radius, const float height, const float phi) {
        return {radius * std::cos(phi), radius * std::sin(phi), height};
    }

    [[nodiscard]] PreviewMeshBuildResult build_disk_preview_mesh(const std::vector<xayah::PbrtParameter>& parameters) {
        constexpr float pi = 3.14159265358979323846f;
        const float height = parameter_float(parameters, "height", 0.0f);
        const float radius = parameter_float(parameters, "radius", 1.0f);
        const float inner  = parameter_float(parameters, "innerradius", 0.0f);
        if (radius <= 0.0f) return make_unsupported_preview_mesh("disk radius must be positive", preview_bounds_for_shape("disk", parameters));
        if (inner < 0.0f || inner >= radius) return make_unsupported_preview_mesh("disk inner radius must be in [0, radius)", preview_bounds_for_shape("disk", parameters));
        const float phi_max = clamp_float(parameter_float(parameters, "phimax", 360.0f), 0.0f, 360.0f) * pi / 180.0f;
        if (phi_max <= 0.0f) return make_unsupported_preview_mesh("disk phimax has no previewable area", preview_bounds_for_shape("disk", parameters));

        constexpr std::uint32_t segment_count = 96;
        std::vector<xayah::PbrtPreviewVertex> vertices{};
        vertices.reserve(static_cast<std::size_t>(segment_count) * 6u);
        constexpr std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
        for (std::uint32_t segment = 0; segment < segment_count; ++segment) {
            const float p0                    = static_cast<float>(segment) / static_cast<float>(segment_count);
            const float p1                    = static_cast<float>(segment + 1u) / static_cast<float>(segment_count);
            const float phi0                  = phi_max * p0;
            const float phi1                  = phi_max * p1;
            const std::array<float, 3> inner0 = disk_point(inner, height, phi0);
            const std::array<float, 3> inner1 = disk_point(inner, height, phi1);
            const std::array<float, 3> outer0 = disk_point(radius, height, phi0);
            const std::array<float, 3> outer1 = disk_point(radius, height, phi1);
            append_preview_triangle(vertices, inner0, outer0, outer1, normal, normal, normal);
            append_preview_triangle(vertices, inner0, outer1, inner1, normal, normal, normal);
        }
        return make_supported_preview_mesh(std::move(vertices));
    }

    [[nodiscard]] std::filesystem::path resolve_pbrt_asset_path(const std::filesystem::path& source_path, const std::string& filename) {
        if (filename.empty()) throw std::runtime_error("PBRT asset filename must not be empty");
        std::filesystem::path path{filename};
        if (path.is_absolute()) return path;
        if (!source_path.empty()) return source_path.parent_path() / path;
        return std::filesystem::current_path() / path;
    }

    [[nodiscard]] std::array<float, 3> pbrt_point(const pbrt::Point3f& point) {
        return {static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)};
    }

    [[nodiscard]] std::array<float, 3> pbrt_normal(const pbrt::Normal3f& normal) {
        return normalize_vector({static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z)});
    }

    [[nodiscard]] PreviewMeshBuildResult build_plymesh_preview_mesh(const std::filesystem::path& source_path, const std::vector<xayah::PbrtParameter>& parameters) {
        if (parameter_present(parameters, "displacement")) return make_unsupported_preview_mesh("plymesh displacement preview is not supported", preview_bounds_for_shape("plymesh", parameters));
        const std::filesystem::path path = resolve_pbrt_asset_path(source_path, parameter_string(parameters, "filename", ""));
        if (!std::filesystem::exists(path)) return make_unsupported_preview_mesh(std::string{"plymesh file does not exist: "} + path.string(), preview_bounds_for_shape("plymesh", parameters));

        pbrt::TriQuadMesh mesh = pbrt::TriQuadMesh::ReadPLY(path.string());
        mesh.ConvertToOnlyTriangles();
        if (mesh.p.empty() || mesh.triIndices.empty()) return make_unsupported_preview_mesh("plymesh has no previewable triangles", preview_bounds_for_shape("plymesh", parameters));

        const bool has_normals = mesh.n.size() == mesh.p.size();
        std::vector<xayah::PbrtPreviewVertex> vertices{};
        vertices.reserve(mesh.triIndices.size());
        for (std::size_t index = 0; index < mesh.triIndices.size(); index += 3u) {
            const int i0 = mesh.triIndices[index + 0u];
            const int i1 = mesh.triIndices[index + 1u];
            const int i2 = mesh.triIndices[index + 2u];
            if (i0 < 0 || i1 < 0 || i2 < 0) return make_unsupported_preview_mesh("plymesh has negative vertex index", preview_bounds_for_shape("plymesh", parameters));
            if (static_cast<std::size_t>(i0) >= mesh.p.size() || static_cast<std::size_t>(i1) >= mesh.p.size() || static_cast<std::size_t>(i2) >= mesh.p.size()) return make_unsupported_preview_mesh("plymesh index is outside vertex range", preview_bounds_for_shape("plymesh", parameters));
            const std::array<float, 3> p0 = pbrt_point(mesh.p[static_cast<std::size_t>(i0)]);
            const std::array<float, 3> p1 = pbrt_point(mesh.p[static_cast<std::size_t>(i1)]);
            const std::array<float, 3> p2 = pbrt_point(mesh.p[static_cast<std::size_t>(i2)]);
            if (has_normals) {
                append_preview_triangle(vertices, p0, p1, p2, pbrt_normal(mesh.n[static_cast<std::size_t>(i0)]), pbrt_normal(mesh.n[static_cast<std::size_t>(i1)]), pbrt_normal(mesh.n[static_cast<std::size_t>(i2)]));
            } else {
                append_preview_triangle_flat(vertices, p0, p1, p2);
            }
        }
        return make_supported_preview_mesh(std::move(vertices));
    }

    [[nodiscard]] PreviewMeshBuildResult build_preview_mesh_for_shape(const std::filesystem::path& source_path, const std::string& type, const std::vector<xayah::PbrtParameter>& parameters) {
        if (type == "trianglemesh") return build_trianglemesh_preview_mesh(parameters);
        if (type == "sphere") return build_sphere_preview_mesh(parameters);
        if (type == "disk") return build_disk_preview_mesh(parameters);
        if (type == "plymesh") return build_plymesh_preview_mesh(source_path, parameters);
        return make_unsupported_preview_mesh(std::string{"Unsupported PBRT preview shape: "} + type, preview_bounds_for_shape(type, parameters));
    }

    [[nodiscard]] xayah::BoundingBoxBounds transformed_bounds(const std::array<float, 16>& matrix, const xayah::BoundingBoxBounds& bounds) {
        bool initialized{};
        xayah::BoundingBoxBounds result{};
        for (std::uint32_t corner = 0; corner < 8; ++corner) {
            const std::array<float, 3> local_point{
                (corner & 1u) != 0u ? bounds.maximum[0] : bounds.minimum[0],
                (corner & 2u) != 0u ? bounds.maximum[1] : bounds.minimum[1],
                (corner & 4u) != 0u ? bounds.maximum[2] : bounds.minimum[2],
            };
            const std::array<float, 3> point = xayah::transform_point(matrix, local_point);
            if (!initialized) {
                result      = {point, point};
                initialized = true;
            } else {
                for (std::uint32_t axis = 0; axis < 3; ++axis) {
                    if (point[axis] < result.minimum[axis]) result.minimum[axis] = point[axis];
                    if (point[axis] > result.maximum[axis]) result.maximum[axis] = point[axis];
                }
            }
        }
        if (!initialized) throw std::runtime_error("Cannot transform an empty PBRT preview bound");
        return result;
    }

    void expand_bounds(xayah::BoundingBoxBounds& result, const xayah::BoundingBoxBounds& value) {
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            if (value.minimum[axis] < result.minimum[axis]) result.minimum[axis] = value.minimum[axis];
            if (value.maximum[axis] > result.maximum[axis]) result.maximum[axis] = value.maximum[axis];
        }
    }

    const char* element_kind_label(const xayah::PbrtElementKind kind) {
        if (kind == xayah::PbrtElementKind::camera) return "Camera";
        if (kind == xayah::PbrtElementKind::shape) return "Shape";
        if (kind == xayah::PbrtElementKind::material) return "Material";
        if (kind == xayah::PbrtElementKind::texture) return "Texture";
        if (kind == xayah::PbrtElementKind::light) return "Light";
        if (kind == xayah::PbrtElementKind::medium) return "Medium";
        if (kind == xayah::PbrtElementKind::render_setting) return "Render Setting";
        if (kind == xayah::PbrtElementKind::instance) return "Instance";
        throw std::runtime_error("Unknown PBRT element kind");
    }

    const char* preview_state_label(const xayah::PbrtPreviewState state) {
        if (state == xayah::PbrtPreviewState::supported) return "Preview";
        if (state == xayah::PbrtPreviewState::unsupported) return "Unsupported";
        if (state == xayah::PbrtPreviewState::prototype) return "Prototype";
        if (state == xayah::PbrtPreviewState::none) return "None";
        throw std::runtime_error("Unknown PBRT preview state");
    }

    int resize_input_text(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
        std::string* value = static_cast<std::string*>(data->UserData);
        value->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = value->data();
        return 0;
    }

    [[nodiscard]] bool input_text_string(const char* label, std::string& value) {
        const std::size_t text_size = value.size();
        value.resize(value.capacity());
        value.data()[text_size] = '\0';
        const bool changed = ImGui::InputText(label, value.data(), value.size() + 1u, ImGuiInputTextFlags_CallbackResize, resize_input_text, &value);
        value.resize(std::strlen(value.c_str()));
        return changed;
    }

    void copy_parameters(const pbrt::ParsedParameterVector& source, std::vector<xayah::PbrtParameter>& destination) {
        destination.clear();
        destination.reserve(source.size());
        for (const pbrt::ParsedParameter* parsed : source) {
            xayah::PbrtParameter parameter{};
            parameter.type = parsed->type;
            parameter.name = parsed->name;
            parameter.floats.reserve(parsed->floats.size());
            for (const pbrt::Float value : parsed->floats) parameter.floats.push_back(static_cast<float>(value));
            parameter.ints.reserve(parsed->ints.size());
            for (const int value : parsed->ints) parameter.ints.push_back(value);
            parameter.strings = std::vector<std::string>{parsed->strings.begin(), parsed->strings.end()};
            parameter.bools.reserve(parsed->bools.size());
            for (const std::uint8_t value : parsed->bools) parameter.bools.push_back(value != 0);
            destination.push_back(std::move(parameter));
        }
    }

    pbrt::ParsedParameterVector make_parsed_parameters(const std::vector<xayah::PbrtParameter>& parameters) {
        pbrt::ParsedParameterVector result;
        for (const xayah::PbrtParameter& parameter : parameters) {
            pbrt::ParsedParameter* parsed = new pbrt::ParsedParameter({});
            parsed->type                  = parameter.type;
            parsed->name                  = parameter.name;
            for (const float value : parameter.floats) parsed->AddFloat(static_cast<pbrt::Float>(value));
            for (const int value : parameter.ints) parsed->AddInt(value);
            for (const std::string& value : parameter.strings) parsed->AddString(value);
            for (const bool value : parameter.bools) parsed->AddBool(value);
            result.push_back(parsed);
        }
        return result;
    }

    void delete_parsed_parameters(const pbrt::ParsedParameterVector& parameters) {
        for (pbrt::ParsedParameter* parameter : parameters) delete parameter;
    }

    void replay_transform_override(pbrt::BasicSceneBuilder& builder, const xayah::PbrtElement& element) {
        std::array<float, 16> row_major = spectra_matrix_to_pbrt_file_matrix(element.transform);
        builder.AttributeBegin({});
        builder.Transform(row_major.data(), {});
    }

    void replay_camera_transform_override(pbrt::BasicSceneBuilder& builder, const xayah::PbrtElement& element) {
        const std::array<float, 16> camera_from_world = xayah::inverse_affine_matrix(element.transform);
        std::array<float, 16> row_major               = spectra_matrix_to_pbrt_file_matrix(camera_from_world);
        builder.Transform(row_major.data(), {});
    }
} // namespace

namespace xayah {
    class PbrtDocumentLoader final : public pbrt::ParserTarget {
    public:
        explicit PbrtDocumentLoader(PbrtDocument& document) : document{document} {}

        void Option(const std::string& name, const std::string& value, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::option};
            command.text[0] = name;
            command.text[1] = value;
            this->append_command(std::move(command));
            this->append_element(PbrtElementKind::render_setting, name, value, {});
        }

        void Identity(pbrt::FileLoc) override {
            this->matrix_stack.back() = identity_matrix();
            this->append_command({PbrtDocument::PbrtCommandKind::identity});
        }

        void Translate(const pbrt::Float dx, const pbrt::Float dy, const pbrt::Float dz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::translate};
            command.values            = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), translation_matrix(command.values[0], command.values[1], command.values[2]));
            this->append_command(std::move(command));
        }

        void Rotate(const pbrt::Float angle, const pbrt::Float ax, const pbrt::Float ay, const pbrt::Float az, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::rotate};
            command.values            = {static_cast<float>(angle), static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(az)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), rotation_matrix(command.values[0], command.values[1], command.values[2], command.values[3]));
            this->append_command(std::move(command));
        }

        void Scale(const pbrt::Float sx, const pbrt::Float sy, const pbrt::Float sz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::scale};
            command.values            = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz)};
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), scale_matrix(command.values[0], command.values[1], command.values[2]));
            this->append_command(std::move(command));
        }

        void LookAt(const pbrt::Float ex, const pbrt::Float ey, const pbrt::Float ez, const pbrt::Float lx, const pbrt::Float ly, const pbrt::Float lz, const pbrt::Float ux, const pbrt::Float uy, const pbrt::Float uz, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::look_at};
            command.values = {static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez), static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz)};
            const std::array<float, 16> camera_to_world = camera_to_world_look_at_matrix(command.values[0], command.values[1], command.values[2], command.values[3], command.values[4], command.values[5], command.values[6], command.values[7], command.values[8]);
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), xayah::inverse_affine_matrix(camera_to_world));
            this->append_command(std::move(command));
        }

        void ConcatTransform(pbrt::Float transform[16], pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::concat_transform};
            command.values.assign(transform, transform + 16);
            this->matrix_stack.back() = multiply_preview_matrix(this->matrix_stack.back(), pbrt_file_matrix_to_spectra_matrix(command.values));
            this->append_command(std::move(command));
        }

        void Transform(pbrt::Float transform[16], pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::transform};
            command.values.assign(transform, transform + 16);
            this->matrix_stack.back() = pbrt_file_matrix_to_spectra_matrix(command.values);
            this->append_command(std::move(command));
        }

        void CoordinateSystem(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::coordinate_system};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void CoordSysTransform(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::coord_sys_transform};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void ActiveTransformAll(pbrt::FileLoc) override {
            this->append_command({PbrtDocument::PbrtCommandKind::active_transform_all});
        }

        void ActiveTransformEndTime(pbrt::FileLoc) override {
            this->append_command({PbrtDocument::PbrtCommandKind::active_transform_end_time});
        }

        void ActiveTransformStartTime(pbrt::FileLoc) override {
            this->append_command({PbrtDocument::PbrtCommandKind::active_transform_start_time});
        }

        void TransformTimes(const pbrt::Float start, const pbrt::Float end, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::transform_times};
            command.values = {static_cast<float>(start), static_cast<float>(end)};
            this->append_command(std::move(command));
        }

        void ColorSpace(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::color_space};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void PixelFilter(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::pixel_filter, PbrtElementKind::render_setting, name, {}, params);
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::film, PbrtElementKind::render_setting, "Film", type, params);
        }

        void Sampler(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::sampler, PbrtElementKind::render_setting, "Sampler", name, params);
        }

        void Accelerator(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::accelerator, PbrtElementKind::render_setting, "Accelerator", name, params);
        }

        void Integrator(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::integrator, PbrtElementKind::render_setting, "Integrator", name, params);
        }

        void Camera(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::camera};
            command.text[0] = "Camera";
            command.text[1] = name;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(PbrtElementKind::camera, "Camera", name, command.parameters);
            PbrtElement* element = this->document.find_element(element_id);
            if (element == nullptr) throw std::runtime_error("PBRT camera element was not created");
            element->transform = xayah::inverse_affine_matrix(this->matrix_stack.back());
            command.element_id = element_id;
            this->append_command(std::move(command));
        }

        void MakeNamedMedium(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::make_named_medium, PbrtElementKind::medium, name, {}, params);
        }

        void MediumInterface(const std::string& insideName, const std::string& outsideName, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::medium_interface};
            command.text[0] = insideName;
            command.text[1] = outsideName;
            this->append_command(std::move(command));
        }

        void WorldBegin(pbrt::FileLoc) override {
            this->matrix_stack.back() = identity_matrix();
            this->material_stack.back() = "";
            this->append_command({PbrtDocument::PbrtCommandKind::world_begin});
        }

        void AttributeBegin(pbrt::FileLoc) override {
            this->matrix_stack.push_back(this->matrix_stack.back());
            this->material_stack.push_back(this->material_stack.back());
            this->append_command({PbrtDocument::PbrtCommandKind::attribute_begin});
        }

        void AttributeEnd(pbrt::FileLoc) override {
            if (this->matrix_stack.size() <= 1) throw std::runtime_error("PBRT AttributeEnd without matching AttributeBegin");
            this->matrix_stack.pop_back();
            this->material_stack.pop_back();
            this->append_command({PbrtDocument::PbrtCommandKind::attribute_end});
        }

        void Attribute(const std::string& target, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::attribute};
            command.text[0] = target;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            this->append_command(std::move(command));
        }

        void Texture(const std::string& name, const std::string& type, const std::string& texname, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::texture};
            command.text[0] = name;
            command.text[1] = type;
            command.text[2] = texname;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(PbrtElementKind::texture, name, type + " " + texname, command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void Material(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->material_stack.back() = name;
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::material, PbrtElementKind::material, "inline material", name, params);
        }

        void MakeNamedMaterial(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::make_named_material, PbrtElementKind::material, name, {}, params);
        }

        void NamedMaterial(const std::string& name, pbrt::FileLoc) override {
            this->material_stack.back() = name;
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::named_material};
            command.text[0] = name;
            this->append_command(std::move(command));
        }

        void LightSource(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::light_source, PbrtElementKind::light, "Light", name, params);
        }

        void AreaLightSource(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            this->append_named_parameter_command(PbrtDocument::PbrtCommandKind::area_light_source, PbrtElementKind::light, "Area Light", name, params);
        }

        void Shape(const std::string& name, pbrt::ParsedParameterVector params, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::shape};
            command.text[0] = name;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(PbrtElementKind::shape, name, this->material_stack.back(), command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void ReverseOrientation(pbrt::FileLoc) override {
            this->append_command({PbrtDocument::PbrtCommandKind::reverse_orientation});
        }

        void ObjectBegin(const std::string& name, pbrt::FileLoc) override {
            if (!this->object_stack.empty()) throw std::runtime_error("PBRT nested ObjectBegin is not supported");
            this->matrix_stack.push_back(this->matrix_stack.back());
            this->material_stack.push_back(this->material_stack.back());
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::object_begin};
            command.text[0] = name;
            this->append_command(std::move(command));
            this->object_stack.push_back(name);
        }

        void ObjectEnd(pbrt::FileLoc) override {
            if (this->matrix_stack.size() <= 1) throw std::runtime_error("PBRT ObjectEnd without matching ObjectBegin");
            if (this->object_stack.empty()) throw std::runtime_error("PBRT ObjectEnd without active object prototype");
            this->object_stack.pop_back();
            this->matrix_stack.pop_back();
            this->material_stack.pop_back();
            this->append_command({PbrtDocument::PbrtCommandKind::object_end});
        }

        void ObjectInstance(const std::string& name, pbrt::FileLoc) override {
            PbrtDocument::PbrtCommand command{PbrtDocument::PbrtCommandKind::object_instance};
            command.text[0]                = name;
            const std::uint64_t element_id = this->append_element(PbrtElementKind::instance, name, {}, {});
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }

        void EndOfFiles() override {}

    private:
        PbrtDocument& document;
        std::vector<std::array<float, 16>> matrix_stack{identity_matrix()};
        std::vector<std::string> material_stack{""};
        std::vector<std::string> object_stack{};

        void append_command(PbrtDocument::PbrtCommand&& command) {
            this->document.commands.push_back(std::move(command));
        }

        std::uint64_t append_element(const PbrtElementKind kind, const std::string& name, const std::string& detail, const std::vector<PbrtParameter>& parameters) {
            PbrtElement element{};
            element.id            = this->document.next_element_id++;
            element.kind          = kind;
            element.type          = name;
            element.name          = name.empty() ? std::string{element_kind_label(kind)} + " " + std::to_string(element.id) : name;
            element.detail        = detail;
            element.command_index = this->document.commands.size();
            element.parameters    = parameters;
            element.transform     = this->matrix_stack.back();
            if (kind == PbrtElementKind::shape && !this->object_stack.empty()) element.prototype_name = this->object_stack.back();
            if (kind == PbrtElementKind::instance) {
                element.prototype_name = name;
                element.detail         = "object instance";
            }
            if (kind == PbrtElementKind::shape) element.local_bounds = preview_bounds_for_shape(name, parameters);
            this->document.elements.push_back(std::move(element));
            return this->document.elements.back().id;
        }

        void append_named_parameter_command(const PbrtDocument::PbrtCommandKind command_kind, const PbrtElementKind element_kind, const std::string& name, const std::string& type, const pbrt::ParsedParameterVector& params) {
            PbrtDocument::PbrtCommand command{command_kind};
            command.text[0] = name;
            command.text[1] = type;
            copy_parameters(params, command.parameters);
            delete_parsed_parameters(params);
            const std::uint64_t element_id = this->append_element(element_kind, name, type, command.parameters);
            command.element_id             = element_id;
            this->append_command(std::move(command));
        }
    };

    PbrtPreviewRenderer::PbrtPreviewRenderer() = default;

    PbrtPreviewRenderer::~PbrtPreviewRenderer() noexcept = default;

    void PbrtPreviewRenderer::create(const RenderCreateContext& context) {
        if (this->active()) throw std::runtime_error("PBRT preview renderer is already initialized");
        if (context.device == nullptr || !**context.device) throw std::runtime_error("Cannot create PBRT preview renderer without a Vulkan device");
        if (context.color_format == vk::Format::eUndefined || context.depth_format == vk::Format::eUndefined) throw std::runtime_error("Cannot create PBRT preview renderer without swapchain formats");
        if (context.frame_count == 0) throw std::runtime_error("Cannot create PBRT preview renderer without frames in flight");

        const vk::Format stencil_format = static_cast<bool>(context.depth_aspect & vk::ImageAspectFlagBits::eStencil) ? context.depth_format : vk::Format::eUndefined;
        vk::PipelineRenderingCreateInfo rendering_create_info{};
        rendering_create_info.colorAttachmentCount    = 1;
        rendering_create_info.pColorAttachmentFormats = &context.color_format;
        rendering_create_info.depthAttachmentFormat   = context.depth_format;
        rendering_create_info.stencilAttachmentFormat = stencil_format;
        vk::PipelineViewportStateCreateInfo viewport_state{};
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount  = 1;
        constexpr vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};

        {
            const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "pbrt_preview.vert.spv");
            const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "pbrt_preview.frag.spv");
            const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
            const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
            const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
            const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };

            constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, sizeof(PbrtPreviewSurfaceShaderParameters)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
            this->surface_pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

            constexpr vk::VertexInputBindingDescription vertex_binding{0, sizeof(PbrtPreviewVertex), vk::VertexInputRate::eVertex};
            constexpr std::array vertex_attributes{
                vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
                vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 3>))},
                vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(sizeof(std::array<float, 3>) * 2u)},
            };
            const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
            constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
            constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.blendEnable    = VK_FALSE;
            color_blend_attachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.pNext               = &rendering_create_info;
            pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
            pipeline_create_info.pStages             = shader_stages.data();
            pipeline_create_info.pVertexInputState   = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState      = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState   = &multisample_state;
            pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
            pipeline_create_info.pColorBlendState    = &color_blend_state;
            pipeline_create_info.pDynamicState       = &dynamic_state;
            pipeline_create_info.layout              = *this->surface_pipeline_layout;
            this->surface_pipeline                   = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
        }

        {
            const std::vector<std::uint32_t> vertex_code   = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.vert.spv");
            const std::vector<std::uint32_t> fragment_code = read_spirv(std::filesystem::path{SPECTRA_SHADER_DIR} / "bounding_box.frag.spv");
            const vk::ShaderModuleCreateInfo vertex_module_create_info{{}, vertex_code.size() * sizeof(std::uint32_t), vertex_code.data()};
            const vk::ShaderModuleCreateInfo fragment_module_create_info{{}, fragment_code.size() * sizeof(std::uint32_t), fragment_code.data()};
            const vk::raii::ShaderModule vertex_shader{*context.device, vertex_module_create_info};
            const vk::raii::ShaderModule fragment_shader{*context.device, fragment_module_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };

            constexpr vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex, 0, sizeof(PbrtPreviewOverlayShaderParameters)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 0, nullptr, 1, &push_constant_range};
            this->overlay_pipeline_layout = vk::raii::PipelineLayout{*context.device, pipeline_layout_create_info};

            constexpr vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
            constexpr vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eLineList, VK_FALSE};
            constexpr vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
            constexpr vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
            vk::PipelineColorBlendAttachmentState color_blend_attachment{};
            color_blend_attachment.blendEnable         = VK_TRUE;
            color_blend_attachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
            color_blend_attachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            color_blend_attachment.colorBlendOp        = vk::BlendOp::eAdd;
            color_blend_attachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
            color_blend_attachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
            color_blend_attachment.alphaBlendOp        = vk::BlendOp::eAdd;
            color_blend_attachment.colorWriteMask      = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
            const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1, &color_blend_attachment};
            vk::GraphicsPipelineCreateInfo pipeline_create_info{};
            pipeline_create_info.pNext               = &rendering_create_info;
            pipeline_create_info.stageCount          = static_cast<std::uint32_t>(shader_stages.size());
            pipeline_create_info.pStages             = shader_stages.data();
            pipeline_create_info.pVertexInputState   = &vertex_input_state;
            pipeline_create_info.pInputAssemblyState = &input_assembly_state;
            pipeline_create_info.pViewportState      = &viewport_state;
            pipeline_create_info.pRasterizationState = &rasterization_state;
            pipeline_create_info.pMultisampleState   = &multisample_state;
            pipeline_create_info.pDepthStencilState  = &depth_stencil_state;
            pipeline_create_info.pColorBlendState    = &color_blend_state;
            pipeline_create_info.pDynamicState       = &dynamic_state;
            pipeline_create_info.layout              = *this->overlay_pipeline_layout;
            this->overlay_pipeline                   = vk::raii::Pipeline{*context.device, nullptr, pipeline_create_info};
        }

        this->frame_resources.resize(context.frame_count);
    }

    void PbrtPreviewRenderer::destroy() noexcept {
        this->frame_resources.clear();
        this->overlay_pipeline        = nullptr;
        this->overlay_pipeline_layout = nullptr;
        this->surface_pipeline        = nullptr;
        this->surface_pipeline_layout = nullptr;
    }

    void PbrtPreviewRenderer::render(const RenderFrameContext& context, const std::span<const PbrtPreviewVertex> vertices, const std::span<const PbrtPreviewOverlay> overlays) {
        if (context.physical_device == nullptr || context.device == nullptr || context.command_buffer == nullptr) throw std::runtime_error("PBRT preview render context is incomplete");
        if (context.frame_index >= context.frame_count || context.frame_count != this->frame_resources.size()) throw std::runtime_error("PBRT preview frame index is outside resource range");
        if (!*this->surface_pipeline_layout || !*this->surface_pipeline || !*this->overlay_pipeline_layout || !*this->overlay_pipeline) throw std::runtime_error("PBRT preview renderer is not initialized");
        if (!vertices.empty()) {
            if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("PBRT preview vertex count exceeds Vulkan draw limit");
            constexpr vk::MemoryPropertyFlags upload_memory_properties = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
            FrameResources& resources                                  = this->frame_resources.at(context.frame_index);
            ensure_buffer(*context.physical_device, *context.device, resources.vertex_buffer, resources.vertex_memory, resources.vertex_size, vertices.size() * sizeof(PbrtPreviewVertex), vk::BufferUsageFlagBits::eVertexBuffer, upload_memory_properties);
            write_buffer(resources.vertex_memory, resources.vertex_size, vertices.data(), vertices.size() * sizeof(PbrtPreviewVertex));

            PbrtPreviewSurfaceShaderParameters parameters{};
            parameters.view_projection = context.view_projection;
            parameters.light_direction = {-0.45f, -0.85f, -0.25f, 0.0f};
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->surface_pipeline);
            context.command_buffer->pushConstants(*this->surface_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0, vk::ArrayProxy<const PbrtPreviewSurfaceShaderParameters>{1, &parameters});
            const std::array vertex_buffers{static_cast<vk::Buffer>(*resources.vertex_buffer)};
            constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
            context.command_buffer->bindVertexBuffers(0, vertex_buffers, vertex_offsets);
            context.command_buffer->draw(static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
        }

        if (!overlays.empty()) {
            context.command_buffer->bindPipeline(vk::PipelineBindPoint::eGraphics, *this->overlay_pipeline);
            for (const PbrtPreviewOverlay& overlay : overlays) {
                PbrtPreviewOverlayShaderParameters parameters{};
                parameters.model_view_projection = multiply_matrix(context.view_projection, overlay.transform);
                parameters.bounds_min            = {overlay.bounds.minimum[0], overlay.bounds.minimum[1], overlay.bounds.minimum[2], 1.0f};
                parameters.bounds_max            = {overlay.bounds.maximum[0], overlay.bounds.maximum[1], overlay.bounds.maximum[2], 1.0f};
                parameters.color                 = overlay.color;
                context.command_buffer->pushConstants(*this->overlay_pipeline_layout, vk::ShaderStageFlagBits::eVertex, 0, vk::ArrayProxy<const PbrtPreviewOverlayShaderParameters>{1, &parameters});
                context.command_buffer->draw(24, 1, 0, 0);
            }
        }
    }

    bool PbrtPreviewRenderer::active() const {
        return static_cast<bool>(*this->surface_pipeline) && static_cast<bool>(*this->overlay_pipeline);
    }

    PbrtDocument::PbrtDocument() = default;

    PbrtDocument::~PbrtDocument() noexcept = default;

    void PbrtDocument::load(const std::filesystem::path& path) {
        if (path.empty()) throw std::runtime_error("PBRT document path must not be empty");
        if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"PBRT document does not exist: "} + path.string());
        this->source_path       = std::filesystem::absolute(path);
        this->elements          = {};
        this->commands          = {};
        this->preview_meshes    = {};
        this->preview_instances = {};
        this->selection         = {};
        this->next_element_id   = 1;
        PbrtDocumentLoader loader{*this};
        const std::string filename = this->source_path.string();
        std::array<std::string, 1> files{filename};
        pbrt::ParseFiles(&loader, pstd::MakeConstSpan(files.data(), files.size()));
        this->rebuild_preview_cache();
        this->dirty = false;
        this->validate();
    }

    void PbrtDocument::create_default() {
        this->source_path                   = std::filesystem::path{};
        this->elements                      = {};
        this->commands                      = {};
        this->preview_meshes                = {};
        this->preview_instances             = {};
        this->selection                     = {};
        this->next_element_id               = 1;
        constexpr const char* default_scene = R"(
Film "rgb" "integer xresolution" [1280] "integer yresolution" [720] "string filename" "render-output.exr"
Sampler "zsobol" "integer pixelsamples" [64]
Integrator "volpath" "integer maxdepth" [5]
LookAt 4 3 5 0 0 0 0 1 0
Camera "perspective" "float fov" [45]
WorldBegin
LightSource "distant" "rgb L" [3 3 3] "point3 from" [0 4 4] "point3 to" [0 0 0]
MakeNamedMaterial "matte_gray" "string type" "diffuse" "rgb reflectance" [0.72 0.72 0.72]
NamedMaterial "matte_gray"
Shape "sphere" "float radius" [1]
)";
        PbrtDocumentLoader loader{*this};
        pbrt::ParseString(&loader, default_scene);
        this->rebuild_preview_cache();
        this->dirty = false;
        this->validate();
    }

    void PbrtDocument::validate() const {
        if (this->commands.empty()) throw std::runtime_error("PBRT document has no commands");
        if (this->elements.empty()) throw std::runtime_error("PBRT document has no editable elements");
        std::set<std::uint64_t> ids{};
        for (const PbrtElement& element : this->elements) {
            if (element.id == 0) throw std::runtime_error("PBRT element id must not be zero");
            if (!ids.insert(element.id).second) throw std::runtime_error(std::string{"Duplicate PBRT element id: "} + std::to_string(element.id));
        }
        if (this->selection.element_id != 0 && this->find_element(this->selection.element_id) == nullptr) throw std::runtime_error("PBRT selection points to a missing element");
    }

    void PbrtDocument::mark_document_dirty() {
        this->dirty = true;
    }

    void PbrtDocument::mark_transform_edited(PbrtElement& element) {
        element.transform_override = true;
        this->mark_document_dirty();
    }

    void PbrtDocument::mark_parameters_edited(PbrtElement& element) {
        if (element.kind == PbrtElementKind::shape) this->rebuild_preview_cache();
        this->mark_document_dirty();
    }

    void PbrtDocument::rebuild_preview_cache() {
        this->preview_meshes.clear();
        this->preview_instances.clear();

        std::map<std::uint64_t, std::size_t> mesh_indices{};
        std::map<std::string, std::vector<std::uint64_t>> prototype_shapes{};

        for (PbrtElement& element : this->elements) {
            element.preview_state          = PbrtPreviewState::none;
            element.preview_message        = {};
            element.preview_triangle_count = 0;
            if (element.kind != PbrtElementKind::shape) continue;

            PreviewMeshBuildResult build = build_preview_mesh_for_shape(this->source_path, element.type, element.parameters);
            element.local_bounds         = build.local_bounds;
            element.preview_message      = build.message;
            if (!element.prototype_name.empty()) prototype_shapes[element.prototype_name].push_back(element.id);

            if (build.supported) {
                PbrtPreviewMesh mesh{};
                mesh.source_element_id         = element.id;
                mesh.vertices                  = std::move(build.vertices);
                mesh.local_bounds              = build.local_bounds;
                mesh_indices[element.id]       = this->preview_meshes.size();
                element.preview_triangle_count = mesh.vertices.size() / 3u;
                element.preview_state          = element.prototype_name.empty() ? PbrtPreviewState::supported : PbrtPreviewState::prototype;
                this->preview_meshes.push_back(std::move(mesh));
            } else {
                element.preview_state = PbrtPreviewState::unsupported;
            }

            if (element.prototype_name.empty()) {
                PbrtPreviewInstance instance{};
                instance.element_id        = element.id;
                instance.source_element_id = element.id;
                instance.unsupported       = !build.supported;
                instance.local_bounds      = element.local_bounds;
                if (build.supported) instance.mesh_index = mesh_indices.at(element.id);
                this->preview_instances.push_back(instance);
            }
        }

        for (PbrtElement& element : this->elements) {
            if (element.kind != PbrtElementKind::instance) continue;
            const auto prototype_iterator = prototype_shapes.find(element.prototype_name);
            if (prototype_iterator == prototype_shapes.end()) throw std::runtime_error(std::string{"PBRT ObjectInstance references unknown prototype: "} + element.prototype_name);

            bool has_supported_preview{};
            bool has_unsupported_preview{};
            std::size_t triangle_count{};
            for (const std::uint64_t source_id : prototype_iterator->second) {
                PbrtElement* source = this->find_element(source_id);
                if (source == nullptr) throw std::runtime_error("PBRT prototype source element is missing");
                PbrtPreviewInstance instance{};
                instance.element_id        = element.id;
                instance.source_element_id = source->id;
                instance.local_bounds      = source->local_bounds;
                const auto mesh_iterator   = mesh_indices.find(source->id);
                if (mesh_iterator == mesh_indices.end()) {
                    instance.unsupported    = true;
                    has_unsupported_preview = true;
                } else {
                    instance.mesh_index = mesh_iterator->second;
                    triangle_count += source->preview_triangle_count;
                    has_supported_preview = true;
                }
                this->preview_instances.push_back(instance);
            }

            element.preview_triangle_count = triangle_count;
            if (has_supported_preview && !has_unsupported_preview) {
                element.preview_state   = PbrtPreviewState::supported;
                element.preview_message = "Object instance preview ready";
            } else if (has_supported_preview && has_unsupported_preview) {
                element.preview_state   = PbrtPreviewState::unsupported;
                element.preview_message = "Object instance has partial unsupported preview";
            } else {
                element.preview_state   = PbrtPreviewState::unsupported;
                element.preview_message = "Object instance has no supported preview geometry";
            }
        }
    }

    void PbrtDocument::create_render_resources(const RenderCreateContext& context) {
        this->preview_renderer.create(context);
    }

    void PbrtDocument::destroy_render_resources() noexcept {
        this->preview_renderer.destroy();
    }

    void PbrtDocument::recreate_render_resources(const RenderCreateContext& context) {
        this->destroy_render_resources();
        this->create_render_resources(context);
    }

    void PbrtDocument::render(const RenderFrameContext& context) {
        constexpr std::array<float, 3> shape_surface_color{0.72f, 0.78f, 0.90f};
        constexpr std::array<float, 3> selected_surface_color{1.0f, 0.76f, 0.30f};
        constexpr std::array<float, 4> selected_color{1.0f, 0.76f, 0.30f, 0.98f};
        constexpr std::array<float, 4> light_color{1.0f, 0.88f, 0.45f, 0.92f};
        constexpr std::array<float, 4> unsupported_color{1.0f, 0.18f, 0.12f, 0.96f};
        std::vector<PbrtPreviewVertex> vertices{};
        std::vector<PbrtPreviewOverlay> overlays{};
        std::size_t vertex_count{};
        for (const PbrtPreviewInstance& instance : this->preview_instances) {
            const PbrtElement* element = this->find_element(instance.element_id);
            const PbrtElement* source  = this->find_element(instance.source_element_id);
            if (element == nullptr || source == nullptr) throw std::runtime_error("PBRT preview instance references a missing element");
            if (!element->visible || !source->visible) continue;
            if (instance.unsupported) continue;
            vertex_count += this->preview_meshes.at(instance.mesh_index).vertices.size();
        }
        vertices.reserve(vertex_count);
        overlays.reserve(this->preview_instances.size() + this->elements.size());

        for (const PbrtPreviewInstance& instance : this->preview_instances) {
            const PbrtElement* element = this->find_element(instance.element_id);
            const PbrtElement* source  = this->find_element(instance.source_element_id);
            if (element == nullptr || source == nullptr) throw std::runtime_error("PBRT preview instance references a missing element");
            if (!element->visible || !source->visible) continue;
            const std::array<float, 16> transform = this->preview_instance_transform(instance);
            const bool selected                   = this->selection.element_id == instance.element_id || this->selection.element_id == instance.source_element_id;
            if (instance.unsupported) {
                overlays.push_back({transform, instance.local_bounds, unsupported_color});
                if (selected) overlays.push_back({transform, instance.local_bounds, selected_color});
                continue;
            }
            const std::array<float, 3> color = selected ? selected_surface_color : shape_surface_color;
            const PbrtPreviewMesh& mesh      = this->preview_meshes.at(instance.mesh_index);
            for (const PbrtPreviewVertex& vertex : mesh.vertices) {
                vertices.push_back({
                    transform_point(transform, vertex.position),
                    normalize_vector(transform_direction(transform, vertex.normal)),
                    color,
                });
            }
            if (selected) overlays.push_back({transform, instance.local_bounds, selected_color});
        }

        for (const PbrtElement& element : this->elements) {
            if (!element.visible) continue;
            if (element.kind != PbrtElementKind::light) continue;
            std::array<float, 4> color = light_color;
            if (this->selection.element_id == element.id) color = selected_color;
            overlays.push_back({element.transform, element.local_bounds, color});
        }
        this->preview_renderer.render(context, vertices, overlays);
    }

    PbrtRenderResult PbrtDocument::render_final(const PbrtRenderSettings& settings) {
        if (settings.samples_per_pixel <= 0) throw std::runtime_error("PBRT samples per pixel must be positive");
        if (settings.thread_count <= 0) throw std::runtime_error("PBRT thread count must be positive");
        if (settings.resolution[0] <= 0 || settings.resolution[1] <= 0) throw std::runtime_error("PBRT render resolution must be positive");
        const std::string output_path = settings.output_path.data();
        if (output_path.empty()) throw std::runtime_error("PBRT output path must not be empty");

        pbrt::PBRTOptions options{};
        options.nThreads       = settings.thread_count;
        options.pixelSamples   = settings.samples_per_pixel;
        options.imageFile      = output_path;
        options.renderingSpace = pbrt::RenderingCoordinateSystem::CameraWorld;
        options.quiet          = true;
        options.useGPU         = settings.backend == PbrtPathTraceBackend::gpu;
        options.wavefront      = settings.backend == PbrtPathTraceBackend::wavefront;

        const auto started = std::chrono::steady_clock::now();
        bool initialized{};
        try {
            pbrt::InitPBRT(options);
            initialized = true;
            pbrt::BasicScene scene;
            pbrt::BasicSceneBuilder builder{&scene};
            for (const PbrtCommand& command : this->commands) {
                PbrtElement* element                                = command.element_id == 0 ? nullptr : this->find_element(command.element_id);
                const bool skip_invisible_element                   = element != nullptr && !element->visible && (command.kind == PbrtCommandKind::shape || command.kind == PbrtCommandKind::light_source || command.kind == PbrtCommandKind::area_light_source || command.kind == PbrtCommandKind::object_instance);
                if (skip_invisible_element) continue;
                const std::vector<PbrtParameter>& replay_parameters = element == nullptr ? command.parameters : element->parameters;
                const bool world_transform_override                 = element != nullptr && element->transform_override && (command.kind == PbrtCommandKind::shape || command.kind == PbrtCommandKind::light_source || command.kind == PbrtCommandKind::object_instance);
                const bool camera_transform_override                = element != nullptr && element->transform_override && command.kind == PbrtCommandKind::camera;
                if (camera_transform_override) replay_camera_transform_override(builder, *element);
                if (world_transform_override) replay_transform_override(builder, *element);
                switch (command.kind) {
                case PbrtCommandKind::option: builder.Option(command.text[0], command.text[1], {}); break;
                case PbrtCommandKind::identity: builder.Identity({}); break;
                case PbrtCommandKind::translate: builder.Translate(command.values[0], command.values[1], command.values[2], {}); break;
                case PbrtCommandKind::rotate: builder.Rotate(command.values[0], command.values[1], command.values[2], command.values[3], {}); break;
                case PbrtCommandKind::scale: builder.Scale(command.values[0], command.values[1], command.values[2], {}); break;
                case PbrtCommandKind::look_at: builder.LookAt(command.values[0], command.values[1], command.values[2], command.values[3], command.values[4], command.values[5], command.values[6], command.values[7], command.values[8], {}); break;
                case PbrtCommandKind::concat_transform:
                    {
                        std::array<pbrt::Float, 16> values{};
                        for (std::size_t index = 0; index < values.size(); ++index) values[index] = command.values[index];
                        builder.ConcatTransform(values.data(), {});
                        break;
                    }
                case PbrtCommandKind::transform:
                    {
                        std::array<pbrt::Float, 16> values{};
                        for (std::size_t index = 0; index < values.size(); ++index) values[index] = command.values[index];
                        builder.Transform(values.data(), {});
                        break;
                    }
                case PbrtCommandKind::coordinate_system: builder.CoordinateSystem(command.text[0], {}); break;
                case PbrtCommandKind::coord_sys_transform: builder.CoordSysTransform(command.text[0], {}); break;
                case PbrtCommandKind::active_transform_all: builder.ActiveTransformAll({}); break;
                case PbrtCommandKind::active_transform_end_time: builder.ActiveTransformEndTime({}); break;
                case PbrtCommandKind::active_transform_start_time: builder.ActiveTransformStartTime({}); break;
                case PbrtCommandKind::transform_times: builder.TransformTimes(command.values[0], command.values[1], {}); break;
                case PbrtCommandKind::color_space: builder.ColorSpace(command.text[0], {}); break;
                case PbrtCommandKind::pixel_filter: builder.PixelFilter(command.text[0], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::film: builder.Film(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::sampler: builder.Sampler(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::accelerator: builder.Accelerator(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::integrator: builder.Integrator(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::camera: builder.Camera(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::make_named_medium: builder.MakeNamedMedium(command.text[0], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::medium_interface: builder.MediumInterface(command.text[0], command.text[1], {}); break;
                case PbrtCommandKind::world_begin: builder.WorldBegin({}); break;
                case PbrtCommandKind::attribute_begin: builder.AttributeBegin({}); break;
                case PbrtCommandKind::attribute_end: builder.AttributeEnd({}); break;
                case PbrtCommandKind::attribute: builder.Attribute(command.text[0], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::texture: builder.Texture(command.text[0], command.text[1], command.text[2], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::material: builder.Material(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::make_named_material: builder.MakeNamedMaterial(command.text[0], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::named_material: builder.NamedMaterial(command.text[0], {}); break;
                case PbrtCommandKind::light_source: builder.LightSource(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::area_light_source: builder.AreaLightSource(command.text[1], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::reverse_orientation: builder.ReverseOrientation({}); break;
                case PbrtCommandKind::shape: builder.Shape(command.text[0], make_parsed_parameters(replay_parameters), {}); break;
                case PbrtCommandKind::object_begin: builder.ObjectBegin(command.text[0], {}); break;
                case PbrtCommandKind::object_end: builder.ObjectEnd({}); break;
                case PbrtCommandKind::object_instance: builder.ObjectInstance(command.text[0], {}); break;
                }
                if (world_transform_override) builder.AttributeEnd({});
            }
            builder.EndOfFiles();
            if (options.useGPU || options.wavefront)
                pbrt::RenderWavefront(scene);
            else
                pbrt::RenderCPU(scene);
            pbrt::CleanupPBRT();
        } catch (...) {
            if (initialized) pbrt::CleanupPBRT();
            throw;
        }
        const auto finished = std::chrono::steady_clock::now();
        PbrtRenderResult result{};
        result.success     = true;
        result.seconds     = std::chrono::duration<double>(finished - started).count();
        result.output_path = output_path;
        result.message     = "Render complete";
        return result;
    }

    const std::filesystem::path& PbrtDocument::path() const {
        return this->source_path;
    }

    std::size_t PbrtDocument::element_count() const {
        return this->elements.size();
    }

    std::size_t PbrtDocument::object_count() const {
        return this->preview_instances.size();
    }

    PbrtDocumentStats PbrtDocument::stats() const {
        PbrtDocumentStats result{};
        result.commands = this->commands.size();
        for (const PbrtElement& element : this->elements) {
            if (element.kind == PbrtElementKind::camera) ++result.cameras;
            if (element.kind == PbrtElementKind::shape) ++result.shapes;
            if (element.kind == PbrtElementKind::material) ++result.materials;
            if (element.kind == PbrtElementKind::texture) ++result.textures;
            if (element.kind == PbrtElementKind::light) ++result.lights;
            if (element.kind == PbrtElementKind::medium) ++result.media;
            if (element.kind == PbrtElementKind::instance) ++result.instances;
            if (element.kind == PbrtElementKind::render_setting) ++result.render_settings;
            if (element.preview_state == PbrtPreviewState::unsupported) ++result.unsupported_preview;
        }
        result.preview_instances = this->preview_instances.size();
        for (const PbrtPreviewInstance& instance : this->preview_instances) {
            if (instance.unsupported) continue;
            result.preview_triangles += this->preview_meshes.at(instance.mesh_index).vertices.size() / 3u;
        }
        return result;
    }

    bool PbrtDocument::has_selection() const {
        return this->selection.element_id != 0;
    }

    void PbrtDocument::clear_selection() {
        this->selection.element_id = 0;
    }

    void PbrtDocument::select_element(const std::uint64_t element_id) {
        if (this->find_element(element_id) == nullptr) throw std::runtime_error("Cannot select a missing PBRT element");
        this->selection.element_id = element_id;
    }

    PbrtElement& PbrtDocument::selected_element() {
        PbrtElement* element = this->find_element(this->selection.element_id);
        if (element == nullptr) throw std::runtime_error("PBRT selection is empty or invalid");
        return *element;
    }

    const PbrtElement& PbrtDocument::selected_element() const {
        const PbrtElement* element = this->find_element(this->selection.element_id);
        if (element == nullptr) throw std::runtime_error("PBRT selection is empty or invalid");
        return *element;
    }

    std::vector<std::uint64_t> PbrtDocument::element_ids(const PbrtElementKind kind) const {
        std::vector<std::uint64_t> ids{};
        ids.reserve(this->elements.size());
        for (const PbrtElement& element : this->elements) {
            if (element.kind == kind) ids.push_back(element.id);
        }
        return ids;
    }

    PbrtElement& PbrtDocument::element_by_id(const std::uint64_t element_id) {
        PbrtElement* element = this->find_element(element_id);
        if (element == nullptr) throw std::runtime_error("PBRT element id does not exist");
        return *element;
    }

    const PbrtElement& PbrtDocument::element_by_id(const std::uint64_t element_id) const {
        const PbrtElement* element = this->find_element(element_id);
        if (element == nullptr) throw std::runtime_error("PBRT element id does not exist");
        return *element;
    }

    BoundingBoxBounds PbrtDocument::world_bounds() const {
        bool initialized{};
        BoundingBoxBounds result{};
        for (const PbrtPreviewInstance& instance : this->preview_instances) {
            const PbrtElement* element = this->find_element(instance.element_id);
            const PbrtElement* source  = this->find_element(instance.source_element_id);
            if (element == nullptr || source == nullptr) throw std::runtime_error("PBRT preview instance references a missing element");
            if (!element->visible || !source->visible) continue;
            const BoundingBoxBounds bounds = this->preview_instance_world_bounds(instance);
            if (!initialized) {
                result      = bounds;
                initialized = true;
            } else {
                expand_bounds(result, bounds);
            }
        }
        if (!initialized) throw std::runtime_error("PBRT document has no previewable bounds");
        return result;
    }

    BoundingBoxBounds PbrtDocument::selected_world_bounds() const {
        const PbrtElement& element = this->selected_element();
        bool initialized{};
        BoundingBoxBounds result{};
        for (const PbrtPreviewInstance& instance : this->preview_instances) {
            if (instance.element_id != element.id && instance.source_element_id != element.id) continue;
            const BoundingBoxBounds bounds = this->preview_instance_world_bounds(instance);
            if (!initialized) {
                result      = bounds;
                initialized = true;
            } else {
                expand_bounds(result, bounds);
            }
        }
        if (initialized) return result;
        if (element.kind == PbrtElementKind::light) return transformed_bounds(element.transform, element.local_bounds);
        throw std::runtime_error("Selected PBRT element has no previewable bounds");
    }

    void PbrtDocument::mark_element_transform_edited(const std::uint64_t element_id) {
        this->mark_transform_edited(this->element_by_id(element_id));
    }

    void PbrtDocument::mark_element_parameters_edited(const std::uint64_t element_id) {
        this->mark_parameters_edited(this->element_by_id(element_id));
    }

    void PbrtDocument::mark_element_visibility_edited(const std::uint64_t element_id) {
        static_cast<void>(this->element_by_id(element_id));
        this->mark_document_dirty();
    }

    std::array<float, 16> PbrtDocument::preview_instance_transform(const PbrtPreviewInstance& instance) const {
        const PbrtElement* element = this->find_element(instance.element_id);
        const PbrtElement* source  = this->find_element(instance.source_element_id);
        if (element == nullptr || source == nullptr) throw std::runtime_error("PBRT preview instance references a missing element");
        if (element->id == source->id) return element->transform;
        return multiply_preview_matrix(element->transform, source->transform);
    }

    BoundingBoxBounds PbrtDocument::preview_instance_world_bounds(const PbrtPreviewInstance& instance) const {
        return transformed_bounds(this->preview_instance_transform(instance), instance.local_bounds);
    }

    void PbrtDocument::draw_scene_browser_ui() {
        const PbrtDocumentStats document_stats = this->stats();
        if (ImGui::BeginTable("PbrtSceneSummary", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            const auto row = [](const char* label, const std::size_t value) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(label);
                ImGui::TableNextColumn();
                ImGui::Text("%zu", value);
            };
            row("Elements", this->elements.size());
            row("Shapes", document_stats.shapes);
            row("Materials", document_stats.materials);
            row("Textures", document_stats.textures);
            row("Lights", document_stats.lights);
            row("Media", document_stats.media);
            row("Preview Instances", document_stats.preview_instances);
            row("Preview Triangles", document_stats.preview_triangles);
            row("Unsupported Preview", document_stats.unsupported_preview);
            row("Commands", document_stats.commands);
            ImGui::EndTable();
        }

        if (!this->source_path.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", this->source_path.string().c_str());
        }
        ImGui::Text("Document: %s", this->dirty ? "Modified" : "Clean");

        ImGui::Separator();
        if (ImGui::BeginTabBar("PbrtSceneBrowserTabs")) {
            if (ImGui::BeginTabItem("Scene List")) {
                for (const PbrtElementKind kind : {PbrtElementKind::camera, PbrtElementKind::shape, PbrtElementKind::material, PbrtElementKind::texture, PbrtElementKind::light, PbrtElementKind::medium, PbrtElementKind::instance, PbrtElementKind::render_setting}) {
                    if (!ImGui::CollapsingHeader(element_kind_label(kind))) continue;
                    for (PbrtElement& element : this->elements) {
                        if (element.kind != kind) continue;
                        const bool selected     = this->selection.element_id == element.id;
                        const std::string id    = std::to_string(element.id);
                        const std::string label = std::format("{}  {}  {}  [{}]##PbrtElement{}", element_kind_label(element.kind), element.name, element.detail, preview_state_label(element.preview_state), id);
                        if (ImGui::Selectable(label.c_str(), selected)) this->selection.element_id = element.id;
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                ImGui::Text("Dirty: %s", this->dirty ? "yes" : "no");
                ImGui::Text("Next id: %llu", static_cast<unsigned long long>(this->next_element_id));
                ImGui::Text("Preview meshes: %zu", this->preview_meshes.size());
                ImGui::Text("Preview instances: %zu", this->preview_instances.size());
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void PbrtDocument::draw_selected_inspector_ui(const bool editing_enabled) {
        if (!this->has_selection()) {
            ImGui::TextDisabled("No selection");
            return;
        }
        const PbrtElement& element = this->selected_element();
        ImGui::Text("%s: %s", element_kind_label(element.kind), element.name.c_str());
        if (!element.detail.empty()) ImGui::TextDisabled("%s", element.detail.c_str());
        ImGui::Text("Preview: %s", preview_state_label(element.preview_state));
        if (!element.prototype_name.empty()) ImGui::TextDisabled("Prototype: %s", element.prototype_name.c_str());
        if (element.preview_triangle_count != 0) ImGui::TextDisabled("Triangles: %zu", element.preview_triangle_count);
        if (!element.preview_message.empty()) ImGui::TextDisabled("%s", element.preview_message.c_str());
        ImGui::Separator();
        if (!editing_enabled) ImGui::TextDisabled("Editing is locked while PBRT final render is running");

        if (element.kind == PbrtElementKind::shape || (element.kind == PbrtElementKind::light && element.type == "Light") || element.kind == PbrtElementKind::instance || element.kind == PbrtElementKind::camera) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) this->draw_element_transform_ui(element.id, editing_enabled);
        }
        if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) this->draw_element_parameters_ui(element.id, editing_enabled);
    }

    void PbrtDocument::draw_element_transform_ui(const std::uint64_t element_id, const bool editing_enabled) {
        PbrtElement& element = this->element_by_id(element_id);
        if (element.kind != PbrtElementKind::shape && element.kind != PbrtElementKind::instance && element.kind != PbrtElementKind::camera && (element.kind != PbrtElementKind::light || element.type != "Light")) {
            ImGui::TextDisabled("No editable transform");
            return;
        }
        ImGui::PushID(static_cast<int>(element.id));
        ImGui::BeginDisabled(!editing_enabled);
        std::array<float, 3> translation{element.transform[12], element.transform[13], element.transform[14]};
        if (ImGui::InputFloat3("Translation", translation.data(), "%.3f")) {
            element.transform[12] = translation[0];
            element.transform[13] = translation[1];
            element.transform[14] = translation[2];
            this->mark_transform_edited(element);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    void PbrtDocument::draw_element_parameters_ui(const std::uint64_t element_id, const bool editing_enabled) {
        PbrtElement& element = this->element_by_id(element_id);
        if (element.parameters.empty()) {
            ImGui::TextDisabled("No parameters");
            return;
        }
        ImGui::PushID(static_cast<int>(element.id));
        ImGui::BeginDisabled(!editing_enabled);
        if (ImGui::BeginTable("PbrtParameterTable", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 160.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            for (PbrtParameter& parameter : element.parameters) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(parameter.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", parameter.type.c_str());
                ImGui::TableNextColumn();
                ImGui::PushID(&parameter);
                bool changed = false;
                for (std::size_t index = 0; index < parameter.floats.size(); ++index) {
                    ImGui::SetNextItemWidth(-1.0f);
                    changed |= ImGui::InputFloat(std::format("##float{}", index).c_str(), &parameter.floats[index], 0.0f, 0.0f, "%.6f");
                }
                for (std::size_t index = 0; index < parameter.ints.size(); ++index) {
                    ImGui::SetNextItemWidth(-1.0f);
                    changed |= ImGui::InputInt(std::format("##int{}", index).c_str(), &parameter.ints[index]);
                }
                for (std::size_t index = 0; index < parameter.strings.size(); ++index) {
                    ImGui::SetNextItemWidth(-1.0f);
                    changed |= input_text_string(std::format("##string{}", index).c_str(), parameter.strings[index]);
                }
                for (std::size_t index = 0; index < parameter.bools.size(); ++index) {
                    bool value = parameter.bools[index];
                    if (ImGui::Checkbox(std::format("##bool{}", index).c_str(), &value)) {
                        parameter.bools[index] = value;
                        changed                = true;
                    }
                }
                if (parameter.floats.empty() && parameter.ints.empty() && parameter.strings.empty() && parameter.bools.empty()) ImGui::TextDisabled("Empty");
                if (changed) this->mark_parameters_edited(element);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    PbrtElement* PbrtDocument::find_element(const std::uint64_t element_id) {
        const auto iterator = std::ranges::find(this->elements, element_id, &PbrtElement::id);
        if (iterator == this->elements.end()) return nullptr;
        return &*iterator;
    }

    const PbrtElement* PbrtDocument::find_element(const std::uint64_t element_id) const {
        const auto iterator = std::ranges::find(this->elements, element_id, &PbrtElement::id);
        if (iterator == this->elements.end()) return nullptr;
        return &*iterator;
    }
} // namespace xayah
