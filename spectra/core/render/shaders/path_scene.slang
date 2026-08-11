module path_scene;

#include "shader_semantics.h"

public static const uint gpu_geometry_attribute_normal = SPECTRA_GPU_ATTRIBUTE_NORMAL;
public static const uint gpu_geometry_attribute_tangent = SPECTRA_GPU_ATTRIBUTE_TANGENT;
public static const uint gpu_geometry_attribute_texture_coordinate = SPECTRA_GPU_ATTRIBUTE_TEXTURE_COORDINATE;

public struct PathPackedFloat3 {
    public float x;
    public float y;
    public float z;
}

public struct PathPrimitive {
    public uint material;
    public uint area_light;
    public uint first_light_shape;
    public uint flags;
    public DescriptorHandle<StructuredBuffer<uint>> indices;
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> normals;
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> tangents;
    public DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    public uint4 attributes;
    public float4 geometry_parameters;
    public uint4 material_binding;
    public uint4 media;
    public uint2 object_id;
    public uint2 reserved;
}

public static const uint geometry_triangle = SPECTRA_GEOMETRY_TRIANGLE;
public static const uint geometry_sphere = SPECTRA_GEOMETRY_SPHERE;
public static const uint geometry_disk = SPECTRA_GEOMETRY_DISK;
public static const uint geometry_cylinder = SPECTRA_GEOMETRY_CYLINDER;
public static const uint geometry_box = SPECTRA_GEOMETRY_BOX;
public static const uint geometry_rectangle = SPECTRA_GEOMETRY_RECTANGLE;
public static const uint geometry_sphere_set = SPECTRA_GEOMETRY_SPHERE_SET;

public static const uint material_interface = SPECTRA_MATERIAL_INTERFACE;
public static const uint material_diffuse = SPECTRA_MATERIAL_DIFFUSE;
public static const uint material_diffuse_transmission = SPECTRA_MATERIAL_DIFFUSE_TRANSMISSION;
public static const uint material_conductor = SPECTRA_MATERIAL_CONDUCTOR;
public static const uint material_dielectric = SPECTRA_MATERIAL_DIELECTRIC;
public static const uint material_thin_dielectric = SPECTRA_MATERIAL_THIN_DIELECTRIC;
public static const uint material_coated_diffuse = SPECTRA_MATERIAL_COATED_DIFFUSE;
public static const uint material_coated_conductor = SPECTRA_MATERIAL_COATED_CONDUCTOR;
public static const uint material_mix = SPECTRA_MATERIAL_MIX;

public struct PathMaterialHeader {
    public uint kind;
    public uint local_index;
    public uint normal_map;
    public uint bump_map;
    public uint2 texture_requests;
    public uint2 stable_id;
}

public struct PathMaterialTextureRequest {
    public uint4 data;
}

public static const uint material_texture_parameter = 0;
public static const uint material_texture_normal = 1;
public static const uint material_texture_bump = 2;
public static const uint material_texture_bump_u = 3;
public static const uint material_texture_bump_v = 4;

public struct TextureEvaluationContext {
    public float3 position;
    public float3 normal;
    public float2 texture_coordinates;
    public float3 position_dx;
    public float3 position_dy;
    public float2 texture_coordinates_dx;
    public float2 texture_coordinates_dy;
    public uint2 texture_requests;
    public DescriptorHandle<RWStructuredBuffer<float4>> material_texture_values;
    public uint material_texture_value_base;
    public uint2 reserved;
}

public struct PathSpectrumParameter {
    public uint spectrum;
    public uint texture;
    public uint2 reserved;
}

public struct PathFloatParameter {
    public float value;
    public uint texture;
    public uint2 reserved;
}

public struct PathDiffuseMaterial {
    public uint4 reflectance;
}

public struct PathDiffuseTransmissionMaterial {
    public uint4 reflectance;
    public uint4 transmittance;
    public float4 scales;
}

public struct PathConductorMaterial {
    public uint4 eta_or_reflectance;
    public uint4 k;
    public float4 roughness;
    public uint4 roughness_textures;
    public uint4 metadata;
}

public struct PathDielectricMaterial {
    public uint4 eta;
    public float4 roughness;
    public uint4 roughness_textures;
    public uint4 metadata;
}

public struct PathThinDielectricMaterial {
    public uint4 eta;
}

public struct PathCoatedDiffuseMaterial {
    public uint4 reflectance;
    public uint4 eta;
    public uint4 albedo;
    public float4 parameters;
    public uint4 parameter_textures;
    public uint4 metadata;
}

public struct PathCoatedConductorMaterial {
    public uint4 interface_eta;
    public uint4 eta_or_reflectance;
    public uint4 k;
    public uint4 albedo;
    public float4 interface_roughness;
    public uint4 interface_roughness_textures;
    public float4 conductor_roughness;
    public uint4 conductor_roughness_textures;
    public float4 coating;
    public uint4 coating_textures;
    public uint4 metadata;
}

public struct PathMixMaterial {
    public uint4 materials;
    public float amount;
    public uint amount_texture;
    public uint2 reserved;
}

public static const uint texture_constant = SPECTRA_TEXTURE_CONSTANT;
public static const uint texture_image = SPECTRA_TEXTURE_IMAGE;
public static const uint texture_checkerboard = SPECTRA_TEXTURE_CHECKERBOARD;
public static const uint texture_scale = SPECTRA_TEXTURE_SCALE;
public static const uint texture_mix = SPECTRA_TEXTURE_MIX;
public static const uint texture_direction_mix = SPECTRA_TEXTURE_DIRECTION_MIX;
public static const uint texture_bilerp = SPECTRA_TEXTURE_BILERP;

public static const uint mapping_uv = SPECTRA_MAPPING_UV;
public static const uint mapping_planar = SPECTRA_MAPPING_PLANAR;
public static const uint mapping_spherical = SPECTRA_MAPPING_SPHERICAL;
public static const uint mapping_cylindrical = SPECTRA_MAPPING_CYLINDRICAL;
public static const uint mapping_checkerboard_3d = SPECTRA_MAPPING_CHECKERBOARD_3D;

public static const uint area_source_triangle = SPECTRA_AREA_SOURCE_TRIANGLE;
public static const uint area_source_sphere_set = SPECTRA_AREA_SOURCE_SPHERE_SET;
public static const uint area_source_analytic = SPECTRA_AREA_SOURCE_ANALYTIC;

public static const uint spectrum_rgb = SPECTRA_SPECTRUM_RGB;
public static const uint spectrum_constant = SPECTRA_SPECTRUM_CONSTANT;
public static const uint spectrum_blackbody = SPECTRA_SPECTRUM_BLACKBODY;
public static const uint spectrum_piecewise_linear = SPECTRA_SPECTRUM_PIECEWISE_LINEAR;
public static const uint illuminant_none = SPECTRA_ILLUMINANT_NONE;
public static const uint illuminant_d65 = SPECTRA_ILLUMINANT_D65;
public static const uint illuminant_d60 = SPECTRA_ILLUMINANT_D60;

public struct PathTextureHeader {
    public uint kind;
    public uint local_index;
    public uint value_kind;
    public uint spectrum_type;
    public uint2 program;
    public uint2 reserved;
}

public struct PathTextureMapping {
    public uint4 metadata;
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 transform_row_3;
    public float4 parameter_0;
    public float4 parameter_1;
}

public struct PathConstantTexture {
    public float scalar;
    public uint spectrum;
    public uint2 reserved;
}

public struct PathImageTexture {
    public DescriptorHandle<Texture2D<float4>> image;
    public DescriptorHandle<SamplerState> sampler;
    public uint4 metadata;
    public float4 parameters;
}

public struct PathCheckerboardTexture {
    public uint4 data;
}

public struct PathScaleTexture {
    public uint4 data;
}

public struct PathMixTexture {
    public uint4 data;
}

public struct PathDirectionMixTexture {
    public uint4 data;
    public float4 direction;
}

public struct PathBilerpTexture {
    public float4 scalars;
    public uint4 spectra;
    public uint4 data;
}

public static const uint light_point = SPECTRA_LIGHT_POINT;
public static const uint light_spot = SPECTRA_LIGHT_SPOT;
public static const uint light_distant = SPECTRA_LIGHT_DISTANT;
public static const uint light_diffuse_area = SPECTRA_LIGHT_DIFFUSE_AREA;
public static const uint light_infinite = SPECTRA_LIGHT_INFINITE;
public static const uint light_portal_infinite = SPECTRA_LIGHT_PORTAL_INFINITE;

public static const uint light_delta_position = 1;
public static const uint light_delta_direction = 2;
public static const uint light_infinite_flag = 4;
public static const uint light_two_sided = 8;
public static const uint light_textured = 16;

public struct PathLight {
    public uint4 identifiers;
    public uint4 references;
    public float4 position_and_scale;
    public float4 parameters;
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 inverse_transform_row_0;
    public float4 inverse_transform_row_1;
    public float4 inverse_transform_row_2;
    public float4 selection;
}

public struct PathLightDistribution {
    public uint4 metadata;
    public uint4 offsets;
    public float4 parameters;
}

public struct PathPortal {
    public float4 point_0;
    public float4 point_2;
    public float4 frame_x;
    public float4 frame_y;
    public float4 frame_z;
}

public struct PathLightBvhNode {
    public float4 bounds_minimum_and_phi;
    public float4 bounds_maximum_and_cos_theta_o;
    public float4 direction_and_cos_theta_e;
    public uint4 metadata;
}

public struct PathSpectrum {
    public float4 parameters;
    public uint4 metadata;
}

public static const uint medium_homogeneous = SPECTRA_MEDIUM_HOMOGENEOUS;
public static const uint medium_volume = SPECTRA_MEDIUM_VOLUME;

public static const uint volume_density_grid = SPECTRA_VOLUME_DENSITY_GRID;
public static const uint volume_rgb_grid = SPECTRA_VOLUME_RGB_GRID;
public static const uint volume_nanovdb = SPECTRA_VOLUME_NANOVDB;
public static const uint volume_procedural_cloud = SPECTRA_VOLUME_PROCEDURAL_CLOUD;

public struct PathMedium {
    public uint4 spectra;
    public float4 scales;
    public float4 temperature;
    public uint4 metadata;
}

public struct PathVolume {
    public uint4 metadata;
    public uint4 majorant_metadata;
    public float4 bounds_minimum;
    public float4 bounds_maximum;
    public float4 inverse_transform_row_0;
    public float4 inverse_transform_row_1;
    public float4 inverse_transform_row_2;
    public float4 procedural_parameters;
    public DescriptorHandle<StructuredBuffer<float>> density;
    public DescriptorHandle<StructuredBuffer<float>> temperature;
    public DescriptorHandle<StructuredBuffer<float>> emission_scale;
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> sigma_a;
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> sigma_s;
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> emission;
    public DescriptorHandle<StructuredBuffer<uint>> nanovdb_density;
    public DescriptorHandle<StructuredBuffer<uint>> nanovdb_temperature;
    public DescriptorHandle<StructuredBuffer<float>> majorant;
}

public struct PathSceneBindings {
    public DescriptorHandle<StructuredBuffer<PathPrimitive>> primitives;
    public DescriptorHandle<StructuredBuffer<PathMaterialHeader>> material_headers;
    public DescriptorHandle<StructuredBuffer<PathDiffuseMaterial>> diffuse_materials;
    public DescriptorHandle<StructuredBuffer<PathDiffuseTransmissionMaterial>> diffuse_transmission_materials;
    public DescriptorHandle<StructuredBuffer<PathConductorMaterial>> conductor_materials;
    public DescriptorHandle<StructuredBuffer<PathDielectricMaterial>> dielectric_materials;
    public DescriptorHandle<StructuredBuffer<PathThinDielectricMaterial>> thin_dielectric_materials;
    public DescriptorHandle<StructuredBuffer<PathCoatedDiffuseMaterial>> coated_diffuse_materials;
    public DescriptorHandle<StructuredBuffer<PathCoatedConductorMaterial>> coated_conductor_materials;
    public DescriptorHandle<StructuredBuffer<PathMixMaterial>> mix_materials;
    public DescriptorHandle<StructuredBuffer<PathMaterialTextureRequest>> material_texture_requests;
    public DescriptorHandle<StructuredBuffer<PathTextureHeader>> texture_headers;
    public DescriptorHandle<StructuredBuffer<PathTextureMapping>> texture_mappings;
    public DescriptorHandle<StructuredBuffer<PathConstantTexture>> constant_textures;
    public DescriptorHandle<StructuredBuffer<PathImageTexture>> image_textures;
    public DescriptorHandle<StructuredBuffer<PathCheckerboardTexture>> checkerboard_textures;
    public DescriptorHandle<StructuredBuffer<PathScaleTexture>> scale_textures;
    public DescriptorHandle<StructuredBuffer<PathMixTexture>> mix_textures;
    public DescriptorHandle<StructuredBuffer<PathDirectionMixTexture>> direction_mix_textures;
    public DescriptorHandle<StructuredBuffer<PathBilerpTexture>> bilerp_textures;
    public DescriptorHandle<StructuredBuffer<PathLight>> lights;
    public DescriptorHandle<StructuredBuffer<PathLightShape>> light_shapes;
    public DescriptorHandle<StructuredBuffer<PathLightDistribution>> light_distributions;
    public DescriptorHandle<StructuredBuffer<float>> light_distribution_data;
    public DescriptorHandle<StructuredBuffer<PathPortal>> portals;
    public DescriptorHandle<StructuredBuffer<PathLightBvhNode>> light_bvh_nodes;
    public DescriptorHandle<StructuredBuffer<uint>> light_bvh_bit_trails;
    public DescriptorHandle<StructuredBuffer<uint>> face_materials;
    public DescriptorHandle<StructuredBuffer<PathMedium>> media;
    public DescriptorHandle<StructuredBuffer<PathVolume>> volumes;
    public DescriptorHandle<StructuredBuffer<PathSpectrum>> spectra;
    public DescriptorHandle<StructuredBuffer<float2>> piecewise_spectra;
    public DescriptorHandle<ByteAddressBuffer> cie_spectra;
    public DescriptorHandle<ByteAddressBuffer> rgb_to_spectrum_tables;
    public uint4 metadata;
    public uint4 light_metadata;
}

public struct PathLightShape {
    public float4 position_0_and_area;
    public float4 position_1;
    public float4 position_2;
    public uint4 identifiers;
    public float4 geometry_parameters;
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 texture_coordinates_0_1;
    public float4 texture_coordinates_2;
    public float4 normal_0;
    public float4 normal_1;
    public float4 normal_2;
}
