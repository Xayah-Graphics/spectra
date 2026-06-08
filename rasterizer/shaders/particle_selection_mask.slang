struct CameraUniform {
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
    float3 cameraPosition;
    float _pad0;
    float3 lightDirection;
    float _pad1;
    float3 lightColor;
    float lightIntensity;
    float3 cameraRight;
    float _pad2;
    float3 cameraUp;
    float _pad3;
    float4 viewport;
};

struct ParticleSelectionPushConstants {
    float4 color;
    uint objectId;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::push_constant]] ConstantBuffer<ParticleSelectionPushConstants> selection;

struct ParticleInput {
    [[vk::location(0)]] float4 positionRadius : POSITION;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 localPosition : TEXCOORD0;
};

float2 quad_corner(uint vertexIndex) {
    uint corner = vertexIndex % 6;
    if (corner == 0) return float2(-1.0, -1.0);
    if (corner == 1) return float2(1.0, -1.0);
    if (corner == 2) return float2(1.0, 1.0);
    if (corner == 3) return float2(-1.0, -1.0);
    if (corner == 4) return float2(1.0, 1.0);
    return float2(-1.0, 1.0);
}

[shader("vertex")]
VertexOutput vertex_main(ParticleInput input, uint vertexIndex : SV_VertexID) {
    float2 corner = quad_corner(vertexIndex);
    float radius = input.positionRadius.w;
    float3 world = input.positionRadius.xyz + camera.cameraRight * corner.x * radius + camera.cameraUp * corner.y * radius;
    VertexOutput output;
    output.position = mul(float4(world, 1.0), camera.viewProjection);
    output.localPosition = corner;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    if (dot(input.localPosition, input.localPosition) > 1.0) discard;
    return selection.color;
}
