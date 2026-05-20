struct SpectraRasterDrawGpu {
    float4x4 object_from_local;
    float4x4 normal_from_local;
    uint material_index;
    uint3 padding;
};

struct SpectraRasterMaterialGpu {
    float4 base_color_roughness;
};

struct SpectraRasterPushConstants {
    float4x4 view_projection;
    uint draw_index;
    uint3 padding;
};

[[vk::binding(0, 0)]]
StructuredBuffer<SpectraRasterDrawGpu> raster_draws;

[[vk::binding(1, 0)]]
StructuredBuffer<SpectraRasterMaterialGpu> raster_materials;

[[vk::push_constant]]
SpectraRasterPushConstants raster_parameters;

struct VertexInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_normal : NORMAL0;
    nointerpolation uint material_index : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput vertex) {
    const SpectraRasterDrawGpu draw = raster_draws[raster_parameters.draw_index];
    const float4 world_position = mul(draw.object_from_local, float4(vertex.position, 1.0));

    VertexOutput output;
    output.position = mul(raster_parameters.view_projection, world_position);
    output.world_normal = normalize(mul(draw.normal_from_local, float4(vertex.normal, 0.0)).xyz);
    output.material_index = draw.material_index;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    const SpectraRasterMaterialGpu material = raster_materials[input.material_index];
    const float3 normal = normalize(input.world_normal);
    const float3 light_direction = normalize(float3(-0.35, -0.75, -0.55));
    const float diffuse = max(dot(normal, -light_direction), 0.0);
    const float lighting = 0.22 + 0.78 * diffuse;
    return float4(saturate(material.base_color_roughness.xyz * lighting), 1.0);
}
