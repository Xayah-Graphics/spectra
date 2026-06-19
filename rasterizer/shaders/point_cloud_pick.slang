static const uint MaxViewportDirectLights = 8;

struct CameraUniform {
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float3 cameraPosition;
    float _pad0;
    float3 environmentColor;
    float environmentIntensity;
    float4 lightDirections[MaxViewportDirectLights];
    float4 lightColorIntensities[MaxViewportDirectLights];
    uint4 lightCounts;
    float3 cameraForward;
    float _pad1;
    float3 cameraRight;
    float _pad2;
    float3 cameraUp;
    float _pad3;
    float4 viewport;
};

struct PointCloudSelectionPushConstants {
    float4 color;
    uint objectId;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::push_constant]] ConstantBuffer<PointCloudSelectionPushConstants> selection;

struct PointCloudInput {
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
VertexOutput vertex_main(PointCloudInput input, uint vertexIndex : SV_VertexID) {
    float2 corner = quad_corner(vertexIndex);
    float radius = input.positionRadius.w;
    float3 world = input.positionRadius.xyz + camera.cameraRight * corner.x * radius + camera.cameraUp * corner.y * radius;
    VertexOutput output;
    output.position = mul(float4(world, 1.0), camera.worldToClip);
    output.localPosition = corner;
    return output;
}

[shader("fragment")]
uint fragment_main(VertexOutput input) : SV_Target {
    if (dot(input.localPosition, input.localPosition) > 1.0) discard;
    return selection.objectId;
}
