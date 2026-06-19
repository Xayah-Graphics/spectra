static const uint MaxViewportDirectLights = 8;
static const uint ViewportSegmentWidthModeWorld = 1;

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

struct ViewportSegmentInput {
    [[vk::location(0)]] float4 startWidth : POSITION0;
    [[vk::location(1)]] float3 end : POSITION1;
    [[vk::location(2)]] uint flags : TEXCOORD0;
    [[vk::location(3)]] float4 color : COLOR0;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

float segment_endpoint(uint vertexIndex) {
    uint corner = vertexIndex % 6;
    if (corner == 1 || corner == 2 || corner == 4) return 1.0;
    return 0.0;
}

float segment_side(uint vertexIndex) {
    uint corner = vertexIndex % 6;
    if (corner == 0 || corner == 1 || corner == 3) return -1.0;
    return 1.0;
}

float3 safe_normalize(float3 value, float3 alternate) {
    float lengthSquared = dot(value, value);
    if (lengthSquared <= 1.0e-12) return alternate;
    return value * rsqrt(lengthSquared);
}

float2 safe_normalize_2d(float2 value, float2 alternate) {
    float lengthSquared = dot(value, value);
    if (lengthSquared <= 1.0e-12) return alternate;
    return value * rsqrt(lengthSquared);
}

[shader("vertex")]
VertexOutput vertex_main(ViewportSegmentInput input, uint vertexIndex : SV_VertexID) {
    float3 start = input.startWidth.xyz;
    float width = input.startWidth.w;
    float3 end = input.end;
    float endpoint = segment_endpoint(vertexIndex);
    float side = segment_side(vertexIndex);

    VertexOutput output;
    output.color = input.color;
    if (input.flags == ViewportSegmentWidthModeWorld) {
        float3 segmentDirection = safe_normalize(end - start, float3(1.0, 0.0, 0.0));
        float3 midpoint = (start + end) * 0.5;
        float3 viewDirection = safe_normalize(midpoint - camera.cameraPosition, float3(0.0, 0.0, 1.0));
        float3 sideDirection = cross(viewDirection, segmentDirection);
        if (dot(sideDirection, sideDirection) <= 1.0e-12) sideDirection = cross(camera.cameraUp, segmentDirection);
        if (dot(sideDirection, sideDirection) <= 1.0e-12) sideDirection = cross(camera.cameraRight, segmentDirection);
        sideDirection = safe_normalize(sideDirection, camera.cameraRight);
        float3 world = lerp(start, end, endpoint) + sideDirection * side * width * 0.5;
        output.position = mul(float4(world, 1.0), camera.worldToClip);
        return output;
    }

    float4 startClip = mul(float4(start, 1.0), camera.worldToClip);
    float4 endClip = mul(float4(end, 1.0), camera.worldToClip);
    float2 startNdc = startClip.xy / startClip.w;
    float2 endNdc = endClip.xy / endClip.w;
    float2 direction = safe_normalize_2d(endNdc - startNdc, float2(1.0, 0.0));
    float2 perpendicular = float2(-direction.y, direction.x);
    float2 offset = perpendicular * side * max(width, 1.0) / max(camera.viewport.xy, float2(1.0, 1.0));
    float4 clip = endpoint < 0.5 ? startClip : endClip;
    clip.xy += offset * clip.w;
    output.position = clip;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    return input.color;
}
