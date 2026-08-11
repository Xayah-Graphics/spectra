module raster_scene;

public static const uint gpu_geometry_attribute_tangent = 1u << 1u;
public static const uint gpu_geometry_attribute_texture_coordinate = 1u << 2u;

public static const uint raster_display_wireframe = 1;

public struct RasterPrimitive {
    public uint transform_index;
    public uint material_index;
    public uint area_light_index;
    public uint flags;
    public uint face_material_offset;
    public uint face_material_count;
    public uint alpha_texture;
    public uint reserved;
}

public struct RasterTransform {
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 transform_row_3;
}

public float3 raster_transform_position(
    RasterTransform transform,
    float3 position) {
    const float4 homogeneous =
        float4(position, 1.0);
    return float3(
        dot(
            transform.transform_row_0,
            homogeneous),
        dot(
            transform.transform_row_1,
            homogeneous),
        dot(
            transform.transform_row_2,
            homogeneous));
}

public float3 raster_transform_normal(
    RasterTransform transform,
    float3 normal) {
    const float3 column_0 =
        float3(
            transform.transform_row_0.x,
            transform.transform_row_1.x,
            transform.transform_row_2.x);
    const float3 column_1 =
        float3(
            transform.transform_row_0.y,
            transform.transform_row_1.y,
            transform.transform_row_2.y);
    const float3 column_2 =
        float3(
            transform.transform_row_0.z,
            transform.transform_row_1.z,
            transform.transform_row_2.z);
    return normalize(
        cross(column_1, column_2) *
                normal.x +
            cross(column_2, column_0) *
                normal.y +
            cross(column_0, column_1) *
                normal.z);
}

public float3 raster_inverse_transform_position(
    RasterTransform transform,
    float3 position) {
    const float3 column_0 =
        float3(
            transform.transform_row_0.x,
            transform.transform_row_1.x,
            transform.transform_row_2.x);
    const float3 column_1 =
        float3(
            transform.transform_row_0.y,
            transform.transform_row_1.y,
            transform.transform_row_2.y);
    const float3 column_2 =
        float3(
            transform.transform_row_0.z,
            transform.transform_row_1.z,
            transform.transform_row_2.z);
    const float determinant =
        dot(
            column_0,
            cross(column_1, column_2));
    const float3 offset =
        position -
        float3(
            transform.transform_row_0.w,
            transform.transform_row_1.w,
            transform.transform_row_2.w);
    return float3(
        dot(cross(column_1, column_2), offset),
        dot(cross(column_2, column_0), offset),
        dot(cross(column_0, column_1), offset)) /
        determinant;
}

public float3 raster_inverse_transform_vector(
    RasterTransform transform,
    float3 vector) {
    const float3 column_0 =
        float3(
            transform.transform_row_0.x,
            transform.transform_row_1.x,
            transform.transform_row_2.x);
    const float3 column_1 =
        float3(
            transform.transform_row_0.y,
            transform.transform_row_1.y,
            transform.transform_row_2.y);
    const float3 column_2 =
        float3(
            transform.transform_row_0.z,
            transform.transform_row_1.z,
            transform.transform_row_2.z);
    const float determinant =
        dot(
            column_0,
            cross(column_1, column_2));
    return float3(
        dot(cross(column_1, column_2), vector),
        dot(cross(column_2, column_0), vector),
        dot(cross(column_0, column_1), vector)) /
        determinant;
}

public struct RasterMaterial {
    public uint4 metadata;
    public uint4 materials;
    public float4 spectrum_value_0;
    public uint4 spectrum_data_0;
    public float4 spectrum_value_1;
    public uint4 spectrum_data_1;
    public float4 scalar_values_0;
    public uint4 scalar_textures_0;
}

public struct RasterTextureHeader {
    public uint4 metadata;
    public uint2 program;
    public uint2 reserved;
}

public struct RasterTextureMapping {
    public uint4 metadata;
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 transform_row_3;
    public float4 parameter_0;
    public float4 parameter_1;
}

public struct RasterConstantTexture {
    public float4 value;
}

public struct RasterImageTexture {
    public DescriptorHandle<Texture2D<float4>> image;
    public DescriptorHandle<SamplerState> sampler;
    public uint4 metadata;
    public float4 parameters;
}

public struct RasterCompositeTexture {
    public uint4 data;
}

public struct RasterDirectionMixTexture {
    public uint4 data;
    public float4 direction;
}

public struct RasterBilerpTexture {
    public float4 values[4];
    public uint4 data;
}

public struct RasterAreaLight {
    public float4 emission;
    public uint4 metadata;
}

public struct RasterSceneBindings {
    public DescriptorHandle<StructuredBuffer<RasterPrimitive>> primitives;
    public DescriptorHandle<StructuredBuffer<RasterTransform>> transforms;
    public DescriptorHandle<StructuredBuffer<RasterMaterial>> materials;
    public DescriptorHandle<StructuredBuffer<uint>> face_materials;
    public DescriptorHandle<StructuredBuffer<RasterAreaLight>> area_lights;
    public DescriptorHandle<StructuredBuffer<RasterTextureHeader>> texture_headers;
    public DescriptorHandle<StructuredBuffer<RasterTextureMapping>> texture_mappings;
    public DescriptorHandle<StructuredBuffer<RasterConstantTexture>> constant_textures;
    public DescriptorHandle<StructuredBuffer<RasterImageTexture>> image_textures;
    public DescriptorHandle<StructuredBuffer<RasterCompositeTexture>> checkerboard_textures;
    public DescriptorHandle<StructuredBuffer<RasterCompositeTexture>> scale_textures;
    public DescriptorHandle<StructuredBuffer<RasterCompositeTexture>> mix_textures;
    public DescriptorHandle<StructuredBuffer<RasterDirectionMixTexture>> direction_mix_textures;
    public DescriptorHandle<StructuredBuffer<RasterBilerpTexture>> bilerp_textures;
}

public struct RasterPushData {
    public DescriptorHandle<StructuredBuffer<float3>> positions;
    public DescriptorHandle<StructuredBuffer<float3>> normals;
    public DescriptorHandle<StructuredBuffer<float3>> tangents;
    public DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    public DescriptorHandle<StructuredBuffer<uint>> indices;
    public DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    public uint scene_primitive_index;
    public uint element_count;
    public uint2 attributes;
    public float4 view_projection_row_0;
    public float4 view_projection_row_1;
    public float4 view_projection_row_2;
    public float4 view_projection_row_3;
    public float4 camera_position;
    public float4 camera_right;
    public float4 camera_up;
    public float4 camera_forward;
}

public struct RasterVolume {
    public uint4 metadata;
    public float4 bounds_minimum;
    public float4 bounds_maximum;
    public float4 inverse_transform_row_0;
    public float4 inverse_transform_row_1;
    public float4 inverse_transform_row_2;
    public float4 sigma_a;
    public float4 sigma_s;
    public float4 emission;
    public float4 scales;
    public float4 temperature;
    public float4 procedural_parameters;
    public DescriptorHandle<StructuredBuffer<float>> density;
    public DescriptorHandle<StructuredBuffer<float>> temperature_field;
    public DescriptorHandle<StructuredBuffer<float3>> sigma_a_field;
    public DescriptorHandle<StructuredBuffer<float3>> sigma_s_field;
    public DescriptorHandle<StructuredBuffer<float>> emission_scale_field;
    public DescriptorHandle<StructuredBuffer<float3>> emission_field;
}
