static const uint MaxViewportDirectLights = 8;

struct CameraUniform {
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float3 cameraPosition;
    float _pad0;
    float3 environmentColor;
    float environmentIntensity;
    float4 lightDirections[MaxViewportDirectLights];
    float4 lightPositions[MaxViewportDirectLights];
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

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::binding(0, 1)]] Texture2D<float4> imageTexture;
[[vk::binding(1, 1)]] SamplerState imageSampler;

struct ViewportImagePlaneInput {
    [[vk::location(0)]] float4 modelRow0 : TEXCOORD0;
    [[vk::location(1)]] float4 modelRow1 : TEXCOORD1;
    [[vk::location(2)]] float4 modelRow3 : TEXCOORD2;
    [[vk::location(3)]] float4 tint : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 tint : COLOR0;
};

float2 quad_corner(uint vertexIndex) {
    uint index = vertexIndex % 6;
    if (index == 0) return float2(-0.5, -0.5);
    if (index == 1) return float2(0.5, -0.5);
    if (index == 2) return float2(0.5, 0.5);
    if (index == 3) return float2(-0.5, -0.5);
    if (index == 4) return float2(0.5, 0.5);
    return float2(-0.5, 0.5);
}

float2 quad_uv(uint vertexIndex) {
    uint index = vertexIndex % 6;
    if (index == 0) return float2(0.0, 1.0);
    if (index == 1) return float2(1.0, 1.0);
    if (index == 2) return float2(1.0, 0.0);
    if (index == 3) return float2(0.0, 1.0);
    if (index == 4) return float2(1.0, 0.0);
    return float2(0.0, 0.0);
}

[shader("vertex")]
VertexOutput vertex_main(ViewportImagePlaneInput input, uint vertexIndex : SV_VertexID) {
    float2 corner = quad_corner(vertexIndex);
    float4 world = input.modelRow0 * corner.x + input.modelRow1 * corner.y + input.modelRow3;
    VertexOutput output;
    output.position = mul(world, camera.worldToClip);
    output.uv = quad_uv(vertexIndex);
    output.tint = input.tint;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    float4 texel = imageTexture.Sample(imageSampler, input.uv);
    return texel * input.tint;
}
