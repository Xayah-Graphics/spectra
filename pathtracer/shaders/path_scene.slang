module path_scene;

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
    public DescriptorHandle<StructuredBuffer<PathPackedFloat3>> particle_positions;
    public DescriptorHandle<StructuredBuffer<float>> particle_radii;
    public uint4 attributes;
    public float4 geometry_parameters;
    public uint4 material_binding;
    public uint4 media;
    public uint2 object_id;
    public uint2 reserved;
}

public static const uint geometry_triangle = 0;
public static const uint geometry_sphere = 1;
public static const uint geometry_disk = 2;
public static const uint geometry_cylinder = 3;
public static const uint geometry_box = 4;
public static const uint geometry_rectangle = 5;
public static const uint geometry_particle = 6;

public static const uint material_interface = 0;
public static const uint material_diffuse = 1;
public static const uint material_diffuse_transmission = 2;
public static const uint material_conductor = 3;
public static const uint material_dielectric = 4;
public static const uint material_thin_dielectric = 5;
public static const uint material_coated_diffuse = 6;
public static const uint material_coated_conductor = 7;
public static const uint material_mix = 8;

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

public static const uint texture_constant = 0;
public static const uint texture_image = 1;
public static const uint texture_checkerboard = 2;
public static const uint texture_scale = 3;
public static const uint texture_mix = 4;
public static const uint texture_direction_mix = 5;
public static const uint texture_bilerp = 6;

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

public static const uint light_point = 0;
public static const uint light_spot = 1;
public static const uint light_distant = 2;
public static const uint light_diffuse_area = 3;
public static const uint light_infinite = 4;
public static const uint light_portal_infinite = 5;

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

public static const uint medium_homogeneous = 0;
public static const uint medium_volume = 1;

public static const uint volume_density_grid = 0;
public static const uint volume_rgb_grid = 1;
public static const uint volume_nanovdb = 2;
public static const uint volume_procedural_cloud = 3;

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
