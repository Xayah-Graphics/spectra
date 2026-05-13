struct MeshParameters {
    float4x4 view_projection;
    float4x4 model;
    float4x4 normal_matrix;
    float4 light_direction;
};

struct VertexInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[[vk::binding(0, 0)]]
StructuredBuffer<MeshParameters> parameters_buffer;

struct VertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput vertex) {
    const MeshParameters parameters = parameters_buffer[0];

    VertexOutput output;
    const float4 world_position = mul(parameters.model, float4(vertex.position, 1.0));
    output.position = mul(parameters.view_projection, world_position);
    output.normal = normalize(mul(parameters.normal_matrix, float4(vertex.normal, 0.0)).xyz);
    output.color = saturate(vertex.color);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    const MeshParameters parameters = parameters_buffer[0];
    const float3 normal = normalize(input.normal);
    const float3 light_direction = normalize(parameters.light_direction.xyz);
    const float diffuse = max(dot(normal, -light_direction), 0.0);
    const float lighting = 0.18 + diffuse * 0.82;
    return float4(saturate(input.color * lighting), 1.0);
}
