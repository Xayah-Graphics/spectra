static const uint MaxViewportDirectLights = 8;

struct CameraUniform {
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
    float3 cameraPosition;
    float _pad0;
    float3 environmentColor;
    float environmentIntensity;
    float4 lightDirections[MaxViewportDirectLights];
    float4 lightColorIntensities[MaxViewportDirectLights];
    uint4 lightCounts;
    float3 cameraRight;
    float _pad1;
    float3 cameraUp;
    float _pad2;
    float4 viewport;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;

struct PointCloudInput {
    [[vk::location(0)]] float4 positionRadius : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 localPosition : TEXCOORD0;
    [[vk::location(1)]] float4 color : COLOR0;
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
    output.position = mul(float4(world, 1.0), camera.viewProjection);
    output.localPosition = corner;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    float distanceSquared = dot(input.localPosition, input.localPosition);
    if (distanceSquared > 1.0) discard;
    float alpha = input.color.a * saturate((1.0 - distanceSquared) * 1.6);
    return float4(input.color.rgb, alpha);
}
