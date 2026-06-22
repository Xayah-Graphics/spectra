module;

#ifndef SPECTRA_SCENES_ROOT
#error "SPECTRA_SCENES_ROOT must point to the project-local scene asset directory."
#endif

#include <zlib.h>
#include <lodepng/lodepng.h>

module spectra.scene;

import std;
import spectra.scene.plugin_codec;
import spectra.scene.plugin_library;

namespace spectra::scene {
    [[nodiscard]] Scene::Document make_preview_document_from_pbrt(const Scene::ResolvedScene& scene);
    [[nodiscard]] Scene::Info describe_scene(const Scene::ResolvedScene& scene);

    namespace {
        struct VolumeFloatStats {
            std::uint64_t count{};
            std::uint64_t positive_count{};
            float min{};
            float max{};
            double mean{};
        };

        [[nodiscard]] bool volume_debug_enabled() {
            const char* value = std::getenv("SPECTRA_VOLUME_DEBUG");
            if (value == nullptr) return false;
            return std::string_view{value} == "1" || std::string_view{value} == "true" || std::string_view{value} == "TRUE";
        }

        [[nodiscard]] VolumeFloatStats compute_volume_float_stats(const std::span<const float> values) {
            VolumeFloatStats stats{.count = static_cast<std::uint64_t>(values.size())};
            if (values.empty()) return stats;
            stats.min = values.front();
            stats.max = values.front();
            double sum = 0.0;
            for (const float value : values) {
                stats.min = std::min(stats.min, value);
                stats.max = std::max(stats.max, value);
                sum += static_cast<double>(value);
                if (value > 0.0f) ++stats.positive_count;
            }
            stats.mean = sum / static_cast<double>(values.size());
            return stats;
        }

        [[nodiscard]] VolumeFloatStats compute_volume_majorant_stats(const std::vector<float>& values, const std::array<std::uint32_t, 3>& dimensions) {
            constexpr std::uint32_t majorant_resolution = 16u;
            std::vector<float> majorants{};
            majorants.reserve(static_cast<std::size_t>(majorant_resolution) * majorant_resolution * majorant_resolution);
            const std::uint32_t nx = dimensions[0];
            const std::uint32_t ny = dimensions[1];
            const std::uint32_t nz = dimensions[2];
            if (nx == 0u || ny == 0u || nz == 0u) return {};
            for (std::uint32_t block_z = 0u; block_z < majorant_resolution; ++block_z) {
                const std::uint32_t z0 = block_z * nz / majorant_resolution;
                const std::uint32_t z1 = (block_z + 1u) * nz / majorant_resolution;
                for (std::uint32_t block_y = 0u; block_y < majorant_resolution; ++block_y) {
                    const std::uint32_t y0 = block_y * ny / majorant_resolution;
                    const std::uint32_t y1 = (block_y + 1u) * ny / majorant_resolution;
                    for (std::uint32_t block_x = 0u; block_x < majorant_resolution; ++block_x) {
                        const std::uint32_t x0 = block_x * nx / majorant_resolution;
                        const std::uint32_t x1 = (block_x + 1u) * nx / majorant_resolution;
                        float block_max = 0.0f;
                        for (std::uint32_t z = z0; z < z1; ++z) {
                            const std::uint64_t z_offset = static_cast<std::uint64_t>(nx) * static_cast<std::uint64_t>(ny) * static_cast<std::uint64_t>(z);
                            for (std::uint32_t y = y0; y < y1; ++y) {
                                const std::uint64_t row_offset = z_offset + static_cast<std::uint64_t>(nx) * static_cast<std::uint64_t>(y);
                                for (std::uint32_t x = x0; x < x1; ++x) block_max = std::max(block_max, values.at(static_cast<std::size_t>(row_offset + x)));
                            }
                        }
                        majorants.push_back(block_max);
                    }
                }
            }
            return compute_volume_float_stats(majorants);
        }

        void log_volume_pathtracer_stats(const Scene::VolumeGrid& volume, const Scene::PreviewMaterial& material, const std::vector<float>& density_values) {
            if (!volume_debug_enabled()) return;
            const VolumeFloatStats density_stats = compute_volume_float_stats(density_values);
            const VolumeFloatStats majorant_stats = compute_volume_majorant_stats(density_values, volume.dimensions);
            const double density_positive_percent = density_stats.count == 0u ? 0.0 : static_cast<double>(density_stats.positive_count) * 100.0 / static_cast<double>(density_stats.count);
            const double majorant_positive_percent = majorant_stats.count == 0u ? 0.0 : static_cast<double>(majorant_stats.positive_count) * 100.0 / static_cast<double>(majorant_stats.count);
            std::cerr
                << std::format(
                       "[spectra.volume.debug] pathtracer_volume name=\"{}\" material=\"{}\" dims={}x{}x{} density_min={} density_max={} density_mean={} density_positive={}/{} ({:.3f}%) scale={} approx_majorant_min={} approx_majorant_max={} approx_majorant_mean={} approx_majorant_positive={}/{} ({:.3f}%) approx_scaled_majorant_max={} approx_scaled_majorant_mean={}\n",
                       volume.name,
                       material.name,
                       volume.dimensions[0],
                       volume.dimensions[1],
                       volume.dimensions[2],
                       density_stats.min,
                       density_stats.max,
                       density_stats.mean,
                       density_stats.positive_count,
                       density_stats.count,
                       density_positive_percent,
                       material.volume_density_scale,
                       majorant_stats.min,
                       majorant_stats.max,
                       majorant_stats.mean,
                       majorant_stats.positive_count,
                       majorant_stats.count,
                       majorant_positive_percent,
                       static_cast<double>(majorant_stats.max) * static_cast<double>(material.volume_density_scale),
                       majorant_stats.mean * static_cast<double>(material.volume_density_scale)
                   );
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

        void validate_viewport_segment_width_mode(const Scene::ViewportSegmentWidthMode width_mode, const std::string_view context) {
            switch (width_mode) {
            case Scene::ViewportSegmentWidthMode::Screen: return;
            case Scene::ViewportSegmentWidthMode::World: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported width mode value {}", context, static_cast<std::uint32_t>(width_mode)));
        }

        void validate_viewport_segment_depth_mode(const Scene::ViewportSegmentDepthMode depth_mode, const std::string_view context) {
            switch (depth_mode) {
            case Scene::ViewportSegmentDepthMode::DepthTested: return;
            case Scene::ViewportSegmentDepthMode::AlwaysVisible: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported depth mode value {}", context, static_cast<std::uint32_t>(depth_mode)));
        }

        void validate_viewport_voxel_grid_source_kind(const Scene::ViewportVoxelGridSourceKind source_kind, const std::string_view context) {
            switch (source_kind) {
            case Scene::ViewportVoxelGridSourceKind::IndexList: return;
            case Scene::ViewportVoxelGridSourceKind::Bitfield: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported voxel source kind value {}", context, static_cast<std::uint32_t>(source_kind)));
        }

        void validate_viewport_voxel_grid_index_encoding(const Scene::ViewportVoxelGridIndexEncoding index_encoding, const std::string_view context) {
            switch (index_encoding) {
            case Scene::ViewportVoxelGridIndexEncoding::Linear: return;
            case Scene::ViewportVoxelGridIndexEncoding::Morton3D: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported voxel index encoding value {}", context, static_cast<std::uint32_t>(index_encoding)));
        }

        void validate_volume_channel_source_kind(const Scene::VolumeChannelSourceKind source_kind, const std::string_view context) {
            switch (source_kind) {
            case Scene::VolumeChannelSourceKind::Values: return;
            case Scene::VolumeChannelSourceKind::ExternalGpuBuffer: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported volume channel source kind value {}", context, static_cast<std::uint32_t>(source_kind)));
        }

        void validate_volume_channel_index_encoding(const Scene::VolumeChannelIndexEncoding index_encoding, const std::string_view context) {
            switch (index_encoding) {
            case Scene::VolumeChannelIndexEncoding::Linear: return;
            case Scene::VolumeChannelIndexEncoding::Morton3D: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported volume channel index encoding value {}", context, static_cast<std::uint32_t>(index_encoding)));
        }

        void validate_volume_channel_format(const Scene::VolumeChannelFormat format, const std::string_view context) {
            switch (format) {
            case Scene::VolumeChannelFormat::Float32: return;
            case Scene::VolumeChannelFormat::Float32x3: return;
            }
            throw std::runtime_error(std::format("{} has an unsupported volume channel format value {}", context, static_cast<std::uint32_t>(format)));
        }

        [[nodiscard]] std::uint32_t volume_channel_component_count(const Scene::VolumeChannelFormat format) {
            switch (format) {
            case Scene::VolumeChannelFormat::Float32: return 1u;
            case Scene::VolumeChannelFormat::Float32x3: return 3u;
            }
            throw std::runtime_error("Unknown Spectra volume channel format");
        }

        [[nodiscard]] std::uint64_t checked_volume_cell_count(const Scene::VolumeGrid& volume) {
            const std::uint64_t dim_x = volume.dimensions[0];
            const std::uint64_t dim_y = volume.dimensions[1];
            const std::uint64_t dim_z = volume.dimensions[2];
            if (dim_x == 0u || dim_y == 0u || dim_z == 0u) throw std::runtime_error(std::format("Volume \"{}\" dimensions must be positive", volume.name));
            if (dim_x > std::numeric_limits<std::uint64_t>::max() / dim_y) throw std::runtime_error(std::format("Volume \"{}\" cell count exceeds uint64 range", volume.name));
            const std::uint64_t slice = dim_x * dim_y;
            if (slice > std::numeric_limits<std::uint64_t>::max() / dim_z) throw std::runtime_error(std::format("Volume \"{}\" cell count exceeds uint64 range", volume.name));
            return slice * dim_z;
        }

        [[nodiscard]] std::uint64_t checked_volume_channel_value_count(const Scene::VolumeGrid& volume, const Scene::VolumeChannel& channel) {
            const std::uint64_t cell_count = checked_volume_cell_count(volume);
            const std::uint32_t component_count = volume_channel_component_count(channel.format);
            if (cell_count > std::numeric_limits<std::uint64_t>::max() / component_count) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" value count exceeds uint64 range", volume.name, channel.name));
            return cell_count * component_count;
        }

        [[nodiscard]] std::uint64_t checked_viewport_voxel_grid_cell_count(const Scene::ViewportVoxelGrid& voxel_grid) {
            const std::uint64_t dim_x = voxel_grid.dimensions[0];
            const std::uint64_t dim_y = voxel_grid.dimensions[1];
            const std::uint64_t dim_z = voxel_grid.dimensions[2];
            if (dim_x == 0u || dim_y == 0u || dim_z == 0u) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" dimensions must be positive", voxel_grid.name));
            if (dim_x > std::numeric_limits<std::uint64_t>::max() / dim_y) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" cell count exceeds uint64 range", voxel_grid.name));
            const std::uint64_t slice = dim_x * dim_y;
            if (slice > std::numeric_limits<std::uint64_t>::max() / dim_z) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" cell count exceeds uint64 range", voxel_grid.name));
            return slice * dim_z;
        }

        void validate_viewport_annotation_transform(const Transform& transform, const std::string_view context) {
            if (!is_finite(transform.position)) throw std::runtime_error(std::format("{} has a non-finite transform position", context));
            if (!is_finite(transform.scale)) throw std::runtime_error(std::format("{} has a non-finite transform scale", context));
            if (transform.scale.x == 0.0f || transform.scale.y == 0.0f || transform.scale.z == 0.0f) throw std::runtime_error(std::format("{} has a zero transform scale component", context));
            const float length_squared_value = transform.rotation.x * transform.rotation.x + transform.rotation.y * transform.rotation.y + transform.rotation.z * transform.rotation.z + transform.rotation.w * transform.rotation.w;
            if (!std::isfinite(length_squared_value) || length_squared_value <= 1.0e-12f) throw std::runtime_error(std::format("{} has an invalid rotation quaternion", context));
        }

        void validate_viewport_annotation_color(const Vector4 color, const std::string_view context) {
            if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) || !std::isfinite(color.w)) throw std::runtime_error(std::format("{} has a non-finite color", context));
            if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || color.w < 0.0f || color.w > 1.0f) throw std::runtime_error(std::format("{} has an invalid color", context));
        }

        [[nodiscard]] const char* scene_entity_kind_name(const Scene::SceneEntityKind kind) {
            switch (kind) {
            case Scene::SceneEntityKind::Mesh: return "mesh";
            case Scene::SceneEntityKind::Sphere: return "sphere";
            case Scene::SceneEntityKind::PointCloud: return "point cloud";
            case Scene::SceneEntityKind::VolumeGrid: return "volume";
            case Scene::SceneEntityKind::Camera: return "camera";
            case Scene::SceneEntityKind::Light: return "light";
            }
            throw std::runtime_error("Unknown Spectra scene entity kind");
        }

        template <typename Item>
        [[nodiscard]] bool contains_scene_item_name(const std::vector<Item>& items, const std::string& name) {
            return std::ranges::any_of(items, [&name](const Item& item) { return item.name == name; });
        }

        [[nodiscard]] bool scene_entity_exists(const Scene::SceneEntityRef& entity, const Scene::ResolvedFrame& frame, const Scene::Document& document) {
            switch (entity.kind) {
            case Scene::SceneEntityKind::Mesh: return contains_scene_item_name(frame.meshes, entity.name);
            case Scene::SceneEntityKind::Sphere: return contains_scene_item_name(frame.spheres, entity.name);
            case Scene::SceneEntityKind::PointCloud: return contains_scene_item_name(frame.point_clouds, entity.name);
            case Scene::SceneEntityKind::VolumeGrid: return contains_scene_item_name(frame.volumes, entity.name);
            case Scene::SceneEntityKind::Camera: return contains_scene_item_name(frame.cameras, entity.name);
            case Scene::SceneEntityKind::Light: return contains_scene_item_name(document.lights, entity.name);
            }
            throw std::runtime_error("Unknown Spectra scene entity kind");
        }

        void validate_scene_entity_ref(const Scene::SceneEntityRef& entity, const Scene::ResolvedFrame& frame, const Scene::Document& document, const std::string_view context) {
            static_cast<void>(scene_entity_kind_name(entity.kind));
            if (entity.name.empty()) throw std::runtime_error(std::format("{} owner entity name must not be empty", context));
            if (!scene_entity_exists(entity, frame, document)) throw std::runtime_error(std::format("{} references missing {} owner \"{}\"", context, scene_entity_kind_name(entity.kind), entity.name));
        }

        [[nodiscard]] const Scene::Camera& require_camera_entity(const Scene::ResolvedFrame& frame, const std::string& name, const std::string_view context) {
            for (const Scene::Camera& camera : frame.cameras)
                if (camera.name == name) return camera;
            throw std::runtime_error(std::format("{} references missing camera owner \"{}\"", context, name));
        }

        void validate_viewport_segment_set(const Scene::ViewportSegmentSet& segment_set) {
            if (segment_set.name.empty()) throw std::runtime_error("Viewport segment set name must not be empty");
            if (!std::isfinite(segment_set.width) || segment_set.width <= 0.0f) throw std::runtime_error(std::format("Viewport segment set \"{}\" has an invalid default width", segment_set.name));
            if (!segment_set.widths.empty() && segment_set.widths.size() != segment_set.segments.size()) throw std::runtime_error(std::format("Viewport segment set \"{}\" width count does not match segment count", segment_set.name));
            if (!segment_set.colors.empty() && segment_set.colors.size() != segment_set.segments.size()) throw std::runtime_error(std::format("Viewport segment set \"{}\" color count does not match segment count", segment_set.name));
            validate_viewport_segment_width_mode(segment_set.width_mode, std::format("Viewport segment set \"{}\"", segment_set.name));
            validate_viewport_segment_depth_mode(segment_set.depth_mode, std::format("Viewport segment set \"{}\"", segment_set.name));
            for (std::size_t index = 0u; index < segment_set.segments.size(); ++index) {
                const Scene::ViewportSegment& segment = segment_set.segments.at(index);
                if (!is_finite(segment.start) || !is_finite(segment.end)) throw std::runtime_error(std::format("Viewport segment set \"{}\" contains a non-finite segment endpoint", segment_set.name));
                if (length_squared(segment.end - segment.start) <= 0.0f) throw std::runtime_error(std::format("Viewport segment set \"{}\" contains a zero-length segment", segment_set.name));
                if (!segment_set.widths.empty() && (!std::isfinite(segment_set.widths.at(index)) || segment_set.widths.at(index) <= 0.0f)) throw std::runtime_error(std::format("Viewport segment set \"{}\" contains an invalid segment width", segment_set.name));
                if (!segment_set.colors.empty()) validate_viewport_annotation_color(segment_set.colors.at(index), std::format("Viewport segment set \"{}\" segment #{}", segment_set.name, index));
            }
        }

        void validate_viewport_segment_sets(const std::vector<Scene::ViewportSegmentSet>& segment_sets, const Scene::ResolvedFrame& frame, const Scene::Document& document) {
            validate_unique_scene_item_names(segment_sets, "Scene resolved frame", "viewport segment set");
            for (const Scene::ViewportSegmentSet& segment_set : segment_sets) {
                validate_scene_entity_ref(segment_set.owner, frame, document, std::format("Viewport segment set \"{}\"", segment_set.name));
                validate_viewport_segment_set(segment_set);
            }
        }

        void validate_viewport_voxel_grid(const Scene::ViewportVoxelGrid& voxel_grid) {
            if (voxel_grid.name.empty()) throw std::runtime_error("Viewport voxel grid name must not be empty");
            validate_viewport_annotation_transform(voxel_grid.transform, std::format("Viewport voxel grid \"{}\"", voxel_grid.name));
            validate_viewport_annotation_color(voxel_grid.color, std::format("Viewport voxel grid \"{}\" color", voxel_grid.name));
            validate_viewport_segment_depth_mode(voxel_grid.depth_mode, std::format("Viewport voxel grid \"{}\"", voxel_grid.name));
            validate_viewport_voxel_grid_source_kind(voxel_grid.source_kind, std::format("Viewport voxel grid \"{}\"", voxel_grid.name));
            validate_viewport_voxel_grid_index_encoding(voxel_grid.index_encoding, std::format("Viewport voxel grid \"{}\"", voxel_grid.name));
            if (!is_finite(voxel_grid.origin)) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" origin must be finite", voxel_grid.name));
            if (!is_finite(voxel_grid.voxel_size) || voxel_grid.voxel_size.x <= 0.0f || voxel_grid.voxel_size.y <= 0.0f || voxel_grid.voxel_size.z <= 0.0f) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" voxel size must be finite and positive", voxel_grid.name));
            if (!std::isfinite(voxel_grid.cell_scale) || voxel_grid.cell_scale <= 0.0f || voxel_grid.cell_scale > 1.0f) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" cell scale must be in (0, 1]", voxel_grid.name));
            const std::uint64_t cell_count = checked_viewport_voxel_grid_cell_count(voxel_grid);
            if (voxel_grid.source_kind == Scene::ViewportVoxelGridSourceKind::IndexList) {
                if (voxel_grid.index_count > cell_count) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" index count exceeds grid cell count", voxel_grid.name));
                if (voxel_grid.index_count == 0u) return;
                if (voxel_grid.buffer_id == 0u) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" has indices but no voxel buffer id", voxel_grid.name));
                if (voxel_grid.index_count > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t)) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" index byte count exceeds uint64 range", voxel_grid.name));
                if (voxel_grid.source_byte_size < voxel_grid.index_count * sizeof(std::uint32_t)) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" source byte size is smaller than its index list", voxel_grid.name));
                return;
            }
            if (voxel_grid.buffer_id == 0u) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" bitfield source has no voxel buffer id", voxel_grid.name));
            if (voxel_grid.index_count != 0u) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" bitfield source must not provide an index count", voxel_grid.name));
            const std::uint64_t bitfield_byte_count = (cell_count + 7u) / 8u;
            if (voxel_grid.source_byte_size < bitfield_byte_count) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" source byte size is smaller than its bitfield", voxel_grid.name));
            if (voxel_grid.source_byte_size % sizeof(std::uint32_t) != 0u) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" bitfield source byte size must be uint32 aligned", voxel_grid.name));
        }

        void validate_viewport_voxel_grids(const std::vector<Scene::ViewportVoxelGrid>& voxel_grids, const Scene::ResolvedFrame& frame, const Scene::Document& document) {
            validate_unique_scene_item_names(voxel_grids, "Scene resolved frame", "viewport voxel grid");
            for (const Scene::ViewportVoxelGrid& voxel_grid : voxel_grids) {
                if (voxel_grid.owner.kind != Scene::SceneEntityKind::VolumeGrid) throw std::runtime_error(std::format("Viewport voxel grid \"{}\" owner must be a volume grid", voxel_grid.name));
                validate_scene_entity_ref(voxel_grid.owner, frame, document, std::format("Viewport voxel grid \"{}\"", voxel_grid.name));
                validate_viewport_voxel_grid(voxel_grid);
            }
        }

        void validate_viewport_camera_visual_image(const Scene::ViewportCameraVisualImage& image, const std::string_view context) {
            validate_viewport_annotation_color(image.tint, std::format("{} image tint", context));
            if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("{} RGBA8 image dimensions must be non-zero", context));
            const std::uint64_t byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
            if (image.rgba8_size != byte_count) throw std::runtime_error(std::format("{} RGBA8 image byte count must be width * height * 4", context));
            if (image.rgba8 == nullptr) throw std::runtime_error(std::format("{} RGBA8 image pointer must not be null", context));
        }

        void validate_viewport_camera_visual(const Scene::ViewportCameraVisual& visual, const Scene::ResolvedFrame& frame, const Scene::Document& document) {
            if (visual.name.empty()) throw std::runtime_error("Viewport camera visual name must not be empty");
            if (visual.owner.kind != Scene::SceneEntityKind::Camera) throw std::runtime_error(std::format("Viewport camera visual \"{}\" owner must be a camera", visual.name));
            validate_scene_entity_ref(visual.owner, frame, document, std::format("Viewport camera visual \"{}\"", visual.name));
            const Scene::Camera& camera = require_camera_entity(frame, visual.owner.name, std::format("Viewport camera visual \"{}\"", visual.name));
            if (camera.view.projection.kind == CameraProjectionKind::Orthographic) throw std::runtime_error(std::format("Viewport camera visual \"{}\" requires perspective or pinhole projection", visual.name));
            validate_viewport_annotation_color(visual.color, std::format("Viewport camera visual \"{}\" color", visual.name));
            if (!std::isfinite(visual.width) || !(visual.width > 0.0f)) throw std::runtime_error(std::format("Viewport camera visual \"{}\" width must be positive", visual.name));
            validate_viewport_segment_width_mode(visual.width_mode, std::format("Viewport camera visual \"{}\"", visual.name));
            validate_viewport_segment_depth_mode(visual.depth_mode, std::format("Viewport camera visual \"{}\"", visual.name));
            if (!std::isfinite(visual.visual_near) || !std::isfinite(visual.visual_far) || !(visual.visual_near > 0.0f) || !(visual.visual_far > visual.visual_near)) throw std::runtime_error(std::format("Viewport camera visual \"{}\" range must satisfy visual_far > visual_near > 0", visual.name));
            if (visual.image.has_value()) validate_viewport_camera_visual_image(*visual.image, std::format("Viewport camera visual \"{}\"", visual.name));
        }

        void validate_debug_attachment_set(const Scene::DebugAttachmentSet& attachments, const Scene::ResolvedFrame& frame, const Scene::Document& document) {
            validate_viewport_segment_sets(attachments.viewport_segment_sets, frame, document);
            validate_viewport_voxel_grids(attachments.viewport_voxel_grids, frame, document);
            validate_unique_scene_item_names(attachments.viewport_camera_visuals, "Scene resolved frame", "viewport camera visual");
            for (const Scene::ViewportCameraVisual& visual : attachments.viewport_camera_visuals) validate_viewport_camera_visual(visual, frame, document);
        }

        void validate_volume_channel(const Scene::VolumeChannel& channel, const Scene::VolumeGrid& volume) {
            if (channel.name.empty()) throw std::runtime_error(std::format("Volume \"{}\" contains an unnamed channel", volume.name));
            validate_volume_channel_format(channel.format, std::format("Volume \"{}\" channel \"{}\"", volume.name, channel.name));
            validate_volume_channel_source_kind(channel.source_kind, std::format("Volume \"{}\" channel \"{}\"", volume.name, channel.name));
            validate_volume_channel_index_encoding(channel.index_encoding, std::format("Volume \"{}\" channel \"{}\"", volume.name, channel.name));
            if (channel.dimensions != volume.dimensions) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" dimensions do not match", volume.name, channel.name));
            if (channel.name == "density" && channel.format != Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Volume \"{}\" density channel must use Float32 format", volume.name));
            if (channel.name == "temperature" && channel.format != Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Volume \"{}\" temperature channel must use Float32 format", volume.name));
            if (channel.name == "color" && channel.format != Scene::VolumeChannelFormat::Float32x3) throw std::runtime_error(std::format("Volume \"{}\" color channel must use Float32x3 format", volume.name));
            const std::uint64_t value_count = checked_volume_channel_value_count(volume, channel);
            if (channel.source_kind == Scene::VolumeChannelSourceKind::Values) {
                if (channel.values.size() != value_count) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" value count does not match dimensions", volume.name, channel.name));
                for (const float value : channel.values)
                    if (!std::isfinite(value)) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" contains a non-finite value", volume.name, channel.name));
                if (channel.name == "color")
                    for (const float value : channel.values)
                        if (value < 0.0f) throw std::runtime_error(std::format("Volume \"{}\" color channel contains a negative value", volume.name));
                if (channel.buffer_id != 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" CPU source must not provide a GPU buffer id", volume.name, channel.name));
                if (channel.external_device_pointer != 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" CPU source must not provide an external device pointer", volume.name, channel.name));
                if (channel.source_byte_size != 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" CPU source must not provide a GPU byte size", volume.name, channel.name));
                return;
            }
            if (!channel.values.empty()) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" external GPU source must not provide CPU values", volume.name, channel.name));
            if (channel.buffer_id == 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" external GPU source has no buffer id", volume.name, channel.name));
            if (channel.external_device_pointer == 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" external GPU source has no external device pointer", volume.name, channel.name));
            if (value_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" byte count exceeds uint64 range", volume.name, channel.name));
            if (channel.source_byte_size < value_count * sizeof(float)) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" external GPU source byte size is too small", volume.name, channel.name));
            if (channel.revision == 0u) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" external GPU source revision must not be zero", volume.name, channel.name));
        }

        void validate_volume_grid(const Scene::VolumeGrid& volume, const Scene::Document& document) {
            if (volume.name.empty()) throw std::runtime_error("Volume name must not be empty");
            if (volume.dimensions[0] == 0u || volume.dimensions[1] == 0u || volume.dimensions[2] == 0u) throw std::runtime_error(std::format("Volume \"{}\" dimensions must be positive", volume.name));
            if (!is_finite(volume.origin)) throw std::runtime_error(std::format("Volume \"{}\" origin must be finite", volume.name));
            if (!is_finite(volume.voxel_size) || volume.voxel_size.x <= 0.0f || volume.voxel_size.y <= 0.0f || volume.voxel_size.z <= 0.0f) throw std::runtime_error(std::format("Volume \"{}\" voxel size must be finite and positive", volume.name));
            if (!contains_scene_item_name(document.materials, volume.material_name)) throw std::runtime_error(std::format("Volume \"{}\" references unknown material \"{}\"", volume.name, volume.material_name));
            validate_unique_scene_item_names(volume.channels, std::format("Volume \"{}\"", volume.name), "channel");
            for (const Scene::VolumeChannel& channel : volume.channels) validate_volume_channel(channel, volume);
        }

        void validate_volumes(const std::vector<Scene::VolumeGrid>& volumes, const Scene::Document& document) {
            validate_unique_scene_item_names(volumes, "Scene resolved frame", "volume");
            for (const Scene::VolumeGrid& volume : volumes) validate_volume_grid(volume, document);
        }

        void validate_camera(const Scene::Camera& camera, const std::string_view context) {
            if (camera.name.empty()) throw std::runtime_error(std::format("{} camera name must not be empty", context));
            static_cast<void>(make_vulkan_camera_matrices(camera.view, 1.0f, camera.view.projection.far_plane));
        }

        void validate_cameras(const std::vector<Scene::Camera>& cameras, const std::string& active_camera_name, const std::string_view context) {
            validate_unique_scene_item_names(cameras, context, "camera");
            if (active_camera_name.empty()) throw std::runtime_error(std::format("{} active camera name must not be empty", context));
            bool found_active_camera = false;
            for (const Scene::Camera& camera : cameras) {
                validate_camera(camera, context);
                found_active_camera = found_active_camera || camera.name == active_camera_name;
            }
            if (!found_active_camera) throw std::runtime_error(std::format("{} active camera \"{}\" does not exist", context, active_camera_name));
        }

        [[nodiscard]] const Scene::Camera& require_active_camera(const std::vector<Scene::Camera>& cameras, const std::string& active_camera_name, const std::string_view context) {
            for (const Scene::Camera& camera : cameras)
                if (camera.name == active_camera_name) return camera;
            throw std::runtime_error(std::format("{} active camera \"{}\" does not exist", context, active_camera_name));
        }

        [[nodiscard]] float pathtracer_camera_fov_degrees(const Scene::Camera& camera) {
            switch (camera.view.projection.kind) {
            case CameraProjectionKind::Perspective:
                return camera.view.projection.vertical_fov_degrees;
            case CameraProjectionKind::Pinhole: {
                constexpr float principal_point_tolerance = 1.0e-3f;
                const float centered_x = static_cast<float>(camera.view.projection.image_width) * 0.5f;
                const float centered_y = static_cast<float>(camera.view.projection.image_height) * 0.5f;
                if (std::abs(camera.view.projection.cx - centered_x) > principal_point_tolerance || std::abs(camera.view.projection.cy - centered_y) > principal_point_tolerance)
                    throw std::runtime_error(std::format("Active camera \"{}\" uses off-center pinhole intrinsics; current pathtracer camera adapter requires a centered principal point", camera.name));
                return camera_projection_vertical_fov_degrees(camera.view.projection);
            }
            case CameraProjectionKind::Orthographic:
                throw std::runtime_error(std::format("Active camera \"{}\" uses orthographic projection; preview pathtracer conversion currently requires a perspective camera", camera.name));
            }
            throw std::runtime_error("Unknown scene camera projection kind");
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

        [[nodiscard]] Scene::Parameter string_parameter(std::string name, std::vector<std::string> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "string", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter rgb_parameter(std::string name, const Vector3 value, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "rgb", .name = std::move(name), .values = std::vector<float>{value.x, value.y, value.z}, .source = source};
        }

        [[nodiscard]] Scene::Parameter rgb_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "rgb", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter point3_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "point3", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter point2_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "point2", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter normal_parameter(std::string name, std::vector<float> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "normal", .name = std::move(name), .values = std::move(values), .source = source};
        }

        [[nodiscard]] Scene::Parameter texture_parameter(std::string name, std::vector<std::string> values, const Scene::SourceLocation& source) {
            return Scene::Parameter{.type = "texture", .name = std::move(name), .values = std::move(values), .source = source};
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

        [[nodiscard]] SceneTransform make_preview_scene_transform(const Transform& transform, const std::string_view context) {
            if (!is_finite(transform.position)) throw std::runtime_error(std::format("{} has a non-finite transform position", context));
            if (!is_finite(transform.scale)) throw std::runtime_error(std::format("{} has a non-finite transform scale", context));
            if (transform.scale.x == 0.0f || transform.scale.y == 0.0f || transform.scale.z == 0.0f) throw std::runtime_error(std::format("{} has a zero transform scale component", context));
            const Quaternion rotation = normalized_quaternion(transform.rotation, context);
            const float x = rotation.x;
            const float y = rotation.y;
            const float z = rotation.z;
            const float w = rotation.w;
            const float r00 = 1.0f - 2.0f * y * y - 2.0f * z * z;
            const float r01 = 2.0f * x * y - 2.0f * w * z;
            const float r02 = 2.0f * x * z + 2.0f * w * y;
            const float r10 = 2.0f * x * y + 2.0f * w * z;
            const float r11 = 1.0f - 2.0f * x * x - 2.0f * z * z;
            const float r12 = 2.0f * y * z - 2.0f * w * x;
            const float r20 = 2.0f * x * z - 2.0f * w * y;
            const float r21 = 2.0f * y * z + 2.0f * w * x;
            const float r22 = 1.0f - 2.0f * x * x - 2.0f * y * y;
            const float inv00 = r00 / transform.scale.x;
            const float inv01 = r10 / transform.scale.x;
            const float inv02 = r20 / transform.scale.x;
            const float inv10 = r01 / transform.scale.y;
            const float inv11 = r11 / transform.scale.y;
            const float inv12 = r21 / transform.scale.y;
            const float inv20 = r02 / transform.scale.z;
            const float inv21 = r12 / transform.scale.z;
            const float inv22 = r22 / transform.scale.z;
            return SceneTransform{
                .matrix =
                    {
                        r00 * transform.scale.x,
                        r01 * transform.scale.y,
                        r02 * transform.scale.z,
                        transform.position.x,
                        r10 * transform.scale.x,
                        r11 * transform.scale.y,
                        r12 * transform.scale.z,
                        transform.position.y,
                        r20 * transform.scale.x,
                        r21 * transform.scale.y,
                        r22 * transform.scale.z,
                        transform.position.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
                .inverse =
                    {
                        inv00,
                        inv01,
                        inv02,
                        -(inv00 * transform.position.x + inv01 * transform.position.y + inv02 * transform.position.z),
                        inv10,
                        inv11,
                        inv12,
                        -(inv10 * transform.position.x + inv11 * transform.position.y + inv12 * transform.position.z),
                        inv20,
                        inv21,
                        inv22,
                        -(inv20 * transform.position.x + inv21 * transform.position.y + inv22 * transform.position.z),
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
            };
        }

        [[nodiscard]] float preview_scalar(const Vector4 color) {
            return std::max(0.0f, (color.x + color.y + color.z) / 3.0f);
        }

        [[nodiscard]] const Scene::PreviewMaterial* find_preview_material(const Scene::Document& document, const std::string& name) {
            for (const Scene::PreviewMaterial& material : document.materials)
                if (material.name == name) return &material;
            return nullptr;
        }

        [[nodiscard]] std::set<std::string> append_canonical_textures(Scene::ResolvedScene& scene, const Scene::Document& document) {
            std::set<std::string> names{};
            for (const Scene::Texture& texture : document.textures) {
                if (texture.name.empty()) throw std::runtime_error("Preview texture name must not be empty when building canonical scene");
                if (!names.insert(texture.name).second) throw std::runtime_error(std::format("Preview texture \"{}\" is duplicated when building canonical scene", texture.name));
                require_static_scene_transform(texture.transform, texture.entity.source, std::format("Preview texture \"{}\"", texture.name));
                scene.textures.push_back(texture);
            }
            return names;
        }

        void require_preview_texture_reference(const std::set<std::string>& texture_names, const std::string& texture_name, const std::string_view material_name, const std::string_view parameter_name) {
            if (texture_name.empty()) return;
            if (!texture_names.contains(texture_name)) throw std::runtime_error(std::format("Preview material \"{}\" references unknown {} texture \"{}\"", material_name, parameter_name, texture_name));
        }

        void append_canonical_materials(Scene::ResolvedScene& scene, const Scene::Document& document, const std::set<std::string>& texture_names, const bool needs_volume_interface_material) {
            std::set<std::string> names{};
            for (const Scene::PreviewMaterial& material : document.materials) {
                if (material.name.empty()) throw std::runtime_error("Preview material name must not be empty when building canonical scene");
                if (!names.insert(material.name).second) throw std::runtime_error(std::format("Preview material \"{}\" is duplicated when building canonical scene", material.name));
                if (material.surface_kind == Scene::PreviewSurfaceKind::Volume) continue;
                require_preview_texture_reference(texture_names, material.base_color_texture, material.name, "base color");
                require_preview_texture_reference(texture_names, material.emission_texture, material.name, "emission");
                require_preview_texture_reference(texture_names, material.roughness_texture, material.name, "roughness");
                require_preview_texture_reference(texture_names, material.normal_texture, material.name, "normal");
                if (!material.pathtracer_material.type.empty()) {
                    scene.materials.push_back(Scene::Material{
                        .name   = material.name,
                        .entity = material.pathtracer_material,
                    });
                    continue;
                }
                const Vector3 reflectance{std::max(0.0f, material.base_color.x), std::max(0.0f, material.base_color.y), std::max(0.0f, material.base_color.z)};
                std::vector<Scene::Parameter> parameters{};
                if (material.base_color_texture.empty())
                    parameters.push_back(rgb_parameter("reflectance", reflectance, {}));
                else
                    parameters.push_back(texture_parameter("reflectance", {material.base_color_texture}, {}));
                scene.materials.push_back(Scene::Material{
                    .name = material.name,
                    .entity = Scene::Entity{
                        .type = "diffuse",
                        .parameters = std::move(parameters),
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

        [[nodiscard]] std::optional<Scene::AreaLight> make_preview_area_light(const Scene::Document& document, const std::string& material_name, const Scene::SourceLocation& source) {
            const Scene::PreviewMaterial* material = find_preview_material(document, material_name);
            if (material == nullptr) throw std::runtime_error(std::format("Preview shape references unknown material \"{}\"", material_name));
            if (material->surface_kind != Scene::PreviewSurfaceKind::EmissiveSurface) return {};
            if (!is_finite(material->emission_color)) throw std::runtime_error(std::format("Preview emissive material \"{}\" emission color must be finite when building canonical scene", material_name));
            if (material->emission_color.x < 0.0f || material->emission_color.y < 0.0f || material->emission_color.z < 0.0f) throw std::runtime_error(std::format("Preview emissive material \"{}\" emission color must be non-negative when building canonical scene", material_name));
            if (!std::isfinite(material->emission_strength) || material->emission_strength < 0.0f) throw std::runtime_error(std::format("Preview emissive material \"{}\" emission strength must be finite and non-negative when building canonical scene", material_name));
            if (!material->emission_texture.empty()) throw std::runtime_error(std::format("Preview emissive material \"{}\" uses an emission texture, which cannot be converted to a canonical diffuse area light", material_name));
            return Scene::AreaLight{
                .entity = Scene::Entity{
                    .type = "diffuse",
                    .parameters = {
                        rgb_parameter("L", material->emission_color, source),
                        float_parameter("scale", {material->emission_strength}, source),
                    },
                    .source = source,
                },
            };
        }

        void append_mesh_shape(Scene::ResolvedScene& scene, const Scene::Document& document, const Scene::Mesh& mesh) {
            if (mesh.name.empty()) throw std::runtime_error("Preview mesh name must not be empty when building canonical scene");
            if (mesh.material_name.empty()) throw std::runtime_error(std::format("Preview mesh \"{}\" material name must not be empty when building canonical scene", mesh.name));
            if (mesh.positions.empty()) throw std::runtime_error(std::format("Preview mesh \"{}\" has no positions when building canonical scene", mesh.name));
            if (mesh.normals.size() != mesh.positions.size()) throw std::runtime_error(std::format("Preview mesh \"{}\" normal count does not match position count when building canonical scene", mesh.name));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Preview mesh \"{}\" has invalid triangle indices when building canonical scene", mesh.name));
            std::vector<float> positions{};
            std::vector<float> normals{};
            std::vector<int> indices{};
            std::vector<float> uvs{};
            positions.reserve(mesh.positions.size() * 3u);
            normals.reserve(mesh.normals.size() * 3u);
            indices.reserve(mesh.indices.size());
            if (!mesh.uvs.empty() && mesh.uvs.size() != mesh.positions.size()) throw std::runtime_error(std::format("Preview mesh \"{}\" uv count does not match position count when building canonical scene", mesh.name));
            uvs.reserve(mesh.uvs.size() * 2u);
            const std::string context = std::format("Preview mesh \"{}\"", mesh.name);
            for (std::size_t index = 0u; index < mesh.positions.size(); ++index) {
                const Vector3 point = transform_point(mesh.transform, mesh.positions.at(index), context);
                const Vector3 normal = transform_normal(mesh.transform, mesh.normals.at(index), context);
                positions.insert(positions.end(), {point.x, point.y, point.z});
                normals.insert(normals.end(), {normal.x, normal.y, normal.z});
                if (!mesh.uvs.empty()) uvs.insert(uvs.end(), {mesh.uvs.at(index).at(0), mesh.uvs.at(index).at(1)});
            }
            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.positions.size()) throw std::runtime_error(std::format("Preview mesh \"{}\" has an out-of-range triangle index when building canonical scene", mesh.name));
                if (index > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) throw std::runtime_error(std::format("Preview mesh \"{}\" triangle index exceeds PBRT integer range", mesh.name));
                indices.push_back(static_cast<int>(index));
            }
            std::vector<Scene::Parameter> parameters{
                point3_parameter("P", std::move(positions), mesh.source),
                normal_parameter("N", std::move(normals), mesh.source),
                integer_parameter("indices", std::move(indices), mesh.source),
            };
            if (!uvs.empty()) parameters.push_back(point2_parameter("uv", std::move(uvs), mesh.source));
            scene.shapes.push_back(Scene::Shape{
                .name = mesh.name,
                .entity = Scene::Entity{
                    .type = "trianglemesh",
                    .parameters = std::move(parameters),
                    .source = mesh.source,
                },
                .material_name = mesh.material_name,
                .area_light    = make_preview_area_light(document, mesh.material_name, mesh.source),
            });
        }

        void append_sphere_shape(Scene::ResolvedScene& scene, const Scene::Document& document, const Scene::Sphere& sphere) {
            if (sphere.name.empty()) throw std::runtime_error("Preview sphere name must not be empty when building canonical scene");
            if (sphere.material_name.empty()) throw std::runtime_error(std::format("Preview sphere \"{}\" material name must not be empty when building canonical scene", sphere.name));
            if (!std::isfinite(sphere.radius) || sphere.radius <= 0.0f) throw std::runtime_error(std::format("Preview sphere \"{}\" radius must be finite and positive when building canonical scene", sphere.name));
            const SceneTransform transform = make_preview_scene_transform(sphere.transform, std::format("Preview sphere \"{}\"", sphere.name));
            scene.shapes.push_back(Scene::Shape{
                .name = sphere.name,
                .entity = Scene::Entity{
                    .type = "sphere",
                    .parameters = {float_parameter("radius", {sphere.radius}, sphere.source)},
                    .source = sphere.source,
                },
                .transform = SceneTransformSet{.start = transform, .end = transform},
                .material_name = sphere.material_name,
                .area_light    = make_preview_area_light(document, sphere.material_name, sphere.source),
            });
        }

        [[nodiscard]] Scene::Mesh make_sphere_preview_mesh(const Scene::Sphere& sphere) {
            if (sphere.name.empty()) throw std::runtime_error("Preview sphere name must not be empty when building rasterizer mesh");
            if (sphere.material_name.empty()) throw std::runtime_error(std::format("Preview sphere \"{}\" material name must not be empty when building rasterizer mesh", sphere.name));
            if (!std::isfinite(sphere.radius) || sphere.radius <= 0.0f) throw std::runtime_error(std::format("Preview sphere \"{}\" radius must be finite and positive when building rasterizer mesh", sphere.name));
            static_cast<void>(make_preview_scene_transform(sphere.transform, std::format("Preview sphere \"{}\"", sphere.name)));
            constexpr std::uint32_t latitude_segments = 32u;
            constexpr std::uint32_t longitude_segments = 64u;
            const std::uint32_t latitude_count = latitude_segments + 1u;
            const std::uint32_t longitude_count = longitude_segments + 1u;
            Scene::Mesh mesh{
                .name = sphere.name,
                .material_name = sphere.material_name,
                .transform = sphere.transform,
                .dynamic = sphere.dynamic,
                .source = sphere.source,
            };
            mesh.positions.reserve(static_cast<std::size_t>(latitude_count) * static_cast<std::size_t>(longitude_count));
            mesh.normals.reserve(mesh.positions.capacity());
            for (std::uint32_t latitude = 0u; latitude <= latitude_segments; ++latitude) {
                const float phi = std::numbers::pi_v<float> * static_cast<float>(latitude) / static_cast<float>(latitude_segments);
                const float y = std::cos(phi);
                const float ring = std::sin(phi);
                for (std::uint32_t longitude = 0u; longitude <= longitude_segments; ++longitude) {
                    const float theta = 2.0f * std::numbers::pi_v<float> * static_cast<float>(longitude) / static_cast<float>(longitude_segments);
                    const Vector3 normal{ring * std::cos(theta), y, ring * std::sin(theta)};
                    mesh.positions.push_back(Vector3{normal.x * sphere.radius, normal.y * sphere.radius, normal.z * sphere.radius});
                    mesh.normals.push_back(normal);
                }
            }
            mesh.indices.reserve(static_cast<std::size_t>(latitude_segments) * static_cast<std::size_t>(longitude_segments) * 6u);
            for (std::uint32_t latitude = 0u; latitude < latitude_segments; ++latitude) {
                for (std::uint32_t longitude = 0u; longitude < longitude_segments; ++longitude) {
                    const std::uint32_t current = latitude * longitude_count + longitude;
                    const std::uint32_t next = current + longitude_count;
                    if (latitude != 0u) {
                        mesh.indices.push_back(current);
                        mesh.indices.push_back(next);
                        mesh.indices.push_back(current + 1u);
                    }
                    if (latitude + 1u != latitude_segments) {
                        mesh.indices.push_back(current + 1u);
                        mesh.indices.push_back(next);
                        mesh.indices.push_back(next + 1u);
                    }
                }
            }
            return mesh;
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

        [[nodiscard]] std::vector<float> materialize_pathtracer_volume_channel(const Scene::VolumeGrid& volume, const Scene::VolumeChannel& channel, const std::uint64_t value_count, std::move_only_function<std::vector<float>(const Scene::VolumeGrid&, const Scene::VolumeChannel&)>* external_volume_materializer) {
            if (channel.source_kind == Scene::VolumeChannelSourceKind::Values) {
                if (channel.values.size() != value_count) throw std::runtime_error(std::format("Preview volume \"{}\" channel \"{}\" count does not match dimensions", volume.name, channel.name));
                return channel.values;
            }
            if (external_volume_materializer == nullptr || !*external_volume_materializer) throw std::runtime_error(std::format("Volume \"{}\" channel \"{}\" uses an external GPU source; pathtracer scene construction requires an explicit static volume snapshot materializer", volume.name, channel.name));
            std::vector<float> values = (*external_volume_materializer)(volume, channel);
            if (values.size() != value_count) throw std::runtime_error(std::format("Pathtracer volume snapshot for \"{}\" channel \"{}\" produced {} values; expected {}", volume.name, channel.name, values.size(), value_count));
            for (const float value : values)
                if (!std::isfinite(value)) throw std::runtime_error(std::format("Pathtracer volume snapshot for \"{}\" channel \"{}\" contains a non-finite value", volume.name, channel.name));
            return values;
        }

        void append_volume(Scene::ResolvedScene& scene, const Scene::Document& document, const Scene::VolumeGrid& volume, std::move_only_function<std::vector<float>(const Scene::VolumeGrid&, const Scene::VolumeChannel&)>* external_volume_materializer) {
            if (volume.name.empty()) throw std::runtime_error("Preview volume name must not be empty when building canonical scene");
            const Scene::PreviewMaterial* material = find_preview_material(document, volume.material_name);
            if (material == nullptr) throw std::runtime_error(std::format("Preview volume \"{}\" references unknown material \"{}\"", volume.name, volume.material_name));
            const Scene::VolumeChannel* density = find_volume_channel(volume, "density");
            if (density == nullptr) throw std::runtime_error(std::format("Preview volume \"{}\" requires a density channel for canonical path tracing", volume.name));
            const Scene::VolumeChannel* temperature = find_volume_channel(volume, "temperature");
            const Scene::VolumeChannel* color = find_volume_channel(volume, "color");
            if (density->format != Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Preview volume \"{}\" density channel must use Float32 format", volume.name));
            if (temperature != nullptr && temperature->format != Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Preview volume \"{}\" temperature channel must use Float32 format", volume.name));
            if (color != nullptr && color->format != Scene::VolumeChannelFormat::Float32x3) throw std::runtime_error(std::format("Preview volume \"{}\" color channel must use Float32x3 format", volume.name));
            if (color != nullptr && temperature != nullptr) throw std::runtime_error(std::format("Preview volume \"{}\" cannot build a colored rgbgrid medium with a temperature channel", volume.name));
            const std::uint64_t value_count = checked_volume_cell_count(volume);
            std::vector<float> density_values = materialize_pathtracer_volume_channel(volume, *density, value_count, external_volume_materializer);
            log_volume_pathtracer_stats(volume, *material, density_values);
            const std::string medium_type = color == nullptr ? "uniformgrid" : "rgbgrid";
            std::vector<Scene::Parameter> parameters{
                string_parameter("type", {medium_type}, volume.source),
                integer_parameter("nx", {static_cast<int>(volume.dimensions[0])}, volume.source),
                integer_parameter("ny", {static_cast<int>(volume.dimensions[1])}, volume.source),
                integer_parameter("nz", {static_cast<int>(volume.dimensions[2])}, volume.source),
                float_parameter("scale", {material->volume_density_scale}, volume.source),
            };
            if (color == nullptr) {
                parameters.push_back(float_parameter("density", std::move(density_values), volume.source));
                parameters.push_back(float_parameter("temperaturescale", {material->volume_temperature_scale}, volume.source));
                parameters.push_back(rgb_parameter("sigma_a", Vector3{0.08f, 0.08f, 0.08f}, volume.source));
                parameters.push_back(rgb_parameter("sigma_s", Vector3{0.92f, 0.92f, 0.92f}, volume.source));
            } else {
                if (value_count > std::numeric_limits<std::uint64_t>::max() / 3u) throw std::runtime_error(std::format("Preview volume \"{}\" color value count exceeds uint64 range", volume.name));
                const std::uint64_t color_value_count = value_count * 3u;
                std::vector<float> color_values = materialize_pathtracer_volume_channel(volume, *color, color_value_count, external_volume_materializer);
                std::vector<float> sigma_a(color_values.size(), 0.0f);
                std::vector<float> sigma_s{};
                sigma_s.reserve(color_values.size());
                for (std::uint64_t index = 0u; index < value_count; ++index) {
                    const float density_value = std::max(0.0f, density_values.at(static_cast<std::size_t>(index)));
                    for (std::uint32_t component = 0u; component < 3u; ++component) {
                        const float color_value = color_values.at(static_cast<std::size_t>(index * 3u + component));
                        if (color_value < 0.0f) throw std::runtime_error(std::format("Preview volume \"{}\" color channel contains a negative value", volume.name));
                        sigma_s.push_back(density_value * color_value);
                    }
                }
                parameters.push_back(rgb_parameter("sigma_a", std::move(sigma_a), volume.source));
                parameters.push_back(rgb_parameter("sigma_s", std::move(sigma_s), volume.source));
            }
            if (temperature != nullptr) {
                std::vector<float> temperature_values = materialize_pathtracer_volume_channel(volume, *temperature, value_count, external_volume_materializer);
                parameters.push_back(float_parameter("temperature", std::move(temperature_values), volume.source));
            }
            const std::string medium_name = std::format("{}.__medium", volume.name);
            scene.media.push_back(Scene::Medium{
                .name = medium_name,
                .entity = Scene::Entity{.type = medium_type, .parameters = std::move(parameters), .source = volume.source},
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
                if (light.kind == Scene::PreviewLightKind::Area) throw std::runtime_error(std::format("Preview light \"{}\" uses an area light kind without explicit area geometry", light.name));
                if (!is_finite(light.color)) throw std::runtime_error(std::format("Preview light \"{}\" color must be finite when building canonical scene", light.name));
                if (light.color.x < 0.0f || light.color.y < 0.0f || light.color.z < 0.0f) throw std::runtime_error(std::format("Preview light \"{}\" color must be non-negative when building canonical scene", light.name));
                if (!std::isfinite(light.intensity) || light.intensity < 0.0f) throw std::runtime_error(std::format("Preview light \"{}\" intensity must be finite and non-negative when building canonical scene", light.name));
                if (light.kind == Scene::PreviewLightKind::Spot && (!std::isfinite(light.cone_angle_degrees) || light.cone_angle_degrees <= 0.0f || light.cone_angle_degrees >= 180.0f)) throw std::runtime_error(std::format("Preview light \"{}\" cone angle must be inside (0, 180) when building canonical scene", light.name));
                const SceneTransform transform = make_preview_scene_transform(light.transform, std::format("Preview light \"{}\"", light.name));
                std::vector<Scene::Parameter> parameters{};
                std::string type{};
                if (light.kind == Scene::PreviewLightKind::Directional) {
                    type = "distant";
                    parameters.push_back(rgb_parameter("L", light.color, light.source));
                    parameters.push_back(float_parameter("scale", {light.intensity}, light.source));
                    parameters.push_back(point3_parameter("from", {0.0f, 0.0f, 1.0f}, light.source));
                    parameters.push_back(point3_parameter("to", {0.0f, 0.0f, 0.0f}, light.source));
                }
                if (light.kind == Scene::PreviewLightKind::Environment) {
                    type = "infinite";
                    parameters.push_back(rgb_parameter("L", light.color, light.source));
                    parameters.push_back(float_parameter("scale", {light.intensity}, light.source));
                }
                if (light.kind == Scene::PreviewLightKind::Point) {
                    type = "point";
                    parameters.push_back(rgb_parameter("I", light.color, light.source));
                    parameters.push_back(float_parameter("scale", {light.intensity}, light.source));
                }
                if (light.kind == Scene::PreviewLightKind::Spot) {
                    type = "spot";
                    parameters.push_back(rgb_parameter("I", light.color, light.source));
                    parameters.push_back(float_parameter("scale", {light.intensity}, light.source));
                    parameters.push_back(point3_parameter("from", {0.0f, 0.0f, 0.0f}, light.source));
                    parameters.push_back(point3_parameter("to", {0.0f, 0.0f, -1.0f}, light.source));
                    parameters.push_back(float_parameter("coneangle", {light.cone_angle_degrees}, light.source));
                }
                if (type.empty()) throw std::runtime_error(std::format("Preview light \"{}\" uses an unmapped light kind", light.name));
                scene.lights.push_back(Scene::Light{
                    .name = light.name,
                    .entity = Scene::Entity{
                        .type = std::move(type),
                        .parameters = std::move(parameters),
                        .source = light.source,
                    },
                    .transform = SceneTransformSet{.start = transform, .end = transform},
                });
            }
        }

        [[nodiscard]] SceneTransform make_look_at_transform(const Vector3 eye, const Vector3 target, const Vector3 up) {
            return camera_world_from_camera(camera_pose_from_look_at(eye, target, up));
        }

        [[nodiscard]] Scene::ResolvedScene make_resolved_scene_from_preview(const Scene::Document& document, const Scene::ResolvedFrame& frame, const Scene::Revision revision, std::move_only_function<std::vector<float>(const Scene::VolumeGrid&, const Scene::VolumeChannel&)>* external_volume_materializer) {
            if (document.name.empty()) throw std::runtime_error("Preview document name must not be empty when building canonical scene");
            validate_cameras(frame.cameras, document.active_camera_name, std::format("Preview document \"{}\"", document.name));
            const Scene::Camera& active_camera = require_active_camera(frame.cameras, document.active_camera_name, std::format("Preview document \"{}\"", document.name));
            Scene::ResolvedScene scene{
                .revision = revision,
                .name = document.name,
                .title = document.title.empty() ? document.name : document.title,
                .source = document.source.empty() ? std::format("scene://{}", document.name) : document.source,
            };
            scene.render_settings.camera = Scene::Entity{
                .type = "perspective",
                .parameters = {float_parameter("fov", {pathtracer_camera_fov_degrees(active_camera)}, active_camera.source)},
                .source = active_camera.source,
            };
            const SceneTransform world_from_camera = camera_world_from_camera(active_camera.view.pose);
            scene.render_settings.camera_transform = SceneTransformSet{.start = world_from_camera, .end = world_from_camera};
            const std::set<std::string> texture_names = append_canonical_textures(scene, document);
            append_canonical_materials(scene, document, texture_names, !frame.volumes.empty());
            append_preview_lights(scene, document);
            for (const Scene::Mesh& mesh : frame.meshes) append_mesh_shape(scene, document, mesh);
            for (const Scene::Sphere& sphere : frame.spheres) append_sphere_shape(scene, document, sphere);
            for (const Scene::PointCloud& point_cloud : frame.point_clouds) append_point_cloud_shapes(scene, document, point_cloud);
            for (const Scene::VolumeGrid& volume : frame.volumes) append_volume(scene, document, volume, external_volume_materializer);
            if (scene.shapes.empty()) throw EmptySceneError{std::format("Preview document \"{}\" produced no canonical pathtracer shapes", document.name)};
            return scene;
        }

        [[nodiscard]] Scene::DebugAttachmentSet resolve_debug_attachment_set(const Scene::DebugAttachmentSet& document_attachments, const Scene::DebugAttachmentSet& frame_attachments) {
            return Scene::DebugAttachmentSet{
                .viewport_segment_sets = resolve_scene_items(document_attachments.viewport_segment_sets, frame_attachments.viewport_segment_sets, "viewport segment set"),
                .viewport_voxel_grids = resolve_scene_items(document_attachments.viewport_voxel_grids, frame_attachments.viewport_voxel_grids, "viewport voxel grid"),
                .viewport_camera_visuals = resolve_scene_items(document_attachments.viewport_camera_visuals, frame_attachments.viewport_camera_visuals, "viewport camera visual"),
            };
        }

        [[nodiscard]] Scene::ResolvedFrame resolve_document_frame(const Scene::Document& document, const Scene::FrameSnapshot& frame) {
            Scene::ResolvedFrame resolved{
                .meshes = resolve_scene_items(document.meshes, frame.meshes, "mesh"),
                .spheres = resolve_scene_items(document.spheres, frame.spheres, "sphere"),
                .point_clouds = resolve_scene_items(document.point_clouds, frame.point_clouds, "point cloud"),
                .volumes = resolve_scene_items(document.volumes, frame.volumes, "volume"),
                .cameras = resolve_scene_items(document.cameras, frame.cameras, "camera"),
                .debug_attachments = resolve_debug_attachment_set(document.debug_attachments, frame.debug_attachments),
            };
            validate_cameras(resolved.cameras, document.active_camera_name, "Scene resolved frame");
            validate_volumes(resolved.volumes, document);
            validate_debug_attachment_set(resolved.debug_attachments, resolved, document);
            return resolved;
        }

        [[nodiscard]] Scene::ResolvedFrame make_rasterizer_preview_frame(Scene::ResolvedFrame frame) {
            std::set<std::string> mesh_names{};
            for (const Scene::Mesh& mesh : frame.meshes) {
                if (mesh.name.empty()) throw std::runtime_error("Rasterizer preview mesh name must not be empty");
                if (!mesh_names.insert(mesh.name).second) throw std::runtime_error(std::format("Rasterizer preview mesh \"{}\" is duplicated", mesh.name));
            }
            frame.meshes.reserve(frame.meshes.size() + frame.spheres.size());
            for (const Scene::Sphere& sphere : frame.spheres) {
                if (!mesh_names.insert(sphere.name).second) throw std::runtime_error(std::format("Rasterizer preview sphere \"{}\" conflicts with a mesh name", sphere.name));
                frame.meshes.push_back(make_sphere_preview_mesh(sphere));
            }
            frame.spheres.clear();
            return frame;
        }

        void require_pbrt_export_color_space(const Scene::ColorSpace color_space, const std::string_view context) {
            if (color_space != Scene::ColorSpace::sRGB) throw std::runtime_error(std::format("{} uses a non-sRGB color space; PBRT export currently requires standard sRGB scene data", context));
        }

        [[nodiscard]] std::string pbrt_export_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} contains a non-finite float value", context));
            return std::format("{:.9g}", value);
        }

        [[nodiscard]] std::string pbrt_export_quoted(const std::string_view value, const std::string_view context) {
            std::string result{"\""};
            for (const char character : value) {
                if (character == '"' || character == '\\') {
                    result.push_back('\\');
                    result.push_back(character);
                    continue;
                }
                if (std::iscntrl(static_cast<unsigned char>(character))) throw std::runtime_error(std::format("{} contains a control character that cannot be written to a PBRT quoted string", context));
                result.push_back(character);
            }
            result.push_back('"');
            return result;
        }

        void write_pbrt_indent(std::ostream& output, const std::size_t indent) {
            for (std::size_t index = 0u; index < indent; ++index) output << ' ';
        }

        void require_pbrt_export_matching_type_parameter(const Scene::Entity& entity, const Scene::Parameter& parameter, const std::string_view kind) {
            const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
            if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("PBRT export {} \"{}\" has an invalid string type parameter", kind, entity.type));
            if (values->front() != entity.type) throw std::runtime_error(std::format("PBRT export {} type parameter \"{}\" does not match entity type \"{}\"", kind, values->front(), entity.type));
        }

        void require_pbrt_export_entity_type(const Scene::Entity& entity, const std::set<std::string_view>& supported, const std::string_view kind) {
            if (!supported.contains(std::string_view{entity.type.data(), entity.type.size()})) throw std::runtime_error(std::format("PBRT export does not support {} type \"{}\"", kind, entity.type));
        }

        void write_pbrt_parameter_values(std::ostream& output, const Scene::Parameter& parameter, const std::string_view context) {
            std::visit(
                [&output, &parameter, context](const auto& values) {
                    if (values.empty()) throw std::runtime_error(std::format("{} parameter \"{} {}\" must not be empty when exporting PBRT", context, parameter.type, parameter.name));
                    for (std::size_t index = 0u; index < values.size(); ++index) {
                        if (index != 0u) output << ' ';
                        if constexpr (std::is_same_v<typename std::remove_cvref_t<decltype(values)>::value_type, float>) {
                            output << pbrt_export_float(values.at(index), context);
                        } else if constexpr (std::is_same_v<typename std::remove_cvref_t<decltype(values)>::value_type, int>) {
                            if (parameter.type != "integer") throw std::runtime_error(std::format("{} parameter \"{} {}\" stores integers but is not an integer parameter", context, parameter.type, parameter.name));
                            output << values.at(index);
                        } else if constexpr (std::is_same_v<typename std::remove_cvref_t<decltype(values)>::value_type, std::string>) {
                            if (parameter.type != "string" && parameter.type != "texture" && parameter.type != "spectrum") throw std::runtime_error(std::format("{} parameter \"{} {}\" stores strings but is not a string-like PBRT parameter", context, parameter.type, parameter.name));
                            output << pbrt_export_quoted(values.at(index), context);
                        } else if constexpr (std::is_same_v<typename std::remove_cvref_t<decltype(values)>::value_type, std::uint8_t>) {
                            if (parameter.type != "bool") throw std::runtime_error(std::format("{} parameter \"{} {}\" stores bool values but is not a bool parameter", context, parameter.type, parameter.name));
                            output << (values.at(index) == 0u ? "false" : "true");
                        }
                    }
                },
                parameter.values);
        }

        void write_pbrt_parameter(std::ostream& output, const Scene::Parameter& parameter, const std::size_t indent, const std::string_view context) {
            if (parameter.type.empty() || parameter.name.empty()) throw std::runtime_error(std::format("{} contains a PBRT parameter with an empty declaration", context));
            require_pbrt_export_color_space(parameter.color_space, context);
            write_pbrt_indent(output, indent);
            output << pbrt_export_quoted(std::format("{} {}", parameter.type, parameter.name), context) << " [";
            write_pbrt_parameter_values(output, parameter, context);
            output << "]\n";
        }

        void write_pbrt_entity_parameters(std::ostream& output, const Scene::Entity& entity, const std::size_t indent, const bool skip_type_parameter, const std::string_view kind) {
            require_pbrt_export_color_space(entity.color_space, kind);
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (skip_type_parameter && parameter.type == "string" && parameter.name == "type") {
                    require_pbrt_export_matching_type_parameter(entity, parameter, kind);
                    continue;
                }
                write_pbrt_parameter(output, parameter, indent, kind);
            }
        }

        void write_pbrt_transform_matrix(std::ostream& output, const std::array<float, 16>& matrix, const std::size_t indent) {
            write_pbrt_indent(output, indent);
            output << "Transform [";
            for (std::size_t row = 0u; row < 4u; ++row) {
                for (std::size_t column = 0u; column < 4u; ++column) {
                    output << ' ' << pbrt_export_float(matrix.at(column * 4u + row), "PBRT export transform");
                }
            }
            output << " ]\n";
        }

        void write_pbrt_transform(std::ostream& output, const SceneTransformSet& transform, const Scene::SourceLocation& source, const std::string_view owner, const std::size_t indent) {
            require_static_scene_transform(transform, source, owner);
            write_pbrt_transform_matrix(output, transform.start.matrix, indent);
        }

        void write_pbrt_option_entity(std::ostream& output, const char* directive, const Scene::Entity& entity) {
            output << directive << ' ' << pbrt_export_quoted(entity.type, directive) << '\n';
            write_pbrt_entity_parameters(output, entity, 4u, false, directive);
        }

        void write_pbrt_named_material(std::ostream& output, const Scene::Material& material) {
            static const std::set<std::string_view> supported_materials{"diffuse", "conductor", "dielectric", "coateddiffuse", "interface", "none"};
            require_pbrt_export_entity_type(material.entity, supported_materials, "material");
            output << "MakeNamedMaterial " << pbrt_export_quoted(material.name, "PBRT export material name") << '\n';
            write_pbrt_indent(output, 4u);
            output << "\"string type\" [" << pbrt_export_quoted(material.entity.type, "PBRT export material type") << "]\n";
            write_pbrt_entity_parameters(output, material.entity, 4u, true, std::format("PBRT export material \"{}\"", material.name));
        }

        void write_pbrt_texture(std::ostream& output, const Scene::Texture& texture) {
            static const std::set<std::string_view> supported_textures{"constant", "imagemap", "scale", "mix"};
            require_pbrt_export_entity_type(texture.entity, supported_textures, "texture");
            if (texture.kind != "float" && texture.kind != "spectrum") throw std::runtime_error(std::format("PBRT export texture \"{}\" has unsupported kind \"{}\"", texture.name, texture.kind));
            output << "AttributeBegin\n";
            write_pbrt_transform(output, texture.transform, texture.entity.source, std::format("PBRT export texture \"{}\"", texture.name), 4u);
            write_pbrt_indent(output, 4u);
            output << "Texture " << pbrt_export_quoted(texture.name, "PBRT export texture name") << ' ' << pbrt_export_quoted(texture.kind, "PBRT export texture kind") << ' ' << pbrt_export_quoted(texture.entity.type, "PBRT export texture type") << '\n';
            write_pbrt_entity_parameters(output, texture.entity, 8u, false, std::format("PBRT export texture \"{}\"", texture.name));
            output << "AttributeEnd\n\n";
        }

        void write_pbrt_named_medium(std::ostream& output, const Scene::Medium& medium) {
            static const std::set<std::string_view> supported_media{"homogeneous", "uniformgrid", "rgbgrid"};
            require_pbrt_export_entity_type(medium.entity, supported_media, "medium");
            output << "AttributeBegin\n";
            write_pbrt_transform(output, medium.transform, medium.entity.source, std::format("PBRT export medium \"{}\"", medium.name), 4u);
            write_pbrt_indent(output, 4u);
            output << "MakeNamedMedium " << pbrt_export_quoted(medium.name, "PBRT export medium name") << '\n';
            write_pbrt_indent(output, 8u);
            output << "\"string type\" [" << pbrt_export_quoted(medium.entity.type, "PBRT export medium type") << "]\n";
            write_pbrt_entity_parameters(output, medium.entity, 8u, true, std::format("PBRT export medium \"{}\"", medium.name));
            output << "AttributeEnd\n\n";
        }

        void write_pbrt_medium_interface(std::ostream& output, const Scene::MediumInterface& medium_interface, const std::size_t indent) {
            if (medium_interface.inside.empty() && medium_interface.outside.empty()) return;
            write_pbrt_indent(output, indent);
            output << "MediumInterface " << pbrt_export_quoted(medium_interface.inside, "PBRT export medium interface") << ' ' << pbrt_export_quoted(medium_interface.outside, "PBRT export medium interface") << '\n';
        }

        void write_pbrt_light(std::ostream& output, const Scene::Light& light) {
            static const std::set<std::string_view> supported_lights{"point", "spot", "distant", "infinite"};
            require_pbrt_export_entity_type(light.entity, supported_lights, "light");
            output << "AttributeBegin\n";
            write_pbrt_transform(output, light.transform, light.entity.source, std::format("PBRT export light \"{}\"", light.name), 4u);
            if (!light.medium.empty()) {
                write_pbrt_indent(output, 4u);
                output << "MediumInterface " << pbrt_export_quoted("", "PBRT export light medium") << ' ' << pbrt_export_quoted(light.medium, "PBRT export light medium") << '\n';
            }
            write_pbrt_indent(output, 4u);
            output << "LightSource " << pbrt_export_quoted(light.entity.type, "PBRT export light type") << '\n';
            write_pbrt_entity_parameters(output, light.entity, 8u, false, std::format("PBRT export light \"{}\"", light.name));
            output << "AttributeEnd\n\n";
        }

        void write_pbrt_shape(std::ostream& output, const Scene::Shape& shape, const std::size_t indent) {
            static const std::set<std::string_view> supported_shapes{"trianglemesh", "plymesh", "sphere", "disk", "cylinder"};
            require_pbrt_export_entity_type(shape.entity, supported_shapes, "shape");
            write_pbrt_indent(output, indent);
            output << "AttributeBegin\n";
            write_pbrt_transform(output, shape.transform, shape.entity.source, std::format("PBRT export shape \"{}\"", shape.name), indent + 4u);
            if (shape.reverse_orientation) {
                write_pbrt_indent(output, indent + 4u);
                output << "ReverseOrientation\n";
            }
            write_pbrt_medium_interface(output, shape.medium_interface, indent + 4u);
            write_pbrt_indent(output, indent + 4u);
            output << "NamedMaterial " << pbrt_export_quoted(shape.material_name, "PBRT export shape material") << '\n';
            if (shape.area_light.has_value()) {
                if (shape.area_light->entity.type != "diffuse") throw std::runtime_error(std::format("PBRT export shape \"{}\" uses unsupported area light type \"{}\"", shape.name, shape.area_light->entity.type));
                write_pbrt_indent(output, indent + 4u);
                output << "AreaLightSource " << pbrt_export_quoted(shape.area_light->entity.type, "PBRT export area light type") << '\n';
                write_pbrt_entity_parameters(output, shape.area_light->entity, indent + 8u, false, std::format("PBRT export shape \"{}\" area light", shape.name));
            }
            write_pbrt_indent(output, indent + 4u);
            output << "Shape " << pbrt_export_quoted(shape.entity.type, "PBRT export shape type") << '\n';
            write_pbrt_entity_parameters(output, shape.entity, indent + 8u, false, std::format("PBRT export shape \"{}\"", shape.name));
            write_pbrt_indent(output, indent);
            output << "AttributeEnd\n\n";
        }

        void write_pbrt_object_definition(std::ostream& output, const Scene::ObjectDefinition& definition) {
            output << "ObjectBegin " << pbrt_export_quoted(definition.name, "PBRT export object definition name") << '\n';
            for (const Scene::Shape& shape : definition.shapes) write_pbrt_shape(output, shape, 4u);
            output << "ObjectEnd\n\n";
        }

        void write_pbrt_object_instance(std::ostream& output, const Scene::ObjectInstance& instance) {
            output << "AttributeBegin\n";
            write_pbrt_transform(output, instance.transform, instance.source, std::format("PBRT export object instance \"{}\"", instance.name), 4u);
            write_pbrt_indent(output, 4u);
            output << "ObjectInstance " << pbrt_export_quoted(instance.definition_name, "PBRT export object instance definition") << '\n';
            output << "AttributeEnd\n\n";
        }

        void require_pbrt_export_texture_references(const Scene::Entity& entity, const std::set<std::string>& texture_names, const std::string_view owner) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "texture") continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values == nullptr || values->empty()) throw std::runtime_error(std::format("PBRT export {} parameter \"texture {}\" must contain texture names", owner, parameter.name));
                for (const std::string& texture_name : *values) {
                    if (texture_name.empty()) throw std::runtime_error(std::format("PBRT export {} parameter \"texture {}\" references an empty texture name", owner, parameter.name));
                    if (!texture_names.contains(texture_name)) throw std::runtime_error(std::format("PBRT export {} parameter \"texture {}\" references unknown texture \"{}\"", owner, parameter.name, texture_name));
                }
            }
        }

        void require_pbrt_export_texture_references(const Scene::ResolvedScene& scene) {
            std::set<std::string> texture_names{};
            for (const Scene::Texture& texture : scene.textures) texture_names.insert(texture.name);
            for (const Scene::Texture& texture : scene.textures) require_pbrt_export_texture_references(texture.entity, texture_names, std::format("texture \"{}\"", texture.name));
            for (const Scene::Material& material : scene.materials) require_pbrt_export_texture_references(material.entity, texture_names, std::format("material \"{}\"", material.name));
            for (const Scene::Medium& medium : scene.media) require_pbrt_export_texture_references(medium.entity, texture_names, std::format("medium \"{}\"", medium.name));
            for (const Scene::Light& light : scene.lights) require_pbrt_export_texture_references(light.entity, texture_names, std::format("light \"{}\"", light.name));
            for (const Scene::Shape& shape : scene.shapes) {
                require_pbrt_export_texture_references(shape.entity, texture_names, std::format("shape \"{}\"", shape.name));
                if (shape.area_light.has_value()) require_pbrt_export_texture_references(shape.area_light->entity, texture_names, std::format("shape \"{}\" area light", shape.name));
            }
            for (const Scene::ObjectDefinition& definition : scene.object_definitions) {
                for (const Scene::Shape& shape : definition.shapes) {
                    require_pbrt_export_texture_references(shape.entity, texture_names, std::format("object definition \"{}\" shape \"{}\"", definition.name, shape.name));
                    if (shape.area_light.has_value()) require_pbrt_export_texture_references(shape.area_light->entity, texture_names, std::format("object definition \"{}\" shape \"{}\" area light", definition.name, shape.name));
                }
            }
        }

        void write_pbrt_scene_file(const Scene::ResolvedScene& scene, const std::filesystem::path& path) {
            validate_canonical_scene(scene);
            require_pbrt_export_texture_references(scene);
            if (!path.has_filename()) throw std::runtime_error("PBRT export path must include a filename");
            const std::filesystem::path parent = path.parent_path();
            if (!parent.empty() && !std::filesystem::exists(parent)) throw std::runtime_error(std::format("PBRT export directory does not exist: {}", parent.string()));

            std::ofstream output(path, std::ios::binary);
            if (!output) throw std::runtime_error(std::format("Failed to open PBRT export file: {}", path.string()));

            output << "# Generated by Spectra from canonical Y-up scene " << pbrt_export_quoted(scene.name, "PBRT export scene name") << "\n\n";
            write_pbrt_option_entity(output, "PixelFilter", scene.render_settings.filter);
            write_pbrt_option_entity(output, "Film", scene.render_settings.film);
            write_pbrt_option_entity(output, "Sampler", scene.render_settings.sampler);
            write_pbrt_option_entity(output, "Integrator", scene.render_settings.integrator);
            write_pbrt_option_entity(output, "Accelerator", scene.render_settings.accelerator);
            if (!scene.render_settings.camera_medium.empty()) output << "MediumInterface \"\" " << pbrt_export_quoted(scene.render_settings.camera_medium, "PBRT export camera medium") << '\n';
            require_static_scene_transform(scene.render_settings.camera_transform, scene.render_settings.camera.source, "PBRT export camera");
            write_pbrt_transform_matrix(output, scene.render_settings.camera_transform.start.inverse, 0u);
            write_pbrt_option_entity(output, "Camera", scene.render_settings.camera);
            output << "\nWorldBegin\n\n";

            for (const Scene::Texture& texture : scene.textures) write_pbrt_texture(output, texture);
            for (const Scene::Material& material : scene.materials) write_pbrt_named_material(output, material);
            if (!scene.materials.empty()) output << '\n';
            for (const Scene::Medium& medium : scene.media) write_pbrt_named_medium(output, medium);
            for (const Scene::Light& light : scene.lights) write_pbrt_light(output, light);
            for (const Scene::Shape& shape : scene.shapes) write_pbrt_shape(output, shape, 0u);
            for (const Scene::ObjectDefinition& definition : scene.object_definitions) write_pbrt_object_definition(output, definition);
            for (const Scene::ObjectInstance& instance : scene.object_instances) write_pbrt_object_instance(output, instance);

            if (!output) throw std::runtime_error(std::format("Failed while writing PBRT export file: {}", path.string()));
        }
    } // namespace

    void Scene::Edit::replace_timeline(Scene::Timeline timeline) {
        this->timeline_replacement = std::move(timeline);
        this->dirty = Scene::combine_dirty_flags(this->dirty, Scene::DirtyFlags::Timeline);
    }

    void Scene::Edit::replace_document(Scene::Document document) {
        this->document_replacement = std::move(document);
        this->dirty = Scene::combine_dirty_flags(this->dirty, Scene::DirtyFlags::Document);
    }

    void Scene::Edit::replace_frame(Scene::FrameSnapshot frame) {
        this->frame_replacement = std::move(frame);
        this->dirty = Scene::combine_dirty_flags(this->dirty, Scene::DirtyFlags::Frame);
    }

    namespace {
        [[nodiscard]] Scene::Document make_empty_document() {
            return Scene::Document{
                .revision = Scene::Revision{1},
                .name = "untitled",
                .title = "Untitled",
                .source = "scene://untitled",
                .timeline_enabled = false,
                .cameras = {
                    Scene::Camera{
                        .name = "Camera",
                        .view = camera_view_from_look_at(
                            Vector3{0.0f, 1.0f, 5.0f},
                            Vector3{0.0f, 0.0f, 0.0f},
                            Vector3{0.0f, 1.0f, 0.0f},
                            CameraProjection{
                                .kind = CameraProjectionKind::Perspective,
                                .vertical_fov_degrees = 45.0f,
                                .near_plane = 0.01f,
                                .far_plane = 200.0f,
                            }
                        ),
                    },
                },
                .active_camera_name = "Camera",
            };
        }

        void commit_scene_frame(Scene& scene_instance, Scene::FrameSnapshot frame) {
            Scene::Edit edit{};
            edit.replace_frame(std::move(frame));
            const Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void commit_scene_timeline(Scene& scene_instance, Scene::Timeline timeline) {
            Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            const Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
        }

        void commit_scene_timeline_and_frame(Scene& scene_instance, Scene::Timeline timeline, Scene::FrameSnapshot frame) {
            Scene::Edit edit{};
            edit.replace_timeline(std::move(timeline));
            edit.replace_frame(std::move(frame));
            const Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void commit_scene_document_and_frame(Scene& scene_instance, Scene::Document document, Scene::FrameSnapshot frame) {
            Scene::Edit edit{};
            edit.replace_document(std::move(document));
            edit.replace_frame(std::move(frame));
            const Scene::DirtyFlags dirty = scene_instance.commit(std::move(edit));
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Document)) throw std::runtime_error("Scene document commit did not mark the document dirty");
            if (!Scene::has_dirty_flag(dirty, Scene::DirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        [[nodiscard]] bool resolved_frame_has_renderable_entity(const Scene::ResolvedFrame& frame) {
            return !frame.meshes.empty() || !frame.spheres.empty() || !frame.point_clouds.empty() || !frame.volumes.empty();
        }

        void validate_scene_renderable_entities(Scene& scene_instance, const std::string_view context) {
            const Scene::ResolvedFrame frame = scene_instance.resolved_frame();
            if (!resolved_frame_has_renderable_entity(frame)) throw std::runtime_error(std::format("{} must contain at least one renderable Mesh, Sphere, PointCloud, or VolumeGrid entity", context));
        }

        [[nodiscard]] ControlTimelineMode control_timeline_mode(const Scene::TimelineMode mode) {
            switch (mode) {
            case Scene::TimelineMode::Live: return ControlTimelineMode::Live;
            case Scene::TimelineMode::Record: return ControlTimelineMode::Record;
            case Scene::TimelineMode::Playback: return ControlTimelineMode::Playback;
            }
            throw std::runtime_error("Unknown scene timeline mode");
        }

        [[nodiscard]] std::string lowercase_ascii(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] bool path_extension_is(const std::filesystem::path& path, const std::string_view extension) {
            return lowercase_ascii(path.extension().string()) == lowercase_ascii(std::string{extension});
        }

        [[nodiscard]] bool is_pbrt_scene_file(const std::filesystem::path& path) {
            if (path_extension_is(path, ".pbrt")) return true;
            if (!path_extension_is(path, ".gz")) return false;
            return path_extension_is(path.stem(), ".pbrt");
        }

        [[nodiscard]] std::string scene_file_title(const std::filesystem::path& path) {
            std::filesystem::path filename = path.filename();
            if (path_extension_is(filename, ".gz")) filename = filename.stem();
            if (path_extension_is(filename, ".pbrt")) filename = filename.stem();
            if (filename.empty()) throw std::runtime_error("PBRT scene path has an empty filename");
            return filename.string();
        }
    } // namespace

    void HostServiceRouter::set_gpu_buffer_backend(std::move_only_function<GpuBufferAllocation(const GpuBufferRequest&)> request_callback, std::move_only_function<void(std::uint64_t)> release_callback) {
        if (!request_callback) throw std::runtime_error("Scene GPU buffer request callback must not be empty");
        if (!release_callback) throw std::runtime_error("Scene GPU buffer release callback must not be empty");
        this->request_gpu_buffer_callback = std::move(request_callback);
        this->release_gpu_buffer_callback = std::move(release_callback);
        this->last_error_message.clear();
    }

    void HostServiceRouter::clear_gpu_buffer_backend() noexcept {
        this->request_gpu_buffer_callback = nullptr;
        this->release_gpu_buffer_callback = nullptr;
        this->gpu_buffer_allocations.clear();
        this->last_error_message.clear();
    }

    GpuBufferAllocation HostServiceRouter::request_gpu_buffer(const GpuBufferRequest& request) {
        try {
            if (!this->request_gpu_buffer_callback) throw std::runtime_error("Scene GPU buffer backend is not available");
            this->last_error_message.clear();
            GpuBufferAllocation allocation = this->request_gpu_buffer_callback(request);
            if (allocation.resource_id == 0u) throw std::runtime_error("Scene GPU buffer backend returned a zero resource id");
            if (allocation.byte_size == 0u) throw std::runtime_error("Scene GPU buffer backend returned a zero byte size");
            if (allocation.kind != request.kind) throw std::runtime_error(std::format("Scene GPU buffer backend returned kind {} for request kind {}", allocation.kind, request.kind));
            if (!this->gpu_buffer_allocations.emplace(allocation.resource_id, allocation).second) throw std::runtime_error(std::format("Scene GPU buffer resource {} already exists", allocation.resource_id));
            return allocation;
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    void HostServiceRouter::release_gpu_buffer(const std::uint64_t resource_id) {
        try {
            if (!this->release_gpu_buffer_callback) throw std::runtime_error("Scene GPU buffer backend is not available");
            const std::map<std::uint64_t, GpuBufferAllocation>::iterator found = this->gpu_buffer_allocations.find(resource_id);
            if (found == this->gpu_buffer_allocations.end()) throw std::runtime_error(std::format("Scene GPU buffer resource {} does not exist", resource_id));
            this->last_error_message.clear();
            this->release_gpu_buffer_callback(resource_id);
            this->gpu_buffer_allocations.erase(found);
        } catch (const std::exception& error) {
            this->last_error_message = error.what();
            throw;
        }
    }

    std::string_view HostServiceRouter::last_error() const {
        return this->last_error_message;
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

    Scene::Scene() : Scene(make_empty_document()) {}

    Scene::Scene(Scene::Document document) {
        if (document.revision.value == 0) document.revision = Scene::Revision{1};
        document.default_coordinate_system = coordinate_system(document.default_coordinate_system.name);
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
        preview_document.default_coordinate_system = coordinate_system(preview_document.default_coordinate_system.name);
        validate_canonical_scene(scene);
        this->current_revision = scene.revision;
        this->current_document = std::make_shared<Scene::Document>(std::move(preview_document));
        this->current_timeline.frames_per_second = this->current_document->frames_per_second;
        this->canonical_scene = std::move(scene);
    }

    Scene::Scene(Scene&& other) noexcept = default;
    Scene& Scene::operator=(Scene&& other) noexcept = default;
    Scene::~Scene() noexcept = default;

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
        return make_rasterizer_preview_frame(resolve_document_frame(document, frame_value));
    }

    Scene::ResolvedScene Scene::resolved_scene() const {
        return this->resolved_scene(std::move_only_function<std::vector<float>(const Scene::VolumeGrid&, const Scene::VolumeChannel&)>{});
    }

    Scene::ResolvedScene Scene::resolved_scene(std::move_only_function<std::vector<float>(const Scene::VolumeGrid&, const Scene::VolumeChannel&)> external_volume_materializer) const {
        if (this->current_document == nullptr && !this->canonical_scene.has_value()) throw std::runtime_error("Scene workspace does not contain a loaded scene");
        if (this->canonical_scene.has_value() && !this->current_timeline.current_frame.has_value()) {
            Scene::ResolvedScene scene = *this->canonical_scene;
            scene.revision = this->current_revision;
            validate_canonical_scene(scene);
            return scene;
        }
        const Scene::Document& document = this->preview_document();
        const Scene::FrameSnapshot empty_frame{};
        const Scene::FrameSnapshot& frame_value = this->current_timeline.current_frame.has_value() ? *this->current_timeline.current_frame : empty_frame;
        Scene::ResolvedScene scene = make_resolved_scene_from_preview(document, resolve_document_frame(document, frame_value), this->current_revision, &external_volume_materializer);
        validate_canonical_scene(scene);
        return scene;
    }

    Scene::Info Scene::info() const {
        return describe_scene(this->resolved_scene());
    }

    SceneKind Scene::kind() const {
        if (this->descriptor_valid) return this->current_descriptor.kind;
        return SceneKind::Static;
    }

    bool Scene::has_descriptor() const {
        return this->descriptor_valid;
    }

    const SceneDescriptor& Scene::descriptor() const {
        if (!this->descriptor_valid) throw std::runtime_error("Scene has no active descriptor");
        return this->current_descriptor;
    }

    bool Scene::has_controls() const {
        return this->descriptor_valid && this->current_descriptor.kind == SceneKind::Dynamic && this->driver_runtime.driver != nullptr;
    }

    std::shared_ptr<HostServiceRouter> Scene::host_services() const {
        return this->host;
    }

    ControlState Scene::control_state() const {
        return this->active_driver().control_state();
    }

    void Scene::replace_with_scene(Scene scene) {
        const std::shared_ptr<HostServiceRouter> preserved_host = std::move(this->host);
        const Scene::Revision replacement_revision{std::max(scene.current_revision.value, this->current_revision.value + 1u)};
        this->current_revision = replacement_revision;
        if (scene.current_document != nullptr) {
            Scene::Document document = *scene.current_document;
            document.revision = replacement_revision;
            this->current_document = std::make_shared<Scene::Document>(std::move(document));
        } else {
            this->current_document.reset();
        }
        this->current_timeline = std::move(scene.current_timeline);
        if (scene.canonical_scene.has_value()) {
            Scene::ResolvedScene resolved = std::move(*scene.canonical_scene);
            resolved.revision = replacement_revision;
            this->canonical_scene = std::move(resolved);
        } else {
            this->canonical_scene.reset();
        }
        this->current_descriptor = std::move(scene.current_descriptor);
        this->descriptor_valid = scene.descriptor_valid;
        this->driver_runtime = std::move(scene.driver_runtime);
        this->host = preserved_host == nullptr ? std::make_shared<HostServiceRouter>() : preserved_host;
    }

    void Scene::reset_driver_runtime() {
        this->driver_runtime.driver.reset();
        this->driver_runtime.frame_accumulator_seconds = 0.0;
        this->driver_runtime.stream_time_seconds = 0.0;
        this->driver_runtime.stream_frame_index = 0u;
        this->driver_runtime.observed_reset_request_serial = 0u;
        this->driver_runtime.observed_clear_recording_request_serial = 0u;
        this->driver_runtime.observed_scene_revision = 0u;
        this->driver_runtime.committed_playback_frame_index.reset();
        this->driver_runtime.updated_frame_number.reset();
    }

    SceneDriver& Scene::active_driver() const {
        if (!this->descriptor_valid || this->current_descriptor.kind != SceneKind::Dynamic) throw std::runtime_error("Active scene is not plugin-driven");
        if (this->driver_runtime.driver == nullptr) throw std::runtime_error("Active scene has no driver");
        return *this->driver_runtime.driver;
    }

    void Scene::close() {
        this->reset_driver_runtime();
        this->replace_with_scene(Scene{});
        this->descriptor_valid = false;
    }

    void Scene::open_static_scene(std::string id, std::string title, Scene scene) {
        if (id.empty()) throw std::runtime_error("Static scene id must not be empty");
        if (title.empty()) title = scene.info().title;
        if (title.empty()) throw std::runtime_error("Static scene title must not be empty");
        this->reset_driver_runtime();
        this->replace_with_scene(std::move(scene));
        this->current_descriptor = SceneDescriptor{
            .id = std::move(id),
            .title = std::move(title),
            .kind = SceneKind::Static,
        };
        this->descriptor_valid = true;
    }

    void Scene::open_pbrt_file(const std::filesystem::path& scene_path) {
        if (scene_path.empty()) throw std::runtime_error("Scene path must not be empty");
        const std::filesystem::path absolute_path = std::filesystem::absolute(scene_path).lexically_normal();
        if (std::filesystem::is_directory(absolute_path)) throw std::runtime_error("Scene path must be a PBRT file, not a directory");
        if (!std::filesystem::is_regular_file(absolute_path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", absolute_path.string()));
        if (!is_pbrt_scene_file(absolute_path)) throw std::runtime_error(std::format("{}: scene file must use .pbrt or .pbrt.gz", absolute_path.string()));
        this->open_static_scene(absolute_path.string(), scene_file_title(absolute_path), Scene::parse_pbrt_file(absolute_path));
    }

    void Scene::attach_driver(std::string id, std::string title, std::unique_ptr<SceneDriver> driver) {
        if (id.empty()) throw std::runtime_error("Plugin-driven scene id must not be empty");
        if (driver == nullptr) throw std::runtime_error("Plugin-driven scene requires a driver");
        driver->reset();
        Scene::Document document = driver->create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Plugin-driven scene document must enable timeline");
        if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Plugin-driven scene document frame rate must be finite and positive");
        if (title.empty()) title = document.title;
        if (title.empty()) throw std::runtime_error("Plugin-driven scene title must not be empty");
        Scene scene_instance{std::move(document)};
        Scene::FrameSnapshot snapshot = driver->create_scene_frame(Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds = 0.0,
            .frame_index = 0u,
        });
        const std::uint64_t scene_revision = driver->scene_revision();
        const std::shared_ptr<const Scene::Document> scene_document = scene_instance.document();
        Scene::Timeline timeline{
            .mode = Scene::TimelineMode::Live,
            .frames_per_second = scene_document->frames_per_second,
            .playing = true,
            .selected_frame_index = 0u,
        };
        commit_scene_timeline_and_frame(scene_instance, std::move(timeline), std::move(snapshot));
        validate_scene_renderable_entities(scene_instance, "Plugin-driven scene initial frame");
        this->reset_driver_runtime();
        this->replace_with_scene(std::move(scene_instance));
        this->driver_runtime.driver = std::move(driver);
        this->driver_runtime.observed_scene_revision = scene_revision;
        this->current_descriptor = SceneDescriptor{
            .id = std::move(id),
            .title = std::move(title),
            .kind = SceneKind::Dynamic,
        };
        this->descriptor_valid = true;
    }

    void Scene::open_plugin_scene(ScenePluginOpenRequest request) {
        if (request.host == nullptr) request.host = this->host;
        std::shared_ptr<PluginLibrary> plugin = std::make_shared<PluginLibrary>(std::move(request.plugin_path), std::move(request.options), std::move(request.host));
        std::string scene_id = plugin->scene_id();
        this->attach_driver(std::move(scene_id), {}, make_plugin_driver(std::move(plugin)));
    }

    void Scene::advance(const std::uint64_t frame_number, const double delta_seconds) {
        if (this->kind() == SceneKind::Static) return;
        if (this->driver_runtime.updated_frame_number.has_value() && *this->driver_runtime.updated_frame_number == frame_number) return;
        SceneDriver& driver = this->active_driver();
        const std::shared_ptr<const Scene::Document> document = this->document();
        if (!document->timeline_enabled) throw std::runtime_error("Plugin-driven scene must enable timeline");
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Scene delta time is invalid");
        Scene::Timeline timeline = this->timeline();
        if (timeline.frames_per_second <= 0.0) throw std::runtime_error("Scene timeline frame rate must be positive");
        const double fixed_delta_seconds = 1.0 / timeline.frames_per_second;
        const auto mark_updated = [this, frame_number] {
            this->driver_runtime.updated_frame_number = frame_number;
        };
        if (timeline.reset_request_serial != this->driver_runtime.observed_reset_request_serial) {
            this->reset_driver_scene(std::move(timeline));
            this->driver_runtime.observed_reset_request_serial = this->timeline().reset_request_serial;
            this->driver_runtime.committed_playback_frame_index.reset();
            mark_updated();
            return;
        }
        if (timeline.clear_recording_request_serial != this->driver_runtime.observed_clear_recording_request_serial) {
            timeline.recorded_frames.clear();
            timeline.selected_frame_index = 0u;
            commit_scene_timeline(*this, std::move(timeline));
            this->driver_runtime.observed_clear_recording_request_serial = this->timeline().clear_recording_request_serial;
            this->driver_runtime.committed_playback_frame_index.reset();
            mark_updated();
            return;
        }
        if (timeline.mode == Scene::TimelineMode::Playback) {
            if (timeline.recorded_frames.empty()) {
                mark_updated();
                return;
            }
            if (timeline.selected_frame_index >= timeline.recorded_frames.size()) throw std::runtime_error("Scene playback selected frame is out of range");
            if (this->driver_runtime.committed_playback_frame_index.has_value() && *this->driver_runtime.committed_playback_frame_index == timeline.selected_frame_index) {
                mark_updated();
                return;
            }
            Scene::FrameSnapshot selected_frame = timeline.recorded_frames.at(timeline.selected_frame_index);
            commit_scene_frame(*this, std::move(selected_frame));
            validate_scene_renderable_entities(*this, "Scene playback frame");
            this->driver_runtime.committed_playback_frame_index = timeline.selected_frame_index;
            mark_updated();
            return;
        }
        this->driver_runtime.committed_playback_frame_index.reset();
        const bool scene_advancing = timeline.playing && timeline.mode != Scene::TimelineMode::Playback;
        driver.update(UpdateInfo{
            .wall_delta_seconds = delta_seconds,
            .scene_delta_seconds = scene_advancing ? delta_seconds : 0.0,
            .time_seconds = this->driver_runtime.stream_time_seconds,
            .frame_index = this->driver_runtime.stream_frame_index,
            .timeline_mode = control_timeline_mode(timeline.mode),
            .timeline_playing = timeline.playing,
        });
        this->commit_driver_revision("Scene update");
        if (!timeline.playing) {
            mark_updated();
            return;
        }
        this->driver_runtime.frame_accumulator_seconds += delta_seconds;
        bool updated = false;
        Scene::FrameSnapshot snapshot{};
        while (this->driver_runtime.frame_accumulator_seconds >= fixed_delta_seconds) {
            this->driver_runtime.frame_accumulator_seconds -= fixed_delta_seconds;
            ++this->driver_runtime.stream_frame_index;
            this->driver_runtime.stream_time_seconds += fixed_delta_seconds;
            snapshot = driver.create_scene_frame(Scene::FrameInfo{
                .delta_seconds = fixed_delta_seconds,
                .time_seconds = this->driver_runtime.stream_time_seconds,
                .frame_index = this->driver_runtime.stream_frame_index,
            });
            updated = true;
        }
        if (!updated) {
            mark_updated();
            return;
        }
        if (timeline.mode == Scene::TimelineMode::Record) {
            timeline.recorded_frames.push_back(snapshot);
            timeline.selected_frame_index = timeline.recorded_frames.size() - 1u;
            commit_scene_timeline_and_frame(*this, std::move(timeline), std::move(snapshot));
            validate_scene_renderable_entities(*this, "Scene recorded frame");
            mark_updated();
            return;
        }
        commit_scene_frame(*this, std::move(snapshot));
        validate_scene_renderable_entities(*this, "Scene live frame");
        mark_updated();
    }

    void Scene::toggle_timeline_playback() {
        if (!this->document()->timeline_enabled) throw std::runtime_error("Scene does not support timeline playback");
        Scene::Timeline timeline = this->timeline();
        if (timeline.mode == Scene::TimelineMode::Playback) throw std::runtime_error("Scene playback can only be toggled in Live or Record mode");
        timeline.playing = !timeline.playing;
        commit_scene_timeline(*this, std::move(timeline));
        this->driver_runtime.updated_frame_number.reset();
    }

    void Scene::request_timeline_reset() {
        if (!this->document()->timeline_enabled) throw std::runtime_error("Scene does not support timeline reset");
        Scene::Timeline timeline = this->timeline();
        ++timeline.reset_request_serial;
        commit_scene_timeline(*this, std::move(timeline));
        this->driver_runtime.updated_frame_number.reset();
    }

    void Scene::execute_control_action(const std::string_view action_id, const std::span<const ControlOption> options) {
        if (action_id.empty()) throw std::runtime_error("Scene control action id must not be empty");
        this->active_driver().execute_control_action(action_id, options);
        this->commit_driver_revision("Scene control action");
        this->driver_runtime.updated_frame_number.reset();
    }

    void Scene::update_control_setting(const std::string_view key, const std::string_view value) {
        if (key.empty()) throw std::runtime_error("Scene control setting key must not be empty");
        this->active_driver().update_control_setting(key, value);
        this->commit_driver_revision("Scene control setting");
        this->driver_runtime.updated_frame_number.reset();
    }

    void Scene::commit_driver_revision(const std::string_view context) {
        SceneDriver& driver = this->active_driver();
        const std::uint64_t scene_revision = driver.scene_revision();
        if (scene_revision == this->driver_runtime.observed_scene_revision) return;
        Scene::Document document = driver.create_scene_document();
        if (!document.timeline_enabled) throw std::runtime_error("Plugin-driven scene document must enable timeline");
        if (!std::isfinite(document.frames_per_second) || document.frames_per_second <= 0.0) throw std::runtime_error("Plugin-driven scene document frame rate must be finite and positive");
        Scene::FrameSnapshot snapshot = driver.create_scene_frame(Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds = this->driver_runtime.stream_time_seconds,
            .frame_index = this->driver_runtime.stream_frame_index,
        });
        commit_scene_document_and_frame(*this, std::move(document), std::move(snapshot));
        validate_scene_renderable_entities(*this, context);
        this->driver_runtime.observed_scene_revision = scene_revision;
    }

    void Scene::reset_driver_scene(Scene::Timeline timeline) {
        SceneDriver& driver = this->active_driver();
        this->driver_runtime.frame_accumulator_seconds = 0.0;
        this->driver_runtime.stream_time_seconds = 0.0;
        this->driver_runtime.stream_frame_index = 0u;
        driver.reset();
        this->driver_runtime.observed_scene_revision = driver.scene_revision();
        Scene::FrameSnapshot snapshot = driver.create_scene_frame(Scene::FrameInfo{
            .delta_seconds = 0.0,
            .time_seconds = 0.0,
            .frame_index = 0u,
        });
        timeline.selected_frame_index = 0u;
        commit_scene_timeline_and_frame(*this, std::move(timeline), std::move(snapshot));
        validate_scene_renderable_entities(*this, "Scene reset frame");
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
        if (edit.document_replacement.has_value()) {
            Scene::Document document = std::move(*edit.document_replacement);
            document.revision = this->current_revision;
            document.default_coordinate_system = coordinate_system(document.default_coordinate_system.name);
            this->current_document = std::make_shared<Scene::Document>(std::move(document));
            this->current_timeline.frames_per_second = this->current_document->frames_per_second;
            this->canonical_scene.reset();
        }
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

    void WritePbrtScene(const Scene::ResolvedScene& scene, const std::filesystem::path& path) {
        write_pbrt_scene_file(scene, path);
    }

    bool is_plugin_file(const std::filesystem::path& path) {
        const PluginAbiCodec codec{};
        return codec.accepts_plugin_path(path);
    }

    ScenePluginInfo inspect_plugin(const std::filesystem::path& plugin_path) {
        PluginLibrary plugin{plugin_path};
        return plugin.info();
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

        [[nodiscard]] std::array<float, 16> multiply_matrix(const std::array<float, 16>& left, const std::array<float, 16>& right) {
            std::array<float, 16> result{};
            for (std::size_t row = 0u; row < 4u; ++row)
                for (std::size_t column = 0u; column < 4u; ++column)
                    result.at(row * 4u + column) =
                        matrix_value(left, row, 0u) * matrix_value(right, 0u, column) +
                        matrix_value(left, row, 1u) * matrix_value(right, 1u, column) +
                        matrix_value(left, row, 2u) * matrix_value(right, 2u, column) +
                        matrix_value(left, row, 3u) * matrix_value(right, 3u, column);
            return result;
        }

        [[nodiscard]] SceneTransform multiply_transform(const SceneTransform& left, const SceneTransform& right) {
            return SceneTransform{
                .matrix  = multiply_matrix(left.matrix, right.matrix),
                .inverse = multiply_matrix(right.inverse, left.inverse),
            };
        }

        [[nodiscard]] bool transform_differs(const SceneTransform& left, const SceneTransform& right) {
            return left.matrix != right.matrix || left.inverse != right.inverse;
        }

        [[nodiscard]] SceneTransformSet multiply_transform_set(const SceneTransformSet& left, const SceneTransformSet& right) {
            SceneTransformSet result{
                .start      = multiply_transform(left.start, right.start),
                .end        = multiply_transform(left.end, right.end),
                .start_time = left.start_time,
                .end_time   = left.end_time,
            };
            result.animated = transform_differs(result.start, result.end);
            return result;
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

        [[nodiscard]] const std::vector<float>* optional_float_values(const Scene::Entity& entity, const std::string_view type, const std::string_view name) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != type || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("PBRT preview light parameter \"{}\" must contain float values", name));
                return values;
            }
            return nullptr;
        }

        [[nodiscard]] std::string optional_texture_reference_value(const Scene::Entity& entity, const std::string_view name) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "texture" || parameter.name != name) continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("PBRT preview material parameter \"{}\" must contain exactly one texture name", name));
                if (values->front().empty()) throw std::runtime_error(std::format("PBRT preview material parameter \"{}\" references an empty texture name", name));
                return values->front();
            }
            return {};
        }

        [[nodiscard]] std::string required_string_value(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one string", context, name));
                if (values->front().empty()) throw std::runtime_error(std::format("{} parameter \"{}\" must not be empty", context, name));
                return values->front();
            }
            throw std::runtime_error(std::format("{} requires \"string {}\"", context, name));
        }

        [[nodiscard]] std::string optional_string_value(const Scene::Entity& entity, const std::string_view name, const std::string_view default_value, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one string", context, name));
                if (values->front().empty()) throw std::runtime_error(std::format("{} parameter \"{}\" must not be empty", context, name));
                return values->front();
            }
            return std::string{default_value};
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

        [[nodiscard]] const std::vector<int>* optional_int_values(const Scene::Entity& entity, const std::string_view name) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "integer" || parameter.name != name) continue;
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("PBRT preview parameter \"{}\" must contain integer values", name));
                return values;
            }
            return nullptr;
        }

        [[nodiscard]] int optional_one_int_value(const Scene::Entity& entity, const std::string_view name, const int default_value, const std::string_view context) {
            const std::vector<int>* values = optional_int_values(entity, name);
            if (values == nullptr) return default_value;
            if (values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one integer", context, name));
            return values->front();
        }

        [[nodiscard]] Vector3 required_rgb_value(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            const std::vector<float>& values = required_float_values(entity, "rgb", name, context);
            if (values.size() != 3u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly three RGB values", context, name));
            return Vector3{values.at(0), values.at(1), values.at(2)};
        }

        [[nodiscard]] Vector3 optional_rgb_value(const Scene::Entity& entity, const std::string_view name, const Vector3 default_value) {
            const std::vector<float>* values = optional_float_values(entity, "rgb", name);
            if (values == nullptr) return default_value;
            if (values->size() != 3u) throw std::runtime_error(std::format("PBRT preview light parameter \"{}\" must contain exactly three RGB values", name));
            return Vector3{values->at(0), values->at(1), values->at(2)};
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

        [[nodiscard]] float optional_one_float_value(const Scene::Entity& entity, const std::string_view name, const float default_value) {
            const std::vector<float>* values = optional_float_values(entity, "float", name);
            if (values == nullptr) return default_value;
            if (values->size() != 1u) throw std::runtime_error(std::format("PBRT preview light parameter \"{}\" must contain exactly one float", name));
            return values->front();
        }

        [[nodiscard]] Vector3 optional_point3_value(const Scene::Entity& entity, const std::string_view name, const Vector3 default_value) {
            const std::vector<float>* values = optional_float_values(entity, "point3", name);
            if (values == nullptr) return default_value;
            if (values->size() != 3u) throw std::runtime_error(std::format("PBRT preview light parameter \"{}\" must contain exactly three point values", name));
            return Vector3{values->at(0), values->at(1), values->at(2)};
        }

        [[nodiscard]] Vector3 optional_vector3_value(const Scene::Entity& entity, const std::string_view name, const Vector3 default_value) {
            const std::vector<float>* values = optional_float_values(entity, "vector3", name);
            if (values == nullptr) return default_value;
            if (values->size() != 3u) throw std::runtime_error(std::format("PBRT preview parameter \"{}\" must contain exactly three vector values", name));
            return Vector3{values->at(0), values->at(1), values->at(2)};
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

        [[nodiscard]] Vector3 transform_vector(const SceneTransform& transform, const Vector3 vector) {
            const std::array<float, 16>& matrix = transform.matrix;
            return Vector3{
                matrix_value(matrix, 0u, 0u) * vector.x + matrix_value(matrix, 0u, 1u) * vector.y + matrix_value(matrix, 0u, 2u) * vector.z,
                matrix_value(matrix, 1u, 0u) * vector.x + matrix_value(matrix, 1u, 1u) * vector.y + matrix_value(matrix, 1u, 2u) * vector.z,
                matrix_value(matrix, 2u, 0u) * vector.x + matrix_value(matrix, 2u, 1u) * vector.y + matrix_value(matrix, 2u, 2u) * vector.z,
            };
        }

        [[nodiscard]] Vector3 transform_position(const SceneTransform& transform) {
            const std::array<float, 16>& matrix = transform.matrix;
            return Vector3{matrix_value(matrix, 0u, 3u), matrix_value(matrix, 1u, 3u), matrix_value(matrix, 2u, 3u)};
        }

        [[nodiscard]] Quaternion quaternion_from_light_forward(const Vector3 light_forward, const std::string_view context) {
            const Vector3 source{0.0f, 0.0f, -1.0f};
            const Vector3 target = normalize(light_forward, context);
            const float cosine = dot(source, target);
            if (cosine > 0.999999f) return Quaternion{};
            if (cosine < -0.999999f) return Quaternion{0.0f, 1.0f, 0.0f, 0.0f};
            const Vector3 axis = cross(source, target);
            return normalized_quaternion(Quaternion{axis.x, axis.y, axis.z, 1.0f + cosine}, context);
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

        [[nodiscard]] std::vector<std::string> split_ascii_words(const std::string& line) {
            std::istringstream stream{line};
            std::vector<std::string> words{};
            std::string word{};
            while (stream >> word) words.push_back(std::move(word));
            return words;
        }

        [[nodiscard]] std::size_t parse_ascii_size(const std::string& text, const std::string_view context) {
            std::size_t value{};
            const char* begin = text.data();
            const char* end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end) throw std::runtime_error(std::format("{} must be an unsigned integer", context));
            return value;
        }

        [[nodiscard]] std::uint32_t parse_ascii_u32(const std::string& text, const std::string_view context) {
            const std::size_t value = parse_ascii_size(text, context);
            if (value > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error(std::format("{} exceeds uint32 range", context));
            return static_cast<std::uint32_t>(value);
        }

        [[nodiscard]] float parse_ascii_float(const std::string& text, const std::string_view context) {
            float value{};
            const char* begin = text.data();
            const char* end = begin + text.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) throw std::runtime_error(std::format("{} must be a finite float", context));
            return value;
        }

        [[nodiscard]] std::optional<std::size_t> property_index(const std::vector<std::string>& properties, const std::string_view name) {
            for (std::size_t index = 0u; index < properties.size(); ++index) if (properties.at(index) == name) return index;
            return std::nullopt;
        }

        void generate_mesh_normals(Scene::Mesh& mesh, const std::string_view context) {
            mesh.normals.assign(mesh.positions.size(), Vector3{});
            for (std::size_t index = 0u; index < mesh.indices.size(); index += 3u) {
                const std::uint32_t i0 = mesh.indices.at(index);
                const std::uint32_t i1 = mesh.indices.at(index + 1u);
                const std::uint32_t i2 = mesh.indices.at(index + 2u);
                if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size()) throw std::runtime_error(std::format("{} face references an out-of-range vertex", context));
                const Vector3& p0 = mesh.positions.at(i0);
                const Vector3& p1 = mesh.positions.at(i1);
                const Vector3& p2 = mesh.positions.at(i2);
                const Vector3 normal = cross(p1 - p0, p2 - p0);
                mesh.normals.at(i0) += normal;
                mesh.normals.at(i1) += normal;
                mesh.normals.at(i2) += normal;
            }
            for (Vector3& normal : mesh.normals) normal = normalize(normal, std::format("{} generated normal", context));
        }

        [[nodiscard]] Scene::Mesh read_ascii_ply_mesh(const std::filesystem::path& path, const std::string& object_name, const std::string& material_name, const Scene::SourceLocation& source) {
            std::ifstream input(path);
            if (!input) throw std::runtime_error(std::format("{}: unable to open PLY mesh", path.string()));
            std::string line{};
            if (!std::getline(input, line) || line != "ply") throw std::runtime_error(std::format("{}: expected PLY header", path.string()));

            bool ascii_format = false;
            bool header_complete = false;
            std::string active_element{};
            std::size_t vertex_count = 0u;
            std::size_t face_count = 0u;
            std::vector<std::string> vertex_properties{};
            std::string face_index_property{};
            while (std::getline(input, line)) {
                const std::vector<std::string> words = split_ascii_words(line);
                if (words.empty()) continue;
                if (words.at(0) == "comment" || words.at(0) == "obj_info") continue;
                if (words.at(0) == "format") {
                    if (words.size() != 3u) throw std::runtime_error(std::format("{}: invalid PLY format line", path.string()));
                    if (words.at(1) != "ascii" || words.at(2) != "1.0") throw std::runtime_error(std::format("{}: PBRT preview supports only ASCII PLY format 1.0", path.string()));
                    ascii_format = true;
                    continue;
                }
                if (words.at(0) == "element") {
                    if (words.size() != 3u) throw std::runtime_error(std::format("{}: invalid PLY element line", path.string()));
                    active_element = words.at(1);
                    const std::size_t count = parse_ascii_size(words.at(2), std::format("{} element count", path.string()));
                    if (active_element == "vertex") vertex_count = count;
                    if (active_element == "face") face_count = count;
                    continue;
                }
                if (words.at(0) == "property") {
                    if (active_element == "vertex") {
                        if (words.size() != 3u) throw std::runtime_error(std::format("{}: vertex properties must be scalar", path.string()));
                        vertex_properties.push_back(words.at(2));
                        continue;
                    }
                    if (active_element == "face") {
                        if (words.size() == 5u && words.at(1) == "list") {
                            if (face_index_property.empty()) face_index_property = words.at(4);
                            continue;
                        }
                        continue;
                    }
                    continue;
                }
                if (words.at(0) == "end_header") {
                    header_complete = true;
                    break;
                }
                throw std::runtime_error(std::format("{}: unsupported PLY header directive \"{}\"", path.string(), words.at(0)));
            }
            if (!header_complete || !ascii_format) throw std::runtime_error(std::format("{}: incomplete ASCII PLY header", path.string()));
            if (vertex_count == 0u || face_count == 0u) throw std::runtime_error(std::format("{}: PLY mesh requires vertex and face elements", path.string()));
            const std::optional<std::size_t> x_index = property_index(vertex_properties, "x");
            const std::optional<std::size_t> y_index = property_index(vertex_properties, "y");
            const std::optional<std::size_t> z_index = property_index(vertex_properties, "z");
            if (!x_index.has_value() || !y_index.has_value() || !z_index.has_value()) throw std::runtime_error(std::format("{}: PLY vertex coordinates x/y/z are required", path.string()));
            const std::optional<std::size_t> nx_index = property_index(vertex_properties, "nx");
            const std::optional<std::size_t> ny_index = property_index(vertex_properties, "ny");
            const std::optional<std::size_t> nz_index = property_index(vertex_properties, "nz");
            const bool has_normals = nx_index.has_value() && ny_index.has_value() && nz_index.has_value();
            if ((nx_index.has_value() || ny_index.has_value() || nz_index.has_value()) && !has_normals) throw std::runtime_error(std::format("{}: PLY normals require nx/ny/nz together", path.string()));
            std::optional<std::size_t> u_index = property_index(vertex_properties, "u");
            std::optional<std::size_t> v_index = property_index(vertex_properties, "v");
            if (!u_index.has_value()) u_index = property_index(vertex_properties, "s");
            if (!v_index.has_value()) v_index = property_index(vertex_properties, "t");
            const bool has_uvs = u_index.has_value() && v_index.has_value();
            if ((u_index.has_value() || v_index.has_value()) && !has_uvs) throw std::runtime_error(std::format("{}: PLY UVs require u/v or s/t together", path.string()));
            if (face_index_property != "vertex_indices" && face_index_property != "vertex_index") throw std::runtime_error(std::format("{}: PLY face list property vertex_indices is required", path.string()));

            Scene::Mesh mesh{
                .name          = object_name,
                .material_name = material_name,
                .dynamic       = false,
                .source        = source,
            };
            mesh.positions.reserve(vertex_count);
            if (has_normals) mesh.normals.reserve(vertex_count);
            if (has_uvs) mesh.uvs.reserve(vertex_count);
            for (std::size_t vertex = 0u; vertex < vertex_count; ++vertex) {
                if (!std::getline(input, line)) throw std::runtime_error(std::format("{}: missing PLY vertex row {}", path.string(), vertex));
                const std::vector<std::string> words = split_ascii_words(line);
                if (words.size() < vertex_properties.size()) throw std::runtime_error(std::format("{}: PLY vertex row {} has too few values", path.string(), vertex));
                const std::string vertex_context = std::format("{} vertex {}", path.string(), vertex);
                mesh.positions.push_back(Vector3{
                    parse_ascii_float(words.at(*x_index), std::format("{} x", vertex_context)),
                    parse_ascii_float(words.at(*y_index), std::format("{} y", vertex_context)),
                    parse_ascii_float(words.at(*z_index), std::format("{} z", vertex_context)),
                });
                if (has_normals)
                    mesh.normals.push_back(normalize(Vector3{
                        parse_ascii_float(words.at(*nx_index), std::format("{} nx", vertex_context)),
                        parse_ascii_float(words.at(*ny_index), std::format("{} ny", vertex_context)),
                        parse_ascii_float(words.at(*nz_index), std::format("{} nz", vertex_context)),
                    }, std::format("{} normal", vertex_context)));
                if (has_uvs)
                    mesh.uvs.push_back(std::array<float, 2>{
                        parse_ascii_float(words.at(*u_index), std::format("{} u", vertex_context)),
                        parse_ascii_float(words.at(*v_index), std::format("{} v", vertex_context)),
                    });
            }
            mesh.indices.reserve(face_count * 6u);
            for (std::size_t face = 0u; face < face_count; ++face) {
                if (!std::getline(input, line)) throw std::runtime_error(std::format("{}: missing PLY face row {}", path.string(), face));
                const std::vector<std::string> words = split_ascii_words(line);
                if (words.empty()) throw std::runtime_error(std::format("{}: empty PLY face row {}", path.string(), face));
                const std::size_t vertex_per_face = parse_ascii_size(words.at(0), std::format("{} face {} vertex count", path.string(), face));
                if (vertex_per_face != 3u && vertex_per_face != 4u) throw std::runtime_error(std::format("{}: PLY face {} has {} vertices; only triangles and quads are supported", path.string(), face, vertex_per_face));
                if (words.size() < vertex_per_face + 1u) throw std::runtime_error(std::format("{}: PLY face row {} has too few indices", path.string(), face));
                const std::uint32_t i0 = parse_ascii_u32(words.at(1), std::format("{} face {} index 0", path.string(), face));
                const std::uint32_t i1 = parse_ascii_u32(words.at(2), std::format("{} face {} index 1", path.string(), face));
                const std::uint32_t i2 = parse_ascii_u32(words.at(3), std::format("{} face {} index 2", path.string(), face));
                if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) throw std::runtime_error(std::format("{}: PLY face {} has an out-of-range vertex index", path.string(), face));
                mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
                if (vertex_per_face == 4u) {
                    const std::uint32_t i3 = parse_ascii_u32(words.at(4), std::format("{} face {} index 3", path.string(), face));
                    if (i3 >= vertex_count) throw std::runtime_error(std::format("{}: PLY face {} has an out-of-range vertex index", path.string(), face));
                    mesh.indices.insert(mesh.indices.end(), {i0, i2, i3});
                }
            }
            if (input.bad()) throw std::runtime_error(std::format("{}: PLY read failed", path.string()));
            if (!has_normals) generate_mesh_normals(mesh, path.string());
            return mesh;
        }

        [[nodiscard]] std::filesystem::path resolve_shape_asset_path(const Scene::Shape& shape, const std::string& value) {
            std::filesystem::path path{value};
            if (path.is_absolute()) return path;
            if (shape.entity.source.filename.empty()) throw std::runtime_error(std::format("PBRT preview shape \"{}\" references relative asset \"{}\" without a source filename", shape.name, value));
            return std::filesystem::path{shape.entity.source.filename}.parent_path() / path;
        }

        void validate_positive_float(const float value, const std::string_view context) {
            if (!std::isfinite(value) || value <= 0.0f) throw std::runtime_error(std::format("{} must be finite and positive", context));
        }

        void validate_finite_float(const float value, const std::string_view context) {
            if (!std::isfinite(value)) throw std::runtime_error(std::format("{} must be finite", context));
        }

        [[nodiscard]] std::set<std::string> referenced_shape_material_names(const Scene::ResolvedScene& scene) {
            std::set<std::string> names{};
            for (const Scene::Shape& shape : scene.shapes) {
                if (shape.material_name.empty()) throw std::runtime_error("PBRT preview shape references an empty material name");
                names.insert(shape.material_name);
            }
            for (const Scene::ObjectDefinition& definition : scene.object_definitions) {
                for (const Scene::Shape& shape : definition.shapes) {
                    if (shape.material_name.empty()) throw std::runtime_error(std::format("PBRT preview object definition \"{}\" shape references an empty material name", definition.name));
                    names.insert(shape.material_name);
                }
            }
            return names;
        }

        [[nodiscard]] const Scene::Material& material_by_name(const Scene::ResolvedScene& scene, const std::string& name, const std::string_view context) {
            for (const Scene::Material& material : scene.materials)
                if (material.name == name) return material;
            throw std::runtime_error(std::format("{} references unknown material \"{}\"", context, name));
        }

        [[nodiscard]] std::vector<std::string> optional_string_array_value(const Scene::Entity& entity, const std::string_view name, const std::string_view context) {
            for (const Scene::Parameter& parameter : entity.parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = std::get_if<std::vector<std::string>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain string values", context, name));
                for (const std::string& value : *values)
                    if (value.empty()) throw std::runtime_error(std::format("{} parameter \"{}\" contains an empty string", context, name));
                return *values;
            }
            return {};
        }

        struct TextureLookup {
            std::map<std::string, const Scene::Texture*> float_textures{};
            std::map<std::string, const Scene::Texture*> spectrum_textures{};
        };

        [[nodiscard]] TextureLookup make_texture_lookup(const Scene::ResolvedScene& scene) {
            TextureLookup lookup{};
            for (const Scene::Texture& texture : scene.textures) {
                if (texture.name.empty()) throw std::runtime_error("PBRT preview texture name must not be empty");
                if (texture.kind == "float") {
                    if (!lookup.float_textures.emplace(texture.name, &texture).second) throw std::runtime_error(std::format("PBRT preview float texture \"{}\" is duplicated", texture.name));
                } else if (texture.kind == "spectrum") {
                    if (!lookup.spectrum_textures.emplace(texture.name, &texture).second) throw std::runtime_error(std::format("PBRT preview spectrum texture \"{}\" is duplicated", texture.name));
                } else {
                    throw std::runtime_error(std::format("PBRT preview texture \"{}\" has unsupported kind \"{}\"", texture.name, texture.kind));
                }
            }
            return lookup;
        }

        [[nodiscard]] const Scene::Texture& find_texture(const std::map<std::string, const Scene::Texture*>& textures, const std::string& name, const std::string_view kind, const std::string_view context) {
            const std::map<std::string, const Scene::Texture*>::const_iterator iter = textures.find(name);
            if (iter == textures.end()) throw std::runtime_error(std::format("{} references unknown {} texture \"{}\"", context, kind, name));
            return *iter->second;
        }

        [[nodiscard]] Vector3 clamp_color(const Vector3 color) {
            return Vector3{
                std::clamp(color.x, 0.0f, 1.0f),
                std::clamp(color.y, 0.0f, 1.0f),
                std::clamp(color.z, 0.0f, 1.0f),
            };
        }

        [[nodiscard]] Vector3 multiply_color(const Vector3 left, const Vector3 right) {
            return Vector3{left.x * right.x, left.y * right.y, left.z * right.z};
        }

        [[nodiscard]] float preview_scalar(const Vector3 color) {
            return std::clamp((color.x + color.y + color.z) / 3.0f, 0.0f, 1.0f);
        }

        [[nodiscard]] Vector3 optional_spectrum_value(const Scene::Entity& entity, const std::string_view name, const Vector3 default_value, const std::string_view context) {
            const std::vector<float>* values = optional_float_values(entity, "rgb", name);
            if (values == nullptr) values = optional_float_values(entity, "spectrum", name);
            if (values == nullptr) return default_value;
            if (values->size() == 1u) return Vector3{values->front(), values->front(), values->front()};
            if (values->size() != 3u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain one or three spectrum values", context, name));
            return Vector3{values->at(0), values->at(1), values->at(2)};
        }

        [[nodiscard]] std::filesystem::path resolve_texture_asset_path(const Scene::Texture& texture, const std::string& value) {
            std::filesystem::path path{value};
            if (path.is_absolute()) return path;
            if (texture.entity.source.filename.empty()) throw std::runtime_error(std::format("PBRT preview texture \"{}\" references relative asset \"{}\" without a source filename", texture.name, value));
            return std::filesystem::path{texture.entity.source.filename}.parent_path() / path;
        }

        [[nodiscard]] std::filesystem::path resolve_entity_asset_path(const Scene::Entity& entity, const std::string& value, const std::string_view context) {
            std::filesystem::path path{value};
            if (path.is_absolute()) return path;
            if (entity.source.filename.empty()) throw std::runtime_error(std::format("{} references relative asset \"{}\" without a source filename", context, value));
            return std::filesystem::path{entity.source.filename}.parent_path() / path;
        }

        [[nodiscard]] std::string lowercase_preview_string(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] std::string next_ppm_token(std::istream& input, const std::filesystem::path& path) {
            std::string token{};
            while (input >> token) {
                if (!token.empty() && token.front() == '#') {
                    std::string ignored{};
                    std::getline(input, ignored);
                    continue;
                }
                return token;
            }
            throw std::runtime_error(std::format("{}: unexpected end of PPM preview image", path.string()));
        }

        [[nodiscard]] float parse_ppm_float_token(const std::string& token, const std::filesystem::path& path) {
            float value{};
            const char* begin = token.data();
            const char* end = begin + token.size();
            const std::from_chars_result result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) throw std::runtime_error(std::format("{}: invalid PPM numeric token \"{}\"", path.string(), token));
            return value;
        }

        [[nodiscard]] Vector3 read_ppm_average_color(const std::filesystem::path& path, const std::string_view context) {
            std::ifstream input{path};
            if (!input) throw std::runtime_error(std::format("{} references unreadable P3 PPM preview image \"{}\"", context, path.string()));
            if (next_ppm_token(input, path) != "P3") throw std::runtime_error(std::format("{} image \"{}\" must be an ASCII P3 PPM for rasterizer preview sampling", context, path.string()));
            const float width_float = parse_ppm_float_token(next_ppm_token(input, path), path);
            const float height_float = parse_ppm_float_token(next_ppm_token(input, path), path);
            const float max_value = parse_ppm_float_token(next_ppm_token(input, path), path);
            if (width_float <= 0.0f || height_float <= 0.0f || max_value <= 0.0f) throw std::runtime_error(std::format("{} image \"{}\" has invalid PPM dimensions or max value", context, path.string()));
            const std::size_t width = static_cast<std::size_t>(width_float);
            const std::size_t height = static_cast<std::size_t>(height_float);
            if (static_cast<float>(width) != width_float || static_cast<float>(height) != height_float) throw std::runtime_error(std::format("{} image \"{}\" has non-integer PPM dimensions", context, path.string()));
            Vector3 sum{};
            const std::size_t pixel_count = width * height;
            for (std::size_t index = 0u; index < pixel_count; ++index) {
                const float r = parse_ppm_float_token(next_ppm_token(input, path), path) / max_value;
                const float g = parse_ppm_float_token(next_ppm_token(input, path), path) / max_value;
                const float b = parse_ppm_float_token(next_ppm_token(input, path), path) / max_value;
                sum += Vector3{r, g, b};
            }
            return clamp_color(sum / static_cast<float>(pixel_count));
        }

        [[nodiscard]] Vector3 read_png_average_color(const std::filesystem::path& path, const std::string_view context) {
            std::vector<unsigned char> pixels{};
            unsigned width{};
            unsigned height{};
            const unsigned error = lodepng::decode(pixels, width, height, path.string());
            if (error != 0u) throw std::runtime_error(std::format("{} image \"{}\" PNG decode failed: {}", context, path.string(), lodepng_error_text(error)));
            if (width == 0u || height == 0u) throw std::runtime_error(std::format("{} image \"{}\" has zero PNG dimensions", context, path.string()));
            const std::size_t pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            if (pixels.size() != pixel_count * 4u) throw std::runtime_error(std::format("{} image \"{}\" decoded to an unexpected PNG byte count", context, path.string()));
            Vector3 sum{};
            for (std::size_t index = 0u; index < pixel_count; ++index) {
                sum += Vector3{
                    static_cast<float>(pixels.at(index * 4u)) / 255.0f,
                    static_cast<float>(pixels.at(index * 4u + 1u)) / 255.0f,
                    static_cast<float>(pixels.at(index * 4u + 2u)) / 255.0f,
                };
            }
            return clamp_color(sum / static_cast<float>(pixel_count));
        }

        [[nodiscard]] Vector3 read_preview_image_average_color(const std::filesystem::path& path, const std::string_view context) {
            const std::string extension = lowercase_preview_string(path.extension().string());
            if (extension == ".ppm") return read_ppm_average_color(path, context);
            if (extension == ".png") return read_png_average_color(path, context);
            throw std::runtime_error(std::format("{} image \"{}\" has unsupported rasterizer preview image format \"{}\"", context, path.string(), extension));
        }

        void require_texture_acyclic(const std::vector<std::string>& stack, const std::string& name, const std::string_view kind, const std::string_view context) {
            for (const std::string& active : stack) {
                if (active == name) throw std::runtime_error(std::format("{} has a recursive {} texture reference through \"{}\"", context, kind, name));
            }
        }

        [[nodiscard]] float preview_float_texture_value(const TextureLookup& textures, const std::string& name, std::vector<std::string>& stack);
        [[nodiscard]] Vector3 preview_spectrum_texture_value(const TextureLookup& textures, const std::string& name, std::vector<std::string>& stack);

        [[nodiscard]] float preview_float_parameter(const TextureLookup& textures, const Scene::Entity& entity, const std::string_view name, const float default_value, const std::string_view context, std::vector<std::string>& stack) {
            const std::string texture_name = optional_texture_reference_value(entity, name);
            if (!texture_name.empty()) return preview_float_texture_value(textures, texture_name, stack);
            return optional_one_float_value(entity, name, default_value);
        }

        [[nodiscard]] Vector3 preview_spectrum_parameter(const TextureLookup& textures, const Scene::Entity& entity, const std::string_view name, const Vector3 default_value, const std::string_view context, std::vector<std::string>& stack) {
            const std::string texture_name = optional_texture_reference_value(entity, name);
            if (!texture_name.empty()) return preview_spectrum_texture_value(textures, texture_name, stack);
            return optional_spectrum_value(entity, name, default_value, context);
        }

        [[nodiscard]] float preview_float_texture_value(const TextureLookup& textures, const std::string& name, std::vector<std::string>& stack) {
            require_texture_acyclic(stack, name, "float", "PBRT preview texture graph");
            const Scene::Texture& texture = find_texture(textures.float_textures, name, "float", "PBRT preview texture graph");
            const std::string context = std::format("PBRT preview float texture \"{}\"", name);
            stack.push_back(name);
            float value = 1.0f;
            if (texture.entity.type == "constant") {
                value = optional_one_float_value(texture.entity, "value", 1.0f);
            } else if (texture.entity.type == "scale") {
                value = preview_float_parameter(textures, texture.entity, "tex", 1.0f, context, stack) * preview_float_parameter(textures, texture.entity, "scale", 1.0f, context, stack);
            } else if (texture.entity.type == "mix") {
                const float amount = std::clamp(preview_float_parameter(textures, texture.entity, "amount", 0.5f, context, stack), 0.0f, 1.0f);
                value = preview_float_parameter(textures, texture.entity, "tex1", 0.0f, context, stack) * (1.0f - amount) + preview_float_parameter(textures, texture.entity, "tex2", 1.0f, context, stack) * amount;
            } else if (texture.entity.type == "directionmix") {
                const Vector3 dir = optional_vector3_value(texture.entity, "dir", Vector3{0.0f, 1.0f, 0.0f});
                const float amount = std::clamp(0.5f + 0.5f * normalize(dir, context).y, 0.0f, 1.0f);
                value = preview_float_parameter(textures, texture.entity, "tex1", 0.0f, context, stack) * (1.0f - amount) + preview_float_parameter(textures, texture.entity, "tex2", 1.0f, context, stack) * amount;
            } else if (texture.entity.type == "bilerp") {
                const float s = 0.37f;
                const float t = 0.61f;
                const float v00 = optional_one_float_value(texture.entity, "v00", 0.0f);
                const float v01 = optional_one_float_value(texture.entity, "v01", 1.0f);
                const float v10 = optional_one_float_value(texture.entity, "v10", 0.0f);
                const float v11 = optional_one_float_value(texture.entity, "v11", 1.0f);
                value = v00 * (1.0f - s) * (1.0f - t) + v10 * s * (1.0f - t) + v01 * (1.0f - s) * t + v11 * s * t;
            } else if (texture.entity.type == "imagemap") {
                const std::filesystem::path path = resolve_texture_asset_path(texture, required_string_value(texture.entity, "filename", context));
                value = preview_scalar(read_preview_image_average_color(path, context)) * optional_one_float_value(texture.entity, "scale", 1.0f);
                if (optional_one_int_value(texture.entity, "invert", 0, context) != 0) value = 1.0f - value;
            } else if (texture.entity.type == "checkerboard") {
                const int dimension = optional_one_int_value(texture.entity, "dimension", 2, context);
                if (dimension != 2 && dimension != 3) throw std::runtime_error(std::format("{} checkerboard dimension must be 2 or 3", context));
                const float selector = dimension == 2 ? 0.0f : 1.0f;
                value = selector < 0.5f ? preview_float_parameter(textures, texture.entity, "tex1", 1.0f, context, stack) : preview_float_parameter(textures, texture.entity, "tex2", 0.0f, context, stack);
            } else if (texture.entity.type == "dots") {
                value = preview_float_parameter(textures, texture.entity, "inside", 1.0f, context, stack);
            } else if (texture.entity.type == "fbm") {
                value = std::clamp(0.38f + 0.045f * static_cast<float>(optional_one_int_value(texture.entity, "octaves", 8, context)) + 0.18f * optional_one_float_value(texture.entity, "roughness", 0.5f), 0.0f, 1.0f);
            } else if (texture.entity.type == "wrinkled") {
                value = std::clamp(0.62f + 0.025f * static_cast<float>(optional_one_int_value(texture.entity, "octaves", 8, context)) + 0.16f * optional_one_float_value(texture.entity, "roughness", 0.5f), 0.0f, 1.0f);
            } else if (texture.entity.type == "windy") {
                value = 0.54f;
            } else {
                throw std::runtime_error(std::format("{} uses unsupported float texture type \"{}\"", context, texture.entity.type));
            }
            stack.pop_back();
            return std::clamp(value, 0.0f, 1.0f);
        }

        [[nodiscard]] Vector3 preview_spectrum_texture_value(const TextureLookup& textures, const std::string& name, std::vector<std::string>& stack) {
            require_texture_acyclic(stack, name, "spectrum", "PBRT preview texture graph");
            const Scene::Texture& texture = find_texture(textures.spectrum_textures, name, "spectrum", "PBRT preview texture graph");
            const std::string context = std::format("PBRT preview spectrum texture \"{}\"", name);
            stack.push_back(name);
            Vector3 value{1.0f, 1.0f, 1.0f};
            if (texture.entity.type == "constant") {
                value = optional_spectrum_value(texture.entity, "value", Vector3{1.0f, 1.0f, 1.0f}, context);
            } else if (texture.entity.type == "scale") {
                value = preview_spectrum_parameter(textures, texture.entity, "tex", Vector3{1.0f, 1.0f, 1.0f}, context, stack) * preview_float_parameter(textures, texture.entity, "scale", 1.0f, context, stack);
            } else if (texture.entity.type == "mix") {
                const float amount = std::clamp(preview_float_parameter(textures, texture.entity, "amount", 0.5f, context, stack), 0.0f, 1.0f);
                value = preview_spectrum_parameter(textures, texture.entity, "tex1", Vector3{}, context, stack) * (1.0f - amount) + preview_spectrum_parameter(textures, texture.entity, "tex2", Vector3{1.0f, 1.0f, 1.0f}, context, stack) * amount;
            } else if (texture.entity.type == "directionmix") {
                const Vector3 dir = optional_vector3_value(texture.entity, "dir", Vector3{0.0f, 1.0f, 0.0f});
                const float amount = std::clamp(0.5f + 0.5f * normalize(dir, context).y, 0.0f, 1.0f);
                value = preview_spectrum_parameter(textures, texture.entity, "tex1", Vector3{}, context, stack) * (1.0f - amount) + preview_spectrum_parameter(textures, texture.entity, "tex2", Vector3{1.0f, 1.0f, 1.0f}, context, stack) * amount;
            } else if (texture.entity.type == "bilerp") {
                const float s = 0.37f;
                const float t = 0.61f;
                const Vector3 v00 = optional_spectrum_value(texture.entity, "v00", Vector3{}, context);
                const Vector3 v01 = optional_spectrum_value(texture.entity, "v01", Vector3{1.0f, 1.0f, 1.0f}, context);
                const Vector3 v10 = optional_spectrum_value(texture.entity, "v10", Vector3{}, context);
                const Vector3 v11 = optional_spectrum_value(texture.entity, "v11", Vector3{1.0f, 1.0f, 1.0f}, context);
                value = v00 * ((1.0f - s) * (1.0f - t)) + v10 * (s * (1.0f - t)) + v01 * ((1.0f - s) * t) + v11 * (s * t);
            } else if (texture.entity.type == "imagemap") {
                const std::filesystem::path path = resolve_texture_asset_path(texture, required_string_value(texture.entity, "filename", context));
                value = read_preview_image_average_color(path, context) * optional_one_float_value(texture.entity, "scale", 1.0f);
                if (optional_one_int_value(texture.entity, "invert", 0, context) != 0) value = Vector3{1.0f - value.x, 1.0f - value.y, 1.0f - value.z};
            } else if (texture.entity.type == "checkerboard") {
                const int dimension = optional_one_int_value(texture.entity, "dimension", 2, context);
                if (dimension != 2 && dimension != 3) throw std::runtime_error(std::format("{} checkerboard dimension must be 2 or 3", context));
                value = dimension == 2 ? preview_spectrum_parameter(textures, texture.entity, "tex1", Vector3{1.0f, 1.0f, 1.0f}, context, stack) : preview_spectrum_parameter(textures, texture.entity, "tex2", Vector3{}, context, stack);
            } else if (texture.entity.type == "dots") {
                value = preview_spectrum_parameter(textures, texture.entity, "inside", Vector3{1.0f, 1.0f, 1.0f}, context, stack);
            } else if (texture.entity.type == "marble") {
                const float variation = optional_one_float_value(texture.entity, "variation", 0.2f);
                const float scale = optional_one_float_value(texture.entity, "scale", 1.0f);
                const float amount = std::clamp(0.5f + 0.5f * std::sin(0.61f * scale + variation), 0.0f, 1.0f);
                value = Vector3{0.30f, 0.30f, 0.45f} * (1.0f - amount) + Vector3{0.82f, 0.80f, 0.78f} * amount;
            } else {
                throw std::runtime_error(std::format("{} uses unsupported spectrum texture type \"{}\"", context, texture.entity.type));
            }
            stack.pop_back();
            return clamp_color(value);
        }

        [[nodiscard]] Vector3 preview_material_color(const TextureLookup& textures, const Scene::Entity& entity) {
            std::vector<std::string> texture_stack{};
            if (entity.type == "diffuse" || entity.type == "coateddiffuse") return preview_spectrum_parameter(textures, entity, "reflectance", Vector3{0.8f, 0.8f, 0.8f}, "PBRT preview material", texture_stack);
            if (entity.type == "conductor" || entity.type == "coatedconductor") return preview_spectrum_parameter(textures, entity, "reflectance", Vector3{0.9f, 0.72f, 0.38f}, "PBRT preview material", texture_stack);
            if (entity.type == "diffusetransmission") {
                const Vector3 reflectance = preview_spectrum_parameter(textures, entity, "reflectance", Vector3{0.35f, 0.45f, 0.65f}, "PBRT preview material", texture_stack);
                const Vector3 transmittance = preview_spectrum_parameter(textures, entity, "transmittance", Vector3{0.35f, 0.55f, 0.80f}, "PBRT preview material", texture_stack);
                return (reflectance + transmittance) * 0.5f;
            }
            if (entity.type == "subsurface") return preview_spectrum_parameter(textures, entity, "reflectance", Vector3{0.86f, 0.48f, 0.38f}, "PBRT preview material", texture_stack);
            if (entity.type == "measured") return Vector3{0.78f, 0.76f, 0.70f};
            if (entity.type == "dielectric" || entity.type == "thindielectric") return Vector3{0.82f, 0.9f, 1.0f};
            if (entity.type == "hair") return preview_spectrum_parameter(textures, entity, "reflectance", Vector3{0.46f, 0.24f, 0.12f}, "PBRT preview material", texture_stack);
            return Vector3{0.8f, 0.8f, 0.8f};
        }

        [[nodiscard]] std::map<std::string, std::size_t> append_materials(const Scene::ResolvedScene& scene, const std::set<std::string>& referenced_material_names, Scene::Document& document) {
            const TextureLookup texture_lookup = make_texture_lookup(scene);
            std::map<std::string, std::size_t> material_indices{};
            for (const Scene::Material& material : scene.materials) {
                if (!referenced_material_names.contains(material.name)) continue;
                if (material.name.empty()) throw std::runtime_error("PBRT preview material name must not be empty");
                Scene::PreviewMaterial preview_material{
                    .name                = material.name,
                    .surface_kind        = Scene::PreviewSurfaceKind::LitSurface,
                    .base_color          = Vector4{0.8f, 0.8f, 0.8f, 1.0f},
                    .roughness           = 0.72f,
                    .pathtracer_material = material.entity,
                };
                if (material.entity.type == "diffuse") {
                    std::vector<std::string> texture_stack{};
                    const Vector3 reflectance = preview_spectrum_parameter(texture_lookup, material.entity, "reflectance", Vector3{0.8f, 0.8f, 0.8f}, std::format("PBRT preview material \"{}\"", material.name), texture_stack);
                    preview_material.base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                } else if (material.entity.type == "coateddiffuse") {
                    std::vector<std::string> texture_stack{};
                    const Vector3 reflectance = preview_spectrum_parameter(texture_lookup, material.entity, "reflectance", Vector3{0.8f, 0.8f, 0.8f}, std::format("PBRT preview material \"{}\"", material.name), texture_stack);
                    preview_material.base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.roughness = std::clamp(preview_float_parameter(texture_lookup, material.entity, "roughness", 0.35f, std::format("PBRT preview material \"{}\"", material.name), texture_stack), 0.02f, 1.0f);
                    preview_material.roughness_texture = optional_texture_reference_value(material.entity, "roughness");
                } else if (material.entity.type == "diffusetransmission") {
                    const Vector3 color = preview_material_color(texture_lookup, material.entity);
                    preview_material.base_color = Vector4{color.x, color.y, color.z, 0.62f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.alpha_mode = Scene::PreviewAlphaMode::Blend;
                    preview_material.roughness = 0.58f;
                } else if (material.entity.type == "conductor") {
                    std::vector<std::string> texture_stack{};
                    const Vector3 reflectance = preview_spectrum_parameter(texture_lookup, material.entity, "reflectance", Vector3{0.9f, 0.82f, 0.65f}, std::format("PBRT preview material \"{}\"", material.name), texture_stack);
                    preview_material.base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.roughness = std::clamp(preview_float_parameter(texture_lookup, material.entity, "roughness", 0.28f, std::format("PBRT preview material \"{}\"", material.name), texture_stack), 0.02f, 1.0f);
                    preview_material.roughness_texture = optional_texture_reference_value(material.entity, "roughness");
                    preview_material.metallic = 1.0f;
                } else if (material.entity.type == "coatedconductor") {
                    std::vector<std::string> texture_stack{};
                    const Vector3 reflectance = preview_spectrum_parameter(texture_lookup, material.entity, "reflectance", Vector3{0.92f, 0.70f, 0.34f}, std::format("PBRT preview material \"{}\"", material.name), texture_stack);
                    preview_material.base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.roughness = std::clamp(preview_float_parameter(texture_lookup, material.entity, "conductor.roughness", 0.16f, std::format("PBRT preview material \"{}\"", material.name), texture_stack), 0.02f, 1.0f);
                    preview_material.roughness_texture = optional_texture_reference_value(material.entity, "conductor.roughness");
                    preview_material.metallic = 1.0f;
                } else if (material.entity.type == "dielectric") {
                    preview_material.base_color = Vector4{0.82f, 0.9f, 1.0f, 0.42f};
                    preview_material.alpha_mode = Scene::PreviewAlphaMode::Blend;
                    preview_material.roughness = 0.05f;
                } else if (material.entity.type == "thindielectric") {
                    preview_material.base_color = Vector4{0.88f, 0.94f, 1.0f, 0.28f};
                    preview_material.alpha_mode = Scene::PreviewAlphaMode::Blend;
                    preview_material.roughness = 0.02f;
                } else if (material.entity.type == "hair") {
                    std::vector<std::string> texture_stack{};
                    const Vector3 reflectance = preview_spectrum_parameter(texture_lookup, material.entity, "reflectance", Vector3{0.46f, 0.24f, 0.12f}, std::format("PBRT preview material \"{}\"", material.name), texture_stack);
                    preview_material.base_color = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.roughness = std::clamp(optional_one_float_value(material.entity, "beta_m", 0.35f), 0.08f, 1.0f);
                } else if (material.entity.type == "subsurface") {
                    const Vector3 color = preview_material_color(texture_lookup, material.entity);
                    preview_material.base_color = Vector4{color.x, color.y, color.z, 0.82f};
                    preview_material.base_color_texture = optional_texture_reference_value(material.entity, "reflectance");
                    preview_material.alpha_mode = Scene::PreviewAlphaMode::Blend;
                    preview_material.roughness = std::clamp(optional_one_float_value(material.entity, "roughness", 0.42f), 0.08f, 1.0f);
                } else if (material.entity.type == "measured") {
                    const Vector3 color = preview_material_color(texture_lookup, material.entity);
                    preview_material.base_color = Vector4{color.x, color.y, color.z, 1.0f};
                    preview_material.roughness = 0.46f;
                } else if (material.entity.type == "mix") {
                    const std::vector<std::string> material_names = optional_string_array_value(material.entity, "materials", std::format("PBRT preview material \"{}\"", material.name));
                    if (material_names.size() != 2u) throw std::runtime_error(std::format("PBRT preview material \"{}\" mix requires exactly two material names", material.name));
                    const Vector3 first = preview_material_color(texture_lookup, material_by_name(scene, material_names.at(0), std::format("PBRT preview material \"{}\"", material.name)).entity);
                    const Vector3 second = preview_material_color(texture_lookup, material_by_name(scene, material_names.at(1), std::format("PBRT preview material \"{}\"", material.name)).entity);
                    std::vector<std::string> texture_stack{};
                    const float amount = std::clamp(preview_float_parameter(texture_lookup, material.entity, "amount", 0.5f, std::format("PBRT preview material \"{}\"", material.name), texture_stack), 0.0f, 1.0f);
                    const Vector3 color = first * (1.0f - amount) + second * amount;
                    preview_material.base_color = Vector4{color.x, color.y, color.z, 1.0f};
                    preview_material.roughness = 0.42f;
                } else if (material.entity.type == "interface" || material.entity.type == "none") {
                    preview_material.surface_kind = Scene::PreviewSurfaceKind::UnlitSurface;
                    preview_material.alpha_mode = Scene::PreviewAlphaMode::Blend;
                    preview_material.base_color = Vector4{0.62f, 0.7f, 0.78f, 0.18f};
                    preview_material.roughness = 1.0f;
                } else {
                    throw std::runtime_error(std::format("PBRT preview material \"{}\" uses unsupported type \"{}\"", material.name, material.entity.type));
                }
                const bool inserted = material_indices.emplace(material.name, document.materials.size()).second;
                if (!inserted) throw std::runtime_error(std::format("PBRT preview material \"{}\" is duplicated", material.name));
                document.materials.push_back(std::move(preview_material));
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

        [[nodiscard]] const Scene::Medium& medium_by_name(const Scene::ResolvedScene& scene, const std::string& name, const std::string_view context) {
            for (const Scene::Medium& medium : scene.media)
                if (medium.name == name) return medium;
            throw std::runtime_error(std::format("{} references unknown medium \"{}\"", context, name));
        }

        [[nodiscard]] Vector3 medium_preview_color(const Scene::Medium& medium) {
            if (medium.entity.type == "homogeneous") return optional_rgb_value(medium.entity, "sigma_s", Vector3{0.62f, 0.74f, 0.95f});
            if (medium.entity.type == "uniformgrid") return Vector3{0.52f, 0.72f, 0.46f};
            if (medium.entity.type == "rgbgrid") return Vector3{0.84f, 0.56f, 0.34f};
            if (medium.entity.type == "cloud") return Vector3{0.68f, 0.70f, 0.74f};
            if (medium.entity.type == "nanovdb") return Vector3{0.58f, 0.50f, 0.86f};
            throw std::runtime_error(std::format("PBRT preview medium \"{}\" uses unsupported type \"{}\"", medium.name, medium.entity.type));
        }

        void apply_medium_boundary_material(const Scene::ResolvedScene& scene, const Scene::Shape& shape, const std::size_t shape_index, Scene::Document& document, const std::map<std::string, std::size_t>& material_indices) {
            if (shape.medium_interface.inside.empty() && shape.medium_interface.outside.empty()) return;
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            const std::string& medium_name = shape.medium_interface.inside.empty() ? shape.medium_interface.outside : shape.medium_interface.inside;
            const Scene::Medium& medium = medium_by_name(scene, medium_name, context);
            Scene::PreviewMaterial& material = material_for_name(document, material_indices, shape.material_name);
            if (material.surface_kind != Scene::PreviewSurfaceKind::UnlitSurface) return;
            const Vector3 color = clamp_color(medium_preview_color(medium));
            material.alpha_mode = Scene::PreviewAlphaMode::Blend;
            material.base_color = Vector4{color.x, color.y, color.z, 0.24f};
            material.roughness = 1.0f;
        }

        void apply_area_light_material(const Scene::Shape& shape, const std::size_t shape_index, Scene::Document& document, const std::map<std::string, std::size_t>& material_indices) {
            if (!shape.area_light.has_value()) return;
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.area_light->entity.type != "diffuse") throw std::runtime_error(std::format("{} uses unsupported area light type \"{}\"", context, shape.area_light->entity.type));
            Scene::PreviewMaterial& material = material_for_name(document, material_indices, shape.material_name);
            const Vector3 radiance = required_rgb_value(shape.area_light->entity, "L", context);
            const float scale = optional_one_float_value(shape.area_light->entity, "scale", 1.0f);
            if (scale < 0.0f) throw std::runtime_error(std::format("{} area light scale must be non-negative", context));
            const bool already_emissive = material.surface_kind == Scene::PreviewSurfaceKind::EmissiveSurface;
            if (already_emissive && (material.emission_color.x != radiance.x || material.emission_color.y != radiance.y || material.emission_color.z != radiance.z || material.emission_strength != scale)) throw std::runtime_error(std::format("PBRT preview material \"{}\" is reused by area lights with different emission", material.name));
            material.surface_kind      = Scene::PreviewSurfaceKind::EmissiveSurface;
            material.emission_color    = radiance;
            material.emission_strength = scale;
        }

        [[nodiscard]] Scene::Mesh make_mesh(const std::string_view object_source_prefix_value, const Scene::Shape& shape, const std::size_t shape_index, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            require_static_transform(shape.transform, context);
            if (shape.reverse_orientation) throw std::runtime_error(std::format("{} uses ReverseOrientation, which is not supported by the PBRT preview scene loader", context));
            if (!material_indices.contains(shape.material_name)) throw std::runtime_error(std::format("{} references unknown material \"{}\"", context, shape.material_name));
            const std::string object_name = make_shape_object_name(object_source_prefix_value, shape_index);
            Scene::Mesh mesh{
                .name         = object_name,
                .material_name = shape.material_name,
                .dynamic      = false,
                .source       = shape.entity.source,
            };
            const auto append_vertex = [&mesh, &shape, &bounds](const Vector3 point, const Vector3 normal) {
                const Vector3 transformed_point = transform_point(shape.transform.start, point);
                mesh.positions.push_back(transformed_point);
                mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                include_point(bounds, transformed_point);
            };
            const auto append_quad = [&mesh](const std::uint32_t a, const std::uint32_t b, const std::uint32_t c, const std::uint32_t d) {
                mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
            };
            if (shape.entity.type == "plymesh") {
                const std::string filename = required_string_value(shape.entity, "filename", context);
                const std::filesystem::path path = resolve_shape_asset_path(shape, filename);
                Scene::Mesh ply_mesh = read_ascii_ply_mesh(path, object_name, shape.material_name, shape.entity.source);
                if (ply_mesh.positions.size() != ply_mesh.normals.size()) throw std::runtime_error(std::format("{} PLY normal count does not match position count", context));
                mesh.positions.reserve(ply_mesh.positions.size());
                mesh.normals.reserve(ply_mesh.normals.size());
                mesh.uvs.reserve(ply_mesh.uvs.size());
                for (std::size_t vertex_index = 0u; vertex_index < ply_mesh.positions.size(); ++vertex_index) {
                    append_vertex(ply_mesh.positions.at(vertex_index), ply_mesh.normals.at(vertex_index));
                    if (!ply_mesh.uvs.empty()) mesh.uvs.push_back(ply_mesh.uvs.at(vertex_index));
                }
                mesh.indices = std::move(ply_mesh.indices);
                return mesh;
            }
            if (shape.entity.type == "bilinearmesh") {
                const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
                if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("{} has invalid point3 P data", context));
                const std::size_t vertex_count = positions.size() / 3u;
                const std::vector<float>* normals = optional_float_values(shape.entity, "normal", "N");
                const std::vector<float>* uvs = optional_float_values(shape.entity, "point2", "uv");
                if (normals != nullptr && normals->size() != positions.size()) throw std::runtime_error(std::format("{} normal count does not match position count", context));
                if (uvs != nullptr && uvs->size() != vertex_count * 2u) throw std::runtime_error(std::format("{} uv count does not match position count", context));

                std::vector<int> default_patch_indices{};
                const std::vector<int>* patch_indices = optional_int_values(shape.entity, "indices");
                if (patch_indices == nullptr) {
                    if (vertex_count != 4u) throw std::runtime_error(std::format("{} requires \"integer indices\" unless exactly four control points are provided", context));
                    default_patch_indices = {0, 1, 2, 3};
                    patch_indices = &default_patch_indices;
                }
                if (patch_indices->empty() || patch_indices->size() % 4u != 0u) throw std::runtime_error(std::format("{} bilinearmesh indices must contain a non-empty multiple of four entries", context));

                mesh.positions.reserve(vertex_count);
                if (normals != nullptr) mesh.normals.reserve(vertex_count);
                if (uvs != nullptr) mesh.uvs.reserve(vertex_count);
                for (std::size_t vertex_index = 0u; vertex_index < vertex_count; ++vertex_index) {
                    const Vector3 point{positions.at(vertex_index * 3u), positions.at(vertex_index * 3u + 1u), positions.at(vertex_index * 3u + 2u)};
                    const Vector3 transformed_point = transform_point(shape.transform.start, point);
                    mesh.positions.push_back(transformed_point);
                    include_point(bounds, transformed_point);
                    if (normals != nullptr) {
                        const Vector3 normal{normals->at(vertex_index * 3u), normals->at(vertex_index * 3u + 1u), normals->at(vertex_index * 3u + 2u)};
                        mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                    }
                    if (uvs != nullptr) mesh.uvs.push_back(std::array<float, 2>{uvs->at(vertex_index * 2u), uvs->at(vertex_index * 2u + 1u)});
                }
                mesh.indices.reserve(patch_indices->size() / 4u * 6u);
                const auto checked_patch_index = [vertex_count, &context](const int index) {
                    if (index < 0) throw std::runtime_error(std::format("{} contains a negative bilinearmesh vertex index", context));
                    const std::uint32_t converted_index = static_cast<std::uint32_t>(index);
                    if (converted_index >= vertex_count) throw std::runtime_error(std::format("{} contains an out-of-range bilinearmesh vertex index", context));
                    return converted_index;
                };
                for (std::size_t index = 0u; index < patch_indices->size(); index += 4u) {
                    const std::uint32_t p00 = checked_patch_index(patch_indices->at(index));
                    const std::uint32_t p10 = checked_patch_index(patch_indices->at(index + 1u));
                    const std::uint32_t p01 = checked_patch_index(patch_indices->at(index + 2u));
                    const std::uint32_t p11 = checked_patch_index(patch_indices->at(index + 3u));
                    mesh.indices.insert(mesh.indices.end(), {p00, p10, p11, p00, p11, p01});
                }
                if (normals == nullptr) generate_mesh_normals(mesh, context);
                return mesh;
            }
            if (shape.entity.type == "loopsubdiv") {
                const int levels = optional_one_int_value(shape.entity, "levels", 3, context);
                if (levels < 0) throw std::runtime_error(std::format("{} loopsubdiv levels must be non-negative", context));
                const std::string scheme = optional_string_value(shape.entity, "scheme", "loop", context);
                if (scheme != "loop") throw std::runtime_error(std::format("{} only supports loopsubdiv scheme \"loop\", got \"{}\"", context, scheme));
                const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
                const std::vector<int>& indices = required_int_values(shape.entity, "indices", context);
                const std::vector<float>* normals = optional_float_values(shape.entity, "normal", "N");
                if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("{} has invalid point3 P data", context));
                if (indices.empty() || indices.size() % 3u != 0u) throw std::runtime_error(std::format("{} loopsubdiv preview indices must contain triangles", context));
                const std::size_t vertex_count = positions.size() / 3u;
                if (normals != nullptr && normals->size() != positions.size()) throw std::runtime_error(std::format("{} normal count does not match position count", context));

                mesh.positions.reserve(vertex_count);
                if (normals != nullptr) mesh.normals.reserve(vertex_count);
                for (std::size_t vertex_index = 0u; vertex_index < vertex_count; ++vertex_index) {
                    const Vector3 point{positions.at(vertex_index * 3u), positions.at(vertex_index * 3u + 1u), positions.at(vertex_index * 3u + 2u)};
                    const Vector3 transformed_point = transform_point(shape.transform.start, point);
                    mesh.positions.push_back(transformed_point);
                    include_point(bounds, transformed_point);
                    if (normals != nullptr) {
                        const Vector3 normal{normals->at(vertex_index * 3u), normals->at(vertex_index * 3u + 1u), normals->at(vertex_index * 3u + 2u)};
                        mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                    }
                }
                mesh.indices.reserve(indices.size());
                for (const int index : indices) {
                    if (index < 0) throw std::runtime_error(std::format("{} contains a negative loopsubdiv vertex index", context));
                    const std::uint32_t converted_index = static_cast<std::uint32_t>(index);
                    if (converted_index >= vertex_count) throw std::runtime_error(std::format("{} contains an out-of-range loopsubdiv vertex index", context));
                    mesh.indices.push_back(converted_index);
                }
                if (normals == nullptr) generate_mesh_normals(mesh, context);
                return mesh;
            }
            if (shape.entity.type == "curve") {
                const int degree = optional_one_int_value(shape.entity, "degree", 3, context);
                if (degree != 3) throw std::runtime_error(std::format("{} preview supports only cubic curve degree 3", context));
                const std::string basis = optional_string_value(shape.entity, "basis", "bezier", context);
                if (basis != "bezier") throw std::runtime_error(std::format("{} preview supports only bezier curve basis", context));
                const std::string curve_type = optional_string_value(shape.entity, "type", "flat", context);
                if (curve_type != "cylinder") throw std::runtime_error(std::format("{} preview supports only cylinder curve type", context));
                const float width = optional_one_float_value(shape.entity, "width", 0.02f);
                const float width0 = optional_one_float_value(shape.entity, "width0", width);
                const float width1 = optional_one_float_value(shape.entity, "width1", width);
                validate_positive_float(width0, std::format("{} curve width0", context));
                validate_positive_float(width1, std::format("{} curve width1", context));
                const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
                if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("{} has invalid point3 P data", context));
                const std::size_t control_point_count = positions.size() / 3u;
                if (control_point_count < 4u || (control_point_count - 4u) % 3u != 0u) throw std::runtime_error(std::format("{} cubic bezier curve requires 4 + 3n control points", context));
                std::vector<Vector3> control_points{};
                control_points.reserve(control_point_count);
                for (std::size_t point_index = 0u; point_index < control_point_count; ++point_index)
                    control_points.push_back(Vector3{positions.at(point_index * 3u), positions.at(point_index * 3u + 1u), positions.at(point_index * 3u + 2u)});

                constexpr std::uint32_t samples_per_segment = 16u;
                constexpr std::uint32_t radial_segments = 8u;
                const std::uint32_t segment_count = static_cast<std::uint32_t>((control_point_count - 1u) / 3u);
                const std::uint32_t ring_count = segment_count * samples_per_segment + 1u;
                mesh.positions.reserve(static_cast<std::size_t>(ring_count) * radial_segments);
                mesh.normals.reserve(mesh.positions.capacity());
                const auto bezier_point = [](const Vector3 p0, const Vector3 p1, const Vector3 p2, const Vector3 p3, const float t) {
                    const float omt = 1.0f - t;
                    return p0 * (omt * omt * omt) + p1 * (3.0f * omt * omt * t) + p2 * (3.0f * omt * t * t) + p3 * (t * t * t);
                };
                const auto bezier_tangent = [](const Vector3 p0, const Vector3 p1, const Vector3 p2, const Vector3 p3, const float t) {
                    const float omt = 1.0f - t;
                    return (p1 - p0) * (3.0f * omt * omt) + (p2 - p1) * (6.0f * omt * t) + (p3 - p2) * (3.0f * t * t);
                };
                const auto append_ring = [&](const Vector3 center_value, const Vector3 tangent_value, const float radius_value) {
                    const Vector3 tangent = normalize(tangent_value, std::format("{} curve tangent", context));
                    const Vector3 reference = std::abs(tangent.y) < 0.92f ? Vector3{0.0f, 1.0f, 0.0f} : Vector3{1.0f, 0.0f, 0.0f};
                    const Vector3 side = normalize(cross(reference, tangent), std::format("{} curve side", context));
                    const Vector3 up = normalize(cross(tangent, side), std::format("{} curve up", context));
                    for (std::uint32_t radial = 0u; radial < radial_segments; ++radial) {
                        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(radial) / static_cast<float>(radial_segments);
                        const Vector3 normal = normalize(side * std::cos(angle) + up * std::sin(angle), std::format("{} curve radial normal", context));
                        const Vector3 point = center_value + normal * radius_value;
                        const Vector3 transformed_point = transform_point(shape.transform.start, point);
                        mesh.positions.push_back(transformed_point);
                        mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                        include_point(bounds, transformed_point);
                    }
                };
                for (std::uint32_t segment = 0u; segment < segment_count; ++segment) {
                    const std::size_t control_offset = static_cast<std::size_t>(segment) * 3u;
                    const Vector3 p0 = control_points.at(control_offset);
                    const Vector3 p1 = control_points.at(control_offset + 1u);
                    const Vector3 p2 = control_points.at(control_offset + 2u);
                    const Vector3 p3 = control_points.at(control_offset + 3u);
                    for (std::uint32_t sample = 0u; sample < samples_per_segment; ++sample) {
                        const float local_t = static_cast<float>(sample) / static_cast<float>(samples_per_segment);
                        const float global_t = (static_cast<float>(segment) + local_t) / static_cast<float>(segment_count);
                        append_ring(bezier_point(p0, p1, p2, p3, local_t), bezier_tangent(p0, p1, p2, p3, local_t), std::lerp(width0, width1, global_t) * 0.5f);
                    }
                }
                {
                    const std::size_t control_offset = static_cast<std::size_t>(segment_count - 1u) * 3u;
                    const Vector3 p0 = control_points.at(control_offset);
                    const Vector3 p1 = control_points.at(control_offset + 1u);
                    const Vector3 p2 = control_points.at(control_offset + 2u);
                    const Vector3 p3 = control_points.at(control_offset + 3u);
                    append_ring(p3, bezier_tangent(p0, p1, p2, p3, 1.0f), width1 * 0.5f);
                }
                mesh.indices.reserve(static_cast<std::size_t>(ring_count - 1u) * radial_segments * 6u);
                for (std::uint32_t ring = 0u; ring + 1u < ring_count; ++ring) {
                    for (std::uint32_t radial = 0u; radial < radial_segments; ++radial) {
                        const std::uint32_t next_radial = (radial + 1u) % radial_segments;
                        const std::uint32_t current = ring * radial_segments + radial;
                        const std::uint32_t current_next = ring * radial_segments + next_radial;
                        const std::uint32_t next = (ring + 1u) * radial_segments + radial;
                        const std::uint32_t next_next = (ring + 1u) * radial_segments + next_radial;
                        mesh.indices.insert(mesh.indices.end(), {current, next, next_next, current, next_next, current_next});
                    }
                }
                return mesh;
            }
            if (shape.entity.type == "sphere") {
                const float radius_value = optional_one_float_value(shape.entity, "radius", 1.0f);
                validate_positive_float(radius_value, std::format("{} sphere radius", context));
                const float z_min = std::clamp(optional_one_float_value(shape.entity, "zmin", -radius_value), -radius_value, radius_value);
                const float z_max = std::clamp(optional_one_float_value(shape.entity, "zmax", radius_value), -radius_value, radius_value);
                if (z_min >= z_max) throw std::runtime_error(std::format("{} sphere zmin must be smaller than zmax", context));
                const float phi_max_degrees = std::clamp(optional_one_float_value(shape.entity, "phimax", 360.0f), 0.0f, 360.0f);
                if (phi_max_degrees <= 0.0f) throw std::runtime_error(std::format("{} sphere phimax must be positive", context));
                constexpr std::uint32_t latitude_segments = 32u;
                constexpr std::uint32_t longitude_segments = 64u;
                const std::uint32_t longitude_count = longitude_segments + 1u;
                const float theta_min = std::acos(std::clamp(z_max / radius_value, -1.0f, 1.0f));
                const float theta_max = std::acos(std::clamp(z_min / radius_value, -1.0f, 1.0f));
                const float phi_max = phi_max_degrees * std::numbers::pi_v<float> / 180.0f;
                mesh.positions.reserve(static_cast<std::size_t>(latitude_segments + 1u) * static_cast<std::size_t>(longitude_count));
                mesh.normals.reserve(mesh.positions.capacity());
                for (std::uint32_t latitude = 0u; latitude <= latitude_segments; ++latitude) {
                    const float theta = theta_min + (theta_max - theta_min) * static_cast<float>(latitude) / static_cast<float>(latitude_segments);
                    const float sin_theta = std::sin(theta);
                    const float cos_theta = std::cos(theta);
                    for (std::uint32_t longitude = 0u; longitude <= longitude_segments; ++longitude) {
                        const float phi = phi_max * static_cast<float>(longitude) / static_cast<float>(longitude_segments);
                        const Vector3 normal{sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};
                        append_vertex(normal * radius_value, normal);
                    }
                }
                mesh.indices.reserve(static_cast<std::size_t>(latitude_segments) * static_cast<std::size_t>(longitude_segments) * 6u);
                for (std::uint32_t latitude = 0u; latitude < latitude_segments; ++latitude) {
                    for (std::uint32_t longitude = 0u; longitude < longitude_segments; ++longitude) {
                        const std::uint32_t current = latitude * longitude_count + longitude;
                        append_quad(current, current + longitude_count, current + longitude_count + 1u, current + 1u);
                    }
                }
                return mesh;
            }
            if (shape.entity.type == "disk") {
                const float height = optional_one_float_value(shape.entity, "height", 0.0f);
                const float radius_value = optional_one_float_value(shape.entity, "radius", 1.0f);
                const float inner_radius = optional_one_float_value(shape.entity, "innerradius", 0.0f);
                const float phi_max_degrees = std::clamp(optional_one_float_value(shape.entity, "phimax", 360.0f), 0.0f, 360.0f);
                validate_finite_float(height, std::format("{} disk height", context));
                validate_positive_float(radius_value, std::format("{} disk radius", context));
                if (!std::isfinite(inner_radius) || inner_radius < 0.0f || inner_radius >= radius_value) throw std::runtime_error(std::format("{} disk innerradius must be finite and inside [0, radius)", context));
                if (phi_max_degrees <= 0.0f) throw std::runtime_error(std::format("{} disk phimax must be positive", context));
                constexpr std::uint32_t segments = 96u;
                const float phi_max = phi_max_degrees * std::numbers::pi_v<float> / 180.0f;
                mesh.positions.reserve((segments + 1u) * 2u);
                mesh.normals.reserve(mesh.positions.capacity());
                for (std::uint32_t segment = 0u; segment <= segments; ++segment) {
                    const float phi = phi_max * static_cast<float>(segment) / static_cast<float>(segments);
                    const float cosine = std::cos(phi);
                    const float sine = std::sin(phi);
                    append_vertex(Vector3{inner_radius * cosine, inner_radius * sine, height}, Vector3{0.0f, 0.0f, 1.0f});
                    append_vertex(Vector3{radius_value * cosine, radius_value * sine, height}, Vector3{0.0f, 0.0f, 1.0f});
                }
                mesh.indices.reserve(static_cast<std::size_t>(segments) * 6u);
                for (std::uint32_t segment = 0u; segment < segments; ++segment) {
                    const std::uint32_t current = segment * 2u;
                    append_quad(current, current + 1u, current + 3u, current + 2u);
                }
                return mesh;
            }
            if (shape.entity.type == "cylinder") {
                const float radius_value = optional_one_float_value(shape.entity, "radius", 1.0f);
                const float z_min = optional_one_float_value(shape.entity, "zmin", -1.0f);
                const float z_max = optional_one_float_value(shape.entity, "zmax", 1.0f);
                const float phi_max_degrees = std::clamp(optional_one_float_value(shape.entity, "phimax", 360.0f), 0.0f, 360.0f);
                validate_positive_float(radius_value, std::format("{} cylinder radius", context));
                validate_finite_float(z_min, std::format("{} cylinder zmin", context));
                validate_finite_float(z_max, std::format("{} cylinder zmax", context));
                if (z_min >= z_max) throw std::runtime_error(std::format("{} cylinder zmin must be smaller than zmax", context));
                if (phi_max_degrees <= 0.0f) throw std::runtime_error(std::format("{} cylinder phimax must be positive", context));
                constexpr std::uint32_t segments = 96u;
                const float phi_max = phi_max_degrees * std::numbers::pi_v<float> / 180.0f;
                mesh.positions.reserve((segments + 1u) * 2u);
                mesh.normals.reserve(mesh.positions.capacity());
                for (std::uint32_t segment = 0u; segment <= segments; ++segment) {
                    const float phi = phi_max * static_cast<float>(segment) / static_cast<float>(segments);
                    const Vector3 normal{std::cos(phi), std::sin(phi), 0.0f};
                    append_vertex(Vector3{radius_value * normal.x, radius_value * normal.y, z_min}, normal);
                    append_vertex(Vector3{radius_value * normal.x, radius_value * normal.y, z_max}, normal);
                }
                mesh.indices.reserve(static_cast<std::size_t>(segments) * 6u);
                for (std::uint32_t segment = 0u; segment < segments; ++segment) {
                    const std::uint32_t current = segment * 2u;
                    append_quad(current, current + 1u, current + 3u, current + 2u);
                }
                return mesh;
            }
            if (shape.entity.type != "trianglemesh") throw std::runtime_error(std::format("PBRT preview scene loader only supports trianglemesh, plymesh, sphere, disk, cylinder, bilinearmesh, loopsubdiv, and cylinder bezier curve shapes, got \"{}\"", shape.entity.type));
            const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
            const std::vector<float>& normals = required_float_values(shape.entity, "normal", "N", context);
            const std::vector<int>& indices = required_int_values(shape.entity, "indices", context);
            const std::vector<float>* uvs = optional_float_values(shape.entity, "point2", "uv");
            if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid point3 P data", object_name));
            if (normals.size() != positions.size()) throw std::runtime_error(std::format("PBRT preview shape \"{}\" normal count does not match position count", object_name));
            if (indices.empty() || indices.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid triangle index data", object_name));
            if (uvs != nullptr && uvs->size() != positions.size() / 3u * 2u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" uv count does not match position count", object_name));

            const std::size_t vertex_count = positions.size() / 3u;
            mesh.positions.reserve(vertex_count);
            mesh.normals.reserve(vertex_count);
            if (uvs != nullptr) mesh.uvs.reserve(vertex_count);
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Vector3 point{positions.at(vertex_index * 3u), positions.at(vertex_index * 3u + 1u), positions.at(vertex_index * 3u + 2u)};
                const Vector3 normal{normals.at(vertex_index * 3u), normals.at(vertex_index * 3u + 1u), normals.at(vertex_index * 3u + 2u)};
                const Vector3 transformed_point = transform_point(shape.transform.start, point);
                mesh.positions.push_back(transformed_point);
                mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                if (uvs != nullptr) mesh.uvs.push_back(std::array<float, 2>{uvs->at(vertex_index * 2u), uvs->at(vertex_index * 2u + 1u)});
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

        [[nodiscard]] Scene::PreviewLight make_area_light_preview(const Scene::Shape& shape, const Scene::Mesh& mesh, const std::size_t shape_index) {
            if (!shape.area_light.has_value()) throw std::runtime_error("PBRT preview area light requires an area light shape");
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.area_light->entity.type != "diffuse") throw std::runtime_error(std::format("{} uses unsupported area light type \"{}\"", context, shape.area_light->entity.type));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("{} area light mesh has invalid triangle index data", context));

            float total_area = 0.0f;
            Vector3 weighted_center{};
            Vector3 weighted_normal{};
            for (std::size_t index = 0u; index < mesh.indices.size(); index += 3u) {
                const Vector3& p0 = mesh.positions.at(mesh.indices.at(index));
                const Vector3& p1 = mesh.positions.at(mesh.indices.at(index + 1u));
                const Vector3& p2 = mesh.positions.at(mesh.indices.at(index + 2u));
                const Vector3 cross_value = cross(p1 - p0, p2 - p0);
                const float double_area = length(cross_value);
                if (!std::isfinite(double_area)) throw std::runtime_error(std::format("{} area light has non-finite triangle area", context));
                if (double_area <= 0.0f) continue;
                const float triangle_area = double_area * 0.5f;
                total_area += triangle_area;
                weighted_center += ((p0 + p1 + p2) / 3.0f) * triangle_area;
                weighted_normal += cross_value;
            }
            if (!std::isfinite(total_area) || total_area <= 0.0f) throw std::runtime_error(std::format("{} area light has zero surface area", context));
            const Vector3 center_value = weighted_center / total_area;
            const Vector3 normal_value = normalize(weighted_normal, std::format("{} area light normal", context));
            const Vector3 radiance = required_rgb_value(shape.area_light->entity, "L", context);
            const float scale = optional_one_float_value(shape.area_light->entity, "scale", 1.0f);
            if (scale < 0.0f) throw std::runtime_error(std::format("{} area light scale must be non-negative", context));
            return Scene::PreviewLight{
                .name      = std::format("{}#area-light", mesh.name),
                .kind      = Scene::PreviewLightKind::Area,
                .transform = Transform{.position = center_value, .rotation = quaternion_from_light_forward(normal_value, std::format("{} area light rotation", context))},
                .color     = radiance,
                .intensity = total_area * scale,
                .source    = shape.area_light->entity.source,
            };
        }

        [[nodiscard]] std::map<std::string, const Scene::ObjectDefinition*> object_definition_map(const Scene::ResolvedScene& scene) {
            std::map<std::string, const Scene::ObjectDefinition*> definitions{};
            for (const Scene::ObjectDefinition& definition : scene.object_definitions) {
                if (definition.name.empty()) throw std::runtime_error("PBRT preview object definition name must not be empty");
                if (!definitions.emplace(definition.name, &definition).second) throw std::runtime_error(std::format("PBRT preview object definition \"{}\" is duplicated", definition.name));
            }
            return definitions;
        }

        void append_meshes(const std::string_view object_source_prefix_value, const Scene::ResolvedScene& scene, Scene::Document& document, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            std::map<std::string, bool> material_used_by_area_light{};
            std::size_t preview_shape_index = 0u;
            const auto append_shape = [&](const Scene::Shape& shape) {
                const bool is_area_light = shape.area_light.has_value();
                const std::pair<std::map<std::string, bool>::iterator, bool> material_usage = material_used_by_area_light.emplace(shape.material_name, is_area_light);
                if (!material_usage.second && material_usage.first->second != is_area_light) throw std::runtime_error(std::format("PBRT preview material \"{}\" is shared by emissive and non-emissive shapes", shape.material_name));
                apply_area_light_material(shape, preview_shape_index, document, material_indices);
                apply_medium_boundary_material(scene, shape, preview_shape_index, document, material_indices);
                Scene::Mesh mesh = make_mesh(object_source_prefix_value, shape, preview_shape_index, material_indices, bounds);
                if (shape.area_light.has_value()) document.lights.push_back(make_area_light_preview(shape, mesh, preview_shape_index));
                document.meshes.push_back(std::move(mesh));
                ++preview_shape_index;
            };
            for (const Scene::Shape& shape : scene.shapes) append_shape(shape);
            const std::map<std::string, const Scene::ObjectDefinition*> definitions = object_definition_map(scene);
            for (const Scene::ObjectInstance& instance : scene.object_instances) {
                const std::map<std::string, const Scene::ObjectDefinition*>::const_iterator definition_iter = definitions.find(instance.definition_name);
                if (definition_iter == definitions.end()) throw std::runtime_error(std::format("PBRT preview object instance \"{}\" references unknown definition \"{}\"", instance.name, instance.definition_name));
                require_static_transform(instance.transform, std::format("PBRT preview object instance \"{}\"", instance.name));
                const Scene::ObjectDefinition& definition = *definition_iter->second;
                for (const Scene::Shape& shape : definition.shapes) {
                    if (shape.area_light.has_value()) throw std::runtime_error(std::format("PBRT preview object definition \"{}\" contains an instanced area light, which is not supported", definition.name));
                    Scene::Shape instanced_shape = shape;
                    instanced_shape.name         = std::format("{}:{}", instance.name, shape.name);
                    instanced_shape.transform    = multiply_transform_set(instance.transform, shape.transform);
                    append_shape(instanced_shape);
                }
            }
            if (document.meshes.empty()) throw std::runtime_error("PBRT preview scene loader did not find any trianglemesh shapes");
        }

        [[nodiscard]] Scene::Camera make_camera(const Scene::ResolvedScene& scene, const Bounds& bounds) {
            if (scene.render_settings.camera.type != "perspective") throw std::runtime_error(std::format("PBRT preview scene loader only supports perspective cameras, got \"{}\"", scene.render_settings.camera.type));
            require_static_transform(scene.render_settings.camera_transform, "PBRT preview camera");
            const CameraPose pose = camera_pose_from_world_from_camera(scene.render_settings.camera_transform.start);
            const CameraFrame frame = camera_frame(pose);
            const Vector3 target = center(bounds);
            const float scene_radius = radius(bounds);
            const float camera_distance = length(pose.position - target);
            const float far_plane = std::max(20.0f, camera_distance + scene_radius * 4.0f);
            return Scene::Camera{
                .name               = "camera.main",
                .view               = CameraViewState{
                    .pose = pose,
                    .focus = target,
                    .navigation_up = frame.up,
                    .projection = CameraProjection{
                        .kind = CameraProjectionKind::Perspective,
                        .vertical_fov_degrees = required_one_float_value(scene.render_settings.camera, "fov", "PBRT preview camera"),
                        .near_plane = 0.01f,
                        .far_plane = far_plane,
                    },
                },
                .source             = scene.render_settings.camera.source,
            };
        }

        void append_pbrt_preview_lights(const Scene::ResolvedScene& scene, Scene::Document& document) {
            for (const Scene::Light& light : scene.lights) {
                const std::string context = std::format("PBRT preview light \"{}\"", light.name);
                require_static_transform(light.transform, context);
                if (light.entity.type == "infinite") {
                    document.lights.push_back(Scene::PreviewLight{
                        .name      = light.name,
                        .kind      = Scene::PreviewLightKind::Environment,
                        .transform = Transform{.position = transform_position(light.transform.start)},
                        .color     = optional_rgb_value(light.entity, "L", Vector3{1.0f, 1.0f, 1.0f}),
                        .intensity = optional_one_float_value(light.entity, "scale", 1.0f),
                        .source    = light.entity.source,
                    });
                    continue;
                }
                if (light.entity.type == "distant") {
                    const Vector3 from = optional_point3_value(light.entity, "from", Vector3{0.0f, 0.0f, 0.0f});
                    const Vector3 to = optional_point3_value(light.entity, "to", Vector3{0.0f, 0.0f, 1.0f});
                    const Vector3 local_light_forward = normalize(to - from, context);
                    const Vector3 world_light_forward = normalize(transform_vector(light.transform.start, local_light_forward), context);
                    document.lights.push_back(Scene::PreviewLight{
                        .name      = light.name,
                        .kind      = Scene::PreviewLightKind::Directional,
                        .transform = Transform{.position = transform_position(light.transform.start), .rotation = quaternion_from_light_forward(world_light_forward, std::format("{} direction rotation", context))},
                        .color     = optional_rgb_value(light.entity, "L", Vector3{1.0f, 1.0f, 1.0f}),
                        .intensity = optional_one_float_value(light.entity, "scale", 1.0f),
                        .source    = light.entity.source,
                    });
                    continue;
                }
                if (light.entity.type == "point") {
                    document.lights.push_back(Scene::PreviewLight{
                        .name      = light.name,
                        .kind      = Scene::PreviewLightKind::Point,
                        .transform = Transform{.position = transform_point(light.transform.start, optional_point3_value(light.entity, "from", Vector3{}))},
                        .color     = optional_rgb_value(light.entity, "I", Vector3{1.0f, 1.0f, 1.0f}),
                        .intensity = optional_one_float_value(light.entity, "scale", 1.0f),
                        .source    = light.entity.source,
                    });
                    continue;
                }
                if (light.entity.type == "spot") {
                    const Vector3 from = optional_point3_value(light.entity, "from", Vector3{0.0f, 0.0f, 0.0f});
                    const Vector3 to = optional_point3_value(light.entity, "to", Vector3{0.0f, 0.0f, 1.0f});
                    const Vector3 local_light_forward = normalize(to - from, context);
                    const Vector3 world_light_forward = normalize(transform_vector(light.transform.start, local_light_forward), context);
                    document.lights.push_back(Scene::PreviewLight{
                        .name               = light.name,
                        .kind               = Scene::PreviewLightKind::Spot,
                        .transform          = Transform{.position = transform_point(light.transform.start, from), .rotation = quaternion_from_light_forward(world_light_forward, std::format("{} spot rotation", context))},
                        .color              = optional_rgb_value(light.entity, "I", Vector3{1.0f, 1.0f, 1.0f}),
                        .intensity          = optional_one_float_value(light.entity, "scale", 1.0f),
                        .cone_angle_degrees = optional_one_float_value(light.entity, "coneangle", 30.0f),
                        .source             = light.entity.source,
                    });
                    continue;
                }
                if (light.entity.type == "projection") {
                    const std::string filename = required_string_value(light.entity, "filename", context);
                    const Vector3 image_color = read_preview_image_average_color(resolve_entity_asset_path(light.entity, filename, context), context);
                    const Vector3 world_light_forward = normalize(transform_vector(light.transform.start, Vector3{0.0f, 0.0f, 1.0f}), context);
                    document.lights.push_back(Scene::PreviewLight{
                        .name               = light.name,
                        .kind               = Scene::PreviewLightKind::Spot,
                        .transform          = Transform{.position = transform_position(light.transform.start), .rotation = quaternion_from_light_forward(world_light_forward, std::format("{} projection rotation", context))},
                        .color              = image_color,
                        .intensity          = optional_one_float_value(light.entity, "scale", 1.0f),
                        .cone_angle_degrees = optional_one_float_value(light.entity, "fov", 90.0f),
                        .source             = light.entity.source,
                    });
                    continue;
                }
                if (light.entity.type == "goniometric") {
                    Vector3 image_color{1.0f, 1.0f, 1.0f};
                    const std::string filename = optional_string_value(light.entity, "filename", "", context);
                    if (!filename.empty()) image_color = read_preview_image_average_color(resolve_entity_asset_path(light.entity, filename, context), context);
                    const Vector3 intensity_color = optional_rgb_value(light.entity, "I", Vector3{1.0f, 1.0f, 1.0f});
                    const Vector3 color = multiply_color(intensity_color, image_color);
                    document.lights.push_back(Scene::PreviewLight{
                        .name      = light.name,
                        .kind      = Scene::PreviewLightKind::Point,
                        .transform = Transform{.position = transform_position(light.transform.start)},
                        .color     = color,
                        .intensity = optional_one_float_value(light.entity, "scale", 1.0f),
                        .source    = light.entity.source,
                    });
                    continue;
                }
                throw std::runtime_error(std::format("{} uses unsupported light type \"{}\"", context, light.entity.type));
            }
        }
    } // namespace

    Scene::Document make_preview_document_from_pbrt(const Scene::ResolvedScene& scene) {
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
        document.textures = scene.textures;
        Bounds bounds{};
        const std::set<std::string> referenced_material_names = referenced_shape_material_names(scene);
        const std::map<std::string, std::size_t> material_indices = append_materials(scene, referenced_material_names, document);
        append_meshes(object_source_prefix_value, scene, document, material_indices, bounds);
        Scene::Camera camera = make_camera(scene, bounds);
        document.active_camera_name = camera.name;
        document.cameras.push_back(std::move(camera));
        append_pbrt_preview_lights(scene, document);
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

        [[nodiscard]] std::string SourceString(const Scene::SourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::runtime_error ParseError(const Scene::SourceLocation& source, const std::string_view message) {
            return std::runtime_error(std::format("{}: {}", SourceString(source), message));
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
            if (path_extension_is(path, ".gz")) return ReadGzipFile(path);
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
            const std::string lower = lowercase_ascii(name);
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

        [[nodiscard]] std::filesystem::path ResolveScenePathByUniqueStem(const std::filesystem::path& root, const std::string& name) {
            std::optional<std::filesystem::path> match;
            if (!std::filesystem::exists(root)) throw std::runtime_error(std::format("{}: scene root does not exist", root.string()));
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                const std::filesystem::path path = entry.path();
                if (!is_pbrt_scene_file(path)) continue;
                if (scene_file_title(path) != name) continue;
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

        [[nodiscard]] Scene LoadPbrtSceneFile(const std::filesystem::path& scenePath, std::string sceneName) {
            if (scenePath.empty()) throw std::runtime_error("PBRT scene file path must not be empty");
            const std::filesystem::path absolutePath = std::filesystem::absolute(scenePath).lexically_normal();
            if (!std::filesystem::is_regular_file(absolutePath)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", absolutePath.string()));
            if (!is_pbrt_scene_file(absolutePath)) throw std::runtime_error(std::format("{}: PBRT scene file must use .pbrt or .pbrt.gz", absolutePath.string()));

            ScenePbrtBuilder builder(absolutePath);
            Scene::ResolvedScene scene = builder.Parse();
            scene.name = std::move(sceneName);
            scene.title = scene_file_title(absolutePath);
            if (scene.revision.value == 0) scene.revision = Scene::Revision{1};
            return Scene{std::move(scene)};
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
            .coordinate_system       = coordinate_system_label(coordinate_system("PBRT")),
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
        return LoadPbrtSceneFile(scenePath, std::string{scene_id});
    }

    Scene Scene::parse_pbrt_file(const std::filesystem::path& scene_path) {
        if (scene_path.empty()) throw std::runtime_error("PBRT scene file path must not be empty");
        const std::filesystem::path absolutePath = std::filesystem::absolute(scene_path).lexically_normal();
        return LoadPbrtSceneFile(absolutePath, absolutePath.string());
    }

} // namespace spectra::scene
