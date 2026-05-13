struct CameraPushConstants {
    float4x4 view_projection;
};

[[vk::push_constant]]
ConstantBuffer<CameraPushConstants> camera;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

struct VertexInput {
    float3 position : POSITION0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    output.position = mul(camera.view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
