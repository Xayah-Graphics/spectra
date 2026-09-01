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
#define SPECTRA_VOLUME_PROCEDURAL_CLOUD 2u
#define SPECTRA_VOLUME_OPENVDB          3u

#define SPECTRA_SPECTRUM_RGB              0u
#define SPECTRA_SPECTRUM_CONSTANT         1u
#define SPECTRA_SPECTRUM_BLACKBODY        2u
#define SPECTRA_SPECTRUM_PIECEWISE_LINEAR 3u
#define SPECTRA_ILLUMINANT_NONE           0u
#define SPECTRA_ILLUMINANT_D65            1u
#define SPECTRA_ILLUMINANT_D60            2u

#define SPECTRA_VISUALIZATION_SEGMENTS           1u
#define SPECTRA_VISUALIZATION_VECTORS            2u
#define SPECTRA_VISUALIZATION_IMAGE              3u
#define SPECTRA_VISUALIZATION_SURFACE            4u
#define SPECTRA_VISUALIZATION_PARTICLES          5u
#define SPECTRA_VISUALIZATION_VOLUME_SLICE       8u
#define SPECTRA_VISUALIZATION_VOLUME_RAY_MARCH   9u
#define SPECTRA_VISUALIZATION_VOLUME_MIP         10u
#define SPECTRA_VISUALIZATION_VOLUME_ISOSURFACE  11u
#define SPECTRA_VISUALIZATION_VOLUME_GLYPHS      12u
#define SPECTRA_VISUALIZATION_VOLUME_STREAMLINES 13u
#define SPECTRA_VISUALIZATION_VOLUME_LIC         14u
#define SPECTRA_VISUALIZATION_REFERENCE_OVERLAY  15u
#define SPECTRA_VISUALIZATION_REFERENCE_PLANE    16u
#define SPECTRA_VISUALIZATION_VOLUME_CELLS       17u
#define SPECTRA_VISUALIZATION_INDEXED_POINTS     18u
#define SPECTRA_VISUALIZATION_INDEXED_SEGMENTS   19u
#define SPECTRA_VISUALIZATION_MESH_VECTORS       20u
#define SPECTRA_VISUALIZATION_MESH_WIREFRAME     21u
#define SPECTRA_VISUALIZATION_MESH_VERTICES      22u
#define SPECTRA_VISUALIZATION_VERTEX_NORMALS     23u
#define SPECTRA_VISUALIZATION_FACE_NORMALS       24u

#define SPECTRA_DIAGNOSTIC_LINES            0u
#define SPECTRA_DIAGNOSTIC_BOXES            1u
#define SPECTRA_DIAGNOSTIC_TRIANGLE_EDGES   2u
#define SPECTRA_DIAGNOSTIC_MESH_VERTICES    3u
#define SPECTRA_DIAGNOSTIC_MESH_NORMALS     4u
#define SPECTRA_DIAGNOSTIC_MESH_TANGENTS    5u
#define SPECTRA_DIAGNOSTIC_SPHERE_CENTERS   6u
#define SPECTRA_DIAGNOSTIC_SPHERE_WIREFRAME 8u
#define SPECTRA_DIAGNOSTIC_OCCUPANCY_CELLS  9u
#define SPECTRA_DIAGNOSTIC_PARTICLE_POINTS  10u
#define SPECTRA_DIAGNOSTIC_PARTICLE_VECTORS 11u

#define SPECTRA_FIELD_FLOAT      0u
#define SPECTRA_FIELD_FLOAT3     1u
#define SPECTRA_FIELD_UINT32     2u
#define SPECTRA_FIELD_MAC_FLOAT3 3u

#define SPECTRA_DEPTH_TESTED  0u
#define SPECTRA_DEPTH_XRAY    1u
#define SPECTRA_DEPTH_OVERLAY 2u

#define SPECTRA_COLOR_SOURCE_ELEMENT 0u
#define SPECTRA_COLOR_SOURCE_UNIFORM 1u
#define SPECTRA_COLOR_SOURCE_SCALAR  2u

#define SPECTRA_COLOR_MAP_VIRIDIS   0u
#define SPECTRA_COLOR_MAP_TURBO     1u
#define SPECTRA_COLOR_MAP_COOL_WARM 2u
#define SPECTRA_COLOR_MAP_GRAYSCALE 3u

#define SPECTRA_FIELD_MAPPING_VALUE          0u
#define SPECTRA_FIELD_MAPPING_MAGNITUDE      1u
#define SPECTRA_FIELD_MAPPING_X              2u
#define SPECTRA_FIELD_MAPPING_Y              3u
#define SPECTRA_FIELD_MAPPING_Z              4u
#define SPECTRA_FIELD_MAPPING_DIVERGENCE     5u
#define SPECTRA_FIELD_MAPPING_CURL_MAGNITUDE 6u
#define SPECTRA_FIELD_MAPPING_Q_CRITERION    7u

#define SPECTRA_FIELD_SAMPLING_CELL   0u
#define SPECTRA_FIELD_SAMPLING_VERTEX 1u
#define SPECTRA_FIELD_STORAGE_OPENVDB (1u << 16u)

#define SPECTRA_VECTOR_SPACE_GRID  0u
#define SPECTRA_VECTOR_SPACE_LOCAL 1u
#define SPECTRA_VECTOR_SPACE_WORLD 2u

#define SPECTRA_PARTICLE_POINTS  0u
#define SPECTRA_PARTICLE_DISCS   1u
#define SPECTRA_PARTICLE_SPHERES 2u

#define SPECTRA_COLOR_SPACE_SRGB       0u
#define SPECTRA_COLOR_SPACE_REC2020    1u
#define SPECTRA_COLOR_SPACE_ACES2065_1 2u

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
    inline constexpr std::uint32_t volume_procedural_cloud = SPECTRA_VOLUME_PROCEDURAL_CLOUD;
    inline constexpr std::uint32_t volume_openvdb          = SPECTRA_VOLUME_OPENVDB;

    inline constexpr std::uint32_t spectrum_rgb              = SPECTRA_SPECTRUM_RGB;
    inline constexpr std::uint32_t spectrum_constant         = SPECTRA_SPECTRUM_CONSTANT;
    inline constexpr std::uint32_t spectrum_blackbody        = SPECTRA_SPECTRUM_BLACKBODY;
    inline constexpr std::uint32_t spectrum_piecewise_linear = SPECTRA_SPECTRUM_PIECEWISE_LINEAR;
    inline constexpr std::uint32_t illuminant_none           = SPECTRA_ILLUMINANT_NONE;
    inline constexpr std::uint32_t illuminant_d65            = SPECTRA_ILLUMINANT_D65;
    inline constexpr std::uint32_t illuminant_d60            = SPECTRA_ILLUMINANT_D60;

    inline constexpr std::uint32_t visualization_segments           = SPECTRA_VISUALIZATION_SEGMENTS;
    inline constexpr std::uint32_t visualization_vectors            = SPECTRA_VISUALIZATION_VECTORS;
    inline constexpr std::uint32_t visualization_image              = SPECTRA_VISUALIZATION_IMAGE;
    inline constexpr std::uint32_t visualization_surface            = SPECTRA_VISUALIZATION_SURFACE;
    inline constexpr std::uint32_t visualization_particles          = SPECTRA_VISUALIZATION_PARTICLES;
    inline constexpr std::uint32_t visualization_volume_slice       = SPECTRA_VISUALIZATION_VOLUME_SLICE;
    inline constexpr std::uint32_t visualization_volume_ray_march   = SPECTRA_VISUALIZATION_VOLUME_RAY_MARCH;
    inline constexpr std::uint32_t visualization_volume_mip         = SPECTRA_VISUALIZATION_VOLUME_MIP;
    inline constexpr std::uint32_t visualization_volume_isosurface  = SPECTRA_VISUALIZATION_VOLUME_ISOSURFACE;
    inline constexpr std::uint32_t visualization_volume_glyphs      = SPECTRA_VISUALIZATION_VOLUME_GLYPHS;
    inline constexpr std::uint32_t visualization_volume_streamlines = SPECTRA_VISUALIZATION_VOLUME_STREAMLINES;
    inline constexpr std::uint32_t visualization_volume_lic         = SPECTRA_VISUALIZATION_VOLUME_LIC;
    inline constexpr std::uint32_t visualization_reference_overlay  = SPECTRA_VISUALIZATION_REFERENCE_OVERLAY;
    inline constexpr std::uint32_t visualization_reference_plane    = SPECTRA_VISUALIZATION_REFERENCE_PLANE;
    inline constexpr std::uint32_t visualization_volume_cells       = SPECTRA_VISUALIZATION_VOLUME_CELLS;
    inline constexpr std::uint32_t visualization_indexed_points     = SPECTRA_VISUALIZATION_INDEXED_POINTS;
    inline constexpr std::uint32_t visualization_indexed_segments   = SPECTRA_VISUALIZATION_INDEXED_SEGMENTS;
    inline constexpr std::uint32_t visualization_mesh_vectors       = SPECTRA_VISUALIZATION_MESH_VECTORS;
    inline constexpr std::uint32_t visualization_mesh_wireframe     = SPECTRA_VISUALIZATION_MESH_WIREFRAME;
    inline constexpr std::uint32_t visualization_mesh_vertices      = SPECTRA_VISUALIZATION_MESH_VERTICES;
    inline constexpr std::uint32_t visualization_vertex_normals     = SPECTRA_VISUALIZATION_VERTEX_NORMALS;
    inline constexpr std::uint32_t visualization_face_normals       = SPECTRA_VISUALIZATION_FACE_NORMALS;

    inline constexpr std::uint32_t diagnostic_lines            = SPECTRA_DIAGNOSTIC_LINES;
    inline constexpr std::uint32_t diagnostic_boxes            = SPECTRA_DIAGNOSTIC_BOXES;
    inline constexpr std::uint32_t diagnostic_triangle_edges   = SPECTRA_DIAGNOSTIC_TRIANGLE_EDGES;
    inline constexpr std::uint32_t diagnostic_mesh_vertices    = SPECTRA_DIAGNOSTIC_MESH_VERTICES;
    inline constexpr std::uint32_t diagnostic_mesh_normals     = SPECTRA_DIAGNOSTIC_MESH_NORMALS;
    inline constexpr std::uint32_t diagnostic_mesh_tangents    = SPECTRA_DIAGNOSTIC_MESH_TANGENTS;
    inline constexpr std::uint32_t diagnostic_sphere_centers   = SPECTRA_DIAGNOSTIC_SPHERE_CENTERS;
    inline constexpr std::uint32_t diagnostic_sphere_wireframe = SPECTRA_DIAGNOSTIC_SPHERE_WIREFRAME;
    inline constexpr std::uint32_t diagnostic_occupancy_cells  = SPECTRA_DIAGNOSTIC_OCCUPANCY_CELLS;
    inline constexpr std::uint32_t diagnostic_particle_points  = SPECTRA_DIAGNOSTIC_PARTICLE_POINTS;
    inline constexpr std::uint32_t diagnostic_particle_vectors = SPECTRA_DIAGNOSTIC_PARTICLE_VECTORS;

    inline constexpr std::uint32_t field_float      = SPECTRA_FIELD_FLOAT;
    inline constexpr std::uint32_t field_float3     = SPECTRA_FIELD_FLOAT3;
    inline constexpr std::uint32_t field_uint32     = SPECTRA_FIELD_UINT32;
    inline constexpr std::uint32_t field_mac_float3 = SPECTRA_FIELD_MAC_FLOAT3;

    inline constexpr std::uint32_t depth_tested  = SPECTRA_DEPTH_TESTED;
    inline constexpr std::uint32_t depth_xray    = SPECTRA_DEPTH_XRAY;
    inline constexpr std::uint32_t depth_overlay = SPECTRA_DEPTH_OVERLAY;

    inline constexpr std::uint32_t color_source_element = SPECTRA_COLOR_SOURCE_ELEMENT;
    inline constexpr std::uint32_t color_source_uniform = SPECTRA_COLOR_SOURCE_UNIFORM;
    inline constexpr std::uint32_t color_source_scalar  = SPECTRA_COLOR_SOURCE_SCALAR;

    inline constexpr std::uint32_t color_map_viridis   = SPECTRA_COLOR_MAP_VIRIDIS;
    inline constexpr std::uint32_t color_map_turbo     = SPECTRA_COLOR_MAP_TURBO;
    inline constexpr std::uint32_t color_map_cool_warm = SPECTRA_COLOR_MAP_COOL_WARM;
    inline constexpr std::uint32_t color_map_grayscale = SPECTRA_COLOR_MAP_GRAYSCALE;

    inline constexpr std::uint32_t field_mapping_value          = SPECTRA_FIELD_MAPPING_VALUE;
    inline constexpr std::uint32_t field_mapping_magnitude      = SPECTRA_FIELD_MAPPING_MAGNITUDE;
    inline constexpr std::uint32_t field_mapping_x              = SPECTRA_FIELD_MAPPING_X;
    inline constexpr std::uint32_t field_mapping_y              = SPECTRA_FIELD_MAPPING_Y;
    inline constexpr std::uint32_t field_mapping_z              = SPECTRA_FIELD_MAPPING_Z;
    inline constexpr std::uint32_t field_mapping_divergence     = SPECTRA_FIELD_MAPPING_DIVERGENCE;
    inline constexpr std::uint32_t field_mapping_curl_magnitude = SPECTRA_FIELD_MAPPING_CURL_MAGNITUDE;
    inline constexpr std::uint32_t field_mapping_q_criterion    = SPECTRA_FIELD_MAPPING_Q_CRITERION;

    inline constexpr std::uint32_t field_sampling_cell   = SPECTRA_FIELD_SAMPLING_CELL;
    inline constexpr std::uint32_t field_sampling_vertex = SPECTRA_FIELD_SAMPLING_VERTEX;
    inline constexpr std::uint32_t field_storage_openvdb = SPECTRA_FIELD_STORAGE_OPENVDB;

    inline constexpr std::uint32_t vector_space_grid  = SPECTRA_VECTOR_SPACE_GRID;
    inline constexpr std::uint32_t vector_space_local = SPECTRA_VECTOR_SPACE_LOCAL;
    inline constexpr std::uint32_t vector_space_world = SPECTRA_VECTOR_SPACE_WORLD;

    inline constexpr std::uint32_t particle_points  = SPECTRA_PARTICLE_POINTS;
    inline constexpr std::uint32_t particle_discs   = SPECTRA_PARTICLE_DISCS;
    inline constexpr std::uint32_t particle_spheres = SPECTRA_PARTICLE_SPHERES;

    inline constexpr std::uint32_t color_space_srgb       = SPECTRA_COLOR_SPACE_SRGB;
    inline constexpr std::uint32_t color_space_rec2020    = SPECTRA_COLOR_SPACE_REC2020;
    inline constexpr std::uint32_t color_space_aces2065_1 = SPECTRA_COLOR_SPACE_ACES2065_1;
} // namespace spectra::shader_semantics
#endif

#endif
