#ifndef SPECTRA_SHADER_SEMANTICS_H
#define SPECTRA_SHADER_SEMANTICS_H

#define SPECTRA_GPU_ATTRIBUTE_NORMAL             (1u << 0u)
#define SPECTRA_GPU_ATTRIBUTE_TANGENT            (1u << 1u)
#define SPECTRA_GPU_ATTRIBUTE_TEXTURE_COORDINATE (1u << 2u)

#define SPECTRA_GEOMETRY_TRIANGLE   0u
#define SPECTRA_GEOMETRY_SPHERE     1u
#define SPECTRA_GEOMETRY_DISK       2u
#define SPECTRA_GEOMETRY_CYLINDER   3u
#define SPECTRA_GEOMETRY_BOX        4u
#define SPECTRA_GEOMETRY_RECTANGLE  5u
#define SPECTRA_GEOMETRY_SPHERE_SET 6u

#define SPECTRA_MATERIAL_INTERFACE            0u
#define SPECTRA_MATERIAL_DIFFUSE              1u
#define SPECTRA_MATERIAL_DIFFUSE_TRANSMISSION 2u
#define SPECTRA_MATERIAL_CONDUCTOR            3u
#define SPECTRA_MATERIAL_DIELECTRIC           4u
#define SPECTRA_MATERIAL_THIN_DIELECTRIC      5u
#define SPECTRA_MATERIAL_COATED_DIFFUSE       6u
#define SPECTRA_MATERIAL_COATED_CONDUCTOR     7u
#define SPECTRA_MATERIAL_MIX                  8u

#define SPECTRA_RASTER_MATERIAL_INTERFACE            0u
#define SPECTRA_RASTER_MATERIAL_DIFFUSE              1u
#define SPECTRA_RASTER_MATERIAL_DIFFUSE_TRANSMISSION 2u
#define SPECTRA_RASTER_MATERIAL_CONDUCTOR            3u
#define SPECTRA_RASTER_MATERIAL_DIELECTRIC           4u
#define SPECTRA_RASTER_MATERIAL_COATED_DIFFUSE       5u
#define SPECTRA_RASTER_MATERIAL_COATED_CONDUCTOR     6u

#define SPECTRA_TEXTURE_CONSTANT      0u
#define SPECTRA_TEXTURE_IMAGE         1u
#define SPECTRA_TEXTURE_CHECKERBOARD  2u
#define SPECTRA_TEXTURE_SCALE         3u
#define SPECTRA_TEXTURE_MIX           4u
#define SPECTRA_TEXTURE_DIRECTION_MIX 5u
#define SPECTRA_TEXTURE_BILERP        6u

#define SPECTRA_MAPPING_UV              0u
#define SPECTRA_MAPPING_PLANAR          1u
#define SPECTRA_MAPPING_SPHERICAL       2u
#define SPECTRA_MAPPING_CYLINDRICAL     3u
#define SPECTRA_MAPPING_CHECKERBOARD_3D 4u

#define SPECTRA_AREA_SOURCE_TRIANGLE   0u
#define SPECTRA_AREA_SOURCE_SPHERE_SET 1u
#define SPECTRA_AREA_SOURCE_ANALYTIC   2u

#define SPECTRA_LIGHT_POINT           0u
#define SPECTRA_LIGHT_SPOT            1u
#define SPECTRA_LIGHT_DISTANT         2u
#define SPECTRA_LIGHT_DIFFUSE_AREA    3u
#define SPECTRA_LIGHT_INFINITE        4u
#define SPECTRA_LIGHT_PORTAL_INFINITE 5u

#define SPECTRA_MEDIUM_HOMOGENEOUS      0u
#define SPECTRA_MEDIUM_VOLUME           1u
#define SPECTRA_VOLUME_DENSITY_GRID     0u
#define SPECTRA_VOLUME_RGB_GRID         1u
#define SPECTRA_VOLUME_NANOVDB          2u
#define SPECTRA_VOLUME_PROCEDURAL_CLOUD 3u

#define SPECTRA_SPECTRUM_RGB              0u
#define SPECTRA_SPECTRUM_CONSTANT         1u
#define SPECTRA_SPECTRUM_BLACKBODY        2u
#define SPECTRA_SPECTRUM_PIECEWISE_LINEAR 3u
#define SPECTRA_ILLUMINANT_NONE           0u
#define SPECTRA_ILLUMINANT_D65            1u
#define SPECTRA_ILLUMINANT_D60            2u

#ifdef __cplusplus
#include <cstdint>

namespace spectra::shader_semantics {
    inline constexpr std::uint32_t gpu_attribute_normal             = SPECTRA_GPU_ATTRIBUTE_NORMAL;
    inline constexpr std::uint32_t gpu_attribute_tangent            = SPECTRA_GPU_ATTRIBUTE_TANGENT;
    inline constexpr std::uint32_t gpu_attribute_texture_coordinate = SPECTRA_GPU_ATTRIBUTE_TEXTURE_COORDINATE;

    inline constexpr std::uint32_t geometry_triangle   = SPECTRA_GEOMETRY_TRIANGLE;
    inline constexpr std::uint32_t geometry_sphere     = SPECTRA_GEOMETRY_SPHERE;
    inline constexpr std::uint32_t geometry_disk       = SPECTRA_GEOMETRY_DISK;
    inline constexpr std::uint32_t geometry_cylinder   = SPECTRA_GEOMETRY_CYLINDER;
    inline constexpr std::uint32_t geometry_box        = SPECTRA_GEOMETRY_BOX;
    inline constexpr std::uint32_t geometry_rectangle  = SPECTRA_GEOMETRY_RECTANGLE;
    inline constexpr std::uint32_t geometry_sphere_set = SPECTRA_GEOMETRY_SPHERE_SET;

    inline constexpr std::uint32_t material_interface            = SPECTRA_MATERIAL_INTERFACE;
    inline constexpr std::uint32_t material_diffuse              = SPECTRA_MATERIAL_DIFFUSE;
    inline constexpr std::uint32_t material_diffuse_transmission = SPECTRA_MATERIAL_DIFFUSE_TRANSMISSION;
    inline constexpr std::uint32_t material_conductor            = SPECTRA_MATERIAL_CONDUCTOR;
    inline constexpr std::uint32_t material_dielectric           = SPECTRA_MATERIAL_DIELECTRIC;
    inline constexpr std::uint32_t material_thin_dielectric      = SPECTRA_MATERIAL_THIN_DIELECTRIC;
    inline constexpr std::uint32_t material_coated_diffuse       = SPECTRA_MATERIAL_COATED_DIFFUSE;
    inline constexpr std::uint32_t material_coated_conductor     = SPECTRA_MATERIAL_COATED_CONDUCTOR;
    inline constexpr std::uint32_t material_mix                  = SPECTRA_MATERIAL_MIX;

    inline constexpr std::uint32_t raster_material_interface            = SPECTRA_RASTER_MATERIAL_INTERFACE;
    inline constexpr std::uint32_t raster_material_diffuse              = SPECTRA_RASTER_MATERIAL_DIFFUSE;
    inline constexpr std::uint32_t raster_material_diffuse_transmission = SPECTRA_RASTER_MATERIAL_DIFFUSE_TRANSMISSION;
    inline constexpr std::uint32_t raster_material_conductor            = SPECTRA_RASTER_MATERIAL_CONDUCTOR;
    inline constexpr std::uint32_t raster_material_dielectric           = SPECTRA_RASTER_MATERIAL_DIELECTRIC;
    inline constexpr std::uint32_t raster_material_coated_diffuse       = SPECTRA_RASTER_MATERIAL_COATED_DIFFUSE;
    inline constexpr std::uint32_t raster_material_coated_conductor     = SPECTRA_RASTER_MATERIAL_COATED_CONDUCTOR;

    inline constexpr std::uint32_t texture_constant      = SPECTRA_TEXTURE_CONSTANT;
    inline constexpr std::uint32_t texture_image         = SPECTRA_TEXTURE_IMAGE;
    inline constexpr std::uint32_t texture_checkerboard  = SPECTRA_TEXTURE_CHECKERBOARD;
    inline constexpr std::uint32_t texture_scale         = SPECTRA_TEXTURE_SCALE;
    inline constexpr std::uint32_t texture_mix           = SPECTRA_TEXTURE_MIX;
    inline constexpr std::uint32_t texture_direction_mix = SPECTRA_TEXTURE_DIRECTION_MIX;
    inline constexpr std::uint32_t texture_bilerp        = SPECTRA_TEXTURE_BILERP;

    inline constexpr std::uint32_t mapping_uv              = SPECTRA_MAPPING_UV;
    inline constexpr std::uint32_t mapping_planar          = SPECTRA_MAPPING_PLANAR;
    inline constexpr std::uint32_t mapping_spherical       = SPECTRA_MAPPING_SPHERICAL;
    inline constexpr std::uint32_t mapping_cylindrical     = SPECTRA_MAPPING_CYLINDRICAL;
    inline constexpr std::uint32_t mapping_checkerboard_3d = SPECTRA_MAPPING_CHECKERBOARD_3D;

    inline constexpr std::uint32_t area_source_triangle   = SPECTRA_AREA_SOURCE_TRIANGLE;
    inline constexpr std::uint32_t area_source_sphere_set = SPECTRA_AREA_SOURCE_SPHERE_SET;
    inline constexpr std::uint32_t area_source_analytic   = SPECTRA_AREA_SOURCE_ANALYTIC;

    inline constexpr std::uint32_t light_point           = SPECTRA_LIGHT_POINT;
    inline constexpr std::uint32_t light_spot            = SPECTRA_LIGHT_SPOT;
    inline constexpr std::uint32_t light_distant         = SPECTRA_LIGHT_DISTANT;
    inline constexpr std::uint32_t light_diffuse_area    = SPECTRA_LIGHT_DIFFUSE_AREA;
    inline constexpr std::uint32_t light_infinite        = SPECTRA_LIGHT_INFINITE;
    inline constexpr std::uint32_t light_portal_infinite = SPECTRA_LIGHT_PORTAL_INFINITE;

    inline constexpr std::uint32_t medium_homogeneous      = SPECTRA_MEDIUM_HOMOGENEOUS;
    inline constexpr std::uint32_t medium_volume           = SPECTRA_MEDIUM_VOLUME;
    inline constexpr std::uint32_t volume_density_grid     = SPECTRA_VOLUME_DENSITY_GRID;
    inline constexpr std::uint32_t volume_rgb_grid         = SPECTRA_VOLUME_RGB_GRID;
    inline constexpr std::uint32_t volume_nanovdb          = SPECTRA_VOLUME_NANOVDB;
    inline constexpr std::uint32_t volume_procedural_cloud = SPECTRA_VOLUME_PROCEDURAL_CLOUD;

    inline constexpr std::uint32_t spectrum_rgb              = SPECTRA_SPECTRUM_RGB;
    inline constexpr std::uint32_t spectrum_constant         = SPECTRA_SPECTRUM_CONSTANT;
    inline constexpr std::uint32_t spectrum_blackbody        = SPECTRA_SPECTRUM_BLACKBODY;
    inline constexpr std::uint32_t spectrum_piecewise_linear = SPECTRA_SPECTRUM_PIECEWISE_LINEAR;
    inline constexpr std::uint32_t illuminant_none           = SPECTRA_ILLUMINANT_NONE;
    inline constexpr std::uint32_t illuminant_d65            = SPECTRA_ILLUMINANT_D65;
    inline constexpr std::uint32_t illuminant_d60            = SPECTRA_ILLUMINANT_D60;
} // namespace spectra::shader_semantics
#endif

#endif
