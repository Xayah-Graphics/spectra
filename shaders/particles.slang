struct ParticleParameters {
    float4x4 view_projection;
};

[[vk::push_constant]]
ConstantBuffer<ParticleParameters> parameters;

struct VertexInput {
    float3 position : POSITION0;
    float2 local_position : TEXCOORD0;
    float3 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 local_position : TEXCOORD0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position = mul(parameters.view_projection, float4(input.position, 1.0));
    output.local_position = input.local_position;
    output.color = saturate(input.color);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    if (dot(input.local_position, input.local_position) > 1.0) discard;
    return float4(input.color, 1.0);
}
