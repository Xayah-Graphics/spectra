module spectra.scene_runtime.plugin_conversion;

import std;
import spectra.scene;
import spectra.scene_runtime.plugin_c_abi;
import spectra.scene_runtime.contracts;

namespace spectra::scene_runtime {
        [[nodiscard]] std::string lowercase_ascii(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
            return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
        }

        [[nodiscard]] std::string abi_string(const char* value, const std::string_view context, const bool allow_empty) {
            const std::string_view view = value == nullptr ? std::string_view{} : std::string_view{value};
            if (!allow_empty && view.empty()) throw std::runtime_error(std::format("{} must not be empty", context));
            return std::string{view};
        }

        template <typename Value>
        [[nodiscard]] std::span<const Value> abi_span(const Value* data, const std::uint64_t count, const std::string_view context) {
            if (count == 0u) return {};
            if (data == nullptr) throw std::runtime_error(std::format("{} data pointer is null", context));
            if (count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::format("{} item count is too large", context));
            return std::span<const Value>{data, static_cast<std::size_t>(count)};
        }

        template <typename Span>
            requires requires(const Span& span) {
                span.data;
                span.count;
            }
        [[nodiscard]] auto abi_span(const Span& span, const std::string_view context) {
            return abi_span(span.data, span.count, context);
        }

        [[nodiscard]] std::string dynamic_scene_item_kind_name(const std::uint32_t kind) {
            switch (kind) {
            case SPECTRA_DYNAMIC_SCENE_ITEM_MATERIAL: return "material";
            case SPECTRA_DYNAMIC_SCENE_ITEM_LIGHT: return "light";
            case SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA: return "camera";
            case SPECTRA_DYNAMIC_SCENE_ITEM_MESH: return "mesh";
            case SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE: return "sphere";
            case SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD: return "point cloud";
            case SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME: return "volume grid";
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET: return "viewport segment set";
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID: return "viewport voxel grid";
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL: return "viewport camera visual";
            default: return std::format("unknown({})", kind);
            }
        }

        [[nodiscard]] bool dynamic_scene_document_item_kind_allowed(const std::uint32_t kind) {
            switch (kind) {
            case SPECTRA_DYNAMIC_SCENE_ITEM_MATERIAL:
            case SPECTRA_DYNAMIC_SCENE_ITEM_LIGHT:
            case SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA:
            case SPECTRA_DYNAMIC_SCENE_ITEM_MESH:
            case SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE:
            case SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL:
                return true;
            default:
                return false;
            }
        }

        [[nodiscard]] bool dynamic_scene_frame_item_kind_allowed(const std::uint32_t kind) {
            switch (kind) {
            case SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA:
            case SPECTRA_DYNAMIC_SCENE_ITEM_MESH:
            case SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE:
            case SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID:
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL:
                return true;
            default:
                return false;
            }
        }

        template <typename Value>
        [[nodiscard]] std::span<const Value> abi_typed_span(const SpectraDynamicSceneTypedSpan& span, const std::uint32_t expected_kind, const std::string_view context) {
            if (span.kind != expected_kind) throw std::runtime_error(std::format("{} kind {} does not match expected {}", context, dynamic_scene_item_kind_name(span.kind), dynamic_scene_item_kind_name(expected_kind)));
            if (span.item_size != sizeof(Value)) throw std::runtime_error(std::format("{} item size {} does not match expected {}", context, span.item_size, sizeof(Value)));
            if (span.count == 0u) throw std::runtime_error(std::format("{} typed span is empty; omit unused item kinds instead", context));
            if (span.data == nullptr) throw std::runtime_error(std::format("{} data pointer is null", context));
            if (span.count > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) throw std::runtime_error(std::format("{} item count is too large", context));
            return std::span<const Value>{static_cast<const Value*>(span.data), static_cast<std::size_t>(span.count)};
        }

        template <typename IsAllowed>
        void require_valid_typed_scene_items(const std::span<const SpectraDynamicSceneTypedSpan> items, const IsAllowed& is_allowed, const std::string_view context) {
            std::set<std::uint32_t> kinds{};
            for (const SpectraDynamicSceneTypedSpan& item : items) {
                if (!is_allowed(item.kind)) throw std::runtime_error(std::format("{} contains unsupported item kind {}", context, dynamic_scene_item_kind_name(item.kind)));
                if (!kinds.insert(item.kind).second) throw std::runtime_error(std::format("{} contains duplicated item kind {}", context, dynamic_scene_item_kind_name(item.kind)));
            }
        }

        [[nodiscard]] float finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] double finite_double(const double value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] scene::Vector3 make_vector3(const float (&value)[3], const std::string_view context) {
            return scene::Vector3{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
            };
        }

        [[nodiscard]] scene::Vector4 make_vector4(const float (&value)[4], const std::string_view context) {
            return scene::Vector4{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
                finite_float(value[3], std::format("{} w", context)),
            };
        }

        [[nodiscard]] scene::Transform make_transform(const SpectraDynamicSceneTransform& transform, const std::string_view context) {
            return scene::Transform{
                .position = make_vector3(transform.position, std::format("{} position", context)),
                .rotation = scene::Quaternion{
                    finite_float(transform.rotation[0], std::format("{} rotation x", context)),
                    finite_float(transform.rotation[1], std::format("{} rotation y", context)),
                    finite_float(transform.rotation[2], std::format("{} rotation z", context)),
                    finite_float(transform.rotation[3], std::format("{} rotation w", context)),
                },
                .scale = make_vector3(transform.scale, std::format("{} scale", context)),
            };
        }

        [[nodiscard]] scene::Scene::ViewportSegmentWidthMode viewport_segment_width_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentWidthMode::Screen;
            case 1u: return scene::Scene::ViewportSegmentWidthMode::World;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment width mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportSegmentDepthMode viewport_segment_depth_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportSegmentDepthMode::DepthTested;
            case 1u: return scene::Scene::ViewportSegmentDepthMode::AlwaysVisible;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment depth mode {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridSourceKind viewport_voxel_grid_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridSourceKind::IndexList;
            case 1u: return scene::Scene::ViewportVoxelGridSourceKind::Bitfield;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid source kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGridIndexEncoding viewport_voxel_grid_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::ViewportVoxelGridIndexEncoding::Linear;
            case 1u: return scene::Scene::ViewportVoxelGridIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid index encoding {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelSourceKind volume_channel_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelSourceKind::Values;
            case 1u: return scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel source kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelIndexEncoding volume_channel_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelIndexEncoding::Linear;
            case 1u: return scene::Scene::VolumeChannelIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel index encoding {}", context, value));
        }

        [[nodiscard]] scene::Scene::VolumeChannelFormat volume_channel_format_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::VolumeChannelFormat::Float32;
            case 1u: return scene::Scene::VolumeChannelFormat::Float32x3;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel format {}", context, value));
        }

        [[nodiscard]] std::uint32_t volume_channel_component_count(const scene::Scene::VolumeChannelFormat format) {
            switch (format) {
            case scene::Scene::VolumeChannelFormat::Float32: return 1u;
            case scene::Scene::VolumeChannelFormat::Float32x3: return 3u;
            }
            throw std::runtime_error("Unknown dynamic scene volume channel format");
        }

        [[nodiscard]] scene::Scene::SceneEntityKind scene_entity_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return scene::Scene::SceneEntityKind::Mesh;
            case 1u: return scene::Scene::SceneEntityKind::Sphere;
            case 2u: return scene::Scene::SceneEntityKind::PointCloud;
            case 3u: return scene::Scene::SceneEntityKind::VolumeGrid;
            case 4u: return scene::Scene::SceneEntityKind::Camera;
            case 5u: return scene::Scene::SceneEntityKind::Light;
            }
            throw std::runtime_error(std::format("{} has invalid scene entity kind {}", context, value));
        }

        [[nodiscard]] scene::Scene::SceneEntityRef make_entity_ref(const SpectraDynamicSceneEntityRef& entity, const std::string_view context) {
            return scene::Scene::SceneEntityRef{
                .kind = scene_entity_kind_from_u32(entity.kind, context),
                .name = abi_string(entity.name, std::format("{} name", context), false),
            };
        }

        [[nodiscard]] scene::Scene::PreviewSurfaceKind preview_surface_kind_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "lit_surface") return scene::Scene::PreviewSurfaceKind::LitSurface;
            if (value == "unlit_surface") return scene::Scene::PreviewSurfaceKind::UnlitSurface;
            if (value == "emissive_surface") return scene::Scene::PreviewSurfaceKind::EmissiveSurface;
            if (value == "volume") return scene::Scene::PreviewSurfaceKind::Volume;
            if (value == "point_sprite") return scene::Scene::PreviewSurfaceKind::PointGlyph;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid preview surface kind \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewAlphaMode preview_alpha_mode_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "opaque") return scene::Scene::PreviewAlphaMode::Opaque;
            if (value == "masked") return scene::Scene::PreviewAlphaMode::Masked;
            if (value == "blend") return scene::Scene::PreviewAlphaMode::Blend;
            throw std::runtime_error(std::format("Dynamic scene material \"{}\" has invalid alpha mode \"{}\"", material_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewLightKind light_kind_from_string(const std::string_view value, const std::string_view light_name) {
            if (value == "directional") return scene::Scene::PreviewLightKind::Directional;
            if (value == "point") return scene::Scene::PreviewLightKind::Point;
            if (value == "spot") return scene::Scene::PreviewLightKind::Spot;
            if (value == "area") return scene::Scene::PreviewLightKind::Area;
            if (value == "environment") return scene::Scene::PreviewLightKind::Environment;
            throw std::runtime_error(std::format("Dynamic scene light \"{}\" has invalid kind \"{}\"", light_name, value));
        }

        [[nodiscard]] scene::Scene::PreviewMaterial make_material(const SpectraDynamicSceneMaterial& material) {
            const std::string name = abi_string(material.name, "Dynamic scene material name", false);
            return scene::Scene::PreviewMaterial{
                .name = name,
                .surface_kind = preview_surface_kind_from_string(abi_string(material.model, std::format("Dynamic scene material \"{}\" model", name), true), name),
                .alpha_mode = preview_alpha_mode_from_string(abi_string(material.alpha_mode, std::format("Dynamic scene material \"{}\" alpha mode", name), true), name),
                .base_color = make_vector4(material.base_color, std::format("Dynamic scene material \"{}\" base color", name)),
                .emission_color = make_vector3(material.emission_color, std::format("Dynamic scene material \"{}\" emission color", name)),
                .emission_strength = finite_float(material.emission_strength, std::format("Dynamic scene material \"{}\" emission strength", name)),
                .roughness = finite_float(material.roughness, std::format("Dynamic scene material \"{}\" roughness", name)),
                .metallic = finite_float(material.metallic, std::format("Dynamic scene material \"{}\" metallic", name)),
                .alpha_cutoff = finite_float(material.alpha_cutoff, std::format("Dynamic scene material \"{}\" alpha cutoff", name)),
                .volume_density_scale = finite_float(material.volume_density_scale, std::format("Dynamic scene material \"{}\" volume density scale", name)),
                .volume_temperature_scale = finite_float(material.volume_temperature_scale, std::format("Dynamic scene material \"{}\" volume temperature scale", name)),
            };
        }

        [[nodiscard]] scene::Scene::PreviewLight make_light(const SpectraDynamicSceneLight& light) {
            const std::string name = abi_string(light.name, "Dynamic scene light name", false);
            return scene::Scene::PreviewLight{
                .name = name,
                .kind = light_kind_from_string(abi_string(light.kind, std::format("Dynamic scene light \"{}\" kind", name), true), name),
                .transform = make_transform(light.transform, std::format("Dynamic scene light \"{}\"", name)),
                .color = make_vector3(light.color, std::format("Dynamic scene light \"{}\" color", name)),
                .intensity = finite_float(light.intensity, std::format("Dynamic scene light \"{}\" intensity", name)),
                .cone_angle_degrees = finite_float(light.cone_angle_degrees, std::format("Dynamic scene light \"{}\" cone angle", name)),
            };
        }

        [[nodiscard]] scene::CameraProjection camera_projection(const SpectraDynamicSceneCamera& camera, const std::string& name) {
            scene::CameraProjection projection{
                .near_plane = finite_float(camera.near_plane, std::format("Dynamic scene camera \"{}\" near plane", name)),
                .far_plane = finite_float(camera.far_plane, std::format("Dynamic scene camera \"{}\" far plane", name)),
            };
            switch (camera.projection) {
            case 0u:
                projection.kind = scene::CameraProjectionKind::Perspective;
                projection.vertical_fov_degrees = finite_float(camera.vertical_fov_degrees, std::format("Dynamic scene camera \"{}\" vertical fov", name));
                return projection;
            case 1u:
                projection.kind = scene::CameraProjectionKind::Pinhole;
                projection.image_width = camera.image_width;
                projection.image_height = camera.image_height;
                projection.fx = finite_float(camera.fx, std::format("Dynamic scene camera \"{}\" fx", name));
                projection.fy = finite_float(camera.fy, std::format("Dynamic scene camera \"{}\" fy", name));
                projection.cx = finite_float(camera.cx, std::format("Dynamic scene camera \"{}\" cx", name));
                projection.cy = finite_float(camera.cy, std::format("Dynamic scene camera \"{}\" cy", name));
                return projection;
            }
            throw std::runtime_error(std::format("Dynamic scene camera \"{}\" has invalid projection {}", name, camera.projection));
        }

        [[nodiscard]] scene::Scene::Camera make_camera(const SpectraDynamicSceneCamera& camera) {
            const std::string name = abi_string(camera.name, "Dynamic scene camera name", false);
            const std::string local_coordinate_system_name = abi_string(camera.local_coordinate_system, std::format("Dynamic scene camera \"{}\" local coordinate system", name), false);
            const scene::Transform transform = make_transform(camera.transform, std::format("Dynamic scene camera \"{}\"", name));
            const scene::Vector3 target = make_vector3(camera.target, std::format("Dynamic scene camera \"{}\" target", name));
            const scene::Vector3 up = make_vector3(camera.up, std::format("Dynamic scene camera \"{}\" up", name));
            return scene::Scene::Camera{
                .name = name,
                .view = scene::CameraViewState{
                    .pose = scene::CameraPose{
                        .position = transform.position,
                        .orientation = scene::normalized_quaternion(transform.rotation, std::format("Dynamic scene camera \"{}\" orientation", name)),
                        .local_convention = scene::coordinate_system(local_coordinate_system_name).convention,
                    },
                    .focus = target,
                    .navigation_up = scene::normalize(up, std::format("Dynamic scene camera \"{}\" up", name)),
                    .projection = camera_projection(camera, name),
                },
            };
        }

        [[nodiscard]] scene::Scene::Mesh make_mesh(const SpectraDynamicSceneMesh& mesh, const bool dynamic) {
            const std::string name = abi_string(mesh.name, "Dynamic scene mesh name", false);
            scene::Scene::Mesh result{
                .name = name,
                .material_name = abi_string(mesh.material_name, std::format("Dynamic scene mesh \"{}\" material name", name), false),
                .transform = make_transform(mesh.transform, std::format("Dynamic scene mesh \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicSceneMeshVertex> vertices = abi_span(mesh.vertices.data, mesh.vertices.count, std::format("Dynamic scene mesh \"{}\" vertices", name));
            result.positions.reserve(vertices.size());
            result.normals.reserve(vertices.size());
            for (std::size_t index = 0u; index < vertices.size(); ++index) {
                result.positions.push_back(make_vector3(vertices[index].position, std::format("Dynamic scene mesh \"{}\" vertex #{} position", name, index)));
                result.normals.push_back(make_vector3(vertices[index].normal, std::format("Dynamic scene mesh \"{}\" vertex #{} normal", name, index)));
            }
            const std::span<const std::uint32_t> indices = abi_span(mesh.indices.data, mesh.indices.count, std::format("Dynamic scene mesh \"{}\" indices", name));
            result.indices.assign(indices.begin(), indices.end());
            if (result.positions.empty()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain vertices", name));
            if (result.indices.empty() || result.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" must contain triangle indices", name));
            for (const std::uint32_t index : result.indices)
                if (index >= result.positions.size()) throw std::runtime_error(std::format("Dynamic scene mesh \"{}\" contains an out-of-range vertex index", name));
            return result;
        }

        [[nodiscard]] scene::Scene::Sphere make_sphere(const SpectraDynamicSceneSphere& sphere, const bool dynamic) {
            const std::string name = abi_string(sphere.name, "Dynamic scene sphere name", false);
            const float radius = finite_float(sphere.radius, std::format("Dynamic scene sphere \"{}\" radius", name));
            if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene sphere \"{}\" radius must be positive", name));
            return scene::Scene::Sphere{
                .name = name,
                .radius = radius,
                .material_name = abi_string(sphere.material_name, std::format("Dynamic scene sphere \"{}\" material name", name), false),
                .transform = make_transform(sphere.transform, std::format("Dynamic scene sphere \"{}\"", name)),
                .dynamic = dynamic,
            };
        }

        [[nodiscard]] scene::Scene::PointCloud make_point_cloud(const SpectraDynamicScenePointCloud& point_cloud, const bool dynamic) {
            const std::string name = abi_string(point_cloud.name, "Dynamic scene point cloud name", false);
            scene::Scene::PointCloud result{
                .name = name,
                .material_name = abi_string(point_cloud.material_name, std::format("Dynamic scene point cloud \"{}\" material name", name), false),
                .transform = make_transform(point_cloud.transform, std::format("Dynamic scene point cloud \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraDynamicScenePoint> points = abi_span(point_cloud.points.data, point_cloud.points.count, std::format("Dynamic scene point cloud \"{}\" points", name));
            result.positions.reserve(points.size());
            result.normals.reserve(points.size());
            result.colors.reserve(points.size());
            result.radii.reserve(points.size());
            for (std::size_t index = 0u; index < points.size(); ++index) {
                result.positions.push_back(make_vector3(points[index].position, std::format("Dynamic scene point cloud \"{}\" point #{} position", name, index)));
                result.normals.push_back(make_vector3(points[index].normal, std::format("Dynamic scene point cloud \"{}\" point #{} normal", name, index)));
                result.colors.push_back(make_vector4(points[index].color, std::format("Dynamic scene point cloud \"{}\" point #{} color", name, index)));
                const float radius = finite_float(points[index].radius, std::format("Dynamic scene point cloud \"{}\" point #{} radius", name, index));
                if (radius <= 0.0f) throw std::runtime_error(std::format("Dynamic scene point cloud \"{}\" point #{} radius must be positive", name, index));
                result.radii.push_back(radius);
            }
            return result;
        }

        [[nodiscard]] scene::Scene::VolumeGrid make_volume(const SpectraDynamicSceneVolume& volume, const bool dynamic) {
            const std::string name = abi_string(volume.name, "Dynamic scene volume name", false);
            scene::Scene::VolumeGrid result{
                .name = name,
                .dimensions = {volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]},
                .origin = make_vector3(volume.origin, std::format("Dynamic scene volume \"{}\" origin", name)),
                .voxel_size = make_vector3(volume.voxel_size, std::format("Dynamic scene volume \"{}\" voxel size", name)),
                .material_name = abi_string(volume.material_name, std::format("Dynamic scene volume \"{}\" material name", name), false),
                .dynamic = dynamic,
            };
            if (result.dimensions[0] == 0u || result.dimensions[1] == 0u || result.dimensions[2] == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" dimensions must be positive", name));
            const std::span<const SpectraDynamicSceneVolumeChannel> channels = abi_span(volume.channels.data, volume.channels.count, std::format("Dynamic scene volume \"{}\" channels", name));
            for (const SpectraDynamicSceneVolumeChannel& channel : channels) {
                const std::string channel_name = abi_string(channel.name, std::format("Dynamic scene volume \"{}\" channel name", name), false);
                scene::Scene::VolumeChannel converted{
                    .name = channel_name,
                    .dimensions = {channel.dimensions[0], channel.dimensions[1], channel.dimensions[2]},
                    .format = volume_channel_format_from_u32(channel.format, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .source_kind = volume_channel_source_kind_from_u32(channel.source_kind, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .index_encoding = volume_channel_index_encoding_from_u32(channel.index_encoding, std::format("Dynamic scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .buffer_id = channel.buffer_id,
                    .external_device_pointer = channel.external_device_pointer,
                    .source_byte_size = channel.source_byte_size,
                    .revision = channel.revision,
                };
                if (converted.dimensions != result.dimensions) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" dimensions do not match", name, converted.name));
                const std::uint64_t cell_count = static_cast<std::uint64_t>(converted.dimensions[0]) * static_cast<std::uint64_t>(converted.dimensions[1]) * static_cast<std::uint64_t>(converted.dimensions[2]);
                const std::uint32_t component_count = volume_channel_component_count(converted.format);
                if (cell_count > std::numeric_limits<std::uint64_t>::max() / component_count) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value count exceeds uint64 range", name, converted.name));
                const std::uint64_t expected_count = cell_count * component_count;
                const std::span<const float> values = abi_span(channel.values.data, channel.values.count, std::format("Dynamic scene volume \"{}\" channel \"{}\" values", name, converted.name));
                if (converted.source_kind == scene::Scene::VolumeChannelSourceKind::Values) {
                    if (expected_count != values.size()) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value count does not match dimensions", name, converted.name));
                    converted.values.assign(values.begin(), values.end());
                    for (std::size_t index = 0u; index < converted.values.size(); ++index)
                        if (!std::isfinite(converted.values[index])) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" value #{} must be finite", name, converted.name, index));
                    if (converted.name == "color")
                        for (std::size_t index = 0u; index < converted.values.size(); ++index)
                            if (converted.values[index] < 0.0f) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" color channel value #{} must be non-negative", name, index));
                } else {
                    if (!values.empty()) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source must not provide CPU values", name, converted.name));
                    if (expected_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" byte count exceeds uint64 range", name, converted.name));
                    if (converted.source_byte_size < expected_count * sizeof(float)) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source byte size is too small", name, converted.name));
                    if (converted.buffer_id == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source has no buffer id", name, converted.name));
                    if (converted.external_device_pointer == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source has no device pointer for pathtracer snapshot", name, converted.name));
                    if (converted.revision == 0u) throw std::runtime_error(std::format("Dynamic scene volume \"{}\" channel \"{}\" external GPU source revision must not be zero", name, converted.name));
                }
                result.channels.push_back(std::move(converted));
            }
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportCameraVisualImage make_viewport_camera_visual_image(const SpectraDynamicSceneViewportCameraVisualImage& image, const std::string& name) {
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image dimensions must be non-zero", name));
            const std::uint64_t expected_byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
            if (image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image byte count must be width * height * 4", name));
            scene::Scene::ViewportCameraVisualImage result{
                .width = image.width,
                .height = image.height,
                .rgba8 = image.rgba8,
                .rgba8_size = image.rgba8_size,
                .revision = image.revision,
                .tint = make_vector4(image.tint, std::format("Dynamic scene viewport camera visual \"{}\" image tint", name)),
            };
            static_cast<void>(abi_span(image.rgba8, image.rgba8_size, std::format("Dynamic scene viewport camera visual \"{}\" RGBA8 image", name)));
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportCameraVisual make_viewport_camera_visual(const SpectraDynamicSceneViewportCameraVisual& visual, const bool dynamic) {
            const std::string name = abi_string(visual.name, "Dynamic scene viewport camera visual name", false);
            scene::Scene::ViewportCameraVisual result{
                .name = name,
                .owner = make_entity_ref(visual.owner, std::format("Dynamic scene viewport camera visual \"{}\" owner", name)),
                .color = make_vector4(visual.color, std::format("Dynamic scene viewport camera visual \"{}\" color", name)),
                .width = finite_float(visual.width, std::format("Dynamic scene viewport camera visual \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(visual.width_mode, std::format("Dynamic scene viewport camera visual \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(visual.depth_mode, std::format("Dynamic scene viewport camera visual \"{}\"", name)),
                .visual_near = finite_float(visual.visual_near, std::format("Dynamic scene viewport camera visual \"{}\" near", name)),
                .visual_far = finite_float(visual.visual_far, std::format("Dynamic scene viewport camera visual \"{}\" far", name)),
                .dynamic = dynamic,
            };
            if (visual.has_image != 0u) result.image = make_viewport_camera_visual_image(visual.image, name);
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportSegmentSet make_viewport_segment_set(const SpectraDynamicSceneViewportSegmentSet& segment_set, const bool dynamic) {
            const std::string name = abi_string(segment_set.name, "Dynamic scene viewport segment set name", false);
            scene::Scene::ViewportSegmentSet result{
                .name = name,
                .owner = make_entity_ref(segment_set.owner, std::format("Dynamic scene viewport segment set \"{}\" owner", name)),
                .width = finite_float(segment_set.width, std::format("Dynamic scene viewport segment set \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(segment_set.width_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(segment_set.depth_mode, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .transform = make_transform(segment_set.transform, std::format("Dynamic scene viewport segment set \"{}\"", name)),
                .dynamic = dynamic,
            };

            const std::span<const SpectraDynamicSceneViewportSegment> segments = abi_span(segment_set.segments, std::format("Dynamic scene viewport segment set \"{}\" segments", name));
            result.segments.reserve(segments.size());
            for (std::size_t index = 0u; index < segments.size(); ++index) {
                result.segments.push_back(scene::Scene::ViewportSegment{
                    .start = make_vector3(segments[index].start, std::format("Dynamic scene viewport segment set \"{}\" segment #{} start", name, index)),
                    .end = make_vector3(segments[index].end, std::format("Dynamic scene viewport segment set \"{}\" segment #{} end", name, index)),
                });
            }

            const std::span<const SpectraDynamicSceneColor> colors = abi_span(segment_set.colors, std::format("Dynamic scene viewport segment set \"{}\" colors", name));
            if (!colors.empty() && colors.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" color count does not match segment count", name));
            result.colors.reserve(colors.size());
            for (std::size_t index = 0u; index < colors.size(); ++index) result.colors.push_back(make_vector4(colors[index].value, std::format("Dynamic scene viewport segment set \"{}\" color #{}", name, index)));

            const std::span<const float> widths = abi_span(segment_set.widths.data, segment_set.widths.count, std::format("Dynamic scene viewport segment set \"{}\" widths", name));
            if (!widths.empty() && widths.size() != result.segments.size()) throw std::runtime_error(std::format("Dynamic scene viewport segment set \"{}\" width count does not match segment count", name));
            result.widths.reserve(widths.size());
            for (std::size_t index = 0u; index < widths.size(); ++index) result.widths.push_back(finite_float(widths[index], std::format("Dynamic scene viewport segment set \"{}\" width #{}", name, index)));
            return result;
        }

        [[nodiscard]] scene::Scene::ViewportVoxelGrid make_viewport_voxel_grid(const SpectraDynamicSceneViewportVoxelGrid& voxel_grid, const bool dynamic) {
            const std::string name = abi_string(voxel_grid.name, "Dynamic scene viewport voxel grid name", false);
            return scene::Scene::ViewportVoxelGrid{
                .name = name,
                .owner = make_entity_ref(voxel_grid.owner, std::format("Dynamic scene viewport voxel grid \"{}\" owner", name)),
                .dimensions = {voxel_grid.dimensions[0], voxel_grid.dimensions[1], voxel_grid.dimensions[2]},
                .origin = make_vector3(voxel_grid.origin, std::format("Dynamic scene viewport voxel grid \"{}\" origin", name)),
                .voxel_size = make_vector3(voxel_grid.voxel_size, std::format("Dynamic scene viewport voxel grid \"{}\" voxel size", name)),
                .transform = make_transform(voxel_grid.transform, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .color = make_vector4(voxel_grid.color, std::format("Dynamic scene viewport voxel grid \"{}\" color", name)),
                .cell_scale = finite_float(voxel_grid.cell_scale, std::format("Dynamic scene viewport voxel grid \"{}\" cell scale", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(voxel_grid.depth_mode, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .source_kind = viewport_voxel_grid_source_kind_from_u32(voxel_grid.source_kind, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .index_encoding = viewport_voxel_grid_index_encoding_from_u32(voxel_grid.index_encoding, std::format("Dynamic scene viewport voxel grid \"{}\"", name)),
                .buffer_id = voxel_grid.buffer_id,
                .source_byte_size = voxel_grid.source_byte_size,
                .index_count = voxel_grid.index_count,
                .revision = voxel_grid.revision,
                .dynamic = dynamic,
            };
        }

        template <typename Item>
        void require_unique_name(std::set<std::string>& names, const Item& item, const std::string_view kind) {
            if (item.name.empty()) throw std::runtime_error(std::format("Dynamic scene {} name must not be empty", kind));
            if (!names.insert(item.name).second) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" is duplicated", kind, item.name));
        }

        [[nodiscard]] std::set<std::string> collect_material_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewMaterial& material : document.materials) require_unique_name(names, material, "material");
            return names;
        }

        [[nodiscard]] std::set<std::string> collect_light_names(const scene::Scene::Document& document) {
            std::set<std::string> names{};
            for (const scene::Scene::PreviewLight& light : document.lights) require_unique_name(names, light, "light");
            return names;
        }

        template <typename Primitive>
        void require_material_reference(const Primitive& primitive, const std::set<std::string>& material_names, const std::string_view kind) {
            if (primitive.material_name.empty()) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" material name must not be empty", kind, primitive.name));
            if (!material_names.contains(primitive.material_name)) throw std::runtime_error(std::format("Dynamic scene {} \"{}\" references unknown material \"{}\"", kind, primitive.name, primitive.material_name));
        }

        void append_debug_attachment_item(scene::Scene::DebugAttachmentSet& attachments, const SpectraDynamicSceneTypedSpan& item, const bool dynamic, const std::string_view context) {
            switch (item.kind) {
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET:
                for (const SpectraDynamicSceneViewportSegmentSet& segment_set_view : abi_typed_span<SpectraDynamicSceneViewportSegmentSet>(item, SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET, std::format("{} viewport segment sets", context)))
                    attachments.viewport_segment_sets.push_back(make_viewport_segment_set(segment_set_view, dynamic));
                return;
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID:
                for (const SpectraDynamicSceneViewportVoxelGrid& voxel_grid_view : abi_typed_span<SpectraDynamicSceneViewportVoxelGrid>(item, SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID, std::format("{} viewport voxel grids", context)))
                    attachments.viewport_voxel_grids.push_back(make_viewport_voxel_grid(voxel_grid_view, dynamic));
                return;
            case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL:
                for (const SpectraDynamicSceneViewportCameraVisual& visual_view : abi_typed_span<SpectraDynamicSceneViewportCameraVisual>(item, SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL, std::format("{} viewport camera visuals", context)))
                    attachments.viewport_camera_visuals.push_back(make_viewport_camera_visual(visual_view, dynamic));
                return;
            default:
                throw std::runtime_error(std::format("{} item kind {} is not a debug attachment", context, dynamic_scene_item_kind_name(item.kind)));
            }
        }

        void append_document_view(scene::Scene::Document& document, const SpectraDynamicSceneDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneDocumentView)) throw std::runtime_error("Dynamic scene document view ABI size mismatch");
            const std::string coordinate_system_name = abi_string(view.default_coordinate_system, "Dynamic scene document default coordinate system", true);
            if (!coordinate_system_name.empty()) document.default_coordinate_system = scene::coordinate_system(coordinate_system_name);
            const std::string active_camera_name = abi_string(view.active_camera_name, "Dynamic scene document active camera name", true);
            if (!active_camera_name.empty()) document.active_camera_name = active_camera_name;
            const std::span<const SpectraDynamicSceneTypedSpan> items = abi_span(view.items, view.item_count, "Dynamic scene document items");
            require_valid_typed_scene_items(items, dynamic_scene_document_item_kind_allowed, "Dynamic scene document");
            const std::size_t mesh_begin = document.meshes.size();
            const std::size_t sphere_begin = document.spheres.size();
            const std::size_t point_cloud_begin = document.point_clouds.size();
            const std::size_t volume_begin = document.volumes.size();
            for (const SpectraDynamicSceneTypedSpan& item : items) {
                switch (item.kind) {
                case SPECTRA_DYNAMIC_SCENE_ITEM_MATERIAL:
                    for (const SpectraDynamicSceneMaterial& material_view : abi_typed_span<SpectraDynamicSceneMaterial>(item, SPECTRA_DYNAMIC_SCENE_ITEM_MATERIAL, "Dynamic scene document materials")) {
                        scene::Scene::PreviewMaterial material = make_material(material_view);
                        require_unique_name(material_names, material, "material");
                        document.materials.push_back(std::move(material));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_LIGHT:
                    for (const SpectraDynamicSceneLight& light_view : abi_typed_span<SpectraDynamicSceneLight>(item, SPECTRA_DYNAMIC_SCENE_ITEM_LIGHT, "Dynamic scene document lights")) {
                        scene::Scene::PreviewLight light = make_light(light_view);
                        require_unique_name(light_names, light, "light");
                        document.lights.push_back(std::move(light));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA:
                    for (const SpectraDynamicSceneCamera& camera_view : abi_typed_span<SpectraDynamicSceneCamera>(item, SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA, "Dynamic scene document cameras"))
                        document.cameras.push_back(make_camera(camera_view));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_MESH:
                    for (const SpectraDynamicSceneMesh& mesh_view : abi_typed_span<SpectraDynamicSceneMesh>(item, SPECTRA_DYNAMIC_SCENE_ITEM_MESH, "Dynamic scene document meshes"))
                        document.meshes.push_back(make_mesh(mesh_view, false));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE:
                    for (const SpectraDynamicSceneSphere& sphere_view : abi_typed_span<SpectraDynamicSceneSphere>(item, SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE, "Dynamic scene document spheres"))
                        document.spheres.push_back(make_sphere(sphere_view, false));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD:
                    for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_typed_span<SpectraDynamicScenePointCloud>(item, SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD, "Dynamic scene document point clouds"))
                        document.point_clouds.push_back(make_point_cloud(point_cloud_view, false));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME:
                    for (const SpectraDynamicSceneVolume& volume_view : abi_typed_span<SpectraDynamicSceneVolume>(item, SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME, "Dynamic scene document volumes"))
                        document.volumes.push_back(make_volume(volume_view, false));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET:
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID:
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL:
                    append_debug_attachment_item(document.debug_attachments, item, false, "Dynamic scene document debug attachments");
                    break;
                default:
                    throw std::runtime_error(std::format("Dynamic scene document item kind {} is unsupported", dynamic_scene_item_kind_name(item.kind)));
                }
            }
            for (std::size_t index = mesh_begin; index < document.meshes.size(); ++index) require_material_reference(document.meshes[index], material_names, "mesh");
            for (std::size_t index = sphere_begin; index < document.spheres.size(); ++index) require_material_reference(document.spheres[index], material_names, "sphere");
            for (std::size_t index = point_cloud_begin; index < document.point_clouds.size(); ++index) require_material_reference(document.point_clouds[index], material_names, "point cloud");
            for (std::size_t index = volume_begin; index < document.volumes.size(); ++index) require_material_reference(document.volumes[index], material_names, "volume");
        }

        [[nodiscard]] scene::Scene::FrameSnapshot make_frame_snapshot(const SpectraDynamicSceneFrameView& view, const scene::Scene::FrameInfo& frame, const std::set<std::string>& material_names) {
            if (view.struct_size != sizeof(SpectraDynamicSceneFrameView)) throw std::runtime_error("Dynamic scene frame view ABI size mismatch");
            scene::Scene::FrameSnapshot snapshot{.cursor = scene::Scene::make_frame_cursor(frame)};
            const std::span<const SpectraDynamicSceneTypedSpan> items = abi_span(view.items, view.item_count, "Dynamic scene frame items");
            require_valid_typed_scene_items(items, dynamic_scene_frame_item_kind_allowed, "Dynamic scene frame");
            for (const SpectraDynamicSceneTypedSpan& item : items) {
                switch (item.kind) {
                case SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA:
                    for (const SpectraDynamicSceneCamera& camera_view : abi_typed_span<SpectraDynamicSceneCamera>(item, SPECTRA_DYNAMIC_SCENE_ITEM_CAMERA, "Dynamic scene frame cameras"))
                        snapshot.cameras.push_back(make_camera(camera_view));
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_MESH:
                    for (const SpectraDynamicSceneMesh& mesh_view : abi_typed_span<SpectraDynamicSceneMesh>(item, SPECTRA_DYNAMIC_SCENE_ITEM_MESH, "Dynamic scene frame meshes")) {
                        scene::Scene::Mesh mesh = make_mesh(mesh_view, true);
                        require_material_reference(mesh, material_names, "mesh");
                        snapshot.meshes.push_back(std::move(mesh));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE:
                    for (const SpectraDynamicSceneSphere& sphere_view : abi_typed_span<SpectraDynamicSceneSphere>(item, SPECTRA_DYNAMIC_SCENE_ITEM_SPHERE, "Dynamic scene frame spheres")) {
                        scene::Scene::Sphere sphere = make_sphere(sphere_view, true);
                        require_material_reference(sphere, material_names, "sphere");
                        snapshot.spheres.push_back(std::move(sphere));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD:
                    for (const SpectraDynamicScenePointCloud& point_cloud_view : abi_typed_span<SpectraDynamicScenePointCloud>(item, SPECTRA_DYNAMIC_SCENE_ITEM_POINT_CLOUD, "Dynamic scene frame point clouds")) {
                        scene::Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, true);
                        require_material_reference(point_cloud, material_names, "point cloud");
                        snapshot.point_clouds.push_back(std::move(point_cloud));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME:
                    for (const SpectraDynamicSceneVolume& volume_view : abi_typed_span<SpectraDynamicSceneVolume>(item, SPECTRA_DYNAMIC_SCENE_ITEM_VOLUME, "Dynamic scene frame volumes")) {
                        scene::Scene::VolumeGrid volume = make_volume(volume_view, true);
                        require_material_reference(volume, material_names, "volume");
                        snapshot.volumes.push_back(std::move(volume));
                    }
                    break;
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_SEGMENT_SET:
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_VOXEL_GRID:
                case SPECTRA_DYNAMIC_SCENE_ITEM_VIEWPORT_CAMERA_VISUAL:
                    append_debug_attachment_item(snapshot.debug_attachments, item, true, "Dynamic scene frame debug attachments");
                    break;
                default:
                    throw std::runtime_error(std::format("Dynamic scene frame item kind {} is unsupported", dynamic_scene_item_kind_name(item.kind)));
                }
            }
            return snapshot;
        }


        [[nodiscard]] bool parse_bool_default(const std::string_view value) {
            if (value == "true") return true;
            if (value == "false") return false;
            throw std::runtime_error("Dynamic scene open option bool default must be true or false");
        }

        [[nodiscard]] float parse_float_default(const std::string_view value, const std::string_view context) {
            float parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) throw std::runtime_error(std::format("{} must be a finite float", context));
            return parsed;
        }

        [[nodiscard]] std::uint64_t parse_unsigned_integer_default(const std::string_view value, const std::string_view context) {
            std::uint64_t parsed{};
            const char* const begin = value.data();
            const char* const end = value.data() + value.size();
            const std::from_chars_result result = std::from_chars(begin, end, parsed);
            if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error(std::format("{} must be an unsigned integer", context));
            return parsed;
        }


        [[nodiscard]] DynamicSceneOptionKind make_open_option_kind(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
                case SPECTRA_DYNAMIC_SCENE_OPTION_TEXT: return DynamicSceneOptionKind::Text;
                case SPECTRA_DYNAMIC_SCENE_OPTION_DIRECTORY_PATH: return DynamicSceneOptionKind::DirectoryPath;
                case SPECTRA_DYNAMIC_SCENE_OPTION_FILE_PATH: return DynamicSceneOptionKind::FilePath;
                case SPECTRA_DYNAMIC_SCENE_OPTION_CHOICE: return DynamicSceneOptionKind::Choice;
                case SPECTRA_DYNAMIC_SCENE_OPTION_BOOL: return DynamicSceneOptionKind::Bool;
                case SPECTRA_DYNAMIC_SCENE_OPTION_FLOAT: return DynamicSceneOptionKind::Float;
                case SPECTRA_DYNAMIC_SCENE_OPTION_UNSIGNED_INTEGER: return DynamicSceneOptionKind::UnsignedInteger;
                default: throw std::runtime_error(std::format("{} has unknown kind {}", context, kind));
            }
        }

        [[nodiscard]] DynamicSceneOptionSchema make_open_option_schema(const SpectraDynamicSceneOptionSchema& schema, const std::string_view context) {
            DynamicSceneOptionSchema converted{
                .key = abi_string(schema.key, std::format("{} key", context), false),
                .label = abi_string(schema.label, std::format("{} label", context), false),
                .description = abi_string(schema.description, std::format("{} description", context), true),
                .kind = make_open_option_kind(schema.kind, context),
                .required = schema.required != 0u,
                .default_value = abi_string(schema.default_value, std::format("{} default value", context), true),
                .group = abi_string(schema.group, std::format("{} group", context), true),
                .advanced = schema.advanced != 0u,
                .priority = schema.priority,
            };
            if (schema.required != 0u && schema.required != 1u) throw std::runtime_error(std::format("{} required flag must be 0 or 1", context));
            if (schema.advanced != 0u && schema.advanced != 1u) throw std::runtime_error(std::format("{} advanced flag must be 0 or 1", context));
            const std::span<const SpectraDynamicSceneOptionChoice> choices = abi_span(schema.choices.data, schema.choices.count, std::format("{} choices", context));
            if (converted.kind == DynamicSceneOptionKind::Choice && choices.empty()) throw std::runtime_error(std::format("{} choice option must provide at least one choice", context));
            if (converted.kind != DynamicSceneOptionKind::Choice && !choices.empty()) throw std::runtime_error(std::format("{} non-choice option must not provide choices", context));
            std::set<std::string> choice_values{};
            converted.choices.reserve(choices.size());
            for (std::size_t choice_index = 0u; choice_index < choices.size(); ++choice_index) {
                const SpectraDynamicSceneOptionChoice& choice = choices[choice_index];
                DynamicSceneOptionChoice converted_choice{
                    .value = abi_string(choice.value, std::format("{} choice {} value", context, choice_index), false),
                    .label = abi_string(choice.label, std::format("{} choice {} label", context, choice_index), false),
                };
                if (!choice_values.insert(converted_choice.value).second) throw std::runtime_error(std::format("{} choice value '{}' is duplicated", context, converted_choice.value));
                converted.choices.push_back(std::move(converted_choice));
            }
            if (converted.kind == DynamicSceneOptionKind::Choice && !converted.default_value.empty() && !choice_values.contains(converted.default_value)) throw std::runtime_error(std::format("{} default value '{}' is not one of its choices", context, converted.default_value));
            if (converted.kind == DynamicSceneOptionKind::Bool && !converted.default_value.empty()) static_cast<void>(parse_bool_default(converted.default_value));
            if (converted.kind == DynamicSceneOptionKind::Float && !converted.default_value.empty()) static_cast<void>(parse_float_default(converted.default_value, std::format("{} default value", context)));
            if (converted.kind == DynamicSceneOptionKind::UnsignedInteger && !converted.default_value.empty()) static_cast<void>(parse_unsigned_integer_default(converted.default_value, std::format("{} default value", context)));
            return converted;
        }

        [[nodiscard]] std::vector<DynamicSceneOptionSchema> make_open_option_schemas(const SpectraDynamicSceneOptionSchemaSpan schemas, const std::string_view context) {
            const std::span<const SpectraDynamicSceneOptionSchema> schema_span = abi_span(schemas.data, schemas.count, context);
            std::set<std::string> schema_keys{};
            std::vector<DynamicSceneOptionSchema> converted{};
            converted.reserve(schema_span.size());
            for (std::size_t schema_index = 0u; schema_index < schema_span.size(); ++schema_index) {
                DynamicSceneOptionSchema schema = make_open_option_schema(schema_span[schema_index], std::format("{} {}", context, schema_index));
                if (!schema_keys.insert(schema.key).second) throw std::runtime_error(std::format("{} option '{}' is duplicated", context, schema.key));
                converted.push_back(std::move(schema));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlAction make_control_action(const SpectraDynamicSceneControlAction& action, const std::string_view context) {
            if (action.group > DynamicSceneControlActionGroupUtility) throw std::runtime_error(std::format("{} has unknown action group {}", context, action.group));
            if (action.style > DynamicSceneControlActionStyleDanger) throw std::runtime_error(std::format("{} has unknown action style {}", context, action.style));
            return DynamicSceneControlAction{
                .id = abi_string(action.id, std::format("{} id", context), false),
                .label = abi_string(action.label, std::format("{} label", context), false),
                .description = abi_string(action.description, std::format("{} description", context), true),
                .group = action.group,
                .priority = action.priority,
                .style = action.style,
                .options = make_open_option_schemas(action.options, std::format("{} option schema", context)),
            };
        }

        [[nodiscard]] std::vector<DynamicSceneControlAction> make_control_actions(const SpectraDynamicSceneControlActionSpan actions, const std::string_view context) {
            const std::span<const SpectraDynamicSceneControlAction> action_span = abi_span(actions.data, actions.count, context);
            std::set<std::string> action_ids{};
            std::vector<DynamicSceneControlAction> converted{};
            converted.reserve(action_span.size());
            for (std::size_t action_index = 0u; action_index < action_span.size(); ++action_index) {
                DynamicSceneControlAction action = make_control_action(action_span[action_index], std::format("{} {}", context, action_index));
                if (!action_ids.insert(action.id).second) throw std::runtime_error(std::format("{} action '{}' is duplicated", context, action.id));
                converted.push_back(std::move(action));
            }
            return converted;
        }

        [[nodiscard]] bool control_setting_kind_supported(const DynamicSceneOptionKind kind) {
            return kind == DynamicSceneOptionKind::Choice || kind == DynamicSceneOptionKind::Bool || kind == DynamicSceneOptionKind::Float || kind == DynamicSceneOptionKind::UnsignedInteger;
        }

        void validate_control_setting_schema(const DynamicSceneOptionSchema& setting, const std::string_view context) {
            if (!control_setting_kind_supported(setting.kind)) throw std::runtime_error(std::format("{} setting '{}' must use Bool, Choice, Float, or UnsignedInteger", context, setting.key));
            if (setting.required) throw std::runtime_error(std::format("{} setting '{}' must not be required; settings always have a current value", context, setting.key));
        }

        void validate_control_setting_value(const DynamicSceneOptionSchema& schema, const std::string& value, const std::string_view context) {
            switch (schema.kind) {
                case DynamicSceneOptionKind::Choice:
                    if (!std::ranges::any_of(schema.choices, [&value](const DynamicSceneOptionChoice& choice) { return choice.value == value; })) throw std::runtime_error(std::format("{} setting '{}' value '{}' is not one of its choices", context, schema.key, value));
                    return;
                case DynamicSceneOptionKind::Bool:
                    static_cast<void>(parse_bool_default(value));
                    return;
                case DynamicSceneOptionKind::Float:
                    static_cast<void>(parse_float_default(value, std::format("{} setting '{}' value", context, schema.key)));
                    return;
                case DynamicSceneOptionKind::UnsignedInteger:
                    static_cast<void>(parse_unsigned_integer_default(value, std::format("{} setting '{}' value", context, schema.key)));
                    return;
                default:
                    throw std::runtime_error(std::format("{} setting '{}' uses an unsupported kind", context, schema.key));
            }
        }

        [[nodiscard]] std::vector<DynamicSceneOptionSchema> make_control_setting_schemas(const SpectraDynamicSceneOptionSchemaSpan schemas, const std::string_view context) {
            std::vector<DynamicSceneOptionSchema> converted = make_open_option_schemas(schemas, context);
            for (const DynamicSceneOptionSchema& setting : converted) {
                validate_control_setting_schema(setting, context);
                if (!setting.default_value.empty()) validate_control_setting_value(setting, setting.default_value, context);
            }
            return converted;
        }

        [[nodiscard]] std::vector<DynamicSceneControlSettingValue> make_control_setting_values(const std::span<const SpectraDynamicSceneControlSettingValue> setting_span, const std::span<const DynamicSceneOptionSchema> schemas, const std::string_view context) {
            std::set<std::string> setting_keys{};
            std::map<std::string, const DynamicSceneOptionSchema*> schema_by_key{};
            for (const DynamicSceneOptionSchema& schema : schemas) schema_by_key.emplace(schema.key, &schema);
            std::vector<DynamicSceneControlSettingValue> converted{};
            converted.reserve(setting_span.size());
            for (std::size_t setting_index = 0u; setting_index < setting_span.size(); ++setting_index) {
                DynamicSceneControlSettingValue setting{
                    .key = abi_string(setting_span[setting_index].key, std::format("{} setting {} key", context, setting_index), false),
                    .value = abi_string(setting_span[setting_index].value, std::format("{} setting {} value", context, setting_index), false),
                };
                if (!setting_keys.insert(setting.key).second) throw std::runtime_error(std::format("{} setting '{}' is duplicated", context, setting.key));
                const auto schema = schema_by_key.find(setting.key);
                if (schema == schema_by_key.end()) throw std::runtime_error(std::format("{} setting '{}' is not declared in the plugin descriptor", context, setting.key));
                validate_control_setting_value(*schema->second, setting.value, context);
                converted.push_back(std::move(setting));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlStatus make_control_status(const SpectraDynamicSceneControlStatusView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlStatusView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            DynamicSceneControlStatus status{
                .phase = abi_string(view.phase, std::format("{} phase", context), false),
                .headline = abi_string(view.headline, std::format("{} headline", context), false),
                .detail = abi_string(view.detail, std::format("{} detail", context), true),
            };
            const std::span<const SpectraDynamicSceneControlMetric> metrics = abi_span(view.metrics.data, view.metrics.count, std::format("{} metrics", context));
            std::set<std::string> metric_keys{};
            status.metrics.reserve(metrics.size());
            for (std::size_t metric_index = 0u; metric_index < metrics.size(); ++metric_index) {
                const SpectraDynamicSceneControlMetric& metric = metrics[metric_index];
                DynamicSceneControlMetric converted{
                    .key = abi_string(metric.key, std::format("{} metric {} key", context, metric_index), false),
                    .label = abi_string(metric.label, std::format("{} metric {} label", context, metric_index), false),
                    .value = abi_string(metric.value, std::format("{} metric {} value", context, metric_index), false),
                    .placement_flags = metric.placement_flags,
                    .priority = metric.priority,
                    .has_color = metric.has_color != 0u,
                    .color = {
                        finite_float(metric.color[0], std::format("{} metric {} color r", context, metric_index)),
                        finite_float(metric.color[1], std::format("{} metric {} color g", context, metric_index)),
                        finite_float(metric.color[2], std::format("{} metric {} color b", context, metric_index)),
                        finite_float(metric.color[3], std::format("{} metric {} color a", context, metric_index)),
                    },
                };
                if ((converted.placement_flags & ~(DynamicSceneControlPlacementViewportOverlay | DynamicSceneControlPlacementPanelSummary | DynamicSceneControlPlacementPanelDetail)) != 0u) throw std::runtime_error(std::format("{} metric '{}' has unknown placement flags {}", context, converted.key, converted.placement_flags));
                if (metric.has_color != 0u && metric.has_color != 1u) throw std::runtime_error(std::format("{} metric '{}' has_color flag must be 0 or 1", context, converted.key));
                if (!metric_keys.insert(converted.key).second) throw std::runtime_error(std::format("{} metric '{}' is duplicated", context, converted.key));
                status.metrics.push_back(std::move(converted));
            }
            const std::span<const SpectraDynamicSceneControlActionState> action_states = abi_span(view.action_states.data, view.action_states.count, std::format("{} action states", context));
            std::set<std::string> action_ids{};
            status.action_states.reserve(action_states.size());
            for (std::size_t action_index = 0u; action_index < action_states.size(); ++action_index) {
                const SpectraDynamicSceneControlActionState& action = action_states[action_index];
                DynamicSceneControlActionState converted{
                    .action_id = abi_string(action.action_id, std::format("{} action state {} id", context, action_index), false),
                    .enabled = action.enabled != 0u,
                    .disabled_reason = abi_string(action.disabled_reason, std::format("{} action state {} disabled reason", context, action_index), true),
                };
                if (action.enabled != 0u && action.enabled != 1u) throw std::runtime_error(std::format("{} action state '{}' enabled flag must be 0 or 1", context, converted.action_id));
                if (!action_ids.insert(converted.action_id).second) throw std::runtime_error(std::format("{} action state '{}' is duplicated", context, converted.action_id));
                if (!converted.enabled && converted.disabled_reason.empty()) throw std::runtime_error(std::format("{} disabled action '{}' must provide disabled_reason", context, converted.action_id));
                if (converted.enabled && !converted.disabled_reason.empty()) throw std::runtime_error(std::format("{} enabled action '{}' must not provide disabled_reason", context, converted.action_id));
                status.action_states.push_back(std::move(converted));
            }
            return status;
        }

        [[nodiscard]] std::vector<DynamicSceneControlLogEntry> make_control_logs(const std::span<const SpectraDynamicSceneControlLogEntry> entries, const std::string_view context) {
            std::vector<DynamicSceneControlLogEntry> converted{};
            converted.reserve(entries.size());
            for (std::size_t entry_index = 0u; entry_index < entries.size(); ++entry_index) {
                const SpectraDynamicSceneControlLogEntry& entry = entries[entry_index];
                converted.push_back(DynamicSceneControlLogEntry{
                    .sequence = entry.sequence,
                    .level = abi_string(entry.level, std::format("{} entry {} level", context, entry_index), false),
                    .message = abi_string(entry.message, std::format("{} entry {} message", context, entry_index), false),
                });
            }
            return converted;
        }

        [[nodiscard]] std::uint64_t checked_control_image_rgba8_byte_count(const DynamicSceneControlImage& image, const std::string_view context) {
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("{} image '{}' dimensions must be non-zero", context, image.id));
            const std::uint64_t width = image.width;
            const std::uint64_t height = image.height;
            if (width > std::numeric_limits<std::uint64_t>::max() / height / 4u) throw std::runtime_error(std::format("{} image '{}' dimensions overflow RGBA8 byte count", context, image.id));
            return width * height * 4u;
        }

        [[nodiscard]] std::vector<DynamicSceneControlImage> make_control_images(const std::span<const SpectraDynamicSceneControlImage> images, const std::string_view context) {
            std::set<std::string> image_ids{};
            std::vector<DynamicSceneControlImage> converted{};
            converted.reserve(images.size());
            for (std::size_t image_index = 0u; image_index < images.size(); ++image_index) {
                const SpectraDynamicSceneControlImage& image = images[image_index];
                DynamicSceneControlImage converted_image{
                    .id = abi_string(image.id, std::format("{} image {} id", context, image_index), false),
                    .label = abi_string(image.label, std::format("{} image {} label", context, image_index), false),
                    .description = abi_string(image.description, std::format("{} image {} description", context, image_index), true),
                    .rgba8 = image.rgba8,
                    .rgba8_size = image.rgba8_size,
                    .revision = image.revision,
                    .width = image.width,
                    .height = image.height,
                };
                if (!image_ids.insert(converted_image.id).second) throw std::runtime_error(std::format("{} image '{}' is duplicated", context, converted_image.id));
                const std::uint64_t expected_byte_count = checked_control_image_rgba8_byte_count(converted_image, context);
                if (converted_image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("{} image '{}' RGBA8 byte count must be width * height * 4", context, converted_image.id));
                if (converted_image.rgba8 == nullptr) throw std::runtime_error(std::format("{} image '{}' RGBA8 pointer must not be null", context, converted_image.id));
                if (converted_image.revision == 0u) throw std::runtime_error(std::format("{} image '{}' revision must not be zero", context, converted_image.id));
                static_cast<void>(abi_span(converted_image.rgba8, converted_image.rgba8_size, std::format("{} image '{}' RGBA8 data", context, converted_image.id)));
                converted.push_back(std::move(converted_image));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlScalarSample make_control_scalar_sample(const SpectraDynamicSceneControlScalarSample& sample, const std::string_view context) {
            return DynamicSceneControlScalarSample{
                .step = sample.step,
                .time_seconds = finite_double(sample.time_seconds, std::format("{} time", context)),
                .value = finite_double(sample.value, std::format("{} value", context)),
            };
        }

        [[nodiscard]] std::vector<DynamicSceneControlScalarSeries> make_control_scalar_series(const std::span<const SpectraDynamicSceneControlScalarSeries> series_span, const std::string_view context) {
            std::set<std::string> series_ids{};
            std::vector<DynamicSceneControlScalarSeries> converted{};
            converted.reserve(series_span.size());
            for (std::size_t series_index = 0u; series_index < series_span.size(); ++series_index) {
                const SpectraDynamicSceneControlScalarSeries& series = series_span[series_index];
                DynamicSceneControlScalarSeries converted_series{
                    .id = abi_string(series.id, std::format("{} series {} id", context, series_index), false),
                    .label = abi_string(series.label, std::format("{} series {} label", context, series_index), false),
                    .description = abi_string(series.description, std::format("{} series {} description", context, series_index), true),
                    .unit = abi_string(series.unit, std::format("{} series {} unit", context, series_index), true),
                    .color = {
                        finite_float(series.color[0], std::format("{} series {} color r", context, series_index)),
                        finite_float(series.color[1], std::format("{} series {} color g", context, series_index)),
                        finite_float(series.color[2], std::format("{} series {} color b", context, series_index)),
                        finite_float(series.color[3], std::format("{} series {} color a", context, series_index)),
                    },
                    .group = series.group,
                    .priority = series.priority,
                    .revision = series.revision,
                };
                if (converted_series.group > DynamicSceneControlActionGroupUtility) throw std::runtime_error(std::format("{} series '{}' has unknown group {}", context, converted_series.id, converted_series.group));
                if (!series_ids.insert(converted_series.id).second) throw std::runtime_error(std::format("{} series '{}' is duplicated", context, converted_series.id));
                if (converted_series.revision == 0u) throw std::runtime_error(std::format("{} series '{}' revision must not be zero", context, converted_series.id));
                const std::span<const SpectraDynamicSceneControlScalarSample> samples = abi_span(series.samples, std::format("{} series '{}' samples", context, converted_series.id));
                converted_series.samples.reserve(samples.size());
                std::uint64_t previous_step{};
                for (std::size_t sample_index = 0u; sample_index < samples.size(); ++sample_index) {
                    DynamicSceneControlScalarSample sample = make_control_scalar_sample(samples[sample_index], std::format("{} series '{}' sample {}", context, converted_series.id, sample_index));
                    if (sample_index != 0u && sample.step < previous_step) throw std::runtime_error(std::format("{} series '{}' samples must be ordered by nondecreasing step", context, converted_series.id));
                    previous_step = sample.step;
                    converted_series.samples.push_back(sample);
                }
                converted.push_back(std::move(converted_series));
            }
            return converted;
        }

        [[nodiscard]] DynamicSceneControlSnapshot make_control_snapshot(const SpectraDynamicSceneControlSnapshotView& view, const std::span<const DynamicSceneControlAction> actions, const std::span<const DynamicSceneOptionSchema> setting_schemas, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraDynamicSceneControlSnapshotView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            DynamicSceneControlSnapshot snapshot{};
            const std::span<const SpectraDynamicSceneControlTypedSpan> items = abi_span(view.items, view.item_count, std::format("{} items", context));
            std::set<std::uint32_t> item_kinds{};
            bool has_status{};
            bool has_settings{};
            for (std::size_t item_index = 0u; item_index < items.size(); ++item_index) {
                const SpectraDynamicSceneControlTypedSpan& item = items[item_index];
                if (!item_kinds.insert(item.kind).second) throw std::runtime_error(std::format("{} item kind {} is duplicated", context, item.kind));
                switch (item.kind) {
                    case SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_SETTINGS: {
                        if (item.item_size != sizeof(SpectraDynamicSceneControlSettingValue)) throw std::runtime_error(std::format("{} settings item size mismatch", context));
                        const std::span<const SpectraDynamicSceneControlSettingValue> settings = abi_span(static_cast<const SpectraDynamicSceneControlSettingValue*>(item.data), item.count, std::format("{} settings", context));
                        snapshot.settings = make_control_setting_values(settings, setting_schemas, std::format("{} settings", context));
                        if (snapshot.settings.size() != setting_schemas.size()) throw std::runtime_error(std::format("{} settings must provide one value for each declared control setting", context));
                        has_settings = true;
                        break;
                    }
                    case SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_STATUS: {
                        if (item.item_size != sizeof(SpectraDynamicSceneControlStatusView)) throw std::runtime_error(std::format("{} status item size mismatch", context));
                        const std::span<const SpectraDynamicSceneControlStatusView> status = abi_span(static_cast<const SpectraDynamicSceneControlStatusView*>(item.data), item.count, std::format("{} status", context));
                        if (status.size() != 1u) throw std::runtime_error(std::format("{} status item count must be one", context));
                        snapshot.status = make_control_status(status.front(), std::format("{} status", context));
                        has_status = true;
                        break;
                    }
                    case SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_LOG: {
                        if (item.item_size != sizeof(SpectraDynamicSceneControlLogEntry)) throw std::runtime_error(std::format("{} log item size mismatch", context));
                        snapshot.logs = make_control_logs(abi_span(static_cast<const SpectraDynamicSceneControlLogEntry*>(item.data), item.count, std::format("{} logs", context)), std::format("{} logs", context));
                        break;
                    }
                    case SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_IMAGE: {
                        if (item.item_size != sizeof(SpectraDynamicSceneControlImage)) throw std::runtime_error(std::format("{} image item size mismatch", context));
                        snapshot.images = make_control_images(abi_span(static_cast<const SpectraDynamicSceneControlImage*>(item.data), item.count, std::format("{} images", context)), std::format("{} images", context));
                        break;
                    }
                    case SPECTRA_DYNAMIC_SCENE_CONTROL_ITEM_SCALAR_SERIES: {
                        if (item.item_size != sizeof(SpectraDynamicSceneControlScalarSeries)) throw std::runtime_error(std::format("{} scalar series item size mismatch", context));
                        snapshot.scalar_series = make_control_scalar_series(abi_span(static_cast<const SpectraDynamicSceneControlScalarSeries*>(item.data), item.count, std::format("{} scalar series", context)), std::format("{} scalar series", context));
                        break;
                    }
                    default:
                        throw std::runtime_error(std::format("{} has unknown control item kind {}", context, item.kind));
                }
            }
            if (!has_status) throw std::runtime_error(std::format("{} must include a status item", context));
            if (!setting_schemas.empty() && !has_settings) throw std::runtime_error(std::format("{} must include a settings item because the plugin declared control settings", context));
            if (setting_schemas.empty() && has_settings) throw std::runtime_error(std::format("{} must not include a settings item because the plugin declared no control settings", context));

            std::set<std::string> action_ids{};
            for (const DynamicSceneControlAction& action : actions) action_ids.insert(action.id);
            std::set<std::string> action_state_ids{};
            for (const DynamicSceneControlActionState& state : snapshot.status.action_states) {
                if (!action_ids.contains(state.action_id)) throw std::runtime_error(std::format("{} status references unknown action '{}'", context, state.action_id));
                action_state_ids.insert(state.action_id);
            }
            for (const std::string& action_id : action_ids)
                if (!action_state_ids.contains(action_id)) throw std::runtime_error(std::format("{} status did not provide a state for action '{}'", context, action_id));
            return snapshot;
        }

} // namespace spectra::scene_runtime
