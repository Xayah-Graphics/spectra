struct PbrtPreviewParameters {
    float4x4 view_projection;
    float4 light_direction;
};

struct VertexInput {
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[[vk::push_constant]]
PbrtPreviewParameters parameters;

struct VertexOutput {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput vertex) {
    VertexOutput output;
    output.position = mul(parameters.view_projection, float4(vertex.position, 1.0));
    output.normal = normalize(vertex.normal);
    output.color = saturate(vertex.color);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    const float3 normal = normalize(input.normal);
    const float3 light_direction = normalize(parameters.light_direction.xyz);
    const float diffuse = max(dot(normal, -light_direction), 0.0);
    const float lighting = 0.22 + diffuse * 0.78;
    return float4(saturate(input.color * lighting), 1.0);
}
