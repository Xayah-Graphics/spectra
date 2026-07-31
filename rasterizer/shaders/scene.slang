module scene;

public struct RasterPrimitive {
    public uint transform;
    public uint material;
    public uint area_light;
    public uint flags;
    public uint face_material_offset;
    public uint face_material_count;
    public uint2 reserved;
}

public struct RasterTransform {
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 transform_row_3;
}

public struct RasterMaterial {
    public uint4 metadata;
    public uint4 materials;
    public float4 spectrum_value_0;
    public uint4 spectrum_data_0;
    public float4 spectrum_value_1;
    public uint4 spectrum_data_1;
    public float4 spectrum_value_2;
    public uint4 spectrum_data_2;
    public float4 spectrum_value_3;
    public uint4 spectrum_data_3;
    public float4 scalar_values_0;
    public uint4 scalar_textures_0;
    public float4 scalar_values_1;
    public uint4 scalar_textures_1;
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
    public uint4 metadata;
}

public struct RasterPushData {
    public DescriptorHandle<StructuredBuffer<float3>> positions;
    public DescriptorHandle<StructuredBuffer<float3>> normals;
    public DescriptorHandle<StructuredBuffer<float3>> tangents;
    public DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    public DescriptorHandle<StructuredBuffer<uint>> indices;
    public DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    public uint instance_index;
    public uint element_count;
    public uint2 attributes;
    public float4 view_projection_row_0;
    public float4 view_projection_row_1;
    public float4 view_projection_row_2;
    public float4 view_projection_row_3;
    public float4 camera_position;
}

public struct GpuPickPrimitive {
    public uint4 metadata;
    public DescriptorHandle<StructuredBuffer<float3>> positions;
    public DescriptorHandle<StructuredBuffer<float>> radii;
    public float4 parameters;
}
