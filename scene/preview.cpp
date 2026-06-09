module spectra.scene.preview;

import spectra.scene.pbrt;
import std;


namespace spectra::scene {
    namespace {
        struct Bounds {
            Vector3 minimum{};
            Vector3 maximum{};
            bool valid{false};
        };

        [[nodiscard]] SceneSourceLocation to_scene_source(const SceneSourceLocation& source) {
            return SceneSourceLocation{
                .filename = source.filename,
                .line     = source.line,
                .column   = source.column,
            };
        }

        [[nodiscard]] float matrix_value(const std::array<float, 16>& matrix, const std::size_t row, const std::size_t column) {
            return matrix.at(row * 4u + column);
        }

        [[nodiscard]] float dot(const Vector3 lhs, const Vector3 rhs) {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        [[nodiscard]] Vector3 operator-(const Vector3 lhs, const Vector3 rhs) {
            return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        [[nodiscard]] float length(const Vector3 value) {
            return std::sqrt(dot(value, value));
        }

        [[nodiscard]] Vector3 normalize(const Vector3 value, const std::string_view context) {
            const float vector_length = length(value);
            if (!std::isfinite(vector_length) || vector_length <= 0.0f) throw std::runtime_error(std::format("{} contains a zero-length vector", context));
            return Vector3{value.x / vector_length, value.y / vector_length, value.z / vector_length};
        }

        void include_point(Bounds& bounds, const Vector3 point) {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z)) throw std::runtime_error("PBRT preview mesh contains a non-finite point");
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid   = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        }

        [[nodiscard]] Vector3 center(const Bounds& bounds) {
            if (!bounds.valid) throw std::runtime_error("Cannot compute PBRT preview bounds center without mesh geometry");
            return Vector3{
                (bounds.minimum.x + bounds.maximum.x) * 0.5f,
                (bounds.minimum.y + bounds.maximum.y) * 0.5f,
                (bounds.minimum.z + bounds.maximum.z) * 0.5f,
            };
        }

        [[nodiscard]] float radius(const Bounds& bounds) {
            if (!bounds.valid) throw std::runtime_error("Cannot compute PBRT preview bounds radius without mesh geometry");
            return length(Vector3{
                (bounds.maximum.x - bounds.minimum.x) * 0.5f,
                (bounds.maximum.y - bounds.minimum.y) * 0.5f,
                (bounds.maximum.z - bounds.minimum.z) * 0.5f,
            });
        }

        [[nodiscard]] const std::vector<float>& required_float_values(const PbrtSceneEntity& entity, const std::string_view type, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != type || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain float values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"{} {}\"", context, type, name));
        }

        [[nodiscard]] const std::vector<int>& required_int_values(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != "integer" || parameter.name != name) continue;
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain integer values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"integer {}\"", context, name));
        }

        [[nodiscard]] Vector3 required_rgb_value(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            const std::vector<float>& values = required_float_values(entity, "rgb", name, context);
            if (values.size() != 3u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly three RGB values", context, name));
            return Vector3{values.at(0), values.at(1), values.at(2)};
        }

        [[nodiscard]] float required_one_float_value(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != "float" || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one float", context, name));
                return values->front();
            }
            throw std::runtime_error(std::format("{} requires \"float {}\"", context, name));
        }

        [[nodiscard]] Vector3 transform_point(const PbrtSceneTransform& transform, const Vector3 point) {
            const std::array<float, 16>& matrix = transform.matrix;
            const float x = matrix_value(matrix, 0u, 0u) * point.x + matrix_value(matrix, 0u, 1u) * point.y + matrix_value(matrix, 0u, 2u) * point.z + matrix_value(matrix, 0u, 3u);
            const float y = matrix_value(matrix, 1u, 0u) * point.x + matrix_value(matrix, 1u, 1u) * point.y + matrix_value(matrix, 1u, 2u) * point.z + matrix_value(matrix, 1u, 3u);
            const float z = matrix_value(matrix, 2u, 0u) * point.x + matrix_value(matrix, 2u, 1u) * point.y + matrix_value(matrix, 2u, 2u) * point.z + matrix_value(matrix, 2u, 3u);
            const float w = matrix_value(matrix, 3u, 0u) * point.x + matrix_value(matrix, 3u, 1u) * point.y + matrix_value(matrix, 3u, 2u) * point.z + matrix_value(matrix, 3u, 3u);
            if (!std::isfinite(w) || w == 0.0f) throw std::runtime_error("PBRT preview mesh transform produced an invalid homogeneous point");
            return Vector3{x / w, y / w, z / w};
        }

        [[nodiscard]] Vector3 transform_normal(const PbrtSceneTransform& transform, const Vector3 normal) {
            const std::array<float, 16>& inverse = transform.inverse;
            const Vector3 transformed{
                matrix_value(inverse, 0u, 0u) * normal.x + matrix_value(inverse, 1u, 0u) * normal.y + matrix_value(inverse, 2u, 0u) * normal.z,
                matrix_value(inverse, 0u, 1u) * normal.x + matrix_value(inverse, 1u, 1u) * normal.y + matrix_value(inverse, 2u, 1u) * normal.z,
                matrix_value(inverse, 0u, 2u) * normal.x + matrix_value(inverse, 1u, 2u) * normal.y + matrix_value(inverse, 2u, 2u) * normal.z,
            };
            return normalize(transformed, "PBRT preview mesh normal transform");
        }

        void require_static_transform(const PbrtSceneTransformSet& transform, const std::string_view context) {
            if (transform.animated) throw std::runtime_error(std::format("{} uses animated transforms, which are not supported by the PBRT preview scene loader", context));
        }

        [[nodiscard]] std::string object_source_prefix(const PbrtSceneSnapshot& scene) {
            if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
            if (scene.source.starts_with("pbrt://")) return scene.source;
            return std::format("pbrt://{}", scene.source);
        }

        [[nodiscard]] std::string make_shape_object_name(const PbrtSceneSnapshot& scene, const std::size_t shape_index) {
            return std::format("{}#shape:{}", object_source_prefix(scene), shape_index);
        }

        [[nodiscard]] std::set<std::string> referenced_shape_material_names(const PbrtSceneSnapshot& scene) {
            std::set<std::string> names{};
            for (const PbrtSceneShape& shape : scene.shapes) {
                if (shape.materialName.empty()) throw std::runtime_error("PBRT preview shape references an empty material name");
                names.insert(shape.materialName);
            }
            return names;
        }

        [[nodiscard]] std::map<std::string, std::size_t> append_materials(const PbrtSceneSnapshot& scene, const std::set<std::string>& referenced_material_names, SceneDocument& document) {
            std::map<std::string, std::size_t> material_indices{};
            for (const PbrtSceneMaterial& material : scene.materials) {
                if (!referenced_material_names.contains(material.name)) continue;
                if (material.name.empty()) throw std::runtime_error("PBRT preview material name must not be empty");
                if (material.entity.type != "diffuse") throw std::runtime_error(std::format("PBRT preview material \"{}\" uses unsupported type \"{}\"", material.name, material.entity.type));
                const Vector3 reflectance = required_rgb_value(material.entity, "reflectance", std::format("PBRT preview material \"{}\"", material.name));
                const bool inserted = material_indices.emplace(material.name, document.materials.size()).second;
                if (!inserted) throw std::runtime_error(std::format("PBRT preview material \"{}\" is duplicated", material.name));
                document.materials.push_back(SceneMaterial{
                    .name      = material.name,
                    .baseColor = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f},
                    .roughness = 0.72f,
                });
            }
            for (const std::string& material_name : referenced_material_names) {
                if (!material_indices.contains(material_name)) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", material_name));
            }
            return material_indices;
        }

        [[nodiscard]] SceneMaterial& material_for_name(SceneDocument& document, const std::map<std::string, std::size_t>& material_indices, const std::string& name) {
            const std::map<std::string, std::size_t>::const_iterator iter = material_indices.find(name);
            if (iter == material_indices.end()) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", name));
            return document.materials.at(iter->second);
        }

        void apply_area_light_material(const PbrtSceneShape& shape, const std::size_t shape_index, SceneDocument& document, const std::map<std::string, std::size_t>& material_indices) {
            if (!shape.areaLight.has_value()) return;
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.areaLight->entity.type != "diffuse") throw std::runtime_error(std::format("{} uses unsupported area light type \"{}\"", context, shape.areaLight->entity.type));
            SceneMaterial& material = material_for_name(document, material_indices, shape.materialName);
            const Vector3 radiance = required_rgb_value(shape.areaLight->entity, "L", context);
            if (material.emissionStrength != 0.0f && (material.emissionColor.x != radiance.x || material.emissionColor.y != radiance.y || material.emissionColor.z != radiance.z)) throw std::runtime_error(std::format("PBRT preview material \"{}\" is reused by area lights with different radiance", material.name));
            material.emissionColor    = radiance;
            material.emissionStrength = 1.0f;
        }

        [[nodiscard]] SceneMesh make_mesh(const PbrtSceneSnapshot& scene, const PbrtSceneShape& shape, const std::size_t shape_index, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.entity.type != "trianglemesh") throw std::runtime_error(std::format("PBRT preview scene loader only supports trianglemesh shapes, got \"{}\"", shape.entity.type));
            require_static_transform(shape.transform, context);
            if (shape.reverseOrientation) throw std::runtime_error(std::format("{} uses ReverseOrientation, which is not supported by the PBRT preview scene loader", context));
            if (!shape.mediumInterface.inside.empty() || !shape.mediumInterface.outside.empty()) throw std::runtime_error(std::format("{} uses MediumInterface, which is not supported by the PBRT preview scene loader", context));
            if (!material_indices.contains(shape.materialName)) throw std::runtime_error(std::format("{} references unknown material \"{}\"", context, shape.materialName));
            const std::string object_name = make_shape_object_name(scene, shape_index);
            const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
            const std::vector<float>& normals = required_float_values(shape.entity, "normal", "N", context);
            const std::vector<int>& indices = required_int_values(shape.entity, "indices", context);
            if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid point3 P data", object_name));
            if (normals.size() != positions.size()) throw std::runtime_error(std::format("PBRT preview shape \"{}\" normal count does not match position count", object_name));
            if (indices.empty() || indices.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid triangle index data", object_name));

            SceneMesh mesh{
                .name         = object_name,
                .materialName = shape.materialName,
                .dynamic      = false,
                .source       = to_scene_source(shape.entity.source),
            };
            const std::size_t vertex_count = positions.size() / 3u;
            mesh.positions.reserve(vertex_count);
            mesh.normals.reserve(vertex_count);
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Vector3 point{positions.at(vertex_index * 3u), positions.at(vertex_index * 3u + 1u), positions.at(vertex_index * 3u + 2u)};
                const Vector3 normal{normals.at(vertex_index * 3u), normals.at(vertex_index * 3u + 1u), normals.at(vertex_index * 3u + 2u)};
                const Vector3 transformed_point = transform_point(shape.transform.start, point);
                mesh.positions.push_back(transformed_point);
                mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                include_point(bounds, transformed_point);
            }
            mesh.indices.reserve(indices.size());
            for (const int index : indices) {
                if (index < 0) throw std::runtime_error(std::format("PBRT preview shape \"{}\" contains a negative vertex index", object_name));
                const std::uint32_t converted_index = static_cast<std::uint32_t>(index);
                if (converted_index >= vertex_count) throw std::runtime_error(std::format("PBRT preview shape \"{}\" contains an out-of-range vertex index", object_name));
                mesh.indices.push_back(converted_index);
            }
            return mesh;
        }

        void append_meshes(const PbrtSceneSnapshot& scene, SceneDocument& document, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            std::map<std::string, bool> material_used_by_area_light{};
            for (std::size_t shape_index = 0; shape_index < scene.shapes.size(); ++shape_index) {
                const PbrtSceneShape& shape = scene.shapes.at(shape_index);
                const bool is_area_light = shape.areaLight.has_value();
                const std::pair<std::map<std::string, bool>::iterator, bool> material_usage = material_used_by_area_light.emplace(shape.materialName, is_area_light);
                if (!material_usage.second && material_usage.first->second != is_area_light) throw std::runtime_error(std::format("PBRT preview material \"{}\" is shared by emissive and non-emissive shapes", shape.materialName));
                apply_area_light_material(shape, shape_index, document, material_indices);
                SceneMesh mesh = make_mesh(scene, shape, shape_index, material_indices, bounds);
                document.meshes.push_back(std::move(mesh));
            }
            if (document.meshes.empty()) throw std::runtime_error("PBRT preview scene loader did not find any trianglemesh shapes");
        }

        [[nodiscard]] SceneCamera make_camera(const PbrtSceneSnapshot& scene, const Bounds& bounds) {
            if (scene.renderSettings.camera.type != "perspective") throw std::runtime_error(std::format("PBRT preview scene loader only supports perspective cameras, got \"{}\"", scene.renderSettings.camera.type));
            require_static_transform(scene.renderSettings.cameraTransform, "PBRT preview camera");
            const std::array<float, 16>& world_from_camera = scene.renderSettings.cameraTransform.start.matrix;
            const Vector3 eye{matrix_value(world_from_camera, 0u, 3u), matrix_value(world_from_camera, 1u, 3u), matrix_value(world_from_camera, 2u, 3u)};
            const Vector3 up = normalize(Vector3{matrix_value(world_from_camera, 0u, 1u), matrix_value(world_from_camera, 1u, 1u), matrix_value(world_from_camera, 2u, 1u)}, "PBRT preview camera up vector");
            const Vector3 target = center(bounds);
            const float scene_radius = radius(bounds);
            const float camera_distance = length(eye - target);
            const float far_plane = std::max(20.0f, camera_distance + scene_radius * 4.0f);
            return SceneCamera{
                .name               = "camera.main",
                .transform          = Transform{.position = eye},
                .target             = target,
                .up                 = up,
                .verticalFovDegrees = required_one_float_value(scene.renderSettings.camera, "fov", "PBRT preview camera"),
                .nearPlane          = 0.01f,
                .farPlane           = far_plane,
                .source             = to_scene_source(scene.renderSettings.camera.source),
            };
        }

        void reject_unsupported_scene_content(const PbrtSceneSnapshot& scene) {
            if (!scene.textures.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT textures");
            if (!scene.media.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT media");
            if (!scene.lights.empty()) throw std::runtime_error("PBRT preview scene loader only supports mesh area lights");
            if (!scene.objectDefinitions.empty() || !scene.objectInstances.empty()) throw std::runtime_error("PBRT preview scene loader only supports top-level trianglemesh shapes");
        }
    } // namespace

    SceneDocument MakePreviewSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene) {
        reject_unsupported_scene_content(scene);
        if (scene.revision.value == 0u) throw std::runtime_error("PBRT preview scene revision must not be zero");
        if (scene.name.empty()) throw std::runtime_error("PBRT preview scene name must not be empty");
        if (scene.title.empty()) throw std::runtime_error("PBRT preview scene title must not be empty");
        if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
        SceneDocument document{
            .revision        = SceneRevision{scene.revision.value},
            .name            = scene.name,
            .title           = scene.title,
            .source          = object_source_prefix(scene),
            .framesPerSecond = 24.0,
            .timelineEnabled = false,
        };
        Bounds bounds{};
        const std::set<std::string> referenced_material_names = referenced_shape_material_names(scene);
        const std::map<std::string, std::size_t> material_indices = append_materials(scene, referenced_material_names, document);
        append_meshes(scene, document, material_indices, bounds);
        document.camera = make_camera(scene, bounds);
        document.lights.push_back(SceneLight{
            .name      = "preview.key",
            .kind      = SceneLightKind::Directional,
            .color     = Vector3{1.0f, 0.96f, 0.86f},
            .intensity = 1.8f,
        });
        return document;
    }

    SceneDocument LoadPreviewSceneDocumentFromPbrt(const std::string_view scene_id) {
        PbrtSceneWorkspace workspace = BuildPbrtScene(scene_id);
        const std::shared_ptr<const PbrtSceneSnapshot> scene = workspace.snapshot();
        if (scene == nullptr) throw std::runtime_error("PBRT workspace did not produce a scene snapshot");
        return MakePreviewSceneDocumentFromPbrt(*scene);
    }

} // namespace spectra::scene

