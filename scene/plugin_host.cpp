module;

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

module spectra.scene.plugin_host;

import std;
import spectra.scene;
import spectra.scene.plugin_abi;

namespace spectra::scene {
    namespace {
        struct PluginDescriptor {
            std::string id{};
            std::string title{};
            std::string open_action_label{};
            double frames_per_second{};
            std::vector<ControlSection> sections{};
            std::vector<ControlOptionSchema> open_options{};
            std::vector<ControlAction> control_actions{};
            std::vector<ControlOptionSchema> control_settings{};
        };

        struct SceneSymbols {
            std::set<std::string> material_names{};
            std::set<std::string> light_names{};
        };

        [[nodiscard]] std::uint32_t abi_gpu_resource_handle_kind(const GpuResourceHandleKind kind) {
            switch (kind) {
            case GpuResourceHandleKind::OpaqueWin32: return 1u;
            case GpuResourceHandleKind::OpaqueFileDescriptor: return 2u;
            }
            throw std::runtime_error("Scene GPU resource handle kind is invalid");
        }

        [[nodiscard]] SpectraSceneGpuDeviceIdentity abi_gpu_device_identity(const GpuDeviceIdentity& identity) {
            SpectraSceneGpuDeviceIdentity result{
                .vendor_id = identity.vendor_id,
                .device_id = identity.device_id,
                .device_node_mask = identity.device_node_mask,
            };
            for (std::size_t index = 0u; index < identity.device_uuid.size(); ++index) result.device_uuid[index] = identity.device_uuid[index];
            for (std::size_t index = 0u; index < identity.device_luid.size(); ++index) result.device_luid[index] = identity.device_luid[index];
            return result;
        }

        [[nodiscard]] std::uint32_t scene_gpu_buffer_kind_from_abi(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
            case SPECTRA_SCENE_GPU_BUFFER_VOLUME_CHANNEL: return GpuBufferKindVolumeChannel;
            case SPECTRA_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID: return GpuBufferKindViewportVoxelGrid;
            default: throw std::runtime_error(std::format("{} GPU buffer kind {} is unknown", context, kind));
            }
        }

        [[nodiscard]] std::uint32_t abi_gpu_buffer_kind(const std::uint32_t kind) {
            switch (kind) {
            case GpuBufferKindVolumeChannel: return SPECTRA_SCENE_GPU_BUFFER_VOLUME_CHANNEL;
            case GpuBufferKindViewportVoxelGrid: return SPECTRA_SCENE_GPU_BUFFER_VIEWPORT_VOXEL_GRID;
            default: throw std::runtime_error(std::format("Scene GPU buffer kind {} is invalid", kind));
            }
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

        [[nodiscard]] float finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] double finite_double(const double value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
            return value;
        }

        [[nodiscard]] Vector3 make_vector3(const float (&value)[3], const std::string_view context) {
            return Vector3{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
            };
        }

        [[nodiscard]] Vector4 make_vector4(const float (&value)[4], const std::string_view context) {
            return Vector4{
                finite_float(value[0], std::format("{} x", context)),
                finite_float(value[1], std::format("{} y", context)),
                finite_float(value[2], std::format("{} z", context)),
                finite_float(value[3], std::format("{} w", context)),
            };
        }

        [[nodiscard]] Transform make_transform(const SpectraSceneTransform& transform, const std::string_view context) {
            const Transform result{
                .position = make_vector3(transform.position, std::format("{} position", context)),
                .rotation = Quaternion{
                    finite_float(transform.rotation[0], std::format("{} rotation x", context)),
                    finite_float(transform.rotation[1], std::format("{} rotation y", context)),
                    finite_float(transform.rotation[2], std::format("{} rotation z", context)),
                    finite_float(transform.rotation[3], std::format("{} rotation w", context)),
                },
                .scale = make_vector3(transform.scale, std::format("{} scale", context)),
            };
            const float rotation_length_squared = result.rotation.x * result.rotation.x + result.rotation.y * result.rotation.y + result.rotation.z * result.rotation.z + result.rotation.w * result.rotation.w;
            if (std::abs(rotation_length_squared - 1.0f) > 1.0e-3f) throw std::runtime_error(std::format("{} rotation quaternion must be unit length", context));
            if (result.scale.x <= 0.0f || result.scale.y <= 0.0f || result.scale.z <= 0.0f) throw std::runtime_error(std::format("{} scale must be positive", context));
            return result;
        }

        [[nodiscard]] Scene::ViewportSegmentWidthMode viewport_segment_width_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::ViewportSegmentWidthMode::Screen;
            case 1u: return Scene::ViewportSegmentWidthMode::World;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment width mode {}", context, value));
        }

        [[nodiscard]] Scene::ViewportSegmentDepthMode viewport_segment_depth_mode_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::ViewportSegmentDepthMode::DepthTested;
            case 1u: return Scene::ViewportSegmentDepthMode::AlwaysVisible;
            }
            throw std::runtime_error(std::format("{} has invalid viewport segment depth mode {}", context, value));
        }

        [[nodiscard]] Scene::ViewportVoxelGridSourceKind viewport_voxel_grid_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::ViewportVoxelGridSourceKind::IndexList;
            case 1u: return Scene::ViewportVoxelGridSourceKind::Bitfield;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid source kind {}", context, value));
        }

        [[nodiscard]] Scene::ViewportVoxelGridIndexEncoding viewport_voxel_grid_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::ViewportVoxelGridIndexEncoding::Linear;
            case 1u: return Scene::ViewportVoxelGridIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid viewport voxel grid index encoding {}", context, value));
        }

        [[nodiscard]] Scene::VolumeChannelSourceKind volume_channel_source_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::VolumeChannelSourceKind::Values;
            case 1u: return Scene::VolumeChannelSourceKind::ExternalGpuBuffer;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel source kind {}", context, value));
        }

        [[nodiscard]] Scene::VolumeChannelIndexEncoding volume_channel_index_encoding_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::VolumeChannelIndexEncoding::Linear;
            case 1u: return Scene::VolumeChannelIndexEncoding::Morton3D;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel index encoding {}", context, value));
        }

        [[nodiscard]] Scene::VolumeChannelFormat volume_channel_format_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::VolumeChannelFormat::Float32;
            case 1u: return Scene::VolumeChannelFormat::Float32x3;
            }
            throw std::runtime_error(std::format("{} has invalid volume channel format {}", context, value));
        }

        [[nodiscard]] std::uint32_t volume_channel_component_count(const Scene::VolumeChannelFormat format) {
            switch (format) {
            case Scene::VolumeChannelFormat::Float32: return 1u;
            case Scene::VolumeChannelFormat::Float32x3: return 3u;
            }
            throw std::runtime_error("Unknown Scene volume channel format");
        }

        [[nodiscard]] Scene::SceneEntityKind scene_entity_kind_from_u32(const std::uint32_t value, const std::string_view context) {
            switch (value) {
            case 0u: return Scene::SceneEntityKind::Mesh;
            case 1u: return Scene::SceneEntityKind::Sphere;
            case 2u: return Scene::SceneEntityKind::PointCloud;
            case 3u: return Scene::SceneEntityKind::VolumeGrid;
            case 4u: return Scene::SceneEntityKind::Camera;
            case 5u: return Scene::SceneEntityKind::Light;
            }
            throw std::runtime_error(std::format("{} has invalid scene entity kind {}", context, value));
        }

        [[nodiscard]] Scene::SceneEntityRef make_entity_ref(const SpectraSceneEntityRef& entity, const std::string_view context) {
            return Scene::SceneEntityRef{
                .kind = scene_entity_kind_from_u32(entity.kind, context),
                .name = abi_string(entity.name, std::format("{} name", context), false),
            };
        }

        [[nodiscard]] Scene::PreviewSurfaceKind preview_surface_kind_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "lit_surface") return Scene::PreviewSurfaceKind::LitSurface;
            if (value == "unlit_surface") return Scene::PreviewSurfaceKind::UnlitSurface;
            if (value == "emissive_surface") return Scene::PreviewSurfaceKind::EmissiveSurface;
            if (value == "volume") return Scene::PreviewSurfaceKind::Volume;
            if (value == "point_sprite") return Scene::PreviewSurfaceKind::PointGlyph;
            throw std::runtime_error(std::format("Scene material \"{}\" has invalid preview surface kind \"{}\"", material_name, value));
        }

        [[nodiscard]] Scene::PreviewAlphaMode preview_alpha_mode_from_string(const std::string_view value, const std::string_view material_name) {
            if (value == "opaque") return Scene::PreviewAlphaMode::Opaque;
            if (value == "masked") return Scene::PreviewAlphaMode::Masked;
            if (value == "blend") return Scene::PreviewAlphaMode::Blend;
            throw std::runtime_error(std::format("Scene material \"{}\" has invalid alpha mode \"{}\"", material_name, value));
        }

        [[nodiscard]] Scene::PreviewLightKind light_kind_from_string(const std::string_view value, const std::string_view light_name) {
            if (value == "directional") return Scene::PreviewLightKind::Directional;
            if (value == "point") return Scene::PreviewLightKind::Point;
            if (value == "spot") return Scene::PreviewLightKind::Spot;
            if (value == "area") return Scene::PreviewLightKind::Area;
            if (value == "environment") return Scene::PreviewLightKind::Environment;
            throw std::runtime_error(std::format("Scene light \"{}\" has invalid kind \"{}\"", light_name, value));
        }

        [[nodiscard]] Scene::PreviewMaterial make_material(const SpectraSceneMaterial& material) {
            const std::string name = abi_string(material.name, "Scene material name", false);
            return Scene::PreviewMaterial{
                .name = name,
                .surface_kind = preview_surface_kind_from_string(abi_string(material.model, std::format("Scene material \"{}\" model", name), true), name),
                .alpha_mode = preview_alpha_mode_from_string(abi_string(material.alpha_mode, std::format("Scene material \"{}\" alpha mode", name), true), name),
                .base_color = make_vector4(material.base_color, std::format("Scene material \"{}\" base color", name)),
                .emission_color = make_vector3(material.emission_color, std::format("Scene material \"{}\" emission color", name)),
                .emission_strength = finite_float(material.emission_strength, std::format("Scene material \"{}\" emission strength", name)),
                .roughness = finite_float(material.roughness, std::format("Scene material \"{}\" roughness", name)),
                .metallic = finite_float(material.metallic, std::format("Scene material \"{}\" metallic", name)),
                .alpha_cutoff = finite_float(material.alpha_cutoff, std::format("Scene material \"{}\" alpha cutoff", name)),
                .volume_density_scale = finite_float(material.volume_density_scale, std::format("Scene material \"{}\" volume density scale", name)),
                .volume_temperature_scale = finite_float(material.volume_temperature_scale, std::format("Scene material \"{}\" volume temperature scale", name)),
            };
        }

        [[nodiscard]] Scene::PreviewLight make_light(const SpectraSceneLight& light) {
            const std::string name = abi_string(light.name, "Scene light name", false);
            return Scene::PreviewLight{
                .name = name,
                .kind = light_kind_from_string(abi_string(light.kind, std::format("Scene light \"{}\" kind", name), true), name),
                .transform = make_transform(light.transform, std::format("Scene light \"{}\"", name)),
                .color = make_vector3(light.color, std::format("Scene light \"{}\" color", name)),
                .intensity = finite_float(light.intensity, std::format("Scene light \"{}\" intensity", name)),
                .cone_angle_degrees = finite_float(light.cone_angle_degrees, std::format("Scene light \"{}\" cone angle", name)),
            };
        }

        [[nodiscard]] CameraProjection camera_projection(const SpectraSceneCamera& camera, const std::string& name) {
            CameraProjection projection{
                .near_plane = finite_float(camera.near_plane, std::format("Scene camera \"{}\" near plane", name)),
                .far_plane = finite_float(camera.far_plane, std::format("Scene camera \"{}\" far plane", name)),
            };
            switch (camera.projection) {
            case 0u:
                projection.kind = CameraProjectionKind::Perspective;
                projection.vertical_fov_degrees = finite_float(camera.vertical_fov_degrees, std::format("Scene camera \"{}\" vertical fov", name));
                return projection;
            case 1u:
                projection.kind = CameraProjectionKind::Pinhole;
                projection.image_width = camera.image_width;
                projection.image_height = camera.image_height;
                projection.fx = finite_float(camera.fx, std::format("Scene camera \"{}\" fx", name));
                projection.fy = finite_float(camera.fy, std::format("Scene camera \"{}\" fy", name));
                projection.cx = finite_float(camera.cx, std::format("Scene camera \"{}\" cx", name));
                projection.cy = finite_float(camera.cy, std::format("Scene camera \"{}\" cy", name));
                return projection;
            }
            throw std::runtime_error(std::format("Scene camera \"{}\" has invalid projection {}", name, camera.projection));
        }

        [[nodiscard]] Scene::CameraImage make_camera_image(const SpectraSceneCameraImage& image, const std::string& name) {
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("Scene camera \"{}\" RGBA8 image dimensions must be non-zero", name));
            const std::uint64_t expected_byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
            if (image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("Scene camera \"{}\" RGBA8 image byte count must be width * height * 4", name));
            const Scene::CameraImage result{
                .width = image.width,
                .height = image.height,
                .rgba8 = image.rgba8,
                .rgba8_size = image.rgba8_size,
                .revision = image.revision,
            };
            static_cast<void>(abi_span(image.rgba8, image.rgba8_size, std::format("Scene camera \"{}\" RGBA8 image", name)));
            return result;
        }

        [[nodiscard]] Scene::Camera make_camera(const SpectraSceneCamera& camera) {
            const std::string name = abi_string(camera.name, "Scene camera name", false);
            const Vector3 position = make_vector3(camera.position, std::format("Scene camera \"{}\" position", name));
            const Vector3 right = make_vector3(camera.right, std::format("Scene camera \"{}\" right", name));
            const Vector3 down = make_vector3(camera.down, std::format("Scene camera \"{}\" down", name));
            const Vector3 forward = make_vector3(camera.forward, std::format("Scene camera \"{}\" forward", name));
            Scene::Camera result{
                .name = name,
                .pose = camera_pose_from_frame(position, right, down, forward),
                .projection = camera_projection(camera, name),
            };
            if (camera.has_image != 0u) result.image = make_camera_image(camera.image, name);
            return result;
        }

        [[nodiscard]] Scene::Mesh make_mesh(const SpectraSceneMesh& mesh, const bool dynamic) {
            const std::string name = abi_string(mesh.name, "Scene mesh name", false);
            Scene::Mesh result{
                .name = name,
                .material_name = abi_string(mesh.material_name, std::format("Scene mesh \"{}\" material name", name), false),
                .transform = make_transform(mesh.transform, std::format("Scene mesh \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraSceneMeshVertex> vertices = abi_span(mesh.vertices.data, mesh.vertices.count, std::format("Scene mesh \"{}\" vertices", name));
            result.positions.reserve(vertices.size());
            result.normals.reserve(vertices.size());
            for (std::size_t index = 0u; index < vertices.size(); ++index) {
                result.positions.push_back(make_vector3(vertices[index].position, std::format("Scene mesh \"{}\" vertex #{} position", name, index)));
                result.normals.push_back(make_vector3(vertices[index].normal, std::format("Scene mesh \"{}\" vertex #{} normal", name, index)));
            }
            const std::span<const std::uint32_t> indices = abi_span(mesh.indices.data, mesh.indices.count, std::format("Scene mesh \"{}\" indices", name));
            result.indices.assign(indices.begin(), indices.end());
            if (result.positions.empty()) throw std::runtime_error(std::format("Scene mesh \"{}\" must contain vertices", name));
            if (result.indices.empty() || result.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Scene mesh \"{}\" must contain triangle indices", name));
            for (const std::uint32_t index : result.indices)
                if (index >= result.positions.size()) throw std::runtime_error(std::format("Scene mesh \"{}\" contains an out-of-range vertex index", name));
            return result;
        }

        [[nodiscard]] Scene::Sphere make_sphere(const SpectraSceneSphere& sphere, const bool dynamic) {
            const std::string name = abi_string(sphere.name, "Scene sphere name", false);
            const float radius = finite_float(sphere.radius, std::format("Scene sphere \"{}\" radius", name));
            if (radius <= 0.0f) throw std::runtime_error(std::format("Scene sphere \"{}\" radius must be positive", name));
            return Scene::Sphere{
                .name = name,
                .radius = radius,
                .material_name = abi_string(sphere.material_name, std::format("Scene sphere \"{}\" material name", name), false),
                .transform = make_transform(sphere.transform, std::format("Scene sphere \"{}\"", name)),
                .dynamic = dynamic,
            };
        }

        [[nodiscard]] Scene::PointCloud make_point_cloud(const SpectraScenePointCloud& point_cloud, const bool dynamic) {
            const std::string name = abi_string(point_cloud.name, "Scene point cloud name", false);
            Scene::PointCloud result{
                .name = name,
                .material_name = abi_string(point_cloud.material_name, std::format("Scene point cloud \"{}\" material name", name), false),
                .transform = make_transform(point_cloud.transform, std::format("Scene point cloud \"{}\"", name)),
                .dynamic = dynamic,
            };
            const std::span<const SpectraScenePoint> points = abi_span(point_cloud.points.data, point_cloud.points.count, std::format("Scene point cloud \"{}\" points", name));
            result.positions.reserve(points.size());
            result.normals.reserve(points.size());
            result.colors.reserve(points.size());
            result.radii.reserve(points.size());
            for (std::size_t index = 0u; index < points.size(); ++index) {
                result.positions.push_back(make_vector3(points[index].position, std::format("Scene point cloud \"{}\" point #{} position", name, index)));
                result.normals.push_back(make_vector3(points[index].normal, std::format("Scene point cloud \"{}\" point #{} normal", name, index)));
                result.colors.push_back(make_vector4(points[index].color, std::format("Scene point cloud \"{}\" point #{} color", name, index)));
                const float radius = finite_float(points[index].radius, std::format("Scene point cloud \"{}\" point #{} radius", name, index));
                if (radius <= 0.0f) throw std::runtime_error(std::format("Scene point cloud \"{}\" point #{} radius must be positive", name, index));
                result.radii.push_back(radius);
            }
            return result;
        }

        [[nodiscard]] Scene::VolumeGrid make_volume(const SpectraSceneVolume& volume, const bool dynamic) {
            const std::string name = abi_string(volume.name, "Scene volume name", false);
            Scene::VolumeGrid result{
                .name = name,
                .dimensions = {volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]},
                .origin = make_vector3(volume.origin, std::format("Scene volume \"{}\" origin", name)),
                .voxel_size = make_vector3(volume.voxel_size, std::format("Scene volume \"{}\" voxel size", name)),
                .material_name = abi_string(volume.material_name, std::format("Scene volume \"{}\" material name", name), false),
                .dynamic = dynamic,
            };
            if (result.dimensions[0] == 0u || result.dimensions[1] == 0u || result.dimensions[2] == 0u) throw std::runtime_error(std::format("Scene volume \"{}\" dimensions must be positive", name));
            const std::span<const SpectraSceneVolumeChannel> channels = abi_span(volume.channels.data, volume.channels.count, std::format("Scene volume \"{}\" channels", name));
            for (const SpectraSceneVolumeChannel& channel : channels) {
                const std::string channel_name = abi_string(channel.name, std::format("Scene volume \"{}\" channel name", name), false);
                Scene::VolumeChannel converted{
                    .name = channel_name,
                    .dimensions = {channel.dimensions[0], channel.dimensions[1], channel.dimensions[2]},
                    .format = volume_channel_format_from_u32(channel.format, std::format("Scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .source_kind = volume_channel_source_kind_from_u32(channel.source_kind, std::format("Scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .index_encoding = volume_channel_index_encoding_from_u32(channel.index_encoding, std::format("Scene volume \"{}\" channel \"{}\"", name, channel_name)),
                    .buffer_id = channel.buffer_id,
                    .external_device_pointer = channel.external_device_pointer,
                    .source_byte_size = channel.source_byte_size,
                    .revision = channel.revision,
                };
                if (converted.dimensions != result.dimensions) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" dimensions do not match", name, converted.name));
                const std::uint64_t cell_count = static_cast<std::uint64_t>(converted.dimensions[0]) * static_cast<std::uint64_t>(converted.dimensions[1]) * static_cast<std::uint64_t>(converted.dimensions[2]);
                const std::uint32_t component_count = volume_channel_component_count(converted.format);
                if (cell_count > std::numeric_limits<std::uint64_t>::max() / component_count) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" value count exceeds uint64 range", name, converted.name));
                const std::uint64_t expected_count = cell_count * component_count;
                const std::span<const float> values = abi_span(channel.values.data, channel.values.count, std::format("Scene volume \"{}\" channel \"{}\" values", name, converted.name));
                if (converted.source_kind == Scene::VolumeChannelSourceKind::Values) {
                    if (expected_count != values.size()) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" value count does not match dimensions", name, converted.name));
                    converted.values.assign(values.begin(), values.end());
                    for (std::size_t index = 0u; index < converted.values.size(); ++index)
                        if (!std::isfinite(converted.values[index])) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" value #{} must be finite", name, converted.name, index));
                    if (converted.name == "color")
                        for (std::size_t index = 0u; index < converted.values.size(); ++index)
                            if (converted.values[index] < 0.0f) throw std::runtime_error(std::format("Scene volume \"{}\" color channel value #{} must be non-negative", name, index));
                } else {
                    if (!values.empty()) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" external GPU source must not provide CPU values", name, converted.name));
                    if (expected_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" byte count exceeds uint64 range", name, converted.name));
                    if (converted.source_byte_size < expected_count * sizeof(float)) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" external GPU source byte size is too small", name, converted.name));
                    if (converted.buffer_id == 0u) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" external GPU source has no buffer id", name, converted.name));
                    if (converted.external_device_pointer == 0u) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" external GPU source has no device pointer for pathtracer snapshot", name, converted.name));
                    if (converted.revision == 0u) throw std::runtime_error(std::format("Scene volume \"{}\" channel \"{}\" external GPU source revision must not be zero", name, converted.name));
                }
                result.channels.push_back(std::move(converted));
            }
            return result;
        }

        [[nodiscard]] Scene::ViewportSegmentSet make_viewport_segment_set(const SpectraSceneViewportSegmentSet& segment_set, const bool dynamic) {
            const std::string name = abi_string(segment_set.name, "Scene viewport segment set name", false);
            Scene::ViewportSegmentSet result{
                .name = name,
                .owner = make_entity_ref(segment_set.owner, std::format("Scene viewport segment set \"{}\" owner", name)),
                .width = finite_float(segment_set.width, std::format("Scene viewport segment set \"{}\" width", name)),
                .width_mode = viewport_segment_width_mode_from_u32(segment_set.width_mode, std::format("Scene viewport segment set \"{}\"", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(segment_set.depth_mode, std::format("Scene viewport segment set \"{}\"", name)),
                .transform = make_transform(segment_set.transform, std::format("Scene viewport segment set \"{}\"", name)),
                .dynamic = dynamic,
            };

            const std::span<const SpectraSceneViewportSegment> segments = abi_span(segment_set.segments, std::format("Scene viewport segment set \"{}\" segments", name));
            result.segments.reserve(segments.size());
            for (std::size_t index = 0u; index < segments.size(); ++index) {
                result.segments.push_back(Scene::ViewportSegment{
                    .start = make_vector3(segments[index].start, std::format("Scene viewport segment set \"{}\" segment #{} start", name, index)),
                    .end = make_vector3(segments[index].end, std::format("Scene viewport segment set \"{}\" segment #{} end", name, index)),
                });
            }

            const std::span<const SpectraSceneColor> colors = abi_span(segment_set.colors, std::format("Scene viewport segment set \"{}\" colors", name));
            if (!colors.empty() && colors.size() != result.segments.size()) throw std::runtime_error(std::format("Scene viewport segment set \"{}\" color count does not match segment count", name));
            result.colors.reserve(colors.size());
            for (std::size_t index = 0u; index < colors.size(); ++index) result.colors.push_back(make_vector4(colors[index].value, std::format("Scene viewport segment set \"{}\" color #{}", name, index)));

            const std::span<const float> widths = abi_span(segment_set.widths.data, segment_set.widths.count, std::format("Scene viewport segment set \"{}\" widths", name));
            if (!widths.empty() && widths.size() != result.segments.size()) throw std::runtime_error(std::format("Scene viewport segment set \"{}\" width count does not match segment count", name));
            result.widths.reserve(widths.size());
            for (std::size_t index = 0u; index < widths.size(); ++index) result.widths.push_back(finite_float(widths[index], std::format("Scene viewport segment set \"{}\" width #{}", name, index)));
            return result;
        }

        [[nodiscard]] Scene::ViewportVoxelGrid make_viewport_voxel_grid(const SpectraSceneViewportVoxelGrid& voxel_grid, const bool dynamic) {
            const std::string name = abi_string(voxel_grid.name, "Scene viewport voxel grid name", false);
            return Scene::ViewportVoxelGrid{
                .name = name,
                .owner = make_entity_ref(voxel_grid.owner, std::format("Scene viewport voxel grid \"{}\" owner", name)),
                .dimensions = {voxel_grid.dimensions[0], voxel_grid.dimensions[1], voxel_grid.dimensions[2]},
                .origin = make_vector3(voxel_grid.origin, std::format("Scene viewport voxel grid \"{}\" origin", name)),
                .voxel_size = make_vector3(voxel_grid.voxel_size, std::format("Scene viewport voxel grid \"{}\" voxel size", name)),
                .color = make_vector4(voxel_grid.color, std::format("Scene viewport voxel grid \"{}\" color", name)),
                .cell_scale = finite_float(voxel_grid.cell_scale, std::format("Scene viewport voxel grid \"{}\" cell scale", name)),
                .depth_mode = viewport_segment_depth_mode_from_u32(voxel_grid.depth_mode, std::format("Scene viewport voxel grid \"{}\"", name)),
                .source_kind = viewport_voxel_grid_source_kind_from_u32(voxel_grid.source_kind, std::format("Scene viewport voxel grid \"{}\"", name)),
                .index_encoding = viewport_voxel_grid_index_encoding_from_u32(voxel_grid.index_encoding, std::format("Scene viewport voxel grid \"{}\"", name)),
                .buffer_id = voxel_grid.buffer_id,
                .source_byte_size = voxel_grid.source_byte_size,
                .index_count = voxel_grid.index_count,
                .revision = voxel_grid.revision,
                .dynamic = dynamic,
            };
        }

        template <typename Item>
        void require_unique_name(std::set<std::string>& names, const Item& item, const std::string_view kind) {
            if (item.name.empty()) throw std::runtime_error(std::format("Scene {} name must not be empty", kind));
            if (!names.insert(item.name).second) throw std::runtime_error(std::format("Scene {} \"{}\" is duplicated", kind, item.name));
        }

        [[nodiscard]] std::set<std::string> collect_material_names(const Scene::Document& document) {
            std::set<std::string> names{};
            for (const Scene::PreviewMaterial& material : document.materials) require_unique_name(names, material, "material");
            return names;
        }

        [[nodiscard]] std::set<std::string> collect_light_names(const Scene::Document& document) {
            std::set<std::string> names{};
            for (const Scene::PreviewLight& light : document.lights) require_unique_name(names, light, "light");
            return names;
        }

        template <typename Primitive>
        void require_material_reference(const Primitive& primitive, const std::set<std::string>& material_names, const std::string_view kind) {
            if (primitive.material_name.empty()) throw std::runtime_error(std::format("Scene {} \"{}\" material name must not be empty", kind, primitive.name));
            if (!material_names.contains(primitive.material_name)) throw std::runtime_error(std::format("Scene {} \"{}\" references unknown material \"{}\"", kind, primitive.name, primitive.material_name));
        }

        void append_debug_attachments(Scene::DebugAttachmentSet& attachments, const SpectraSceneItems& items, const bool dynamic, const std::string_view context) {
            for (const SpectraSceneViewportSegmentSet& segment_set_view : abi_span(items.viewport_segment_sets, std::format("{} viewport segment sets", context)))
                attachments.viewport_segment_sets.push_back(make_viewport_segment_set(segment_set_view, dynamic));
            for (const SpectraSceneViewportVoxelGrid& voxel_grid_view : abi_span(items.viewport_voxel_grids, std::format("{} viewport voxel grids", context)))
                attachments.viewport_voxel_grids.push_back(make_viewport_voxel_grid(voxel_grid_view, dynamic));
        }

        void append_document_view(Scene::Document& document, const SpectraSceneDocumentView& view, std::set<std::string>& material_names, std::set<std::string>& light_names) {
            if (view.struct_size != sizeof(SpectraSceneDocumentView)) throw std::runtime_error("Scene document view ABI size mismatch");
            const std::string active_camera_name = abi_string(view.active_camera_name, "Scene document active camera name", true);
            if (!active_camera_name.empty()) document.active_camera_name = active_camera_name;

            const std::size_t mesh_begin = document.meshes.size();
            const std::size_t sphere_begin = document.spheres.size();
            const std::size_t point_cloud_begin = document.point_clouds.size();
            const std::size_t volume_begin = document.volumes.size();

            for (const SpectraSceneMaterial& material_view : abi_span(view.items.materials, "Scene document materials")) {
                Scene::PreviewMaterial material = make_material(material_view);
                require_unique_name(material_names, material, "material");
                document.materials.push_back(std::move(material));
            }
            for (const SpectraSceneLight& light_view : abi_span(view.items.lights, "Scene document lights")) {
                Scene::PreviewLight light = make_light(light_view);
                require_unique_name(light_names, light, "light");
                document.lights.push_back(std::move(light));
            }
            for (const SpectraSceneCamera& camera_view : abi_span(view.items.cameras, "Scene document cameras")) document.cameras.push_back(make_camera(camera_view));
            for (const SpectraSceneMesh& mesh_view : abi_span(view.items.meshes, "Scene document meshes")) document.meshes.push_back(make_mesh(mesh_view, false));
            for (const SpectraSceneSphere& sphere_view : abi_span(view.items.spheres, "Scene document spheres")) document.spheres.push_back(make_sphere(sphere_view, false));
            for (const SpectraScenePointCloud& point_cloud_view : abi_span(view.items.point_clouds, "Scene document point clouds")) document.point_clouds.push_back(make_point_cloud(point_cloud_view, false));
            for (const SpectraSceneVolume& volume_view : abi_span(view.items.volumes, "Scene document volumes")) document.volumes.push_back(make_volume(volume_view, false));
            append_debug_attachments(document.debug_attachments, view.items, false, "Scene document debug attachments");

            for (std::size_t index = mesh_begin; index < document.meshes.size(); ++index) require_material_reference(document.meshes[index], material_names, "mesh");
            for (std::size_t index = sphere_begin; index < document.spheres.size(); ++index) require_material_reference(document.spheres[index], material_names, "sphere");
            for (std::size_t index = point_cloud_begin; index < document.point_clouds.size(); ++index) require_material_reference(document.point_clouds[index], material_names, "point cloud");
            for (std::size_t index = volume_begin; index < document.volumes.size(); ++index) require_material_reference(document.volumes[index], material_names, "volume");
        }

        [[nodiscard]] Scene::FrameSnapshot make_frame_snapshot(const SpectraSceneFrameView& view, const Scene::FrameInfo& frame, const std::set<std::string>& material_names) {
            if (view.struct_size != sizeof(SpectraSceneFrameView)) throw std::runtime_error("Scene frame view ABI size mismatch");
            Scene::FrameSnapshot snapshot{.cursor = Scene::make_frame_cursor(frame)};
            for (const SpectraSceneCamera& camera_view : abi_span(view.items.cameras, "Scene frame cameras")) snapshot.cameras.push_back(make_camera(camera_view));
            for (const SpectraSceneMesh& mesh_view : abi_span(view.items.meshes, "Scene frame meshes")) {
                Scene::Mesh mesh = make_mesh(mesh_view, true);
                require_material_reference(mesh, material_names, "mesh");
                snapshot.meshes.push_back(std::move(mesh));
            }
            for (const SpectraSceneSphere& sphere_view : abi_span(view.items.spheres, "Scene frame spheres")) {
                Scene::Sphere sphere = make_sphere(sphere_view, true);
                require_material_reference(sphere, material_names, "sphere");
                snapshot.spheres.push_back(std::move(sphere));
            }
            for (const SpectraScenePointCloud& point_cloud_view : abi_span(view.items.point_clouds, "Scene frame point clouds")) {
                Scene::PointCloud point_cloud = make_point_cloud(point_cloud_view, true);
                require_material_reference(point_cloud, material_names, "point cloud");
                snapshot.point_clouds.push_back(std::move(point_cloud));
            }
            for (const SpectraSceneVolume& volume_view : abi_span(view.items.volumes, "Scene frame volumes")) {
                Scene::VolumeGrid volume = make_volume(volume_view, true);
                require_material_reference(volume, material_names, "volume");
                snapshot.volumes.push_back(std::move(volume));
            }
            append_debug_attachments(snapshot.debug_attachments, view.items, true, "Scene frame debug attachments");
            return snapshot;
        }
        [[nodiscard]] ControlOptionKind make_open_option_kind(const std::uint32_t kind, const std::string_view context) {
            switch (kind) {
                case SPECTRA_SCENE_OPTION_TEXT: return ControlOptionKind::Text;
                case SPECTRA_SCENE_OPTION_DIRECTORY_PATH: return ControlOptionKind::DirectoryPath;
                case SPECTRA_SCENE_OPTION_FILE_PATH: return ControlOptionKind::FilePath;
                case SPECTRA_SCENE_OPTION_CHOICE: return ControlOptionKind::Choice;
                case SPECTRA_SCENE_OPTION_BOOL: return ControlOptionKind::Bool;
                case SPECTRA_SCENE_OPTION_FLOAT: return ControlOptionKind::Float;
                case SPECTRA_SCENE_OPTION_UNSIGNED_INTEGER: return ControlOptionKind::UnsignedInteger;
                default: throw std::runtime_error(std::format("{} has unknown kind {}", context, kind));
            }
        }

        [[nodiscard]] bool parse_bool_default(const std::string_view value) {
            if (value == "true") return true;
            if (value == "false") return false;
            throw std::runtime_error("Scene option bool value must be true or false");
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

        [[nodiscard]] std::vector<ControlSection> make_control_sections(const SpectraSceneControlSectionSpan sections, const std::string_view context) {
            const std::span<const SpectraSceneControlSection> section_span = abi_span(sections.data, sections.count, context);
            if (section_span.empty()) throw std::runtime_error(std::format("{} must declare at least one section", context));
            std::set<std::string> section_ids{};
            std::vector<ControlSection> converted{};
            converted.reserve(section_span.size());
            for (std::size_t section_index = 0u; section_index < section_span.size(); ++section_index) {
                ControlSection section{
                    .id = abi_string(section_span[section_index].id, std::format("{} {} id", context, section_index), false),
                    .label = abi_string(section_span[section_index].label, std::format("{} {} label", context, section_index), false),
                };
                if (!section_ids.insert(section.id).second) throw std::runtime_error(std::format("{} section '{}' is duplicated", context, section.id));
                converted.push_back(std::move(section));
            }
            return converted;
        }

        [[nodiscard]] std::set<std::string> make_section_id_set(const std::span<const ControlSection> sections) {
            std::set<std::string> section_ids{};
            for (const ControlSection& section : sections) section_ids.insert(section.id);
            return section_ids;
        }

        void require_known_section_id(const std::set<std::string>& section_ids, const std::string& section_id, const std::string_view context) {
            if (section_id.empty()) throw std::runtime_error(std::format("{} section_id must not be empty", context));
            if (!section_ids.contains(section_id)) throw std::runtime_error(std::format("{} references unknown section '{}'", context, section_id));
        }

        void validate_option_presentation(const ControlOptionSchema& schema, const std::string_view context) {
            if (schema.presentation != ControlOptionPresentationDefault && schema.presentation != ControlOptionPresentationSlider) throw std::runtime_error(std::format("{} has unknown presentation {}", context, schema.presentation));
            if (!schema.has_numeric_range) {
                if (schema.presentation == ControlOptionPresentationSlider) throw std::runtime_error(std::format("{} slider presentation requires a numeric range", context));
                return;
            }
            if (schema.kind != ControlOptionKind::Float) throw std::runtime_error(std::format("{} numeric range is only supported for float options", context));
            if (schema.presentation != ControlOptionPresentationSlider) throw std::runtime_error(std::format("{} numeric range requires slider presentation", context));
            if (!std::isfinite(schema.numeric_min) || !std::isfinite(schema.numeric_max) || !std::isfinite(schema.numeric_step)) throw std::runtime_error(std::format("{} numeric range must be finite", context));
            if (!(schema.numeric_min < schema.numeric_max)) throw std::runtime_error(std::format("{} numeric range min must be less than max", context));
            if (!(schema.numeric_step > 0.0f)) throw std::runtime_error(std::format("{} numeric range step must be positive", context));
            if (schema.default_value.empty()) return;
            const float default_value = parse_float_default(schema.default_value, std::format("{} default value", context));
            if (default_value < schema.numeric_min || default_value > schema.numeric_max) throw std::runtime_error(std::format("{} default value is outside its numeric range", context));
        }

        [[nodiscard]] ControlOptionSchema make_open_option_schema(const SpectraSceneControlOptionSchema& schema, const std::string_view context) {
            if (schema.has_numeric_range != 0u && schema.has_numeric_range != 1u) throw std::runtime_error(std::format("{} has_numeric_range flag must be 0 or 1", context));
            ControlOptionSchema converted{
                .key = abi_string(schema.key, std::format("{} key", context), false),
                .label = abi_string(schema.label, std::format("{} label", context), false),
                .description = abi_string(schema.description, std::format("{} description", context), true),
                .kind = make_open_option_kind(schema.kind, context),
                .required = schema.required != 0u,
                .default_value = abi_string(schema.default_value, std::format("{} default value", context), true),
                .section_id = abi_string(schema.section_id, std::format("{} section_id", context), false),
                .presentation = schema.presentation,
                .has_numeric_range = schema.has_numeric_range != 0u,
                .numeric_min = schema.numeric_min,
                .numeric_max = schema.numeric_max,
                .numeric_step = schema.numeric_step,
            };
            if (schema.required != 0u && schema.required != 1u) throw std::runtime_error(std::format("{} required flag must be 0 or 1", context));
            const std::span<const SpectraSceneControlOptionChoice> choices = abi_span(schema.choices.data, schema.choices.count, std::format("{} choices", context));
            if (converted.kind == ControlOptionKind::Choice && choices.empty()) throw std::runtime_error(std::format("{} choice option must provide at least one choice", context));
            if (converted.kind != ControlOptionKind::Choice && !choices.empty()) throw std::runtime_error(std::format("{} non-choice option must not provide choices", context));
            std::set<std::string> choice_values{};
            converted.choices.reserve(choices.size());
            for (std::size_t choice_index = 0u; choice_index < choices.size(); ++choice_index) {
                const SpectraSceneControlOptionChoice& choice = choices[choice_index];
                ControlOptionChoice converted_choice{
                    .value = abi_string(choice.value, std::format("{} choice {} value", context, choice_index), false),
                    .label = abi_string(choice.label, std::format("{} choice {} label", context, choice_index), false),
                };
                if (!choice_values.insert(converted_choice.value).second) throw std::runtime_error(std::format("{} choice value '{}' is duplicated", context, converted_choice.value));
                converted.choices.push_back(std::move(converted_choice));
            }
            if (converted.kind == ControlOptionKind::Choice && !converted.default_value.empty() && !choice_values.contains(converted.default_value)) throw std::runtime_error(std::format("{} default value '{}' is not one of its choices", context, converted.default_value));
            if (converted.kind == ControlOptionKind::Bool && !converted.default_value.empty()) static_cast<void>(parse_bool_default(converted.default_value));
            if (converted.kind == ControlOptionKind::Float && !converted.default_value.empty()) static_cast<void>(parse_float_default(converted.default_value, std::format("{} default value", context)));
            if (converted.kind == ControlOptionKind::UnsignedInteger && !converted.default_value.empty()) static_cast<void>(parse_unsigned_integer_default(converted.default_value, std::format("{} default value", context)));
            validate_option_presentation(converted, context);
            return converted;
        }

        [[nodiscard]] std::vector<ControlOptionSchema> make_open_option_schemas(const SpectraSceneControlOptionSchemaSpan schemas, const std::string_view context) {
            const std::span<const SpectraSceneControlOptionSchema> schema_span = abi_span(schemas.data, schemas.count, context);
            std::set<std::string> schema_keys{};
            std::vector<ControlOptionSchema> converted{};
            converted.reserve(schema_span.size());
            for (std::size_t schema_index = 0u; schema_index < schema_span.size(); ++schema_index) {
                ControlOptionSchema schema = make_open_option_schema(schema_span[schema_index], std::format("{} {}", context, schema_index));
                if (!schema_keys.insert(schema.key).second) throw std::runtime_error(std::format("{} option '{}' is duplicated", context, schema.key));
                converted.push_back(std::move(schema));
            }
            return converted;
        }

        [[nodiscard]] ControlAction make_control_action(const SpectraSceneControlAction& action, const std::string_view context) {
            return ControlAction{
                .id = abi_string(action.id, std::format("{} id", context), false),
                .label = abi_string(action.label, std::format("{} label", context), false),
                .description = abi_string(action.description, std::format("{} description", context), true),
                .section_id = abi_string(action.section_id, std::format("{} section_id", context), false),
                .options = make_open_option_schemas(action.options, std::format("{} option schema", context)),
            };
        }

        [[nodiscard]] std::vector<ControlAction> make_control_actions(const SpectraSceneControlActionSpan actions, const std::string_view context) {
            const std::span<const SpectraSceneControlAction> action_span = abi_span(actions.data, actions.count, context);
            std::set<std::string> action_ids{};
            std::vector<ControlAction> converted{};
            converted.reserve(action_span.size());
            for (std::size_t action_index = 0u; action_index < action_span.size(); ++action_index) {
                ControlAction action = make_control_action(action_span[action_index], std::format("{} {}", context, action_index));
                if (!action_ids.insert(action.id).second) throw std::runtime_error(std::format("{} action '{}' is duplicated", context, action.id));
                converted.push_back(std::move(action));
            }
            return converted;
        }

        [[nodiscard]] bool control_setting_kind_supported(const ControlOptionKind kind) {
            return kind == ControlOptionKind::Choice || kind == ControlOptionKind::Bool || kind == ControlOptionKind::Float || kind == ControlOptionKind::UnsignedInteger;
        }

        void validate_control_setting_schema(const ControlOptionSchema& setting, const std::string_view context) {
            if (!control_setting_kind_supported(setting.kind)) throw std::runtime_error(std::format("{} setting '{}' must use Bool, Choice, Float, or UnsignedInteger", context, setting.key));
            if (setting.required) throw std::runtime_error(std::format("{} setting '{}' must not be required; settings always have a current value", context, setting.key));
            if (setting.default_value.empty()) throw std::runtime_error(std::format("{} setting '{}' must provide a default value", context, setting.key));
        }

        void validate_control_setting_value(const ControlOptionSchema& schema, const std::string& value, const std::string_view context) {
            switch (schema.kind) {
                case ControlOptionKind::Choice:
                    if (!std::ranges::any_of(schema.choices, [&value](const ControlOptionChoice& choice) { return choice.value == value; })) throw std::runtime_error(std::format("{} setting '{}' value '{}' is not one of its choices", context, schema.key, value));
                    return;
                case ControlOptionKind::Bool:
                    static_cast<void>(parse_bool_default(value));
                    return;
                case ControlOptionKind::Float:
                    static_cast<void>(parse_float_default(value, std::format("{} setting '{}' value", context, schema.key)));
                    return;
                case ControlOptionKind::UnsignedInteger:
                    static_cast<void>(parse_unsigned_integer_default(value, std::format("{} setting '{}' value", context, schema.key)));
                    return;
                default:
                    throw std::runtime_error(std::format("{} setting '{}' uses an unsupported kind", context, schema.key));
            }
        }

        [[nodiscard]] std::vector<ControlOptionSchema> make_control_setting_schemas(const SpectraSceneControlOptionSchemaSpan schemas, const std::string_view context) {
            std::vector<ControlOptionSchema> converted = make_open_option_schemas(schemas, context);
            for (const ControlOptionSchema& setting : converted) {
                validate_control_setting_schema(setting, context);
                validate_control_setting_value(setting, setting.default_value, context);
            }
            return converted;
        }

        [[nodiscard]] ControlState make_control_state(const SpectraSceneControlStateView& view, const std::string_view context) {
            if (view.struct_size != sizeof(SpectraSceneControlStateView)) throw std::runtime_error(std::format("{} ABI size mismatch", context));
            ControlState state{
                .phase = abi_string(view.phase, std::format("{} phase", context), false),
                .headline = abi_string(view.headline, std::format("{} headline", context), false),
                .detail = abi_string(view.detail, std::format("{} detail", context), true),
            };
            const std::span<const SpectraSceneControlMetric> metrics = abi_span(view.metrics.data, view.metrics.count, std::format("{} metrics", context));
            std::set<std::string> metric_keys{};
            state.metrics.reserve(metrics.size());
            for (std::size_t metric_index = 0u; metric_index < metrics.size(); ++metric_index) {
                const SpectraSceneControlMetric& metric = metrics[metric_index];
                ControlMetric converted{
                    .key = abi_string(metric.key, std::format("{} metric {} key", context, metric_index), false),
                    .label = abi_string(metric.label, std::format("{} metric {} label", context, metric_index), false),
                    .value = abi_string(metric.value, std::format("{} metric {} value", context, metric_index), false),
                    .section_id = abi_string(metric.section_id, std::format("{} metric {} section_id", context, metric_index), false),
                    .display_flags = metric.display_flags,
                    .has_color = metric.has_color != 0u,
                    .color = {
                        finite_float(metric.color[0], std::format("{} metric {} color r", context, metric_index)),
                        finite_float(metric.color[1], std::format("{} metric {} color g", context, metric_index)),
                        finite_float(metric.color[2], std::format("{} metric {} color b", context, metric_index)),
                        finite_float(metric.color[3], std::format("{} metric {} color a", context, metric_index)),
                    },
                };
                if ((converted.display_flags & ~ControlMetricDisplayPrimary) != 0u) throw std::runtime_error(std::format("{} metric '{}' has unknown display flags {}", context, converted.key, converted.display_flags));
                if (metric.has_color != 0u && metric.has_color != 1u) throw std::runtime_error(std::format("{} metric '{}' has_color flag must be 0 or 1", context, converted.key));
                if (!metric_keys.insert(converted.key).second) throw std::runtime_error(std::format("{} metric '{}' is duplicated", context, converted.key));
                state.metrics.push_back(std::move(converted));
            }
            const std::span<const SpectraSceneControlActionState> action_states = abi_span(view.action_states.data, view.action_states.count, std::format("{} action states", context));
            std::set<std::string> action_ids{};
            state.action_states.reserve(action_states.size());
            for (std::size_t action_index = 0u; action_index < action_states.size(); ++action_index) {
                const SpectraSceneControlActionState& action = action_states[action_index];
                ControlActionState converted{
                    .action_id = abi_string(action.action_id, std::format("{} action state {} id", context, action_index), false),
                    .enabled = action.enabled != 0u,
                    .disabled_reason = abi_string(action.disabled_reason, std::format("{} action state {} disabled reason", context, action_index), true),
                };
                if (action.enabled != 0u && action.enabled != 1u) throw std::runtime_error(std::format("{} action state '{}' enabled flag must be 0 or 1", context, converted.action_id));
                if (!action_ids.insert(converted.action_id).second) throw std::runtime_error(std::format("{} action state '{}' is duplicated", context, converted.action_id));
                if (!converted.enabled && converted.disabled_reason.empty()) throw std::runtime_error(std::format("{} disabled action '{}' must provide disabled_reason", context, converted.action_id));
                if (converted.enabled && !converted.disabled_reason.empty()) throw std::runtime_error(std::format("{} enabled action '{}' must not provide disabled_reason", context, converted.action_id));
                state.action_states.push_back(std::move(converted));
            }
            return state;
        }

        void validate_descriptor_section_references(const PluginDescriptor& descriptor, const std::string_view context) {
            const std::set<std::string> section_ids = make_section_id_set(descriptor.sections);
            for (const ControlOptionSchema& option : descriptor.open_options) require_known_section_id(section_ids, option.section_id, std::format("{} open option '{}'", context, option.key));
            for (const ControlAction& action : descriptor.control_actions) {
                require_known_section_id(section_ids, action.section_id, std::format("{} action '{}'", context, action.id));
                for (const ControlOptionSchema& option : action.options) require_known_section_id(section_ids, option.section_id, std::format("{} action '{}' option '{}'", context, action.id, option.key));
            }
            for (const ControlOptionSchema& setting : descriptor.control_settings) require_known_section_id(section_ids, setting.section_id, std::format("{} setting '{}'", context, setting.key));
        }

        void validate_state_section_references(const ControlState& state, const std::span<const ControlSection> sections, const std::string_view context) {
            const std::set<std::string> section_ids = make_section_id_set(sections);
            for (const ControlMetric& metric : state.metrics) require_known_section_id(section_ids, metric.section_id, std::format("{} metric '{}'", context, metric.key));
        }

        [[nodiscard]] ControlState make_checked_control_state(const SpectraSceneControlStateView& view, const std::span<const ControlSection> sections, const std::span<const ControlAction> actions, const std::string_view context) {
            ControlState state = make_control_state(view, context);
            std::set<std::string> action_ids{};
            for (const ControlAction& action : actions) action_ids.insert(action.id);
            std::set<std::string> action_state_ids{};
            for (const ControlActionState& action_state : state.action_states) {
                if (!action_ids.contains(action_state.action_id)) throw std::runtime_error(std::format("{} state references unknown action '{}'", context, action_state.action_id));
                action_state_ids.insert(action_state.action_id);
            }
            for (const std::string& action_id : action_ids)
                if (!action_state_ids.contains(action_id)) throw std::runtime_error(std::format("{} did not provide a state for action '{}'", context, action_id));
            validate_state_section_references(state, sections, context);
            return state;
        }

        [[nodiscard]] PluginDescriptor decode_plugin_descriptor(const SpectraScenePlugin& plugin) {
            PluginDescriptor descriptor{
                .id = abi_string(plugin.id, "Scene plugin id", false),
                .title = abi_string(plugin.title, "Scene plugin title", false),
                .open_action_label = abi_string(plugin.open_action_label, "Scene plugin open action label", false),
                .frames_per_second = finite_double(plugin.frames_per_second, "Scene plugin frame rate"),
                .sections = make_control_sections(plugin.sections, "Scene plugin control sections"),
                .open_options = make_open_option_schemas(plugin.open_options, "Scene plugin open option schema"),
                .control_actions = make_control_actions(plugin.control_actions, "Scene plugin controls action"),
                .control_settings = make_control_setting_schemas(plugin.control_settings, "Scene plugin controls setting schema"),
            };
            validate_descriptor_section_references(descriptor, "Scene plugin descriptor");
            return descriptor;
        }

        [[nodiscard]] std::string decode_plugin_last_error(const SpectraScenePlugin& plugin, SpectraSceneInstance* instance, const std::string_view action) {
            return abi_string(plugin.last_error(instance), std::format("{} error message", action), true);
        }

        [[nodiscard]] GpuBufferRequest decode_gpu_buffer_request(const SpectraSceneGpuBufferRequest& request, const std::string_view context) {
            if (request.struct_size != sizeof(SpectraSceneGpuBufferRequest)) throw std::runtime_error(std::format("{} GPU buffer request ABI size mismatch", context));
            return GpuBufferRequest{
                .kind = scene_gpu_buffer_kind_from_abi(request.kind, context),
                .byte_size = request.byte_size,
                .debug_name = abi_string(request.debug_name, std::format("{} GPU buffer debug name", context), true),
            };
        }

        [[nodiscard]] SpectraSceneGpuBufferAllocation encode_gpu_buffer_allocation(const GpuBufferAllocation& allocation) {
            return SpectraSceneGpuBufferAllocation{
                .struct_size = sizeof(SpectraSceneGpuBufferAllocation),
                .resource_id = allocation.resource_id,
                .byte_size = allocation.byte_size,
                .kind = abi_gpu_buffer_kind(allocation.kind),
                .handle_kind = abi_gpu_resource_handle_kind(allocation.handle_kind),
                .handle = allocation.handle,
                .device_identity = abi_gpu_device_identity(allocation.device_identity),
            };
        }

        class NativeLibrary final {
        public:
            explicit NativeLibrary(std::filesystem::path path) : path(std::move(path)) {
#if defined(_WIN32)
                this->handle = static_cast<void*>(LoadLibraryExW(this->path.wstring().c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS));
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load Scene plugin, Win32 error {}", this->path.string(), GetLastError()));
#else
                this->handle = ::dlopen(this->path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
                if (this->handle == nullptr) throw std::runtime_error(std::format("{}: failed to load Scene plugin: {}", this->path.string(), ::dlerror()));
#endif
            }

            NativeLibrary(const NativeLibrary& other) = delete;
            NativeLibrary(NativeLibrary&& other) = delete;
            NativeLibrary& operator=(const NativeLibrary& other) = delete;
            NativeLibrary& operator=(NativeLibrary&& other) = delete;

            ~NativeLibrary() noexcept {
#if defined(_WIN32)
                if (this->handle != nullptr) static_cast<void>(FreeLibrary(static_cast<HMODULE>(this->handle)));
#else
                if (this->handle != nullptr) static_cast<void>(::dlclose(this->handle));
#endif
            }

            [[nodiscard]] void* symbol(const char* name) const {
#if defined(_WIN32)
                auto symbol_address = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(this->handle), name));
                if (symbol_address == nullptr) throw std::runtime_error(std::format("{}: Scene plugin is missing export \"{}\", Win32 error {}", this->path.string(), name, GetLastError()));
                return symbol_address;
#else
                ::dlerror();
                void* symbol_address = ::dlsym(this->handle, name);
                const char* error = ::dlerror();
                if (error != nullptr) throw std::runtime_error(std::format("{}: Scene plugin is missing export \"{}\": {}", this->path.string(), name, error));
                return symbol_address;
#endif
            }

        private:
            std::filesystem::path path{};
            void* handle{};
        };

        struct PluginOptionStorage {
            std::string key{};
            std::string value{};
        };

        struct PluginOpenRequestStorage {
            std::filesystem::path plugin_path{};
            std::vector<PluginOptionStorage> options{};
            std::vector<SpectraSceneOption> option_views{};
            std::shared_ptr<HostServices> host{};
            SpectraSceneHostServices host_view{};
            std::string scene_id{};
        };

        thread_local std::string scene_host_service_callback_error{};

        [[nodiscard]] SpectraSceneResult request_gpu_buffer(void* user_data, const SpectraSceneGpuBufferRequest* request, SpectraSceneGpuBufferAllocation* allocation) noexcept {
            try {
                scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Scene host services user data pointer is null");
                if (request == nullptr) throw std::runtime_error("Scene GPU buffer request pointer is null");
                if (allocation == nullptr) throw std::runtime_error("Scene GPU buffer allocation pointer is null");
                HostServices& host = *static_cast<HostServices*>(user_data);
                const GpuBufferAllocation allocated = host.request_gpu_buffer(decode_gpu_buffer_request(*request, "Scene host services"));
                *allocation = encode_gpu_buffer_allocation(allocated);
                return SPECTRA_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                scene_host_service_callback_error = error.what();
                return SPECTRA_SCENE_RESULT_ERROR;
            } catch (...) {
                scene_host_service_callback_error = "unknown scene host service error";
                return SPECTRA_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] SpectraSceneResult release_gpu_buffer(void* user_data, const std::uint64_t resource_id) noexcept {
            try {
                scene_host_service_callback_error.clear();
                if (user_data == nullptr) throw std::runtime_error("Scene host services user data pointer is null");
                HostServices& host = *static_cast<HostServices*>(user_data);
                host.release_gpu_buffer(resource_id);
                return SPECTRA_SCENE_RESULT_OK;
            } catch (const std::exception& error) {
                scene_host_service_callback_error = error.what();
                return SPECTRA_SCENE_RESULT_ERROR;
            } catch (...) {
                scene_host_service_callback_error = "unknown scene host service error";
                return SPECTRA_SCENE_RESULT_ERROR;
            }
        }

        [[nodiscard]] const char* scene_host_last_error(void* user_data) noexcept {
            if (user_data == nullptr) return scene_host_service_callback_error.c_str();
            const HostServices& host = *static_cast<HostServices*>(user_data);
            const std::string_view service_error = host.last_error();
            thread_local std::string host_service_error_text{};
            if (!service_error.empty()) {
                host_service_error_text = service_error;
                return host_service_error_text.c_str();
            }
            return scene_host_service_callback_error.c_str();
        }

        [[nodiscard]] SpectraSceneHostServices make_host_view(HostServices& host) {
            return SpectraSceneHostServices{
                .struct_size = sizeof(SpectraSceneHostServices),
                .user_data = &host,
                .request_gpu_buffer = request_gpu_buffer,
                .release_gpu_buffer = release_gpu_buffer,
                .last_error = scene_host_last_error,
            };
        }

        [[nodiscard]] Scene::Camera make_host_inspection_camera() {
            return Scene::Camera{
                .name = "Spectra Inspector Camera",
                .pose = camera_pose_from_look_at(Vector3{0.0f, 1.0f, 5.0f}, Vector3{0.0f, 0.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}),
                .projection = CameraProjection{
                    .kind = CameraProjectionKind::Perspective,
                    .vertical_fov_degrees = 45.0f,
                    .near_plane = 0.01f,
                    .far_plane = 200.0f,
                },
            };
        }

        void ensure_scene_camera(Scene::Document& document, const std::string_view plugin_id) {
            if (document.cameras.empty()) {
                document.cameras.push_back(make_host_inspection_camera());
                document.active_camera_name = document.cameras.back().name;
                return;
            }
            if (!document.active_camera_name.empty()) return;
            if (document.cameras.size() != 1u) throw std::runtime_error(std::format("Scene plugin \"{}\" provided {} cameras but no active camera name", plugin_id, document.cameras.size()));
            document.active_camera_name = document.cameras.front().name;
        }

        [[nodiscard]] std::filesystem::path normalized_scene_plugin_path(const std::filesystem::path& plugin_path) {
            if (plugin_path.empty()) throw std::runtime_error("Scene plugin path must not be empty");
            const std::filesystem::path absolute_path = std::filesystem::absolute(plugin_path).lexically_normal();
            if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Drop a Scene plugin library, not a folder");
            if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: Scene plugin file does not exist", absolute_path.string()));
            if (!is_plugin_file(absolute_path)) throw std::runtime_error(std::format("{}: Scene plugin file extension is not supported on this platform", absolute_path.string()));
            return absolute_path;
        }

        [[nodiscard]] std::uint64_t fnv1a64_append(std::uint64_t hash, const std::string_view value) {
            for (const char character : value) {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        [[nodiscard]] std::string make_plugin_scene_id(const std::filesystem::path& plugin_path, const std::vector<PluginOptionStorage>& options) {
            std::vector<PluginOptionStorage> sorted_options = options;
            std::ranges::sort(sorted_options, {}, &PluginOptionStorage::key);
            std::uint64_t hash = 14695981039346656037ull;
            hash = fnv1a64_append(hash, plugin_path.string());
            for (const PluginOptionStorage& option : sorted_options) {
                hash = fnv1a64_append(hash, "\n");
                hash = fnv1a64_append(hash, option.key);
                hash = fnv1a64_append(hash, "=");
                hash = fnv1a64_append(hash, option.value);
            }
            return std::format("{}#plugin-open-{:016x}", plugin_path.string(), hash);
        }

        [[nodiscard]] PluginOpenRequestStorage make_plugin_open_request_storage(const std::filesystem::path& plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host) {
            PluginOpenRequestStorage storage{
                .plugin_path = normalized_scene_plugin_path(plugin_path),
            };
            if (host == nullptr) throw std::runtime_error("Scene open request requires host services");
            storage.host = std::move(host);
            storage.host_view = make_host_view(*storage.host);
            std::set<std::string> option_keys{};
            storage.options.reserve(options.size());
            for (ControlOption& option : options) {
                if (option.key.empty()) throw std::runtime_error("Scene open option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Scene open option '{}' is duplicated", option.key));
                storage.options.push_back(PluginOptionStorage{
                    .key = std::move(option.key),
                    .value = std::move(option.value),
                });
            }
            storage.scene_id = make_plugin_scene_id(storage.plugin_path, storage.options);
            storage.option_views.reserve(storage.options.size());
            for (const PluginOptionStorage& option : storage.options) {
                storage.option_views.push_back(SpectraSceneOption{
                    .key = option.key.c_str(),
                    .value = option.value.c_str(),
                });
            }
            return storage;
        }


        [[nodiscard]] PluginOpenRequestStorage make_plugin_inspect_request_storage(const std::filesystem::path& plugin_path) {
            PluginOpenRequestStorage storage{
                .plugin_path = normalized_scene_plugin_path(plugin_path),
            };
            storage.scene_id = make_plugin_scene_id(storage.plugin_path, storage.options);
            return storage;
        }
    } // namespace

    struct PluginHost::State final {
        explicit State(PluginOpenRequestStorage open_request) : open_request(std::move(open_request)), native(this->open_request.plugin_path) {
            void* entry_address = this->native.symbol("spectra_scene_plugin_v12");
            const auto entry = reinterpret_cast<SpectraScenePluginEntryFn>(entry_address);
            this->plugin = entry();
            if (this->plugin == nullptr) throw std::runtime_error(std::format("{}: Scene plugin entry returned null", this->open_request.plugin_path.string()));
            this->validate_plugin_descriptor();
            this->descriptor = decode_plugin_descriptor(*this->plugin);
            this->validate_scene_api();
            this->validate_controls_api();
        }

        State(const State& other) = delete;
        State(State&& other) = delete;
        State& operator=(const State& other) = delete;
        State& operator=(State&& other) = delete;
        ~State() noexcept = default;

        [[nodiscard]] PluginInfo info() const {
            return PluginInfo{
                .id = this->descriptor.id,
                .title = this->descriptor.title,
                .open_action_label = this->descriptor.open_action_label,
                .path = this->open_request.plugin_path,
                .sections = this->descriptor.sections,
                .open_options = this->descriptor.open_options,
                .control_actions = this->descriptor.control_actions,
                .control_settings = this->descriptor.control_settings,
            };
        }

        [[nodiscard]] std::string scene_id() const {
            return this->open_request.scene_id;
        }

        [[nodiscard]] Scene::Document make_base_document() const {
            return Scene::Document{
                .revision = Scene::Revision{1},
                .name = this->descriptor.id,
                .title = this->descriptor.title,
                .source = this->open_request.scene_id,
                .frames_per_second = this->descriptor.frames_per_second,
                .timeline_enabled = true,
            };
        }

        void check_result(const SpectraSceneResult result, SpectraSceneInstance* instance, const std::string_view action) const {
            if (result == SPECTRA_SCENE_RESULT_OK) return;
            if (result != SPECTRA_SCENE_RESULT_ERROR) throw std::runtime_error(std::format("{} returned an unknown result code {}", action, static_cast<int>(result)));
            std::string error = decode_plugin_last_error(*this->plugin, instance, action);
            if (error.empty()) error = "unknown plugin error";
            throw std::runtime_error(std::format("{} failed: {}", action, error));
        }

        [[nodiscard]] SpectraSceneInstance* create_instance() const {
            if (this->open_request.host == nullptr) throw std::runtime_error("Scene plugin instance creation requires host services");
            SpectraSceneInstance* instance{};
            const std::string plugin_path_text = this->open_request.plugin_path.string();
            const SpectraSceneOpenInfo open_info{
                .struct_size = sizeof(SpectraSceneOpenInfo),
                .plugin_path = plugin_path_text.c_str(),
                .options = SpectraSceneOptionSpan{
                    .data = this->open_request.option_views.empty() ? nullptr : this->open_request.option_views.data(),
                    .count = static_cast<std::uint64_t>(this->open_request.option_views.size()),
                },
                .host_services = &this->open_request.host_view,
            };
            this->check_result(this->plugin->create(&open_info, &instance), nullptr, "Scene plugin create");
            if (instance == nullptr) throw std::runtime_error("Scene plugin create returned a null instance");
            return instance;
        }

        void destroy_instance(SpectraSceneInstance* instance) const noexcept {
            if (instance != nullptr) this->plugin->destroy(instance);
        }

        void update(SpectraSceneInstance* instance, const UpdateInfo& update) const {
            const SpectraSceneUpdateInfo update_info{
                .struct_size = sizeof(SpectraSceneUpdateInfo),
                .wall_delta_seconds = update.wall_delta_seconds,
                .scene_delta_seconds = update.scene_delta_seconds,
                .time_seconds = update.time_seconds,
                .frame_index = update.frame_index,
                .timeline_playing = update.timeline_playing ? 1u : 0u,
            };
            this->check_result(this->plugin->update(instance, &update_info), instance, "Scene plugin update");
        }

        [[nodiscard]] std::uint64_t scene_revision(SpectraSceneInstance* instance) const {
            std::uint64_t revision{};
            this->check_result(this->plugin->scene_revision(instance, &revision), instance, "Scene plugin controls scene revision");
            if (revision == 0u) throw std::runtime_error("Scene plugin controls scene revision must not be zero");
            return revision;
        }

        void control_action(SpectraSceneInstance* instance, const std::string_view action_id, const std::span<const ControlOption> options) const {
            if (action_id.empty()) throw std::runtime_error("Scene plugin controls action id must not be empty");
            const std::string action_id_text{action_id};
            std::vector<PluginOptionStorage> option_storage{};
            std::vector<SpectraSceneOption> option_views{};
            std::set<std::string> option_keys{};
            option_storage.reserve(options.size());
            option_views.reserve(options.size());
            for (const ControlOption& option : options) {
                if (option.key.empty()) throw std::runtime_error("Scene controls action option key must not be empty");
                if (!option_keys.insert(option.key).second) throw std::runtime_error(std::format("Scene controls action option '{}' is duplicated", option.key));
                option_storage.push_back(PluginOptionStorage{
                    .key = option.key,
                    .value = option.value,
                });
            }
            for (const PluginOptionStorage& option : option_storage) {
                option_views.push_back(SpectraSceneOption{
                    .key = option.key.c_str(),
                    .value = option.value.c_str(),
                });
            }
            this->check_result(
                this->plugin->control_action(
                    instance,
                    action_id_text.c_str(),
                    SpectraSceneOptionSpan{
                        .data = option_views.empty() ? nullptr : option_views.data(),
                        .count = static_cast<std::uint64_t>(option_views.size()),
                    }),
                instance,
                std::format("Scene plugin controls action '{}'", action_id));
        }

        void control_setting_update(SpectraSceneInstance* instance, const std::string_view key, const std::string_view value) const {
            if (key.empty()) throw std::runtime_error("Scene plugin controls setting key must not be empty");
            const std::string key_text{key};
            const std::string value_text{value};
            this->check_result(this->plugin->control_setting_update(instance, key_text.c_str(), value_text.c_str()), instance, std::format("Scene plugin controls setting '{}'", key));
        }

        [[nodiscard]] ControlState control_state(SpectraSceneInstance* instance) const {
            SpectraSceneControlStateView view{};
            this->check_result(this->plugin->control_state(instance, &view), instance, "Scene plugin controls state");
            return make_checked_control_state(view, this->descriptor.sections, this->descriptor.control_actions, "Scene plugin controls state");
        }

        [[nodiscard]] SpectraSceneDocumentView document(SpectraSceneInstance* instance) const {
            SpectraSceneDocumentView view{};
            this->check_result(this->plugin->document(instance, &view), instance, "Scene plugin document");
            return view;
        }

        [[nodiscard]] SpectraSceneFrameView frame(SpectraSceneInstance* instance, const Scene::FrameInfo& frame_info) const {
            SpectraSceneFrameView view{};
            this->check_result(this->plugin->frame(instance, SpectraSceneFrameInfo{.delta_seconds = frame_info.delta_seconds, .time_seconds = frame_info.time_seconds, .frame_index = frame_info.frame_index}, &view), instance, "Scene plugin frame");
            return view;
        }

    private:
        void validate_plugin_descriptor() const {
            if (this->plugin->abi_version != plugin_abi_version) throw std::runtime_error(std::format("{}: scene plugin ABI version {} does not match host ABI version {}", this->open_request.plugin_path.string(), this->plugin->abi_version, plugin_abi_version));
            if (this->plugin->struct_size != sizeof(SpectraScenePlugin)) throw std::runtime_error(std::format("{}: Scene plugin descriptor size mismatch", this->open_request.plugin_path.string()));
        }

        void validate_scene_api() const {
            const double fps = this->descriptor.frames_per_second;
            if (fps <= 0.0) throw std::runtime_error(std::format("{}: Scene plugin frame rate must be positive", this->open_request.plugin_path.string()));
            if (this->plugin->create == nullptr) throw std::runtime_error(std::format("{}: Scene plugin create function is null", this->open_request.plugin_path.string()));
            if (this->plugin->destroy == nullptr) throw std::runtime_error(std::format("{}: Scene plugin destroy function is null", this->open_request.plugin_path.string()));
            if (this->plugin->update == nullptr) throw std::runtime_error(std::format("{}: Scene plugin update function is null", this->open_request.plugin_path.string()));
            if (this->plugin->document == nullptr) throw std::runtime_error(std::format("{}: Scene plugin document function is null", this->open_request.plugin_path.string()));
            if (this->plugin->frame == nullptr) throw std::runtime_error(std::format("{}: Scene plugin frame function is null", this->open_request.plugin_path.string()));
            if (this->plugin->last_error == nullptr) throw std::runtime_error(std::format("{}: Scene plugin last_error function is null", this->open_request.plugin_path.string()));
        }

        void validate_controls_api() const {
            if (this->plugin->scene_revision == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls scene_revision function is null", this->open_request.plugin_path.string()));
            if (this->plugin->control_action == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_action function is null", this->open_request.plugin_path.string()));
            if (this->plugin->control_setting_update == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_setting_update function is null", this->open_request.plugin_path.string()));
            if (this->plugin->control_state == nullptr) throw std::runtime_error(std::format("{}: Scene plugin controls control_state function is null", this->open_request.plugin_path.string()));
        }

        PluginOpenRequestStorage open_request{};
        PluginDescriptor descriptor{};
        NativeLibrary native;
        const SpectraScenePlugin* plugin{};
    };

    class PluginHost::Instance final {
    public:
        explicit Instance(std::shared_ptr<PluginHost> plugin) : plugin(std::move(plugin)) {
            if (this->plugin == nullptr) throw std::runtime_error("Scene plugin driver requires a plugin host");
            this->instance = this->plugin->state->create_instance();
        }

        Instance(const Instance& other) = delete;
        Instance(Instance&& other) = delete;
        Instance& operator=(const Instance& other) = delete;
        Instance& operator=(Instance&& other) = delete;

        ~Instance() noexcept {
            this->plugin->state->destroy_instance(this->instance);
            this->instance = nullptr;
        }

    private:
        std::shared_ptr<PluginHost> plugin{};
        SpectraSceneInstance* instance{};
        SceneSymbols scene_symbols{};
        bool document_validated{};

        friend class PluginHost;
    };

    PluginHost::PluginHost(std::filesystem::path plugin_path) : state(std::make_unique<State>(make_plugin_inspect_request_storage(std::move(plugin_path)))) {}

    PluginHost::PluginHost(std::filesystem::path plugin_path, std::vector<ControlOption> options, std::shared_ptr<HostServices> host) : state(std::make_unique<State>(make_plugin_open_request_storage(std::move(plugin_path), std::move(options), std::move(host)))) {}

    PluginHost::~PluginHost() noexcept = default;

    PluginInfo PluginHost::info() const {
        return this->state->info();
    }

    std::string PluginHost::scene_id() const {
        return this->state->scene_id();
    }

    std::shared_ptr<PluginHost::Instance> PluginHost::create_instance() {
        return std::make_shared<Instance>(this->shared_from_this());
    }

    void PluginHost::update(Instance& instance, const UpdateInfo& update) const {
        this->state->update(instance.instance, update);
    }

    std::uint64_t PluginHost::scene_revision(Instance& instance) const {
        return this->state->scene_revision(instance.instance);
    }

    void PluginHost::execute_control_action(Instance& instance, const std::string_view action_id, const std::span<const ControlOption> options) const {
        this->state->control_action(instance.instance, action_id, options);
    }

    void PluginHost::update_control_setting(Instance& instance, const std::string_view key, const std::string_view value) const {
        this->state->control_setting_update(instance.instance, key, value);
    }

    ControlState PluginHost::control_state(Instance& instance) const {
        return this->state->control_state(instance.instance);
    }

    Scene::Document PluginHost::create_scene_document(Instance& instance) const {
        Scene::Document document = this->state->make_base_document();
        SceneSymbols symbols{
            .material_names = collect_material_names(document),
            .light_names = collect_light_names(document),
        };
        append_document_view(document, this->state->document(instance.instance), symbols.material_names, symbols.light_names);
        ensure_scene_camera(document, document.name);
        document.timeline_enabled = true;
        instance.scene_symbols = std::move(symbols);
        instance.document_validated = true;
        return document;
    }

    Scene::FrameSnapshot PluginHost::create_scene_frame(Instance& instance, const Scene::FrameInfo& frame) const {
        if (!instance.document_validated) throw std::runtime_error("Scene plugin frame was requested before document material validation");
        return make_frame_snapshot(this->state->frame(instance.instance, frame), frame, instance.scene_symbols.material_names);
    }

} // namespace spectra::scene
