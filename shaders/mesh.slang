struct MeshVertex {
    float4 position;
    float4 normal;
    float4 color;
};

struct MeshParameters {
    float4x4 view_projection;
    float4 light_direction;
};

[[vk::binding(0, 0)]]
StructuredBuffer<MeshVertex> vertices;

[[vk::binding(1, 0)]]
StructuredBuffer<uint> indices;

[[vk::binding(2, 0)]]
StructuredBuffer<MeshParameters> parameters_buffer;

struct VertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_index : SV_VertexID) {
    const uint mesh_vertex_index = indices[vertex_index];
    const MeshVertex vertex = vertices[mesh_vertex_index];
    const MeshParameters parameters = parameters_buffer[0];

    VertexOutput output;
    output.position = mul(parameters.view_projection, float4(vertex.position.xyz, 1.0));
    output.normal = normalize(vertex.normal.xyz);
    output.color = saturate(vertex.color.xyz);
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
