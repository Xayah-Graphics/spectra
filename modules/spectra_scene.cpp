module;
#include <pbrt/options.h>
#include <pbrt/parser.h>
#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/util/mesh.h>
#include <pbrt/util/transform.h>

module spectra;
import std;
import :runtime;

namespace {
    constexpr std::uint32_t start_transform_bit{1u << 0u};
    constexpr std::uint32_t end_transform_bit{1u << 1u};
    constexpr std::uint32_t all_transform_bits{start_transform_bit | end_transform_bit};

    [[nodiscard]] std::array<float, 16> identity_matrix_array() {
        return {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
    }

    [[nodiscard]] xayah::SpectraPbrtFileLocation copy_file_location(const pbrt::FileLoc& location) {
        return {std::string{location.filename}, location.line, location.column};
    }

    [[nodiscard]] std::vector<xayah::SpectraPbrtParameter> copy_parameters(const pbrt::ParsedParameterVector& parameters) {
        std::vector<xayah::SpectraPbrtParameter> copied_parameters{};
        copied_parameters.reserve(parameters.size());
        for (const pbrt::ParsedParameter* parameter : parameters) {
            if (parameter == nullptr) throw std::runtime_error("PBRT parser produced a null parameter");
            xayah::SpectraPbrtParameter copied_parameter{};
            copied_parameter.type          = parameter->type;
            copied_parameter.name          = parameter->name;
            copied_parameter.location      = copy_file_location(parameter->loc);
            copied_parameter.may_be_unused = parameter->mayBeUnused;
            copied_parameter.floats.reserve(parameter->floats.size());
            copied_parameter.ints.reserve(parameter->ints.size());
            copied_parameter.strings.reserve(parameter->strings.size());
            copied_parameter.bools.reserve(parameter->bools.size());
            for (const pbrt::Float value : parameter->floats) copied_parameter.floats.push_back(static_cast<float>(value));
            for (const int value : parameter->ints) copied_parameter.ints.push_back(value);
            for (const std::string& value : parameter->strings) copied_parameter.strings.push_back(value);
            for (const std::uint8_t value : parameter->bools) copied_parameter.bools.push_back(value);
            copied_parameters.push_back(std::move(copied_parameter));
        }
        return copied_parameters;
    }

    [[nodiscard]] xayah::SpectraPbrtDirective make_directive(const xayah::SpectraPbrtDirectiveKind kind, const pbrt::FileLoc& location) {
        xayah::SpectraPbrtDirective directive{};
        directive.kind      = kind;
        directive.location  = copy_file_location(location);
        directive.transform = identity_matrix_array();
        return directive;
    }

    [[nodiscard]] std::array<float, 16> copy_transform_matrix(const pbrt::Float transform[16]) {
        std::array<float, 16> copied_transform{};
        for (std::size_t index = 0; index < copied_transform.size(); ++index) copied_transform[index] = static_cast<float>(transform[index]);
        return copied_transform;
    }

    [[nodiscard]] pbrt::Transform pbrt_transform_from_parser_matrix(const pbrt::Float transform[16]) {
        return pbrt::Transpose(pbrt::Transform{pbrt::SquareMatrix<4>{pstd::MakeSpan(transform, 16)}});
    }

    [[nodiscard]] std::array<float, 16> matrix_array_from_transform(const pbrt::Transform& transform) {
        std::array<float, 16> matrix{};
        const pbrt::SquareMatrix<4>& pbrt_matrix = transform.GetMatrix();
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) matrix[static_cast<std::size_t>(row * 4 + column)] = static_cast<float>(pbrt_matrix[row][column]);
        }
        return matrix;
    }

    [[nodiscard]] std::array<pbrt::Transform, pbrt::MaxTransforms> inverse_transform_set(const std::array<pbrt::Transform, pbrt::MaxTransforms>& transform_set) {
        std::array<pbrt::Transform, pbrt::MaxTransforms> inverse_set{};
        for (std::size_t index = 0; index < transform_set.size(); ++index) inverse_set[index] = pbrt::Inverse(transform_set[index]);
        return inverse_set;
    }

    [[nodiscard]] std::string lowercase_copy(const std::string& text) {
        std::string lower_text{};
        lower_text.reserve(text.size());
        for (const char character : text) lower_text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        return lower_text;
    }

    [[nodiscard]] bool contains_token_case_insensitive(const std::string& text, const std::string& token) {
        return lowercase_copy(text).find(lowercase_copy(token)) != std::string::npos;
    }

    [[nodiscard]] std::string first_string_parameter_value(const std::vector<xayah::SpectraPbrtParameter>& parameters, const std::string& parameter_name) {
        for (const xayah::SpectraPbrtParameter& parameter : parameters) {
            if (parameter.name == parameter_name && !parameter.strings.empty()) return parameter.strings.front();
        }
        return {};
    }

    template <typename Value>
    void append_vector(std::vector<Value>& destination, std::vector<Value>& source) {
        destination.reserve(destination.size() + source.size());
        for (Value& value : source) destination.push_back(std::move(value));
        source.clear();
    }

    void merge_setting(xayah::SpectraSceneRenderSetting& destination, xayah::SpectraSceneRenderSetting& source) {
        if (source.present) destination = std::move(source);
        source = {};
    }

    std::size_t find_or_create_object_definition(std::vector<xayah::SpectraSceneObjectDefinition>& object_definitions, const std::string& name, const xayah::SpectraPbrtFileLocation& location) {
        for (std::size_t index = 0; index < object_definitions.size(); ++index) {
            if (object_definitions[index].name == name) return index;
        }
        xayah::SpectraSceneObjectDefinition object_definition{};
        object_definition.name     = name;
        object_definition.location = location;
        object_definitions.push_back(std::move(object_definition));
        return object_definitions.size() - 1;
    }

    [[nodiscard]] const xayah::SpectraPbrtParameter* find_parameter(const std::vector<xayah::SpectraPbrtParameter>& parameters, const std::string& name) {
        for (const xayah::SpectraPbrtParameter& parameter : parameters) {
            if (parameter.name == name) return &parameter;
        }
        return nullptr;
    }

    void add_raster_diagnostic(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraRasterDiagnosticKind kind, const std::string& source_type, const std::string& source_name, const std::string& message, const xayah::SpectraPbrtFileLocation& location) {
        xayah::SpectraRasterDiagnostic diagnostic{};
        diagnostic.kind        = kind;
        diagnostic.source_type = source_type;
        diagnostic.source_name = source_name;
        diagnostic.message     = message;
        diagnostic.location    = location;
        raster_scene.diagnostics.push_back(std::move(diagnostic));
    }

    [[nodiscard]] bool material_parameter_references_texture(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.name == "type") return false;
        return parameter.type == "texture" || !parameter.strings.empty();
    }

    [[nodiscard]] std::array<float, 3> vector_subtract(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {left[0] - right[0], left[1] - right[1], left[2] - right[2]};
    }

    [[nodiscard]] std::array<float, 3> vector_cross(const std::array<float, 3>& left, const std::array<float, 3>& right) {
        return {
            left[1] * right[2] - left[2] * right[1],
            left[2] * right[0] - left[0] * right[2],
            left[0] * right[1] - left[1] * right[0],
        };
    }

    [[nodiscard]] float vector_length(const std::array<float, 3>& value) {
        return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
    }

    [[nodiscard]] std::array<float, 3> vector_normalize(const std::array<float, 3>& value) {
        const float length = vector_length(value);
        if (length == 0.0f) return {0.0f, 0.0f, 0.0f};
        return {value[0] / length, value[1] / length, value[2] / length};
    }

    void vector_add_in_place(std::array<float, 3>& target, const std::array<float, 3>& value) {
        target[0] += value[0];
        target[1] += value[1];
        target[2] += value[2];
    }

    [[nodiscard]] std::array<float, 16> multiply_matrix_arrays(const std::array<float, 16>& left, const std::array<float, 16>& right) {
        std::array<float, 16> result{};
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t column = 0; column < 4; ++column) {
                float sum{0.0f};
                for (std::size_t index = 0; index < 4; ++index) sum += left[row * 4 + index] * right[index * 4 + column];
                result[row * 4 + column] = sum;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::array<float, 3>> read_constant_rgb_parameter(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneMaterial& material, const std::string& material_label, const std::string& parameter_name) {
        const xayah::SpectraPbrtParameter* parameter = find_parameter(material.parameters, parameter_name);
        if (parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material requires constant rgb parameter \"{}\"", material.type, parameter_name), material.location);
            return std::nullopt;
        }
        if (material_parameter_references_texture(*parameter)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        if ((parameter->type != "rgb" && parameter->type != "color") || parameter->floats.size() != 3 || !parameter->ints.empty() || !parameter->bools.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" must be a 3-component constant rgb value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        return std::array<float, 3>{parameter->floats[0], parameter->floats[1], parameter->floats[2]};
    }

    [[nodiscard]] std::optional<float> read_constant_float_parameter(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneMaterial& material, const std::string& material_label, const std::string& parameter_name) {
        const xayah::SpectraPbrtParameter* parameter = find_parameter(material.parameters, parameter_name);
        if (parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material requires constant float parameter \"{}\"", material.type, parameter_name), material.location);
            return std::nullopt;
        }
        if (material_parameter_references_texture(*parameter)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        if (parameter->type != "float" || parameter->floats.size() != 1 || !parameter->ints.empty() || !parameter->bools.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" must be a single constant float value", material.type, parameter_name), parameter->location);
            return std::nullopt;
        }
        return parameter->floats[0];
    }

    [[nodiscard]] std::string raster_material_label(const xayah::SpectraSceneMaterial& material, const std::size_t material_index) {
        if (material.named) return material.name;
        return std::format("<inline:{}>", material_index);
    }

    [[nodiscard]] std::optional<std::size_t> resolve_shape_material_index(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape) {
        if (!shape.material_name.empty()) {
            for (std::size_t index = 0; index < scene.materials.size(); ++index) {
                if (scene.materials[index].named && scene.materials[index].name == shape.material_name) return index;
            }
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, std::format("Named material \"{}\" was not recorded in SpectraScene", shape.material_name), shape.location);
            return std::nullopt;
        }
        if (shape.material_index < 0) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, "Shape has no explicit PBRT material; raster v1 does not synthesize a default material", shape.location);
            return std::nullopt;
        }
        const std::size_t material_index = static_cast<std::size_t>(shape.material_index);
        if (material_index >= scene.materials.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingMaterial, "shape", shape.type, std::format("Shape references material index {} but SpectraScene has {} materials", material_index, scene.materials.size()), shape.location);
            return std::nullopt;
        }
        return material_index;
    }

    [[nodiscard]] std::optional<std::size_t> build_raster_material(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t source_material_index, std::map<std::size_t, std::size_t>& material_indices) {
        const auto found_material = material_indices.find(source_material_index);
        if (found_material != material_indices.end()) return found_material->second;
        if (source_material_index >= scene.materials.size()) throw std::runtime_error("Raster material source index is out of range");

        const xayah::SpectraSceneMaterial& material = scene.materials[source_material_index];
        const std::string material_label = raster_material_label(material, source_material_index);
        if (material.type != "diffuse" && material.type != "coateddiffuse") {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("Raster v1 only supports diffuse and coateddiffuse materials, not \"{}\"", material.type), material.location);
            return std::nullopt;
        }
        for (const xayah::SpectraPbrtParameter& parameter : material.parameters) {
            if (parameter.name != "type" && parameter.name != "reflectance" && !(material.type == "coateddiffuse" && parameter.name == "roughness")) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMaterial, "material", material_label, std::format("{} material parameter \"{}\" is not in the raster v1 whitelist", material.type, parameter.name), parameter.location);
                return std::nullopt;
            }
            if (material_parameter_references_texture(parameter)) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedTexture, "material", material_label, std::format("{} material parameter \"{}\" references a texture or string value", material.type, parameter.name), parameter.location);
                return std::nullopt;
            }
        }

        const std::optional<std::array<float, 3>> base_color = read_constant_rgb_parameter(raster_scene, material, material_label, "reflectance");
        if (!base_color.has_value()) return std::nullopt;

        xayah::SpectraRasterMaterial raster_material{};
        raster_material.name                  = material_label;
        raster_material.source_type           = material.type;
        raster_material.base_color            = base_color.value();
        raster_material.source_material_index = source_material_index;
        if (material.type == "coateddiffuse") {
            const std::optional<float> roughness = read_constant_float_parameter(raster_scene, material, material_label, "roughness");
            if (!roughness.has_value()) return std::nullopt;
            raster_material.roughness = roughness.value();
        }

        raster_scene.materials.push_back(std::move(raster_material));
        const std::size_t raster_material_index = raster_scene.materials.size() - 1;
        material_indices[source_material_index] = raster_material_index;
        return raster_material_index;
    }

    [[nodiscard]] std::vector<std::array<float, 3>> read_point3_array_parameter(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.floats.size() % 3 != 0u) throw std::runtime_error(std::format("Parameter {} has {} float values, not a multiple of 3", parameter.name, parameter.floats.size()));
        std::vector<std::array<float, 3>> values{};
        values.reserve(parameter.floats.size() / 3);
        for (std::size_t index = 0; index < parameter.floats.size(); index += 3) values.push_back({parameter.floats[index], parameter.floats[index + 1], parameter.floats[index + 2]});
        return values;
    }

    [[nodiscard]] std::vector<std::array<float, 2>> read_point2_array_parameter(const xayah::SpectraPbrtParameter& parameter) {
        if (parameter.floats.size() % 2 != 0u) throw std::runtime_error(std::format("Parameter {} has {} float values, not a multiple of 2", parameter.name, parameter.floats.size()));
        std::vector<std::array<float, 2>> values{};
        values.reserve(parameter.floats.size() / 2);
        for (std::size_t index = 0; index < parameter.floats.size(); index += 2) values.push_back({parameter.floats[index], parameter.floats[index + 1]});
        return values;
    }

    [[nodiscard]] std::vector<std::array<float, 3>> compute_triangle_vertex_normals(const std::vector<std::array<float, 3>>& positions, const std::vector<std::uint32_t>& indices) {
        std::vector<std::array<float, 3>> normals(positions.size(), {0.0f, 0.0f, 0.0f});
        for (std::size_t index = 0; index < indices.size(); index += 3) {
            const std::uint32_t vertex0 = indices[index];
            const std::uint32_t vertex1 = indices[index + 1];
            const std::uint32_t vertex2 = indices[index + 2];
            const std::array<float, 3> edge10 = vector_subtract(positions[vertex1], positions[vertex0]);
            const std::array<float, 3> edge20 = vector_subtract(positions[vertex2], positions[vertex0]);
            const std::array<float, 3> face_normal = vector_normalize(vector_cross(edge10, edge20));
            vector_add_in_place(normals[vertex0], face_normal);
            vector_add_in_place(normals[vertex1], face_normal);
            vector_add_in_place(normals[vertex2], face_normal);
        }
        for (std::array<float, 3>& normal : normals) normal = vector_normalize(normal);
        return normals;
    }

    [[nodiscard]] std::optional<std::size_t> append_raster_mesh(xayah::SpectraRasterScene& raster_scene, const std::size_t source_shape_index, const std::string& source_type, const std::filesystem::path& source_path, const xayah::SpectraPbrtFileLocation& location, const std::vector<std::array<float, 3>>& positions, const std::vector<std::array<float, 3>>& normals, const std::vector<std::array<float, 2>>& uvs, const std::vector<std::uint32_t>& local_indices) {
        if (positions.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh has no vertex positions", location);
            return std::nullopt;
        }
        if (local_indices.empty() || local_indices.size() % 3 != 0u) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh index count must be a non-empty multiple of 3", location);
            return std::nullopt;
        }
        if (!normals.empty() && normals.size() != positions.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh normal count does not match vertex count", location);
            return std::nullopt;
        }
        if (!uvs.empty() && uvs.size() != positions.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, "Mesh uv count does not match vertex count", location);
            return std::nullopt;
        }
        for (const std::uint32_t index : local_indices) {
            if (static_cast<std::size_t>(index) >= positions.size()) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", source_type, std::format("Mesh index {} is out of range for {} vertices", index, positions.size()), location);
                return std::nullopt;
            }
        }
        if (raster_scene.vertices.size() + positions.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Raster scene vertex count exceeds uint32 index range");

        const std::size_t first_vertex = raster_scene.vertices.size();
        const std::size_t first_index = raster_scene.indices.size();
        const std::vector<std::array<float, 3>> computed_normals = normals.empty() ? compute_triangle_vertex_normals(positions, local_indices) : normals;

        raster_scene.vertices.reserve(raster_scene.vertices.size() + positions.size());
        for (std::size_t index = 0; index < positions.size(); ++index) {
            xayah::SpectraRasterVertex vertex{};
            vertex.position = positions[index];
            vertex.normal   = computed_normals[index];
            if (!uvs.empty()) vertex.uv = uvs[index];
            raster_scene.vertices.push_back(vertex);
        }

        raster_scene.indices.reserve(raster_scene.indices.size() + local_indices.size());
        for (const std::uint32_t index : local_indices) raster_scene.indices.push_back(static_cast<std::uint32_t>(first_vertex) + index);

        xayah::SpectraRasterGeometry geometry{};
        geometry.source_shape_index = source_shape_index;
        geometry.source_type        = source_type;
        geometry.source_path        = source_path;
        geometry.first_vertex       = first_vertex;
        geometry.vertex_count       = positions.size();
        geometry.first_index        = first_index;
        geometry.index_count        = local_indices.size();
        raster_scene.geometries.push_back(std::move(geometry));
        return raster_scene.geometries.size() - 1;
    }

    [[nodiscard]] std::optional<std::size_t> append_trianglemesh_geometry(xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape, const std::size_t shape_index) {
        const xayah::SpectraPbrtParameter* position_parameter = find_parameter(shape.parameters, "P");
        if (position_parameter == nullptr) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh requires point3 P positions", shape.location);
            return std::nullopt;
        }

        std::vector<std::array<float, 3>> positions{};
        try {
            positions = read_point3_array_parameter(*position_parameter);
        } catch (const std::exception& error) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), position_parameter->location);
            return std::nullopt;
        }

        std::vector<std::uint32_t> local_indices{};
        const xayah::SpectraPbrtParameter* index_parameter = find_parameter(shape.parameters, "indices");
        if (index_parameter == nullptr) {
            if (positions.size() != 3) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh without indices must contain exactly 3 positions", shape.location);
                return std::nullopt;
            }
            local_indices = {0u, 1u, 2u};
        } else {
            if (index_parameter->ints.empty() || index_parameter->ints.size() % 3 != 0u) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "trianglemesh indices must be a non-empty multiple of 3", index_parameter->location);
                return std::nullopt;
            }
            local_indices.reserve(index_parameter->ints.size());
            for (const int index : index_parameter->ints) {
                if (index < 0) {
                    add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, std::format("trianglemesh index {} is negative", index), index_parameter->location);
                    return std::nullopt;
                }
                local_indices.push_back(static_cast<std::uint32_t>(index));
            }
        }

        std::vector<std::array<float, 3>> normals{};
        const xayah::SpectraPbrtParameter* normal_parameter = find_parameter(shape.parameters, "N");
        if (normal_parameter != nullptr) {
            try {
                normals = read_point3_array_parameter(*normal_parameter);
            } catch (const std::exception& error) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), normal_parameter->location);
                return std::nullopt;
            }
        }

        std::vector<std::array<float, 2>> uvs{};
        const xayah::SpectraPbrtParameter* uv_parameter = find_parameter(shape.parameters, "uv");
        if (uv_parameter != nullptr) {
            try {
                uvs = read_point2_array_parameter(*uv_parameter);
            } catch (const std::exception& error) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, error.what(), uv_parameter->location);
                return std::nullopt;
            }
        }

        return append_raster_mesh(raster_scene, shape_index, shape.type, {}, shape.location, positions, normals, uvs, local_indices);
    }

    [[nodiscard]] std::filesystem::path resolve_shape_relative_path(const xayah::SpectraScene& scene, const xayah::SpectraSceneShape& shape, const std::string& filename) {
        std::filesystem::path resolved_path{filename};
        if (resolved_path.is_absolute()) return resolved_path;
        std::filesystem::path base_path{};
        if (!shape.location.filename.empty()) base_path = std::filesystem::path{shape.location.filename}.parent_path();
        if (base_path.empty()) base_path = scene.scene_path.parent_path();
        return base_path / resolved_path;
    }

    [[nodiscard]] std::optional<std::size_t> append_plymesh_geometry(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const xayah::SpectraSceneShape& shape, const std::size_t shape_index) {
        const xayah::SpectraPbrtParameter* filename_parameter = find_parameter(shape.parameters, "filename");
        if (filename_parameter == nullptr || filename_parameter->strings.size() != 1) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh requires exactly one string filename parameter", shape.location);
            return std::nullopt;
        }

        const std::filesystem::path ply_path = resolve_shape_relative_path(scene, shape, filename_parameter->strings.front());
        if (!std::filesystem::exists(ply_path)) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::MissingPlyFile, "shape", shape.type, std::format("PLY file does not exist: {}", ply_path.string()), filename_parameter->location);
            return std::nullopt;
        }

        pbrt::TriQuadMesh mesh = pbrt::TriQuadMesh::ReadPLY(ply_path.string());
        mesh.ConvertToOnlyTriangles();
        if (mesh.n.empty()) mesh.ComputeNormals();
        if (mesh.p.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh produced no vertices", shape.location);
            return std::nullopt;
        }
        if (mesh.triIndices.empty() || mesh.triIndices.size() % 3 != 0u) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh produced no triangle indices", shape.location);
            return std::nullopt;
        }
        if (!mesh.n.empty() && mesh.n.size() != mesh.p.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh normal count does not match vertex count", shape.location);
            return std::nullopt;
        }
        if (!mesh.uv.empty() && mesh.uv.size() != mesh.p.size()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, "plymesh uv count does not match vertex count", shape.location);
            return std::nullopt;
        }

        std::vector<std::array<float, 3>> positions{};
        positions.reserve(mesh.p.size());
        for (const pbrt::Point3f& point : mesh.p) positions.push_back({static_cast<float>(point.x), static_cast<float>(point.y), static_cast<float>(point.z)});

        std::vector<std::array<float, 3>> normals{};
        normals.reserve(mesh.n.size());
        for (const pbrt::Normal3f& normal : mesh.n) normals.push_back({static_cast<float>(normal.x), static_cast<float>(normal.y), static_cast<float>(normal.z)});

        std::vector<std::array<float, 2>> uvs{};
        uvs.reserve(mesh.uv.size());
        for (const pbrt::Point2f& uv : mesh.uv) uvs.push_back({static_cast<float>(uv.x), static_cast<float>(uv.y)});

        std::vector<std::uint32_t> local_indices{};
        local_indices.reserve(mesh.triIndices.size());
        for (const int index : mesh.triIndices) {
            if (index < 0) {
                add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::InvalidMesh, "shape", shape.type, std::format("plymesh index {} is negative", index), shape.location);
                return std::nullopt;
            }
            local_indices.push_back(static_cast<std::uint32_t>(index));
        }

        return append_raster_mesh(raster_scene, shape_index, shape.type, ply_path, shape.location, positions, normals, uvs, local_indices);
    }

    [[nodiscard]] bool shape_is_supported_for_raster(const xayah::SpectraSceneShape& shape, xayah::SpectraRasterScene& raster_scene) {
        if (shape.animated_transform) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform, "shape", shape.type, "Raster v1 does not support animated shape transforms", shape.location);
            return false;
        }
        if (!shape.inside_medium.empty() || !shape.outside_medium.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedMediumBinding, "shape", shape.type, "Raster v1 does not support PBRT medium-bound shapes", shape.location);
            return false;
        }
        if (!shape.area_light_type.empty()) {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedAreaLight, "shape", shape.type, "Raster v1 does not render PBRT area-light shapes", shape.location);
            return false;
        }
        if (shape.type != "trianglemesh" && shape.type != "plymesh") {
            add_raster_diagnostic(raster_scene, xayah::SpectraRasterDiagnosticKind::UnsupportedShape, "shape", shape.type, std::format("Raster v1 only supports trianglemesh and plymesh, not \"{}\"", shape.type), shape.location);
            return false;
        }
        return true;
    }

    [[nodiscard]] std::optional<std::size_t> build_raster_geometry(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t shape_index, std::map<std::size_t, std::size_t>& geometry_indices) {
        const auto found_geometry = geometry_indices.find(shape_index);
        if (found_geometry != geometry_indices.end()) return found_geometry->second;
        if (shape_index >= scene.shapes.size()) throw std::runtime_error("Raster shape index is out of range");

        const xayah::SpectraSceneShape& shape = scene.shapes[shape_index];
        std::optional<std::size_t> geometry_index{};
        if (shape.type == "trianglemesh") geometry_index = append_trianglemesh_geometry(raster_scene, shape, shape_index);
        else if (shape.type == "plymesh") geometry_index = append_plymesh_geometry(scene, raster_scene, shape, shape_index);
        else throw std::runtime_error("Raster geometry builder received an unsupported shape type");
        if (geometry_index.has_value()) geometry_indices[shape_index] = geometry_index.value();
        return geometry_index;
    }

    void append_raster_draw(const xayah::SpectraScene& scene, xayah::SpectraRasterScene& raster_scene, const std::size_t shape_index, const std::array<float, 16>& transform, const std::size_t instance_index, std::map<std::size_t, std::size_t>& material_indices, std::map<std::size_t, std::size_t>& geometry_indices) {
        if (shape_index >= scene.shapes.size()) throw std::runtime_error("Raster draw shape index is out of range");
        const xayah::SpectraSceneShape& shape = scene.shapes[shape_index];
        if (!shape_is_supported_for_raster(shape, raster_scene)) return;

        const std::optional<std::size_t> source_material_index = resolve_shape_material_index(scene, raster_scene, shape);
        if (!source_material_index.has_value()) return;
        const std::optional<std::size_t> raster_material_index = build_raster_material(scene, raster_scene, source_material_index.value(), material_indices);
        if (!raster_material_index.has_value()) return;
        const std::optional<std::size_t> raster_geometry_index = build_raster_geometry(scene, raster_scene, shape_index, geometry_indices);
        if (!raster_geometry_index.has_value()) return;

        xayah::SpectraRasterDraw draw{};
        draw.geometry_index        = raster_geometry_index.value();
        draw.material_index        = raster_material_index.value();
        draw.source_shape_index    = shape_index;
        draw.source_instance_index = instance_index;
        draw.transform             = transform;
        draw.reverse_orientation   = shape.reverse_orientation;
        raster_scene.draws.push_back(std::move(draw));
    }

    [[nodiscard]] const xayah::SpectraSceneObjectDefinition* find_object_definition(const xayah::SpectraScene& scene, const std::string& name) {
        for (const xayah::SpectraSceneObjectDefinition& object_definition : scene.object_definitions) {
            if (object_definition.name == name) return &object_definition;
        }
        return nullptr;
    }

    struct SpectraPbrtBuilderGraphicsState {
        std::string current_inside_medium{};
        std::string current_outside_medium{};
        std::string current_material_name{};
        int current_material_index{-1};
        std::string area_light_type{};
        xayah::SpectraPbrtFileLocation area_light_location{};
        std::vector<xayah::SpectraPbrtParameter> area_light_parameters{};
        bool reverse_orientation{false};
        std::array<pbrt::Transform, pbrt::MaxTransforms> ctm{};
        std::uint32_t active_transform_bits{all_transform_bits};
        pbrt::Float transform_start_time{0.0f};
        pbrt::Float transform_end_time{1.0f};
    };

    struct SpectraPbrtSceneBuilder final : pbrt::ParserTarget {
        xayah::SpectraScene* spectra_scene{nullptr};
        xayah::SpectraSceneBuildChunk chunk{};
        SpectraPbrtBuilderGraphicsState graphics_state{};
        std::vector<SpectraPbrtBuilderGraphicsState> pushed_graphics_states{};
        std::map<std::string, std::array<pbrt::Transform, pbrt::MaxTransforms>> named_coordinate_systems{};
        std::vector<std::unique_ptr<pbrt::ParserTarget>> imported_builders{};
        std::string active_object_definition_name{};
        std::size_t material_index_base{0};
        bool root_builder{true};
        bool world_begun{false};

        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene) : spectra_scene{&scene} {}
        SpectraPbrtSceneBuilder(xayah::SpectraScene& scene, const SpectraPbrtBuilderGraphicsState& parent_graphics_state, const std::vector<SpectraPbrtBuilderGraphicsState>& parent_pushed_graphics_states, const std::map<std::string, std::array<pbrt::Transform, pbrt::MaxTransforms>>& parent_named_coordinate_systems, const std::string& parent_active_object_definition_name, const std::size_t parent_material_index_base, const bool parent_world_begun) : spectra_scene{&scene}, graphics_state{parent_graphics_state}, pushed_graphics_states{parent_pushed_graphics_states}, named_coordinate_systems{parent_named_coordinate_systems}, active_object_definition_name{parent_active_object_definition_name}, material_index_base{parent_material_index_base}, root_builder{false}, world_begun{parent_world_begun} {}

        ~SpectraPbrtSceneBuilder() override = default;

        SpectraPbrtSceneBuilder(const SpectraPbrtSceneBuilder& other)                = delete;
        SpectraPbrtSceneBuilder(SpectraPbrtSceneBuilder&& other) noexcept            = delete;
        SpectraPbrtSceneBuilder& operator=(const SpectraPbrtSceneBuilder& other)     = delete;
        SpectraPbrtSceneBuilder& operator=(SpectraPbrtSceneBuilder&& other) noexcept = delete;

        void record_directive(xayah::SpectraPbrtDirective directive) {
            this->chunk.pbrt_directives.push_back(std::move(directive));
        }

        void merge_chunk(xayah::SpectraSceneBuildChunk& imported_chunk) {
            append_vector(this->chunk.pbrt_directives, imported_chunk.pbrt_directives);
            merge_setting(this->chunk.pixel_filter, imported_chunk.pixel_filter);
            merge_setting(this->chunk.film, imported_chunk.film);
            merge_setting(this->chunk.sampler, imported_chunk.sampler);
            merge_setting(this->chunk.accelerator, imported_chunk.accelerator);
            merge_setting(this->chunk.integrator, imported_chunk.integrator);
            merge_setting(this->chunk.camera, imported_chunk.camera);
            append_vector(this->chunk.textures, imported_chunk.textures);
            append_vector(this->chunk.materials, imported_chunk.materials);
            append_vector(this->chunk.mediums, imported_chunk.mediums);
            append_vector(this->chunk.medium_bindings, imported_chunk.medium_bindings);
            append_vector(this->chunk.lights, imported_chunk.lights);
            append_vector(this->chunk.shapes, imported_chunk.shapes);
            append_vector(this->chunk.object_definitions, imported_chunk.object_definitions);
            append_vector(this->chunk.object_instances, imported_chunk.object_instances);
            append_vector(this->chunk.unsupported_features, imported_chunk.unsupported_features);
        }

        [[nodiscard]] bool current_transform_is_animated() const {
            for (std::size_t index = 0; index + 1 < this->graphics_state.ctm.size(); ++index) {
                if (this->graphics_state.ctm[index] != this->graphics_state.ctm[index + 1]) return true;
            }
            return false;
        }

        [[nodiscard]] std::array<float, 16> current_transform_matrix() const {
            return matrix_array_from_transform(this->graphics_state.ctm[0]);
        }

        void mark_unsupported(const xayah::SpectraSceneUnsupportedFeatureKind kind, const std::string& source_type, const std::string& source_name, const std::string& message, const pbrt::FileLoc& location) {
            xayah::SpectraSceneUnsupportedFeature feature{};
            feature.kind        = kind;
            feature.source_type = source_type;
            feature.source_name = source_name;
            feature.message     = message;
            feature.location    = copy_file_location(location);
            this->chunk.unsupported_features.push_back(std::move(feature));
        }

        void mark_medium_features(const std::string& name, const std::vector<xayah::SpectraPbrtParameter>& parameters, const pbrt::FileLoc& location) {
            this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "medium", name, "PBRT participating media are not rasterizable in Spectra rasterizer v1", location);
            const std::string medium_type = first_string_parameter_value(parameters, "type");
            bool references_vdb = contains_token_case_insensitive(name, "vdb") || contains_token_case_insensitive(medium_type, "vdb") || contains_token_case_insensitive(medium_type, "nanovdb") || contains_token_case_insensitive(medium_type, "grid");
            for (const xayah::SpectraPbrtParameter& parameter : parameters) {
                for (const std::string& value : parameter.strings) {
                    references_vdb = references_vdb || contains_token_case_insensitive(value, "vdb") || contains_token_case_insensitive(value, "nanovdb") || contains_token_case_insensitive(value, "grid");
                }
            }
            if (references_vdb) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::VdbMedium, "medium", name, "VDB/NanoVDB media require a later explicit raster policy", location);
        }

        [[nodiscard]] xayah::SpectraSceneRenderSetting make_render_setting(const std::string& type, const std::string& name, const std::vector<xayah::SpectraPbrtParameter>& parameters, const pbrt::FileLoc& location, const bool include_transform) const {
            xayah::SpectraSceneRenderSetting setting{};
            setting.present    = true;
            setting.type       = type;
            setting.name       = name;
            setting.location   = copy_file_location(location);
            setting.transform  = include_transform ? this->current_transform_matrix() : identity_matrix_array();
            setting.parameters = parameters;
            return setting;
        }

        void apply_transform_to_active(const pbrt::Transform& transform) {
            for (std::size_t index = 0; index < this->graphics_state.ctm.size(); ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = this->graphics_state.ctm[index] * transform;
            }
        }

        void replace_active_transform(const pbrt::Transform& transform) {
            for (std::size_t index = 0; index < this->graphics_state.ctm.size(); ++index) {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(index);
                if ((this->graphics_state.active_transform_bits & bit) != 0u) this->graphics_state.ctm[index] = transform;
            }
        }

        void Scale(const pbrt::Float sx, const pbrt::Float sy, const pbrt::Float sz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Scale, loc);
            directive.vector                       = {static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz), 0.0f};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Scale(sx, sy, sz));
        }

        void Shape(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Shape, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));

            const bool animated_transform = this->current_transform_is_animated();
            xayah::SpectraSceneShape shape{};
            shape.type                   = name;
            shape.material_name          = this->graphics_state.current_material_name;
            shape.material_index         = this->graphics_state.current_material_index;
            shape.inside_medium          = this->graphics_state.current_inside_medium;
            shape.outside_medium         = this->graphics_state.current_outside_medium;
            shape.object_definition_name = this->active_object_definition_name;
            shape.area_light_type        = this->graphics_state.area_light_type;
            shape.reverse_orientation    = this->graphics_state.reverse_orientation;
            shape.animated_transform     = animated_transform;
            shape.location               = copy_file_location(loc);
            shape.transform              = this->current_transform_matrix();
            shape.parameters             = copied_parameters;
            this->chunk.shapes.push_back(std::move(shape));

            if (animated_transform) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform, "shape", name, "Animated shape transforms are not rasterizable in Spectra rasterizer v1", loc);
            if (!this->graphics_state.current_inside_medium.empty() || !this->graphics_state.current_outside_medium.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "shape", name, "Shape references a PBRT medium interface", loc);
            if (!this->graphics_state.area_light_type.empty()) {
                xayah::SpectraSceneLight light{};
                light.type           = this->graphics_state.area_light_type;
                light.area           = true;
                light.outside_medium = this->graphics_state.current_outside_medium;
                light.location       = this->graphics_state.area_light_location;
                light.transform      = this->current_transform_matrix();
                light.parameters     = this->graphics_state.area_light_parameters;
                this->chunk.lights.push_back(std::move(light));
                if (!this->active_object_definition_name.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AreaLightInObjectDefinition, "shape", name, "Area lights inside PBRT object definitions need a later explicit instance policy", loc);
            }
        }

        void Option(const std::string& name, const std::string& value, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Option, loc);
            directive.name                         = name;
            directive.value                        = value;
            this->record_directive(std::move(directive));
        }

        void Identity(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::Identity, loc));
            this->replace_active_transform(pbrt::Transform{});
        }

        void Translate(const pbrt::Float dx, const pbrt::Float dy, const pbrt::Float dz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Translate, loc);
            directive.vector                       = {static_cast<float>(dx), static_cast<float>(dy), static_cast<float>(dz), 0.0f};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Translate(pbrt::Vector3f{dx, dy, dz}));
        }

        void Rotate(const pbrt::Float angle, const pbrt::Float ax, const pbrt::Float ay, const pbrt::Float az, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Rotate, loc);
            directive.vector                       = {static_cast<float>(angle), static_cast<float>(ax), static_cast<float>(ay), static_cast<float>(az)};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::Rotate(angle, pbrt::Vector3f{ax, ay, az}));
        }

        void LookAt(const pbrt::Float ex, const pbrt::Float ey, const pbrt::Float ez, const pbrt::Float lx, const pbrt::Float ly, const pbrt::Float lz, const pbrt::Float ux, const pbrt::Float uy, const pbrt::Float uz, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::LookAt, loc);
            directive.look_at                      = {static_cast<float>(ex), static_cast<float>(ey), static_cast<float>(ez), static_cast<float>(lx), static_cast<float>(ly), static_cast<float>(lz), static_cast<float>(ux), static_cast<float>(uy), static_cast<float>(uz)};
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt::LookAt(pbrt::Point3f{ex, ey, ez}, pbrt::Point3f{lx, ly, lz}, pbrt::Vector3f{ux, uy, uz}));
        }

        void ConcatTransform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ConcatTransform, loc);
            directive.transform                    = copy_transform_matrix(transform);
            this->record_directive(std::move(directive));
            this->apply_transform_to_active(pbrt_transform_from_parser_matrix(transform));
        }

        void Transform(pbrt::Float transform[16], const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Transform, loc);
            directive.transform                    = copy_transform_matrix(transform);
            this->record_directive(std::move(directive));
            this->replace_active_transform(pbrt_transform_from_parser_matrix(transform));
        }

        void CoordinateSystem(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::CoordinateSystem, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->named_coordinate_systems[name]   = this->graphics_state.ctm;
        }

        void CoordSysTransform(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::CoordSysTransform, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            const auto found = this->named_coordinate_systems.find(name);
            if (found != this->named_coordinate_systems.end()) this->graphics_state.ctm = found->second;
        }

        void ActiveTransformAll(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformAll, loc));
            this->graphics_state.active_transform_bits = all_transform_bits;
        }

        void ActiveTransformEndTime(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformEndTime, loc));
            this->graphics_state.active_transform_bits = end_transform_bit;
        }

        void ActiveTransformStartTime(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ActiveTransformStartTime, loc));
            this->graphics_state.active_transform_bits = start_transform_bit;
        }

        void TransformTimes(const pbrt::Float start, const pbrt::Float end, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::TransformTimes, loc);
            directive.times                        = {static_cast<float>(start), static_cast<float>(end)};
            this->record_directive(std::move(directive));
            this->graphics_state.transform_start_time = start;
            this->graphics_state.transform_end_time   = end;
        }

        void ColorSpace(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ColorSpace, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
        }

        void PixelFilter(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::PixelFilter, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.pixel_filter = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Film, loc);
            directive.type                         = type;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.film = this->make_render_setting(type, {}, copied_parameters, loc, false);
        }

        void Accelerator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Accelerator, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.accelerator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Integrator(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Integrator, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.integrator = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void Camera(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Camera, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.camera = this->make_render_setting(name, name, copied_parameters, loc, true);
            this->named_coordinate_systems["camera"] = inverse_transform_set(this->graphics_state.ctm);
        }

        void MakeNamedMedium(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MakeNamedMedium, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneMedium medium{};
            medium.name       = name;
            medium.type       = first_string_parameter_value(copied_parameters, "type");
            medium.location   = copy_file_location(loc);
            medium.transform  = this->current_transform_matrix();
            medium.parameters = copied_parameters;
            this->chunk.mediums.push_back(std::move(medium));
            this->mark_medium_features(name, copied_parameters, loc);
        }

        void MediumInterface(const std::string& inside_name, const std::string& outside_name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MediumInterface, loc);
            directive.name                         = inside_name;
            directive.value                        = outside_name;
            this->record_directive(std::move(directive));
            this->graphics_state.current_inside_medium  = inside_name;
            this->graphics_state.current_outside_medium = outside_name;
            xayah::SpectraSceneMediumBinding binding{};
            binding.inside   = inside_name;
            binding.outside  = outside_name;
            binding.location = copy_file_location(loc);
            this->chunk.medium_bindings.push_back(std::move(binding));
            if (!inside_name.empty() || !outside_name.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "medium-interface", inside_name + "/" + outside_name, "PBRT medium interfaces are not rasterizable in Spectra rasterizer v1", loc);
        }

        void Sampler(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Sampler, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->chunk.sampler = this->make_render_setting(name, name, copied_parameters, loc, false);
        }

        void WorldBegin(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::WorldBegin, loc));
            this->graphics_state = {};
            this->pushed_graphics_states.clear();
            this->active_object_definition_name.clear();
            this->named_coordinate_systems["world"] = this->graphics_state.ctm;
            this->world_begun = true;
        }

        void AttributeBegin(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::AttributeBegin, loc));
            this->pushed_graphics_states.push_back(this->graphics_state);
        }

        void AttributeEnd(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::AttributeEnd, loc));
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
        }

        void Attribute(const std::string& target, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Attribute, loc);
            directive.target                       = target;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParserAttribute, "attribute", target, "PBRT Attribute directives need explicit rasterizer support before use", loc);
        }

        void Texture(const std::string& name, const std::string& type, const std::string& texture_name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Texture, loc);
            directive.name                         = name;
            directive.type                         = type;
            directive.value                        = texture_name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneTexture texture{};
            texture.name           = name;
            texture.value_type     = type == "float" ? xayah::SpectraSceneTextureValueType::Float : type == "spectrum" ? xayah::SpectraSceneTextureValueType::Spectrum : xayah::SpectraSceneTextureValueType::Unknown;
            texture.implementation = texture_name;
            texture.location       = copy_file_location(loc);
            texture.transform      = this->current_transform_matrix();
            texture.parameters     = copied_parameters;
            this->chunk.textures.push_back(std::move(texture));
            if (texture_name != "imagemap" && texture_name != "constant") this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ProceduralTexture, "texture", name, "Procedural PBRT textures require a later explicit raster policy", loc);
        }

        void Material(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::Material, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneMaterial material{};
            material.name       = {};
            material.type       = name;
            material.named      = false;
            material.location   = copy_file_location(loc);
            material.parameters = copied_parameters;
            this->chunk.materials.push_back(std::move(material));
            this->graphics_state.current_material_index = static_cast<int>(this->material_index_base + this->chunk.materials.size() - 1);
            this->graphics_state.current_material_name.clear();
        }

        void MakeNamedMaterial(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::MakeNamedMaterial, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneMaterial material{};
            material.name       = name;
            material.type       = first_string_parameter_value(copied_parameters, "type");
            material.named      = true;
            material.location   = copy_file_location(loc);
            material.parameters = copied_parameters;
            this->chunk.materials.push_back(std::move(material));
        }

        void NamedMaterial(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::NamedMaterial, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->graphics_state.current_material_name  = name;
            this->graphics_state.current_material_index = -1;
        }

        void LightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::LightSource, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            xayah::SpectraSceneLight light{};
            light.type           = name;
            light.area           = false;
            light.outside_medium = this->graphics_state.current_outside_medium;
            light.location       = copy_file_location(loc);
            light.transform      = this->current_transform_matrix();
            light.parameters     = copied_parameters;
            this->chunk.lights.push_back(std::move(light));
            if (!this->graphics_state.current_outside_medium.empty()) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::ParticipatingMedium, "light", name, "Light references a PBRT outside medium", loc);
        }

        void AreaLightSource(const std::string& name, pbrt::ParsedParameterVector parameters, const pbrt::FileLoc loc) override {
            const std::vector<xayah::SpectraPbrtParameter> copied_parameters = copy_parameters(parameters);
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::AreaLightSource, loc);
            directive.name                         = name;
            directive.parameters                   = copied_parameters;
            this->record_directive(std::move(directive));
            this->graphics_state.area_light_type       = name;
            this->graphics_state.area_light_location   = copy_file_location(loc);
            this->graphics_state.area_light_parameters = copied_parameters;
        }

        void ReverseOrientation(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ReverseOrientation, loc));
            this->graphics_state.reverse_orientation = !this->graphics_state.reverse_orientation;
        }

        void ObjectBegin(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ObjectBegin, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            this->pushed_graphics_states.push_back(this->graphics_state);
            this->active_object_definition_name = name;
            find_or_create_object_definition(this->chunk.object_definitions, name, copy_file_location(loc));
        }

        void ObjectEnd(const pbrt::FileLoc loc) override {
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::ObjectEnd, loc));
            if (!this->pushed_graphics_states.empty()) {
                this->graphics_state = this->pushed_graphics_states.back();
                this->pushed_graphics_states.pop_back();
            }
            this->active_object_definition_name.clear();
        }

        void ObjectInstance(const std::string& name, const pbrt::FileLoc loc) override {
            xayah::SpectraPbrtDirective directive = make_directive(xayah::SpectraPbrtDirectiveKind::ObjectInstance, loc);
            directive.name                         = name;
            this->record_directive(std::move(directive));
            const bool animated_transform = this->current_transform_is_animated();
            xayah::SpectraSceneObjectInstance object_instance{};
            object_instance.name               = name;
            object_instance.animated_transform = animated_transform;
            object_instance.location           = copy_file_location(loc);
            object_instance.transform          = this->current_transform_matrix();
            this->chunk.object_instances.push_back(std::move(object_instance));
            if (animated_transform) this->mark_unsupported(xayah::SpectraSceneUnsupportedFeatureKind::AnimatedTransform, "object-instance", name, "Animated object instance transforms are not rasterizable in Spectra rasterizer v1", loc);
        }

        void EndOfFiles() override {
            pbrt::FileLoc location{};
            location.filename = this->spectra_scene == nullptr ? std::string_view{} : std::string_view{this->spectra_scene->scene_path_text};
            this->record_directive(make_directive(xayah::SpectraPbrtDirectiveKind::EndOfFiles, location));
            if (!this->pushed_graphics_states.empty()) throw std::runtime_error("Missing AttributeEnd before EndOfFiles in Spectra scene parser");
            if (this->root_builder) {
                if (this->spectra_scene == nullptr) throw std::runtime_error("Spectra scene builder has no target scene at EndOfFiles");
                this->spectra_scene->append_build_chunk(std::move(this->chunk));
            }
        }

        bool IsImportAllowed() const override {
            return this->world_begun;
        }

        std::unique_ptr<pbrt::ParserTarget> CopyForImport() override {
            if (this->spectra_scene == nullptr) throw std::runtime_error("Cannot copy Spectra scene parser import target without a scene");
            return std::make_unique<SpectraPbrtSceneBuilder>(*this->spectra_scene, this->graphics_state, this->pushed_graphics_states, this->named_coordinate_systems, this->active_object_definition_name, this->material_index_base + this->chunk.materials.size(), this->world_begun);
        }

        void MergeImported(std::unique_ptr<pbrt::ParserTarget> imported) override {
            SpectraPbrtSceneBuilder* imported_builder = dynamic_cast<SpectraPbrtSceneBuilder*>(imported.get());
            if (imported_builder == nullptr) throw std::runtime_error("PBRT import target type does not match Spectra scene builder");
            this->merge_chunk(imported_builder->chunk);
            this->imported_builders.push_back(std::move(imported));
        }

        bool UsesAsyncImport() const override {
            return false;
        }
    };

    [[nodiscard]] pbrt::ParsedParameter* make_integer_parameter(const std::string_view name, const int value, const pbrt::FileLoc& location) {
        pbrt::ParsedParameter* parameter = new pbrt::ParsedParameter(location);
        parameter->type                  = "integer";
        parameter->name                  = std::string{name};
        parameter->AddInt(value);
        return parameter;
    }

    [[nodiscard]] pbrt::ParsedParameterVector film_parameters_with_resolution(pbrt::ParsedParameterVector parameters, const std::array<int, 2>& resolution, const pbrt::FileLoc& location) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT backend resolution must be positive");
        for (auto iterator = parameters.begin(); iterator != parameters.end();) {
            pbrt::ParsedParameter* parameter = *iterator;
            if (parameter == nullptr) throw std::runtime_error("PBRT Film parameter is null");
            if (parameter->name == "xresolution" || parameter->name == "yresolution") {
                delete parameter;
                iterator = parameters.erase(iterator);
            } else
                ++iterator;
        }
        parameters.push_back(make_integer_parameter("xresolution", resolution[0], location));
        parameters.push_back(make_integer_parameter("yresolution", resolution[1], location));
        return parameters;
    }

    struct SpectraResolutionOverrideSceneBuilder final : pbrt::BasicSceneBuilder {
        SpectraResolutionOverrideSceneBuilder(pbrt::BasicScene* scene, const std::array<int, 2>& resolution) : pbrt::BasicSceneBuilder{scene}, resolution{resolution} {
            if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT backend resolution must be positive");
        }

        void Film(const std::string& type, pbrt::ParsedParameterVector parameters, pbrt::FileLoc location) override {
            this->film_seen = true;
            pbrt::BasicSceneBuilder::Film(type, film_parameters_with_resolution(std::move(parameters), this->resolution, location), location);
        }

        void WorldBegin(pbrt::FileLoc location) override {
            if (!this->film_seen) pbrt::BasicSceneBuilder::Film("rgb", film_parameters_with_resolution(pbrt::ParsedParameterVector{}, this->resolution, location), location);
            pbrt::BasicSceneBuilder::WorldBegin(location);
        }

        std::array<int, 2> resolution{0, 0};
        bool film_seen{false};
    };
}

namespace xayah {
    struct SpectraPbrtBackendSceneState {
        std::unique_ptr<pbrt::BasicScene> scene{};
        std::unique_ptr<pbrt::BasicSceneBuilder> builder{};
    };

    void SpectraScene::load(const std::filesystem::path& path) {
        if (!this->scene_path.empty()) throw std::runtime_error("Spectra scene is already loaded");
        if (path.empty()) throw std::runtime_error("Spectra scene path is empty");
        if (!std::filesystem::exists(path)) throw std::runtime_error(std::string{"Spectra scene does not exist: "} + path.string());

        try {
            this->scene_path      = path;
            this->scene_label     = path.filename().string();
            this->scene_path_text = path.string();
            SpectraPbrtSceneBuilder builder{*this};
            std::vector<std::string> filenames{this->scene_path_text};
            pbrt::ParseFiles(&builder, filenames);
            if (this->pbrt_directives.empty()) throw std::runtime_error("Spectra scene parser recorded no PBRT directives");
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraScene::append_build_chunk(SpectraSceneBuildChunk chunk) {
        std::lock_guard<std::mutex> lock{this->scene_mutex};
        append_vector(this->pbrt_directives, chunk.pbrt_directives);
        merge_setting(this->pixel_filter, chunk.pixel_filter);
        merge_setting(this->film, chunk.film);
        merge_setting(this->sampler, chunk.sampler);
        merge_setting(this->accelerator, chunk.accelerator);
        merge_setting(this->integrator, chunk.integrator);
        merge_setting(this->camera, chunk.camera);
        append_vector(this->textures, chunk.textures);
        append_vector(this->materials, chunk.materials);
        append_vector(this->mediums, chunk.mediums);
        append_vector(this->medium_bindings, chunk.medium_bindings);
        append_vector(this->lights, chunk.lights);
        append_vector(this->object_definitions, chunk.object_definitions);
        for (SpectraSceneShape& shape : chunk.shapes) {
            const std::string object_definition_name = shape.object_definition_name;
            const SpectraPbrtFileLocation shape_location = shape.location;
            const std::size_t shape_index = this->shapes.size();
            this->shapes.push_back(std::move(shape));
            if (!object_definition_name.empty()) {
                const std::size_t object_definition_index = find_or_create_object_definition(this->object_definitions, object_definition_name, shape_location);
                this->object_definitions[object_definition_index].shape_indices.push_back(shape_index);
            }
        }
        chunk.shapes.clear();
        append_vector(this->object_instances, chunk.object_instances);
        append_vector(this->unsupported_features, chunk.unsupported_features);
    }

    void SpectraScene::set_runtime_metadata(const std::array<int, 2>& resolution, const int samples_per_pixel, const std::array<float, 16>& camera_transform) {
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("PBRT film resolution must be positive");
        if (samples_per_pixel <= 0) throw std::runtime_error("PBRT sampler SPP must be positive");
        this->film_resolution      = resolution;
        this->sampler_sample_count = samples_per_pixel;
        this->camera_from_world    = camera_transform;
    }

    void SpectraScene::unload_noexcept() noexcept {
        this->scene_path.clear();
        this->scene_label = "No Scene";
        this->scene_path_text.clear();
        this->film_resolution      = {0, 0};
        this->camera_from_world    = identity_matrix_array();
        this->sampler_sample_count = 0;
        try {
            std::lock_guard<std::mutex> lock{this->scene_mutex};
            this->pbrt_directives.clear();
            this->pixel_filter = {};
            this->film = {};
            this->sampler = {};
            this->accelerator = {};
            this->integrator = {};
            this->camera = {};
            this->textures.clear();
            this->materials.clear();
            this->mediums.clear();
            this->medium_bindings.clear();
            this->lights.clear();
            this->shapes.clear();
            this->object_definitions.clear();
            this->object_instances.clear();
            this->unsupported_features.clear();
        } catch (...) {
        }
    }

    void SpectraRasterScene::build(const SpectraScene& scene) {
        if (!this->scene_path.empty() || !this->vertices.empty() || !this->indices.empty() || !this->materials.empty() || !this->geometries.empty() || !this->draws.empty() || !this->diagnostics.empty()) throw std::runtime_error("Spectra raster scene is already built");
        if (scene.scene_path.empty()) throw std::runtime_error("Cannot build SpectraRasterScene without a loaded SpectraScene");
        if (scene.pbrt_directives.empty()) throw std::runtime_error("Cannot build SpectraRasterScene before SpectraScene parsing is complete");

        try {
            this->scene_path  = scene.scene_path;
            this->scene_label = scene.scene_label;
            std::map<std::size_t, std::size_t> material_indices{};
            std::map<std::size_t, std::size_t> geometry_indices{};

            for (std::size_t shape_index = 0; shape_index < scene.shapes.size(); ++shape_index) {
                const SpectraSceneShape& shape = scene.shapes[shape_index];
                if (!shape.object_definition_name.empty()) continue;
                append_raster_draw(scene, *this, shape_index, shape.transform, std::numeric_limits<std::size_t>::max(), material_indices, geometry_indices);
            }

            for (std::size_t instance_index = 0; instance_index < scene.object_instances.size(); ++instance_index) {
                const SpectraSceneObjectInstance& object_instance = scene.object_instances[instance_index];
                if (object_instance.animated_transform) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedAnimatedTransform, "object-instance", object_instance.name, "Raster v1 does not support animated object instance transforms", object_instance.location);
                    continue;
                }
                const SpectraSceneObjectDefinition* object_definition = find_object_definition(scene, object_instance.name);
                if (object_definition == nullptr) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedObjectInstance, "object-instance", object_instance.name, "ObjectInstance references an object definition that was not recorded", object_instance.location);
                    continue;
                }
                if (object_definition->shape_indices.empty()) {
                    add_raster_diagnostic(*this, SpectraRasterDiagnosticKind::UnsupportedObjectInstance, "object-instance", object_instance.name, "ObjectInstance references an empty object definition", object_instance.location);
                    continue;
                }
                for (const std::size_t shape_index : object_definition->shape_indices) {
                    if (shape_index >= scene.shapes.size()) throw std::runtime_error("Object definition shape index is out of range");
                    const SpectraSceneShape& shape = scene.shapes[shape_index];
                    const std::array<float, 16> transform = multiply_matrix_arrays(object_instance.transform, shape.transform);
                    append_raster_draw(scene, *this, shape_index, transform, instance_index, material_indices, geometry_indices);
                }
            }
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraRasterScene::unload_noexcept() noexcept {
        this->scene_path.clear();
        this->scene_label = "No Scene";
        this->vertices.clear();
        this->indices.clear();
        this->materials.clear();
        this->geometries.clear();
        this->draws.clear();
        this->diagnostics.clear();
    }

    SpectraPbrtBackendScene::SpectraPbrtBackendScene() : state{std::make_unique<SpectraPbrtBackendSceneState>()} {}

    SpectraPbrtBackendScene::~SpectraPbrtBackendScene() noexcept {
        this->unload_noexcept();
    }

    void SpectraPbrtBackendScene::load(const SpectraScene& spectra_scene, const std::array<int, 2>& resolution) {
        if (this->state == nullptr) throw std::runtime_error("PBRT backend scene state is null");
        if (this->state->scene != nullptr) throw std::runtime_error("PBRT backend scene is already loaded");
        if (spectra_scene.scene_path.empty()) throw std::runtime_error("Cannot build PBRT backend scene without a loaded Spectra scene");
        if (spectra_scene.pbrt_directives.empty()) throw std::runtime_error("Cannot build PBRT backend scene before Spectra scene parsing is complete");
        if (resolution[0] <= 0 || resolution[1] <= 0) throw std::runtime_error("Cannot build PBRT backend scene with a non-positive resolution");
        if (pbrt::Options == nullptr) throw std::runtime_error("Cannot build PBRT backend scene before PBRT runtime is initialized");
        try {
            this->state->scene   = std::make_unique<pbrt::BasicScene>();
            this->state->builder = std::make_unique<SpectraResolutionOverrideSceneBuilder>(this->state->scene.get(), resolution);
            std::vector<std::string> filenames{spectra_scene.scene_path_text};
            pbrt::ParseFiles(this->state->builder.get(), filenames);
        } catch (...) {
            this->unload_noexcept();
            throw;
        }
    }

    void SpectraPbrtBackendScene::unload_noexcept() noexcept {
        if (this->state == nullptr) return;
        this->state->builder.reset();
        this->state->scene.reset();
    }

    [[nodiscard]] void* SpectraPbrtBackendScene::native_basic_scene() {
        if (this->state == nullptr) throw std::runtime_error("PBRT backend scene state is null");
        if (this->state->scene == nullptr) throw std::runtime_error("PBRT backend scene is not loaded");
        return this->state->scene.get();
    }

}
