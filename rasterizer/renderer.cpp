module;

#if defined(_WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <material_symbols/IconsMaterialSymbols.h>
#include <spectra_rasterizer_shaders_spv.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <vulkan/vulkan_raii.hpp>

module spectra.rasterizer.renderer;

import spectra.rasterizer.host;
import spectra.rasterizer.math;
import spectra.scene;
import std;

namespace {
    struct RasterizerVertex {
        float px{};
        float py{};
        float pz{};
        float nx{};
        float ny{};
        float nz{};
    };

    struct PointCloudInstance {
        float px{};
        float py{};
        float pz{};
        float radius{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    struct ViewportSegmentInstance {
        float sx{};
        float sy{};
        float sz{};
        float width{};
        float ex{};
        float ey{};
        float ez{};
        std::uint32_t flags{};
        float r{};
        float g{};
        float b{};
        float a{};
    };

    struct ViewportImagePlaneInstance {
        std::array<float, 16> model{};
        std::array<float, 4> tint{};
    };

    struct ViewportVoxelGridPushConstantsData {
        std::array<float, 4> originCellScale{};
        std::array<float, 4> voxelSize{};
        std::array<float, 4> color{};
        std::array<std::uint32_t, 4> dimensions{};
    };

    struct ViewportVoxelGridCompactionPushConstantsData {
        std::array<std::uint32_t, 4> dimensions{};
        std::array<std::uint32_t, 4> counts{};
    };

    struct VolumeUploadPushConstantsData {
        std::array<std::uint32_t, 4> dimensions{};
    };

    struct CameraVisualPlanes {
        std::array<spectra::scene::Vector3, 4> near_corners{};
        std::array<spectra::scene::Vector3, 4> far_corners{};
    };

    struct DrawPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> base_color{};
        std::array<float, 4> emission{};
        std::array<float, 4> material{};
        std::array<std::uint32_t, 4> flags{};
    };

    struct VolumePushConstantsData {
        std::array<float, 4> originDensityScale{};
        std::array<float, 4> extentStepScale{};
        std::array<float, 4> base_color{};
        std::array<float, 4> emission{};
        std::array<float, 4> material{};
    };

    struct SelectionPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> color{};
        std::array<float, 4> base_color{};
        std::array<float, 4> material{};
        std::array<std::uint32_t, 4> flags{};
    };

    struct PointCloudPushConstantsData {
        std::array<float, 16> model{};
    };

    struct PointCloudSelectionPushConstantsData {
        std::array<float, 16> model{};
        std::array<float, 4> color{};
        std::array<std::uint32_t, 4> flags{};
    };

    struct ViewportSegmentPushConstantsData {
        std::array<float, 16> model{};
    };

    struct VolumeSelectionPushConstantsData {
        std::array<float, 4> originDensityScale{};
        std::array<float, 4> extentStepScale{};
        std::array<float, 4> color{};
        std::uint32_t objectId{};
    };

    struct OutlinePushConstantsData {
        std::array<float, 2> inverseExtent{};
        std::array<float, 2> padding{};
    };

    [[nodiscard]] spectra::rasterizer::math::Vector3 to_render_vector(const spectra::scene::Vector3& value) {
        return spectra::rasterizer::math::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] spectra::rasterizer::math::Quaternion to_render_quaternion(const spectra::scene::Quaternion& value) {
        return spectra::rasterizer::math::Quaternion{value.x, value.y, value.z, value.w};
    }

    [[nodiscard]] spectra::rasterizer::math::Transform to_render_transform(const spectra::scene::Transform& value) {
        return spectra::rasterizer::math::Transform{
            .position = to_render_vector(value.position),
            .rotation = to_render_quaternion(value.rotation),
            .scale    = to_render_vector(value.scale),
        };
    }

    [[nodiscard]] spectra::scene::Vector3 to_scene_vector(const spectra::rasterizer::math::Vector3& value) {
        return spectra::scene::Vector3{value.x, value.y, value.z};
    }

    [[nodiscard]] bool finite_scene_vector(const spectra::scene::Vector3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    [[nodiscard]] bool finite_scene_vector(const spectra::scene::Vector4 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
    }

    [[nodiscard]] std::array<spectra::scene::Vector3, 8> point_cloud_bounds_corners(const spectra::scene::Scene::PointCloudBounds& point_bounds) {
        return std::array{
            spectra::scene::Vector3{point_bounds.minimum.x, point_bounds.minimum.y, point_bounds.minimum.z},
            spectra::scene::Vector3{point_bounds.maximum.x, point_bounds.minimum.y, point_bounds.minimum.z},
            spectra::scene::Vector3{point_bounds.minimum.x, point_bounds.maximum.y, point_bounds.minimum.z},
            spectra::scene::Vector3{point_bounds.maximum.x, point_bounds.maximum.y, point_bounds.minimum.z},
            spectra::scene::Vector3{point_bounds.minimum.x, point_bounds.minimum.y, point_bounds.maximum.z},
            spectra::scene::Vector3{point_bounds.maximum.x, point_bounds.minimum.y, point_bounds.maximum.z},
            spectra::scene::Vector3{point_bounds.minimum.x, point_bounds.maximum.y, point_bounds.maximum.z},
            spectra::scene::Vector3{point_bounds.maximum.x, point_bounds.maximum.y, point_bounds.maximum.z},
        };
    }

    [[nodiscard]] spectra::scene::Vector3 camera_visual_local_corner(const spectra::scene::CameraProjection& projection, const float u, const float v, const float distance) {
        if (!std::isfinite(distance) || !(distance > 0.0f)) throw std::runtime_error("Rasterizer camera visual distance must be positive");
        switch (projection.kind) {
        case spectra::scene::CameraProjectionKind::Perspective: {
            const float vertical_fov = spectra::scene::camera_projection_vertical_fov_degrees(projection);
            const float half_height = distance * std::tan(vertical_fov * std::numbers::pi_v<float> / 360.0f);
            const float aspect = projection.image_width != 0u && projection.image_height != 0u ? static_cast<float>(projection.image_width) / static_cast<float>(projection.image_height) : 1.0f;
            const float half_width = half_height * aspect;
            return spectra::scene::Vector3{u * half_width, v * half_height, distance};
        }
        case spectra::scene::CameraProjectionKind::Pinhole:
            return spectra::scene::Vector3{
                (u - projection.cx) / projection.fx * distance,
                (v - projection.cy) / projection.fy * distance,
                distance,
            };
        case spectra::scene::CameraProjectionKind::Orthographic: {
            const float aspect = projection.image_width != 0u && projection.image_height != 0u ? static_cast<float>(projection.image_width) / static_cast<float>(projection.image_height) : 1.0f;
            const float half_height = projection.orthographic_height * 0.5f;
            const float half_width = half_height * aspect;
            return spectra::scene::Vector3{u * half_width, v * half_height, distance};
        }
        }
        throw std::runtime_error("Unknown Spectra camera projection kind");
    }

    [[nodiscard]] std::array<spectra::scene::Vector3, 4> camera_visual_local_corners(const spectra::scene::CameraProjection& projection, const float distance) {
        if (projection.kind == spectra::scene::CameraProjectionKind::Pinhole) {
            const float image_width = static_cast<float>(projection.image_width);
            const float image_height = static_cast<float>(projection.image_height);
            return std::array{
                camera_visual_local_corner(projection, 0.0f, 0.0f, distance),
                camera_visual_local_corner(projection, image_width, 0.0f, distance),
                camera_visual_local_corner(projection, image_width, image_height, distance),
                camera_visual_local_corner(projection, 0.0f, image_height, distance),
            };
        }
        return std::array{
            camera_visual_local_corner(projection, -1.0f, -1.0f, distance),
            camera_visual_local_corner(projection, 1.0f, -1.0f, distance),
            camera_visual_local_corner(projection, 1.0f, 1.0f, distance),
            camera_visual_local_corner(projection, -1.0f, 1.0f, distance),
        };
    }

    [[nodiscard]] spectra::scene::Vector3 camera_visual_world_point(const spectra::scene::CameraFrame& frame, const spectra::scene::Vector3& local) {
        return frame.position + frame.right * local.x + frame.down * local.y + frame.forward * local.z;
    }

    [[nodiscard]] CameraVisualPlanes camera_visual_planes(const spectra::scene::Scene::Camera& camera, const float visual_near, const float visual_far) {
        const spectra::scene::CameraFrame frame = spectra::scene::camera_frame(camera.pose);
        const std::array<spectra::scene::Vector3, 4> near_local = camera_visual_local_corners(camera.projection, visual_near);
        const std::array<spectra::scene::Vector3, 4> far_local = camera_visual_local_corners(camera.projection, visual_far);
        CameraVisualPlanes planes{};
        for (std::size_t index = 0u; index < 4u; ++index) {
            planes.near_corners.at(index) = camera_visual_world_point(frame, near_local.at(index));
            planes.far_corners.at(index) = camera_visual_world_point(frame, far_local.at(index));
        }
        return planes;
    }

    [[nodiscard]] std::array<float, 16> camera_visual_image_model(const CameraVisualPlanes& planes) {
        const spectra::scene::Vector3 right = planes.far_corners.at(1u) - planes.far_corners.at(0u);
        const spectra::scene::Vector3 down = planes.far_corners.at(3u) - planes.far_corners.at(0u);
        const spectra::scene::Vector3 center = (planes.far_corners.at(0u) + planes.far_corners.at(1u) + planes.far_corners.at(2u) + planes.far_corners.at(3u)) * 0.25f;
        return std::array<float, 16>{
            right.x, right.y, right.z, 0.0f,
            down.x, down.y, down.z, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,
            center.x, center.y, center.z, 1.0f,
        };
    }

    [[nodiscard]] const char* preview_surface_kind_name(const spectra::scene::Scene::PreviewSurfaceKind surface_kind) {
        switch (surface_kind) {
        case spectra::scene::Scene::PreviewSurfaceKind::LitSurface: return "LitSurface";
        case spectra::scene::Scene::PreviewSurfaceKind::UnlitSurface: return "UnlitSurface";
        case spectra::scene::Scene::PreviewSurfaceKind::EmissiveSurface: return "EmissiveSurface";
        case spectra::scene::Scene::PreviewSurfaceKind::Volume: return "Volume";
        case spectra::scene::Scene::PreviewSurfaceKind::PointGlyph: return "PointGlyph";
        }
        throw std::runtime_error("Unknown Spectra rasterizer preview surface kind");
    }

    [[nodiscard]] const char* camera_projection_kind_name(const spectra::scene::CameraProjectionKind projection_kind) {
        switch (projection_kind) {
        case spectra::scene::CameraProjectionKind::Perspective: return "Perspective";
        case spectra::scene::CameraProjectionKind::Orthographic: return "Orthographic";
        case spectra::scene::CameraProjectionKind::Pinhole: return "Pinhole";
        }
        throw std::runtime_error("Unknown Spectra camera projection kind");
    }

    [[nodiscard]] std::uint32_t preview_surface_kind_code(const spectra::scene::Scene::PreviewSurfaceKind surface_kind) {
        switch (surface_kind) {
        case spectra::scene::Scene::PreviewSurfaceKind::LitSurface:
        case spectra::scene::Scene::PreviewSurfaceKind::UnlitSurface:
        case spectra::scene::Scene::PreviewSurfaceKind::EmissiveSurface:
        case spectra::scene::Scene::PreviewSurfaceKind::Volume:
        case spectra::scene::Scene::PreviewSurfaceKind::PointGlyph: return static_cast<std::uint32_t>(surface_kind);
        }
        throw std::runtime_error("Unknown Spectra rasterizer preview surface kind");
    }

    [[nodiscard]] std::uint32_t preview_alpha_mode_code(const spectra::scene::Scene::PreviewAlphaMode alpha_mode) {
        switch (alpha_mode) {
        case spectra::scene::Scene::PreviewAlphaMode::Opaque:
        case spectra::scene::Scene::PreviewAlphaMode::Masked:
        case spectra::scene::Scene::PreviewAlphaMode::Blend: return static_cast<std::uint32_t>(alpha_mode);
        }
        throw std::runtime_error("Unknown Spectra rasterizer material alpha mode");
    }

    void validate_material_values(const spectra::scene::Scene::PreviewMaterial& material) {
        if (material.name.empty()) throw std::runtime_error("Rasterizer material names must not be empty");
        if (!finite_scene_vector(material.base_color)) throw std::runtime_error(std::format("Rasterizer material \"{}\" base color must be finite", material.name));
        if (!finite_scene_vector(material.emission_color)) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission color must be finite", material.name));
        if (material.base_color.x < 0.0f || material.base_color.y < 0.0f || material.base_color.z < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" base color RGB must be non-negative", material.name));
        if (material.base_color.w < 0.0f || material.base_color.w > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" alpha must be inside [0, 1]", material.name));
        if (material.emission_color.x < 0.0f || material.emission_color.y < 0.0f || material.emission_color.z < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission color must be non-negative", material.name));
        if (!std::isfinite(material.emission_strength) || material.emission_strength < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" emission strength must be finite and non-negative", material.name));
        if (!std::isfinite(material.roughness) || material.roughness <= 0.0f || material.roughness > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" roughness must be inside (0, 1]", material.name));
        if (!std::isfinite(material.metallic) || material.metallic < 0.0f || material.metallic > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" metallic must be inside [0, 1]", material.name));
        if (!std::isfinite(material.alpha_cutoff) || material.alpha_cutoff < 0.0f || material.alpha_cutoff > 1.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" alpha cutoff must be inside [0, 1]", material.name));
        if (!std::isfinite(material.volume_density_scale) || material.volume_density_scale <= 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" volume density scale must be finite and positive", material.name));
        if (!std::isfinite(material.volume_temperature_scale) || material.volume_temperature_scale < 0.0f) throw std::runtime_error(std::format("Rasterizer material \"{}\" volume temperature scale must be finite and non-negative", material.name));
        static_cast<void>(preview_surface_kind_code(material.surface_kind));
        static_cast<void>(preview_alpha_mode_code(material.alpha_mode));
        if ((material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::PointGlyph || material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::Volume) && material.alpha_mode != spectra::scene::Scene::PreviewAlphaMode::Blend) throw std::runtime_error(std::format("Rasterizer {} material \"{}\" must use Blend alpha mode", preview_surface_kind_name(material.surface_kind), material.name));
    }

    void validate_material_texture_reference(const std::set<std::string_view>& texture_names, const std::string& texture_name, const std::string_view material_name, const std::string_view field_name) {
        if (texture_name.empty()) return;
        if (!texture_names.contains(std::string_view{texture_name})) throw std::runtime_error(std::format("Rasterizer material \"{}\" references unknown {} texture \"{}\"", material_name, field_name, texture_name));
    }

    void validate_material_library(const spectra::scene::Scene::Document& scene) {
        std::set<std::string_view> texture_names{};
        for (const spectra::scene::Scene::Texture& texture : scene.textures) {
            if (texture.name.empty()) throw std::runtime_error("Rasterizer texture names must not be empty");
            const bool inserted = texture_names.insert(std::string_view{texture.name}).second;
            if (!inserted) throw std::runtime_error(std::format("Rasterizer texture \"{}\" is duplicated", texture.name));
        }
        std::set<std::string_view> names{};
        for (const spectra::scene::Scene::PreviewMaterial& material : scene.materials) {
            validate_material_values(material);
            validate_material_texture_reference(texture_names, material.base_color_texture, material.name, "base color");
            validate_material_texture_reference(texture_names, material.emission_texture, material.name, "emission");
            validate_material_texture_reference(texture_names, material.roughness_texture, material.name, "roughness");
            validate_material_texture_reference(texture_names, material.normal_texture, material.name, "normal");
            const bool inserted = names.insert(std::string_view{material.name}).second;
            if (!inserted) throw std::runtime_error(std::format("Rasterizer material \"{}\" is duplicated", material.name));
        }
    }

    void require_surface_material(const spectra::scene::Scene::PreviewMaterial& material, const std::string_view mesh_name) {
        if (material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::LitSurface || material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::UnlitSurface || material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::EmissiveSurface) return;
        throw std::runtime_error(std::format("Rasterizer mesh \"{}\" requires a surface material, got {} material \"{}\"", mesh_name, preview_surface_kind_name(material.surface_kind), material.name));
    }

    void require_point_glyph_material(const spectra::scene::Scene::PreviewMaterial& material, const std::string_view point_cloud_name) {
        if (material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::PointGlyph) return;
        throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" requires a PointGlyph material, got {} material \"{}\"", point_cloud_name, preview_surface_kind_name(material.surface_kind), material.name));
    }

    void require_volume_material(const spectra::scene::Scene::PreviewMaterial& material, const std::string_view volume_name) {
        if (material.surface_kind == spectra::scene::Scene::PreviewSurfaceKind::Volume) return;
        throw std::runtime_error(std::format("Rasterizer volume \"{}\" requires a Volume material, got {} material \"{}\"", volume_name, preview_surface_kind_name(material.surface_kind), material.name));
    }

    [[nodiscard]] DrawPushConstantsData make_draw_push_constants(const spectra::scene::Transform& transform, const spectra::scene::Scene::PreviewMaterial& material) {
        const spectra::rasterizer::math::Matrix4 model_matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
        return DrawPushConstantsData{
            .model      = model_matrix.values,
            .base_color = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .emission   = {material.emission_color.x * material.emission_strength, material.emission_color.y * material.emission_strength, material.emission_color.z * material.emission_strength, 0.0f},
            .material   = {material.roughness, material.metallic, material.alpha_cutoff, 0.0f},
            .flags      = {preview_surface_kind_code(material.surface_kind), preview_alpha_mode_code(material.alpha_mode), 0u, 0u},
        };
    }

    [[nodiscard]] SelectionPushConstantsData make_selection_push_constants(const spectra::scene::Transform& transform, const spectra::scene::Scene::PreviewMaterial& material, const std::array<float, 4>& color, const std::uint32_t object_id) {
        const spectra::rasterizer::math::Matrix4 model_matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
        return SelectionPushConstantsData{
            .model      = model_matrix.values,
            .color      = color,
            .base_color = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .material   = {material.roughness, material.metallic, material.alpha_cutoff, 0.0f},
            .flags      = {object_id, preview_alpha_mode_code(material.alpha_mode), 0u, 0u},
        };
    }

    [[nodiscard]] PointCloudPushConstantsData make_point_cloud_push_constants(const spectra::scene::Transform& transform) {
        return PointCloudPushConstantsData{
            .model = spectra::rasterizer::math::transform_matrix(to_render_transform(transform)).values,
        };
    }

    [[nodiscard]] PointCloudSelectionPushConstantsData make_point_cloud_selection_push_constants(const spectra::scene::Transform& transform, const std::array<float, 4>& color, const std::uint32_t object_id) {
        return PointCloudSelectionPushConstantsData{
            .model = spectra::rasterizer::math::transform_matrix(to_render_transform(transform)).values,
            .color = color,
            .flags = {object_id, 0u, 0u, 0u},
        };
    }

    [[nodiscard]] ViewportSegmentPushConstantsData make_viewport_segment_push_constants(const spectra::scene::Transform& transform) {
        return ViewportSegmentPushConstantsData{
            .model = spectra::rasterizer::math::transform_matrix(to_render_transform(transform)).values,
        };
    }

    struct ViewportLightingData {
        std::array<float, 4> environmentColorIntensity{};
        std::array<std::array<float, 4>, spectra::rasterizer::Renderer::MaxViewportDirectLights> lightDirections{};
        std::array<std::array<float, 4>, spectra::rasterizer::Renderer::MaxViewportDirectLights> lightPositions{};
        std::array<std::array<float, 4>, spectra::rasterizer::Renderer::MaxViewportDirectLights> lightColorIntensities{};
        std::array<std::uint32_t, 4> lightCounts{};
    };

    [[nodiscard]] float preview_light_kind_code(const spectra::scene::Scene::PreviewLightKind kind) {
        switch (kind) {
        case spectra::scene::Scene::PreviewLightKind::Directional: return 0.0f;
        case spectra::scene::Scene::PreviewLightKind::Point: return 1.0f;
        case spectra::scene::Scene::PreviewLightKind::Spot: return 2.0f;
        case spectra::scene::Scene::PreviewLightKind::Area: return 3.0f;
        case spectra::scene::Scene::PreviewLightKind::Environment: return 4.0f;
        }
        throw std::runtime_error("Unknown Spectra rasterizer preview light kind");
    }

    void validate_preview_light_values(const spectra::scene::Scene::PreviewLight& light) {
        if (light.name.empty()) throw std::runtime_error("Rasterizer preview light names must not be empty");
        if (!finite_scene_vector(light.transform.position)) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" position must be finite", light.name));
        if (!finite_scene_vector(light.transform.scale)) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" scale must be finite", light.name));
        if (light.transform.scale.x == 0.0f || light.transform.scale.y == 0.0f || light.transform.scale.z == 0.0f) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" scale must not contain zero", light.name));
        if (!std::isfinite(light.transform.rotation.x) || !std::isfinite(light.transform.rotation.y) || !std::isfinite(light.transform.rotation.z) || !std::isfinite(light.transform.rotation.w)) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" rotation must be finite", light.name));
        const float rotation_length_squared = light.transform.rotation.x * light.transform.rotation.x + light.transform.rotation.y * light.transform.rotation.y + light.transform.rotation.z * light.transform.rotation.z + light.transform.rotation.w * light.transform.rotation.w;
        if (!std::isfinite(rotation_length_squared) || rotation_length_squared <= 0.0f) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" rotation must not be zero length", light.name));
        if (!finite_scene_vector(light.color)) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" color must be finite", light.name));
        if (light.color.x < 0.0f || light.color.y < 0.0f || light.color.z < 0.0f) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" color must be non-negative", light.name));
        if (!std::isfinite(light.intensity) || light.intensity < 0.0f) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" intensity must be finite and non-negative", light.name));
        if (light.kind == spectra::scene::Scene::PreviewLightKind::Spot && (!std::isfinite(light.cone_angle_degrees) || light.cone_angle_degrees <= 0.0f || light.cone_angle_degrees >= 180.0f)) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" cone angle must be inside (0, 180)", light.name));
    }

    [[nodiscard]] spectra::rasterizer::math::Vector3 preview_light_forward_direction(const spectra::scene::Scene::PreviewLight& light) {
        spectra::rasterizer::math::Transform transform = to_render_transform(light.transform);
        transform.scale = spectra::rasterizer::math::Vector3{1.0f, 1.0f, 1.0f};
        const spectra::rasterizer::math::Matrix4 rotation = spectra::rasterizer::math::transform_matrix(transform);
        return spectra::rasterizer::math::normalize(spectra::rasterizer::math::Vector3{-rotation(2u, 0u), -rotation(2u, 1u), -rotation(2u, 2u)});
    }

    [[nodiscard]] ViewportLightingData explicit_scene_lighting_uniform(const spectra::scene::Scene::Document& scene) {
        ViewportLightingData data{};
        std::set<std::string_view> names{};
        for (const spectra::scene::Scene::PreviewLight& light : scene.lights) {
            validate_preview_light_values(light);
            const bool inserted = names.insert(std::string_view{light.name}).second;
            if (!inserted) throw std::runtime_error(std::format("Rasterizer preview light \"{}\" is duplicated", light.name));
            if (light.kind == spectra::scene::Scene::PreviewLightKind::Environment) {
                data.environmentColorIntensity.at(0) += light.color.x * light.intensity;
                data.environmentColorIntensity.at(1) += light.color.y * light.intensity;
                data.environmentColorIntensity.at(2) += light.color.z * light.intensity;
                data.environmentColorIntensity.at(3) = 1.0f;
                continue;
            }
            if (data.lightCounts.at(0) >= spectra::rasterizer::Renderer::MaxViewportDirectLights) throw std::runtime_error(std::format("Rasterizer viewport supports at most {} explicit direct lights", spectra::rasterizer::Renderer::MaxViewportDirectLights));
            const spectra::rasterizer::math::Vector3 direction = preview_light_forward_direction(light);
            const std::size_t index = data.lightCounts.at(0);
            data.lightDirections.at(index) = {direction.x, direction.y, direction.z, preview_light_kind_code(light.kind)};
            data.lightPositions.at(index) = {light.transform.position.x, light.transform.position.y, light.transform.position.z, light.kind == spectra::scene::Scene::PreviewLightKind::Spot ? std::cos(light.cone_angle_degrees * std::numbers::pi_v<float> / 180.0f) : 0.0f};
            data.lightColorIntensities.at(index) = {light.color.x, light.color.y, light.color.z, light.intensity};
            ++data.lightCounts.at(0);
        }
        return data;
    }

    void draw_tooltip(const char* text) {
        if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
        ImGui::SetTooltip("%s", text);
    }

    void transition_image_layout(const vk::raii::CommandBuffer& command_buffer, const vk::Image image, const vk::ImageAspectFlags aspect, const vk::ImageLayout old_layout, const vk::ImageLayout new_layout, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        if (old_layout == new_layout) return;
        const vk::ImageMemoryBarrier2 image_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            old_layout,
            new_layout,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            image,
            {aspect, 0, 1, 0, 1},
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 0, nullptr, 1, &image_memory_barrier};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    void transition_buffer_access(const vk::raii::CommandBuffer& command_buffer, const vk::Buffer buffer, const vk::PipelineStageFlags2 src_stage, const vk::AccessFlags2 src_access, const vk::PipelineStageFlags2 dst_stage, const vk::AccessFlags2 dst_access) {
        const vk::BufferMemoryBarrier2 buffer_memory_barrier{
            src_stage,
            src_access,
            dst_stage,
            dst_access,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            buffer,
            0u,
            VK_WHOLE_SIZE,
        };
        const vk::DependencyInfo dependency_info{{}, 0, nullptr, 1, &buffer_memory_barrier, 0, nullptr};
        command_buffer.pipelineBarrier2(dependency_info);
    }

    [[nodiscard]] std::uint32_t find_memory_type_index(const vk::raii::PhysicalDevice& physical_device, const std::uint32_t memory_type_bits, const vk::MemoryPropertyFlags required_properties) {
        const vk::PhysicalDeviceMemoryProperties memory_properties = physical_device.getMemoryProperties();
        for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount; ++index) {
            const bool supported = (memory_type_bits & (1u << index)) != 0;
            const bool matching  = (memory_properties.memoryTypes[index].propertyFlags & required_properties) == required_properties;
            if (supported && matching) return index;
        }
        throw std::runtime_error("No matching Vulkan memory type for Spectra rasterizer");
    }

    [[nodiscard]] spectra::scene::GpuDeviceIdentity make_scene_gpu_device_identity(const vk::raii::PhysicalDevice& physical_device) {
        const auto properties_chain = physical_device.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();
        const vk::PhysicalDeviceProperties properties = properties_chain.get<vk::PhysicalDeviceProperties2>().properties;
        const vk::PhysicalDeviceIDProperties id_properties = properties_chain.get<vk::PhysicalDeviceIDProperties>();
        spectra::scene::GpuDeviceIdentity identity{
            .vendor_id = properties.vendorID,
            .device_id = properties.deviceID,
            .device_node_mask = id_properties.deviceNodeMask,
        };
        std::ranges::copy(id_properties.deviceUUID, identity.device_uuid.begin());
        std::ranges::copy(id_properties.deviceLUID, identity.device_luid.begin());
        return identity;
    }

    [[nodiscard]] ImVec4 inspector_label_color() {
        return ImVec4{137.0f / 255.0f, 148.0f / 255.0f, 160.0f / 255.0f, 1.0f};
    }

    [[nodiscard]] ImVec4 inspector_section_color() {
        return ImVec4{218.0f / 255.0f, 225.0f / 255.0f, 232.0f / 255.0f, 1.0f};
    }

    [[nodiscard]] float inspector_label_width() {
        return std::clamp(ImGui::GetContentRegionAvail().x * 0.34f, 96.0f, 122.0f);
    }

    void draw_inspector_section(const char* label) {
        ImGui::Spacing();
        ImGui::TextColored(inspector_section_color(), "%s", label);
        const ImVec2 line_min = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2{0.0f, 3.0f});
        ImGui::GetWindowDrawList()->AddLine(ImVec2{line_min.x, line_min.y + 1.0f}, ImVec2{line_min.x + ImGui::GetContentRegionAvail().x, line_min.y + 1.0f}, ImGui::GetColorU32(ImVec4{62.0f / 255.0f, 72.0f / 255.0f, 81.0f / 255.0f, 0.34f}), 1.0f);
    }

    bool draw_property_row(const char* label, const std::string_view value) {
        const float row_start = ImGui::GetCursorPosX();
        const float label_width = inspector_label_width();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(inspector_label_color(), "%s", label);
        ImGui::SameLine(row_start + label_width);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(1.0f, ImGui::GetContentRegionAvail().x));
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
        const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
        ImGui::PopTextWrapPos();
        return hovered;
    }

    void draw_quiet_splitter(const char* id, const float width, const float height) {
        ImGui::InvisibleButton(id, ImVec2{width, height});
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const float y = std::floor((min.y + max.y) * 0.5f);
        const ImU32 color = ImGui::GetColorU32((ImGui::IsItemHovered() || ImGui::IsItemActive()) ? ImVec4{91.0f / 255.0f, 197.0f / 255.0f, 184.0f / 255.0f, 0.72f} : ImVec4{68.0f / 255.0f, 78.0f / 255.0f, 87.0f / 255.0f, 0.28f});
        ImGui::GetWindowDrawList()->AddLine(ImVec2{min.x + 8.0f, y}, ImVec2{max.x - 8.0f, y}, color, 1.0f);
    }

    [[nodiscard]] std::string normalize_display_path(std::string value) {
        for (char& character : value) {
            if (character == '\\') character = '/';
        }
        return value;
    }

    [[nodiscard]] bool path_escapes_root(const std::filesystem::path& path) {
        for (const std::filesystem::path& component : path) {
            if (component == std::filesystem::path{".."}) return true;
        }
        return false;
    }

    [[nodiscard]] std::optional<std::filesystem::path> path_suffix_from_component(const std::filesystem::path& path, const std::string_view component_name) {
        std::filesystem::path suffix{};
        bool found = false;
        for (const std::filesystem::path& component : path) {
            if (!found && component.generic_string() == std::string{component_name}) found = true;
            if (found) suffix /= component;
        }
        if (!found) return std::nullopt;
        return suffix;
    }

    [[nodiscard]] std::string compact_filesystem_path(const std::string_view value) {
        if (value.empty()) return {};
        const std::filesystem::path path{std::string{value}};
        if (!path.is_absolute()) return normalize_display_path(path.generic_string());
        const std::filesystem::path relative = path.lexically_relative(std::filesystem::current_path());
        if (!relative.empty() && !path_escapes_root(relative)) return normalize_display_path(relative.generic_string());
        const std::optional<std::filesystem::path> scene_suffix = path_suffix_from_component(path, "scenes");
        if (scene_suffix.has_value()) return normalize_display_path(scene_suffix->generic_string());
        const std::filesystem::path filename = path.filename();
        if (!filename.empty()) return normalize_display_path(filename.generic_string());
        return normalize_display_path(path.generic_string());
    }

    [[nodiscard]] std::optional<std::string_view> pbrt_shape_token(const std::string_view value) {
        constexpr std::string_view scheme = "pbrt://";
        constexpr std::string_view marker = "#shape:";
        if (!value.starts_with(scheme)) return std::nullopt;
        const std::size_t marker_position = value.find(marker);
        if (marker_position == std::string_view::npos) return std::nullopt;
        const std::size_t shape_index_begin = marker_position + marker.size();
        if (shape_index_begin >= value.size() || value[shape_index_begin] == '#') return std::nullopt;
        const std::size_t token_begin = marker_position + 1u;
        const std::size_t token_end = value.find('#', token_begin);
        if (token_end == std::string_view::npos) return value.substr(token_begin);
        return value.substr(token_begin, token_end - token_begin);
    }

    [[nodiscard]] std::string compact_object_identifier(const std::string_view value) {
        constexpr std::string_view scheme = "pbrt://";
        std::string text{value};
        if (std::string_view{text.data(), text.size()}.starts_with(scheme)) text.erase(0u, scheme.size());
        std::string fragment{};
        const std::size_t fragment_position = text.find('#');
        if (fragment_position != std::string::npos) {
            fragment = text.substr(fragment_position);
            text.erase(fragment_position);
        }
        const std::filesystem::path path{text};
        if (path.is_absolute()) return compact_filesystem_path(text) + fragment;
        return normalize_display_path(text + fragment);
    }

    [[nodiscard]] std::string format_float(const float value) {
        return std::format("{:.6g}", value);
    }

    [[nodiscard]] std::string format_vector3(const spectra::scene::Vector3& value) {
        return std::format("{:.6g}, {:.6g}, {:.6g}", value.x, value.y, value.z);
    }

    [[nodiscard]] std::string format_vector4(const spectra::scene::Vector4& value) {
        return std::format("{:.6g}, {:.6g}, {:.6g}, {:.6g}", value.x, value.y, value.z, value.w);
    }

    [[nodiscard]] std::string format_quaternion(const spectra::scene::Quaternion& value) {
        return std::format("{:.6g}, {:.6g}, {:.6g}, {:.6g}", value.x, value.y, value.z, value.w);
    }

    [[nodiscard]] std::string format_dimensions3(const std::array<std::uint32_t, 3>& value) {
        return std::format("{} x {} x {}", value[0], value[1], value[2]);
    }

    [[nodiscard]] std::string format_source_location(const spectra::scene::Scene::SourceLocation& source) {
        if (source.filename.empty()) return "<generated>";
        return std::format("{}:{}:{}", source.filename, source.line, source.column);
    }

    [[nodiscard]] const char* preview_alpha_mode_name(const spectra::scene::Scene::PreviewAlphaMode alpha_mode) {
        switch (alpha_mode) {
        case spectra::scene::Scene::PreviewAlphaMode::Opaque: return "Opaque";
        case spectra::scene::Scene::PreviewAlphaMode::Masked: return "Masked";
        case spectra::scene::Scene::PreviewAlphaMode::Blend: return "Blend";
        }
        throw std::runtime_error("Unknown Spectra rasterizer material alpha mode");
    }

    [[nodiscard]] const char* preview_light_kind_name(const spectra::scene::Scene::PreviewLightKind kind) {
        switch (kind) {
        case spectra::scene::Scene::PreviewLightKind::Directional: return "Directional";
        case spectra::scene::Scene::PreviewLightKind::Point: return "Point";
        case spectra::scene::Scene::PreviewLightKind::Spot: return "Spot";
        case spectra::scene::Scene::PreviewLightKind::Area: return "Area";
        case spectra::scene::Scene::PreviewLightKind::Environment: return "Environment";
        }
        throw std::runtime_error("Unknown Spectra rasterizer preview light kind");
    }

    [[nodiscard]] const char* compact_preview_light_kind_name(const spectra::scene::Scene::PreviewLightKind kind) {
        switch (kind) {
        case spectra::scene::Scene::PreviewLightKind::Directional: return "directional-light";
        case spectra::scene::Scene::PreviewLightKind::Point: return "point-light";
        case spectra::scene::Scene::PreviewLightKind::Spot: return "spot-light";
        case spectra::scene::Scene::PreviewLightKind::Area: return "area-light";
        case spectra::scene::Scene::PreviewLightKind::Environment: return "environment-light";
        }
        throw std::runtime_error("Unknown Spectra rasterizer preview light kind");
    }

    void draw_color_row(const char* label, const spectra::scene::Vector3& color) {
        const float row_start = ImGui::GetCursorPosX();
        const float label_width = inspector_label_width();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(inspector_label_color(), "%s", label);
        ImGui::SameLine(row_start + label_width);
        ImGui::PushID(label);
        ImGui::ColorButton("##color", ImVec4{color.x, color.y, color.z, 1.0f}, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2{18.0f, 18.0f});
        ImGui::PopID();
        ImGui::SameLine();
        const std::string value = std::format("{:.6g}, {:.6g}, {:.6g}", color.x, color.y, color.z);
        ImGui::TextUnformatted(value.c_str());
    }

    void draw_color_row(const char* label, const spectra::scene::Vector4& color) {
        const float row_start = ImGui::GetCursorPosX();
        const float label_width = inspector_label_width();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(inspector_label_color(), "%s", label);
        ImGui::SameLine(row_start + label_width);
        ImGui::PushID(label);
        ImGui::ColorButton("##color", ImVec4{color.x, color.y, color.z, color.w}, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_AlphaPreview, ImVec2{18.0f, 18.0f});
        ImGui::PopID();
        ImGui::SameLine();
        const std::string value = format_vector4(color);
        ImGui::TextUnformatted(value.c_str());
    }

    [[nodiscard]] float half_to_float(const std::uint16_t value) {
        const std::uint32_t sign     = (value >> 15u) & 0x1u;
        const std::uint32_t exponent = (value >> 10u) & 0x1fu;
        const std::uint32_t mantissa = value & 0x3ffu;
        float result{};
        if (exponent == 0u) {
            result = mantissa == 0u ? 0.0f : std::ldexp(static_cast<float>(mantissa), -24);
        } else if (exponent == 31u) {
            result = mantissa == 0u ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
        } else {
            result = std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15);
        }
        return sign == 0u ? result : -result;
    }

    [[nodiscard]] std::uint8_t float_to_srgb_byte(const float value) {
        if (!std::isfinite(value)) return 0u;
        const float linear = std::clamp(value, 0.0f, 1.0f);
        const float srgb   = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::clamp(std::lround(srgb * 255.0f), 0l, 255l));
    }

    [[nodiscard]] std::uint8_t float_to_alpha_byte(const float value) {
        if (!std::isfinite(value)) return 255u;
        return static_cast<std::uint8_t>(std::clamp(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f), 0l, 255l));
    }

    [[nodiscard]] std::vector<std::uint8_t> rgba16_float_to_rgba8(const void* source, const vk::Extent2D extent) {
        if (source == nullptr) throw std::runtime_error("Cannot encode Spectra rasterizer screenshot from null pixel data");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot encode an empty Spectra rasterizer screenshot");
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u);
        const std::byte* bytes = static_cast<const std::byte*>(source);
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            for (std::uint32_t x = 0; x < extent.width; ++x) {
                const std::size_t pixel_index = static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) + static_cast<std::size_t>(x);
                const std::size_t source_base = pixel_index * 4u * sizeof(std::uint16_t);
                std::uint16_t red{};
                std::uint16_t green{};
                std::uint16_t blue{};
                std::uint16_t alpha{};
                std::memcpy(&red, bytes + source_base, sizeof(red));
                std::memcpy(&green, bytes + source_base + sizeof(std::uint16_t), sizeof(green));
                std::memcpy(&blue, bytes + source_base + sizeof(std::uint16_t) * 2u, sizeof(blue));
                std::memcpy(&alpha, bytes + source_base + sizeof(std::uint16_t) * 3u, sizeof(alpha));
                const std::size_t destination_base = pixel_index * 4u;
                pixels[destination_base]           = float_to_srgb_byte(half_to_float(red));
                pixels[destination_base + 1u]      = float_to_srgb_byte(half_to_float(green));
                pixels[destination_base + 2u]      = float_to_srgb_byte(half_to_float(blue));
                pixels[destination_base + 3u]      = float_to_alpha_byte(half_to_float(alpha));
            }
        }
        return pixels;
    }

    void append_u32_be(std::vector<std::uint8_t>& data, const std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
        data.push_back(static_cast<std::uint8_t>(value & 0xffu));
    }

    [[nodiscard]] std::uint32_t png_crc32_update(std::uint32_t crc, const std::uint8_t byte) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) crc = (crc & 1u) != 0u ? (crc >> 1u) ^ 0xedb88320u : crc >> 1u;
        return crc;
    }

    [[nodiscard]] std::uint32_t png_crc32(const char* type, const std::vector<std::uint8_t>& data) {
        std::uint32_t crc = 0xffffffffu;
        for (std::size_t index = 0; index < 4u; ++index) crc = png_crc32_update(crc, static_cast<std::uint8_t>(type[index]));
        for (const std::uint8_t byte : data) crc = png_crc32_update(crc, byte);
        return ~crc;
    }

    void append_png_chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
        append_u32_be(png, static_cast<std::uint32_t>(data.size()));
        for (std::size_t index = 0; index < 4u; ++index) png.push_back(static_cast<std::uint8_t>(type[index]));
        png.insert(png.end(), data.begin(), data.end());
        append_u32_be(png, png_crc32(type, data));
    }

    [[nodiscard]] std::uint32_t adler32(const std::vector<std::uint8_t>& data) {
        constexpr std::uint32_t modulus = 65521u;
        std::uint32_t a                 = 1u;
        std::uint32_t b                 = 0u;
        for (const std::uint8_t byte : data) {
            a = (a + byte) % modulus;
            b = (b + a) % modulus;
        }
        return (b << 16u) | a;
    }

    [[nodiscard]] std::vector<std::uint8_t> zlib_store(const std::vector<std::uint8_t>& data) {
        std::vector<std::uint8_t> compressed{};
        compressed.reserve(data.size() + data.size() / 65535u * 5u + 16u);
        compressed.push_back(0x78u);
        compressed.push_back(0x01u);
        std::size_t offset = 0;
        while (offset < data.size()) {
            const std::size_t block_size = std::min<std::size_t>(65535u, data.size() - offset);
            const bool final_block       = offset + block_size == data.size();
            compressed.push_back(final_block ? 0x01u : 0x00u);
            const std::uint16_t length = static_cast<std::uint16_t>(block_size);
            const std::uint16_t nlength = static_cast<std::uint16_t>(~length);
            compressed.push_back(static_cast<std::uint8_t>(length & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>((length >> 8u) & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>(nlength & 0xffu));
            compressed.push_back(static_cast<std::uint8_t>((nlength >> 8u) & 0xffu));
            compressed.insert(compressed.end(), data.begin() + static_cast<std::ptrdiff_t>(offset), data.begin() + static_cast<std::ptrdiff_t>(offset + block_size));
            offset += block_size;
        }
        append_u32_be(compressed, adler32(data));
        return compressed;
    }

    void write_png_rgba8(const std::filesystem::path& path, const vk::Extent2D extent, const std::vector<std::uint8_t>& pixels) {
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot write an empty Spectra rasterizer screenshot");
        const std::size_t expected_size = static_cast<std::size_t>(extent.width) * static_cast<std::size_t>(extent.height) * 4u;
        if (pixels.size() != expected_size) throw std::runtime_error("Spectra rasterizer screenshot pixel buffer has an invalid size");

        std::vector<std::uint8_t> raw{};
        raw.reserve(static_cast<std::size_t>(extent.height) * (static_cast<std::size_t>(extent.width) * 4u + 1u));
        for (std::uint32_t y = 0; y < extent.height; ++y) {
            raw.push_back(0u);
            const std::size_t row_begin = static_cast<std::size_t>(y) * static_cast<std::size_t>(extent.width) * 4u;
            raw.insert(raw.end(), pixels.begin() + static_cast<std::ptrdiff_t>(row_begin), pixels.begin() + static_cast<std::ptrdiff_t>(row_begin + static_cast<std::size_t>(extent.width) * 4u));
        }

        std::vector<std::uint8_t> png{0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
        std::vector<std::uint8_t> ihdr{};
        append_u32_be(ihdr, extent.width);
        append_u32_be(ihdr, extent.height);
        ihdr.push_back(8u);
        ihdr.push_back(6u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        ihdr.push_back(0u);
        append_png_chunk(png, "IHDR", ihdr);
        append_png_chunk(png, "IDAT", zlib_store(raw));
        const std::vector<std::uint8_t> empty_chunk{};
        append_png_chunk(png, "IEND", empty_chunk);

        std::filesystem::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary};
        if (!output) throw std::runtime_error(std::format("Failed to open Spectra rasterizer screenshot file: {}", path.string()));
        output.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        if (!output) throw std::runtime_error(std::format("Failed to write Spectra rasterizer screenshot file: {}", path.string()));
    }

    [[nodiscard]] std::filesystem::path next_screenshot_path() {
        const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        const std::int64_t milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        return std::filesystem::current_path() / "screenshots" / std::format("spectra-rasterizer-{}.png", milliseconds);
    }
} // namespace

namespace spectra::rasterizer {
    Renderer::Renderer(std::shared_ptr<scene::Scene> scene_instance, std::shared_ptr<scene::CameraWorkspace> camera_workspace) {
        this->scene.instance = std::move(scene_instance);
        this->scene.camera_workspace = std::move(camera_workspace);
        if (this->scene.instance == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene");
        if (this->scene.camera_workspace == nullptr) throw std::runtime_error("Spectra rasterizer requires a scene camera workspace");
        this->scene.host_services = this->scene.instance->host_services();
        if (this->scene.host_services == nullptr) throw std::runtime_error("Spectra rasterizer requires scene host services");
        static_cast<void>(this->scene.instance->document());
        this->ensure_viewport_camera_session();
        this->synchronize_viewport_camera();
    }

    Renderer::~Renderer() noexcept = default;

    std::string_view Renderer::name() {
        return "Spectra Rasterizer";
    }

    void Renderer::set_scene(std::shared_ptr<scene::Scene> scene_instance, std::shared_ptr<scene::CameraWorkspace> camera_workspace) {
        if (scene_instance == nullptr) throw std::runtime_error("Spectra rasterizer scene must not be null");
        if (camera_workspace == nullptr) throw std::runtime_error("Spectra rasterizer camera workspace must not be null");
        static_cast<void>(scene_instance->document());
        if (this->host.device != nullptr) this->host.device->waitIdle();
        this->clear_selection();
        this->selection.object_ids.clear();
        this->selection.objects_by_id.clear();
        this->selection.registry_valid = false;
        this->ui.scene_ui_cache = SceneUiCache{};
        this->destroy_selection_resources();
        this->destroy_mesh_resources();
        this->destroy_point_cloud_resources();
        this->destroy_viewport_segment_resources();
        this->destroy_viewport_voxel_grid_resources();
        this->destroy_viewport_image_plane_resources();
        this->destroy_volume_resources();
        if (this->lifecycle.attached && this->scene.host_services != nullptr) this->scene.host_services->clear_gpu_buffer_backend();
        this->scene.instance = std::move(scene_instance);
        this->scene.camera_workspace = std::move(camera_workspace);
        this->scene.host_services = this->scene.instance->host_services();
        if (this->lifecycle.attached) this->connect_scene_host();
        this->scene.observed_camera_revision = scene::CameraRevision{};
        this->scene.observed_camera_scene_id.clear();
        this->viewport.camera_initialized = false;
        this->reset_viewport_camera_session();
    }

    void Renderer::attach(HostView host) {
        if (this->lifecycle.attached) throw std::runtime_error("Spectra rasterizer plugin is already attached");
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        std::move_only_function<void(ImVec2, ImVec2)> draw_viewport_overlays = host.take_viewport_overlay_draw_callback();
        this->host.draw_viewport_overlays = [draw_viewport_overlays = std::move(draw_viewport_overlays)](const float x, const float y, const float width, const float height) mutable {
            draw_viewport_overlays(ImVec2{x, y}, ImVec2{width, height});
        };
        this->connect_scene_host();
        this->register_workspace_contributions(host);
        this->lifecycle.attached = true;
    }

    void Renderer::detach() noexcept {
        this->disconnect_scene_host();
        this->destroy_selection_resources();
        this->destroy_viewport_resources();
        this->destroy_screenshot_resources();
        this->destroy_mesh_resources();
        this->destroy_viewport_grid_resources();
        this->destroy_point_cloud_resources();
        this->destroy_viewport_segment_resources();
        this->destroy_viewport_voxel_grid_resources();
        this->destroy_external_storage_buffers();
        this->destroy_viewport_image_plane_resources();
        this->destroy_volume_resources();
        this->destroy_camera_resources();
        this->host.physical_device  = nullptr;
        this->host.device           = nullptr;
        this->host.swapchain_extent = vk::Extent2D{};
        this->host.frame_count      = 0;
        this->host.draw_viewport_overlays = {};
        this->lifecycle.attached    = false;
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::before_imgui_shutdown() noexcept {
        this->destroy_imgui_descriptor();
        this->lifecycle.imgui_ready = false;
    }

    void Renderer::after_imgui_created() {
        this->lifecycle.imgui_ready = true;
        if (*this->viewport.image && this->viewport.imgui_descriptor == VK_NULL_HANDLE) this->create_imgui_descriptor();
    }

    void Renderer::update_host(const vk::raii::PhysicalDevice& physical_device, const vk::raii::Device& device, const std::uint32_t frame_count, const vk::Extent2D swapchain_extent) {
        if (frame_count == 0) throw std::runtime_error("Spectra rasterizer host frame count must be positive");
        if (swapchain_extent.width == 0 || swapchain_extent.height == 0) throw std::runtime_error("Spectra rasterizer host swapchain extent must be positive");
        this->host.physical_device  = &physical_device;
        this->host.device           = &device;
        this->host.swapchain_extent = swapchain_extent;
        this->host.frame_count      = frame_count;
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) this->ui.requested_extent = swapchain_extent;
    }

    void Renderer::wait_device_idle_for_cleanup() noexcept {
        try {
            if (this->host.device != nullptr) this->host.device->waitIdle();
        } catch (...) {
        }
    }

    void Renderer::register_workspace_contributions(HostView& host) {
        host.register_panel(Panel{
            .id                  = "rasterizer.viewport",
            .title               = "Rasterizer Viewport",
            .icon                = ICON_MS_GRID_VIEW,
            .shortcut_label      = "F7",
            .shortcut_key        = ImGuiKey_F7,
            .window_flags        = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse,
            .closable            = false,
            .zero_window_padding = true,
            .draw                = [this] { this->draw_viewport_window(); },
        });
        host.register_command_popover(CommandPopover{
            .id             = "renderer.settings",
            .title          = "Renderer",
            .icon           = ICON_MS_TUNE,
            .shortcut_label = "F8",
            .shortcut_key   = ImGuiKey_F8,
            .draw           = [this] { this->draw_rasterizer_window(); },
        });
    }

    std::string Renderer::window_detail() const {
        const std::uint32_t width  = this->viewport.extent.width != 0 ? this->viewport.extent.width : this->host.swapchain_extent.width;
        const std::uint32_t height = this->viewport.extent.height != 0 ? this->viewport.extent.height : this->host.swapchain_extent.height;
        const std::shared_ptr<const scene::Scene::Document> document = this->scene.instance->document();
        return std::format("{} | {}x{}", document->title.empty() ? document->name : document->title, width, height);
    }

    void Renderer::create_viewport_resources(const vk::Extent2D extent) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport without Vulkan handles");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer viewport with a zero extent");
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.image                             = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = this->viewport.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        this->viewport.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        this->viewport.image.bindMemory(*this->viewport.memory, 0);

        const vk::ImageViewCreateInfo image_view_create_info{{}, *this->viewport.image, vk::ImageViewType::e2D, this->viewport.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        this->viewport.view = vk::raii::ImageView{*this->host.device, image_view_create_info};
        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatOpaqueBlack,
            VK_FALSE,
        };
        this->viewport.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::ImageCreateInfo depth_image_create_info{
            {},
            vk::ImageType::e2D,
            this->viewport.depth_format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eDepthStencilAttachment,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        this->viewport.depth_image                             = vk::raii::Image{*this->host.device, depth_image_create_info};
        const vk::MemoryRequirements depth_memory_requirements = this->viewport.depth_image.getMemoryRequirements();
        const std::uint32_t depth_memory_type                  = find_memory_type_index(*this->host.physical_device, depth_memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo depth_memory_allocate_info{depth_memory_requirements.size, depth_memory_type};
        this->viewport.depth_memory = vk::raii::DeviceMemory{*this->host.device, depth_memory_allocate_info};
        this->viewport.depth_image.bindMemory(*this->viewport.depth_memory, 0);
        const vk::ImageViewCreateInfo depth_view_create_info{{}, *this->viewport.depth_image, vk::ImageViewType::e2D, this->viewport.depth_format, {}, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1}};
        this->viewport.depth_view = vk::raii::ImageView{*this->host.device, depth_view_create_info};

        this->viewport.extent       = extent;
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
        if (this->lifecycle.imgui_ready) this->create_imgui_descriptor();
    }

    void Renderer::destroy_imgui_descriptor() noexcept {
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) return;
        this->wait_device_idle_for_cleanup();
        ImGui_ImplVulkan_RemoveTexture(this->viewport.imgui_descriptor);
        this->viewport.imgui_descriptor = VK_NULL_HANDLE;
    }

    void Renderer::destroy_viewport_resources() noexcept {
        this->destroy_imgui_descriptor();
        this->viewport.depth_view   = nullptr;
        this->viewport.depth_image  = nullptr;
        this->viewport.depth_memory = nullptr;
        this->viewport.sampler      = nullptr;
        this->viewport.view         = nullptr;
        this->viewport.image        = nullptr;
        this->viewport.memory       = nullptr;
        this->viewport.extent       = vk::Extent2D{};
        this->viewport.layout       = vk::ImageLayout::eUndefined;
        this->viewport.depth_layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_screenshot_resources() noexcept {
        this->destroy_host_buffer(this->viewport.screenshot_buffer);
        this->viewport.screenshot_requested   = false;
        this->viewport.screenshot_pending     = false;
        this->viewport.screenshot_frame_index = 0;
        this->viewport.screenshot_extent      = vk::Extent2D{};
        this->viewport.screenshot_path.clear();
    }

    void Renderer::ensure_viewport_resources() {
        if (this->ui.requested_extent.width == 0 || this->ui.requested_extent.height == 0) return;
        if (*this->viewport.image && this->viewport.extent.width == this->ui.requested_extent.width && this->viewport.extent.height == this->ui.requested_extent.height) return;
        this->destroy_viewport_resources();
        this->create_viewport_resources(this->ui.requested_extent);
    }

    void Renderer::create_imgui_descriptor() {
        if (!*this->viewport.sampler || !*this->viewport.view) throw std::runtime_error("Cannot create Spectra rasterizer descriptor before viewport resources exist");
        if (this->viewport.imgui_descriptor != VK_NULL_HANDLE) throw std::runtime_error("Spectra rasterizer viewport descriptor is already allocated");
        this->viewport.imgui_descriptor = ImGui_ImplVulkan_AddTexture(static_cast<VkSampler>(*this->viewport.sampler), static_cast<VkImageView>(*this->viewport.view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) throw std::runtime_error("Failed to allocate Spectra rasterizer viewport descriptor");
    }

    void Renderer::destroy_host_buffer(GpuBuffer& buffer) noexcept {
        if (buffer.mapped != nullptr && this->host.device != nullptr && *buffer.memory) vkUnmapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory));
        buffer.mapped   = nullptr;
        buffer.buffer   = nullptr;
        buffer.memory   = nullptr;
        buffer.capacity = 0;
    }

    void Renderer::destroy_external_buffer(ExternalGpuBuffer& buffer) noexcept {
        buffer.buffer = nullptr;
        buffer.memory = nullptr;
        buffer.capacity = 0;
    }

    void Renderer::destroy_device_buffer(DeviceGpuBuffer& buffer) noexcept {
        buffer.buffer = nullptr;
        buffer.memory = nullptr;
        buffer.capacity = 0;
    }

    void Renderer::ensure_host_buffer(GpuBuffer& buffer, const vk::DeviceSize required_size, const vk::BufferUsageFlags usage) {
        if (required_size == 0) throw std::runtime_error("Cannot allocate an empty Spectra rasterizer buffer");
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot allocate Spectra rasterizer buffers without Vulkan handles");
        if (*buffer.buffer && buffer.capacity >= required_size) return;
        this->destroy_host_buffer(buffer);
        const vk::BufferCreateInfo buffer_create_info{{}, required_size, usage, vk::SharingMode::eExclusive};
        buffer.buffer = vk::raii::Buffer{*this->host.device, buffer_create_info};
        const vk::MemoryRequirements memory_requirements = buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        buffer.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        buffer.buffer.bindMemory(*buffer.memory, 0);
        if (vkMapMemory(static_cast<VkDevice>(**this->host.device), static_cast<VkDeviceMemory>(*buffer.memory), 0, required_size, 0, &buffer.mapped) != VK_SUCCESS) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
        buffer.capacity = required_size;
        if (buffer.mapped == nullptr) throw std::runtime_error("Failed to map Spectra rasterizer buffer memory");
    }

    void Renderer::ensure_device_buffer(DeviceGpuBuffer& buffer, const vk::DeviceSize required_size, const vk::BufferUsageFlags usage) {
        if (required_size == 0) throw std::runtime_error("Cannot allocate an empty Spectra rasterizer device buffer");
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot allocate Spectra rasterizer device buffers without Vulkan handles");
        if (*buffer.buffer && buffer.capacity >= required_size) return;
        this->destroy_device_buffer(buffer);
        const vk::BufferCreateInfo buffer_create_info{{}, required_size, usage, vk::SharingMode::eExclusive};
        buffer.buffer = vk::raii::Buffer{*this->host.device, buffer_create_info};
        const vk::MemoryRequirements memory_requirements = buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        buffer.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        buffer.buffer.bindMemory(*buffer.memory, 0);
        buffer.capacity = required_size;
    }

    scene::GpuResourceHandleKind Renderer::external_storage_handle_kind() const {
#if defined(_WIN32)
        return scene::GpuResourceHandleKind::OpaqueWin32;
#else
        return scene::GpuResourceHandleKind::OpaqueFileDescriptor;
#endif
    }

    vk::BufferUsageFlags external_storage_buffer_usage(const std::uint32_t kind) {
        if (kind == scene::GpuBufferKindVolumeChannel) return vk::BufferUsageFlagBits::eStorageBuffer;
        if (kind == scene::GpuBufferKindViewportVoxelGrid) return vk::BufferUsageFlagBits::eStorageBuffer;
        if (kind == scene::GpuBufferKindPointCloud) return vk::BufferUsageFlagBits::eVertexBuffer;
        if (kind == scene::GpuBufferKindViewportSegmentSet) return vk::BufferUsageFlagBits::eVertexBuffer;
        throw std::runtime_error(std::format("Scene GPU buffer kind {} is unsupported by the rasterizer", kind));
    }

    Renderer::ExternalStorageBuffer& Renderer::external_storage_buffer(const std::uint64_t resource_id, const std::string_view context) {
        const std::map<std::uint64_t, ExternalStorageBuffer>::iterator found = this->external_storage.buffers.find(resource_id);
        if (found == this->external_storage.buffers.end()) throw std::runtime_error(std::format("{} external storage buffer {} does not exist", context, resource_id));
        return found->second;
    }

    const Renderer::ExternalStorageBuffer& Renderer::external_storage_buffer(const std::uint64_t resource_id, const std::string_view context) const {
        const std::map<std::uint64_t, ExternalStorageBuffer>::const_iterator found = this->external_storage.buffers.find(resource_id);
        if (found == this->external_storage.buffers.end()) throw std::runtime_error(std::format("{} external storage buffer {} does not exist", context, resource_id));
        return found->second;
    }

    scene::GpuBufferAllocation Renderer::request_external_storage_buffer(const std::uint32_t kind, const std::uint64_t byte_size, const std::string_view debug_name, const std::string_view context) {
        static_cast<void>(debug_name);
        if (byte_size == 0u) throw std::runtime_error(std::format("{} byte size must be positive", context));
        if (byte_size > static_cast<std::uint64_t>(std::numeric_limits<vk::DeviceSize>::max())) throw std::runtime_error(std::format("{} byte size exceeds Vulkan device size range", context));
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error(std::format("Cannot allocate {} before rasterizer is attached", context));

#if defined(_WIN32)
        constexpr vk::ExternalMemoryHandleTypeFlagBits handle_type = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueWin32;
#else
        constexpr vk::ExternalMemoryHandleTypeFlagBits handle_type = vk::ExternalMemoryHandleTypeFlagBits::eOpaqueFd;
#endif

        const std::uint64_t resource_id = this->external_storage.next_resource_id++;
        if (resource_id == 0u) throw std::runtime_error(std::format("{} resource id overflowed", context));
        ExternalStorageBuffer resource{
            .resource_id = resource_id,
            .byte_size = byte_size,
            .kind = kind,
        };

        const vk::ExternalMemoryBufferCreateInfo external_buffer_create_info{handle_type};
        vk::BufferCreateInfo buffer_create_info{{}, static_cast<vk::DeviceSize>(byte_size), external_storage_buffer_usage(kind), vk::SharingMode::eExclusive};
        buffer_create_info.setPNext(&external_buffer_create_info);
        resource.buffer.buffer = vk::raii::Buffer{*this->host.device, buffer_create_info};

        const vk::MemoryRequirements memory_requirements = resource.buffer.buffer.getMemoryRequirements();
        const std::uint32_t memory_type = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::ExportMemoryAllocateInfo export_memory_allocate_info{handle_type};
        vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        memory_allocate_info.setPNext(&export_memory_allocate_info);
        resource.buffer.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        resource.buffer.buffer.bindMemory(*resource.buffer.memory, 0);
        resource.buffer.capacity = static_cast<vk::DeviceSize>(byte_size);

#if defined(_WIN32)
        const vk::MemoryGetWin32HandleInfoKHR handle_info{*resource.buffer.memory, handle_type};
        HANDLE exported_handle = this->host.device->getMemoryWin32HandleKHR(handle_info);
        if (exported_handle == nullptr) throw std::runtime_error(std::format("Failed to export {} Win32 handle", context));
        const std::uintptr_t exported_handle_value = reinterpret_cast<std::uintptr_t>(exported_handle);
#else
        const vk::MemoryGetFdInfoKHR handle_info{*resource.buffer.memory, handle_type};
        int exported_handle = this->host.device->getMemoryFdKHR(handle_info);
        if (exported_handle < 0) throw std::runtime_error(std::format("Failed to export {} file descriptor", context));
        const std::uintptr_t exported_handle_value = static_cast<std::uintptr_t>(exported_handle);
#endif

        scene::GpuBufferAllocation allocation{
            .resource_id = resource_id,
            .byte_size = static_cast<std::uint64_t>(memory_requirements.size),
            .kind = kind,
            .handle_kind = this->external_storage_handle_kind(),
            .handle = exported_handle_value,
            .device_identity = make_scene_gpu_device_identity(*this->host.physical_device),
        };
        this->external_storage.buffers.emplace(resource_id, std::move(resource));
        return allocation;
    }

    void Renderer::release_external_storage_buffer(const std::uint64_t resource_id, const std::string_view context) {
        if (resource_id == 0u) throw std::runtime_error(std::format("{} resource id must not be zero", context));
        const std::map<std::uint64_t, ExternalStorageBuffer>::iterator found = this->external_storage.buffers.find(resource_id);
        if (found == this->external_storage.buffers.end()) throw std::runtime_error(std::format("{} resource {} does not exist", context, resource_id));
        if (this->host.device != nullptr) this->host.device->waitIdle();
        this->destroy_external_buffer(found->second.buffer);
        this->external_storage.buffers.erase(found);
    }

    scene::GpuBufferAllocation Renderer::request_scene_gpu_buffer(const scene::GpuBufferRequest& request) {
        if (request.kind == scene::GpuBufferKindViewportVoxelGrid) {
            scene::GpuBufferAllocation allocation = this->request_external_storage_buffer(request.kind, request.byte_size, request.debug_name, "Scene viewport voxel buffer");
            ViewportVoxelBufferDescriptor descriptor{};
            this->ensure_viewport_voxel_buffer_descriptor(allocation.resource_id, descriptor);
            const std::pair<std::map<std::uint64_t, ViewportVoxelBufferDescriptor>::iterator, bool> inserted = this->viewport_voxel_grid_pass.buffer_descriptors.emplace(allocation.resource_id, std::move(descriptor));
            if (!inserted.second) throw std::runtime_error(std::format("Scene viewport voxel buffer descriptor {} already exists", allocation.resource_id));
            return allocation;
        }
        if (request.kind == scene::GpuBufferKindVolumeChannel) return this->request_external_storage_buffer(request.kind, request.byte_size, request.debug_name, "Scene volume buffer");
        if (request.kind == scene::GpuBufferKindPointCloud) return this->request_external_storage_buffer(request.kind, request.byte_size, request.debug_name, "Scene point cloud buffer");
        if (request.kind == scene::GpuBufferKindViewportSegmentSet) return this->request_external_storage_buffer(request.kind, request.byte_size, request.debug_name, "Scene viewport segment buffer");
        throw std::runtime_error(std::format("Scene GPU buffer kind {} is unsupported by the rasterizer", request.kind));
    }

    void Renderer::release_scene_gpu_buffer(const std::uint64_t resource_id) {
        const bool viewport_voxel_buffer = this->viewport_voxel_grid_pass.buffer_descriptors.contains(resource_id);
        if (viewport_voxel_buffer) {
            const std::map<std::uint64_t, ViewportVoxelBufferDescriptor>::iterator descriptor = this->viewport_voxel_grid_pass.buffer_descriptors.find(resource_id);
            if (this->host.device != nullptr) this->host.device->waitIdle();
            for (FrameViewportVoxelGridResources& frame_voxel_grids : this->viewport_voxel_grid_pass.frame_voxel_grids) {
                std::erase_if(frame_voxel_grids.drawCommands, [resource_id](const ViewportVoxelGridDrawCommand& draw_command) {
                    return draw_command.bufferId == resource_id;
                });
            }
            for (std::map<ViewportVoxelGridCompactionKey, ViewportVoxelGridCompactionResource>::iterator compaction = this->viewport_voxel_grid_pass.compactions.begin(); compaction != this->viewport_voxel_grid_pass.compactions.end();) {
                if (compaction->first.bufferId != resource_id) {
                    ++compaction;
                    continue;
                }
                this->destroy_viewport_voxel_grid_compaction_resource(compaction->second);
                compaction = this->viewport_voxel_grid_pass.compactions.erase(compaction);
            }
            this->viewport_voxel_grid_pass.buffer_descriptors.erase(descriptor);
            this->release_external_storage_buffer(resource_id, "Scene viewport voxel buffer");
            return;
        }
        const std::map<std::uint64_t, ExternalStorageBuffer>::const_iterator resource = this->external_storage.buffers.find(resource_id);
        if (resource == this->external_storage.buffers.end()) throw std::runtime_error(std::format("Scene GPU buffer resource {} does not exist", resource_id));
        if (resource->second.kind == scene::GpuBufferKindPointCloud) {
            if (this->host.device != nullptr) this->host.device->waitIdle();
            for (FramePointCloudResources& frame_point_cloud : this->point_cloud_pass.frame_point_clouds)
                std::erase_if(frame_point_cloud.drawCommands, [resource_id](const PointCloudDrawCommand& draw_command) { return draw_command.bufferId == resource_id; });
            this->release_external_storage_buffer(resource_id, "Scene point cloud buffer");
            return;
        }
        if (resource->second.kind == scene::GpuBufferKindViewportSegmentSet) {
            if (this->host.device != nullptr) this->host.device->waitIdle();
            for (FrameViewportSegmentResources& frame_segments : this->viewport_segment_pass.frame_segments)
                std::erase_if(frame_segments.drawCommands, [resource_id](const ViewportSegmentDrawCommand& draw_command) { return draw_command.bufferId == resource_id; });
            this->release_external_storage_buffer(resource_id, "Scene viewport segment buffer");
            return;
        }
        if (resource->second.kind == scene::GpuBufferKindVolumeChannel) {
            this->release_external_storage_buffer(resource_id, "Scene volume buffer");
            return;
        }
        throw std::runtime_error(std::format("Scene GPU buffer resource {} has unsupported kind {}", resource_id, resource->second.kind));
    }

    void Renderer::ensure_viewport_voxel_buffer_descriptor(const std::uint64_t resource_id, ViewportVoxelBufferDescriptor& descriptor) {
        if (descriptor.descriptor_valid) return;
        const ExternalStorageBuffer& resource = this->external_storage_buffer(resource_id, "Scene viewport voxel buffer");
        if (!*resource.buffer.buffer) throw std::runtime_error("Scene viewport voxel buffer has no Vulkan buffer");
        if (!*this->viewport_voxel_grid_pass.descriptor_set_layout || !*this->viewport_voxel_grid_pass.descriptor_pool) return;
        const vk::DescriptorSetLayout descriptor_set_layout = *this->viewport_voxel_grid_pass.descriptor_set_layout;
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->viewport_voxel_grid_pass.descriptor_pool, 1u, &descriptor_set_layout};
        descriptor.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (descriptor.descriptor_sets.size() != 1u) throw std::runtime_error("Failed to allocate Scene viewport voxel buffer descriptor set");
        const vk::DescriptorBufferInfo descriptor_buffer_info{*resource.buffer.buffer, 0u, resource.byte_size};
        const std::array descriptor_writes{vk::WriteDescriptorSet{*descriptor.descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_info}};
        this->host.device->updateDescriptorSets(descriptor_writes, {});
        descriptor.descriptor_valid = true;
    }

    void Renderer::ensure_viewport_voxel_grid_compaction_resource(const ViewportVoxelGridDrawCommand& draw_command) {
        if (draw_command.sourceKind != scene::Scene::ViewportVoxelGridSourceKind::Bitfield) throw std::runtime_error("Viewport voxel grid compaction requires a bitfield source");
        const ExternalStorageBuffer& source = this->external_storage_buffer(draw_command.bufferId, "Viewport voxel grid bitfield");
        if (draw_command.cellCount == 0u) throw std::runtime_error("Viewport voxel grid bitfield compaction requires a non-zero cell count");
        if (draw_command.bitfieldByteCount == 0u) throw std::runtime_error("Viewport voxel grid bitfield compaction requires a non-zero bitfield byte count");
        if (draw_command.bitfieldByteCount > source.byte_size) throw std::runtime_error(std::format("Viewport voxel grid bitfield byte count {} exceeds buffer {} byte size {}", draw_command.bitfieldByteCount, draw_command.bufferId, source.byte_size));
        if (!*this->viewport_voxel_grid_pass.descriptor_set_layout || !*this->viewport_voxel_grid_pass.compaction_descriptor_set_layout || !*this->viewport_voxel_grid_pass.descriptor_pool) throw std::runtime_error("Viewport voxel grid compaction descriptors are not initialized");

        const ViewportVoxelGridCompactionKey key{
            .bufferId = draw_command.bufferId,
            .frameIndex = draw_command.frameIndex,
            .dimX = draw_command.dimensions[0],
            .dimY = draw_command.dimensions[1],
            .dimZ = draw_command.dimensions[2],
            .indexEncoding = draw_command.indexEncoding,
        };
        ViewportVoxelGridCompactionResource& resource = this->viewport_voxel_grid_pass.compactions[key];
        const vk::DeviceSize compacted_bytes = static_cast<vk::DeviceSize>(draw_command.cellCount) * sizeof(std::uint32_t);
        const bool compacted_recreated = !*resource.compactedIndexBuffer.buffer || resource.compactedIndexBuffer.capacity < compacted_bytes;
        const bool counter_recreated = !*resource.counterBuffer.buffer || resource.counterBuffer.capacity < sizeof(std::uint32_t);
        const bool indirect_recreated = !*resource.indirectBuffer.buffer || resource.indirectBuffer.capacity < sizeof(vk::DrawIndirectCommand);
        this->ensure_device_buffer(resource.compactedIndexBuffer, compacted_bytes, vk::BufferUsageFlagBits::eStorageBuffer);
        this->ensure_device_buffer(resource.counterBuffer, sizeof(std::uint32_t), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
        this->ensure_device_buffer(resource.indirectBuffer, sizeof(vk::DrawIndirectCommand), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndirectBuffer);
        if (compacted_recreated || counter_recreated || indirect_recreated || resource.source_buffer_id != draw_command.bufferId || resource.source_byte_size != source.byte_size) {
            resource.compute_descriptor_sets = nullptr;
            resource.draw_descriptor_sets = nullptr;
            resource.compute_descriptor_valid = false;
            resource.draw_descriptor_valid = false;
        }
        resource.source_buffer_id = draw_command.bufferId;
        resource.source_byte_size = source.byte_size;
        resource.compacted_capacity = draw_command.cellCount;

        if (!resource.compute_descriptor_valid) {
            const vk::DescriptorSetLayout compute_descriptor_set_layout = *this->viewport_voxel_grid_pass.compaction_descriptor_set_layout;
            const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->viewport_voxel_grid_pass.descriptor_pool, 1u, &compute_descriptor_set_layout};
            resource.compute_descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
            if (resource.compute_descriptor_sets.size() != 1u) throw std::runtime_error("Failed to allocate viewport voxel grid compaction descriptor set");
            const std::array descriptor_buffer_infos{
                vk::DescriptorBufferInfo{*resource.compactedIndexBuffer.buffer, 0u, compacted_bytes},
                vk::DescriptorBufferInfo{*resource.counterBuffer.buffer, 0u, sizeof(std::uint32_t)},
                vk::DescriptorBufferInfo{*source.buffer.buffer, 0u, source.byte_size},
                vk::DescriptorBufferInfo{*resource.indirectBuffer.buffer, 0u, sizeof(vk::DrawIndirectCommand)},
            };
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*resource.compute_descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_infos[0]},
                vk::WriteDescriptorSet{*resource.compute_descriptor_sets.at(0), 1u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_infos[1]},
                vk::WriteDescriptorSet{*resource.compute_descriptor_sets.at(0), 2u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_infos[2]},
                vk::WriteDescriptorSet{*resource.compute_descriptor_sets.at(0), 3u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_infos[3]},
            };
            this->host.device->updateDescriptorSets(descriptor_writes, {});
            resource.compute_descriptor_valid = true;
        }
        if (!resource.draw_descriptor_valid) {
            const vk::DescriptorSetLayout draw_descriptor_set_layout = *this->viewport_voxel_grid_pass.descriptor_set_layout;
            const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->viewport_voxel_grid_pass.descriptor_pool, 1u, &draw_descriptor_set_layout};
            resource.draw_descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
            if (resource.draw_descriptor_sets.size() != 1u) throw std::runtime_error("Failed to allocate viewport voxel grid compacted draw descriptor set");
            const vk::DescriptorBufferInfo descriptor_buffer_info{*resource.compactedIndexBuffer.buffer, 0u, compacted_bytes};
            const std::array descriptor_writes{vk::WriteDescriptorSet{*resource.draw_descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &descriptor_buffer_info}};
            this->host.device->updateDescriptorSets(descriptor_writes, {});
            resource.draw_descriptor_valid = true;
        }
    }

    void Renderer::connect_scene_host() {
        if (this->scene.host_services == nullptr) throw std::runtime_error("Spectra rasterizer Scene host services are not initialized");
        this->scene.host_services->set_gpu_buffer_backend(
            [this](const scene::GpuBufferRequest& request) {
                return this->request_scene_gpu_buffer(request);
            },
            [this](const std::uint64_t resource_id) {
                this->release_scene_gpu_buffer(resource_id);
            });
    }

    void Renderer::disconnect_scene_host() noexcept {
        if (this->scene.host_services != nullptr) this->scene.host_services->clear_gpu_buffer_backend();
    }

    void Renderer::create_image_2d(GpuImage2D& image, const vk::Extent2D extent, const vk::Format format, const vk::ImageUsageFlags usage, const vk::ImageAspectFlags aspect) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer image without Vulkan handles");
        if (extent.width == 0 || extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer image with a zero extent");
        this->destroy_image_2d(image);
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e2D,
            format,
            vk::Extent3D{extent.width, extent.height, 1},
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            usage,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        image.image                                      = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = image.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        image.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        image.image.bindMemory(*image.memory, 0);
        const vk::ImageViewCreateInfo image_view_create_info{{}, *image.image, vk::ImageViewType::e2D, format, {}, {aspect, 0, 1, 0, 1}};
        image.view   = vk::raii::ImageView{*this->host.device, image_view_create_info};
        image.extent = extent;
        image.format = format;
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_image_2d(GpuImage2D& image) noexcept {
        image.view   = nullptr;
        image.image  = nullptr;
        image.memory = nullptr;
        image.extent = vk::Extent2D{};
        image.format = vk::Format{};
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::create_volume_image(GpuImage3D& image, const vk::Extent3D extent, const vk::Format format) {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume image without Vulkan handles");
        if (extent.width == 0 || extent.height == 0 || extent.depth == 0) throw std::runtime_error("Cannot create Spectra rasterizer volume image with zero dimensions");
        this->destroy_volume_image(image);
        const vk::ImageCreateInfo image_create_info{
            {},
            vk::ImageType::e3D,
            format,
            extent,
            1,
            1,
            vk::SampleCountFlagBits::e1,
            vk::ImageTiling::eOptimal,
            vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage,
            vk::SharingMode::eExclusive,
            0,
            nullptr,
            vk::ImageLayout::eUndefined,
        };
        image.image                                      = vk::raii::Image{*this->host.device, image_create_info};
        const vk::MemoryRequirements memory_requirements = image.image.getMemoryRequirements();
        const std::uint32_t memory_type                  = find_memory_type_index(*this->host.physical_device, memory_requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        const vk::MemoryAllocateInfo memory_allocate_info{memory_requirements.size, memory_type};
        image.memory = vk::raii::DeviceMemory{*this->host.device, memory_allocate_info};
        image.image.bindMemory(*image.memory, 0);
        const vk::ImageViewCreateInfo image_view_create_info{{}, *image.image, vk::ImageViewType::e3D, format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};
        image.view   = vk::raii::ImageView{*this->host.device, image_view_create_info};
        image.extent = extent;
        image.format = format;
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_volume_image(GpuImage3D& image) noexcept {
        image.view   = nullptr;
        image.image  = nullptr;
        image.memory = nullptr;
        image.extent = vk::Extent3D{};
        image.format = vk::Format{};
        image.layout = vk::ImageLayout::eUndefined;
    }

    void Renderer::destroy_camera_resources() noexcept {
        if (this->camera.frame_count == 0 && !*this->camera.descriptor_set_layout && !*this->camera.descriptor_pool && this->camera.descriptor_sets.size() == 0 && this->camera.uniform_buffers.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (GpuBuffer& uniform_buffer : this->camera.uniform_buffers) this->destroy_host_buffer(uniform_buffer);
        this->camera.uniform_buffers.clear();
        this->camera.descriptor_sets       = nullptr;
        this->camera.descriptor_pool       = nullptr;
        this->camera.descriptor_set_layout = nullptr;
        this->camera.frame_count           = 0;
    }

    void Renderer::destroy_mesh_resources() noexcept {
        if (this->mesh_pass.frame_count == 0 && !*this->mesh_pass.pipeline_layout && !*this->mesh_pass.pipeline && !*this->mesh_pass.transparent_pipeline && this->mesh_pass.frame_scenes.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameSceneResources& frame_scene : this->mesh_pass.frame_scenes) {
            this->destroy_host_buffer(frame_scene.vertexBuffer);
            this->destroy_host_buffer(frame_scene.indexBuffer);
        }
        this->mesh_pass.frame_scenes.clear();
        this->mesh_pass.transparent_pipeline = nullptr;
        this->mesh_pass.pipeline             = nullptr;
        this->mesh_pass.pipeline_layout      = nullptr;
        this->mesh_pass.frame_count          = 0;
    }

    void Renderer::destroy_viewport_grid_resources() noexcept {
        if (this->viewport_grid_pass.frame_count == 0 && !*this->viewport_grid_pass.pipeline_layout && !*this->viewport_grid_pass.pipeline) return;
        this->wait_device_idle_for_cleanup();
        this->viewport_grid_pass.pipeline        = nullptr;
        this->viewport_grid_pass.pipeline_layout = nullptr;
        this->viewport_grid_pass.frame_count     = 0;
    }

    void Renderer::destroy_point_cloud_resources() noexcept {
        if (this->point_cloud_pass.frame_count == 0 && !*this->point_cloud_pass.pipeline_layout && !*this->point_cloud_pass.pipeline && this->point_cloud_pass.frame_point_clouds.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FramePointCloudResources& frame_point_cloud : this->point_cloud_pass.frame_point_clouds) this->destroy_host_buffer(frame_point_cloud.instanceBuffer);
        this->point_cloud_pass.frame_point_clouds.clear();
        this->point_cloud_pass.pipeline        = nullptr;
        this->point_cloud_pass.pipeline_layout = nullptr;
        this->point_cloud_pass.frame_count     = 0;
    }

    void Renderer::destroy_viewport_segment_resources() noexcept {
        if (this->viewport_segment_pass.frame_count == 0 && !*this->viewport_segment_pass.pipeline_layout && !*this->viewport_segment_pass.depth_tested_pipeline && !*this->viewport_segment_pass.always_visible_pipeline && this->viewport_segment_pass.frame_segments.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameViewportSegmentResources& frame_segments : this->viewport_segment_pass.frame_segments) this->destroy_host_buffer(frame_segments.instanceBuffer);
        this->viewport_segment_pass.frame_segments.clear();
        this->viewport_segment_pass.always_visible_pipeline = nullptr;
        this->viewport_segment_pass.depth_tested_pipeline   = nullptr;
        this->viewport_segment_pass.pipeline_layout         = nullptr;
        this->viewport_segment_pass.frame_count             = 0;
    }

    void Renderer::destroy_viewport_voxel_grid_resources() noexcept {
        if (this->viewport_voxel_grid_pass.frame_count == 0 && !*this->viewport_voxel_grid_pass.descriptor_set_layout && !*this->viewport_voxel_grid_pass.compaction_descriptor_set_layout && !*this->viewport_voxel_grid_pass.descriptor_pool && !*this->viewport_voxel_grid_pass.pipeline_layout && !*this->viewport_voxel_grid_pass.compaction_pipeline_layout && !*this->viewport_voxel_grid_pass.compaction_pipeline && !*this->viewport_voxel_grid_pass.depth_tested_pipeline && !*this->viewport_voxel_grid_pass.always_visible_pipeline && this->viewport_voxel_grid_pass.frame_voxel_grids.empty() && this->viewport_voxel_grid_pass.compactions.empty() && this->viewport_voxel_grid_pass.buffer_descriptors.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (std::pair<const std::uint64_t, ViewportVoxelBufferDescriptor>& descriptor : this->viewport_voxel_grid_pass.buffer_descriptors) {
            descriptor.second.descriptor_sets = nullptr;
            descriptor.second.descriptor_valid = false;
        }
        for (std::pair<const ViewportVoxelGridCompactionKey, ViewportVoxelGridCompactionResource>& resource : this->viewport_voxel_grid_pass.compactions) this->destroy_viewport_voxel_grid_compaction_resource(resource.second);
        this->viewport_voxel_grid_pass.compactions.clear();
        this->viewport_voxel_grid_pass.frame_voxel_grids.clear();
        this->viewport_voxel_grid_pass.always_visible_pipeline = nullptr;
        this->viewport_voxel_grid_pass.depth_tested_pipeline = nullptr;
        this->viewport_voxel_grid_pass.compaction_pipeline = nullptr;
        this->viewport_voxel_grid_pass.compaction_pipeline_layout = nullptr;
        this->viewport_voxel_grid_pass.pipeline_layout = nullptr;
        this->viewport_voxel_grid_pass.descriptor_pool = nullptr;
        this->viewport_voxel_grid_pass.compaction_descriptor_set_layout = nullptr;
        this->viewport_voxel_grid_pass.descriptor_set_layout = nullptr;
        this->viewport_voxel_grid_pass.frame_count = 0;
    }

    void Renderer::destroy_external_storage_buffers() noexcept {
        if (this->external_storage.buffers.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (std::pair<const std::uint64_t, ViewportVoxelBufferDescriptor>& descriptor : this->viewport_voxel_grid_pass.buffer_descriptors) {
            descriptor.second.descriptor_sets = nullptr;
            descriptor.second.descriptor_valid = false;
        }
        for (std::pair<const std::uint64_t, ExternalStorageBuffer>& resource : this->external_storage.buffers) {
            this->destroy_external_buffer(resource.second.buffer);
        }
        this->viewport_voxel_grid_pass.buffer_descriptors.clear();
        this->external_storage.buffers.clear();
    }

    void Renderer::destroy_viewport_voxel_grid_compaction_resource(ViewportVoxelGridCompactionResource& resource) noexcept {
        resource.compute_descriptor_sets = nullptr;
        resource.draw_descriptor_sets = nullptr;
        resource.compute_descriptor_valid = false;
        resource.draw_descriptor_valid = false;
        resource.source_buffer_id = 0u;
        resource.source_byte_size = 0u;
        resource.compacted_capacity = 0u;
        this->destroy_device_buffer(resource.compactedIndexBuffer);
        this->destroy_device_buffer(resource.counterBuffer);
        this->destroy_device_buffer(resource.indirectBuffer);
    }

    void Renderer::destroy_viewport_image_plane_texture(ViewportImagePlaneTexture& texture) noexcept {
        this->destroy_host_buffer(texture.stagingBuffer);
        this->destroy_image_2d(texture.image);
        texture.descriptor_sets = nullptr;
        texture.descriptor_pool = nullptr;
        texture.uploadPending = false;
    }

    void Renderer::destroy_viewport_image_plane_resources() noexcept {
        if (this->viewport_image_plane_pass.frame_count == 0 && !*this->viewport_image_plane_pass.descriptor_set_layout && !*this->viewport_image_plane_pass.sampler && !*this->viewport_image_plane_pass.pipeline_layout && !*this->viewport_image_plane_pass.depth_tested_pipeline && !*this->viewport_image_plane_pass.always_visible_pipeline && this->viewport_image_plane_pass.frame_planes.empty() && this->viewport_image_plane_pass.texture_cache.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameViewportImagePlaneResources& frame_planes : this->viewport_image_plane_pass.frame_planes) this->destroy_host_buffer(frame_planes.instanceBuffer);
        this->viewport_image_plane_pass.frame_planes.clear();
        for (std::pair<const std::string, ViewportImagePlaneTexture>& texture : this->viewport_image_plane_pass.texture_cache) this->destroy_viewport_image_plane_texture(texture.second);
        this->viewport_image_plane_pass.texture_cache.clear();
        this->viewport_image_plane_pass.always_visible_pipeline = nullptr;
        this->viewport_image_plane_pass.depth_tested_pipeline   = nullptr;
        this->viewport_image_plane_pass.pipeline_layout         = nullptr;
        this->viewport_image_plane_pass.sampler                 = nullptr;
        this->viewport_image_plane_pass.descriptor_set_layout   = nullptr;
        this->viewport_image_plane_pass.frame_count             = 0;
    }

    void Renderer::destroy_volume_resources() noexcept {
        if (this->volume_pass.frame_count == 0 && !*this->volume_pass.descriptor_set_layout && !*this->volume_pass.upload_descriptor_set_layout && !*this->volume_pass.descriptor_pool && this->volume_pass.descriptor_sets.size() == 0 && !*this->volume_pass.sampler && !*this->volume_pass.pipeline_layout && !*this->volume_pass.upload_pipeline_layout && !*this->volume_pass.pipeline && !*this->volume_pass.upload_pipeline && !*this->volume_pass.color_upload_pipeline && this->volume_pass.frame_volumes.empty()) return;
        this->wait_device_idle_for_cleanup();
        for (FrameVolumeResources& frame_volume : this->volume_pass.frame_volumes) {
            this->destroy_host_buffer(frame_volume.densityStagingBuffer);
            this->destroy_host_buffer(frame_volume.temperatureStagingBuffer);
            this->destroy_host_buffer(frame_volume.colorStagingBuffer);
            this->destroy_volume_image(frame_volume.densityImage);
            this->destroy_volume_image(frame_volume.temperatureImage);
            this->destroy_volume_image(frame_volume.colorImage);
            frame_volume.externalDensityUploadDescriptorSets = nullptr;
            frame_volume.externalColorUploadDescriptorSets = nullptr;
            frame_volume.externalDensityUploadPending = false;
            frame_volume.externalColorUploadPending = false;
            frame_volume.hasColorChannel = false;
        }
        this->volume_pass.frame_volumes.clear();
        this->volume_pass.color_upload_pipeline        = nullptr;
        this->volume_pass.upload_pipeline              = nullptr;
        this->volume_pass.pipeline                     = nullptr;
        this->volume_pass.upload_pipeline_layout       = nullptr;
        this->volume_pass.pipeline_layout              = nullptr;
        this->volume_pass.sampler                      = nullptr;
        this->volume_pass.descriptor_sets              = nullptr;
        this->volume_pass.descriptor_pool              = nullptr;
        this->volume_pass.upload_descriptor_set_layout = nullptr;
        this->volume_pass.descriptor_set_layout        = nullptr;
        this->volume_pass.frame_count                  = 0;
    }

    void Renderer::destroy_selection_resources() noexcept {
        if (this->selection.frame_count != 0 || *this->selection.object_id_image.image || *this->selection.depth_image.image || *this->selection.mask_image.image || !this->selection.readback_buffers.empty()) this->wait_device_idle_for_cleanup();
        for (GpuBuffer& readback_buffer : this->selection.readback_buffers) this->destroy_host_buffer(readback_buffer);
        this->selection.readback_buffers.clear();
        this->destroy_image_2d(this->selection.object_id_image);
        this->destroy_image_2d(this->selection.depth_image);
        this->destroy_image_2d(this->selection.mask_image);
        this->selection.outline_pipeline              = nullptr;
        this->selection.outline_pipeline_layout       = nullptr;
        this->selection.volume_mask_pipeline          = nullptr;
        this->selection.volume_mask_pipeline_layout   = nullptr;
        this->selection.point_cloud_mask_pipeline         = nullptr;
        this->selection.point_cloud_mask_pipeline_layout  = nullptr;
        this->selection.mesh_mask_pipeline            = nullptr;
        this->selection.mesh_mask_pipeline_layout     = nullptr;
        this->selection.volume_picking_pipeline       = nullptr;
        this->selection.volume_picking_pipeline_layout = nullptr;
        this->selection.point_cloud_picking_pipeline      = nullptr;
        this->selection.point_cloud_picking_pipeline_layout = nullptr;
        this->selection.mesh_picking_pipeline         = nullptr;
        this->selection.mesh_picking_pipeline_layout  = nullptr;
        this->selection.outline_descriptor_sets       = nullptr;
        this->selection.outline_descriptor_pool       = nullptr;
        this->selection.outline_descriptor_set_layout = nullptr;
        this->selection.mask_sampler                  = nullptr;
        this->selection.frame_count                   = 0;
        this->selection.pick_requested                = false;
        this->selection.pick_pending                  = false;
        this->selection.requested_select              = false;
        this->selection.requested_additive            = false;
        this->selection.pending_select                = false;
        this->selection.pending_additive              = false;
        this->selection.requested_x                   = 0;
        this->selection.requested_y                   = 0;
        this->selection.pending_frame_index           = 0;
    }

    void Renderer::ensure_camera_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer camera resources without Vulkan handles");
        if (this->host.frame_count == 0) throw std::runtime_error("Spectra rasterizer frame count must be positive");
        if (*this->camera.descriptor_set_layout && this->camera.descriptor_sets.size() == this->host.frame_count && this->camera.uniform_buffers.size() == this->host.frame_count && this->camera.frame_count == this->host.frame_count) return;
        this->destroy_selection_resources();
        this->destroy_mesh_resources();
        this->destroy_viewport_grid_resources();
        this->destroy_point_cloud_resources();
        this->destroy_viewport_segment_resources();
        this->destroy_viewport_image_plane_resources();
        this->destroy_volume_resources();
        this->destroy_camera_resources();

        const vk::DescriptorSetLayoutBinding camera_binding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment};
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, 1u, &camera_binding};
        this->camera.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};

        const vk::DescriptorPoolSize descriptor_pool_size{vk::DescriptorType::eUniformBuffer, this->host.frame_count};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count, 1u, &descriptor_pool_size};
        this->camera.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->camera.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->camera.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->camera.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer camera descriptor sets");

        this->camera.uniform_buffers.resize(this->host.frame_count);
        for (GpuBuffer& uniform_buffer : this->camera.uniform_buffers) this->ensure_host_buffer(uniform_buffer, sizeof(CameraUniformData), vk::BufferUsageFlagBits::eUniformBuffer);

        std::vector<vk::DescriptorBufferInfo> buffer_infos{};
        std::vector<vk::WriteDescriptorSet> descriptor_writes{};
        buffer_infos.reserve(this->host.frame_count);
        descriptor_writes.reserve(this->host.frame_count);
        for (std::uint32_t frame_index = 0; frame_index < this->host.frame_count; ++frame_index) {
            buffer_infos.emplace_back(*this->camera.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData));
            descriptor_writes.emplace_back(*this->camera.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &buffer_infos.back(), nullptr);
        }
        this->host.device->updateDescriptorSets(descriptor_writes, {});
        this->camera.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_mesh_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer mesh resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer mesh pass requires camera descriptors");
        if (*this->mesh_pass.pipeline && *this->mesh_pass.transparent_pipeline && this->mesh_pass.frame_count == this->host.frame_count) return;
        this->destroy_mesh_resources();

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_mesh_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_mesh_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(RasterizerVertex), vk::VertexInputRate::eVertex};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, nx))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo opaque_depth_stencil_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo transparent_depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState opaque_color_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        constexpr vk::PipelineColorBlendAttachmentState transparent_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo opaque_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &opaque_color_blend_attachment};
        const vk::PipelineColorBlendStateCreateInfo transparent_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &transparent_color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(DrawPushConstantsData)};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
        this->mesh_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        const auto create_mesh_pipeline = [&](const vk::PipelineDepthStencilStateCreateInfo& depth_state, const vk::PipelineColorBlendStateCreateInfo& blend_state) {
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*this->mesh_pass.pipeline_layout);
            return vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };
        this->mesh_pass.pipeline = create_mesh_pipeline(opaque_depth_stencil_state, opaque_color_blend_state);
        this->mesh_pass.transparent_pipeline = create_mesh_pipeline(transparent_depth_stencil_state, transparent_color_blend_state);
        this->mesh_pass.frame_scenes.resize(this->host.frame_count);
        this->mesh_pass.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_viewport_grid_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport grid resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer viewport grid pass requires camera descriptors");
        if (*this->viewport_grid_pass.pipeline && this->viewport_grid_pass.frame_count == this->host.frame_count) return;
        this->destroy_viewport_grid_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 0u, nullptr};
        this->viewport_grid_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_viewport_grid_vertex_spv_sizeInBytes, spectra_rasterizer_viewport_grid_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_viewport_grid_fragment_spv_sizeInBytes, spectra_rasterizer_viewport_grid_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState grid_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo grid_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &grid_color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&grid_color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->viewport_grid_pass.pipeline_layout);
        this->viewport_grid_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->viewport_grid_pass.frame_count = this->host.frame_count;
    }

    void Renderer::ensure_point_cloud_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer point cloud resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer point cloud pass requires camera descriptors");
        if (*this->point_cloud_pass.pipeline && this->point_cloud_pass.frame_count == this->host.frame_count) return;
        this->destroy_point_cloud_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(PointCloudPushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
        this->point_cloud_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_point_cloud_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_point_cloud_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(PointCloudInstance), vk::VertexInputRate::eInstance};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOne,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->point_cloud_pass.pipeline_layout);
        this->point_cloud_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->point_cloud_pass.frame_count = this->host.frame_count;
        this->point_cloud_pass.frame_point_clouds.resize(this->host.frame_count);
    }

    void Renderer::ensure_viewport_segment_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport segment resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer viewport segment pass requires camera descriptors");
        if (*this->viewport_segment_pass.depth_tested_pipeline && *this->viewport_segment_pass.always_visible_pipeline && this->viewport_segment_pass.frame_count == this->host.frame_count) return;
        this->destroy_viewport_segment_resources();

        const vk::DescriptorSetLayout descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(ViewportSegmentPushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
        this->viewport_segment_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_viewport_segment_vertex_spv_sizeInBytes, spectra_rasterizer_viewport_segment_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_viewport_segment_fragment_spv_sizeInBytes, spectra_rasterizer_viewport_segment_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(ViewportSegmentInstance), vk::VertexInputRate::eInstance};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportSegmentInstance, sx))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportSegmentInstance, ex))},
            vk::VertexInputAttributeDescription{2u, 0u, vk::Format::eR32Uint, static_cast<std::uint32_t>(offsetof(ViewportSegmentInstance, flags))},
            vk::VertexInputAttributeDescription{3u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportSegmentInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_tested_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo always_visible_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        const auto create_segment_pipeline = [&](const vk::PipelineDepthStencilStateCreateInfo& depth_state) {
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*this->viewport_segment_pass.pipeline_layout);
            return vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };
        this->viewport_segment_pass.depth_tested_pipeline   = create_segment_pipeline(depth_tested_state);
        this->viewport_segment_pass.always_visible_pipeline = create_segment_pipeline(always_visible_state);
        this->viewport_segment_pass.frame_count             = this->host.frame_count;
        this->viewport_segment_pass.frame_segments.resize(this->host.frame_count);
    }

    void Renderer::ensure_viewport_voxel_grid_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport voxel grid resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer viewport voxel grid pass requires camera descriptors");
        if (*this->viewport_voxel_grid_pass.depth_tested_pipeline && *this->viewport_voxel_grid_pass.always_visible_pipeline && *this->viewport_voxel_grid_pass.compaction_pipeline && *this->viewport_voxel_grid_pass.descriptor_set_layout && *this->viewport_voxel_grid_pass.compaction_descriptor_set_layout && *this->viewport_voxel_grid_pass.descriptor_pool && this->viewport_voxel_grid_pass.frame_count == this->host.frame_count) return;
        this->destroy_viewport_voxel_grid_resources();

        const vk::DescriptorSetLayoutBinding index_buffer_binding{0u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eVertex};
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, 1u, &index_buffer_binding};
        this->viewport_voxel_grid_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};

        const std::array compaction_descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{2u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{3u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eCompute},
        };
        const vk::DescriptorSetLayoutCreateInfo compaction_descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(compaction_descriptor_bindings.size()), compaction_descriptor_bindings.data()};
        this->viewport_voxel_grid_pass.compaction_descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, compaction_descriptor_set_layout_create_info};

        constexpr std::uint32_t max_viewport_voxel_descriptor_sets = 2048u;
        constexpr std::uint32_t max_viewport_voxel_storage_descriptors = 8192u;
        const vk::DescriptorPoolSize descriptor_pool_size{vk::DescriptorType::eStorageBuffer, max_viewport_voxel_storage_descriptors};
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, max_viewport_voxel_descriptor_sets, 1u, &descriptor_pool_size};
        this->viewport_voxel_grid_pass.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};

        const std::array descriptor_set_layouts{*this->camera.descriptor_set_layout, *this->viewport_voxel_grid_pass.descriptor_set_layout};
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(ViewportVoxelGridPushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_set_layouts.size()), descriptor_set_layouts.data(), 1u, &push_constant_range};
        this->viewport_voxel_grid_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const std::array compaction_descriptor_set_layouts{*this->viewport_voxel_grid_pass.compaction_descriptor_set_layout};
        const vk::PushConstantRange compaction_push_constant_range{vk::ShaderStageFlagBits::eCompute, 0u, sizeof(ViewportVoxelGridCompactionPushConstantsData)};
        const vk::PipelineLayoutCreateInfo compaction_pipeline_layout_create_info{{}, static_cast<std::uint32_t>(compaction_descriptor_set_layouts.size()), compaction_descriptor_set_layouts.data(), 1u, &compaction_push_constant_range};
        this->viewport_voxel_grid_pass.compaction_pipeline_layout = vk::raii::PipelineLayout{*this->host.device, compaction_pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_viewport_voxel_grid_vertex_spv_sizeInBytes, spectra_rasterizer_viewport_voxel_grid_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_viewport_voxel_grid_fragment_spv_sizeInBytes, spectra_rasterizer_viewport_voxel_grid_fragment_spv};
        const vk::ShaderModuleCreateInfo compaction_shader_create_info{{}, spectra_rasterizer_viewport_voxel_grid_compact_compute_spv_sizeInBytes, spectra_rasterizer_viewport_voxel_grid_compact_compute_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const vk::raii::ShaderModule compaction_shader{*this->host.device, compaction_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };
        const vk::ComputePipelineCreateInfo compaction_pipeline_create_info{{}, vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eCompute, *compaction_shader, "main"}, *this->viewport_voxel_grid_pass.compaction_pipeline_layout};
        this->viewport_voxel_grid_pass.compaction_pipeline = vk::raii::Pipeline{*this->host.device, nullptr, compaction_pipeline_create_info};

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_tested_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo always_visible_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        const auto create_voxel_pipeline = [&](const vk::PipelineDepthStencilStateCreateInfo& depth_state) {
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*this->viewport_voxel_grid_pass.pipeline_layout);
            return vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };
        this->viewport_voxel_grid_pass.depth_tested_pipeline = create_voxel_pipeline(depth_tested_state);
        this->viewport_voxel_grid_pass.always_visible_pipeline = create_voxel_pipeline(always_visible_state);
        this->viewport_voxel_grid_pass.frame_count = this->host.frame_count;
        this->viewport_voxel_grid_pass.frame_voxel_grids.resize(this->host.frame_count);

        for (std::pair<const std::uint64_t, ViewportVoxelBufferDescriptor>& descriptor : this->viewport_voxel_grid_pass.buffer_descriptors) this->ensure_viewport_voxel_buffer_descriptor(descriptor.first, descriptor.second);
    }

    void Renderer::ensure_viewport_image_plane_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer viewport image plane resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer viewport image plane pass requires camera descriptors");
        if (*this->viewport_image_plane_pass.depth_tested_pipeline && *this->viewport_image_plane_pass.always_visible_pipeline && *this->viewport_image_plane_pass.descriptor_set_layout && *this->viewport_image_plane_pass.sampler && this->viewport_image_plane_pass.frame_count == this->host.frame_count) return;
        this->destroy_viewport_image_plane_resources();

        const std::array image_descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(image_descriptor_bindings.size()), image_descriptor_bindings.data()};
        this->viewport_image_plane_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};

        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->viewport_image_plane_pass.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::DescriptorSetLayout camera_descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::DescriptorSetLayout image_descriptor_set_layout = *this->viewport_image_plane_pass.descriptor_set_layout;
        const std::array descriptor_set_layouts{camera_descriptor_set_layout, image_descriptor_set_layout};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_set_layouts.size()), descriptor_set_layouts.data(), 0u, nullptr};
        this->viewport_image_plane_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_viewport_image_plane_vertex_spv_sizeInBytes, spectra_rasterizer_viewport_image_plane_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_viewport_image_plane_fragment_spv_sizeInBytes, spectra_rasterizer_viewport_image_plane_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::VertexInputBindingDescription vertex_binding{0u, sizeof(ViewportImagePlaneInstance), vk::VertexInputRate::eInstance};
        const std::array vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportImagePlaneInstance, model))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportImagePlaneInstance, model) + sizeof(float) * 4u)},
            vk::VertexInputAttributeDescription{2u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportImagePlaneInstance, model) + sizeof(float) * 12u)},
            vk::VertexInputAttributeDescription{3u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(ViewportImagePlaneInstance, tint))},
        };
        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{{}, 1u, &vertex_binding, static_cast<std::uint32_t>(vertex_attributes.size()), vertex_attributes.data()};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_tested_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo always_visible_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        const auto create_plane_pipeline = [&](const vk::PipelineDepthStencilStateCreateInfo& depth_state) {
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*this->viewport_image_plane_pass.pipeline_layout);
            return vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };
        this->viewport_image_plane_pass.depth_tested_pipeline   = create_plane_pipeline(depth_tested_state);
        this->viewport_image_plane_pass.always_visible_pipeline = create_plane_pipeline(always_visible_state);
        this->viewport_image_plane_pass.frame_count             = this->host.frame_count;
        this->viewport_image_plane_pass.frame_planes.resize(this->host.frame_count);
    }

    void Renderer::ensure_volume_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer volume resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer volume pass requires camera descriptors");
        if (*this->volume_pass.pipeline && this->volume_pass.frame_count == this->host.frame_count) return;
        this->destroy_volume_resources();

        const std::array descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eUniformBuffer, 1u, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{2u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{3u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{4u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(descriptor_bindings.size()), descriptor_bindings.data()};
        this->volume_pass.descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, descriptor_set_layout_create_info};
        const std::array upload_descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eStorageBuffer, 1u, vk::ShaderStageFlagBits::eCompute},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eStorageImage, 1u, vk::ShaderStageFlagBits::eCompute},
        };
        const vk::DescriptorSetLayoutCreateInfo upload_descriptor_set_layout_create_info{{}, static_cast<std::uint32_t>(upload_descriptor_bindings.size()), upload_descriptor_bindings.data()};
        this->volume_pass.upload_descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, upload_descriptor_set_layout_create_info};
        const std::array descriptor_pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eUniformBuffer, this->host.frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, this->host.frame_count * 3u},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, this->host.frame_count},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageBuffer, this->host.frame_count * 2u},
            vk::DescriptorPoolSize{vk::DescriptorType::eStorageImage, this->host.frame_count * 2u},
        };
        const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, this->host.frame_count * 3u, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
        this->volume_pass.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
        const vk::DescriptorSetLayout descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        std::vector<vk::DescriptorSetLayout> descriptor_set_layouts(this->host.frame_count, descriptor_set_layout);
        const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->volume_pass.descriptor_pool, this->host.frame_count, descriptor_set_layouts.data()};
        this->volume_pass.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
        if (this->volume_pass.descriptor_sets.size() != this->host.frame_count) throw std::runtime_error("Failed to allocate Spectra rasterizer volume descriptor sets");

        const vk::SamplerCreateInfo sampler_create_info{
            {},
            vk::Filter::eLinear,
            vk::Filter::eLinear,
            vk::SamplerMipmapMode::eLinear,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            vk::SamplerAddressMode::eClampToBorder,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->volume_pass.sampler = vk::raii::Sampler{*this->host.device, sampler_create_info};

        const vk::DescriptorSetLayout volume_descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(VolumePushConstantsData)};
        const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &volume_descriptor_set_layout, 1u, &push_constant_range};
        this->volume_pass.pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
        const vk::DescriptorSetLayout volume_upload_descriptor_set_layout = *this->volume_pass.upload_descriptor_set_layout;
        const vk::PushConstantRange upload_push_constant_range{vk::ShaderStageFlagBits::eCompute, 0u, sizeof(VolumeUploadPushConstantsData)};
        const vk::PipelineLayoutCreateInfo upload_pipeline_layout_create_info{{}, 1u, &volume_upload_descriptor_set_layout, 1u, &upload_push_constant_range};
        this->volume_pass.upload_pipeline_layout = vk::raii::PipelineLayout{*this->host.device, upload_pipeline_layout_create_info};

        const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, spectra_rasterizer_volume_vertex_spv_sizeInBytes, spectra_rasterizer_volume_vertex_spv};
        const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, spectra_rasterizer_volume_fragment_spv_sizeInBytes, spectra_rasterizer_volume_fragment_spv};
        const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
        const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
        const vk::ShaderModuleCreateInfo upload_shader_create_info{{}, spectra_rasterizer_volume_upload_compute_spv_sizeInBytes, spectra_rasterizer_volume_upload_compute_spv};
        const vk::raii::ShaderModule upload_shader{*this->host.device, upload_shader_create_info};
        const vk::PipelineShaderStageCreateInfo upload_shader_stage{{}, vk::ShaderStageFlagBits::eCompute, *upload_shader, "main"};
        const vk::ComputePipelineCreateInfo upload_pipeline_create_info{{}, upload_shader_stage, *this->volume_pass.upload_pipeline_layout};
        this->volume_pass.upload_pipeline = vk::raii::Pipeline{*this->host.device, nullptr, upload_pipeline_create_info};
        const vk::ShaderModuleCreateInfo color_upload_shader_create_info{{}, spectra_rasterizer_volume_color_upload_compute_spv_sizeInBytes, spectra_rasterizer_volume_color_upload_compute_spv};
        const vk::raii::ShaderModule color_upload_shader{*this->host.device, color_upload_shader_create_info};
        const vk::PipelineShaderStageCreateInfo color_upload_shader_stage{{}, vk::ShaderStageFlagBits::eCompute, *color_upload_shader, "main"};
        const vk::ComputePipelineCreateInfo color_upload_pipeline_create_info{{}, color_upload_shader_stage, *this->volume_pass.upload_pipeline_layout};
        this->volume_pass.color_upload_pipeline = vk::raii::Pipeline{*this->host.device, nullptr, color_upload_pipeline_create_info};
        const std::array shader_stages{
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
            vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
        };

        const vk::PipelineVertexInputStateCreateInfo vertex_input_state{};
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo depth_stencil_state{{}, VK_TRUE, VK_FALSE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        constexpr vk::PipelineColorBlendAttachmentState color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &color_blend_attachment};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        const vk::Format color_format = this->viewport.format;
        const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, this->viewport.depth_format};
        vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
        graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
        graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
        graphics_pipeline_create_info.setPStages(shader_stages.data());
        graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
        graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
        graphics_pipeline_create_info.setPViewportState(&viewport_state);
        graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
        graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
        graphics_pipeline_create_info.setPDepthStencilState(&depth_stencil_state);
        graphics_pipeline_create_info.setPColorBlendState(&color_blend_state);
        graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
        graphics_pipeline_create_info.setLayout(*this->volume_pass.pipeline_layout);
        this->volume_pass.pipeline    = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        this->volume_pass.frame_count = this->host.frame_count;
        this->volume_pass.frame_volumes.resize(this->host.frame_count);
    }

    void Renderer::ensure_selection_resources() {
        if (this->host.physical_device == nullptr || this->host.device == nullptr) throw std::runtime_error("Cannot create Spectra rasterizer selection resources without Vulkan handles");
        if (!*this->camera.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer selection pass requires camera descriptors");
        if (!*this->volume_pass.descriptor_set_layout) throw std::runtime_error("Spectra rasterizer selection pass requires volume descriptors");
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) return;
        if (*this->selection.object_id_image.image && *this->selection.depth_image.image && *this->selection.mask_image.image && *this->selection.mesh_picking_pipeline && *this->selection.outline_pipeline && this->selection.object_id_image.extent == this->viewport.extent && this->selection.frame_count == this->host.frame_count) return;
        this->destroy_selection_resources();

        this->create_image_2d(this->selection.object_id_image, this->viewport.extent, vk::Format::eR32Uint, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc, vk::ImageAspectFlagBits::eColor);
        this->create_image_2d(this->selection.depth_image, this->viewport.extent, this->viewport.depth_format, vk::ImageUsageFlagBits::eDepthStencilAttachment, vk::ImageAspectFlagBits::eDepth);
        this->create_image_2d(this->selection.mask_image, this->viewport.extent, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vk::ImageAspectFlagBits::eColor);

        const vk::SamplerCreateInfo mask_sampler_create_info{
            {},
            vk::Filter::eNearest,
            vk::Filter::eNearest,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            vk::SamplerAddressMode::eClampToEdge,
            0.0f,
            VK_FALSE,
            1.0f,
            VK_FALSE,
            vk::CompareOp::eAlways,
            0.0f,
            0.0f,
            vk::BorderColor::eFloatTransparentBlack,
            VK_FALSE,
        };
        this->selection.mask_sampler = vk::raii::Sampler{*this->host.device, mask_sampler_create_info};

        const std::array outline_descriptor_bindings{
            vk::DescriptorSetLayoutBinding{0u, vk::DescriptorType::eSampledImage, 1u, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1u, vk::DescriptorType::eSampler, 1u, vk::ShaderStageFlagBits::eFragment},
        };
        const vk::DescriptorSetLayoutCreateInfo outline_descriptor_layout_create_info{{}, static_cast<std::uint32_t>(outline_descriptor_bindings.size()), outline_descriptor_bindings.data()};
        this->selection.outline_descriptor_set_layout = vk::raii::DescriptorSetLayout{*this->host.device, outline_descriptor_layout_create_info};
        const std::array outline_descriptor_pool_sizes{
            vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1u},
            vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1u},
        };
        const vk::DescriptorPoolCreateInfo outline_descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1u, static_cast<std::uint32_t>(outline_descriptor_pool_sizes.size()), outline_descriptor_pool_sizes.data()};
        this->selection.outline_descriptor_pool = vk::raii::DescriptorPool{*this->host.device, outline_descriptor_pool_create_info};
        const vk::DescriptorSetLayout outline_descriptor_set_layout = *this->selection.outline_descriptor_set_layout;
        const vk::DescriptorSetAllocateInfo outline_descriptor_set_allocate_info{*this->selection.outline_descriptor_pool, 1u, &outline_descriptor_set_layout};
        this->selection.outline_descriptor_sets = vk::raii::DescriptorSets{*this->host.device, outline_descriptor_set_allocate_info};
        if (this->selection.outline_descriptor_sets.size() != 1u) throw std::runtime_error("Failed to allocate Spectra rasterizer selection outline descriptor set");
        const vk::DescriptorImageInfo mask_image_info{{}, *this->selection.mask_image.view, vk::ImageLayout::eShaderReadOnlyOptimal};
        const vk::DescriptorImageInfo mask_sampler_info{*this->selection.mask_sampler, {}, vk::ImageLayout::eUndefined};
        const std::array outline_descriptor_writes{
            vk::WriteDescriptorSet{*this->selection.outline_descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eSampledImage, &mask_image_info, nullptr, nullptr},
            vk::WriteDescriptorSet{*this->selection.outline_descriptor_sets.at(0), 1u, 0u, 1u, vk::DescriptorType::eSampler, &mask_sampler_info, nullptr, nullptr},
        };
        this->host.device->updateDescriptorSets(outline_descriptor_writes, {});

        const vk::DescriptorSetLayout camera_descriptor_set_layout = *this->camera.descriptor_set_layout;
        const vk::DescriptorSetLayout volume_descriptor_set_layout = *this->volume_pass.descriptor_set_layout;
        const vk::PipelineInputAssemblyStateCreateInfo input_assembly_state{{}, vk::PrimitiveTopology::eTriangleList, VK_FALSE};
        const vk::PipelineViewportStateCreateInfo viewport_state{{}, 1u, nullptr, 1u, nullptr};
        const vk::PipelineRasterizationStateCreateInfo rasterization_state{{}, VK_FALSE, VK_FALSE, vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone, vk::FrontFace::eCounterClockwise, VK_FALSE, 0.0f, 0.0f, 0.0f, 1.0f};
        const vk::PipelineMultisampleStateCreateInfo multisample_state{{}, vk::SampleCountFlagBits::e1, VK_FALSE};
        constexpr std::array dynamic_states{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
        const vk::PipelineDynamicStateCreateInfo dynamic_state{{}, static_cast<std::uint32_t>(dynamic_states.size()), dynamic_states.data()};
        constexpr vk::PipelineColorBlendAttachmentState write_color_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo write_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &write_color_blend_attachment};
        constexpr vk::PipelineColorBlendAttachmentState write_id_blend_attachment{
            VK_FALSE,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eZero,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR,
        };
        const vk::PipelineColorBlendStateCreateInfo write_id_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &write_id_blend_attachment};
        constexpr vk::PipelineColorBlendAttachmentState outline_color_blend_attachment{
            VK_TRUE,
            vk::BlendFactor::eSrcAlpha,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::BlendFactor::eOne,
            vk::BlendFactor::eOneMinusSrcAlpha,
            vk::BlendOp::eAdd,
            vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
        };
        const vk::PipelineColorBlendStateCreateInfo outline_color_blend_state{{}, VK_FALSE, vk::LogicOp::eCopy, 1u, &outline_color_blend_attachment};
        const vk::PipelineDepthStencilStateCreateInfo selection_depth_state{{}, VK_TRUE, VK_TRUE, vk::CompareOp::eLessOrEqual, VK_FALSE, VK_FALSE};
        const vk::PipelineDepthStencilStateCreateInfo no_depth_state{{}, VK_FALSE, VK_FALSE, vk::CompareOp::eAlways, VK_FALSE, VK_FALSE};

        const vk::VertexInputBindingDescription mesh_vertex_binding{0u, sizeof(RasterizerVertex), vk::VertexInputRate::eVertex};
        const std::array mesh_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, nx))},
        };
        const vk::PipelineVertexInputStateCreateInfo mesh_vertex_input_state{{}, 1u, &mesh_vertex_binding, static_cast<std::uint32_t>(mesh_vertex_attributes.size()), mesh_vertex_attributes.data()};
        const std::array mesh_selection_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, static_cast<std::uint32_t>(offsetof(RasterizerVertex, px))},
        };
        const vk::PipelineVertexInputStateCreateInfo mesh_selection_vertex_input_state{{}, 1u, &mesh_vertex_binding, static_cast<std::uint32_t>(mesh_selection_vertex_attributes.size()), mesh_selection_vertex_attributes.data()};
        const vk::VertexInputBindingDescription point_cloud_vertex_binding{0u, sizeof(PointCloudInstance), vk::VertexInputRate::eInstance};
        const std::array point_cloud_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
            vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, r))},
        };
        const vk::PipelineVertexInputStateCreateInfo point_cloud_vertex_input_state{{}, 1u, &point_cloud_vertex_binding, static_cast<std::uint32_t>(point_cloud_vertex_attributes.size()), point_cloud_vertex_attributes.data()};
        const std::array point_cloud_selection_vertex_attributes{
            vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32A32Sfloat, static_cast<std::uint32_t>(offsetof(PointCloudInstance, px))},
        };
        const vk::PipelineVertexInputStateCreateInfo point_cloud_selection_vertex_input_state{{}, 1u, &point_cloud_vertex_binding, static_cast<std::uint32_t>(point_cloud_selection_vertex_attributes.size()), point_cloud_selection_vertex_attributes.data()};
        const vk::PipelineVertexInputStateCreateInfo fullscreen_vertex_input_state{};

        const auto create_graphics_pipeline = [&](const std::size_t vertex_size, const std::uint32_t* vertex_data, const std::size_t fragment_size, const std::uint32_t* fragment_data, const vk::PipelineVertexInputStateCreateInfo& vertex_input_state, const vk::DescriptorSetLayout descriptor_set_layout, const vk::DeviceSize push_constant_size, const vk::Format color_format, const vk::Format depth_format, const vk::PipelineDepthStencilStateCreateInfo& depth_state, const vk::PipelineColorBlendStateCreateInfo& blend_state, vk::raii::PipelineLayout& pipeline_layout, vk::raii::Pipeline& pipeline) {
            const vk::ShaderModuleCreateInfo vertex_shader_create_info{{}, vertex_size, vertex_data};
            const vk::ShaderModuleCreateInfo fragment_shader_create_info{{}, fragment_size, fragment_data};
            const vk::raii::ShaderModule vertex_shader{*this->host.device, vertex_shader_create_info};
            const vk::raii::ShaderModule fragment_shader{*this->host.device, fragment_shader_create_info};
            const std::array shader_stages{
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eVertex, *vertex_shader, "main"},
                vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eFragment, *fragment_shader, "main"},
            };
            const vk::PushConstantRange push_constant_range{vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, static_cast<std::uint32_t>(push_constant_size)};
            const vk::PipelineLayoutCreateInfo pipeline_layout_create_info{{}, 1u, &descriptor_set_layout, 1u, &push_constant_range};
            pipeline_layout = vk::raii::PipelineLayout{*this->host.device, pipeline_layout_create_info};
            const vk::PipelineRenderingCreateInfo pipeline_rendering_create_info{{}, 1u, &color_format, depth_format};
            vk::GraphicsPipelineCreateInfo graphics_pipeline_create_info{};
            graphics_pipeline_create_info.setPNext(&pipeline_rendering_create_info);
            graphics_pipeline_create_info.setStageCount(static_cast<std::uint32_t>(shader_stages.size()));
            graphics_pipeline_create_info.setPStages(shader_stages.data());
            graphics_pipeline_create_info.setPVertexInputState(&vertex_input_state);
            graphics_pipeline_create_info.setPInputAssemblyState(&input_assembly_state);
            graphics_pipeline_create_info.setPViewportState(&viewport_state);
            graphics_pipeline_create_info.setPRasterizationState(&rasterization_state);
            graphics_pipeline_create_info.setPMultisampleState(&multisample_state);
            graphics_pipeline_create_info.setPDepthStencilState(&depth_state);
            graphics_pipeline_create_info.setPColorBlendState(&blend_state);
            graphics_pipeline_create_info.setPDynamicState(&dynamic_state);
            graphics_pipeline_create_info.setLayout(*pipeline_layout);
            pipeline = vk::raii::Pipeline{*this->host.device, nullptr, graphics_pipeline_create_info};
        };

        create_graphics_pipeline(spectra_rasterizer_mesh_pick_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_pick_vertex_spv, spectra_rasterizer_mesh_pick_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_pick_fragment_spv, mesh_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(SelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.mesh_picking_pipeline_layout, this->selection.mesh_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_point_cloud_pick_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_pick_vertex_spv, spectra_rasterizer_point_cloud_pick_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_pick_fragment_spv, point_cloud_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(PointCloudSelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.point_cloud_picking_pipeline_layout, this->selection.point_cloud_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_volume_pick_vertex_spv_sizeInBytes, spectra_rasterizer_volume_pick_vertex_spv, spectra_rasterizer_volume_pick_fragment_spv_sizeInBytes, spectra_rasterizer_volume_pick_fragment_spv, fullscreen_vertex_input_state, volume_descriptor_set_layout, sizeof(VolumeSelectionPushConstantsData), this->selection.object_id_image.format, this->selection.depth_image.format, selection_depth_state, write_id_blend_state, this->selection.volume_picking_pipeline_layout, this->selection.volume_picking_pipeline);
        create_graphics_pipeline(spectra_rasterizer_mesh_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_mesh_selection_mask_vertex_spv, spectra_rasterizer_mesh_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_mesh_selection_mask_fragment_spv, mesh_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(SelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.mesh_mask_pipeline_layout, this->selection.mesh_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_point_cloud_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_point_cloud_selection_mask_vertex_spv, spectra_rasterizer_point_cloud_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_point_cloud_selection_mask_fragment_spv, point_cloud_selection_vertex_input_state, camera_descriptor_set_layout, sizeof(PointCloudSelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.point_cloud_mask_pipeline_layout, this->selection.point_cloud_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_volume_selection_mask_vertex_spv_sizeInBytes, spectra_rasterizer_volume_selection_mask_vertex_spv, spectra_rasterizer_volume_selection_mask_fragment_spv_sizeInBytes, spectra_rasterizer_volume_selection_mask_fragment_spv, fullscreen_vertex_input_state, volume_descriptor_set_layout, sizeof(VolumeSelectionPushConstantsData), this->selection.mask_image.format, this->selection.depth_image.format, selection_depth_state, write_color_blend_state, this->selection.volume_mask_pipeline_layout, this->selection.volume_mask_pipeline);
        create_graphics_pipeline(spectra_rasterizer_selection_outline_vertex_spv_sizeInBytes, spectra_rasterizer_selection_outline_vertex_spv, spectra_rasterizer_selection_outline_fragment_spv_sizeInBytes, spectra_rasterizer_selection_outline_fragment_spv, fullscreen_vertex_input_state, *this->selection.outline_descriptor_set_layout, sizeof(OutlinePushConstantsData), this->viewport.format, vk::Format::eUndefined, no_depth_state, outline_color_blend_state, this->selection.outline_pipeline_layout, this->selection.outline_pipeline);

        this->selection.readback_buffers.resize(this->host.frame_count);
        for (GpuBuffer& readback_buffer : this->selection.readback_buffers) this->ensure_host_buffer(readback_buffer, sizeof(std::uint32_t), vk::BufferUsageFlagBits::eTransferDst);
        this->selection.frame_count = this->host.frame_count;
    }

    scene::Scene::PreviewMaterial Renderer::resolve_material(const std::string_view material_name) const {
        if (material_name.empty()) throw std::runtime_error("Rasterizer material name must not be empty");
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.instance->document();
        for (const scene::Scene::PreviewMaterial& material : scene->materials) {
            if (material.name == material_name) return material;
        }
        throw std::runtime_error(std::format("Rasterizer material \"{}\" does not exist", material_name));
    }

    const scene::Scene::VolumeChannel* Renderer::find_volume_channel(const scene::Scene::VolumeGrid& volume, const std::string_view channel_name) const {
        for (const scene::Scene::VolumeChannel& channel : volume.channels) {
            if (channel.name != channel_name) continue;
            const std::uint64_t expected_count = static_cast<std::uint64_t>(channel.dimensions[0]) * static_cast<std::uint64_t>(channel.dimensions[1]) * static_cast<std::uint64_t>(channel.dimensions[2]);
            if (expected_count == 0u) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" has zero dimensions", channel.name));
            switch (channel.source_kind) {
            case scene::Scene::VolumeChannelSourceKind::Values:
                if (expected_count != channel.values.size()) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" value count does not match dimensions", channel.name));
                for (const float value : channel.values) {
                    if (!std::isfinite(value)) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" contains a non-finite value", channel.name));
                }
                break;
            case scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer:
                if (!channel.values.empty()) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" external GPU source must not provide CPU values", channel.name));
                if (channel.buffer_id == 0u) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" external GPU source has no buffer id", channel.name));
                if (expected_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" byte count exceeds uint64 range", channel.name));
                if (channel.source_byte_size < expected_count * sizeof(float)) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" external GPU source byte size is too small", channel.name));
                if (channel.revision == 0u) throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" external GPU source revision must not be zero", channel.name));
                break;
            default:
                throw std::runtime_error(std::format("Rasterizer volume channel \"{}\" has unsupported source kind", channel.name));
            }
            return &channel;
        }
        return nullptr;
    }

    const scene::Scene::VolumeChannel& Renderer::require_volume_channel(const scene::Scene::VolumeGrid& volume, const std::string_view channel_name) const {
        const scene::Scene::VolumeChannel* channel = this->find_volume_channel(volume, channel_name);
        if (channel != nullptr) return *channel;
        throw std::runtime_error(std::format("Rasterizer volume \"{}\" does not contain required channel \"{}\"", volume.name, channel_name));
    }

    const scene::Scene::VolumeGrid* Renderer::select_render_volume_grid(const std::span<const scene::Scene::VolumeGrid> volumes) const {
        if (volumes.empty()) return nullptr;
        if (volumes.size() != 1u) throw std::runtime_error("Spectra rasterizer volume pass supports exactly one volume grid");
        return &volumes.front();
    }

    void Renderer::rebuild_scene_ui_cache_if_needed() {
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (this->ui.scene_ui_cache.valid && this->ui.scene_ui_cache.revision == scene_revision) return;

        const std::shared_ptr<const scene::Scene::Document> document = this->scene.instance->document();
        const scene::Scene::Timeline timeline = this->scene.instance->timeline();
        std::vector<SceneObjectRecord> objects{};
        objects.reserve(document->cameras.size() + document->lights.size() + document->meshes.size() + document->spheres.size() + document->point_clouds.size() + document->volumes.size());

        std::set<std::string_view> light_names{};
        for (const scene::Scene::PreviewLight& light : document->lights) {
            if (light.name.empty()) throw std::runtime_error("Rasterizer scene collection light name must not be empty");
            if (!light_names.insert(std::string_view{light.name}).second) throw std::runtime_error(std::format("Rasterizer scene collection light \"{}\" is duplicated", light.name));
            objects.push_back(SceneObjectRecord{
                .key                      = SceneObjectKey{SceneObjectKind::Light, light.name},
                .transform                = light.transform,
                .source                   = light.source,
                .light_kind               = light.kind,
                .light_color              = light.color,
                .light_intensity          = light.intensity,
                .light_cone_angle_degrees = light.cone_angle_degrees,
            });
        }

        const auto append_resolved_records = []<typename Item, typename MakeRecord>(const std::span<const Item> document_items, const std::span<const Item> frame_items, const std::string_view kind, MakeRecord make_record, std::vector<SceneObjectRecord>& records) {
            const auto validate_unique_names = []<typename NamedItem>(const std::span<const NamedItem> items, const std::string_view layer, const std::string_view item_kind) {
                std::set<std::string_view> names{};
                for (const NamedItem& item : items) {
                    if (item.name.empty()) throw std::runtime_error(std::format("Rasterizer scene collection {} {} name must not be empty", layer, item_kind));
                    if (!names.insert(std::string_view{item.name}).second) throw std::runtime_error(std::format("Rasterizer scene collection {} {} \"{}\" is duplicated", layer, item_kind, item.name));
                }
            };

            validate_unique_names(document_items, "document", kind);
            validate_unique_names(frame_items, "frame", kind);

            std::map<std::string_view, std::size_t> frame_indices{};
            for (std::size_t index = 0; index < frame_items.size(); ++index) frame_indices.emplace(std::string_view{frame_items[index].name}, index);

            std::set<std::string_view> document_names{};
            for (const Item& document_item : document_items) {
                document_names.insert(std::string_view{document_item.name});
                const std::map<std::string_view, std::size_t>::const_iterator frame_iter = frame_indices.find(std::string_view{document_item.name});
                if (frame_iter != frame_indices.end()) records.push_back(make_record(frame_items[frame_iter->second]));
                else records.push_back(make_record(document_item));
            }

            for (const Item& frame_item : frame_items) {
                if (document_names.contains(std::string_view{frame_item.name})) continue;
                records.push_back(make_record(frame_item));
            }
        };

        std::span<const scene::Scene::Mesh> frame_meshes{};
        std::span<const scene::Scene::Sphere> frame_spheres{};
        std::span<const scene::Scene::PointCloud> frame_point_clouds{};
        std::span<const scene::Scene::VolumeGrid> frame_volumes{};
        std::span<const scene::Scene::Camera> frame_cameras{};
        if (timeline.current_frame.has_value()) {
            frame_meshes = std::span<const scene::Scene::Mesh>{timeline.current_frame->meshes};
            frame_spheres = std::span<const scene::Scene::Sphere>{timeline.current_frame->spheres};
            frame_point_clouds = std::span<const scene::Scene::PointCloud>{timeline.current_frame->point_clouds};
            frame_volumes = std::span<const scene::Scene::VolumeGrid>{timeline.current_frame->volumes};
            frame_cameras = std::span<const scene::Scene::Camera>{timeline.current_frame->cameras};
        }

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        const auto make_camera_record = [&active_camera_name = document->active_camera_name](const scene::Scene::Camera& camera) {
            if (camera.name.empty()) throw std::runtime_error("Rasterizer scene collection camera name must not be empty");
            const scene::CameraFrame camera_frame = scene::camera_frame(camera.pose);
            std::string image_source{};
            if (camera.image.has_value()) image_source = std::format("RGBA8 {}x{}", camera.image->width, camera.image->height);
            float vertical_fov{};
            if (camera.projection.kind != scene::CameraProjectionKind::Orthographic) vertical_fov = scene::camera_projection_vertical_fov_degrees(camera.projection);
            return SceneObjectRecord{
                .key                         = SceneObjectKey{SceneObjectKind::Camera, camera.name},
                .transform                   = scene::Transform{.position = camera.pose.position, .rotation = camera.pose.orientation},
                .source                      = camera.source,
                .camera_forward              = camera_frame.forward,
                .camera_active               = camera.name == active_camera_name,
                .camera_projection_kind      = camera.projection.kind,
                .camera_vertical_fov_degrees = vertical_fov,
                .camera_image_width          = camera.projection.image_width,
                .camera_image_height         = camera.projection.image_height,
                .camera_fx                   = camera.projection.fx,
                .camera_fy                   = camera.projection.fy,
                .camera_cx                   = camera.projection.cx,
                .camera_cy                   = camera.projection.cy,
                .camera_near_plane           = camera.projection.near_plane,
                .camera_far_plane            = camera.projection.far_plane,
                .camera_image_source         = std::move(image_source),
            };
        };

        const auto make_mesh_record = [](const scene::Scene::Mesh& mesh) {
            return SceneObjectRecord{
                .key           = SceneObjectKey{SceneObjectKind::Mesh, mesh.name},
                .material_name = mesh.material_name,
                .transform     = mesh.transform,
                .source        = mesh.source,
                .dynamic       = mesh.dynamic,
                .vertex_count  = mesh.positions.size(),
                .index_count   = mesh.indices.size(),
            };
        };
        const auto make_sphere_record = [](const scene::Scene::Sphere& sphere) {
            return SceneObjectRecord{
                .key           = SceneObjectKey{SceneObjectKind::Sphere, sphere.name},
                .material_name = sphere.material_name,
                .transform     = sphere.transform,
                .source        = sphere.source,
                .dynamic       = sphere.dynamic,
                .sphere_radius = sphere.radius,
            };
        };
        const auto make_point_cloud_record = [](const scene::Scene::PointCloud& point_cloud) {
            if (point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::Values && point_cloud.radii.size() != point_cloud.positions.size()) throw std::runtime_error(std::format("Rasterizer scene collection point cloud \"{}\" radius count does not match point count", point_cloud.name));
            float minimum_radius{};
            float maximum_radius{};
            if (!point_cloud.radii.empty()) {
                const auto radius_range = std::ranges::minmax_element(point_cloud.radii);
                minimum_radius = *radius_range.min;
                maximum_radius = *radius_range.max;
            }
            return SceneObjectRecord{
                .key            = SceneObjectKey{SceneObjectKind::PointCloud, point_cloud.name},
                .material_name  = point_cloud.material_name,
                .transform      = point_cloud.transform,
                .source         = point_cloud.source,
                .dynamic        = point_cloud.dynamic,
                .point_count    = point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::ExternalGpuBuffer ? point_cloud.point_count : point_cloud.positions.size(),
                .minimum_radius = minimum_radius,
                .maximum_radius = maximum_radius,
            };
        };
        const auto make_volume_record = [](const scene::Scene::VolumeGrid& volume) {
            SceneObjectRecord record{
                .key           = SceneObjectKey{SceneObjectKind::VolumeGrid, volume.name},
                .material_name = volume.material_name,
                .source        = volume.source,
                .dynamic       = volume.dynamic,
                .dimensions    = volume.dimensions,
                .origin        = volume.origin,
                .voxel_size    = volume.voxel_size,
            };
            record.volume_channels.reserve(volume.channels.size());
            for (const scene::Scene::VolumeChannel& channel : volume.channels) {
                if (channel.name.empty()) throw std::runtime_error(std::format("Rasterizer scene collection volume \"{}\" channel name must not be empty", volume.name));
                const std::uint64_t logical_value_count = static_cast<std::uint64_t>(channel.dimensions[0]) * static_cast<std::uint64_t>(channel.dimensions[1]) * static_cast<std::uint64_t>(channel.dimensions[2]);
                record.volume_channels.push_back(SceneVolumeChannelSummary{
                    .name        = channel.name,
                    .dimensions  = channel.dimensions,
                    .value_count = channel.source_kind == scene::Scene::VolumeChannelSourceKind::Values ? channel.values.size() : logical_value_count,
                });
            }
            return record;
        };

        append_resolved_records(std::span<const scene::Scene::Camera>{document->cameras}, frame_cameras, "camera", make_camera_record, objects);
        append_resolved_records(std::span<const scene::Scene::Mesh>{document->meshes}, frame_meshes, "mesh", make_mesh_record, objects);
        append_resolved_records(std::span<const scene::Scene::Sphere>{document->spheres}, frame_spheres, "sphere", make_sphere_record, objects);
        append_resolved_records(std::span<const scene::Scene::PointCloud>{document->point_clouds}, frame_point_clouds, "point cloud", make_point_cloud_record, objects);
        append_resolved_records(std::span<const scene::Scene::VolumeGrid>{document->volumes}, frame_volumes, "volume", make_volume_record, objects);

        this->ui.scene_ui_cache.revision = scene_revision;
        this->ui.scene_ui_cache.objects = std::move(objects);
        this->ui.scene_ui_cache.valid = true;
        this->prune_scene_selection_to_cache();
    }

    void Renderer::prune_scene_selection_to_cache() {
        for (std::set<SceneObjectKey>::iterator iter = this->selection.selected_scene_objects.begin(); iter != this->selection.selected_scene_objects.end();) {
            if (this->scene_object_record(*iter) != nullptr) {
                ++iter;
                continue;
            }
            iter = this->selection.selected_scene_objects.erase(iter);
        }
        if (this->selection.active_scene_object.has_value() && this->scene_object_record(*this->selection.active_scene_object) == nullptr) this->selection.active_scene_object.reset();
        if (this->selection.active_scene_object.has_value() && !this->selection.selected_scene_objects.contains(*this->selection.active_scene_object)) this->selection.active_scene_object.reset();
        if (!this->selection.active_scene_object.has_value() && !this->selection.selected_scene_objects.empty()) this->selection.active_scene_object = *this->selection.selected_scene_objects.begin();
        this->sync_renderable_selection_from_scene_selection();
    }

    const Renderer::SceneObjectRecord* Renderer::scene_object_record(const SceneObjectKey& key) const {
        for (const SceneObjectRecord& object : this->ui.scene_ui_cache.objects) {
            if (object.key == key) return &object;
        }
        return nullptr;
    }

    std::optional<Renderer::ObjectKey> Renderer::renderable_key_for_scene_object(const SceneObjectKey& key) const {
        switch (key.kind) {
        case SceneObjectKind::Mesh:
        case SceneObjectKind::Sphere: return ObjectKey{SelectableObjectKind::Mesh, key.name};
        case SceneObjectKind::PointCloud: return ObjectKey{SelectableObjectKind::PointCloud, key.name};
        case SceneObjectKind::VolumeGrid: return ObjectKey{SelectableObjectKind::VolumeGrid, key.name};
        case SceneObjectKind::Camera:
        case SceneObjectKind::Light: return std::nullopt;
        }
        throw std::runtime_error("Unknown Spectra rasterizer scene object kind");
    }

    Renderer::SceneObjectKey Renderer::scene_object_key_for_renderable(const ObjectKey& key) const {
        switch (key.kind) {
        case SelectableObjectKind::Mesh:
            for (const SceneObjectRecord& object : this->ui.scene_ui_cache.objects) {
                if (object.key.kind == SceneObjectKind::Sphere && object.key.name == key.name) return object.key;
            }
            return SceneObjectKey{SceneObjectKind::Mesh, key.name};
        case SelectableObjectKind::PointCloud: return SceneObjectKey{SceneObjectKind::PointCloud, key.name};
        case SelectableObjectKind::VolumeGrid: return SceneObjectKey{SceneObjectKind::VolumeGrid, key.name};
        }
        throw std::runtime_error("Unknown Spectra rasterizer selectable object kind");
    }

    void Renderer::sync_renderable_selection_from_scene_selection() {
        this->selection.selected_objects.clear();
        this->selection.active_object.reset();
        for (const SceneObjectKey& key : this->selection.selected_scene_objects) {
            const std::optional<ObjectKey> renderable_key = this->renderable_key_for_scene_object(key);
            if (!renderable_key.has_value() || !this->selection.object_ids.contains(*renderable_key)) continue;
            this->selection.selected_objects.insert(*renderable_key);
        }
        if (this->selection.active_scene_object.has_value()) {
            const std::optional<ObjectKey> renderable_key = this->renderable_key_for_scene_object(*this->selection.active_scene_object);
            if (renderable_key.has_value() && this->selection.object_ids.contains(*renderable_key)) this->selection.active_object = *renderable_key;
        }
        if (!this->selection.active_object.has_value() && !this->selection.selected_objects.empty()) this->selection.active_object = *this->selection.selected_objects.begin();
    }

    void Renderer::select_scene_object(const SceneObjectKey& key, const bool additive) {
        if (this->scene_object_record(key) == nullptr) throw std::runtime_error(std::format("Spectra rasterizer scene object \"{}\" is not available for selection", key.name));
        if (additive) {
            if (this->selection.selected_scene_objects.contains(key)) {
                this->selection.selected_scene_objects.erase(key);
                if (this->selection.active_scene_object.has_value() && *this->selection.active_scene_object == key) this->selection.active_scene_object.reset();
            } else {
                this->selection.selected_scene_objects.insert(key);
                this->selection.active_scene_object = key;
            }
        } else {
            this->selection.selected_scene_objects.clear();
            this->selection.selected_scene_objects.insert(key);
            this->selection.active_scene_object = key;
        }
        if (!this->selection.active_scene_object.has_value() && !this->selection.selected_scene_objects.empty()) this->selection.active_scene_object = *this->selection.selected_scene_objects.begin();
        this->sync_renderable_selection_from_scene_selection();
    }

    bool Renderer::scene_object_selected(const SceneObjectKey& key) const {
        return this->selection.selected_scene_objects.contains(key);
    }

    bool Renderer::scene_object_active(const SceneObjectKey& key) const {
        return this->selection.active_scene_object.has_value() && *this->selection.active_scene_object == key;
    }

    std::string Renderer::raw_scene_object_name(const SceneObjectRecord& object) const {
        return object.key.name;
    }

    std::string Renderer::compact_scene_object_name(const SceneObjectRecord& object) const {
        const std::optional<std::string_view> shape_token = pbrt_shape_token(object.key.name);
        if (shape_token.has_value()) {
            switch (object.key.kind) {
            case SceneObjectKind::Camera: return std::format("camera · {}", *shape_token);
            case SceneObjectKind::Light: return std::format("{} · {}", compact_preview_light_kind_name(object.light_kind), *shape_token);
            case SceneObjectKind::Mesh:
            case SceneObjectKind::Sphere:
            case SceneObjectKind::PointCloud:
            case SceneObjectKind::VolumeGrid:
                if (!object.material_name.empty()) return std::format("{} · {}", object.material_name, *shape_token);
                break;
            }
            const char* kind_text = "object";
            switch (object.key.kind) {
            case SceneObjectKind::Camera: kind_text = "camera"; break;
            case SceneObjectKind::Light: kind_text = "light"; break;
            case SceneObjectKind::Mesh: kind_text = "mesh"; break;
            case SceneObjectKind::Sphere: kind_text = "sphere"; break;
            case SceneObjectKind::PointCloud: kind_text = "point-cloud"; break;
            case SceneObjectKind::VolumeGrid: kind_text = "volume"; break;
            }
            return std::format("{} · {}", kind_text, *shape_token);
        }
        return compact_object_identifier(object.key.name);
    }

    std::string Renderer::compact_scene_object_name(const SceneObjectKey& key) const {
        const SceneObjectRecord* object = this->scene_object_record(key);
        if (object != nullptr) return this->compact_scene_object_name(*object);
        return compact_object_identifier(key.name);
    }

    std::string Renderer::compact_source_location(const scene::Scene::SourceLocation& source) const {
        if (source.filename.empty()) return "<generated>";
        return std::format("{}:{}:{}", compact_filesystem_path(source.filename), source.line, source.column);
    }

    std::string Renderer::scene_object_label(const SceneObjectKey& key) const {
        const char* kind_text = "Object";
        switch (key.kind) {
        case SceneObjectKind::Camera: kind_text = "Camera"; break;
        case SceneObjectKind::Light: kind_text = "Light"; break;
        case SceneObjectKind::Mesh: kind_text = "Mesh"; break;
        case SceneObjectKind::Sphere: kind_text = "Sphere"; break;
        case SceneObjectKind::PointCloud: kind_text = "Point Cloud"; break;
        case SceneObjectKind::VolumeGrid: kind_text = "Volume"; break;
        }
        return std::format("{} | {}", kind_text, this->compact_scene_object_name(key));
    }

    std::string Renderer::scene_selection_summary() const {
        if (this->selection.active_scene_object.has_value()) return std::format("{} selected | active {}", this->selection.selected_scene_objects.size(), this->scene_object_label(*this->selection.active_scene_object));
        if (this->selection.hovered_object.has_value()) return std::format("hover {}", this->object_label(*this->selection.hovered_object));
        return "No selection";
    }

    void Renderer::rebuild_selection_registry_if_needed() {
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (this->selection.registry_valid && this->selection.registry_revision == scene_revision) return;
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        this->selection.object_ids.clear();
        this->selection.objects_by_id.clear();
        std::set<ObjectKey> unique_keys{};
        std::uint32_t next_id = 1u;
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (mesh.positions.empty()) continue;
            this->register_selectable_object(SelectableObjectKind::Mesh, mesh.name, unique_keys, next_id);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::Values && point_cloud.positions.empty()) continue;
            this->register_selectable_object(SelectableObjectKind::PointCloud, point_cloud.name, unique_keys, next_id);
        }
        const scene::Scene::VolumeGrid* volume = this->select_render_volume_grid(resolved_frame.volumes);
        if (volume != nullptr) this->register_selectable_object(SelectableObjectKind::VolumeGrid, volume->name, unique_keys, next_id);
        this->selection.registry_revision = scene_revision;
        this->selection.registry_valid = true;
        this->prune_selection_to_registry();
        this->sync_renderable_selection_from_scene_selection();
    }

    void Renderer::register_selectable_object(const SelectableObjectKind kind, const std::string_view name, std::set<ObjectKey>& unique_keys, std::uint32_t& next_id) {
        if (name.empty()) throw std::runtime_error("Spectra rasterizer selectable objects must have non-empty names");
        if (next_id == std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("Spectra rasterizer selectable object id range is exhausted");
        ObjectKey key{kind, std::string{name}};
        const bool inserted = unique_keys.insert(key).second;
        if (!inserted) throw std::runtime_error(std::format("Spectra rasterizer selectable object name \"{}\" is duplicated within its object kind", name));
        const std::uint32_t object_id = next_id++;
        this->selection.object_ids.emplace(key, object_id);
        this->selection.objects_by_id.emplace(object_id, std::move(key));
    }

    std::uint32_t Renderer::object_id_for(const ObjectKey& key) const {
        const std::map<ObjectKey, std::uint32_t>::const_iterator iter = this->selection.object_ids.find(key);
        if (iter == this->selection.object_ids.end()) throw std::runtime_error(std::format("Spectra rasterizer object \"{}\" is not registered for selection", key.name));
        return iter->second;
    }

    const Renderer::ObjectKey* Renderer::object_for_id(const std::uint32_t object_id) const {
        if (object_id == 0u) return nullptr;
        const std::map<std::uint32_t, ObjectKey>::const_iterator iter = this->selection.objects_by_id.find(object_id);
        if (iter == this->selection.objects_by_id.end()) throw std::runtime_error(std::format("Spectra rasterizer picking returned unknown object id {}", object_id));
        return &iter->second;
    }

    void Renderer::prune_selection_to_registry() {
        for (std::set<ObjectKey>::iterator iter = this->selection.selected_objects.begin(); iter != this->selection.selected_objects.end();) {
            if (this->selection.object_ids.contains(*iter)) {
                ++iter;
                continue;
            }
            iter = this->selection.selected_objects.erase(iter);
        }
        if (this->selection.active_object.has_value() && !this->selection.object_ids.contains(*this->selection.active_object)) this->selection.active_object.reset();
        if (this->selection.hovered_object.has_value() && !this->selection.object_ids.contains(*this->selection.hovered_object)) this->selection.hovered_object.reset();
        if (!this->selection.active_object.has_value() && !this->selection.selected_objects.empty()) this->selection.active_object = *this->selection.selected_objects.begin();
        this->sync_renderable_selection_from_scene_selection();
    }

    bool Renderer::object_selected(const ObjectKey& key) const {
        return this->selection.selected_objects.contains(key);
    }

    bool Renderer::object_hovered(const ObjectKey& key) const {
        return this->selection.hovered_object.has_value() && *this->selection.hovered_object == key;
    }

    bool Renderer::object_active(const ObjectKey& key) const {
        return this->selection.active_object.has_value() && *this->selection.active_object == key;
    }

    std::array<float, 4> Renderer::selection_mask_color(const ObjectKey& key) const {
        if (this->object_active(key)) return {0.0f, 1.0f, 1.0f, 1.0f};
        if (this->object_selected(key)) return {0.0f, 1.0f, 0.0f, 1.0f};
        if (this->object_hovered(key)) return {1.0f, 0.0f, 0.0f, 1.0f};
        return {};
    }

    std::string Renderer::object_label(const ObjectKey& key) const {
        const char* kind_text = "Object";
        switch (key.kind) {
        case SelectableObjectKind::Mesh: kind_text = "Mesh"; break;
        case SelectableObjectKind::PointCloud: kind_text = "Point Cloud"; break;
        case SelectableObjectKind::VolumeGrid: kind_text = "Volume"; break;
        }
        const SceneObjectKey scene_key = this->scene_object_key_for_renderable(key);
        const SceneObjectRecord* object = this->scene_object_record(scene_key);
        const std::string name = object != nullptr ? this->compact_scene_object_name(*object) : compact_object_identifier(key.name);
        return std::format("{} | {}", kind_text, name);
    }

    void Renderer::clear_selection() {
        this->selection.selected_scene_objects.clear();
        this->selection.active_scene_object.reset();
        this->selection.selected_objects.clear();
        this->selection.active_object.reset();
        this->selection.hovered_object.reset();
        this->selection.pick_requested = false;
        this->selection.pick_pending = false;
    }

    void Renderer::apply_pick_result(const std::uint32_t object_id, const bool select, const bool additive) {
        const ObjectKey* key = this->object_for_id(object_id);
        if (key == nullptr) {
            this->selection.hovered_object.reset();
            if (select && !additive) {
                this->selection.selected_scene_objects.clear();
                this->selection.active_scene_object.reset();
                this->selection.selected_objects.clear();
                this->selection.active_object.reset();
            }
            return;
        }
        this->selection.hovered_object = *key;
        if (!select) return;
        this->rebuild_scene_ui_cache_if_needed();
        this->select_scene_object(this->scene_object_key_for_renderable(*key), additive);
    }

    void Renderer::upload_scene_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer frame scene index is out of range");
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (frame_scene.uploadedRevision == scene_revision) return;

        std::vector<RasterizerVertex> vertices{};
        std::vector<std::uint32_t> indices{};
        std::vector<RenderDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (mesh.positions.empty()) continue;
            const scene::Scene::PreviewMaterial material = this->resolve_material(mesh.material_name);
            require_surface_material(material, mesh.name);
            if (mesh.normals.size() != mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide one normal per position", mesh.name));
            if (mesh.indices.empty() || mesh.indices.size() % 3u != 0u) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" must provide triangle indices", mesh.name));
            if (vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer vertex count exceeds uint32 range");
            const std::uint32_t vertex_offset = static_cast<std::uint32_t>(vertices.size());
            scene::Vector3 minimum{};
            scene::Vector3 maximum{};
            bool bounds_valid{false};
            vertices.reserve(vertices.size() + mesh.positions.size());
            for (std::size_t vertex_index = 0; vertex_index < mesh.positions.size(); ++vertex_index) {
                const scene::Vector3 position = mesh.positions.at(vertex_index);
                const scene::Vector3 normal   = mesh.normals.at(vertex_index);
                if (!finite_scene_vector(position)) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" contains a non-finite position", mesh.name));
                if (!finite_scene_vector(normal)) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" contains a non-finite normal", mesh.name));
                if (!bounds_valid) {
                    minimum      = position;
                    maximum      = position;
                    bounds_valid = true;
                } else {
                    minimum.x = std::min(minimum.x, position.x);
                    minimum.y = std::min(minimum.y, position.y);
                    minimum.z = std::min(minimum.z, position.z);
                    maximum.x = std::max(maximum.x, position.x);
                    maximum.y = std::max(maximum.y, position.y);
                    maximum.z = std::max(maximum.z, position.z);
                }
                vertices.push_back(RasterizerVertex{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .nx = normal.x,
                    .ny = normal.y,
                    .nz = normal.z,
                });
            }
            const std::uint32_t first_index = static_cast<std::uint32_t>(indices.size());
            indices.reserve(indices.size() + mesh.indices.size());
            for (const std::uint32_t index : mesh.indices) {
                if (index >= mesh.positions.size()) throw std::runtime_error(std::format("Rasterizer mesh \"{}\" has an out-of-range index", mesh.name));
                indices.push_back(vertex_offset + index);
            }
            const ObjectKey object_key{SelectableObjectKind::Mesh, mesh.name};
            draw_commands.push_back(RenderDrawCommand{
                .objectKey  = object_key,
                .objectId   = this->object_id_for(object_key),
                .firstIndex = first_index,
                .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
                .sortPoint  = scene::Vector3{(minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f, (minimum.z + maximum.z) * 0.5f},
                .transform  = mesh.transform,
                .material   = material,
            });
        }

        if (!vertices.empty()) {
            const vk::DeviceSize vertex_bytes = static_cast<vk::DeviceSize>(vertices.size() * sizeof(RasterizerVertex));
            const vk::DeviceSize index_bytes  = static_cast<vk::DeviceSize>(indices.size() * sizeof(std::uint32_t));
            this->ensure_host_buffer(frame_scene.vertexBuffer, vertex_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            this->ensure_host_buffer(frame_scene.indexBuffer, index_bytes, vk::BufferUsageFlagBits::eIndexBuffer);
            std::memcpy(frame_scene.vertexBuffer.mapped, vertices.data(), static_cast<std::size_t>(vertex_bytes));
            std::memcpy(frame_scene.indexBuffer.mapped, indices.data(), static_cast<std::size_t>(index_bytes));
        }
        frame_scene.drawCommands     = std::move(draw_commands);
        frame_scene.uploadedRevision = scene_revision;
    }

    void Renderer::upload_point_cloud_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->point_cloud_pass.frame_point_clouds.size()) throw std::runtime_error("Spectra rasterizer point cloud frame index is out of range");
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (frame_point_cloud.uploadedRevision == scene_revision) return;

        std::vector<PointCloudInstance> instances{};
        std::vector<PointCloudDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            const scene::Scene::PreviewMaterial material = this->resolve_material(point_cloud.material_name);
            require_point_glyph_material(material, point_cloud.name);
            const ObjectKey object_key{SelectableObjectKind::PointCloud, point_cloud.name};
            if (point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::ExternalGpuBuffer) {
                if (point_cloud.point_count == 0u) continue;
                if (point_cloud.point_count > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" point count exceeds uint32 draw range", point_cloud.name));
                const ExternalStorageBuffer& resource = this->external_storage_buffer(point_cloud.buffer_id, std::format("Rasterizer point cloud \"{}\"", point_cloud.name));
                if (resource.kind != scene::GpuBufferKindPointCloud) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" source buffer has unexpected kind {}", point_cloud.name, resource.kind));
                if (resource.byte_size < point_cloud.point_count * scene::PointCloudExternalPointBytes) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" source buffer is too small", point_cloud.name));
                draw_commands.push_back(PointCloudDrawCommand{
                    .objectKey     = object_key,
                    .objectId      = this->object_id_for(object_key),
                    .sourceKind    = point_cloud.source_kind,
                    .bufferId      = point_cloud.buffer_id,
                    .firstInstance = 0u,
                    .instanceCount = static_cast<std::uint32_t>(point_cloud.point_count),
                    .transform     = point_cloud.transform,
                });
                continue;
            }
            if (point_cloud.positions.empty()) continue;
            if (point_cloud.radii.size() != point_cloud.positions.size()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" must provide one radius per position", point_cloud.name));
            if (point_cloud.colors.size() != point_cloud.positions.size()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" must provide one color per position", point_cloud.name));
            if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer point cloud instance count exceeds uint32 range");
            const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
            const spectra::rasterizer::math::Matrix4 transform = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            const float emission_red = material.emission_color.x * material.emission_strength;
            const float emission_green = material.emission_color.y * material.emission_strength;
            const float emission_blue = material.emission_color.z * material.emission_strength;
            instances.reserve(instances.size() + point_cloud.positions.size());
            for (std::size_t point_index = 0; point_index < point_cloud.positions.size(); ++point_index) {
                if (!std::isfinite(point_cloud.radii.at(point_index)) || point_cloud.radii.at(point_index) <= 0.0f) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains an invalid radius", point_cloud.name));
                if (!finite_scene_vector(point_cloud.positions.at(point_index))) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains a non-finite position", point_cloud.name));
                const scene::Vector4 color = point_cloud.colors.at(point_index);
                if (!finite_scene_vector(color)) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains a non-finite color", point_cloud.name));
                if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || color.w < 0.0f || color.w > 1.0f) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" contains an invalid color", point_cloud.name));
                const spectra::rasterizer::math::Vector3 position = spectra::rasterizer::math::transform_point(transform, to_render_vector(point_cloud.positions.at(point_index)));
                instances.push_back(PointCloudInstance{
                    .px = position.x,
                    .py = position.y,
                    .pz = position.z,
                    .radius = point_cloud.radii.at(point_index),
                    .r = color.x * material.base_color.x + emission_red,
                    .g = color.y * material.base_color.y + emission_green,
                    .b = color.z * material.base_color.z + emission_blue,
                    .a = color.w * material.base_color.w,
                });
            }
            draw_commands.push_back(PointCloudDrawCommand{
                .objectKey     = object_key,
                .objectId      = this->object_id_for(object_key),
                .sourceKind    = point_cloud.source_kind,
                .firstInstance = first_instance,
                .instanceCount = static_cast<std::uint32_t>(point_cloud.positions.size()),
                .transform     = {},
            });
        }

        frame_point_cloud.drawCommands = std::move(draw_commands);
        if (!instances.empty()) {
            const vk::DeviceSize instance_bytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(PointCloudInstance));
            this->ensure_host_buffer(frame_point_cloud.instanceBuffer, instance_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            std::memcpy(frame_point_cloud.instanceBuffer.mapped, instances.data(), static_cast<std::size_t>(instance_bytes));
        }
        frame_point_cloud.uploadedRevision = scene_revision;
    }

    void Renderer::upload_viewport_segment_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->viewport_segment_pass.frame_segments.size()) throw std::runtime_error("Spectra rasterizer viewport segment frame index is out of range");
        FrameViewportSegmentResources& frame_segments = this->viewport_segment_pass.frame_segments.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        const ViewportDebugUploadKey upload_key{.scene_revision = scene_revision, .settings_revision = this->viewport.camera_visual_revision};
        if (frame_segments.uploadedKey == upload_key) return;

        std::vector<ViewportSegmentInstance> instances{};
        std::vector<ViewportSegmentDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        const auto append_segment_instance = [&instances](const spectra::rasterizer::math::Vector3 start, const spectra::rasterizer::math::Vector3 end, const scene::Vector4 color, const float width, const scene::Scene::ViewportSegmentWidthMode width_mode, const std::string_view context) {
            const spectra::rasterizer::math::Vector3 delta = end - start;
            if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z) || spectra::rasterizer::math::dot(delta, delta) <= 0.0f) throw std::runtime_error(std::format("{} contains an invalid transformed segment", context));
            instances.push_back(ViewportSegmentInstance{
                .sx = start.x,
                .sy = start.y,
                .sz = start.z,
                .width = width,
                .ex = end.x,
                .ey = end.y,
                .ez = end.z,
                .flags = static_cast<std::uint32_t>(width_mode),
                .r = color.x,
                .g = color.y,
                .b = color.z,
                .a = color.w,
            });
        };
        for (const scene::Scene::ViewportSegmentSet& segment_set : resolved_frame.debug_attachments.viewport_segment_sets) {
            if (segment_set.source_kind == scene::Scene::ViewportSegmentSet::SourceKind::ExternalGpuBuffer) {
                if (segment_set.segment_count == 0u) continue;
                if (segment_set.segment_count > std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" segment count exceeds uint32 draw range", segment_set.name));
                const ExternalStorageBuffer& resource = this->external_storage_buffer(segment_set.buffer_id, std::format("Rasterizer viewport segment set \"{}\"", segment_set.name));
                if (resource.kind != scene::GpuBufferKindViewportSegmentSet) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" source buffer has unexpected kind {}", segment_set.name, resource.kind));
                if (resource.byte_size < segment_set.segment_count * scene::ViewportSegmentExternalSegmentBytes) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" source buffer is too small", segment_set.name));
                draw_commands.push_back(ViewportSegmentDrawCommand{
                    .sourceKind    = segment_set.source_kind,
                    .bufferId      = segment_set.buffer_id,
                    .firstInstance = 0u,
                    .instanceCount = static_cast<std::uint32_t>(segment_set.segment_count),
                    .depthMode     = segment_set.depth_mode,
                    .transform     = segment_set.transform,
                });
                continue;
            }
            if (segment_set.segments.empty()) continue;
            if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer viewport segment instance count exceeds uint32 range");
            const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
            const spectra::rasterizer::math::Matrix4 transform = spectra::rasterizer::math::transform_matrix(to_render_transform(segment_set.transform));
            instances.reserve(instances.size() + segment_set.segments.size());
            for (std::size_t segment_index = 0u; segment_index < segment_set.segments.size(); ++segment_index) {
                const scene::Scene::ViewportSegment& segment = segment_set.segments.at(segment_index);
                const float width = segment_set.widths.empty() ? segment_set.width : segment_set.widths.at(segment_index);
                if (!std::isfinite(width) || width <= 0.0f) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" contains an invalid width", segment_set.name));
                const scene::Vector4 color = segment_set.colors.empty() ? scene::Vector4{1.0f, 1.0f, 1.0f, 0.75f} : segment_set.colors.at(segment_index);
                if (!finite_scene_vector(color)) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" contains a non-finite color", segment_set.name));
                if (color.x < 0.0f || color.y < 0.0f || color.z < 0.0f || color.w < 0.0f || color.w > 1.0f) throw std::runtime_error(std::format("Rasterizer viewport segment set \"{}\" contains an invalid color", segment_set.name));
                const spectra::rasterizer::math::Vector3 start = spectra::rasterizer::math::transform_point(transform, to_render_vector(segment.start));
                const spectra::rasterizer::math::Vector3 end = spectra::rasterizer::math::transform_point(transform, to_render_vector(segment.end));
                append_segment_instance(start, end, color, width, segment_set.width_mode, std::format("Rasterizer viewport segment set \"{}\"", segment_set.name));
            }
            draw_commands.push_back(ViewportSegmentDrawCommand{
                .sourceKind    = segment_set.source_kind,
                .firstInstance = first_instance,
                .instanceCount = static_cast<std::uint32_t>(segment_set.segments.size()),
                .depthMode     = segment_set.depth_mode,
                .transform     = {},
            });
        }
        if (this->viewport.camera_visual_frustums_visible) {
            if (!std::isfinite(this->viewport.camera_visual_width) || this->viewport.camera_visual_width <= 0.0f) throw std::runtime_error("Rasterizer camera visual width must be finite and positive");
            if (!std::isfinite(this->viewport.camera_visual_near) || !std::isfinite(this->viewport.camera_visual_far) || this->viewport.camera_visual_near <= 0.0f || this->viewport.camera_visual_far <= this->viewport.camera_visual_near) throw std::runtime_error("Rasterizer camera visual range must satisfy far > near > 0");
            for (std::size_t camera_index = 0u; camera_index < resolved_frame.cameras.size(); ++camera_index) {
                if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer viewport segment instance count exceeds uint32 range");
                if (instances.size() + 12u > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer camera frustum edge count exceeds uint32 range");
                const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
                const scene::Scene::Camera& camera = resolved_frame.cameras.at(camera_index);
                const float t = resolved_frame.cameras.size() > 1u ? static_cast<float>(camera_index) / static_cast<float>(resolved_frame.cameras.size() - 1u) : 0.0f;
                const scene::Vector4 color{0.12f + 0.72f * t, 0.82f, 1.0f - 0.55f * t, 0.52f};
                const CameraVisualPlanes planes = camera_visual_planes(camera, this->viewport.camera_visual_near, this->viewport.camera_visual_far);
                const std::array<std::array<const scene::Vector3*, 2>, 12> edges{{
                    {&planes.near_corners.at(0u), &planes.near_corners.at(1u)},
                    {&planes.near_corners.at(1u), &planes.near_corners.at(2u)},
                    {&planes.near_corners.at(2u), &planes.near_corners.at(3u)},
                    {&planes.near_corners.at(3u), &planes.near_corners.at(0u)},
                    {&planes.far_corners.at(0u), &planes.far_corners.at(1u)},
                    {&planes.far_corners.at(1u), &planes.far_corners.at(2u)},
                    {&planes.far_corners.at(2u), &planes.far_corners.at(3u)},
                    {&planes.far_corners.at(3u), &planes.far_corners.at(0u)},
                    {&planes.near_corners.at(0u), &planes.far_corners.at(0u)},
                    {&planes.near_corners.at(1u), &planes.far_corners.at(1u)},
                    {&planes.near_corners.at(2u), &planes.far_corners.at(2u)},
                    {&planes.near_corners.at(3u), &planes.far_corners.at(3u)},
                }};
                instances.reserve(instances.size() + edges.size());
                for (const std::array<const scene::Vector3*, 2>& edge : edges) {
                    append_segment_instance(
                        to_render_vector(*edge.at(0u)),
                        to_render_vector(*edge.at(1u)),
                        color,
                        this->viewport.camera_visual_width,
                        scene::Scene::ViewportSegmentWidthMode::Screen,
                        std::format("Rasterizer camera visual \"{}\"", camera.name)
                    );
                }
                draw_commands.push_back(ViewportSegmentDrawCommand{
                    .sourceKind    = scene::Scene::ViewportSegmentSet::SourceKind::Values,
                    .firstInstance = first_instance,
                    .instanceCount = static_cast<std::uint32_t>(edges.size()),
                    .depthMode     = scene::Scene::ViewportSegmentDepthMode::AlwaysVisible,
                    .transform     = {},
                });
            }
        }
        if (this->viewport.camera_visual_axes_visible) {
            if (!std::isfinite(this->viewport.camera_visual_width) || this->viewport.camera_visual_width <= 0.0f) throw std::runtime_error("Rasterizer camera visual axis width must be finite and positive");
            if (!std::isfinite(this->viewport.camera_visual_far) || this->viewport.camera_visual_far <= 0.0f) throw std::runtime_error("Rasterizer camera visual axis length must be finite and positive");
            const float axis_length = std::max(0.03f, this->viewport.camera_visual_far * 0.35f);
            const float axis_width = std::max(2.0f, this->viewport.camera_visual_width * 1.35f);
            constexpr std::array axis_colors{
                scene::Vector4{1.0f, 0.16f, 0.12f, 0.95f},
                scene::Vector4{0.20f, 0.92f, 0.28f, 0.95f},
                scene::Vector4{0.25f, 0.52f, 1.0f, 0.95f},
            };
            for (const scene::Scene::Camera& camera : resolved_frame.cameras) {
                if (instances.size() + 3u > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer camera axis segment count exceeds uint32 range");
                const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
                const scene::CameraFrame frame = scene::camera_frame(camera.pose);
                const std::array axes{frame.right, frame.down, frame.forward};
                for (std::size_t axis_index = 0u; axis_index < axes.size(); ++axis_index) {
                    append_segment_instance(
                        to_render_vector(frame.position),
                        to_render_vector(frame.position + axes.at(axis_index) * axis_length),
                        axis_colors.at(axis_index),
                        axis_width,
                        scene::Scene::ViewportSegmentWidthMode::Screen,
                        std::format("Rasterizer camera axes \"{}\"", camera.name)
                    );
                }
                draw_commands.push_back(ViewportSegmentDrawCommand{
                    .sourceKind    = scene::Scene::ViewportSegmentSet::SourceKind::Values,
                    .firstInstance = first_instance,
                    .instanceCount = 3u,
                    .depthMode     = scene::Scene::ViewportSegmentDepthMode::AlwaysVisible,
                    .transform     = {},
                });
            }
        }

        frame_segments.drawCommands = std::move(draw_commands);
        if (!instances.empty()) {
            const vk::DeviceSize instance_bytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(ViewportSegmentInstance));
            this->ensure_host_buffer(frame_segments.instanceBuffer, instance_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            std::memcpy(frame_segments.instanceBuffer.mapped, instances.data(), static_cast<std::size_t>(instance_bytes));
        }
        frame_segments.uploadedKey = upload_key;
    }

    void Renderer::upload_viewport_voxel_grid_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->viewport_voxel_grid_pass.frame_voxel_grids.size()) throw std::runtime_error("Spectra rasterizer viewport voxel grid frame index is out of range");
        FrameViewportVoxelGridResources& frame_voxel_grids = this->viewport_voxel_grid_pass.frame_voxel_grids.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (frame_voxel_grids.uploadedRevision == scene_revision) return;

        std::vector<ViewportVoxelGridDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::ViewportVoxelGrid& voxel_grid : resolved_frame.debug_attachments.viewport_voxel_grids) {
            const std::uint64_t cell_count = static_cast<std::uint64_t>(voxel_grid.dimensions[0]) * static_cast<std::uint64_t>(voxel_grid.dimensions[1]) * static_cast<std::uint64_t>(voxel_grid.dimensions[2]);
            if (cell_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" cell count exceeds uint32 draw range", voxel_grid.name));
            if (voxel_grid.source_kind == scene::Scene::ViewportVoxelGridSourceKind::IndexList && voxel_grid.index_count == 0u) continue;
            if (voxel_grid.source_kind == scene::Scene::ViewportVoxelGridSourceKind::IndexList && voxel_grid.index_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" index count exceeds uint32 draw range", voxel_grid.name));
            const ExternalStorageBuffer& resource = this->external_storage_buffer(voxel_grid.buffer_id, std::format("Rasterizer viewport voxel grid \"{}\"", voxel_grid.name));
            if (voxel_grid.source_byte_size > resource.byte_size) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" source byte size exceeds voxel buffer capacity", voxel_grid.name));
            std::map<std::uint64_t, ViewportVoxelBufferDescriptor>::iterator descriptor = this->viewport_voxel_grid_pass.buffer_descriptors.find(voxel_grid.buffer_id);
            if (descriptor == this->viewport_voxel_grid_pass.buffer_descriptors.end()) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" references buffer {} that is not registered as viewport voxel debug data", voxel_grid.name, voxel_grid.buffer_id));
            this->ensure_viewport_voxel_buffer_descriptor(descriptor->first, descriptor->second);
            if (!descriptor->second.descriptor_valid || descriptor->second.descriptor_sets.size() != 1u) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" voxel buffer descriptor is invalid", voxel_grid.name));
            const std::uint64_t bitfield_byte_count = (cell_count + 7u) / 8u;
            if (bitfield_byte_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error(std::format("Rasterizer viewport voxel grid \"{}\" bitfield byte count exceeds uint32 range", voxel_grid.name));
            ViewportVoxelGridDrawCommand draw_command{
                .bufferId = voxel_grid.buffer_id,
                .sourceKind = voxel_grid.source_kind,
                .indexEncoding = voxel_grid.index_encoding,
                .indexCount = static_cast<std::uint32_t>(voxel_grid.index_count),
                .bitfieldByteCount = static_cast<std::uint32_t>(bitfield_byte_count),
                .cellCount = static_cast<std::uint32_t>(cell_count),
                .frameIndex = frame_index,
                .depthMode = voxel_grid.depth_mode,
                .originCellScale = {voxel_grid.origin.x, voxel_grid.origin.y, voxel_grid.origin.z, voxel_grid.cell_scale},
                .voxelSize = {voxel_grid.voxel_size.x, voxel_grid.voxel_size.y, voxel_grid.voxel_size.z, 0.0f},
                .color = {voxel_grid.color.x, voxel_grid.color.y, voxel_grid.color.z, voxel_grid.color.w},
                .dimensions = {voxel_grid.dimensions[0], voxel_grid.dimensions[1], voxel_grid.dimensions[2], voxel_grid.source_kind == scene::Scene::ViewportVoxelGridSourceKind::Bitfield ? 0u : static_cast<std::uint32_t>(voxel_grid.index_encoding)},
            };
            if (draw_command.sourceKind == scene::Scene::ViewportVoxelGridSourceKind::Bitfield) this->ensure_viewport_voxel_grid_compaction_resource(draw_command);
            draw_commands.push_back(draw_command);
        }
        frame_voxel_grids.drawCommands = std::move(draw_commands);
        frame_voxel_grids.uploadedRevision = scene_revision;
    }

    void Renderer::upload_viewport_image_plane_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->viewport_image_plane_pass.frame_planes.size()) throw std::runtime_error("Spectra rasterizer viewport image plane frame index is out of range");
        if (!*this->viewport_image_plane_pass.descriptor_set_layout || !*this->viewport_image_plane_pass.sampler) throw std::runtime_error("Spectra rasterizer viewport image plane resources are not initialized");
        FrameViewportImagePlaneResources& frame_planes = this->viewport_image_plane_pass.frame_planes.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        const ViewportDebugUploadKey upload_key{.scene_revision = scene_revision, .settings_revision = this->viewport.camera_visual_revision};
        if (frame_planes.uploadedKey == upload_key) return;

        std::vector<ViewportImagePlaneInstance> instances{};
        std::vector<ViewportImagePlaneDrawCommand> draw_commands{};
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        if (this->viewport.camera_visual_images_visible) {
            if (!std::isfinite(this->viewport.camera_visual_image_alpha) || this->viewport.camera_visual_image_alpha < 0.0f || this->viewport.camera_visual_image_alpha > 1.0f) throw std::runtime_error("Rasterizer camera visual image alpha must be in [0, 1]");
            if (!std::isfinite(this->viewport.camera_visual_near) || !std::isfinite(this->viewport.camera_visual_far) || this->viewport.camera_visual_near <= 0.0f || this->viewport.camera_visual_far <= this->viewport.camera_visual_near) throw std::runtime_error("Rasterizer camera visual image range must satisfy far > near > 0");
            for (const scene::Scene::Camera& camera : resolved_frame.cameras) {
                if (!camera.image.has_value()) continue;
                if (instances.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) throw std::runtime_error("Rasterizer viewport image plane instance count exceeds uint32 range");
                const scene::Scene::CameraImage& image = *camera.image;

                if (image.width == 0u || image.height == 0u) throw std::runtime_error(std::format("Rasterizer camera \"{}\" image dimensions must be non-zero", camera.name));
                const std::uint64_t expected_byte_count = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height) * 4u;
                if (image.rgba8_size != expected_byte_count) throw std::runtime_error(std::format("Rasterizer camera \"{}\" RGBA8 byte count must be width * height * 4", camera.name));
                if (image.rgba8 == nullptr) throw std::runtime_error(std::format("Rasterizer camera \"{}\" RGBA8 pointer must not be null", camera.name));

                const std::string image_key = std::format("camera-rgba8://{}", camera.name);
                const ViewportImagePlaneTexture::Source source{
                    .data = reinterpret_cast<std::uintptr_t>(image.rgba8),
                    .byteSize = image.rgba8_size,
                    .width = image.width,
                    .height = image.height,
                    .revision = image.revision,
                };
                const std::map<std::string, ViewportImagePlaneTexture>::const_iterator cached_texture = this->viewport_image_plane_pass.texture_cache.find(image_key);
                if (cached_texture == this->viewport_image_plane_pass.texture_cache.end() || cached_texture->second.source != source) {
                    const std::map<std::string, ViewportImagePlaneTexture>::iterator existing = this->viewport_image_plane_pass.texture_cache.find(image_key);
                    if (existing != this->viewport_image_plane_pass.texture_cache.end()) {
                        this->destroy_viewport_image_plane_texture(existing->second);
                        this->viewport_image_plane_pass.texture_cache.erase(existing);
                    }

                    ViewportImagePlaneTexture texture{};
                    this->create_image_2d(texture.image, vk::Extent2D{image.width, image.height}, vk::Format::eR8G8B8A8Unorm, vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vk::ImageAspectFlagBits::eColor);
                    const vk::DeviceSize texture_bytes = static_cast<vk::DeviceSize>(image.rgba8_size);
                    this->ensure_host_buffer(texture.stagingBuffer, texture_bytes, vk::BufferUsageFlagBits::eTransferSrc);
                    std::memcpy(texture.stagingBuffer.mapped, image.rgba8, static_cast<std::size_t>(texture_bytes));

                    const std::array descriptor_pool_sizes{
                        vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, 1u},
                        vk::DescriptorPoolSize{vk::DescriptorType::eSampler, 1u},
                    };
                    const vk::DescriptorPoolCreateInfo descriptor_pool_create_info{vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet, 1u, static_cast<std::uint32_t>(descriptor_pool_sizes.size()), descriptor_pool_sizes.data()};
                    texture.descriptor_pool = vk::raii::DescriptorPool{*this->host.device, descriptor_pool_create_info};
                    const vk::DescriptorSetLayout image_descriptor_set_layout = *this->viewport_image_plane_pass.descriptor_set_layout;
                    const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*texture.descriptor_pool, 1u, &image_descriptor_set_layout};
                    texture.descriptor_sets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
                    if (texture.descriptor_sets.size() != 1u) throw std::runtime_error(std::format("{}: failed to allocate viewport image plane texture descriptor set", image_key));
                    const vk::DescriptorImageInfo image_info{{}, *texture.image.view, vk::ImageLayout::eShaderReadOnlyOptimal};
                    const vk::DescriptorImageInfo sampler_info{*this->viewport_image_plane_pass.sampler, {}, vk::ImageLayout::eUndefined};
                    const std::array descriptor_writes{
                        vk::WriteDescriptorSet{*texture.descriptor_sets.at(0), 0u, 0u, 1u, vk::DescriptorType::eSampledImage, &image_info, nullptr, nullptr},
                        vk::WriteDescriptorSet{*texture.descriptor_sets.at(0), 1u, 0u, 1u, vk::DescriptorType::eSampler, &sampler_info, nullptr, nullptr},
                    };
                    this->host.device->updateDescriptorSets(descriptor_writes, {});
                    texture.source = source;
                    texture.uploadPending = true;
                    this->viewport_image_plane_pass.texture_cache.emplace(image_key, std::move(texture));
                }

                const std::uint32_t first_instance = static_cast<std::uint32_t>(instances.size());
                instances.push_back(ViewportImagePlaneInstance{
                    .model = camera_visual_image_model(camera_visual_planes(camera, this->viewport.camera_visual_near, this->viewport.camera_visual_far)),
                    .tint = {1.0f, 1.0f, 1.0f, this->viewport.camera_visual_image_alpha},
                });
                draw_commands.push_back(ViewportImagePlaneDrawCommand{
                    .firstInstance = first_instance,
                    .instanceCount = 1u,
                    .depthMode = scene::Scene::ViewportSegmentDepthMode::AlwaysVisible,
                    .imageKey = image_key,
                });
            }
        }

        frame_planes.drawCommands = std::move(draw_commands);
        if (!instances.empty()) {
            const vk::DeviceSize instance_bytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(ViewportImagePlaneInstance));
            this->ensure_host_buffer(frame_planes.instanceBuffer, instance_bytes, vk::BufferUsageFlagBits::eVertexBuffer);
            std::memcpy(frame_planes.instanceBuffer.mapped, instances.data(), static_cast<std::size_t>(instance_bytes));
        }
        frame_planes.uploadedKey = upload_key;
    }

    void Renderer::upload_volume_resources(const std::uint32_t frame_index) {
        if (frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(frame_index);
        const scene::Scene::Revision scene_revision = this->scene.instance->revision();
        if (frame_volume.uploadedRevision == scene_revision) return;

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        const scene::Scene::VolumeGrid* selected_volume = this->select_render_volume_grid(resolved_frame.volumes);
        if (selected_volume == nullptr) {
            frame_volume.uploadedRevision = scene_revision;
            frame_volume.uploadPending    = false;
            frame_volume.externalDensityUploadDescriptorSets = nullptr;
            frame_volume.externalColorUploadDescriptorSets = nullptr;
            frame_volume.externalDensityUploadPending = false;
            frame_volume.externalColorUploadPending = false;
            frame_volume.hasColorChannel = false;
            frame_volume.descriptorValid  = false;
            frame_volume.drawCommand      = VolumeDrawCommand{};
            return;
        }
        const scene::Scene::VolumeGrid& volume = *selected_volume;
        const scene::Scene::PreviewMaterial material = this->resolve_material(volume.material_name);
        require_volume_material(material, volume.name);
        if (volume.dimensions[0] == 0 || volume.dimensions[1] == 0 || volume.dimensions[2] == 0) throw std::runtime_error(std::format("Rasterizer volume \"{}\" has zero dimensions", volume.name));
        if (!finite_scene_vector(volume.origin)) throw std::runtime_error(std::format("Rasterizer volume \"{}\" origin must be finite", volume.name));
        if (!finite_scene_vector(volume.voxel_size) || volume.voxel_size.x <= 0.0f || volume.voxel_size.y <= 0.0f || volume.voxel_size.z <= 0.0f) throw std::runtime_error(std::format("Rasterizer volume \"{}\" voxel size must be finite and positive", volume.name));
        const scene::Scene::VolumeChannel& density_channel = this->require_volume_channel(volume, "density");
        const scene::Scene::VolumeChannel* temperature_channel = this->find_volume_channel(volume, "temperature");
        const scene::Scene::VolumeChannel* color_channel = this->find_volume_channel(volume, "color");
        if (density_channel.dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel dimensions must match the volume dimensions", volume.name));
        if (temperature_channel != nullptr && temperature_channel->dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" temperature channel dimensions must match the volume dimensions", volume.name));
        if (color_channel != nullptr && color_channel->dimensions != volume.dimensions) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel dimensions must match the volume dimensions", volume.name));
        if (density_channel.format != scene::Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel must use Float32 format", volume.name));
        if (temperature_channel != nullptr && temperature_channel->format != scene::Scene::VolumeChannelFormat::Float32) throw std::runtime_error(std::format("Rasterizer volume \"{}\" temperature channel must use Float32 format", volume.name));
        if (color_channel != nullptr && color_channel->format != scene::Scene::VolumeChannelFormat::Float32x3) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel must use Float32x3 format", volume.name));

        const std::uint64_t cell_count = static_cast<std::uint64_t>(volume.dimensions[0]) * static_cast<std::uint64_t>(volume.dimensions[1]) * static_cast<std::uint64_t>(volume.dimensions[2]);
        if (cell_count > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) throw std::runtime_error(std::format("Rasterizer volume \"{}\" byte count exceeds uint64 range", volume.name));
        const vk::DeviceSize channel_bytes = static_cast<vk::DeviceSize>(cell_count * sizeof(float));
        if (cell_count > std::numeric_limits<std::uint64_t>::max() / (4u * sizeof(float))) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color staging byte count exceeds uint64 range", volume.name));
        const vk::DeviceSize color_source_bytes = static_cast<vk::DeviceSize>(cell_count * 3u * sizeof(float));
        const vk::DeviceSize color_image_bytes = static_cast<vk::DeviceSize>(cell_count * 4u * sizeof(float));
        if (temperature_channel != nullptr && temperature_channel->source_kind != scene::Scene::VolumeChannelSourceKind::Values) throw std::runtime_error(std::format("Rasterizer volume \"{}\" temperature channel uses an external GPU source; only density and color support external GPU volume upload", volume.name));

        const vk::Extent3D image_extent{volume.dimensions[0], volume.dimensions[1], volume.dimensions[2]};
        if (!*frame_volume.densityImage.image || frame_volume.densityImage.extent != image_extent || frame_volume.densityImage.format != vk::Format::eR32Sfloat) {
            this->create_volume_image(frame_volume.densityImage, image_extent, vk::Format::eR32Sfloat);
            frame_volume.descriptorValid = false;
        }
        if (!*frame_volume.temperatureImage.image || frame_volume.temperatureImage.extent != image_extent || frame_volume.temperatureImage.format != vk::Format::eR32Sfloat) {
            this->create_volume_image(frame_volume.temperatureImage, image_extent, vk::Format::eR32Sfloat);
            frame_volume.descriptorValid = false;
        }
        if (!*frame_volume.colorImage.image || frame_volume.colorImage.extent != image_extent || frame_volume.colorImage.format != vk::Format::eR32G32B32A32Sfloat) {
            this->create_volume_image(frame_volume.colorImage, image_extent, vk::Format::eR32G32B32A32Sfloat);
            frame_volume.descriptorValid = false;
        }
        if (density_channel.source_kind == scene::Scene::VolumeChannelSourceKind::Values) {
            this->ensure_host_buffer(frame_volume.densityStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
            std::memcpy(frame_volume.densityStagingBuffer.mapped, density_channel.values.data(), static_cast<std::size_t>(channel_bytes));
            frame_volume.externalDensityUploadDescriptorSets = nullptr;
            frame_volume.externalDensityUploadPending = false;
        } else {
            if (!*this->volume_pass.upload_descriptor_set_layout || !*this->volume_pass.descriptor_pool) throw std::runtime_error("Spectra rasterizer volume external upload descriptors are not initialized");
            const ExternalStorageBuffer& source = this->external_storage_buffer(density_channel.buffer_id, std::format("Rasterizer volume \"{}\" density channel", volume.name));
            if (density_channel.source_byte_size > source.byte_size) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel byte size {} exceeds external GPU buffer {} byte size {}", volume.name, density_channel.source_byte_size, density_channel.buffer_id, source.byte_size));
            if (density_channel.source_byte_size < channel_bytes) throw std::runtime_error(std::format("Rasterizer volume \"{}\" density channel external GPU byte size is too small", volume.name));
            this->destroy_host_buffer(frame_volume.densityStagingBuffer);
            if (frame_volume.externalDensityUploadDescriptorSets.size() != 1u) {
                const vk::DescriptorSetLayout upload_descriptor_set_layout = *this->volume_pass.upload_descriptor_set_layout;
                const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->volume_pass.descriptor_pool, 1u, &upload_descriptor_set_layout};
                frame_volume.externalDensityUploadDescriptorSets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
                if (frame_volume.externalDensityUploadDescriptorSets.size() != 1u) throw std::runtime_error("Failed to allocate Spectra rasterizer volume external upload descriptor set");
            }
            const vk::DescriptorBufferInfo source_buffer_info{*source.buffer.buffer, 0u, static_cast<vk::DeviceSize>(density_channel.source_byte_size)};
            const vk::DescriptorImageInfo density_image_info{{}, *frame_volume.densityImage.view, vk::ImageLayout::eGeneral};
            const std::array upload_descriptor_writes{
                vk::WriteDescriptorSet{*frame_volume.externalDensityUploadDescriptorSets.at(0), 0u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &source_buffer_info, nullptr},
                vk::WriteDescriptorSet{*frame_volume.externalDensityUploadDescriptorSets.at(0), 1u, 0u, 1u, vk::DescriptorType::eStorageImage, &density_image_info, nullptr, nullptr},
            };
            this->host.device->updateDescriptorSets(upload_descriptor_writes, {});
            frame_volume.externalDensityUploadPending = true;
        }
        this->ensure_host_buffer(frame_volume.temperatureStagingBuffer, channel_bytes, vk::BufferUsageFlagBits::eTransferSrc);
        if (temperature_channel != nullptr) std::memcpy(frame_volume.temperatureStagingBuffer.mapped, temperature_channel->values.data(), static_cast<std::size_t>(channel_bytes));
        else std::memset(frame_volume.temperatureStagingBuffer.mapped, 0, static_cast<std::size_t>(channel_bytes));

        if (color_channel != nullptr && color_channel->source_kind == scene::Scene::VolumeChannelSourceKind::ExternalGpuBuffer) {
            if (!*this->volume_pass.upload_descriptor_set_layout || !*this->volume_pass.descriptor_pool) throw std::runtime_error("Spectra rasterizer volume color external upload descriptors are not initialized");
            const ExternalStorageBuffer& source = this->external_storage_buffer(color_channel->buffer_id, std::format("Rasterizer volume \"{}\" color channel", volume.name));
            if (color_channel->source_byte_size > source.byte_size) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel byte size {} exceeds external GPU buffer {} byte size {}", volume.name, color_channel->source_byte_size, color_channel->buffer_id, source.byte_size));
            if (color_channel->source_byte_size < color_source_bytes) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel external GPU byte size is too small", volume.name));
            this->destroy_host_buffer(frame_volume.colorStagingBuffer);
            if (frame_volume.externalColorUploadDescriptorSets.size() != 1u) {
                const vk::DescriptorSetLayout upload_descriptor_set_layout = *this->volume_pass.upload_descriptor_set_layout;
                const vk::DescriptorSetAllocateInfo descriptor_set_allocate_info{*this->volume_pass.descriptor_pool, 1u, &upload_descriptor_set_layout};
                frame_volume.externalColorUploadDescriptorSets = vk::raii::DescriptorSets{*this->host.device, descriptor_set_allocate_info};
                if (frame_volume.externalColorUploadDescriptorSets.size() != 1u) throw std::runtime_error("Failed to allocate Spectra rasterizer volume external color upload descriptor set");
            }
            const vk::DescriptorBufferInfo source_buffer_info{*source.buffer.buffer, 0u, static_cast<vk::DeviceSize>(color_channel->source_byte_size)};
            const vk::DescriptorImageInfo color_image_info{{}, *frame_volume.colorImage.view, vk::ImageLayout::eGeneral};
            const std::array upload_descriptor_writes{
                vk::WriteDescriptorSet{*frame_volume.externalColorUploadDescriptorSets.at(0), 0u, 0u, 1u, vk::DescriptorType::eStorageBuffer, nullptr, &source_buffer_info, nullptr},
                vk::WriteDescriptorSet{*frame_volume.externalColorUploadDescriptorSets.at(0), 1u, 0u, 1u, vk::DescriptorType::eStorageImage, &color_image_info, nullptr, nullptr},
            };
            this->host.device->updateDescriptorSets(upload_descriptor_writes, {});
            frame_volume.externalColorUploadPending = true;
        } else {
            this->ensure_host_buffer(frame_volume.colorStagingBuffer, color_image_bytes, vk::BufferUsageFlagBits::eTransferSrc);
            float* color_staging = static_cast<float*>(frame_volume.colorStagingBuffer.mapped);
            if (color_channel == nullptr) {
                for (std::uint64_t index = 0u; index < cell_count; ++index) {
                    color_staging[index * 4u + 0u] = 1.0f;
                    color_staging[index * 4u + 1u] = 1.0f;
                    color_staging[index * 4u + 2u] = 1.0f;
                    color_staging[index * 4u + 3u] = 1.0f;
                }
            } else {
                if (color_channel->values.size() != cell_count * 3u) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel value count does not match dimensions", volume.name));
                for (std::uint64_t index = 0u; index < cell_count; ++index) {
                    const float red = color_channel->values.at(static_cast<std::size_t>(index * 3u + 0u));
                    const float green = color_channel->values.at(static_cast<std::size_t>(index * 3u + 1u));
                    const float blue = color_channel->values.at(static_cast<std::size_t>(index * 3u + 2u));
                    if (!std::isfinite(red) || !std::isfinite(green) || !std::isfinite(blue) || red < 0.0f || green < 0.0f || blue < 0.0f) throw std::runtime_error(std::format("Rasterizer volume \"{}\" color channel contains an invalid value", volume.name));
                    color_staging[index * 4u + 0u] = red;
                    color_staging[index * 4u + 1u] = green;
                    color_staging[index * 4u + 2u] = blue;
                    color_staging[index * 4u + 3u] = 1.0f;
                }
            }
            frame_volume.externalColorUploadDescriptorSets = nullptr;
            frame_volume.externalColorUploadPending = false;
        }
        frame_volume.hasColorChannel = color_channel != nullptr;

        if (!frame_volume.descriptorValid) {
            const vk::DescriptorBufferInfo camera_buffer_info{*this->camera.uniform_buffers.at(frame_index).buffer, 0, sizeof(CameraUniformData)};
            const vk::DescriptorImageInfo density_image_info{{}, *frame_volume.densityImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo temperature_image_info{{}, *frame_volume.temperatureImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo color_image_info{{}, *frame_volume.colorImage.view, vk::ImageLayout::eShaderReadOnlyOptimal};
            const vk::DescriptorImageInfo sampler_info{*this->volume_pass.sampler, {}, vk::ImageLayout::eUndefined};
            const std::array descriptor_writes{
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 0u, 0u, 1u, vk::DescriptorType::eUniformBuffer, nullptr, &camera_buffer_info, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 1u, 0u, 1u, vk::DescriptorType::eSampledImage, &density_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 2u, 0u, 1u, vk::DescriptorType::eSampledImage, &temperature_image_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 3u, 0u, 1u, vk::DescriptorType::eSampler, &sampler_info, nullptr, nullptr},
                vk::WriteDescriptorSet{*this->volume_pass.descriptor_sets.at(frame_index), 4u, 0u, 1u, vk::DescriptorType::eSampledImage, &color_image_info, nullptr, nullptr},
            };
            this->host.device->updateDescriptorSets(descriptor_writes, {});
            frame_volume.descriptorValid = true;
        }
        const ObjectKey object_key{SelectableObjectKind::VolumeGrid, volume.name};
        frame_volume.drawCommand = VolumeDrawCommand{
            .objectKey = object_key,
            .objectId  = this->object_id_for(object_key),
            .volume   = volume,
            .material = material,
        };
        frame_volume.uploadPending    = true;
        frame_volume.uploadedRevision = scene_revision;
    }

    void Renderer::record_pending_viewport_image_plane_uploads(const vk::raii::CommandBuffer& command_buffer) {
        for (std::pair<const std::string, ViewportImagePlaneTexture>& texture_entry : this->viewport_image_plane_pass.texture_cache) {
            ViewportImagePlaneTexture& texture = texture_entry.second;
            if (!texture.uploadPending) continue;
            if (!*texture.image.image) throw std::runtime_error(std::format("{}: viewport image plane upload is missing GPU image", texture_entry.first));
            if (!*texture.stagingBuffer.buffer) throw std::runtime_error(std::format("{}: viewport image plane upload is missing staging buffer", texture_entry.first));
            transition_image_layout(command_buffer, *texture.image.image, vk::ImageAspectFlagBits::eColor, texture.image.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            texture.image.layout = vk::ImageLayout::eTransferDstOptimal;

            const vk::BufferImageCopy region{
                0,
                0,
                0,
                {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
                {0, 0, 0},
                {texture.image.extent.width, texture.image.extent.height, 1u},
            };
            const std::array regions{region};
            command_buffer.copyBufferToImage(*texture.stagingBuffer.buffer, *texture.image.image, vk::ImageLayout::eTransferDstOptimal, regions);

            transition_image_layout(command_buffer, *texture.image.image, vk::ImageAspectFlagBits::eColor, texture.image.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            texture.image.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
            texture.uploadPending = false;
        }
    }

    void Renderer::record_pending_volume_upload(const vk::raii::CommandBuffer& command_buffer, FrameVolumeResources& frame_volume) {
        if (!frame_volume.uploadPending) return;
        if (!*frame_volume.densityImage.image || !*frame_volume.temperatureImage.image || !*frame_volume.colorImage.image) throw std::runtime_error("Spectra rasterizer volume upload is missing GPU images");
        if (!*frame_volume.temperatureStagingBuffer.buffer) throw std::runtime_error("Spectra rasterizer volume upload is missing temperature staging buffer");
        if (frame_volume.externalDensityUploadPending) {
            if (frame_volume.externalDensityUploadDescriptorSets.size() != 1u) throw std::runtime_error("Spectra rasterizer volume external upload is missing descriptor set");
            if (!*this->volume_pass.upload_pipeline || !*this->volume_pass.upload_pipeline_layout) throw std::runtime_error("Spectra rasterizer volume external upload pipeline is not initialized");
            const scene::Scene::VolumeGrid& volume = frame_volume.drawCommand.volume;
            const scene::Scene::VolumeChannel& density_channel = this->require_volume_channel(volume, "density");
            transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);
            frame_volume.densityImage.layout = vk::ImageLayout::eGeneral;
            const VolumeUploadPushConstantsData push_constants{
                .dimensions = {
                    volume.dimensions[0],
                    volume.dimensions[1],
                    volume.dimensions[2],
                    density_channel.index_encoding == scene::Scene::VolumeChannelIndexEncoding::Morton3D ? 1u : 0u,
                },
            };
            command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *this->volume_pass.upload_pipeline);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *this->volume_pass.upload_pipeline_layout, 0u, *frame_volume.externalDensityUploadDescriptorSets.at(0), {});
            command_buffer.pushConstants(*this->volume_pass.upload_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, sizeof(VolumeUploadPushConstantsData), &push_constants);
            command_buffer.dispatch((volume.dimensions[0] + 7u) / 8u, (volume.dimensions[1] + 7u) / 8u, (volume.dimensions[2] + 3u) / 4u);
            transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame_volume.densityImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        } else {
            if (!*frame_volume.densityStagingBuffer.buffer) throw std::runtime_error("Spectra rasterizer volume upload is missing density staging buffer");
            transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            frame_volume.densityImage.layout = vk::ImageLayout::eTransferDstOptimal;
        }
        if (frame_volume.externalColorUploadPending) {
            if (frame_volume.externalColorUploadDescriptorSets.size() != 1u) throw std::runtime_error("Spectra rasterizer volume external color upload is missing descriptor set");
            if (!*this->volume_pass.color_upload_pipeline || !*this->volume_pass.upload_pipeline_layout) throw std::runtime_error("Spectra rasterizer volume external color upload pipeline is not initialized");
            const scene::Scene::VolumeGrid& volume = frame_volume.drawCommand.volume;
            const scene::Scene::VolumeChannel& color_channel = this->require_volume_channel(volume, "color");
            transition_image_layout(command_buffer, *frame_volume.colorImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.colorImage.layout, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite);
            frame_volume.colorImage.layout = vk::ImageLayout::eGeneral;
            const VolumeUploadPushConstantsData push_constants{
                .dimensions = {
                    volume.dimensions[0],
                    volume.dimensions[1],
                    volume.dimensions[2],
                    color_channel.index_encoding == scene::Scene::VolumeChannelIndexEncoding::Morton3D ? 1u : 0u,
                },
            };
            command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *this->volume_pass.color_upload_pipeline);
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *this->volume_pass.upload_pipeline_layout, 0u, *frame_volume.externalColorUploadDescriptorSets.at(0), {});
            command_buffer.pushConstants(*this->volume_pass.upload_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, sizeof(VolumeUploadPushConstantsData), &push_constants);
            command_buffer.dispatch((volume.dimensions[0] + 7u) / 8u, (volume.dimensions[1] + 7u) / 8u, (volume.dimensions[2] + 3u) / 4u);
            transition_image_layout(command_buffer, *frame_volume.colorImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.colorImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderStorageWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame_volume.colorImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        } else {
            if (!*frame_volume.colorStagingBuffer.buffer) throw std::runtime_error("Spectra rasterizer volume upload is missing color staging buffer");
            transition_image_layout(command_buffer, *frame_volume.colorImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.colorImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
            frame_volume.colorImage.layout = vk::ImageLayout::eTransferDstOptimal;
        }
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eTransferDstOptimal;

        const vk::BufferImageCopy region{
            0,
            0,
            0,
            {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            {0, 0, 0},
            frame_volume.densityImage.extent,
        };
        const std::array regions{region};
        if (!frame_volume.externalDensityUploadPending) command_buffer.copyBufferToImage(*frame_volume.densityStagingBuffer.buffer, *frame_volume.densityImage.image, vk::ImageLayout::eTransferDstOptimal, regions);
        command_buffer.copyBufferToImage(*frame_volume.temperatureStagingBuffer.buffer, *frame_volume.temperatureImage.image, vk::ImageLayout::eTransferDstOptimal, regions);
        if (!frame_volume.externalColorUploadPending) command_buffer.copyBufferToImage(*frame_volume.colorStagingBuffer.buffer, *frame_volume.colorImage.image, vk::ImageLayout::eTransferDstOptimal, regions);

        if (!frame_volume.externalDensityUploadPending) {
            transition_image_layout(command_buffer, *frame_volume.densityImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.densityImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame_volume.densityImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
        transition_image_layout(command_buffer, *frame_volume.temperatureImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.temperatureImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        frame_volume.temperatureImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        if (!frame_volume.externalColorUploadPending) {
            transition_image_layout(command_buffer, *frame_volume.colorImage.image, vk::ImageAspectFlagBits::eColor, frame_volume.colorImage.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            frame_volume.colorImage.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
        frame_volume.uploadPending = false;
        frame_volume.externalDensityUploadPending = false;
        frame_volume.externalColorUploadPending = false;
    }

    std::string Renderer::active_scene_id() const {
        if (this->scene.instance->has_descriptor()) {
            const std::string& scene_id = this->scene.instance->descriptor().id;
            if (scene_id.empty()) throw std::runtime_error("Spectra rasterizer scene descriptor id must not be empty");
            return scene_id;
        }
        const std::shared_ptr<const scene::Scene::Document> scene = this->scene.instance->document();
        if (scene->name.empty()) throw std::runtime_error("Spectra rasterizer scene id must not be empty");
        return scene->name;
    }

    scene::Scene::Camera Renderer::active_scene_camera() const {
        const std::shared_ptr<const scene::Scene::Document> document = this->scene.instance->document();
        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::Camera& camera : resolved_frame.cameras) {
            if (camera.name != document->active_camera_name) continue;
            return camera;
        }
        throw std::runtime_error(std::format("Spectra rasterizer viewport requires active scene camera \"{}\"", document->active_camera_name));
    }

    scene::ViewportCamera Renderer::initial_camera_state_from_scene() const {
        const scene::Scene::Camera active_camera = this->active_scene_camera();
        const scene::CameraFrame active_frame = scene::camera_frame(active_camera.pose);
        scene::ViewportCamera state{
            .pose = active_camera.pose,
            .focus = active_frame.position + active_frame.forward,
            .navigation_up = -active_frame.down,
            .projection = active_camera.projection,
        };

        const SceneBounds bounds = this->scene_bounds();
        if (!bounds.valid) return state;
        const scene::Vector3 center{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f,
        };
        const scene::Vector3 view_direction = center - state.pose.position;
        if (scene::length_squared(view_direction) <= 1.0e-12f) throw std::runtime_error("Spectra rasterizer viewport camera position overlaps the scene bounds center");
        if (scene::length_squared(scene::cross(view_direction, state.navigation_up)) <= 1.0e-12f) throw std::runtime_error("Spectra rasterizer viewport camera up is parallel to the scene bounds center direction");
        const spectra::rasterizer::math::Vector3 diagonal = to_render_vector(bounds.maximum) - to_render_vector(bounds.minimum);
        const float radius = std::max(0.1f, spectra::rasterizer::math::length(diagonal) * 0.5f);
        const float camera_distance = spectra::rasterizer::math::length(to_render_vector(view_direction));
        if (!std::isfinite(camera_distance) || camera_distance <= 0.0f) throw std::runtime_error("Spectra rasterizer viewport camera distance must be positive");
        state.focus = center;
        state.pose = scene::camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        state.projection.far_plane = std::max(state.projection.far_plane, camera_distance + radius * 4.0f);
        return state;
    }

    scene::ViewportCamera Renderer::current_viewport_camera_state() const {
        if (!this->viewport.camera_initialized) throw std::runtime_error("Spectra rasterizer viewport camera is not initialized");
        return this->viewport.camera_state;
    }

    float Renderer::current_viewport_camera_distance() const {
        const scene::ViewportCamera state = this->current_viewport_camera_state();
        const spectra::rasterizer::math::Vector3 offset = to_render_vector(state.pose.position) - to_render_vector(state.focus);
        const float distance = spectra::rasterizer::math::length(offset);
        if (!std::isfinite(distance) || distance <= 0.0f) throw std::runtime_error("Spectra rasterizer viewport camera distance must be positive");
        return distance;
    }

    void Renderer::ensure_viewport_camera_session() {
        this->scene.camera_workspace->ensure_camera(this->active_scene_id(), this->initial_camera_state_from_scene());
    }

    void Renderer::reset_viewport_camera_session() {
        const scene::CameraSnapshot snapshot = this->scene.camera_workspace->reset_camera(this->active_scene_id(), this->initial_camera_state_from_scene());
        this->apply_viewport_camera_state(snapshot);
    }

    void Renderer::synchronize_viewport_camera() {
        const std::string scene_id = this->active_scene_id();
        this->scene.camera_workspace->ensure_camera(scene_id, this->initial_camera_state_from_scene());
        const scene::CameraSnapshot snapshot = this->scene.camera_workspace->snapshot(scene_id);
        if (this->viewport.camera_initialized && scene_id == this->scene.observed_camera_scene_id && snapshot.revision == this->scene.observed_camera_revision) return;
        this->apply_viewport_camera_state(snapshot);
    }

    void Renderer::apply_viewport_camera_state(const scene::CameraSnapshot& snapshot) {
        static_cast<void>(this->active_scene_camera());
        this->viewport.camera_state = snapshot.state;
        this->viewport.camera_near_plane = snapshot.state.projection.near_plane;
        this->viewport.camera_far_plane = snapshot.state.projection.far_plane;
        this->viewport.camera_initialized = true;
        this->scene.observed_camera_revision = snapshot.revision;
        this->scene.observed_camera_scene_id = this->active_scene_id();
    }

    void Renderer::commit_viewport_camera_state(scene::ViewportCamera state) {
        const scene::CameraSnapshot snapshot = this->scene.camera_workspace->commit(this->active_scene_id(), std::move(state));
        this->apply_viewport_camera_state(snapshot);
    }

    void Renderer::reset_viewport_camera_from_scene() {
        const scene::CameraSnapshot snapshot = this->scene.camera_workspace->commit(this->active_scene_id(), this->initial_camera_state_from_scene());
        this->apply_viewport_camera_state(snapshot);
    }

    Renderer::SceneBounds Renderer::scene_bounds() const {
        SceneBounds bounds{};
        const auto include_point = [&bounds](const scene::Vector3& point) {
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        };
        const auto include_transformed_point = [&include_point](const scene::Vector3& point, const scene::Transform& transform) {
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
            include_point(to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point))));
        };
        const auto include_transformed_bounds = [&include_transformed_point](const scene::Scene::PointCloudBounds& point_bounds, const scene::Transform& transform) {
            for (const scene::Vector3& corner : point_cloud_bounds_corners(point_bounds)) include_transformed_point(corner, transform);
        };

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            for (const scene::Vector3& position : mesh.positions) include_transformed_point(position, mesh.transform);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::ExternalGpuBuffer) {
                if (!point_cloud.bounds.has_value()) throw std::runtime_error(std::format("Rasterizer point cloud \"{}\" external source is missing explicit bounds", point_cloud.name));
                include_transformed_bounds(*point_cloud.bounds, point_cloud.transform);
                continue;
            }
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            for (std::size_t index = 0; index < point_cloud.positions.size(); ++index) {
                const scene::Vector3 center = to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point_cloud.positions.at(index))));
                const float radius = index < point_cloud.radii.size() ? std::max(0.0f, point_cloud.radii.at(index)) : 0.0f;
                include_point(scene::Vector3{center.x - radius, center.y - radius, center.z - radius});
                include_point(scene::Vector3{center.x + radius, center.y + radius, center.z + radius});
            }
        }
        for (const scene::Scene::VolumeGrid& volume : resolved_frame.volumes) {
            include_point(volume.origin);
            include_point(scene::Vector3{
                volume.origin.x + volume.voxel_size.x * static_cast<float>(volume.dimensions[0]),
                volume.origin.y + volume.voxel_size.y * static_cast<float>(volume.dimensions[1]),
                volume.origin.z + volume.voxel_size.z * static_cast<float>(volume.dimensions[2]),
            });
        }
        return bounds;
    }

    Renderer::SceneBounds Renderer::selected_scene_bounds() const {
        SceneBounds bounds{};
        const auto include_point = [&bounds](const scene::Vector3& point) {
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        };
        const auto include_transformed_point = [&include_point](const scene::Vector3& point, const scene::Transform& transform) {
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(transform));
            include_point(to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point))));
        };
        const auto include_transformed_bounds = [&include_transformed_point](const scene::Scene::PointCloudBounds& point_bounds, const scene::Transform& transform) {
            for (const scene::Vector3& corner : point_cloud_bounds_corners(point_bounds)) include_transformed_point(corner, transform);
        };

        const scene::Scene::ResolvedFrame resolved_frame = this->scene.instance->resolved_frame();
        for (const scene::Scene::Mesh& mesh : resolved_frame.meshes) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::Mesh, mesh.name})) continue;
            for (const scene::Vector3& position : mesh.positions) include_transformed_point(position, mesh.transform);
        }
        for (const scene::Scene::PointCloud& point_cloud : resolved_frame.point_clouds) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::PointCloud, point_cloud.name})) continue;
            if (point_cloud.source_kind == scene::Scene::PointCloud::SourceKind::ExternalGpuBuffer) {
                if (!point_cloud.bounds.has_value()) throw std::runtime_error(std::format("Rasterizer selected point cloud \"{}\" external source is missing explicit bounds", point_cloud.name));
                include_transformed_bounds(*point_cloud.bounds, point_cloud.transform);
                continue;
            }
            const spectra::rasterizer::math::Matrix4 matrix = spectra::rasterizer::math::transform_matrix(to_render_transform(point_cloud.transform));
            for (std::size_t index = 0; index < point_cloud.positions.size(); ++index) {
                const scene::Vector3 center = to_scene_vector(spectra::rasterizer::math::transform_point(matrix, to_render_vector(point_cloud.positions.at(index))));
                const float radius = index < point_cloud.radii.size() ? std::max(0.0f, point_cloud.radii.at(index)) : 0.0f;
                include_point(scene::Vector3{center.x - radius, center.y - radius, center.z - radius});
                include_point(scene::Vector3{center.x + radius, center.y + radius, center.z + radius});
            }
        }
        for (const scene::Scene::VolumeGrid& volume : resolved_frame.volumes) {
            if (!this->object_selected(ObjectKey{SelectableObjectKind::VolumeGrid, volume.name})) continue;
            include_point(volume.origin);
            include_point(scene::Vector3{
                volume.origin.x + volume.voxel_size.x * static_cast<float>(volume.dimensions[0]),
                volume.origin.y + volume.voxel_size.y * static_cast<float>(volume.dimensions[1]),
                volume.origin.z + volume.voxel_size.z * static_cast<float>(volume.dimensions[2]),
            });
        }
        return bounds;
    }

    void Renderer::frame_viewport_scene() {
        const SceneBounds bounds = this->scene_bounds();
        if (!bounds.valid) {
            this->reset_viewport_camera_from_scene();
            return;
        }
        const scene::Vector3 center{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f,
        };
        const spectra::rasterizer::math::Vector3 diagonal = to_render_vector(bounds.maximum) - to_render_vector(bounds.minimum);
        const float radius = std::max(0.1f, spectra::rasterizer::math::length(diagonal) * 0.5f);
        if (!this->viewport.camera_initialized) this->reset_viewport_camera_from_scene();
        scene::ViewportCamera state = this->current_viewport_camera_state();
        const spectra::rasterizer::math::Vector3 direction = spectra::rasterizer::math::normalize(to_render_vector(state.pose.position) - to_render_vector(state.focus));
        const float distance = std::clamp(radius * 2.6f, 0.02f, 1000000.0f);
        state.focus = center;
        state.pose.position = to_scene_vector(to_render_vector(center) + direction * distance);
        state.pose = scene::camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        state.projection.far_plane = std::max(state.projection.far_plane, distance + radius * 6.0f);
        this->viewport.camera_far_plane = std::max(this->viewport.camera_far_plane, distance + radius * 6.0f);
        this->commit_viewport_camera_state(std::move(state));
    }

    void Renderer::frame_selected_objects() {
        const SceneBounds bounds = this->selected_scene_bounds();
        if (!bounds.valid) {
            this->frame_viewport_scene();
            return;
        }
        const scene::Vector3 center{
            (bounds.minimum.x + bounds.maximum.x) * 0.5f,
            (bounds.minimum.y + bounds.maximum.y) * 0.5f,
            (bounds.minimum.z + bounds.maximum.z) * 0.5f,
        };
        const spectra::rasterizer::math::Vector3 diagonal = to_render_vector(bounds.maximum) - to_render_vector(bounds.minimum);
        const float radius = std::max(0.1f, spectra::rasterizer::math::length(diagonal) * 0.5f);
        if (!this->viewport.camera_initialized) this->reset_viewport_camera_from_scene();
        scene::ViewportCamera state = this->current_viewport_camera_state();
        const spectra::rasterizer::math::Vector3 direction = spectra::rasterizer::math::normalize(to_render_vector(state.pose.position) - to_render_vector(state.focus));
        const float distance = std::clamp(radius * 2.3f, 0.02f, 1000000.0f);
        state.focus = center;
        state.pose.position = to_scene_vector(to_render_vector(center) + direction * distance);
        state.pose = scene::camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        state.projection.far_plane = std::max(state.projection.far_plane, distance + radius * 6.0f);
        this->viewport.camera_far_plane = std::max(this->viewport.camera_far_plane, distance + radius * 6.0f);
        this->commit_viewport_camera_state(std::move(state));
    }

    void Renderer::set_viewport_axis_view(const scene::Vector3 direction) {
        const spectra::rasterizer::math::Vector3 normalized = spectra::rasterizer::math::normalize(to_render_vector(direction));
        if (!this->viewport.camera_initialized) this->reset_viewport_camera_from_scene();
        scene::ViewportCamera state = this->current_viewport_camera_state();
        const scene::Vector3 normalized_scene = to_scene_vector(normalized);
        const scene::Vector3 navigation_up = scene::normalize(state.navigation_up, "Spectra rasterizer viewport navigation up");
        const float parallel = std::abs(scene::dot(normalized_scene, navigation_up));
        const scene::Vector3 up = parallel > 0.9f ? scene::camera_frame(state.pose).right : navigation_up;
        state.pose.position = to_scene_vector(to_render_vector(state.focus) + normalized * this->current_viewport_camera_distance());
        state.navigation_up = up;
        state.pose = scene::camera_pose_from_look_at(state.pose.position, state.focus, state.navigation_up);
        this->commit_viewport_camera_state(std::move(state));
    }

    void Renderer::orbit_viewport_camera(const scene::ViewportCameraDelta delta) {
        this->commit_viewport_camera_state(scene::orbit_viewport_camera(this->current_viewport_camera_state(), delta));
    }

    void Renderer::pan_viewport_camera(const scene::ViewportCameraDelta delta, const scene::ViewportCameraSize viewport) {
        this->commit_viewport_camera_state(scene::pan_viewport_camera(this->current_viewport_camera_state(), delta, viewport));
    }

    void Renderer::zoom_viewport_camera(const float steps) {
        this->commit_viewport_camera_state(scene::zoom_viewport_camera(this->current_viewport_camera_state(), steps));
    }

    Renderer::CameraUniformData Renderer::make_viewport_camera_uniform() const {
        if (!this->viewport.camera_initialized) throw std::runtime_error("Spectra rasterizer viewport camera is not initialized");
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot create Spectra rasterizer camera uniform without a viewport extent");
        const std::shared_ptr<const scene::Scene::Document> document = this->scene.instance->document();
        const float aspect = static_cast<float>(this->viewport.extent.width) / static_cast<float>(this->viewport.extent.height);
        const scene::ViewportCamera state = this->current_viewport_camera_state();
        const float distance = this->current_viewport_camera_distance();
        const float far_plane = std::max(this->viewport.camera_far_plane, distance * 4.0f);
        const scene::VulkanCameraMatrices camera_matrices = scene::make_vulkan_camera_matrices(state.pose, state.projection, aspect, far_plane);
        const ViewportLightingData lighting = explicit_scene_lighting_uniform(*document);
        return CameraUniformData{
            .worldToClip               = camera_matrices.world_to_clip,
            .clipToWorld               = camera_matrices.clip_to_world,
            .cameraPosition            = {camera_matrices.frame.position.x, camera_matrices.frame.position.y, camera_matrices.frame.position.z, 0.0f},
            .environmentColorIntensity = lighting.environmentColorIntensity,
            .lightDirections           = lighting.lightDirections,
            .lightPositions            = lighting.lightPositions,
            .lightColorIntensities     = lighting.lightColorIntensities,
            .lightCounts               = lighting.lightCounts,
            .cameraForward             = {camera_matrices.frame.forward.x, camera_matrices.frame.forward.y, camera_matrices.frame.forward.z, 0.0f},
            .cameraRight               = {camera_matrices.frame.right.x, camera_matrices.frame.right.y, camera_matrices.frame.right.z, 0.0f},
            .cameraUp                  = {-camera_matrices.frame.down.x, -camera_matrices.frame.down.y, -camera_matrices.frame.down.z, 0.0f},
            .viewport                  = {static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, distance},
        };
    }

    void Renderer::update_camera_uniform(const std::uint32_t frame_index) {
        if (frame_index >= this->camera.uniform_buffers.size()) throw std::runtime_error("Spectra rasterizer uniform frame index is out of range");
        if (!this->viewport.camera_initialized) this->synchronize_viewport_camera();
        const CameraUniformData camera_uniform = this->make_viewport_camera_uniform();
        std::memcpy(this->camera.uniform_buffers.at(frame_index).mapped, &camera_uniform, sizeof(camera_uniform));
    }

    FrameResult Renderer::begin_frame(HostView host, const FrameContext& frame) {
        this->scene.instance->advance(frame.frame_number, frame.delta_seconds);
        this->update_host(host.physical_device(), host.device(), host.frame_count(), host.swapchain_extent());
        if (frame.frame_index >= this->host.frame_count) throw std::runtime_error("Spectra rasterizer frame index is out of range");
        this->consume_completed_screenshot(frame.frame_index);
        this->consume_completed_selection_pick(frame.frame_index);
        this->lifecycle.active_frame_index = frame.frame_index;
        FrameResult result{};
        this->ensure_viewport_resources();
        this->ensure_camera_resources();
        this->ensure_mesh_resources();
        this->ensure_viewport_grid_resources();
        this->ensure_point_cloud_resources();
        this->ensure_viewport_segment_resources();
        this->ensure_viewport_voxel_grid_resources();
        this->ensure_viewport_image_plane_resources();
        this->ensure_volume_resources();
        this->ensure_selection_resources();
        this->rebuild_selection_registry_if_needed();
        this->synchronize_viewport_camera();
        validate_material_library(*this->scene.instance->document());
        this->upload_scene_resources(frame.frame_index);
        this->upload_point_cloud_resources(frame.frame_index);
        this->upload_viewport_segment_resources(frame.frame_index);
        this->upload_viewport_voxel_grid_resources(frame.frame_index);
        this->upload_viewport_image_plane_resources(frame.frame_index);
        this->upload_volume_resources(frame.frame_index);
        this->update_camera_uniform(frame.frame_index);
        result.window_detail = this->window_detail();
        return result;
    }

    void Renderer::record_mesh_pass(const vk::raii::CommandBuffer& command_buffer) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            if (draw_command.material.alpha_mode == scene::Scene::PreviewAlphaMode::Blend) continue;
            const DrawPushConstantsData push_constants = make_draw_push_constants(draw_command.transform, draw_command.material);
            command_buffer.pushConstants(*this->mesh_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_transparent_mesh_pass(const vk::raii::CommandBuffer& command_buffer) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        struct TransparentMeshSortItem {
            const RenderDrawCommand* command{};
            float distanceSquared{};
        };
        std::vector<TransparentMeshSortItem> draw_commands{};
        draw_commands.reserve(frame_scene.drawCommands.size());
        const scene::ViewportCamera camera_state = this->current_viewport_camera_state();
        const spectra::rasterizer::math::Vector3 camera_position = to_render_vector(camera_state.pose.position);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            if (draw_command.material.alpha_mode != scene::Scene::PreviewAlphaMode::Blend) continue;
            const spectra::rasterizer::math::Vector3 sort_point = spectra::rasterizer::math::transform_point(spectra::rasterizer::math::transform_matrix(to_render_transform(draw_command.transform)), to_render_vector(draw_command.sortPoint));
            const spectra::rasterizer::math::Vector3 delta = sort_point - camera_position;
            draw_commands.push_back(TransparentMeshSortItem{
                .command = &draw_command,
                .distanceSquared = spectra::rasterizer::math::dot(delta, delta),
            });
        }
        if (draw_commands.empty()) return;
        std::ranges::sort(draw_commands, [](const TransparentMeshSortItem& left, const TransparentMeshSortItem& right) {
            return left.distanceSquared > right.distanceSquared;
        });

        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.transparent_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->mesh_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const TransparentMeshSortItem& item : draw_commands) {
            const RenderDrawCommand& draw_command = *item.command;
            const DrawPushConstantsData push_constants = make_draw_push_constants(draw_command.transform, draw_command.material);
            command_buffer.pushConstants(*this->mesh_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_viewport_grid_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->viewport.grid_visible) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->viewport_grid_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_grid_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        command_buffer.draw(3u, 1u, 0u, 0u);
    }

    void Renderer::record_volume_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active volume frame index is out of range");
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index);
        if (!frame_volume.descriptorValid || frame_volume.drawCommand.volume.name.empty()) return;
        const scene::Scene::VolumeGrid& volume = frame_volume.drawCommand.volume;
        const scene::Scene::PreviewMaterial& material = frame_volume.drawCommand.material;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->volume_pass.pipeline_layout, 0u, *this->volume_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const VolumePushConstantsData push_constants{
            .originDensityScale = {volume.origin.x, volume.origin.y, volume.origin.z, material.volume_density_scale},
            .extentStepScale    = {volume.voxel_size.x * static_cast<float>(volume.dimensions[0]), volume.voxel_size.y * static_cast<float>(volume.dimensions[1]), volume.voxel_size.z * static_cast<float>(volume.dimensions[2]), 1.0f},
            .base_color          = {material.base_color.x, material.base_color.y, material.base_color.z, material.base_color.w},
            .emission           = {material.emission_color.x * material.emission_strength, material.emission_color.y * material.emission_strength, material.emission_color.z * material.emission_strength, 0.0f},
            .material           = {material.volume_temperature_scale, frame_volume.hasColorChannel ? 1.0f : 0.0f, 0.0f, 0.0f},
        };
        command_buffer.pushConstants(*this->volume_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(36u, 1u, 0u, 0u);
    }

    void Renderer::transition_external_debug_vertex_buffers(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->point_cloud_pass.frame_point_clouds.size()) throw std::runtime_error("Spectra rasterizer active point cloud frame index is out of range");
        if (this->lifecycle.active_frame_index >= this->viewport_segment_pass.frame_segments.size()) throw std::runtime_error("Spectra rasterizer active viewport segment frame index is out of range");
        std::set<std::uint64_t> transitioned_buffers{};
        const auto transition_external_buffer = [this, &command_buffer, &transitioned_buffers](const std::uint64_t buffer_id, const std::string_view context) {
            if (!transitioned_buffers.insert(buffer_id).second) return;
            const ExternalStorageBuffer& resource = this->external_storage_buffer(buffer_id, context);
            if (!*resource.buffer.buffer) throw std::runtime_error(std::format("{} {} is invalid", context, buffer_id));
            transition_buffer_access(command_buffer, *resource.buffer.buffer, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eVertexAttributeInput, vk::AccessFlagBits2::eVertexAttributeRead);
        };
        const FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(this->lifecycle.active_frame_index);
        for (const PointCloudDrawCommand& draw_command : frame_point_cloud.drawCommands) {
            if (draw_command.sourceKind == scene::Scene::PointCloud::SourceKind::Values) continue;
            transition_external_buffer(draw_command.bufferId, "Rasterizer point cloud external buffer");
        }
        const FrameViewportSegmentResources& frame_segments = this->viewport_segment_pass.frame_segments.at(this->lifecycle.active_frame_index);
        for (const ViewportSegmentDrawCommand& draw_command : frame_segments.drawCommands) {
            if (draw_command.sourceKind == scene::Scene::ViewportSegmentSet::SourceKind::Values) continue;
            transition_external_buffer(draw_command.bufferId, "Rasterizer viewport segment external buffer");
        }
    }

    void Renderer::record_point_cloud_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->point_cloud_pass.frame_point_clouds.size()) throw std::runtime_error("Spectra rasterizer active point cloud frame index is out of range");
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(this->lifecycle.active_frame_index);
        if (frame_point_cloud.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->point_cloud_pass.pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->point_cloud_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        for (const PointCloudDrawCommand& draw_command : frame_point_cloud.drawCommands) {
            if (draw_command.sourceKind == scene::Scene::PointCloud::SourceKind::Values) {
                if (!*frame_point_cloud.instanceBuffer.buffer) throw std::runtime_error("Spectra rasterizer point cloud CPU instance buffer is invalid");
                const std::array<vk::Buffer, 1> vertex_buffers{*frame_point_cloud.instanceBuffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            } else {
                const ExternalStorageBuffer& resource = this->external_storage_buffer(draw_command.bufferId, "Rasterizer point cloud external buffer");
                if (!*resource.buffer.buffer) throw std::runtime_error(std::format("Rasterizer point cloud external buffer {} is invalid", draw_command.bufferId));
                const std::array<vk::Buffer, 1> vertex_buffers{*resource.buffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            }
            const PointCloudPushConstantsData push_constants = make_point_cloud_push_constants(draw_command.transform);
            command_buffer.pushConstants(*this->point_cloud_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
        }
    }

    void Renderer::record_viewport_voxel_grid_compactions(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->viewport_voxel_grid_pass.frame_voxel_grids.size()) throw std::runtime_error("Spectra rasterizer active viewport voxel grid frame index is out of range");
        const FrameViewportVoxelGridResources& frame_voxel_grids = this->viewport_voxel_grid_pass.frame_voxel_grids.at(this->lifecycle.active_frame_index);
        for (const ViewportVoxelGridDrawCommand& draw_command : frame_voxel_grids.drawCommands) {
            if (draw_command.sourceKind == scene::Scene::ViewportVoxelGridSourceKind::Bitfield) this->record_viewport_voxel_grid_compaction(command_buffer, draw_command);
        }
    }

    void Renderer::record_viewport_voxel_grid_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->viewport_voxel_grid_pass.frame_voxel_grids.size()) throw std::runtime_error("Spectra rasterizer active viewport voxel grid frame index is out of range");
        FrameViewportVoxelGridResources& frame_voxel_grids = this->viewport_voxel_grid_pass.frame_voxel_grids.at(this->lifecycle.active_frame_index);
        if (frame_voxel_grids.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_voxel_grid_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        std::optional<scene::Scene::ViewportSegmentDepthMode> bound_depth_mode{};
        for (const ViewportVoxelGridDrawCommand& draw_command : frame_voxel_grids.drawCommands) {
            const vk::raii::DescriptorSets* draw_descriptor_sets{};
            const ViewportVoxelGridCompactionResource* compaction_resource{};
            if (draw_command.sourceKind == scene::Scene::ViewportVoxelGridSourceKind::IndexList) {
                const std::map<std::uint64_t, ViewportVoxelBufferDescriptor>::const_iterator descriptor = this->viewport_voxel_grid_pass.buffer_descriptors.find(draw_command.bufferId);
                if (descriptor == this->viewport_voxel_grid_pass.buffer_descriptors.end()) throw std::runtime_error(std::format("Viewport voxel grid buffer {} was released before rendering", draw_command.bufferId));
                if (!descriptor->second.descriptor_valid || descriptor->second.descriptor_sets.size() != 1u) throw std::runtime_error(std::format("Viewport voxel grid buffer {} descriptor is invalid", draw_command.bufferId));
                draw_descriptor_sets = &descriptor->second.descriptor_sets;
            } else {
                const ViewportVoxelGridCompactionKey key{
                    .bufferId = draw_command.bufferId,
                    .frameIndex = draw_command.frameIndex,
                    .dimX = draw_command.dimensions[0],
                    .dimY = draw_command.dimensions[1],
                    .dimZ = draw_command.dimensions[2],
                    .indexEncoding = draw_command.indexEncoding,
                };
                const std::map<ViewportVoxelGridCompactionKey, ViewportVoxelGridCompactionResource>::const_iterator compaction = this->viewport_voxel_grid_pass.compactions.find(key);
                if (compaction == this->viewport_voxel_grid_pass.compactions.end()) throw std::runtime_error(std::format("Viewport voxel grid compaction resource for buffer {} does not exist", draw_command.bufferId));
                if (!compaction->second.draw_descriptor_valid || compaction->second.draw_descriptor_sets.size() != 1u) throw std::runtime_error(std::format("Viewport voxel grid compacted descriptor for buffer {} is invalid", draw_command.bufferId));
                draw_descriptor_sets = &compaction->second.draw_descriptor_sets;
                compaction_resource = &compaction->second;
            }
            if (!bound_depth_mode.has_value() || *bound_depth_mode != draw_command.depthMode) {
                const vk::raii::Pipeline& pipeline = draw_command.depthMode == scene::Scene::ViewportSegmentDepthMode::AlwaysVisible ? this->viewport_voxel_grid_pass.always_visible_pipeline : this->viewport_voxel_grid_pass.depth_tested_pipeline;
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                bound_depth_mode = draw_command.depthMode;
            }
            command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_voxel_grid_pass.pipeline_layout, 1u, *draw_descriptor_sets->at(0), {});
            const ViewportVoxelGridPushConstantsData push_constants{
                .originCellScale = draw_command.originCellScale,
                .voxelSize = draw_command.voxelSize,
                .color = draw_command.color,
                .dimensions = draw_command.dimensions,
            };
            command_buffer.pushConstants(*this->viewport_voxel_grid_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            if (draw_command.sourceKind == scene::Scene::ViewportVoxelGridSourceKind::IndexList) {
                command_buffer.draw(36u, draw_command.indexCount, 0u, 0u);
            } else {
                if (compaction_resource == nullptr || !*compaction_resource->indirectBuffer.buffer) throw std::runtime_error(std::format("Viewport voxel grid compacted indirect buffer for source {} is invalid", draw_command.bufferId));
                command_buffer.drawIndirect(*compaction_resource->indirectBuffer.buffer, 0u, 1u, sizeof(vk::DrawIndirectCommand));
            }
        }
    }

    void Renderer::record_viewport_voxel_grid_compaction(const vk::raii::CommandBuffer& command_buffer, const ViewportVoxelGridDrawCommand& draw_command) {
        if (draw_command.sourceKind != scene::Scene::ViewportVoxelGridSourceKind::Bitfield) throw std::runtime_error("Viewport voxel grid compaction record requires a bitfield source");
        const ViewportVoxelGridCompactionKey key{
            .bufferId = draw_command.bufferId,
            .frameIndex = draw_command.frameIndex,
            .dimX = draw_command.dimensions[0],
            .dimY = draw_command.dimensions[1],
            .dimZ = draw_command.dimensions[2],
            .indexEncoding = draw_command.indexEncoding,
        };
        const std::map<ViewportVoxelGridCompactionKey, ViewportVoxelGridCompactionResource>::const_iterator compaction = this->viewport_voxel_grid_pass.compactions.find(key);
        if (compaction == this->viewport_voxel_grid_pass.compactions.end()) throw std::runtime_error(std::format("Viewport voxel grid compaction resource for buffer {} does not exist", draw_command.bufferId));
        const ExternalStorageBuffer& source = this->external_storage_buffer(draw_command.bufferId, "Viewport voxel grid bitfield");
        if (!compaction->second.compute_descriptor_valid || compaction->second.compute_descriptor_sets.size() != 1u) throw std::runtime_error(std::format("Viewport voxel grid compaction descriptor for buffer {} is invalid", draw_command.bufferId));
        if (!*compaction->second.compactedIndexBuffer.buffer || !*compaction->second.counterBuffer.buffer || !*compaction->second.indirectBuffer.buffer) throw std::runtime_error(std::format("Viewport voxel grid compaction buffers for source {} are invalid", draw_command.bufferId));

        command_buffer.fillBuffer(*compaction->second.counterBuffer.buffer, 0u, sizeof(std::uint32_t), 0u);
        command_buffer.fillBuffer(*compaction->second.indirectBuffer.buffer, 0u, sizeof(std::uint32_t), 36u);
        command_buffer.fillBuffer(*compaction->second.indirectBuffer.buffer, sizeof(std::uint32_t), sizeof(vk::DrawIndirectCommand) - sizeof(std::uint32_t), 0u);
        transition_buffer_access(command_buffer, *source.buffer.buffer, vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eMemoryWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead);
        transition_buffer_access(command_buffer, *compaction->second.counterBuffer.buffer, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);
        transition_buffer_access(command_buffer, *compaction->second.indirectBuffer.buffer, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite);

        const ViewportVoxelGridCompactionPushConstantsData push_constants{
            .dimensions = {draw_command.dimensions[0], draw_command.dimensions[1], draw_command.dimensions[2], static_cast<std::uint32_t>(draw_command.indexEncoding)},
            .counts = {draw_command.bitfieldByteCount, draw_command.cellCount, 0u, 0u},
        };
        command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *this->viewport_voxel_grid_pass.compaction_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *this->viewport_voxel_grid_pass.compaction_pipeline_layout, 0u, *compaction->second.compute_descriptor_sets.at(0), {});
        command_buffer.pushConstants(*this->viewport_voxel_grid_pass.compaction_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0u, sizeof(push_constants), &push_constants);
        command_buffer.dispatch((draw_command.bitfieldByteCount + 255u) / 256u, 1u, 1u);
        transition_buffer_access(command_buffer, *compaction->second.compactedIndexBuffer.buffer, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eVertexShader, vk::AccessFlagBits2::eShaderRead);
        transition_buffer_access(command_buffer, *compaction->second.indirectBuffer.buffer, vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite, vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead);
    }

    void Renderer::record_viewport_image_plane_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->viewport_image_plane_pass.frame_planes.size()) throw std::runtime_error("Spectra rasterizer active viewport image plane frame index is out of range");
        FrameViewportImagePlaneResources& frame_planes = this->viewport_image_plane_pass.frame_planes.at(this->lifecycle.active_frame_index);
        if (frame_planes.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_image_plane_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_planes.instanceBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        std::optional<scene::Scene::ViewportSegmentDepthMode> bound_depth_mode{};
        std::string bound_image_key{};
        for (const ViewportImagePlaneDrawCommand& draw_command : frame_planes.drawCommands) {
            const std::map<std::string, ViewportImagePlaneTexture>::const_iterator texture_iterator = this->viewport_image_plane_pass.texture_cache.find(draw_command.imageKey);
            if (texture_iterator == this->viewport_image_plane_pass.texture_cache.end()) throw std::runtime_error(std::format("{}: viewport image plane texture was not uploaded", draw_command.imageKey));
            const ViewportImagePlaneTexture& texture = texture_iterator->second;
            if (!*texture.image.view || texture.descriptor_sets.size() != 1u) throw std::runtime_error(std::format("{}: viewport image plane texture descriptor is invalid", draw_command.imageKey));
            if (!bound_depth_mode.has_value() || *bound_depth_mode != draw_command.depthMode) {
                const vk::raii::Pipeline& pipeline = draw_command.depthMode == scene::Scene::ViewportSegmentDepthMode::AlwaysVisible ? this->viewport_image_plane_pass.always_visible_pipeline : this->viewport_image_plane_pass.depth_tested_pipeline;
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                bound_depth_mode = draw_command.depthMode;
            }
            if (bound_image_key != draw_command.imageKey) {
                command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_image_plane_pass.pipeline_layout, 1u, *texture.descriptor_sets.at(0), {});
                bound_image_key = draw_command.imageKey;
            }
            command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
        }
    }

    void Renderer::record_viewport_segment_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (this->lifecycle.active_frame_index >= this->viewport_segment_pass.frame_segments.size()) throw std::runtime_error("Spectra rasterizer active viewport segment frame index is out of range");
        FrameViewportSegmentResources& frame_segments = this->viewport_segment_pass.frame_segments.at(this->lifecycle.active_frame_index);
        if (frame_segments.drawCommands.empty()) return;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->viewport_segment_pass.pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        std::optional<scene::Scene::ViewportSegmentDepthMode> bound_depth_mode{};
        for (const ViewportSegmentDrawCommand& draw_command : frame_segments.drawCommands) {
            if (draw_command.sourceKind == scene::Scene::ViewportSegmentSet::SourceKind::Values) {
                if (!*frame_segments.instanceBuffer.buffer) throw std::runtime_error("Spectra rasterizer viewport segment CPU instance buffer is invalid");
                const std::array<vk::Buffer, 1> vertex_buffers{*frame_segments.instanceBuffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            } else {
                const ExternalStorageBuffer& resource = this->external_storage_buffer(draw_command.bufferId, "Rasterizer viewport segment external buffer");
                if (!*resource.buffer.buffer) throw std::runtime_error(std::format("Rasterizer viewport segment external buffer {} is invalid", draw_command.bufferId));
                const std::array<vk::Buffer, 1> vertex_buffers{*resource.buffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            }
            if (!bound_depth_mode.has_value() || *bound_depth_mode != draw_command.depthMode) {
                const vk::raii::Pipeline& pipeline = draw_command.depthMode == scene::Scene::ViewportSegmentDepthMode::AlwaysVisible ? this->viewport_segment_pass.always_visible_pipeline : this->viewport_segment_pass.depth_tested_pipeline;
                command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *pipeline);
                bound_depth_mode = draw_command.depthMode;
            }
            const ViewportSegmentPushConstantsData push_constants = make_viewport_segment_push_constants(draw_command.transform);
            command_buffer.pushConstants(*this->viewport_segment_pass.pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
        }
    }

    void Renderer::record_frame(const vk::raii::CommandBuffer& command_buffer) {
        if (!*this->viewport.image) return;
        if (this->lifecycle.active_frame_index >= this->mesh_pass.frame_scenes.size()) throw std::runtime_error("Spectra rasterizer active frame index is out of range");
        if (this->lifecycle.active_frame_index >= this->volume_pass.frame_volumes.size()) throw std::runtime_error("Spectra rasterizer active frame volume index is out of range");
        this->record_pending_viewport_image_plane_uploads(command_buffer);
        this->record_pending_volume_upload(command_buffer, this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index));
        this->record_viewport_voxel_grid_compactions(command_buffer);

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->viewport.depth_image, vk::ImageAspectFlagBits::eDepth, this->viewport.depth_layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->viewport.depth_layout = vk::ImageLayout::eDepthAttachmentOptimal;
        this->transition_external_debug_vertex_buffers(command_buffer);

        constexpr std::array<float, 4> clear_color{0.035f, 0.038f, 0.043f, 1.0f};
        const vk::ClearValue clear_value{vk::ClearColorValue{clear_color}};
        const vk::ClearValue depth_clear_value{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo color_attachment{
            *this->viewport.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            clear_value,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->viewport.depth_view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear_value,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_pass(command_buffer);
        this->record_viewport_grid_pass(command_buffer);
        this->record_volume_pass(command_buffer);
        this->record_point_cloud_pass(command_buffer);
        this->record_transparent_mesh_pass(command_buffer);
        this->record_viewport_voxel_grid_pass(command_buffer);
        this->record_viewport_image_plane_pass(command_buffer);
        this->record_viewport_segment_pass(command_buffer);
        command_buffer.endRendering();

        this->record_selection_visuals(command_buffer);
        this->record_selection_pick_pass(command_buffer);

        if (this->viewport.screenshot_requested)
            this->record_viewport_screenshot_copy(command_buffer);
        else {
            transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
            this->viewport.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        }
    }

    void Renderer::request_selection_pick(const std::uint32_t x, const std::uint32_t y, const bool select, const bool additive) {
        if (this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot pick a Spectra rasterizer object before viewport resources exist");
        if (x >= this->viewport.extent.width || y >= this->viewport.extent.height) throw std::runtime_error("Spectra rasterizer picking coordinate is outside the viewport");
        this->selection.requested_x        = x;
        this->selection.requested_y        = y;
        this->selection.requested_select   = select;
        this->selection.requested_additive = additive;
        this->selection.pick_requested     = true;
    }

    void Renderer::record_mesh_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FrameSceneResources& frame_scene = this->mesh_pass.frame_scenes.at(this->lifecycle.active_frame_index);
        if (frame_scene.drawCommands.empty()) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.mesh_picking_pipeline : *this->selection.mesh_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.mesh_picking_pipeline_layout : *this->selection.mesh_mask_pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const std::array<vk::Buffer, 1> vertex_buffers{*frame_scene.vertexBuffer.buffer};
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
        command_buffer.bindIndexBuffer(*frame_scene.indexBuffer.buffer, 0, vk::IndexType::eUint32);
        for (const RenderDrawCommand& draw_command : frame_scene.drawCommands) {
            const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
            if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) continue;
            const SelectionPushConstantsData push_constants = make_selection_push_constants(draw_command.transform, draw_command.material, color, draw_command.objectId);
            command_buffer.pushConstants(picking ? *this->selection.mesh_picking_pipeline_layout : *this->selection.mesh_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.drawIndexed(draw_command.indexCount, 1u, draw_command.firstIndex, 0, 0u);
        }
    }

    void Renderer::record_point_cloud_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FramePointCloudResources& frame_point_cloud = this->point_cloud_pass.frame_point_clouds.at(this->lifecycle.active_frame_index);
        if (frame_point_cloud.drawCommands.empty()) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.point_cloud_picking_pipeline : *this->selection.point_cloud_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.point_cloud_picking_pipeline_layout : *this->selection.point_cloud_mask_pipeline_layout, 0u, *this->camera.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        constexpr std::array<vk::DeviceSize, 1> vertex_offsets{0};
        for (const PointCloudDrawCommand& draw_command : frame_point_cloud.drawCommands) {
            const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
            if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) continue;
            if (draw_command.sourceKind == scene::Scene::PointCloud::SourceKind::Values) {
                if (!*frame_point_cloud.instanceBuffer.buffer) throw std::runtime_error("Spectra rasterizer point cloud CPU selection buffer is invalid");
                const std::array<vk::Buffer, 1> vertex_buffers{*frame_point_cloud.instanceBuffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            } else {
                const ExternalStorageBuffer& resource = this->external_storage_buffer(draw_command.bufferId, "Rasterizer point cloud external selection buffer");
                if (!*resource.buffer.buffer) throw std::runtime_error(std::format("Rasterizer point cloud external selection buffer {} is invalid", draw_command.bufferId));
                const std::array<vk::Buffer, 1> vertex_buffers{*resource.buffer.buffer};
                command_buffer.bindVertexBuffers(0u, vertex_buffers, vertex_offsets);
            }
            const PointCloudSelectionPushConstantsData push_constants = make_point_cloud_selection_push_constants(draw_command.transform, color, draw_command.objectId);
            command_buffer.pushConstants(picking ? *this->selection.point_cloud_picking_pipeline_layout : *this->selection.point_cloud_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
            command_buffer.draw(6u, draw_command.instanceCount, 0u, draw_command.firstInstance);
        }
    }

    void Renderer::record_volume_selection_pass(const vk::raii::CommandBuffer& command_buffer, const bool picking) {
        FrameVolumeResources& frame_volume = this->volume_pass.frame_volumes.at(this->lifecycle.active_frame_index);
        if (!frame_volume.descriptorValid || frame_volume.drawCommand.volume.name.empty()) return;
        const VolumeDrawCommand& draw_command = frame_volume.drawCommand;
        const std::array<float, 4> color = picking ? std::array<float, 4>{} : this->selection_mask_color(draw_command.objectKey);
        if (!picking && color[0] == 0.0f && color[1] == 0.0f && color[2] == 0.0f) return;
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.volume_picking_pipeline : *this->selection.volume_mask_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, picking ? *this->selection.volume_picking_pipeline_layout : *this->selection.volume_mask_pipeline_layout, 0u, *this->volume_pass.descriptor_sets.at(this->lifecycle.active_frame_index), {});
        const scene::Scene::VolumeGrid& volume = draw_command.volume;
        const VolumeSelectionPushConstantsData push_constants{
            .originDensityScale = {volume.origin.x, volume.origin.y, volume.origin.z, draw_command.material.volume_density_scale},
            .extentStepScale    = {volume.voxel_size.x * static_cast<float>(volume.dimensions[0]), volume.voxel_size.y * static_cast<float>(volume.dimensions[1]), volume.voxel_size.z * static_cast<float>(volume.dimensions[2]), 1.0f},
            .color              = color,
            .objectId           = draw_command.objectId,
        };
        command_buffer.pushConstants(picking ? *this->selection.volume_picking_pipeline_layout : *this->selection.volume_mask_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(36u, 1u, 0u, 0u);
    }

    void Renderer::record_selection_pick_pass(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->selection.pick_requested || this->selection.pick_pending) return;
        if (!*this->selection.object_id_image.image || !*this->selection.depth_image.image) throw std::runtime_error("Spectra rasterizer selection picking resources are not initialized");
        if (this->lifecycle.active_frame_index >= this->selection.readback_buffers.size()) throw std::runtime_error("Spectra rasterizer selection readback frame index is out of range");
        transition_image_layout(command_buffer, *this->selection.object_id_image.image, vk::ImageAspectFlagBits::eColor, this->selection.object_id_image.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->selection.object_id_image.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->selection.depth_image.image, vk::ImageAspectFlagBits::eDepth, this->selection.depth_image.layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->selection.depth_image.layout = vk::ImageLayout::eDepthAttachmentOptimal;

        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        const vk::ClearValue id_clear{vk::ClearColorValue{std::array<std::uint32_t, 4>{0u, 0u, 0u, 0u}}};
        const vk::ClearValue depth_clear{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo id_attachment{
            *this->selection.object_id_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            id_clear,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->selection.depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &id_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_selection_pass(command_buffer, true);
        this->record_volume_selection_pass(command_buffer, true);
        this->record_point_cloud_selection_pass(command_buffer, true);
        command_buffer.endRendering();

        transition_image_layout(command_buffer, *this->selection.object_id_image.image, vk::ImageAspectFlagBits::eColor, this->selection.object_id_image.layout, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
        this->selection.object_id_image.layout = vk::ImageLayout::eTransferSrcOptimal;
        const std::array<vk::BufferImageCopy, 1> copy_regions{vk::BufferImageCopy{
            0,
            0,
            0,
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{static_cast<std::int32_t>(this->selection.requested_x), static_cast<std::int32_t>(this->selection.requested_y), 0},
            vk::Extent3D{1, 1, 1},
        }};
        command_buffer.copyImageToBuffer(*this->selection.object_id_image.image, vk::ImageLayout::eTransferSrcOptimal, *this->selection.readback_buffers.at(this->lifecycle.active_frame_index).buffer, copy_regions);
        this->selection.pending_frame_index = this->lifecycle.active_frame_index;
        this->selection.pending_select      = this->selection.requested_select;
        this->selection.pending_additive    = this->selection.requested_additive;
        this->selection.pick_pending        = true;
        this->selection.pick_requested      = false;
    }

    void Renderer::record_selection_visuals(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->selection.visuals_visible) return;
        if (this->selection.selected_objects.empty() && !this->selection.hovered_object.has_value()) return;
        if (!*this->selection.mask_image.image || !*this->selection.depth_image.image) throw std::runtime_error("Spectra rasterizer selection visual resources are not initialized");
        transition_image_layout(command_buffer, *this->selection.mask_image.image, vk::ImageAspectFlagBits::eColor, this->selection.mask_image.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->selection.mask_image.layout = vk::ImageLayout::eColorAttachmentOptimal;
        transition_image_layout(command_buffer, *this->selection.depth_image.image, vk::ImageAspectFlagBits::eDepth, this->selection.depth_image.layout, vk::ImageLayout::eDepthAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        this->selection.depth_image.layout = vk::ImageLayout::eDepthAttachmentOptimal;
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        constexpr std::array<float, 4> clear_color{0.0f, 0.0f, 0.0f, 0.0f};
        const vk::ClearValue mask_clear{vk::ClearColorValue{clear_color}};
        const vk::ClearValue depth_clear{vk::ClearDepthStencilValue{1.0f, 0u}};
        const vk::RenderingAttachmentInfo mask_attachment{
            *this->selection.mask_image.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eStore,
            mask_clear,
        };
        const vk::RenderingAttachmentInfo depth_attachment{
            *this->selection.depth_image.view,
            vk::ImageLayout::eDepthAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eClear,
            vk::AttachmentStoreOp::eDontCare,
            depth_clear,
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &mask_attachment, &depth_attachment, nullptr};
        command_buffer.beginRendering(rendering_info);
        this->record_mesh_selection_pass(command_buffer, false);
        this->record_volume_selection_pass(command_buffer, false);
        this->record_point_cloud_selection_pass(command_buffer, false);
        command_buffer.endRendering();
        transition_image_layout(command_buffer, *this->selection.mask_image.image, vk::ImageAspectFlagBits::eColor, this->selection.mask_image.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->selection.mask_image.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->record_selection_outline_pass(command_buffer);
    }

    void Renderer::record_selection_outline_pass(const vk::raii::CommandBuffer& command_buffer) {
        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eColorAttachmentOptimal, vk::PipelineStageFlagBits2::eAllCommands, {}, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite);
        this->viewport.layout = vk::ImageLayout::eColorAttachmentOptimal;
        const vk::RenderingAttachmentInfo color_attachment{
            *this->viewport.view,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ResolveModeFlagBits::eNone,
            {},
            vk::ImageLayout::eUndefined,
            vk::AttachmentLoadOp::eLoad,
            vk::AttachmentStoreOp::eStore,
            {},
        };
        const vk::RenderingInfo rendering_info{{}, {{0, 0}, this->viewport.extent}, 1, 0, 1, &color_attachment, nullptr, nullptr};
        const vk::Viewport viewport{0.0f, 0.0f, static_cast<float>(this->viewport.extent.width), static_cast<float>(this->viewport.extent.height), 0.0f, 1.0f};
        const vk::Rect2D scissor{{0, 0}, this->viewport.extent};
        command_buffer.beginRendering(rendering_info);
        command_buffer.setViewport(0u, viewport);
        command_buffer.setScissor(0u, scissor);
        command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->selection.outline_pipeline);
        command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *this->selection.outline_pipeline_layout, 0u, *this->selection.outline_descriptor_sets.at(0), {});
        const OutlinePushConstantsData push_constants{
            .inverseExtent = {1.0f / static_cast<float>(this->viewport.extent.width), 1.0f / static_cast<float>(this->viewport.extent.height)},
        };
        command_buffer.pushConstants(*this->selection.outline_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment, 0u, sizeof(push_constants), &push_constants);
        command_buffer.draw(3u, 1u, 0u, 0u);
        command_buffer.endRendering();
    }

    void Renderer::consume_completed_selection_pick(const std::uint32_t frame_index) {
        if (!this->selection.pick_pending || this->selection.pending_frame_index != frame_index) return;
        if (frame_index >= this->selection.readback_buffers.size()) throw std::runtime_error("Spectra rasterizer selection readback frame index is out of range");
        GpuBuffer& readback_buffer = this->selection.readback_buffers.at(frame_index);
        if (readback_buffer.mapped == nullptr) throw std::runtime_error("Spectra rasterizer selection readback buffer is not mapped");
        std::uint32_t object_id{};
        std::memcpy(&object_id, readback_buffer.mapped, sizeof(object_id));
        this->apply_pick_result(object_id, this->selection.pending_select, this->selection.pending_additive);
        this->selection.pick_pending = false;
    }

    void Renderer::request_viewport_screenshot() {
        if (!*this->viewport.image || this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot capture a Spectra rasterizer screenshot before viewport rendering exists");
        if (this->viewport.screenshot_requested || this->viewport.screenshot_pending) throw std::runtime_error("A Spectra rasterizer screenshot is already pending");
        this->viewport.screenshot_path      = next_screenshot_path();
        this->viewport.screenshot_requested = true;
    }

    void Renderer::record_viewport_screenshot_copy(const vk::raii::CommandBuffer& command_buffer) {
        if (!this->viewport.screenshot_requested) return;
        if (this->viewport.screenshot_pending) throw std::runtime_error("Cannot record a Spectra rasterizer screenshot while another screenshot is pending");
        if (!*this->viewport.image || this->viewport.extent.width == 0 || this->viewport.extent.height == 0) throw std::runtime_error("Cannot record a Spectra rasterizer screenshot without a viewport image");
        constexpr vk::DeviceSize bytes_per_pixel = sizeof(std::uint16_t) * 4u;
        const vk::DeviceSize required_size       = static_cast<vk::DeviceSize>(this->viewport.extent.width) * static_cast<vk::DeviceSize>(this->viewport.extent.height) * bytes_per_pixel;
        this->ensure_host_buffer(this->viewport.screenshot_buffer, required_size, vk::BufferUsageFlagBits::eTransferDst);

        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eTransferSrcOptimal, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead);
        this->viewport.layout = vk::ImageLayout::eTransferSrcOptimal;
        const std::array<vk::BufferImageCopy, 1> copy_regions{vk::BufferImageCopy{
            0,
            0,
            0,
            vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
            vk::Offset3D{0, 0, 0},
            vk::Extent3D{this->viewport.extent.width, this->viewport.extent.height, 1},
        }};
        command_buffer.copyImageToBuffer(*this->viewport.image, vk::ImageLayout::eTransferSrcOptimal, *this->viewport.screenshot_buffer.buffer, copy_regions);
        transition_image_layout(command_buffer, *this->viewport.image, vk::ImageAspectFlagBits::eColor, this->viewport.layout, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead, vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eShaderSampledRead);
        this->viewport.layout                 = vk::ImageLayout::eShaderReadOnlyOptimal;
        this->viewport.screenshot_requested   = false;
        this->viewport.screenshot_pending     = true;
        this->viewport.screenshot_frame_index = this->lifecycle.active_frame_index;
        this->viewport.screenshot_extent      = this->viewport.extent;
    }

    void Renderer::consume_completed_screenshot(const std::uint32_t frame_index) {
        if (!this->viewport.screenshot_pending || this->viewport.screenshot_frame_index != frame_index) return;
        if (this->viewport.screenshot_buffer.mapped == nullptr) throw std::runtime_error("Spectra rasterizer screenshot readback buffer is not mapped");
        if (this->viewport.screenshot_path.empty()) throw std::runtime_error("Spectra rasterizer screenshot output path is empty");
        const std::vector<std::uint8_t> pixels = rgba16_float_to_rgba8(this->viewport.screenshot_buffer.mapped, this->viewport.screenshot_extent);
        write_png_rgba8(this->viewport.screenshot_path, this->viewport.screenshot_extent, pixels);
        this->viewport.screenshot_pending     = false;
        this->viewport.screenshot_frame_index = 0;
        this->viewport.screenshot_extent      = vk::Extent2D{};
        this->viewport.screenshot_path.clear();
    }

    void Renderer::handle_viewport_input(const ViewportImageRect image_rect) {
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};
        this->viewport.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(image_min, image_max);
        if (!this->viewport.hovered) return;
        if (!this->viewport.camera_initialized) this->reset_viewport_camera_from_scene();

        ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f) this->zoom_viewport_camera(io.MouseWheel);

        const scene::ViewportCameraDelta delta{
            .x_pixels = io.MouseDelta.x,
            .y_pixels = io.MouseDelta.y,
        };
        const ImVec2 mouse_position = io.MousePos;
        bool over_viewport_control = false;
        if (image_rect.width >= 230.0f && image_rect.height >= 54.0f) {
            constexpr float button_size = 30.0f;
            constexpr float gap = 4.0f;
            const ImVec2 toolbar_origin{image_min.x + 12.0f, image_min.y + 12.0f};
            const ImVec2 toolbar_padding{6.0f, 5.0f};
            const ImVec2 toolbar_max{toolbar_origin.x + toolbar_padding.x * 2.0f + button_size * 5.0f + gap * 4.0f, toolbar_origin.y + toolbar_padding.y * 2.0f + button_size};
            over_viewport_control = mouse_position.x >= toolbar_origin.x && mouse_position.x <= toolbar_max.x && mouse_position.y >= toolbar_origin.y && mouse_position.y <= toolbar_max.y;
        }
        if (!over_viewport_control && this->viewport.overlays_visible && image_rect.width >= 130.0f && image_rect.height >= 110.0f) {
            const ImVec2 gizmo_origin{image_min.x + image_size.x - 94.0f, image_min.y + 14.0f};
            const ImVec2 gizmo_max{gizmo_origin.x + 78.0f, gizmo_origin.y + 78.0f};
            over_viewport_control = mouse_position.x >= gizmo_origin.x && mouse_position.x <= gizmo_max.x && mouse_position.y >= gizmo_origin.y && mouse_position.y <= gizmo_max.y;
        }
        const scene::ViewportCameraSize viewport_size{
            .width = std::max(1.0f, image_size.x),
            .height = std::max(1.0f, image_size.y),
        };
        if (io.KeyAlt) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) this->orbit_viewport_camera(delta);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) this->pan_viewport_camera(delta, viewport_size);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) this->zoom_viewport_camera(scene::viewport_drag_zoom_steps(delta));
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            if (io.KeyShift) this->pan_viewport_camera(delta, viewport_size);
            else if (io.KeyCtrl) this->zoom_viewport_camera(scene::viewport_drag_zoom_steps(delta));
            else this->orbit_viewport_camera(delta);
        }

        if (!io.KeyAlt && !over_viewport_control && !ImGui::IsMouseDragging(ImGuiMouseButton_Middle) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            const std::uint32_t pick_x = static_cast<std::uint32_t>(std::clamp(mouse_position.x - image_min.x, 0.0f, std::max(0.0f, image_size.x - 1.0f)));
            const std::uint32_t pick_y = static_cast<std::uint32_t>(std::clamp(mouse_position.y - image_min.y, 0.0f, std::max(0.0f, image_size.y - 1.0f)));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false)) this->request_selection_pick(pick_x, pick_y, true, io.KeyShift);
            else if (!this->selection.pick_requested && !this->selection.pick_pending) this->request_selection_pick(pick_x, pick_y, false, false);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) this->clear_selection();
        if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (this->selection.selected_objects.empty()) this->frame_viewport_scene();
            else this->frame_selected_objects();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false)) this->frame_viewport_scene();
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad1, false)) this->set_viewport_axis_view(scene::Vector3{0.0f, 0.0f, 1.0f});
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad3, false)) this->set_viewport_axis_view(scene::Vector3{1.0f, 0.0f, 0.0f});
        if (ImGui::IsKeyPressed(ImGuiKey_Keypad7, false)) this->set_viewport_axis_view(scene::Vector3{0.0f, 1.0f, 0.0f});
    }

    void Renderer::draw_viewport_toolbar(const ViewportImageRect image_rect) {
        if (image_rect.width < 268.0f || image_rect.height < 54.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_max{image_rect.x + image_rect.width, image_rect.y + image_rect.height};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        constexpr float button_size = 30.0f;
        constexpr float gap = 4.0f;
        const ImVec2 origin{image_min.x + 12.0f, image_min.y + 12.0f};
        const ImVec2 padding{6.0f, 5.0f};
        const ImVec2 background_max{origin.x + padding.x * 2.0f + button_size * 6.0f + gap * 5.0f, origin.y + padding.y * 2.0f + button_size};
        draw_list->AddRectFilled(origin, background_max, IM_COL32(14, 16, 19, 198), 7.0f);
        draw_list->AddRect(origin, background_max, IM_COL32(86, 98, 108, 72), 7.0f);

        ImGui::PushClipRect(image_min, image_max, true);
        ImGui::PushID("SpectraRasterizerViewportToolbar");
        const auto draw_button = [button_size](const char* label, const char* tooltip, const bool active) {
            if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{35.0f / 255.0f, 65.0f / 255.0f, 73.0f / 255.0f, 1.0f});
            const bool clicked = ImGui::Button(label, ImVec2{button_size, button_size});
            if (active) ImGui::PopStyleColor();
            draw_tooltip(tooltip);
            return clicked;
        };

        ImGui::SetCursorScreenPos(ImVec2{origin.x + padding.x, origin.y + padding.y});
        if (draw_button(ICON_MS_RESET_FOCUS "##reset_view", "Reset View", false)) this->reset_viewport_camera_from_scene();
        ImGui::SameLine(0.0f, gap);
        if (draw_button(ICON_MS_CENTER_FOCUS_STRONG "##frame_all", "Frame All", false)) this->frame_viewport_scene();
        ImGui::SameLine(0.0f, gap);
        if (draw_button(this->viewport.grid_visible ? ICON_MS_GRID_ON "##grid" : ICON_MS_GRID_OFF "##grid", "Grid", this->viewport.grid_visible)) this->viewport.grid_visible = !this->viewport.grid_visible;
        ImGui::SameLine(0.0f, gap);
        const bool camera_visual_active = this->viewport.camera_visual_frustums_visible || this->viewport.camera_visual_images_visible || this->viewport.camera_visual_axes_visible;
        if (draw_button(camera_visual_active ? ICON_MS_PHOTO_CAMERA "##camera_visuals" : ICON_MS_VIDEOCAM_OFF "##camera_visuals", "Camera Visuals", camera_visual_active)) ImGui::OpenPopup("camera_visuals_popup");
        if (ImGui::BeginPopup("camera_visuals_popup")) {
            bool changed = false;
            changed = ImGui::Checkbox("Frustums", &this->viewport.camera_visual_frustums_visible) || changed;
            changed = ImGui::Checkbox("Images", &this->viewport.camera_visual_images_visible) || changed;
            changed = ImGui::Checkbox("Axes", &this->viewport.camera_visual_axes_visible) || changed;
            ImGui::SetNextItemWidth(150.0f);
            changed = ImGui::SliderFloat("Far", &this->viewport.camera_visual_far, this->viewport.camera_visual_near + 0.001f, 5.0f, "%.3f") || changed;
            if (!this->viewport.camera_visual_frustums_visible) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(150.0f);
            changed = ImGui::SliderFloat("Width", &this->viewport.camera_visual_width, 0.25f, 8.0f, "%.2f") || changed;
            if (!this->viewport.camera_visual_frustums_visible) ImGui::EndDisabled();
            if (!this->viewport.camera_visual_images_visible) ImGui::BeginDisabled();
            ImGui::SetNextItemWidth(150.0f);
            changed = ImGui::SliderFloat("Alpha", &this->viewport.camera_visual_image_alpha, 0.0f, 1.0f, "%.2f") || changed;
            if (!this->viewport.camera_visual_images_visible) ImGui::EndDisabled();
            if (changed) ++this->viewport.camera_visual_revision;
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0f, gap);
        if (draw_button(this->viewport.overlays_visible ? ICON_MS_VISIBILITY "##overlays" : ICON_MS_VISIBILITY_OFF "##overlays", "Overlays", this->viewport.overlays_visible)) this->viewport.overlays_visible = !this->viewport.overlays_visible;
        ImGui::SameLine(0.0f, gap);
        const bool screenshot_busy = this->viewport.screenshot_requested || this->viewport.screenshot_pending;
        if (screenshot_busy) ImGui::BeginDisabled();
        if (draw_button(ICON_MS_SCREENSHOT "##screenshot", screenshot_busy ? "Screenshot Pending" : "Screenshot", false)) this->request_viewport_screenshot();
        if (screenshot_busy) ImGui::EndDisabled();
        if (screenshot_busy && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", "Screenshot Pending");
        ImGui::PopID();
        ImGui::PopClipRect();
    }

    void Renderer::draw_orientation_gizmo(const ViewportImageRect image_rect) {
        if (!this->viewport.camera_initialized) return;
        if (image_rect.width < 130.0f || image_rect.height < 110.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        const ImVec2 origin{image_min.x + image_size.x - 94.0f, image_min.y + 14.0f};
        const ImVec2 size{78.0f, 78.0f};
        const ImVec2 center{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(origin, ImVec2{origin.x + size.x, origin.y + size.y}, IM_COL32(13, 15, 18, 186), 9.0f);
        draw_list->AddRect(origin, ImVec2{origin.x + size.x, origin.y + size.y}, IM_COL32(96, 106, 116, 90), 9.0f);

        struct GizmoAxis {
            const char* id{};
            const char* label{};
            spectra::rasterizer::math::Vector3 axis{};
            scene::Vector3 view_direction{};
            std::uint8_t line_red{};
            std::uint8_t line_green{};
            std::uint8_t line_blue{};
            ImVec2 tip{};
            ImVec2 label_center{};
            float depth{};
        };

        constexpr float axis_radius = 22.0f;
        const scene::ViewportCamera state = this->current_viewport_camera_state();
        const scene::CameraFrame frame = scene::camera_frame(state.pose);

        const auto project_axis = [&](const spectra::rasterizer::math::Vector3 axis) {
            const scene::Vector3 scene_axis = to_scene_vector(axis);
            return spectra::rasterizer::math::Vector3{
                scene::dot(scene_axis, frame.right),
                -scene::dot(scene_axis, frame.down),
                -scene::dot(scene_axis, frame.forward),
            };
        };

        std::array<GizmoAxis, 3> axes{{
            GizmoAxis{
                .id             = "x",
                .label          = "X",
                .axis           = spectra::rasterizer::math::Vector3{1.0f, 0.0f, 0.0f},
                .view_direction = scene::Vector3{1.0f, 0.0f, 0.0f},
                .line_red       = 232,
                .line_green     = 94,
                .line_blue      = 82,
            },
            GizmoAxis{
                .id             = "y",
                .label          = "Y",
                .axis           = spectra::rasterizer::math::Vector3{0.0f, 1.0f, 0.0f},
                .view_direction = scene::Vector3{0.0f, 1.0f, 0.0f},
                .line_red       = 112,
                .line_green     = 202,
                .line_blue      = 124,
            },
            GizmoAxis{
                .id             = "z",
                .label          = "Z",
                .axis           = spectra::rasterizer::math::Vector3{0.0f, 0.0f, 1.0f},
                .view_direction = scene::Vector3{0.0f, 0.0f, 1.0f},
                .line_red       = 96,
                .line_green     = 152,
                .line_blue      = 238,
            },
        }};

        for (GizmoAxis& axis : axes) {
            const spectra::rasterizer::math::Vector3 projected = project_axis(axis.axis);
            axis.tip = ImVec2{center.x + projected.x * axis_radius, center.y - projected.y * axis_radius};
            axis.depth = projected.z;
        }

        constexpr float arrow_length = 6.5f;
        constexpr float arrow_half_width = 3.8f;
        constexpr float label_gap = 8.0f;
        std::ranges::sort(axes, {}, &GizmoAxis::depth);
        for (GizmoAxis& axis : axes) {
            const int line_alpha = axis.depth < 0.0f ? 150 : 232;
            const int text_alpha = axis.depth < 0.0f ? 190 : 255;
            const ImVec2 delta{axis.tip.x - center.x, axis.tip.y - center.y};
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            ImVec2 direction{1.0f, 0.0f};
            if (length > 0.001f) direction = ImVec2{delta.x / length, delta.y / length};
            const ImVec2 perpendicular{-direction.y, direction.x};
            const ImVec2 arrow_base{axis.tip.x - direction.x * arrow_length, axis.tip.y - direction.y * arrow_length};
            const ImVec2 arrow_left{arrow_base.x + perpendicular.x * arrow_half_width, arrow_base.y + perpendicular.y * arrow_half_width};
            const ImVec2 arrow_right{arrow_base.x - perpendicular.x * arrow_half_width, arrow_base.y - perpendicular.y * arrow_half_width};
            draw_list->AddLine(center, arrow_base, IM_COL32(0, 0, 0, 95), 3.0f);
            draw_list->AddLine(center, arrow_base, IM_COL32(axis.line_red, axis.line_green, axis.line_blue, line_alpha), 2.0f);
            draw_list->AddTriangleFilled(axis.tip, arrow_left, arrow_right, IM_COL32(0, 0, 0, 86));
            draw_list->AddTriangleFilled(axis.tip, arrow_left, arrow_right, IM_COL32(axis.line_red, axis.line_green, axis.line_blue, line_alpha));
            axis.label_center = ImVec2{axis.tip.x + direction.x * label_gap, axis.tip.y + direction.y * label_gap};
            const ImVec2 text_size = ImGui::CalcTextSize(axis.label);
            const ImVec2 text_position{axis.label_center.x - text_size.x * 0.5f, axis.label_center.y - text_size.y * 0.5f};
            draw_list->AddText(ImVec2{text_position.x + 1.0f, text_position.y + 1.0f}, IM_COL32(0, 0, 0, 180), axis.label);
            draw_list->AddText(text_position, IM_COL32(axis.line_red, axis.line_green, axis.line_blue, text_alpha), axis.label);
        }

        ImGui::PushID("SpectraRasterizerOrientationGizmo");
        for (const GizmoAxis& axis : axes) {
            ImGui::SetCursorScreenPos(ImVec2{axis.label_center.x - 12.0f, axis.label_center.y - 12.0f});
            if (ImGui::InvisibleButton(axis.id, ImVec2{24.0f, 24.0f})) this->set_viewport_axis_view(axis.view_direction);
        }
        ImGui::PopID();
    }

    void Renderer::draw_viewport_overlays(const ViewportImageRect image_rect) {
        if (!this->viewport.overlays_visible) return;
        if (image_rect.width <= 1.0f || image_rect.height <= 1.0f) return;
        const ImVec2 image_min{image_rect.x, image_rect.y};
        const ImVec2 image_size{image_rect.width, image_rect.height};
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};

        ImGui::PushClipRect(image_min, image_max, true);
        const std::shared_ptr<const scene::Scene::Document> document = this->scene.instance->document();
        this->rebuild_scene_ui_cache_if_needed();
        constexpr const char* projection_text = "Perspective";
        const std::string scene_title = document->title.empty() ? document->name : document->title;
        std::string hud = std::format("{} | {}x{} | {}", scene_title, this->viewport.extent.width, this->viewport.extent.height, projection_text);
        const ImVec2 hud_padding{10.0f, 7.0f};
        ImVec2 hud_text = ImGui::CalcTextSize(hud.c_str());
        if (hud_text.x + hud_padding.x * 2.0f > image_size.x - 24.0f) {
            hud = scene_title;
            hud_text = ImGui::CalcTextSize(hud.c_str());
        }
        const ImVec2 hud_min{image_min.x + 12.0f, image_min.y + 58.0f};
        const ImVec2 hud_max{hud_min.x + hud_text.x + hud_padding.x * 2.0f, hud_min.y + hud_text.y + hud_padding.y * 2.0f};
        draw_list->AddRectFilled(hud_min, hud_max, IM_COL32(15, 18, 22, 184), 7.0f);
        draw_list->AddText(ImVec2{hud_min.x + hud_padding.x, hud_min.y + hud_padding.y}, IM_COL32(232, 236, 238, 255), hud.c_str());

        float next_left_overlay_y = hud_max.y + 8.0f;
        std::string selection_text = this->scene_selection_summary();
        const ImVec2 selection_padding{10.0f, 7.0f};
        ImVec2 selection_size = ImGui::CalcTextSize(selection_text.c_str());
        if (selection_size.x + selection_padding.x * 2.0f > image_size.x - 24.0f) {
            selection_text = this->selection.active_scene_object.has_value() ? std::format("{} selected", this->selection.selected_scene_objects.size()) : "No selection";
            selection_size = ImGui::CalcTextSize(selection_text.c_str());
        }
        const ImVec2 selection_min{image_min.x + 12.0f, next_left_overlay_y};
        const ImVec2 selection_max{selection_min.x + selection_size.x + selection_padding.x * 2.0f, selection_min.y + selection_size.y + selection_padding.y * 2.0f};
        const ImU32 selection_background = this->selection.active_scene_object.has_value() ? IM_COL32(12, 38, 48, 190) : IM_COL32(15, 18, 22, 150);
        draw_list->AddRectFilled(selection_min, selection_max, selection_background, 7.0f);
        draw_list->AddRect(selection_min, selection_max, this->selection.active_scene_object.has_value() ? IM_COL32(60, 198, 232, 112) : IM_COL32(92, 102, 112, 72), 7.0f);
        draw_list->AddText(ImVec2{selection_min.x + selection_padding.x, selection_min.y + selection_padding.y}, IM_COL32(218, 236, 242, 255), selection_text.c_str());

        std::size_t primitive_count{};
        for (const SceneObjectRecord& object : this->ui.scene_ui_cache.objects) {
            if (object.key.kind == SceneObjectKind::Mesh || object.key.kind == SceneObjectKind::Sphere || object.key.kind == SceneObjectKind::PointCloud || object.key.kind == SceneObjectKind::VolumeGrid) ++primitive_count;
        }
        const float camera_distance = this->current_viewport_camera_distance();
        std::string chip = std::format("rev {} | {} prim | dist {:.2f}", this->scene.instance->revision().value, primitive_count, camera_distance);
        const ImVec2 chip_padding{10.0f, 7.0f};
        ImVec2 chip_text = ImGui::CalcTextSize(chip.c_str());
        if (chip_text.x + chip_padding.x * 2.0f > image_size.x - 24.0f) {
            chip = std::format("rev {} | dist {:.2f}", this->scene.instance->revision().value, camera_distance);
            chip_text = ImGui::CalcTextSize(chip.c_str());
        }
        const ImVec2 chip_min{image_max.x - chip_text.x - chip_padding.x * 2.0f - 12.0f, image_max.y - chip_text.y - chip_padding.y * 2.0f - 12.0f};
        const ImVec2 chip_max{chip_min.x + chip_text.x + chip_padding.x * 2.0f, chip_min.y + chip_text.y + chip_padding.y * 2.0f};
        draw_list->AddRectFilled(chip_min, chip_max, IM_COL32(15, 18, 22, 164), 7.0f);
        draw_list->AddText(ImVec2{chip_min.x + chip_padding.x, chip_min.y + chip_padding.y}, IM_COL32(206, 214, 220, 255), chip.c_str());

        if (this->host.draw_viewport_overlays) this->host.draw_viewport_overlays(image_min.x, image_min.y, image_size.x, image_size.y);
        this->draw_orientation_gizmo(image_rect);
        ImGui::PopClipRect();
    }

    void Renderer::draw_viewport_window() {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x > 1.0f && available.y > 1.0f) this->ui.requested_extent = vk::Extent2D{static_cast<std::uint32_t>(available.x), static_cast<std::uint32_t>(available.y)};
        const ImVec2 image_min = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(image_min, ImVec2{image_min.x + available.x, image_min.y + available.y}, IM_COL32(9, 11, 14, 255));
        if (this->viewport.imgui_descriptor == VK_NULL_HANDLE) {
            ImGui::Dummy(available);
            return;
        }
        ImGui::Image(reinterpret_cast<ImTextureID>(this->viewport.imgui_descriptor), available, ImVec2{0.0f, 0.0f}, ImVec2{1.0f, 1.0f});
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_size = ImGui::GetItemRectSize();
        const ViewportImageRect image_rect{
            .x      = item_min.x,
            .y      = item_min.y,
            .width  = item_size.x,
            .height = item_size.y,
        };
        this->handle_viewport_input(image_rect);
        this->draw_viewport_overlays(image_rect);
        this->draw_viewport_toolbar(image_rect);
    }

    void Renderer::draw_scene_collection_panel() {
        this->rebuild_scene_ui_cache_if_needed();
        draw_inspector_section("Scene Collection");
        const std::array camera_kinds{SceneObjectKind::Camera};
        const std::array light_kinds{SceneObjectKind::Light};
        const std::array geometry_kinds{SceneObjectKind::Mesh, SceneObjectKind::Sphere, SceneObjectKind::PointCloud, SceneObjectKind::VolumeGrid};
        this->draw_scene_object_group("Camera", ICON_MS_PHOTO_CAMERA, std::span<const SceneObjectKind>{camera_kinds});
        this->draw_scene_object_group("Lights", ICON_MS_LIGHTBULB, std::span<const SceneObjectKind>{light_kinds});
        this->draw_scene_object_group("Geometry", ICON_MS_DEPLOYED_CODE, std::span<const SceneObjectKind>{geometry_kinds});
    }

    void Renderer::draw_scene_object_group(const std::string_view label, const char* icon, const std::span<const SceneObjectKind> kinds) {
        const auto matches_kind = [kinds](const SceneObjectKind kind) {
            for (const SceneObjectKind candidate : kinds) {
                if (candidate == kind) return true;
            }
            return false;
        };

        std::size_t count{};
        for (const SceneObjectRecord& object : this->ui.scene_ui_cache.objects) {
            if (matches_kind(object.key.kind)) ++count;
        }
        if (count == 0u) return;

        const std::string header = std::format("{} {} ({})###{}", icon, label, count, label);
        constexpr ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!ImGui::TreeNodeEx(header.c_str(), flags)) return;
        for (const SceneObjectRecord& object : this->ui.scene_ui_cache.objects) {
            if (!matches_kind(object.key.kind)) continue;
            this->draw_scene_object_row(object);
        }
        ImGui::TreePop();
    }

    void Renderer::draw_scene_object_row(const SceneObjectRecord& object) {
        const char* icon = ICON_MS_CATEGORY;
        switch (object.key.kind) {
        case SceneObjectKind::Camera: icon = ICON_MS_PHOTO_CAMERA; break;
        case SceneObjectKind::Light: icon = ICON_MS_LIGHTBULB; break;
        case SceneObjectKind::Mesh: icon = ICON_MS_POLYLINE; break;
        case SceneObjectKind::Sphere: icon = ICON_MS_CIRCLE; break;
        case SceneObjectKind::PointCloud: icon = ICON_MS_GRAIN; break;
        case SceneObjectKind::VolumeGrid: icon = ICON_MS_WATER_DROP; break;
        }

        const bool selected = this->scene_object_selected(object.key);
        const std::string label = std::format("{} {}", icon, this->compact_scene_object_name(object));
        ImGui::PushID(static_cast<int>(object.key.kind));
        ImGui::PushID(object.key.name.c_str());
        if (ImGui::Selectable(label.c_str(), selected)) this->select_scene_object(object.key, ImGui::GetIO().KeyShift);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            const std::string raw_name = this->raw_scene_object_name(object);
            const std::string raw_source = format_source_location(object.source);
            ImGui::BeginTooltip();
            if (this->scene_object_active(object.key)) ImGui::TextDisabled("%s", "Active object");
            ImGui::TextDisabled("%s", "Internal ID");
            ImGui::TextWrapped("%s", raw_name.c_str());
            ImGui::TextDisabled("%s", "Source");
            ImGui::TextWrapped("%s", raw_source.c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
        ImGui::PopID();
    }

    void Renderer::draw_inspector_transform(const scene::Transform& transform) {
        draw_inspector_section("Transform");
        draw_property_row("Position", format_vector3(transform.position));
        draw_property_row("Rotation", format_quaternion(transform.rotation));
        draw_property_row("Scale", format_vector3(transform.scale));
    }

    void Renderer::draw_inspector_material_block(const std::string_view material_name) {
        const scene::Scene::PreviewMaterial material = this->resolve_material(material_name);
        draw_inspector_section("Material");
        draw_property_row("Name", material.name);
        draw_property_row("Surface", preview_surface_kind_name(material.surface_kind));
        draw_property_row("Alpha", preview_alpha_mode_name(material.alpha_mode));
        draw_color_row("Base Color", material.base_color);
        if (!material.base_color_texture.empty()) draw_property_row("Base Texture", material.base_color_texture);
        draw_color_row("Emission", material.emission_color);
        if (!material.emission_texture.empty()) draw_property_row("Emission Texture", material.emission_texture);
        draw_property_row("Emission Str", format_float(material.emission_strength));
        draw_property_row("Roughness", format_float(material.roughness));
        if (!material.roughness_texture.empty()) draw_property_row("Roughness Texture", material.roughness_texture);
        draw_property_row("Metallic", format_float(material.metallic));
        draw_property_row("Alpha Cutoff", format_float(material.alpha_cutoff));
        if (!material.normal_texture.empty()) draw_property_row("Normal Texture", material.normal_texture);
        if (material.surface_kind == scene::Scene::PreviewSurfaceKind::Volume) {
            draw_property_row("Density Scale", format_float(material.volume_density_scale));
            draw_property_row("Temp Scale", format_float(material.volume_temperature_scale));
        }
    }

    void Renderer::draw_inspector_panel() {
        this->rebuild_scene_ui_cache_if_needed();
        draw_inspector_section("Inspector");
        if (!this->selection.active_scene_object.has_value()) {
            ImGui::TextDisabled("%s", "No object selected");
            return;
        }

        const SceneObjectRecord* object = this->scene_object_record(*this->selection.active_scene_object);
        if (object == nullptr) throw std::runtime_error("Spectra rasterizer inspector active object is missing from the scene collection");

        const auto scene_object_kind_text = [](const SceneObjectKind kind) {
            switch (kind) {
            case SceneObjectKind::Camera: return "Camera";
            case SceneObjectKind::Light: return "Light";
            case SceneObjectKind::Mesh: return "Mesh";
            case SceneObjectKind::Sphere: return "Sphere";
            case SceneObjectKind::PointCloud: return "Point Cloud";
            case SceneObjectKind::VolumeGrid: return "Volume";
            }
            throw std::runtime_error("Unknown Spectra rasterizer scene object kind");
        };

        const std::string compact_name = this->compact_scene_object_name(*object);
        const std::string raw_name = this->raw_scene_object_name(*object);
        const std::string compact_source = this->compact_source_location(object->source);
        const std::string raw_source = format_source_location(object->source);
        draw_property_row("Type", scene_object_kind_text(object->key.kind));
        draw_property_row("Name", compact_name);
        if (compact_name != raw_name && draw_property_row("Internal ID", raw_name)) ImGui::SetTooltip("%s", raw_name.c_str());
        if (draw_property_row("Source", compact_source) && compact_source != raw_source) ImGui::SetTooltip("%s", raw_source.c_str());

        switch (object->key.kind) {
        case SceneObjectKind::Camera:
            this->draw_inspector_transform(object->transform);
            draw_inspector_section("Camera");
            draw_property_row("Active", object->camera_active ? "true" : "false");
            draw_property_row("Forward", format_vector3(object->camera_forward));
            draw_property_row("Projection", camera_projection_kind_name(object->camera_projection_kind));
            if (object->camera_projection_kind != scene::CameraProjectionKind::Orthographic) draw_property_row("FOV", format_float(object->camera_vertical_fov_degrees));
            if (object->camera_projection_kind == scene::CameraProjectionKind::Pinhole) {
                draw_property_row("Image Size", std::format("{} x {}", object->camera_image_width, object->camera_image_height));
                draw_property_row("fx/fy", std::format("{} / {}", format_float(object->camera_fx), format_float(object->camera_fy)));
                draw_property_row("cx/cy", std::format("{} / {}", format_float(object->camera_cx), format_float(object->camera_cy)));
            }
            draw_property_row("Near", format_float(object->camera_near_plane));
            draw_property_row("Far", format_float(object->camera_far_plane));
            if (!object->camera_image_source.empty()) draw_property_row("Image", object->camera_image_source);
            break;
        case SceneObjectKind::Light:
            this->draw_inspector_transform(object->transform);
            draw_inspector_section("Light");
            draw_property_row("Kind", preview_light_kind_name(object->light_kind));
            draw_color_row("Color", object->light_color);
            draw_property_row("Intensity", format_float(object->light_intensity));
            if (object->light_kind == scene::Scene::PreviewLightKind::Spot) draw_property_row("Cone Angle", format_float(object->light_cone_angle_degrees));
            break;
        case SceneObjectKind::Mesh:
            this->draw_inspector_transform(object->transform);
            draw_inspector_section("Mesh");
            draw_property_row("Material", object->material_name);
            draw_property_row("Vertices", std::format("{}", object->vertex_count));
            draw_property_row("Indices", std::format("{}", object->index_count));
            draw_property_row("Triangles", std::format("{}", object->index_count / 3u));
            draw_property_row("Dynamic", object->dynamic ? "true" : "false");
            this->draw_inspector_material_block(object->material_name);
            break;
        case SceneObjectKind::Sphere:
            this->draw_inspector_transform(object->transform);
            draw_inspector_section("Sphere");
            draw_property_row("Material", object->material_name);
            draw_property_row("Radius", format_float(object->sphere_radius));
            draw_property_row("Dynamic", object->dynamic ? "true" : "false");
            this->draw_inspector_material_block(object->material_name);
            break;
        case SceneObjectKind::PointCloud:
            this->draw_inspector_transform(object->transform);
            draw_inspector_section("Point Cloud");
            draw_property_row("Material", object->material_name);
            draw_property_row("Points", std::format("{}", object->point_count));
            draw_property_row("Radius Range", std::format("{} - {}", format_float(object->minimum_radius), format_float(object->maximum_radius)));
            draw_property_row("Dynamic", object->dynamic ? "true" : "false");
            this->draw_inspector_material_block(object->material_name);
            break;
        case SceneObjectKind::VolumeGrid:
            draw_inspector_section("Volume");
            draw_property_row("Material", object->material_name);
            draw_property_row("Dimensions", format_dimensions3(object->dimensions));
            draw_property_row("Origin", format_vector3(object->origin));
            draw_property_row("Voxel Size", format_vector3(object->voxel_size));
            draw_property_row("Dynamic", object->dynamic ? "true" : "false");
            for (std::size_t channel_index = 0; channel_index < object->volume_channels.size(); ++channel_index) {
                const SceneVolumeChannelSummary& channel = object->volume_channels.at(channel_index);
                const std::string channel_label = std::format("Channel {}", channel_index);
                const std::string channel_value = std::format("{} | {} | {} values", channel.name, format_dimensions3(channel.dimensions), channel.value_count);
                draw_property_row(channel_label.c_str(), channel_value);
            }
            this->draw_inspector_material_block(object->material_name);
            break;
        }
    }

    void Renderer::draw_rasterizer_window() {
        this->rebuild_scene_ui_cache_if_needed();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.y <= 1.0f) return;

        constexpr float splitter_height = 6.0f;
        constexpr float minimum_panel_height = 120.0f;
        const float usable_height = std::max(1.0f, available.y - splitter_height);
        float collection_height = usable_height * this->ui.sidebar_split_ratio;
        if (usable_height > minimum_panel_height * 2.0f) collection_height = std::clamp(collection_height, minimum_panel_height, usable_height - minimum_panel_height);
        else collection_height = std::max(1.0f, collection_height);

        ImGui::BeginChild("SpectraRasterizerSceneCollectionPanel", ImVec2{0.0f, collection_height}, false);
        this->draw_scene_collection_panel();
        ImGui::EndChild();

        const float splitter_width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        draw_quiet_splitter("SpectraRasterizerSidebarSplitter", splitter_width, splitter_height);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive() && usable_height > 1.0f) {
            this->ui.sidebar_split_ratio = std::clamp(this->ui.sidebar_split_ratio + ImGui::GetIO().MouseDelta.y / usable_height, 0.25f, 0.75f);
        }

        ImGui::BeginChild("SpectraRasterizerInspectorPanel", ImVec2{0.0f, 0.0f}, false);
        this->draw_inspector_panel();
        ImGui::EndChild();
    }
} // namespace spectra::rasterizer
