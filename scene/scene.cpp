module;

#ifndef SPECTRA_SCENES_ROOT
#error "SPECTRA_SCENES_ROOT must point to the project-local scene asset directory."
#endif

#include <zlib.h>

module spectra.scene;

import std;

namespace spectra::scene {
    [[nodiscard]] Scene::Document make_preview_document_from_pbrt(const Scene::ResolvedScene& scene);
    [[nodiscard]] Scene::Info describe_scene(const Scene::ResolvedScene& scene);

    namespace {
        void validate_scene_id(const std::string_view scene_id) {
            if (scene_id.empty()) throw std::runtime_error("Scene camera workspace requires a non-empty scene id");
        }

        void validate_camera_state(const Scene::CameraState& state) {
            if (!is_finite(state.eye)) throw std::runtime_error("Scene camera eye must be finite");
            if (!is_finite(state.target)) throw std::runtime_error("Scene camera target must be finite");
            if (!is_finite(state.up)) throw std::runtime_error("Scene camera up vector must be finite");
            const Vector3 view = state.target - state.eye;
            if (!(length_squared(view) > 1.0e-12f)) throw std::runtime_error("Scene camera eye and target must not overlap");
            if (!(length_squared(state.up) > 1.0e-12f)) throw std::runtime_error("Scene camera up vector must not be zero");
            if (!(length_squared(cross(view, state.up)) > 1.0e-12f)) throw std::runtime_error("Scene camera up vector must not be parallel to the view direction");
            if (!std::isfinite(state.vertical_fov_degrees) || !(state.vertical_fov_degrees > 0.0f) || !(state.vertical_fov_degrees < 180.0f)) throw std::runtime_error("Scene camera vertical FOV must be inside (0, 180)");
        }

        template <typename Item>
        void validate_unique_scene_item_names(const std::vector<Item>& items, const std::string_view layer, const std::string_view kind) {
            std::set<std::string_view> names{};
            for (const Item& item : items) {
                if (item.name.empty()) throw std::runtime_error(std::format("{} {} item names must not be empty", layer, kind));
                if (!names.insert(std::string_view{item.name}).second) throw std::runtime_error(std::format("{} {} item \"{}\" is duplicated", layer, kind, item.name));
            }
        }

        template <typename Item>
        [[nodiscard]] std::vector<Item> resolve_scene_items(const std::vector<Item>& document_items, const std::vector<Item>& frame_items, const std::string_view kind) {
            validate_unique_scene_item_names(document_items, "Scene document", kind);
            validate_unique_scene_item_names(frame_items, "Scene frame", kind);

            std::vector<Item> resolved = document_items;
            std::map<std::string, std::size_t> document_indices{};
            for (std::size_t index = 0; index < resolved.size(); ++index) document_indices.emplace(resolved.at(index).name, index);

            for (const Item& frame_item : frame_items) {
                const std::map<std::string, std::size_t>::const_iterator found = document_indices.find(frame_item.name);
                if (found != document_indices.end()) {
                    resolved.at(found->second) = frame_item;
                    continue;
                }
                document_indices.emplace(frame_item.name, resolved.size());
                resolved.push_back(frame_item);
            }
            return resolved;
        }

        [[nodiscard]] bool contains_name(const std::set<std::string>& values, const std::string& value) {
            return values.find(value) != values.end();
        }

        [[nodiscard]] std::string scene_source_string(const Scene::SourceLocation& source) {
            if (source.filename.empty()) return "<generated>";
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[noreturn]] void throw_scene_validation_error(const Scene::SourceLocation& source, const std::string_view message) {
            throw std::runtime_error(std::format("{}: {}", scene_source_string(source), message));
        }

        void require_supported_entity(const Scene::Entity& entity, const std::set<std::string>& supported, const std::string_view kind) {
            if (entity.type.empty()) throw_scene_validation_error(entity.source, std::format("Scene {} type must not be empty", kind));
            if (!contains_name(supported, entity.type)) throw_scene_validation_error(entity.source, std::format("Scene pathtracer backend does not support {} type \"{}\"", kind, entity.type));
        }

        void require_static_scene_transform(const SceneTransformSet& transform, const Scene::SourceLocation& source, const std::string_view owner) {
            if (transform.animated) throw_scene_validation_error(source, std::format("{} uses animated transforms, which are not supported by the canonical pathtracer backend", owner));
        }

        [[nodiscard]] std::string scene_string_parameter(const std::vector<Scene::Parameter>& parameters, const std::string_view name) {
            for (const Scene::Parameter& parameter : parameters) {
                if (parameter.name != name) continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values != nullptr && !values->empty()) return values->front();
            }
            return {};
        }

        void require_unique_canonical_name(std::set<std::string>* names, const std::string& name, const Scene::SourceLocation& source, const std::string_view kind) {
            if (names == nullptr) throw std::runtime_error("Scene canonical validation requires a name set");
            if (name.empty()) throw_scene_validation_error(source, std::format("Scene {} name must not be empty", kind));
            if (!names->insert(name).second) throw_scene_validation_error(source, std::format("Scene {} \"{}\" is duplicated", kind, name));
        }

        void validate_canonical_scene(const Scene::ResolvedScene& scene) {
            static const std::set<std::string> supported_filters{"box", "gaussian", "mitchell", "sinc", "triangle"};
            static const std::set<std::string> supported_films{"rgb", "gbuffer", "spectral"};
            static const std::set<std::string> supported_cameras{"perspective", "orthographic", "realistic", "spherical"};
            static const std::set<std::string> supported_samplers{"zsobol", "paddedsobol", "halton", "sobol", "pmj02bn", "independent", "stratified"};
            static const std::set<std::string> supported_integrators{"path", "volpath"};
            static const std::set<std::string> supported_accelerators{"bvh"};
            static const std::set<std::string> supported_materials{"none", "interface", "diffuse", "coateddiffuse", "coatedconductor", "diffusetransmission", "dielectric", "thindielectric", "hair", "conductor", "measured", "subsurface", "mix"};
            static const std::set<std::string> supported_textures{"constant", "scale", "mix", "directionmix", "bilerp", "imagemap", "checkerboard", "dots", "fbm", "wrinkled", "windy", "marble", "ptex"};
            static const std::set<std::string> supported_media{"homogeneous", "uniformgrid", "rgbgrid", "cloud", "nanovdb"};
            static const std::set<std::string> supported_lights{"point", "spot", "goniometric", "projection", "distant", "infinite"};
            static const std::set<std::string> supported_area_lights{"diffuse"};
            static const std::set<std::string> supported_shapes{"sphere", "cylinder", "disk", "bilinearmesh", "curve", "trianglemesh", "plymesh", "loopsubdiv"};
            static const std::set<std::string> supported_light_samplers{"uniform", "power", "bvh", "exhaustive"};

            if (scene.revision.value == 0u) throw std::runtime_error("Scene canonical revision must not be zero");
            if (scene.name.empty()) throw std::runtime_error("Scene canonical name must not be empty");
            if (scene.title.empty()) throw std::runtime_error("Scene canonical title must not be empty");
            if (scene.source.empty()) throw std::runtime_error("Scene canonical source must not be empty");

            require_supported_entity(scene.render_settings.filter, supported_filters, "pixel filter");
            require_supported_entity(scene.render_settings.film, supported_films, "film");
            require_supported_entity(scene.render_settings.camera, supported_cameras, "camera");
            require_supported_entity(scene.render_settings.sampler, supported_samplers, "sampler");
            require_supported_entity(scene.render_settings.integrator, supported_integrators, "integrator");
            require_supported_entity(scene.render_settings.accelerator, supported_accelerators, "accelerator");
            require_static_scene_transform(scene.render_settings.camera_transform, scene.render_settings.camera.source, "Scene camera");
            if (!scene.render_settings.options.empty()) throw_scene_validation_error(scene.render_settings.options.front().source, std::format("Scene Option \"{}\" is represented but not supported by the canonical pathtracer backend", scene.render_settings.options.front().name));

            const std::string light_sampler = scene_string_parameter(scene.render_settings.integrator.parameters, "lightsampler");
            if (!light_sampler.empty() && !contains_name(supported_light_samplers, light_sampler)) throw_scene_validation_error(scene.render_settings.integrator.source, std::format("Scene pathtracer backend does not support light sampler \"{}\"", light_sampler));

            std::set<std::string> material_names{};
            for (const Scene::Material& material : scene.materials) {
                require_unique_canonical_name(&material_names, material.name, material.entity.source, "material");
                require_supported_entity(material.entity, supported_materials, "material");
            }

            std::set<std::string> medium_names{};
            for (const Scene::Medium& medium : scene.media) {
                require_unique_canonical_name(&medium_names, medium.name, medium.entity.source, "medium");
                require_supported_entity(medium.entity, supported_media, "medium");
                require_static_scene_transform(medium.transform, medium.entity.source, std::format("Scene medium \"{}\"", medium.name));
            }

            std::set<std::string> texture_names{};
            for (const Scene::Texture& texture : scene.textures) {
                require_unique_canonical_name(&texture_names, texture.name, texture.entity.source, "texture");
                if (texture.kind != "float" && texture.kind != "spectrum") throw_scene_validation_error(texture.entity.source, std::format("Scene pathtracer backend does not support texture value kind \"{}\"", texture.kind));
                require_supported_entity(texture.entity, supported_textures, "texture");
                if (texture.kind == "float" && texture.entity.type == "marble") throw_scene_validation_error(texture.entity.source, "\"marble\" is only a spectrum texture in the canonical pathtracer backend");
                if (texture.kind == "spectrum" && (texture.entity.type == "fbm" || texture.entity.type == "wrinkled" || texture.entity.type == "windy")) throw_scene_validation_error(texture.entity.source, std::format("\"{}\" is only a float texture in the canonical pathtracer backend", texture.entity.type));
                require_static_scene_transform(texture.transform, texture.entity.source, std::format("Scene texture \"{}\"", texture.name));
            }

            const auto validate_shape = [&material_names, &medium_names](const Scene::Shape& shape, const std::string_view owner) {
                if (shape.name.empty()) throw_scene_validation_error(shape.entity.source, std::format("{} name must not be empty", owner));
                require_supported_entity(shape.entity, supported_shapes, "shape");
                require_static_scene_transform(shape.transform, shape.entity.source, owner);
                if (shape.material_name.empty() || !contains_name(material_names, shape.material_name)) throw_scene_validation_error(shape.entity.source, std::format("{} references unknown material \"{}\"", owner, shape.material_name));
                if (!shape.medium_interface.inside.empty() && !contains_name(medium_names, shape.medium_interface.inside)) throw_scene_validation_error(shape.entity.source, std::format("{} references unknown inside medium \"{}\"", owner, shape.medium_interface.inside));
                if (!shape.medium_interface.outside.empty() && !contains_name(medium_names, shape.medium_interface.outside)) throw_scene_validation_error(shape.entity.source, std::format("{} references unknown outside medium \"{}\"", owner, shape.medium_interface.outside));
                if (shape.area_light.has_value()) require_supported_entity(shape.area_light->entity, supported_area_lights, "area light");
            };

            std::set<std::string> shape_names{};
            for (const Scene::Shape& shape : scene.shapes) {
                require_unique_canonical_name(&shape_names, shape.name, shape.entity.source, "shape");
                validate_shape(shape, std::format("Scene shape \"{}\"", shape.name));
            }

            std::set<std::string> light_names{};
            for (const Scene::Light& light : scene.lights) {
                require_unique_canonical_name(&light_names, light.name, light.entity.source, "light");
                require_supported_entity(light.entity, supported_lights, "light");
                require_static_scene_transform(light.transform, light.entity.source, std::format("Scene light \"{}\"", light.name));
                if (!light.medium.empty() && !contains_name(medium_names, light.medium)) throw_scene_validation_error(light.entity.source, std::format("Scene light \"{}\" references unknown medium \"{}\"", light.name, light.medium));
            }

            std::set<std::string> object_definition_names{};
            for (const Scene::ObjectDefinition& definition : scene.object_definitions) {
                require_unique_canonical_name(&object_definition_names, definition.name, definition.source, "object definition");
                std::set<std::string> definition_shape_names{};
                for (const Scene::Shape& shape : definition.shapes) {
                    require_unique_canonical_name(&definition_shape_names, shape.name, shape.entity.source, "object definition shape");
                    validate_shape(shape, std::format("Scene object definition \"{}\" shape", definition.name));
                    if (shape.area_light.has_value()) throw_scene_validation_error(shape.entity.source, std::format("Scene object definition \"{}\" contains an area light shape; instanced area lights are not supported by the canonical pathtracer backend", definition.name));
                }
            }

            std::set<std::string> object_instance_names{};
            for (const Scene::ObjectInstance& instance : scene.object_instances) {
                require_unique_canonical_name(&object_instance_names, instance.name, instance.source, "object instance");
                if (!contains_name(object_definition_names, instance.definition_name)) throw_scene_validation_error(instance.source, std::format("Scene object instance references unknown definition \"{}\"", instance.definition_name));
                require_static_scene_transform(instance.transform, instance.source, std::format("Scene object instance \"{}\"", instance.name));
            }
        }

        [[nodiscard]] Scene::Parameter float_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "float", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter integer_parameter(std::string name, std::vector<int> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "integer", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter rgb_parameter(std::string name, const Vector3 value, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "rgb", .name = std::move(name), .values = std::vector<float>{value.x, value.y, value.z}, .source = source};
        }

        [[nodiscard]] Scene::Parameter point3_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "point3", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter normal_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "normal", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter string_parameter(std::string name, std::vector<std::string> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "string", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Quaternion normalized_quaternion(const Quaternion value, const std::string_view context) {
            const float length_squared_value = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
            if (!std::isfinite(length_squared_value) || length_squared_value <= 1.0e-12f) throw std::runtime_error(std::format("{} has an invalid rotation quaternion", context));
            const float inv_length = 1.0f / std::sqrt(length_squared_value);
            return Quaternion{value.x * inv_length, value.y * inv_length, value.z * inv_length, value.w * inv_length};
        }

        [[nodiscard]] Vector3 rotate_vector(const Quaternion rotation, const Vector3 value) {
            const Vector3 qv{rotation.x, rotation.y, rotation.z};
            const Vector3 uv = cross(qv, value);
            const Vector3 uuv = cross(qv, uv);
            return value + ((uv * rotation.w) + uuv) * 2.0f;
        }

        [[nodiscard]] Vector3 transform_point(const Transform& transform, const Vector3 point, const std::string_view context) {
            if (!is_finite(transform.position)) throw std::runtime_error(std::format("{} has a non-finite transform position", context));
            if (!is_finite(transform.scale)) throw std::runtime_error(std::format("{} has a non-finite transform scale", context));
            if (transform.scale.x == 0.0f || transform.scale.y == 0.0f || transform.scale.z == 0.0f) throw std::runtime_error(std::format("{} has a zero transform scale component", context));
            const Quaternion rotation = normalized_quaternion(transform.rotation, context);
            return transform.position + rotate_vector(rotation, Vector3{point.x * transform.scale.x, point.y * transform.scale.y, point.z * transform.scale.z});
        }

        [[nodiscard]] Vector3 transform_normal(const Transform& transform, const Vector3 normal, const std::string_view context) {
            if (!is_finite(transform.scale)) throw std::runtime_error(std::format("{} has a non-finite transform scale", context));
            if (transform.scale.x == 0.0f || transform.scale.y == 0.0f || transform.scale.z == 0.0f) throw std::runtime_error(std::format("{} has a zero transform scale component", context));
            const Quaternion rotation = normalized_quaternion(transform.rotation, context);
            return normalize(rotate_vector(rotation, Vector3{normal.x / transform.scale.x, normal.y / transform.scale.y, normal.z / transform.scale.z}), context);
        }

        [[nodiscard]] SceneTransform make_translation_transform(const Vector3 position) {
            SceneTransform transform{};
            transform.matrix[3] = position.x;
            transform.matrix[7] = position.y;
            transform.matrix[11] = position.z;
            transform.inverse[3] = -position.x;
            transform.inverse[7] = -position.y;
            transform.inverse[11] = -position.z;
            return transform;
        }

        [[nodiscard]] float preview_scalar(const Vector4 color) {
            return std::max(0.0f, (color.x + color.y + color.z) / 3.0f);
        }

        [[nodiscard]] const Scene::PreviewMaterial* find_preview_material(const Scene::Document& document, const std::string& name) {
            for (const Scene::PreviewMaterial& material : document.materials)
                if (material.name == name) return &material;
            return nullptr;
        }

        void append_canonical_materials(Scene::ResolvedScene& scene, const Scene::Document& document, const bool needs_volume_interface_material) {
            std::set<std::string> names{};
            for (const Scene::PreviewMaterial& material : document.materials) {
                if (material.name.empty()) throw std::runtime_error("Preview material name must not be empty when building canonical scene");
                if (!names.insert(material.name).second) throw std::runtime_error(std::format("Preview material \"{}\" is duplicated when building canonical scene", material.name));
                if (material.surface_kind == Scene::PreviewSurfaceKind::Volume) continue;
                const Vector3 reflectance{std::max(0.0f, material.base_color.x), std::max(0.0f, material.base_color.y), std::max(0.0f, material.base_color.z)};
                scene.materials.push_back(Scene::Material{
                    .name = material.name,
                    .entity = Scene::Entity{
                        .type = "diffuse",
                        .parameters = {rgb_parameter("reflectance", reflectance, {})},
                    },
                });
            }
            if (needs_volume_interface_material) {
                constexpr std::string_view volume_interface_material_name = "__spectra_volume_interface";
                if (names.contains(std::string{volume_interface_material_name})) throw std::runtime_error(std::format("Preview material \"{}\" conflicts with a reserved canonical volume material name", volume_interface_material_name));
                scene.materials.push_back(Scene::Material{
                    .name = std::string{volume_interface_material_name},
                    .entity = Scene::Entity{.type = "interface"},
                });
            }
        }

        void append_mesh_shape(Scene::ResolvedScene& scene, const Scene::Mesh& mesh) {
            if (mesh.name.empty()) throw std::runtime_error("Preview mesh name must not be empty when building canonical scene");
            if (mesh.material_name.empty()) throw std::runtime_error(std::format("Preview mesh \"{}\" material name must not be empty when building canonical scene", mesh.name));
            if (mesh.positions.empty()) throw std::runtime_error(std::format("Preview mesh \"{}\" has no positions when building canonical scene", mesh.name));
            if (mesh.normals.size() != mesh.positions.size()) throw std::runtime_error(std::format("Preview mesh \"{}\" normal count does not match position count when building canonical scene", mesh.name));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Preview mesh \"{}\" has invalid triangle indices when building canonical scene", mesh.name));
            std::vector<float> positions{};
            std::vector<float> normals{};
            std::vector<int> indices{};
            positions.reserve(mesh.positions.size() * 3u);
            normals.reserve(mesh.normals.size() * 3u);
            indices.reserve(mesh.indices.size());
            const std::string context = std::format("Preview mesh \"{}\"", mesh.name);
            for (std::size_t index = 0u; index < mesh.positions.size(); ++index) {
                const Vector3 point = transform_point(mesh.transform, mesh.positions.at(index), context);
                const Vector3 normal = transform_normal(mesh.transform, mesh.normals.at(index), context);
                positions.insert(positions.end(), {point.x, point.y, point.z});
                normals.insert(normals.end(), {normal.x, normal.y, normal.z});
            }
            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.positions.size()) throw std::runtime_error(std::format("Preview mesh \"{}\" has an out-of-range triangle index when building canonical scene", mesh.name));
                if (index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error(std::format("Preview mesh \"{}\" triangle index exceeds PBRT integer range", mesh.name));
                indices.push_back(static_cast<int>(index));
            }
            scene.shapes.push_back(Scene::Shape{
                .name = mesh.name,
                .entity = Scene::Entity{
                    .type = "trianglemesh",
                    .parameters = {
                        point3_parameter("P", std::move(positions), mesh.source),
                        normal_parameter("N", std::move(normals), mesh.source),
                        integer_parameter("indices", std::move(indices), mesh.source),
                    },
                    .source = mesh.source,
                },
                .material_name = mesh.material_name,
            });
        }

        void append_point_cloud_shapes(Scene::ResolvedScene& scene, const Scene::Document& document, const Scene::PointCloud& point_cloud) {
            if (point_cloud.name.empty()) throw std::runtime_error("Preview point cloud name must not be empty when building canonical scene");
            if (point_cloud.positions.size() != point_cloud.radii.size()) throw std::runtime_error(std::format("Preview point cloud \"{}\" radius count does not match point count when building canonical scene", point_cloud.name));
            const Scene::PreviewMaterial* material = find_preview_material(document, point_cloud.material_name);
            if (material == nullptr) throw std::runtime_error(std::format("Preview point cloud \"{}\" references unknown material \"{}\"", point_cloud.name, point_cloud.material_name));
            const float material_scale = std::max(1.0e-4f, preview_scalar(material->base_color));
            const std::string context = std::format("Preview point cloud \"{}\"", point_cloud.name);
            for (std::size_t index = 0u; index < point_cloud.positions.size(); ++index) {
                const float radius = point_cloud.radii.at(index);
                if (!std::isfinite(radius) || radius <= 0.0f) throw std::runtime_error(std::format("Preview point cloud \"{}\" has an invalid radius", point_cloud.name));
                const Vector3 point = transform_point(point_cloud.transform, point_cloud.positions.at(index), context);
                const float point_scale = point_cloud.colors.size() == point_cloud.positions.size() ? std::max(1.0e-4f, preview_scalar(point_cloud.colors.at(index))) : 1.0f;
                const std::string material_name = std::format("{}.__point_material_{}", point_cloud.name, index);
                scene.materials.push_back(Scene::Material{
                    .name = material_name,
                    .entity = Scene::Entity{
                        .type = "diffuse",
                        .parameters = {rgb_parameter("reflectance", Vector3{material_scale * point_scale, material_scale * point_scale, material_scale * point_scale}, point_cloud.source)},
                        .source = point_cloud.source,
                    },
                });
                scene.shapes.push_back(Scene::Shape{
                    .name = std::format("{}.__point_{}", point_cloud.name, index),
                    .entity = Scene::Entity{
                        .type = "sphere",
                        .parameters = {float_parameter("radius", {radius}, point_cloud.source)},
                        .source = point_cloud.source,
                    },
                    .transform = SceneTransformSet{.start = make_translation_transform(point), .end = make_translation_transform(point)},
                    .material_name = material_name,
                });
            }
        }

        [[nodiscard]] const Scene::VolumeChannel* find_volume_channel(const Scene::VolumeGrid& volume, const std::string_view name) {
            for (const Scene::VolumeChannel& channel : volume.channels)
                if (channel.name == name) return &channel;
            return nullptr;
        }

        void append_volume(Scene::ResolvedScene& scene, const Scene::Document& document, const Scene::VolumeGrid& volume) {
            if (volume.name.empty()) throw std::runtime_error("Preview volume name must not be empty when building canonical scene");
            const Scene::PreviewMaterial* material = find_preview_material(document, volume.material_name);
            if (material == nullptr) throw std::runtime_error(std::format("Preview volume \"{}\" references unknown material \"{}\"", volume.name, volume.material_name));
            const Scene::VolumeChannel* density = find_volume_channel(volume, "density");
            if (density == nullptr) throw std::runtime_error(std::format("Preview volume \"{}\" requires a density channel for canonical path tracing", volume.name));
            const Scene::VolumeChannel* temperature = find_volume_channel(volume, "temperature");
            const std::uint64_t value_count = static_cast<std::uint64_t>(volume.dimensions[0]) * static_cast<std::uint64_t>(volume.dimensions[1]) * static_cast<std::uint64_t>(volume.dimensions[2]);
            if (density->values.size() != value_count) throw std::runtime_error(std::format("Preview volume \"{}\" density count does not match dimensions", volume.name));
            std::vector<Scene::Parameter> parameters{
                integer_parameter("nx", {static_cast<int>(volume.dimensions[0])}, volume.source),
                integer_parameter("ny", {static_cast<int>(volume.dimensions[1])}, volume.source),
                integer_parameter("nz", {static_cast<int>(volume.dimensions[2])}, volume.source),
                float_parameter("density", density->values, volume.source),
                float_parameter("scale", {material->volume_density_scale}, volume.source),
                float_parameter("temperaturescale", {material->volume_temperature_scale}, volume.source),
                rgb_parameter("sigma_a", Vector3{0.08f, 0.08f, 0.08f}, volume.source),
                rgb_parameter("sigma_s", Vector3{0.92f, 0.92f, 0.92f}, volume.source),
            };
            if (temperature != nullptr) {
                if (temperature->values.size() != value_count) throw std::runtime_error(std::format("Preview volume \"{}\" temperature count does not match dimensions", volume.name));
                parameters.push_back(float_parameter("temperature", temperature->values, volume.source));
            }
            const std::string medium_name = std::format("{}.__medium", volume.name);
            scene.media.push_back(Scene::Medium{
                .name = medium_name,
                .entity = Scene::Entity{.type = "uniformgrid", .parameters = std::move(parameters), .source = volume.source},
            });

            const Vector3 p0 = volume.origin;
            const Vector3 p1{
                volume.origin.x + static_cast<float>(volume.dimensions[0]) * volume.voxel_size.x,
                volume.origin.y + static_cast<float>(volume.dimensions[1]) * volume.voxel_size.y,
                volume.origin.z + static_cast<float>(volume.dimensions[2]) * volume.voxel_size.z,
            };
            std::vector<float> positions{
                p0.x, p0.y, p0.z, p1.x, p0.y, p0.z, p1.x, p1.y, p0.z, p0.x, p1.y, p0.z,
                p0.x, p0.y, p1.z, p1.x, p0.y, p1.z, p1.x, p1.y, p1.z, p0.x, p1.y, p1.z,
            };
            std::vector<float> normals{
                0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,
            };
            std::vector<int> indices{0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 1, 5, 6, 1, 6, 2, 2, 6, 7, 2, 7, 3, 3, 7, 4, 3, 4, 0};
            scene.shapes.push_back(Scene::Shape{
                .name = std::format("{}.__medium_boundary", volume.name),
                .entity = Scene::Entity{
                    .type = "trianglemesh",
                    .parameters = {
                        point3_parameter("P", std::move(positions), volume.source),
                        normal_parameter("N", std::move(normals), volume.source),
                        integer_parameter("indices", std::move(indices), volume.source),
                    },
                    .source = volume.source,
                },
                .material_name = "__spectra_volume_interface",
                .medium_interface = Scene::MediumInterface{.inside = medium_name},
            });
        }

        void append_preview_lights(Scene::ResolvedScene& scene, const Scene::Document& document) {
            for (const Scene::PreviewLight& light : document.lights) {
                if (light.name.empty()) throw std::runtime_error("Preview light name must not be empty when building canonical scene");
                if (light.kind != Scene::PreviewLightKind::Directional && light.kind != Scene::PreviewLightKind::Environment) throw std::runtime_error(std::format("Preview light \"{}\" uses a light kind that is not mapped to canonical path tracing yet", light.name));
                scene.lights.push_back(Scene::Light{
                    .name = light.name,
                    .entity = Scene::Entity{
                        .type = light.kind == Scene::PreviewLightKind::Environment ? "infinite" : "distant",
                        .parameters = {
                            rgb_parameter("L", light.color, light.source),
                            float_parameter("scale", {light.intensity}, light.source),
                        },
                        .source = light.source,
                    },
                });
            }
        }

        [[nodiscard]] SceneTransform make_look_at_transform(const Vector3 eye, const Vector3 target, const Vector3 up) {
            const Vector3 forward = normalize(target - eye, "canonical camera forward");
            const Vector3 right = normalize(cross(normalize(up, "canonical camera up"), forward), "canonical camera right");
            const Vector3 camera_up = cross(forward, right);
            SceneTransform transform{};
            transform.matrix = {
                right.x, camera_up.x, forward.x, eye.x,
                right.y, camera_up.y, forward.y, eye.y,
                right.z, camera_up.z, forward.z, eye.z,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            transform.inverse = {
                right.x, right.y, right.z, -dot(right, eye),
                camera_up.x, camera_up.y, camera_up.z, -dot(camera_up, eye),
                forward.x, forward.y, forward.z, -dot(forward, eye),
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            return transform;
        }

        [[nodiscard]] Scene::ResolvedScene make_resolved_scene_from_preview(const Scene::Document& document, const Scene::ResolvedFrame& frame, const Scene::Revision revision) {
            if (document.name.empty()) throw std::runtime_error("Preview document name must not be empty when building canonical scene");
            if (!document.camera.has_value()) throw std::runtime_error(std::format("Preview document \"{}\" requires a camera for canonical path tracing", document.name));
            Scene::ResolvedScene scene{
                .revision = revision,
                .name = document.name,
                .title = document.title.empty() ? document.name : document.title,
                .source = document.source.empty() ? std::format("scene://{}", document.name) : document.source,
            };
            scene.render_settings.camera = Scene::Entity{
                .type = "perspective",
                .parameters = {float_parameter("fov", {document.camera->vertical_fov_degrees}, document.camera->source)},
                .source = document.camera->source,
            };
            scene.render_settings.camera_transform = SceneTransformSet{.start = make_look_at_transform(document.camera->transform.position, document.camera->target, document.camera->up), .end = make_look_at_transform(document.camera->transform.position, document.camera->target, document.camera->up)};
            append_canonical_materials(scene, document, !frame.volumes.empty());
            append_preview_lights(scene, document);
            for (const Scene::Mesh& mesh : frame.meshes) append_mesh_shape(scene, mesh);
            for (const Scene::PointCloud& point_cloud : frame.point_clouds) append_point_cloud_shapes(scene, document, point_cloud);
            for (const Scene::VolumeGrid& volume : frame.volumes) append_volume(scene, document, volume);
            if (scene.shapes.empty()) throw std::runtime_error(std::format("Preview document \"{}\" produced no canonical pathtracer shapes", document.name));
            return scene;
        }
    } // namespace

    void Scene::CameraWorkspace::ensure_camera(std::string scene_id, Scene::CameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
        std::scoped_lock lock{this->mutex};
        if (this->cameras.contains(scene_id)) return;
        this->cameras.emplace(std::move(scene_id), Scene::CameraSnapshot{
                                                       .revision = Scene::Revision{1},
                                                       .state    = std::move(state),
                                                   });
    }

    Scene::CameraSnapshot Scene::CameraWorkspace::snapshot(const std::string_view scene_id) const {
        validate_scene_id(scene_id);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, Scene::CameraSnapshot>::const_iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        return found->second;
    }

    Scene::CameraSnapshot Scene::CameraWorkspace::commit(const std::string_view scene_id, Scene::CameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, Scene::CameraSnapshot>::iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        found->second = Scene::CameraSnapshot{
            .revision = Scene::Revision{found->second.revision.value + 1u},
            .state    = std::move(state),
        };
        return found->second;
    }

    void Scene::Edit::replace_timeline(Scene::Timeline timeline) {
        this->timeline_replacement = std::move(timeline);
        this->dirty = Scene::combine_dirty_flags(this->dirty, Scene::DirtyFlags::Timeline);
    }

    void Scene::Edit::replace_frame(Scene::FrameSnapshot frame) {
        this->frame_replacement = std::move(frame);
        this->dirty = Scene::combine_dirty_flags(this->dirty, Scene::DirtyFlags::Frame);
    }

    Scene::Builder::Builder(std::string name, std::string title, std::string source) {
        if (name.empty()) throw std::runtime_error("Scene builder requires a non-empty scene name");
        if (title.empty()) throw std::runtime_error("Scene builder requires a non-empty scene title");
        if (source.empty()) throw std::runtime_error("Scene builder requires a non-empty scene source");
        this->scene.name = std::move(name);
        this->scene.title = std::move(title);
        this->scene.source = std::move(source);
        this->scene.revision = Scene::Revision{1};
    }

    void Scene::Builder::set_revision(const Scene::Revision revision) {
        if (revision.value == 0u) throw std::runtime_error("Scene builder revision must not be zero");
        this->scene.revision = revision;
    }

    void Scene::Builder::set_render_settings(Scene::RenderSettings render_settings) {
        this->scene.render_settings = std::move(render_settings);
    }

    void Scene::Builder::add_material(Scene::Material material) {
        if (material.name.empty()) throw std::runtime_error("Scene builder material name must not be empty");
        for (const Scene::Material& existing : this->scene.materials) if (existing.name == material.name) throw std::runtime_error(std::format("Scene builder material \"{}\" is duplicated", material.name));
        this->scene.materials.push_back(std::move(material));
    }

    void Scene::Builder::add_texture(Scene::Texture texture) {
        if (texture.name.empty()) throw std::runtime_error("Scene builder texture name must not be empty");
        for (const Scene::Texture& existing : this->scene.textures) if (existing.name == texture.name) throw std::runtime_error(std::format("Scene builder texture \"{}\" is duplicated", texture.name));
        this->scene.textures.push_back(std::move(texture));
    }

    void Scene::Builder::add_medium(Scene::Medium medium) {
        if (medium.name.empty()) throw std::runtime_error("Scene builder medium name must not be empty");
        for (const Scene::Medium& existing : this->scene.media) if (existing.name == medium.name) throw std::runtime_error(std::format("Scene builder medium \"{}\" is duplicated", medium.name));
        this->scene.media.push_back(std::move(medium));
    }

    void Scene::Builder::add_light(Scene::Light light) {
        if (light.name.empty()) throw std::runtime_error("Scene builder light name must not be empty");
        for (const Scene::Light& existing : this->scene.lights) if (existing.name == light.name) throw std::runtime_error(std::format("Scene builder light \"{}\" is duplicated", light.name));
        this->scene.lights.push_back(std::move(light));
    }

    void Scene::Builder::add_shape(Scene::Shape shape) {
        if (shape.name.empty()) throw std::runtime_error("Scene builder shape name must not be empty");
        for (const Scene::Shape& existing : this->scene.shapes) if (existing.name == shape.name) throw std::runtime_error(std::format("Scene builder shape \"{}\" is duplicated", shape.name));
        this->scene.shapes.push_back(std::move(shape));
    }

    void Scene::Builder::add_object_definition(Scene::ObjectDefinition definition) {
        if (definition.name.empty()) throw std::runtime_error("Scene builder object definition name must not be empty");
        for (const Scene::ObjectDefinition& existing : this->scene.object_definitions) if (existing.name == definition.name) throw std::runtime_error(std::format("Scene builder object definition \"{}\" is duplicated", definition.name));
        this->scene.object_definitions.push_back(std::move(definition));
    }

    void Scene::Builder::add_object_instance(Scene::ObjectInstance instance) {
        if (instance.name.empty()) throw std::runtime_error("Scene builder object instance name must not be empty");
        if (instance.definition_name.empty()) throw std::runtime_error(std::format("Scene builder object instance \"{}\" definition name must not be empty", instance.name));
        for (const Scene::ObjectInstance& existing : this->scene.object_instances) if (existing.name == instance.name) throw std::runtime_error(std::format("Scene builder object instance \"{}\" is duplicated", instance.name));
        this->scene.object_instances.push_back(std::move(instance));
    }

    Scene::ResolvedScene Scene::Builder::resolved_scene() && {
        if (this->scene.revision.value == 0u) throw std::runtime_error("Scene builder revision must not be zero");
        validate_canonical_scene(this->scene);
        return std::move(this->scene);
    }

    Scene Scene::Builder::build() && {
        Scene::ResolvedScene scene = std::move(*this).resolved_scene();
        return Scene{std::move(scene)};
    }

    Scene::Scene(Scene::Document document) {
        if (document.revision.value == 0) document.revision = Scene::Revision{1};
        this->current_revision = document.revision;
        this->current_document = std::make_shared<Scene::Document>(std::move(document));
        this->current_timeline.frames_per_second = this->current_document->frames_per_second;
    }

    Scene::Scene(Scene::ResolvedScene scene) {
        if (scene.revision.value == 0) scene.revision = Scene::Revision{1};
        validate_canonical_scene(scene);
        this->current_revision = scene.revision;
        this->canonical_scene = std::move(scene);
    }

    Scene::Scene(Scene::ResolvedScene scene, Scene::Document preview_document) {
        if (scene.revision.value == 0) scene.revision = Scene::Revision{1};
        if (preview_document.revision.value == 0) preview_document.revision = scene.revision;
        validate_canonical_scene(scene);
        this->current_revision = scene.revision;
        this->current_document = std::make_shared<Scene::Document>(std::move(preview_document));
        this->current_timeline.frames_per_second = this->current_document->frames_per_second;
        this->canonical_scene = std::move(scene);
    }

    Scene::Revision Scene::revision() const {
        if (this->current_document == nullptr && !this->canonical_scene.has_value()) throw std::runtime_error("Scene workspace does not contain a loaded scene");
        return this->current_revision;
    }

    std::shared_ptr<const Scene::Document> Scene::document() const {
        static_cast<void>(this->preview_document());
        return this->current_document;
    }

    Scene::Timeline Scene::timeline() const {
        if (this->current_document == nullptr && !this->canonical_scene.has_value()) throw std::runtime_error("Scene workspace does not contain a loaded scene");
        return this->current_timeline;
    }

    Scene::ResolvedFrame Scene::resolved_frame() const {
        const Scene::Document& document = this->preview_document();
        const Scene::FrameSnapshot empty_frame{};
        const Scene::FrameSnapshot& frame_value = this->current_timeline.current_frame.has_value() ? *this->current_timeline.current_frame : empty_frame;
        return Scene::ResolvedFrame{
            .meshes          = resolve_scene_items(document.meshes, frame_value.meshes, "mesh"),
            .point_clouds     = resolve_scene_items(document.point_clouds, frame_value.point_clouds, "point cloud"),
            .volumes         = resolve_scene_items(document.volumes, frame_value.volumes, "volume"),
        };
    }

    Scene::ResolvedScene Scene::resolved_scene() const {
        if (this->current_document == nullptr && !this->canonical_scene.has_value()) throw std::runtime_error("Scene workspace does not contain a loaded scene");
        if (this->canonical_scene.has_value() && !this->current_timeline.current_frame.has_value()) {
            Scene::ResolvedScene scene = *this->canonical_scene;
            scene.revision = this->current_revision;
            validate_canonical_scene(scene);
            return scene;
        }
        const Scene::Document& document = this->preview_document();
        Scene::ResolvedScene scene = make_resolved_scene_from_preview(document, this->resolved_frame(), this->current_revision);
        validate_canonical_scene(scene);
        return scene;
    }

    Scene::Info Scene::info() const {
        return describe_scene(this->resolved_scene());
    }

    Scene::Document Scene::make_preview_document() const {
        return this->preview_document();
    }

    const Scene::Document& Scene::preview_document() const {
        if (this->current_document != nullptr) return *this->current_document;
        if (!this->canonical_scene.has_value()) throw std::runtime_error("Scene workspace does not contain a loaded scene");
        Scene::Document preview_document = make_preview_document_from_pbrt(*this->canonical_scene);
        if (preview_document.revision.value == 0) preview_document.revision = this->current_revision;
        this->current_document = std::make_shared<Scene::Document>(std::move(preview_document));
        return *this->current_document;
    }

    Scene::DirtyFlags Scene::commit(Scene::Edit edit) {
        if (this->current_document == nullptr && !this->canonical_scene.has_value()) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (edit.dirty == Scene::DirtyFlags::None) throw std::runtime_error("Cannot commit an empty scene edit");

        this->current_revision = Scene::Revision{this->current_revision.value + 1};
        if (edit.timeline_replacement.has_value()) this->current_timeline = std::move(*edit.timeline_replacement);
        if (edit.frame_replacement.has_value()) {
            this->current_timeline.cursor = edit.frame_replacement->cursor;
            this->current_timeline.current_frame = std::move(*edit.frame_replacement);
        }

        return edit.dirty;
    }

    Scene::FrameCursor Scene::make_frame_cursor(const Scene::FrameInfo& info) {
        return Scene::FrameCursor{
            .frame_index  = info.frame_index,
            .time_seconds = info.time_seconds,
        };
    }

    namespace {
        struct Bounds {
            Vector3 minimum{};
            Vector3 maximum{};
            bool valid{false};
        };

        [[nodiscard]] float matrix_value(const std::array<float, 16>& matrix, const std::size_t row, const std::size_t column) {
            return matrix.at(row * 4u + column);
        }

        void include_point(Bounds& bounds, const Vector3 point) {
            if (!is_finite(point)) throw std::runtime_error("PBRT preview mesh contains a non-finite point");
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

        [[nodiscard]] const std::vector<float>& required_float_values(const Scene::Entity& entity, const std::string_view type, const std::string_view name, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != type || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain float values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"{} {}\"", context, type, name));
        }

        [[nodiscard]] const std::vector<int>& required_int_values(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "integer" || parameter.name != name) continue;
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain integer values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"integer {}\"", context, name));
        }

        [[nodiscard]] Vector3 required_rgb_value(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            const std::vector<float>& values = required_float_values(entity, "rgb", name, context);
            if (values.size() != 3u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly three RGB values", context, name));
            return Vector3{values.at(0), values.at(1), values.at(2)};
        }

        [[nodiscard]] float required_one_float_value(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "float" || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one float", context, name));
                return values->front();
            }
            throw std::runtime_error(std::format("{} requires \"float {}\"", context, name));
        }

        [[nodiscard]] Vector3 transform_point(const SceneTransform& transform, const Vector3 point) {
            const std::array<float, 16>& matrix = transform.matrix;
            const float x = matrix_value(matrix, 0u, 0u) * point.x + matrix_value(matrix, 0u, 1u) * point.y + matrix_value(matrix, 0u, 2u) * point.z + matrix_value(matrix, 0u, 3u);
            const float y = matrix_value(matrix, 1u, 0u) * point.x + matrix_value(matrix, 1u, 1u) * point.y + matrix_value(matrix, 1u, 2u) * point.z + matrix_value(matrix, 1u, 3u);
            const float z = matrix_value(matrix, 2u, 0u) * point.x + matrix_value(matrix, 2u, 1u) * point.y + matrix_value(matrix, 2u, 2u) * point.z + matrix_value(matrix, 2u, 3u);
            const float w = matrix_value(matrix, 3u, 0u) * point.x + matrix_value(matrix, 3u, 1u) * point.y + matrix_value(matrix, 3u, 2u) * point.z + matrix_value(matrix, 3u, 3u);
            if (!std::isfinite(w) || w == 0.0f) throw std::runtime_error("PBRT preview mesh transform produced an invalid homogeneous point");
            return Vector3{x / w, y / w, z / w};
        }

        [[nodiscard]] Vector3 transform_normal(const SceneTransform& transform, const Vector3 normal) {
            const std::array<float, 16>& inverse = transform.inverse;
            const Vector3 transformed{
                matrix_value(inverse, 0u, 0u) * normal.x + matrix_value(inverse, 1u, 0u) * normal.y + matrix_value(inverse, 2u, 0u) * normal.z,
                matrix_value(inverse, 0u, 1u) * normal.x + matrix_value(inverse, 1u, 1u) * normal.y + matrix_value(inverse, 2u, 1u) * normal.z,
                matrix_value(inverse, 0u, 2u) * normal.x + matrix_value(inverse, 1u, 2u) * normal.y + matrix_value(inverse, 2u, 2u) * normal.z,
            };
            return normalize(transformed, "PBRT preview mesh normal transform");
        }

        void require_static_transform(const SceneTransformSet& transform, const std::string_view context) {
            if (transform.animated) throw std::runtime_error(std::format("{} uses animated transforms, which are not supported by the PBRT preview scene loader", context));
        }

        [[nodiscard]] std::string object_source_prefix(const Scene::ResolvedScene& scene) {
            if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
            if (scene.source.starts_with("pbrt://")) return scene.source;
            return std::format("pbrt://{}", scene.source);
        }

        [[nodiscard]] std::string make_shape_object_name(const std::string_view object_source_prefix_value, const std::size_t shape_index) {
            return std::format("{}#shape:{}", object_source_prefix_value, shape_index);
        }

        [[nodiscard]] std::set<std::string> referenced_shape_material_names(const Scene::ResolvedScene& scene) {
            std::set<std::string> names{};
            for (const Scene::Shape& shape : scene.shapes) {
                if (shape.material_name.empty()) throw std::runtime_error("PBRT preview shape references an empty material name");
                names.insert(shape.material_name);
            }
            return names;
        }

        [[nodiscard]] std::map<std::string, std::size_t> append_materials(const Scene::ResolvedScene& scene, const std::set<std::string>& referenced_material_names, Scene::Document& document) {
            std::map<std::string, std::size_t> material_indices{};
            for (const Scene::Material& material : scene.materials) {
                if (!referenced_material_names.contains(material.name)) continue;
                if (material.name.empty()) throw std::runtime_error("PBRT preview material name must not be empty");
                if (material.entity.type != "diffuse") throw std::runtime_error(std::format("PBRT preview material \"{}\" uses unsupported type \"{}\"", material.name, material.entity.type));
                const Vector3 reflectance = required_rgb_value(material.entity, "reflectance", std::format("PBRT preview material \"{}\"", material.name));
                const bool inserted = material_indices.emplace(material.name, document.materials.size()).second;
                if (!inserted) throw std::runtime_error(std::format("PBRT preview material \"{}\" is duplicated", material.name));
                document.materials.push_back(Scene::PreviewMaterial{
                    .name       = material.name,
                    .surface_kind = Scene::PreviewSurfaceKind::LitSurface,
                    .base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f},
                    .roughness  = 0.72f,
                });
            }
            for (const std::string& material_name : referenced_material_names) {
                if (!material_indices.contains(material_name)) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", material_name));
            }
            return material_indices;
        }

        [[nodiscard]] Scene::PreviewMaterial& material_for_name(Scene::Document& document, const std::map<std::string, std::size_t>& material_indices, const std::string& name) {
            const std::map<std::string, std::size_t>::const_iterator iter = material_indices.find(name);
            if (iter == material_indices.end()) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", name));
            return document.materials.at(iter->second);
        }

        void apply_area_light_material(const Scene::Shape& shape, const std::size_t shape_index, Scene::Document& document, const std::map<std::string, std::size_t>& material_indices) {
            if (!shape.area_light.has_value()) return;
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.area_light->entity.type != "diffuse") throw std::runtime_error(std::format("{} uses unsupported area light type \"{}\"", context, shape.area_light->entity.type));
            Scene::PreviewMaterial& material = material_for_name(document, material_indices, shape.material_name);
            const Vector3 radiance = required_rgb_value(shape.area_light->entity, "L", context);
            if (material.emission_strength != 0.0f && (material.emission_color.x != radiance.x || material.emission_color.y != radiance.y || material.emission_color.z != radiance.z)) throw std::runtime_error(std::format("PBRT preview material \"{}\" is reused by area lights with different radiance", material.name));
            material.surface_kind      = Scene::PreviewSurfaceKind::EmissiveSurface;
            material.emission_color    = radiance;
            material.emission_strength = 1.0f;
        }

        [[nodiscard]] Scene::Mesh make_mesh(const std::string_view object_source_prefix_value, const Scene::Shape& shape, const std::size_t shape_index, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.entity.type != "trianglemesh") throw std::runtime_error(std::format("PBRT preview scene loader only supports trianglemesh shapes, got \"{}\"", shape.entity.type));
            require_static_transform(shape.transform, context);
            if (shape.reverse_orientation) throw std::runtime_error(std::format("{} uses ReverseOrientation, which is not supported by the PBRT preview scene loader", context));
            if (!shape.medium_interface.inside.empty() || !shape.medium_interface.outside.empty()) throw std::runtime_error(std::format("{} uses MediumInterface, which is not supported by the PBRT preview scene loader", context));
            if (!material_indices.contains(shape.material_name)) throw std::runtime_error(std::format("{} references unknown material \"{}\"", context, shape.material_name));
            const std::string object_name = make_shape_object_name(object_source_prefix_value, shape_index);
            const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
            const std::vector<float>& normals = required_float_values(shape.entity, "normal", "N", context);
            const std::vector<int>& indices = required_int_values(shape.entity, "indices", context);
            if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid point3 P data", object_name));
            if (normals.size() != positions.size()) throw std::runtime_error(std::format("PBRT preview shape \"{}\" normal count does not match position count", object_name));
            if (indices.empty() || indices.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid triangle index data", object_name));

            Scene::Mesh mesh{
                .name         = object_name,
                .material_name = shape.material_name,
                .dynamic      = false,
                .source       = shape.entity.source,
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

        void append_meshes(const std::string_view object_source_prefix_value, const Scene::ResolvedScene& scene, Scene::Document& document, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            std::map<std::string, bool> material_used_by_area_light{};
            for (std::size_t shape_index = 0; shape_index < scene.shapes.size(); ++shape_index) {
                const Scene::Shape& shape = scene.shapes.at(shape_index);
                const bool is_area_light = shape.area_light.has_value();
                const std::pair<std::map<std::string, bool>::iterator, bool> material_usage = material_used_by_area_light.emplace(shape.material_name, is_area_light);
                if (!material_usage.second && material_usage.first->second != is_area_light) throw std::runtime_error(std::format("PBRT preview material \"{}\" is shared by emissive and non-emissive shapes", shape.material_name));
                apply_area_light_material(shape, shape_index, document, material_indices);
                Scene::Mesh mesh = make_mesh(object_source_prefix_value, shape, shape_index, material_indices, bounds);
                document.meshes.push_back(std::move(mesh));
            }
            if (document.meshes.empty()) throw std::runtime_error("PBRT preview scene loader did not find any trianglemesh shapes");
        }

        [[nodiscard]] Scene::Camera make_camera(const Scene::ResolvedScene& scene, const Bounds& bounds) {
            if (scene.render_settings.camera.type != "perspective") throw std::runtime_error(std::format("PBRT preview scene loader only supports perspective cameras, got \"{}\"", scene.render_settings.camera.type));
            require_static_transform(scene.render_settings.camera_transform, "PBRT preview camera");
            const std::array<float, 16>& world_from_camera = scene.render_settings.camera_transform.start.matrix;
            const Vector3 eye{matrix_value(world_from_camera, 0u, 3u), matrix_value(world_from_camera, 1u, 3u), matrix_value(world_from_camera, 2u, 3u)};
            const Vector3 up = normalize(Vector3{matrix_value(world_from_camera, 0u, 1u), matrix_value(world_from_camera, 1u, 1u), matrix_value(world_from_camera, 2u, 1u)}, "PBRT preview camera up vector");
            const Vector3 target = center(bounds);
            const float scene_radius = radius(bounds);
            const float camera_distance = length(eye - target);
            const float far_plane = std::max(20.0f, camera_distance + scene_radius * 4.0f);
            return Scene::Camera{
                .name               = "camera.main",
                .transform          = Transform{.position = eye},
                .target             = target,
                .up                 = up,
                .vertical_fov_degrees = required_one_float_value(scene.render_settings.camera, "fov", "PBRT preview camera"),
                .near_plane          = 0.01f,
                .far_plane           = far_plane,
                .source             = scene.render_settings.camera.source,
            };
        }

        void reject_unsupported_scene_content(const Scene::ResolvedScene& scene) {
            if (!scene.textures.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT textures");
            if (!scene.media.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT media");
            if (!scene.lights.empty()) throw std::runtime_error("PBRT preview scene loader only supports mesh area lights");
            if (!scene.object_definitions.empty() || !scene.object_instances.empty()) throw std::runtime_error("PBRT preview scene loader only supports top-level trianglemesh shapes");
        }
    } // namespace

    Scene::Document make_preview_document_from_pbrt(const Scene::ResolvedScene& scene) {
        reject_unsupported_scene_content(scene);
        if (scene.revision.value == 0u) throw std::runtime_error("PBRT preview scene revision must not be zero");
        if (scene.name.empty()) throw std::runtime_error("PBRT preview scene name must not be empty");
        if (scene.title.empty()) throw std::runtime_error("PBRT preview scene title must not be empty");
        if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
        const std::string object_source_prefix_value = object_source_prefix(scene);
        Scene::Document document{
            .revision        = Scene::Revision{scene.revision.value},
            .name            = scene.name,
            .title           = scene.title,
            .source          = object_source_prefix_value,
            .frames_per_second = 24.0,
            .timeline_enabled = false,
        };
        Bounds bounds{};
        const std::set<std::string> referenced_material_names = referenced_shape_material_names(scene);
        const std::map<std::string, std::size_t> material_indices = append_materials(scene, referenced_material_names, document);
        append_meshes(object_source_prefix_value, scene, document, material_indices, bounds);
        document.camera = make_camera(scene, bounds);
        document.lights.push_back(Scene::PreviewLight{
            .name      = "preview.key",
            .kind      = Scene::PreviewLightKind::Directional,
            .color     = Vector3{1.0f, 0.96f, 0.86f},
            .intensity = 1.8f,
        });
        return document;
    }

    namespace {
        constexpr char DefaultMaterialName[] = "__pbrt_default_material";

        enum class TokenKind { Word, QuotedString, LeftBracket, RightBracket };
        enum class EntityUse { Generic, Film, Camera, Texture, Material, Medium, Light, AreaLight, Shape };

        struct Token {
            TokenKind kind{TokenKind::Word};
            std::string text{};
            Scene::SourceLocation source{};
        };

        [[nodiscard]] std::string Lowercase(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] std::string SourceString(const Scene::SourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::runtime_error ParseError(const Scene::SourceLocation& source, const std::string_view message) {
            return std::runtime_error(std::format("{}: {}", SourceString(source), message));
        }

        [[nodiscard]] bool HasExtension(const std::filesystem::path& path, const std::string_view extension) {
            return Lowercase(path.extension().string()) == Lowercase(std::string(extension));
        }

        [[nodiscard]] bool IsSceneFile(const std::filesystem::path& path) {
            if (HasExtension(path, ".pbrt")) return true;
            if (!HasExtension(path, ".gz")) return false;
            return HasExtension(path.stem(), ".pbrt");
        }

        [[nodiscard]] bool IsAbsolutePathString(const std::string& value) {
            if (value.empty()) return false;
            if (std::filesystem::path(value).is_absolute()) return true;
            return value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && (value[2] == '/' || value[2] == '\\');
        }

        [[nodiscard]] bool IsPathLike(const std::string& value) {
            if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) return true;
            const std::filesystem::path path(value);
            return !path.extension().empty();
        }

        [[nodiscard]] std::string ReadPlainFile(const std::filesystem::path& path) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error(std::format("{}: unable to open PBRT scene file", path.string()));

            std::string result;
            std::array<char, 1 << 15> buffer{};
            while (input) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if (count > 0) result.append(buffer.data(), static_cast<std::size_t>(count));
            }
            if (input.bad()) throw std::runtime_error(std::format("{}: PBRT scene file read failed", path.string()));
            return result;
        }

        [[nodiscard]] std::string ReadGzipFile(const std::filesystem::path& path) {
            gzFile file = gzopen(path.string().c_str(), "rb");
            if (file == nullptr) throw std::runtime_error(std::format("{}: unable to open gzip PBRT scene file", path.string()));

            std::string result;
            std::array<char, 1 << 15> buffer{};
            while (true) {
                const int count = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()));
                if (count < 0) {
                    int errorNumber = 0;
                    const char* message = gzerror(file, &errorNumber);
                    gzclose(file);
                    throw std::runtime_error(std::format("{}: gzip read failed: {}", path.string(), message == nullptr ? "unknown zlib error" : message));
                }
                if (count == 0) break;
                result.append(buffer.data(), static_cast<std::size_t>(count));
            }

            const int closeStatus = gzclose(file);
            if (closeStatus != Z_OK) throw std::runtime_error(std::format("{}: gzip close failed", path.string()));
            return result;
        }

        [[nodiscard]] std::string ReadSceneFile(const std::filesystem::path& path) {
            if (HasExtension(path, ".gz")) return ReadGzipFile(path);
            return ReadPlainFile(path);
        }

        class PbrtTokenStream {
        public:
            explicit PbrtTokenStream(std::filesystem::path filename) {
                this->PushFile(std::move(filename));
            }

            [[nodiscard]] std::optional<Token> Next() {
                if (this->pushedToken.has_value()) return std::exchange(this->pushedToken, {});

                while (!this->fileStack.empty()) {
                    PbrtTokenFile& file = this->fileStack.back();
                    this->SkipIgnored(&file);
                    if (file.offset >= file.content.size()) {
                        this->fileStack.pop_back();
                        continue;
                    }

                    const Scene::SourceLocation source{
                        .filename = file.filename.string(),
                        .line     = file.line,
                        .column   = file.column,
                    };

                    const char character = file.content[file.offset];
                    if (character == '[') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::LeftBracket, .text = "[", .source = source};
                    }
                    if (character == ']') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::RightBracket, .text = "]", .source = source};
                    }
                    if (character == '"') return this->ReadString(&file, source);
                    return this->ReadWord(&file, source);
                }

                return {};
            }

            void PushBack(Token token) {
                if (this->pushedToken.has_value()) throw std::runtime_error("PBRT parser internal error: token pushback overflow");
                this->pushedToken = std::move(token);
            }

            void PushFile(std::filesystem::path path) {
                if (!std::filesystem::exists(path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", path.string()));
                std::string content = ReadSceneFile(path);
                this->fileStack.push_back(PbrtTokenFile{
                    .filename = std::move(path),
                    .content  = std::move(content),
                });
            }

        private:
            struct PbrtTokenFile {
                std::filesystem::path filename;
                std::string content;
                std::size_t offset{};
                int line{1};
                int column{1};
            };

            void Advance(PbrtTokenFile* file) {
                if (file->offset >= file->content.size()) return;
                if (file->content[file->offset] == '\n') {
                    ++file->line;
                    file->column = 1;
                } else {
                    ++file->column;
                }
                ++file->offset;
            }

            void SkipIgnored(PbrtTokenFile* file) {
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character))) {
                        this->Advance(file);
                        continue;
                    }
                    if (character == '#') {
                        while (file->offset < file->content.size() && file->content[file->offset] != '\n') this->Advance(file);
                        continue;
                    }
                    return;
                }
            }

            [[nodiscard]] Token ReadString(PbrtTokenFile* file, const Scene::SourceLocation& source) {
                this->Advance(file);
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (character == '"') {
                        this->Advance(file);
                        return Token{.kind = TokenKind::QuotedString, .text = std::move(text), .source = source};
                    }
                    if (character == '\\') {
                        this->Advance(file);
                        if (file->offset >= file->content.size()) throw ParseError(source, "unterminated escape sequence in quoted string");
                        text.push_back(file->content[file->offset]);
                        this->Advance(file);
                        continue;
                    }
                    text.push_back(character);
                    this->Advance(file);
                }
                throw ParseError(source, "unterminated quoted string");
            }

            [[nodiscard]] Token ReadWord(PbrtTokenFile* file, const Scene::SourceLocation& source) {
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character)) || character == '[' || character == ']' || character == '#') break;
                    text.push_back(character);
                    this->Advance(file);
                }
                if (text.empty()) throw ParseError(source, "unexpected character in PBRT scene file");
                return Token{.kind = TokenKind::Word, .text = std::move(text), .source = source};
            }

            std::vector<PbrtTokenFile> fileStack{};
            std::optional<Token> pushedToken{};
        };

        [[nodiscard]] Token RequireToken(PbrtTokenStream& stream, const std::string_view context) {
            std::optional<Token> token = stream.Next();
            if (!token.has_value()) throw std::runtime_error(std::format("Unexpected end of PBRT scene file while parsing {}", context));
            return std::move(*token);
        }

        [[nodiscard]] std::string RequireStringToken(PbrtTokenStream& stream, const std::string_view context) {
            Token token = RequireToken(stream, context);
            if (token.kind != TokenKind::QuotedString) throw ParseError(token.source, std::format("{} expects a quoted string", context));
            return std::move(token.text);
        }

        [[nodiscard]] float ParseFloatToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const float value = std::strtof(begin, &end);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not a floating-point value", token.text));
            return value;
        }

        [[nodiscard]] int ParseIntegerToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const long value  = std::strtol(begin, &end, 10);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not an integer value", token.text));
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) throw ParseError(token.source, std::format("\"{}\" is outside integer range", token.text));
            return static_cast<int>(value);
        }

        [[nodiscard]] std::uint8_t ParseBoolToken(const Token& token) {
            if (token.text == "true") return 1;
            if (token.text == "false") return 0;
            throw ParseError(token.source, std::format("\"{}\" is not a Boolean value", token.text));
        }

        [[nodiscard]] std::array<float, 16> MultiplyMatrix(const std::array<float, 16>& a, const std::array<float, 16>& b) {
            std::array<float, 16> result{};
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    for (std::size_t index = 0; index < 4; ++index) result[row * 4 + column] += a[row * 4 + index] * b[index * 4 + column];
                }
            }
            return result;
        }

        [[nodiscard]] std::array<float, 16> TransposeMatrix(const std::array<float, 16>& matrix) {
            return {
                matrix[0],
                matrix[4],
                matrix[8],
                matrix[12],
                matrix[1],
                matrix[5],
                matrix[9],
                matrix[13],
                matrix[2],
                matrix[6],
                matrix[10],
                matrix[14],
                matrix[3],
                matrix[7],
                matrix[11],
                matrix[15],
            };
        }

        [[nodiscard]] std::array<float, 16> InverseMatrix(const std::array<float, 16>& matrix, const Scene::SourceLocation& source) {
            std::array<std::array<double, 8>, 4> augmented{};
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = matrix[static_cast<std::size_t>(row * 4 + column)];
                augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + row)] = 1.0;
            }

            for (int column = 0; column < 4; ++column) {
                int pivotRow = column;
                double pivot = std::abs(augmented[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(column)]);
                for (int row = column + 1; row < 4; ++row) {
                    const double candidate = std::abs(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
                    if (candidate > pivot) {
                        pivot    = candidate;
                        pivotRow = row;
                    }
                }
                if (!(pivot > 0.0)) throw ParseError(source, "Transform matrix is singular");
                if (pivotRow != column) std::swap(augmented[static_cast<std::size_t>(pivotRow)], augmented[static_cast<std::size_t>(column)]);

                const double denominator = augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(column)];
                for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)] /= denominator;

                for (int row = 0; row < 4; ++row) {
                    if (row == column) continue;
                    const double factor = augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                    for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(index)] -= factor * augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)];
                }
            }

            std::array<float, 16> inverse{};
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) inverse[static_cast<std::size_t>(row * 4 + column)] = static_cast<float>(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + column)]);
            return inverse;
        }

        [[nodiscard]] SceneTransform Multiply(const SceneTransform& a, const SceneTransform& b) {
            return SceneTransform{
                .matrix  = MultiplyMatrix(a.matrix, b.matrix),
                .inverse = MultiplyMatrix(b.inverse, a.inverse),
            };
        }

        [[nodiscard]] SceneTransform Inverse(const SceneTransform& transform) {
            return SceneTransform{
                .matrix  = transform.inverse,
                .inverse = transform.matrix,
            };
        }

        [[nodiscard]] SceneTransform Translate(const Vector3 delta) {
            return SceneTransform{
                .matrix =
                    {
                        1.0f,
                        0.0f,
                        0.0f,
                        delta.x,
                        0.0f,
                        1.0f,
                        0.0f,
                        delta.y,
                        0.0f,
                        0.0f,
                        1.0f,
                        delta.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
                .inverse =
                    {
                        1.0f,
                        0.0f,
                        0.0f,
                        -delta.x,
                        0.0f,
                        1.0f,
                        0.0f,
                        -delta.y,
                        0.0f,
                        0.0f,
                        1.0f,
                        -delta.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
            };
        }

        [[nodiscard]] SceneTransform Scale(float x, float y, float z) {
            return SceneTransform{
                .matrix =
                    {
                        x,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        y,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        z,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
                .inverse =
                    {
                        1.0f / x,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f / y,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f / z,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
            };
        }

        [[nodiscard]] SceneTransform Rotate(float degrees, Vector3 axis) {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            axis                             = normalize(axis, "PBRT rotate axis");
            const float sinTheta             = std::sin(degrees * radiansPerDegree);
            const float cosTheta             = std::cos(degrees * radiansPerDegree);
            const float oneMinusCosTheta     = 1.0f - cosTheta;
            const std::array<float, 16> matrix{
                axis.x * axis.x + (1.0f - axis.x * axis.x) * cosTheta,
                axis.x * axis.y * oneMinusCosTheta - axis.z * sinTheta,
                axis.x * axis.z * oneMinusCosTheta + axis.y * sinTheta,
                0.0f,
                axis.x * axis.y * oneMinusCosTheta + axis.z * sinTheta,
                axis.y * axis.y + (1.0f - axis.y * axis.y) * cosTheta,
                axis.y * axis.z * oneMinusCosTheta - axis.x * sinTheta,
                0.0f,
                axis.x * axis.z * oneMinusCosTheta - axis.y * sinTheta,
                axis.y * axis.z * oneMinusCosTheta + axis.x * sinTheta,
                axis.z * axis.z + (1.0f - axis.z * axis.z) * cosTheta,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            return SceneTransform{
                .matrix  = matrix,
                .inverse = TransposeMatrix(matrix),
            };
        }

        [[nodiscard]] SceneTransform LookAt(const Vector3 position, const Vector3 look, const Vector3 up) {
            const Vector3 direction = normalize(look - position, "PBRT LookAt direction");
            const Vector3 right     = normalize(cross(normalize(up, "PBRT LookAt up vector"), direction), "PBRT LookAt right vector");
            const Vector3 newUp     = cross(direction, right);
            const std::array<float, 16> worldFromCamera{
                right.x,
                newUp.x,
                direction.x,
                position.x,
                right.y,
                newUp.y,
                direction.y,
                position.y,
                right.z,
                newUp.z,
                direction.z,
                position.z,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            const std::array<float, 16> cameraFromWorld{
                right.x,
                right.y,
                right.z,
                -dot(right, position),
                newUp.x,
                newUp.y,
                newUp.z,
                -dot(newUp, position),
                direction.x,
                direction.y,
                direction.z,
                -dot(direction, position),
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            return SceneTransform{
                .matrix  = cameraFromWorld,
                .inverse = worldFromCamera,
            };
        }

        [[nodiscard]] SceneTransform TransformFromPbrtMatrix(const std::array<float, 16>& pbrtMatrix, const Scene::SourceLocation& source) {
            const std::array<float, 16> matrix = TransposeMatrix(pbrtMatrix);
            return SceneTransform{
                .matrix  = matrix,
                .inverse = InverseMatrix(matrix, source),
            };
        }

        [[nodiscard]] bool TransformDiffers(const SceneTransform& left, const SceneTransform& right) {
            return left.matrix != right.matrix || left.inverse != right.inverse;
        }

        void RefreshAnimatedFlag(SceneTransformSet* transform) {
            transform->animated = TransformDiffers(transform->start, transform->end);
        }

        void ApplyTransform(SceneTransformSet* transform, const SceneTransform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = Multiply(transform->start, value);
            if (endActive) transform->end = Multiply(transform->end, value);
            RefreshAnimatedFlag(transform);
        }

        void SetTransform(SceneTransformSet* transform, const SceneTransform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = value;
            if (endActive) transform->end = value;
            RefreshAnimatedFlag(transform);
        }

        [[nodiscard]] Scene::ColorSpace ParseColorSpaceName(const std::string& name, const Scene::SourceLocation& source) {
            const std::string lower = Lowercase(name);
            if (lower == "srgb") return Scene::ColorSpace::sRGB;
            if (lower == "dci-p3") return Scene::ColorSpace::DCI_P3;
            if (lower == "rec2020") return Scene::ColorSpace::Rec2020;
            if (lower == "aces2065-1") return Scene::ColorSpace::ACES2065_1;
            throw ParseError(source, std::format("\"{}\" is not a supported PBRT color space", name));
        }

        [[nodiscard]] std::vector<std::string>* ParameterStringValues(Scene::Parameter* parameter) {
            return std::get_if<std::vector<std::string>>(&parameter->values);
        }

        [[nodiscard]] const std::vector<std::string>* ParameterStringValues(const Scene::Parameter& parameter) {
            return std::get_if<std::vector<std::string>>(&parameter.values);
        }

        [[nodiscard]] const std::vector<float>* ParameterFloatValues(const Scene::Parameter& parameter) {
            return std::get_if<std::vector<float>>(&parameter.values);
        }

        [[nodiscard]] std::string OneStringParameter(const std::vector<Scene::Parameter>& parameters, const std::string& name, std::string default_value) {
            for (const Scene::Parameter& parameter : parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = ParameterStringValues(parameter);
                if (values == nullptr || values->size() != 1) throw ParseError(parameter.source, std::format("PBRT string parameter \"{}\" must contain exactly one string value", name));
                return values->front();
            }
            return default_value;
        }

        [[nodiscard]] float OneFloatParameter(const std::vector<Scene::Parameter>& parameters, const std::string& name, const float default_value) {
            for (const Scene::Parameter& parameter : parameters) {
                if (parameter.name != name) continue;
                if (parameter.type != "float") throw ParseError(parameter.source, std::format("PBRT parameter \"{}\" must be declared as float", name));
                const std::vector<float>* values = ParameterFloatValues(parameter);
                if (values == nullptr || values->size() != 1) throw ParseError(parameter.source, std::format("PBRT float parameter \"{}\" must contain exactly one float value", name));
                return values->front();
            }
            return default_value;
        }

        [[nodiscard]] bool IsBuiltInApertureName(const std::string& value) {
            return value == "gaussian" || value == "square" || value == "pentagon" || value == "star";
        }

        struct GraphicsState {
            SceneTransformSet transform{};
            bool activeStart{true};
            bool activeEnd{true};
            Scene::ColorSpace color_space{Scene::ColorSpace::sRGB};
            std::string currentMaterialName{DefaultMaterialName};
            std::optional<Scene::AreaLight> area_light{};
            Scene::MediumInterface medium_interface{};
            bool reverse_orientation{false};
            std::vector<Scene::Parameter> shapeAttributes{};
            std::vector<Scene::Parameter> lightAttributes{};
            std::vector<Scene::Parameter> materialAttributes{};
            std::vector<Scene::Parameter> mediumAttributes{};
            std::vector<Scene::Parameter> textureAttributes{};
        };

        class ScenePbrtBuilder {
        public:
            explicit ScenePbrtBuilder(std::filesystem::path inputFile) : inputFile(std::filesystem::absolute(std::move(inputFile)).lexically_normal()), searchDirectory(this->inputFile.parent_path()) {
                this->scene.name   = this->inputFile.stem().string();
                this->scene.title  = this->inputFile.stem().string();
                this->scene.source = this->inputFile.string();

                const Scene::SourceLocation source{.filename = this->inputFile.string(), .line = 1, .column = 1};
                this->SetDefaultEntitySources(source);
                this->scene.materials.push_back(Scene::Material{
                    .name   = DefaultMaterialName,
                    .entity = Scene::Entity{.type = "diffuse", .color_space = Scene::ColorSpace::sRGB, .source = source},
                });
                this->material_names.insert(DefaultMaterialName);
                this->namedCoordinateSystems["world"] = this->graphicsState.transform;
            }

            [[nodiscard]] Scene::ResolvedScene Parse() {
                this->ParseFile(this->inputFile);
                this->Finish();
                return std::move(this->scene);
            }

        private:
            enum class BlockState { Options, World };

            void SetDefaultEntitySources(const Scene::SourceLocation& source) {
                this->scene.render_settings.filter.source      = source;
                this->scene.render_settings.film.source        = source;
                this->scene.render_settings.camera.source      = source;
                this->scene.render_settings.sampler.source     = source;
                this->scene.render_settings.integrator.source  = source;
                this->scene.render_settings.accelerator.source = source;
            }

            [[nodiscard]] std::string ResolveResourcePath(const std::string& value) const {
                if (value.empty() || IsAbsolutePathString(value)) return value;
                return (this->searchDirectory / std::filesystem::path(value)).lexically_normal().string();
            }

            [[nodiscard]] std::filesystem::path ResolveIncludePath(const std::string& value, const Scene::SourceLocation& source) const {
                if (value.empty()) throw ParseError(source, "Include filename must not be empty");
                const std::filesystem::path path = IsAbsolutePathString(value) ? std::filesystem::path(value) : this->searchDirectory / std::filesystem::path(value);
                return std::filesystem::absolute(path).lexically_normal();
            }

            void ResolveParameterPaths(std::vector<Scene::Parameter>* parameters, const EntityUse entityUse) const {
                for (Scene::Parameter& parameter : *parameters) {
                    std::vector<std::string>* values = ParameterStringValues(&parameter);
                    if (values == nullptr) continue;
                    if (entityUse == EntityUse::Film && parameter.name == "filename") continue;

                    const bool directFileParameter = parameter.name == "filename" || parameter.name == "normalmap" || parameter.name == "lensfile" || parameter.name == "emissionfilename";
                    const bool apertureParameter   = parameter.name == "aperture";
                    const bool spectrumParameter   = parameter.type == "spectrum";
                    if (!directFileParameter && !apertureParameter && !spectrumParameter) continue;

                    for (std::string& value : *values) {
                        if (value.empty()) continue;
                        if (apertureParameter && IsBuiltInApertureName(value)) continue;
                        if (spectrumParameter && !IsPathLike(value) && !std::filesystem::exists(this->searchDirectory / std::filesystem::path(value))) continue;
                        value = this->ResolveResourcePath(value);
                    }
                }
            }

            [[nodiscard]] std::vector<Scene::Parameter> MergeParameters(const std::vector<Scene::Parameter>& attributes, std::vector<Scene::Parameter> parameters, const EntityUse entityUse) const {
                std::vector<Scene::Parameter> merged;
                merged.reserve(attributes.size() + parameters.size());
                for (Scene::Parameter parameter : attributes) {
                    parameter.may_be_unused = true;
                    merged.push_back(std::move(parameter));
                }
                for (Scene::Parameter& parameter : parameters) {
                    merged.push_back(std::move(parameter));
                }
                this->ResolveParameterPaths(&merged, entityUse);
                return merged;
            }

            [[nodiscard]] Scene::Entity Entity(std::string type, std::vector<Scene::Parameter> parameters, const EntityUse entityUse, const Scene::SourceLocation& source, const Scene::ColorSpace color_space) const {
                this->ResolveParameterPaths(&parameters, entityUse);
                return Scene::Entity{
                    .type       = std::move(type),
                    .parameters = std::move(parameters),
                    .color_space = color_space,
                    .source     = source,
                };
            }

            [[nodiscard]] Scene::Entity EntityWithAttributes(std::string type, std::vector<Scene::Parameter> parameters, const std::vector<Scene::Parameter>& attributes, const EntityUse entityUse, const Scene::SourceLocation& source, const Scene::ColorSpace color_space) const {
                return Scene::Entity{
                    .type       = std::move(type),
                    .parameters = this->MergeParameters(attributes, std::move(parameters), entityUse),
                    .color_space = color_space,
                    .source     = source,
                };
            }

            void ParseFile(const std::filesystem::path& path) {
                PbrtTokenStream stream(path);
                while (std::optional<Token> directive = stream.Next()) {
                    this->ParseDirective(stream, *directive);
                }
            }

            [[nodiscard]] std::vector<Scene::Parameter> ParseParameters(PbrtTokenStream& stream) {
                std::vector<Scene::Parameter> parameters;
                while (true) {
                    std::optional<Token> declaration = stream.Next();
                    if (!declaration.has_value()) return parameters;
                    if (declaration->kind != TokenKind::QuotedString) {
                        stream.PushBack(std::move(*declaration));
                        return parameters;
                    }

                    Scene::Parameter parameter = this->ParseParameterDeclaration(*declaration);
                    Token value              = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                    if (value.kind == TokenKind::LeftBracket) {
                        while (true) {
                            Token element = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                            if (element.kind == TokenKind::RightBracket) break;
                            this->AppendParameterValue(&parameter, element);
                        }
                    } else {
                        this->AppendParameterValue(&parameter, value);
                    }
                    parameters.push_back(std::move(parameter));
                }
            }

            [[nodiscard]] Scene::Parameter ParseParameterDeclaration(const Token& declaration) const {
                const std::string& text = declaration.text;
                std::size_t typeBegin = 0;
                while (typeBegin < text.size() && std::isspace(static_cast<unsigned char>(text[typeBegin]))) ++typeBegin;
                if (typeBegin == text.size()) throw ParseError(declaration.source, "PBRT parameter declaration does not contain a type");

                std::size_t typeEnd = typeBegin;
                while (typeEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[typeEnd]))) ++typeEnd;

                std::size_t nameBegin = typeEnd;
                while (nameBegin < text.size() && std::isspace(static_cast<unsigned char>(text[nameBegin]))) ++nameBegin;
                if (nameBegin == text.size()) throw ParseError(declaration.source, std::format("\"{}\" does not contain a parameter name", text));

                std::size_t nameEnd = nameBegin;
                while (nameEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;

                return Scene::Parameter{
                    .type       = text.substr(typeBegin, typeEnd - typeBegin),
                    .name       = text.substr(nameBegin, nameEnd - nameBegin),
                    .color_space = this->graphicsState.color_space,
                    .source     = declaration.source,
                };
            }

            void AppendParameterValue(Scene::Parameter* parameter, const Token& value) const {
                if (parameter->type == "integer") {
                    if (value.kind == TokenKind::QuotedString) throw ParseError(value.source, std::format("\"integer {}\" expects numeric values", parameter->name));
                    if (!std::holds_alternative<std::vector<int>>(parameter->values)) parameter->values = std::vector<int>{};
                    std::get<std::vector<int>>(parameter->values).push_back(ParseIntegerToken(value));
                    return;
                }
                if (parameter->type == "bool") {
                    if (!std::holds_alternative<std::vector<std::uint8_t>>(parameter->values)) parameter->values = std::vector<std::uint8_t>{};
                    std::get<std::vector<std::uint8_t>>(parameter->values).push_back(ParseBoolToken(value));
                    return;
                }
                if (parameter->type == "string" || parameter->type == "texture" || value.kind == TokenKind::QuotedString) {
                    if (value.kind != TokenKind::QuotedString) throw ParseError(value.source, std::format("\"{} {}\" expects quoted string values", parameter->type, parameter->name));
                    if (!std::holds_alternative<std::vector<std::string>>(parameter->values)) parameter->values = std::vector<std::string>{};
                    std::get<std::vector<std::string>>(parameter->values).push_back(value.text);
                    return;
                }

                if (!std::holds_alternative<std::vector<float>>(parameter->values)) parameter->values = std::vector<float>{};
                std::get<std::vector<float>>(parameter->values).push_back(ParseFloatToken(value));
            }

            void ParseDirective(PbrtTokenStream& stream, const Token& directive) {
                if (directive.kind != TokenKind::Word) throw ParseError(directive.source, "PBRT directive must be an unquoted identifier");

                if (directive.text == "AttributeBegin" || directive.text == "TransformBegin") {
                    this->RequireWorld(directive, directive.text);
                    this->stateStack.push_back(this->graphicsState);
                    this->stackKinds.push_back('a');
                    return;
                }
                if (directive.text == "AttributeEnd" || directive.text == "TransformEnd") {
                    this->RequireWorld(directive, directive.text);
                    this->PopGraphicsState(directive);
                    return;
                }
                if (directive.text == "ActiveTransform") {
                    this->ActiveTransform(RequireToken(stream, "ActiveTransform"), directive.source);
                    return;
                }
                if (directive.text == "AreaLightSource") {
                    this->RequireWorld(directive, "AreaLightSource");
                    const std::string type = RequireStringToken(stream, "AreaLightSource");
                    this->graphicsState.area_light = Scene::AreaLight{.entity = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::AreaLight, directive.source, this->graphicsState.color_space)};
                    return;
                }
                if (directive.text == "Accelerator") {
                    this->RequireOptions(directive, "Accelerator");
                    const std::string type = RequireStringToken(stream, "Accelerator");
                    this->scene.render_settings.accelerator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.color_space);
                    return;
                }
                if (directive.text == "Attribute") {
                    std::string target = RequireStringToken(stream, "Attribute");
                    std::vector<Scene::Parameter> parameters = this->ParseParameters(stream);
                    this->Attribute(std::move(target), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Camera") {
                    this->RequireOptions(directive, "Camera");
                    const std::string type = RequireStringToken(stream, "Camera");
                    this->scene.render_settings.camera          = this->Entity(type, this->ParseParameters(stream), EntityUse::Camera, directive.source, this->graphicsState.color_space);
                    this->scene.render_settings.camera_transform = this->WorldFromCameraTransform();
                    this->scene.render_settings.camera_medium    = this->graphicsState.medium_interface.outside;
                    this->namedCoordinateSystems["camera"]     = this->scene.render_settings.camera_transform;
                    return;
                }
                if (directive.text == "ColorSpace") {
                    this->graphicsState.color_space = ParseColorSpaceName(RequireStringToken(stream, "ColorSpace"), directive.source);
                    return;
                }
                if (directive.text == "ConcatTransform") {
                    this->ConcatTransform(stream, directive.source);
                    return;
                }
                if (directive.text == "CoordinateSystem") {
                    this->namedCoordinateSystems[RequireStringToken(stream, "CoordinateSystem")] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "CoordSysTransform") {
                    const std::string name = RequireStringToken(stream, "CoordSysTransform");
                    const std::map<std::string, SceneTransformSet>::const_iterator iter = this->namedCoordinateSystems.find(name);
                    if (iter == this->namedCoordinateSystems.end()) throw ParseError(directive.source, std::format("Unknown coordinate system \"{}\"", name));
                    this->graphicsState.transform = iter->second;
                    return;
                }
                if (directive.text == "Film") {
                    this->RequireOptions(directive, "Film");
                    const std::string type = RequireStringToken(stream, "Film");
                    this->scene.render_settings.film = this->Entity(type, this->ParseParameters(stream), EntityUse::Film, directive.source, this->graphicsState.color_space);
                    return;
                }
                if (directive.text == "Identity") {
                    this->SetActiveTransform(SceneTransform{});
                    return;
                }
                if (directive.text == "Import") {
                    this->RequireWorld(directive, "Import");
                    this->Import(stream, directive.source);
                    return;
                }
                if (directive.text == "Include") {
                    stream.PushFile(this->ResolveIncludePath(RequireStringToken(stream, "Include"), directive.source));
                    return;
                }
                if (directive.text == "Integrator") {
                    this->RequireOptions(directive, "Integrator");
                    const std::string type = RequireStringToken(stream, "Integrator");
                    this->scene.render_settings.integrator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.color_space);
                    return;
                }
                if (directive.text == "LightSource") {
                    this->RequireWorld(directive, "LightSource");
                    this->LightSource(stream, directive.source);
                    return;
                }
                if (directive.text == "LookAt") {
                    std::array<float, 9> values{};
                    for (float& value : values) value = ParseFloatToken(RequireToken(stream, "LookAt"));
                    this->ApplyActiveTransform(LookAt(Vector3{values[0], values[1], values[2]}, Vector3{values[3], values[4], values[5]}, Vector3{values[6], values[7], values[8]}));
                    return;
                }
                if (directive.text == "MakeNamedMaterial") {
                    this->RequireWorld(directive, "MakeNamedMaterial");
                    std::string name = RequireStringToken(stream, "MakeNamedMaterial");
                    std::vector<Scene::Parameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMaterial(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MakeNamedMedium") {
                    std::string name = RequireStringToken(stream, "MakeNamedMedium");
                    std::vector<Scene::Parameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMedium(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Material") {
                    this->RequireWorld(directive, "Material");
                    std::string type = RequireStringToken(stream, "Material");
                    std::vector<Scene::Parameter> parameters = this->ParseParameters(stream);
                    this->Material(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MediumInterface") {
                    this->MediumInterface(stream);
                    return;
                }
                if (directive.text == "NamedMaterial") {
                    this->RequireWorld(directive, "NamedMaterial");
                    this->graphicsState.currentMaterialName = RequireStringToken(stream, "NamedMaterial");
                    return;
                }
                if (directive.text == "ObjectBegin") {
                    this->ObjectBegin(RequireStringToken(stream, "ObjectBegin"), directive.source);
                    return;
                }
                if (directive.text == "ObjectEnd") {
                    this->ObjectEnd(directive.source);
                    return;
                }
                if (directive.text == "ObjectInstance") {
                    this->ObjectInstance(RequireStringToken(stream, "ObjectInstance"), directive.source);
                    return;
                }
                if (directive.text == "Option") {
                    std::string name = RequireStringToken(stream, "Option");
                    Token value = RequireToken(stream, "Option");
                    this->Option(std::move(name), std::move(value.text), directive.source);
                    return;
                }
                if (directive.text == "PixelFilter") {
                    this->RequireOptions(directive, "PixelFilter");
                    const std::string type = RequireStringToken(stream, "PixelFilter");
                    this->scene.render_settings.filter = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.color_space);
                    return;
                }
                if (directive.text == "ReverseOrientation") {
                    this->RequireWorld(directive, "ReverseOrientation");
                    this->graphicsState.reverse_orientation = !this->graphicsState.reverse_orientation;
                    return;
                }
                if (directive.text == "Rotate") {
                    const float angle = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float x     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float y     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float z     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    this->ApplyActiveTransform(Rotate(angle, Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "Sampler") {
                    this->RequireOptions(directive, "Sampler");
                    const std::string type = RequireStringToken(stream, "Sampler");
                    this->scene.render_settings.sampler = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.color_space);
                    return;
                }
                if (directive.text == "Scale") {
                    const float x = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float y = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float z = ParseFloatToken(RequireToken(stream, "Scale"));
                    this->ApplyActiveTransform(Scale(x, y, z));
                    return;
                }
                if (directive.text == "Shape") {
                    this->RequireWorld(directive, "Shape");
                    std::string type = RequireStringToken(stream, "Shape");
                    std::vector<Scene::Parameter> parameters = this->ParseParameters(stream);
                    this->Shape(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Texture") {
                    this->RequireWorld(directive, "Texture");
                    this->Texture(stream, directive.source);
                    return;
                }
                if (directive.text == "Transform") {
                    this->Transform(stream, directive.source);
                    return;
                }
                if (directive.text == "TransformTimes") {
                    this->RequireOptions(directive, "TransformTimes");
                    this->graphicsState.transform.start_time = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    this->graphicsState.transform.end_time   = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    return;
                }
                if (directive.text == "Translate") {
                    const float x = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float y = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float z = ParseFloatToken(RequireToken(stream, "Translate"));
                    this->ApplyActiveTransform(Translate(Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "WorldBegin") {
                    this->RequireOptions(directive, "WorldBegin");
                    const float start_time = this->graphicsState.transform.start_time;
                    const float end_time   = this->graphicsState.transform.end_time;
                    this->currentBlock = BlockState::World;
                    this->graphicsState.transform   = SceneTransformSet{.start_time = start_time, .end_time = end_time};
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    this->namedCoordinateSystems["world"] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "WorldEnd") throw ParseError(directive.source, "WorldEnd is not used by PBRT v4 scene files");
                throw ParseError(directive.source, std::format("Unknown PBRT directive \"{}\"", directive.text));
            }

            void RequireOptions(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::Options) throw ParseError(directive.source, std::format("{} is only valid before WorldBegin", name));
            }

            void RequireWorld(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(directive.source, std::format("{} is only valid after WorldBegin", name));
            }

            void RequireWorld(const Scene::SourceLocation& source, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(source, std::format("{} is only valid after WorldBegin", name));
            }

            void ApplyActiveTransform(const SceneTransform& transform) {
                ApplyTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void SetActiveTransform(const SceneTransform& transform) {
                SetTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void ActiveTransform(const Token& token, const Scene::SourceLocation& source) {
                if (token.kind != TokenKind::Word) throw ParseError(token.source, "ActiveTransform expects StartTime, EndTime, or All");
                if (token.text == "StartTime") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = false;
                    return;
                }
                if (token.text == "EndTime") {
                    this->graphicsState.activeStart = false;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                if (token.text == "All") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                throw ParseError(source, std::format("Unknown ActiveTransform target \"{}\"", token.text));
            }

            [[nodiscard]] SceneTransformSet WorldFromCameraTransform() const {
                SceneTransformSet result{
                    .start     = Inverse(this->graphicsState.transform.start),
                    .end       = Inverse(this->graphicsState.transform.end),
                    .start_time = this->graphicsState.transform.start_time,
                    .end_time   = this->graphicsState.transform.end_time,
                };
                RefreshAnimatedFlag(&result);
                return result;
            }

            void ReadBracketedMatrix(PbrtTokenStream& stream, const std::string_view context, std::array<float, 16>* values) const {
                Token open = RequireToken(stream, context);
                if (open.kind != TokenKind::LeftBracket) throw ParseError(open.source, std::format("{} expects '['", context));
                for (float& value : *values) value = ParseFloatToken(RequireToken(stream, context));
                Token close = RequireToken(stream, context);
                if (close.kind != TokenKind::RightBracket) throw ParseError(close.source, std::format("{} expects ']'", context));
            }

            void Transform(PbrtTokenStream& stream, const Scene::SourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "Transform", &values);
                this->SetActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void ConcatTransform(PbrtTokenStream& stream, const Scene::SourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "ConcatTransform", &values);
                this->ApplyActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void PopGraphicsState(const Token& directive) {
                if (this->stateStack.empty()) throw ParseError(directive.source, std::format("Unmatched {}", directive.text));
                if (this->stackKinds.empty() || this->stackKinds.back() != 'a') throw ParseError(directive.source, std::format("{} does not match the current graphics state stack", directive.text));
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
            }

            void Attribute(std::string target, std::vector<Scene::Parameter> parameters, const Scene::SourceLocation& source) {
                std::vector<Scene::Parameter>* currentAttributes = nullptr;
                if (target == "shape")
                    currentAttributes = &this->graphicsState.shapeAttributes;
                else if (target == "light")
                    currentAttributes = &this->graphicsState.lightAttributes;
                else if (target == "material")
                    currentAttributes = &this->graphicsState.materialAttributes;
                else if (target == "medium")
                    currentAttributes = &this->graphicsState.mediumAttributes;
                else if (target == "texture")
                    currentAttributes = &this->graphicsState.textureAttributes;
                else
                    throw ParseError(source, std::format("Unknown Attribute target \"{}\"", target));

                for (Scene::Parameter& parameter : parameters) {
                    parameter.may_be_unused = true;
                    parameter.color_space  = this->graphicsState.color_space;
                    currentAttributes->push_back(std::move(parameter));
                }
            }

            void Option(std::string name, std::string value, const Scene::SourceLocation& source) {
                this->scene.render_settings.options.push_back(Scene::Option{
                    .name   = std::move(name),
                    .value  = std::move(value),
                    .source = source,
                });
            }

            void MediumInterface(PbrtTokenStream& stream) {
                const std::string inside = RequireStringToken(stream, "MediumInterface");
                std::optional<Token> outsideToken = stream.Next();
                if (!outsideToken.has_value()) {
                    this->graphicsState.medium_interface = Scene::MediumInterface{.inside = inside, .outside = inside};
                    return;
                }
                if (outsideToken->kind == TokenKind::QuotedString) {
                    this->graphicsState.medium_interface = Scene::MediumInterface{.inside = inside, .outside = std::move(outsideToken->text)};
                    return;
                }
                stream.PushBack(std::move(*outsideToken));
                this->graphicsState.medium_interface = Scene::MediumInterface{.inside = inside, .outside = inside};
            }

            void MakeNamedMedium(std::string name, std::vector<Scene::Parameter> parameters, const Scene::SourceLocation& source) {
                this->RequireUniqueName(this->mediumNames, "medium", name, source);
                Scene::Entity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.mediumAttributes, EntityUse::Medium, source, this->graphicsState.color_space);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMedium \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->mediumNames.insert(name);
                this->scene.media.push_back(Scene::Medium{
                    .name      = std::move(name),
                    .entity    = std::move(entity),
                    .transform = this->graphicsState.transform,
                });
            }

            void LightSource(PbrtTokenStream& stream, const Scene::SourceLocation& source) {
                const std::string type = RequireStringToken(stream, "LightSource");
                this->scene.lights.push_back(Scene::Light{
                    .name      = std::format("__light_{}", this->scene.lights.size()),
                    .entity    = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::Light, source, this->graphicsState.color_space),
                    .transform = this->graphicsState.transform,
                    .medium    = this->graphicsState.medium_interface.outside,
                });
            }

            void Material(std::string type, std::vector<Scene::Parameter> parameters, const Scene::SourceLocation& source) {
                if (type.empty()) type = "interface";
                const std::string name = std::format("__inline_material_{}", this->inlineMaterialCount);
                ++this->inlineMaterialCount;
                this->scene.materials.push_back(Scene::Material{
                    .name   = name,
                    .entity = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.color_space),
                });
                this->material_names.insert(name);
                this->graphicsState.currentMaterialName = name;
            }

            void MakeNamedMaterial(std::string name, std::vector<Scene::Parameter> parameters, const Scene::SourceLocation& source) {
                this->RequireUniqueName(this->material_names, "material", name, source);
                Scene::Entity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.color_space);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMaterial \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->material_names.insert(name);
                this->scene.materials.push_back(Scene::Material{
                    .name   = std::move(name),
                    .entity = std::move(entity),
                });
            }

            void Texture(PbrtTokenStream& stream, const Scene::SourceLocation& source) {
                std::string name = RequireStringToken(stream, "Texture");
                std::string kind = RequireStringToken(stream, "Texture");
                std::string type = RequireStringToken(stream, "Texture");
                if (kind != "float" && kind != "spectrum") throw ParseError(source, std::format("Texture \"{}\" has unsupported value type \"{}\"", name, kind));
                this->RequireUniqueName(kind == "float" ? this->floatTextureNames : this->spectrumTextureNames, "texture", name, source);
                if (kind == "float")
                    this->floatTextureNames.insert(name);
                else
                    this->spectrumTextureNames.insert(name);
                this->scene.textures.push_back(Scene::Texture{
                    .name      = std::move(name),
                    .kind      = std::move(kind),
                    .entity    = this->EntityWithAttributes(std::move(type), this->ParseParameters(stream), this->graphicsState.textureAttributes, EntityUse::Texture, source, this->graphicsState.color_space),
                    .transform = this->graphicsState.transform,
                });
            }

            void Shape(std::string type, std::vector<Scene::Parameter> parameters, const Scene::SourceLocation& source) {
                Scene::Shape shape{
                    .name               = std::format("__shape_{}", this->shapeCount),
                    .entity             = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.shapeAttributes, EntityUse::Shape, source, this->graphicsState.color_space),
                    .transform          = this->graphicsState.transform,
                    .reverse_orientation = this->graphicsState.reverse_orientation,
                    .material_name       = this->graphicsState.currentMaterialName,
                    .area_light          = this->graphicsState.area_light,
                    .medium_interface    = this->graphicsState.medium_interface,
                };
                ++this->shapeCount;

                if (this->activeObjectDefinition.has_value())
                    this->activeObjectDefinition->shapes.push_back(std::move(shape));
                else
                    this->scene.shapes.push_back(std::move(shape));
            }

            void ObjectBegin(std::string name, const Scene::SourceLocation& source) {
                this->RequireWorld(source, "ObjectBegin");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectBegin cannot be nested inside another ObjectBegin");
                this->RequireUniqueName(this->objectDefinitionNames, "object definition", name, source);
                this->stateStack.push_back(this->graphicsState);
                this->stackKinds.push_back('o');
                this->objectDefinitionNames.insert(name);
                this->activeObjectDefinition = Scene::ObjectDefinition{.name = std::move(name), .source = source};
            }

            void ObjectEnd(const Scene::SourceLocation& source) {
                this->RequireWorld(source, "ObjectEnd");
                if (!this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectEnd without ObjectBegin");
                if (this->stateStack.empty() || this->stackKinds.empty() || this->stackKinds.back() != 'o') throw ParseError(source, "ObjectEnd does not match the current graphics state stack");
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
                this->scene.object_definitions.push_back(std::move(*this->activeObjectDefinition));
                this->activeObjectDefinition.reset();
            }

            void ObjectInstance(std::string name, const Scene::SourceLocation& source) {
                this->RequireWorld(source, "ObjectInstance");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectInstance cannot be used inside ObjectBegin");
                this->scene.object_instances.push_back(Scene::ObjectInstance{
                    .name           = std::format("__instance_{}", this->scene.object_instances.size()),
                    .definition_name = std::move(name),
                    .transform      = this->graphicsState.transform,
                    .source         = source,
                });
            }

            void Import(PbrtTokenStream& stream, const Scene::SourceLocation& source) {
                const std::filesystem::path importPath = this->ResolveIncludePath(RequireStringToken(stream, "Import"), source);
                const GraphicsState savedState         = this->graphicsState;
                this->ParseFile(importPath);
                this->graphicsState = savedState;
            }

            void RequireUniqueName(std::set<std::string>& names, const std::string_view kind, const std::string& name, const Scene::SourceLocation& source) {
                if (name.empty()) throw ParseError(source, std::format("PBRT {} name must not be empty", kind));
                if (!names.insert(name).second) throw ParseError(source, std::format("PBRT {} \"{}\" is already defined", kind, name));
            }

            void Finish() const {
                if (!this->stateStack.empty()) throw std::runtime_error(std::format("{}: missing AttributeEnd/ObjectEnd for scene parser stack", this->scene.source));
                if (this->activeObjectDefinition.has_value()) throw std::runtime_error(std::format("{}: missing ObjectEnd", this->scene.source));
            }

            Scene::ResolvedScene scene{};
            std::filesystem::path inputFile;
            std::filesystem::path searchDirectory;
            GraphicsState graphicsState{};
            BlockState currentBlock{BlockState::Options};
            std::vector<GraphicsState> stateStack{};
            std::vector<char> stackKinds{};
            std::map<std::string, SceneTransformSet> namedCoordinateSystems{};
            std::optional<Scene::ObjectDefinition> activeObjectDefinition{};
            std::set<std::string> material_names{};
            std::set<std::string> mediumNames{};
            std::set<std::string> floatTextureNames{};
            std::set<std::string> spectrumTextureNames{};
            std::set<std::string> objectDefinitionNames{};
            std::size_t inlineMaterialCount{};
            std::size_t shapeCount{};
        };

        [[nodiscard]] std::filesystem::path SceneRoot() {
            return std::filesystem::absolute(std::filesystem::path(SPECTRA_SCENES_ROOT)).lexically_normal();
        }

        [[nodiscard]] std::filesystem::path SceneFilenameStem(const std::filesystem::path& path) {
            std::filesystem::path filename = path.filename();
            if (HasExtension(filename, ".gz")) filename = filename.stem();
            if (HasExtension(filename, ".pbrt")) filename = filename.stem();
            return filename;
        }

        [[nodiscard]] std::string SceneDisplayName(const std::filesystem::path& relativePath) {
            return SceneFilenameStem(relativePath).string();
        }

        [[nodiscard]] std::filesystem::path ResolveScenePathByUniqueStem(const std::filesystem::path& root, const std::string& name) {
            std::optional<std::filesystem::path> match;
            if (!std::filesystem::exists(root)) throw std::runtime_error(std::format("{}: scene root does not exist", root.string()));
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                const std::filesystem::path path = entry.path();
                if (!IsSceneFile(path)) continue;
                if (SceneFilenameStem(path).string() != name) continue;
                if (match.has_value()) throw std::runtime_error(std::format("Scene alias \"{}\" is ambiguous; pass a scene-root-relative .pbrt path", name));
                match = path;
            }
            if (match.has_value()) return std::filesystem::absolute(*match).lexically_normal();
            return {};
        }

        [[nodiscard]] std::filesystem::path ResolveScenePath(const std::string_view requestedName) {
            const std::string requested(requestedName);
            const std::filesystem::path root = SceneRoot();
            if (requested == "default") return (root / "pbrt-book" / "book.pbrt").lexically_normal();

            const std::filesystem::path asPath(requested);
            if (std::filesystem::is_regular_file(asPath)) return std::filesystem::absolute(asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / asPath)) return std::filesystem::absolute(root / asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / (requested + ".pbrt"))) return std::filesystem::absolute(root / (requested + ".pbrt")).lexically_normal();
            if (std::filesystem::is_regular_file(root / requested / (requested + ".pbrt"))) return std::filesystem::absolute(root / requested / (requested + ".pbrt")).lexically_normal();

            const std::filesystem::path uniqueStem = ResolveScenePathByUniqueStem(root, requested);
            if (!uniqueStem.empty()) return uniqueStem;

            throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", requested));
        }
    } // namespace

    Scene::Info describe_scene(const Scene::ResolvedScene& scene) {
        const auto one_float_parameter = [](const std::vector<Scene::Parameter>& parameters, const std::string& name, const float default_value) {
            for (const Scene::Parameter& parameter : parameters) {
                if (parameter.type != "float" && parameter.type != "integer") continue;
                if (parameter.name != name) continue;
                if (parameter.type == "float") {
                    const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                    if (values == nullptr || values->empty()) throw std::runtime_error(std::format("PBRT parameter \"{}\" must contain at least one float value", name));
                    return values->front();
                }
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr || values->empty()) throw std::runtime_error(std::format("PBRT parameter \"{}\" must contain at least one integer value", name));
                return static_cast<float>(values->front());
            }
            return default_value;
        };

        std::size_t definition_shape_count = 0;
        std::size_t definition_area_light_count = 0;
        for (const Scene::ObjectDefinition& definition : scene.object_definitions) {
            definition_shape_count += definition.shapes.size();
            for (const Scene::Shape& shape : definition.shapes)
                if (shape.area_light.has_value()) ++definition_area_light_count;
        }

        std::size_t area_light_count = definition_area_light_count;
        for (const Scene::Shape& shape : scene.shapes)
            if (shape.area_light.has_value()) ++area_light_count;

        std::size_t infinite_light_count = 0;
        for (const Scene::Light& light : scene.lights)
            if (light.entity.type == "infinite") ++infinite_light_count;

        const float camera_fov = one_float_parameter(scene.render_settings.camera.parameters, "fov", scene.render_settings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(camera_fov > 0.0f && camera_fov < 180.0f)) throw std::runtime_error(std::format("PBRT scene \"{}\" has invalid camera FOV {}", scene.name, camera_fov));

        return Scene::Info{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = scene.render_settings.camera.type,
            .sampler                 = scene.render_settings.sampler.type,
            .integrator              = scene.render_settings.integrator.type,
            .accelerator             = scene.render_settings.accelerator.type,
            .shape_count             = scene.shapes.size() + definition_shape_count,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = area_light_count,
            .infinite_light_count    = infinite_light_count,
            .object_definition_count = scene.object_definitions.size(),
            .object_instance_count   = scene.object_instances.size(),
            .camera_fov_degrees      = camera_fov,
        };
    }

    Scene Scene::parse_pbrt(const std::string_view scene_id) {
        if (scene_id.empty()) throw std::runtime_error("PBRT scene parse requires a non-empty scene id");
        const std::filesystem::path scenePath = ResolveScenePath(scene_id);
        ScenePbrtBuilder builder(scenePath);
        Scene::ResolvedScene scene = builder.Parse();
        scene.name = std::string{scene_id};
        scene.title = SceneDisplayName(scenePath);
        if (scene.revision.value == 0) scene.revision = Scene::Revision{1};
        return Scene{std::move(scene)};
    }

} // namespace spectra::scene
