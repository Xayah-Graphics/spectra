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

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 ndc : TEXCOORD0;
};

struct FragmentOutput {
    float4 color : SV_Target;
    float depth : SV_Depth;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertexIndex : SV_VertexID) {
    float2 position = vertexIndex == 0 ? float2(-1.0, -1.0) : vertexIndex == 1 ? float2(-1.0, 3.0) : float2(3.0, -1.0);
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.ndc = position;
    return output;
}

float grid_line_alpha(float2 coordinate, float spacing) {
    float2 anchor = floor(camera.cameraPosition.xz / spacing) * spacing;
    float2 scaled = (coordinate - anchor) / spacing;
    float2 derivative = max(fwidth(scaled), float2(1.0e-5, 1.0e-5));
    float2 distanceToLine = abs(frac(scaled + 0.5) - 0.5);
    float2 line = 1.0 - smoothstep(float2(0.0, 0.0), derivative * 1.35, distanceToLine);
    return max(line.x, line.y);
}

float axis_line_alpha(float coordinate) {
    float derivative = max(fwidth(coordinate), 1.0e-5);
    return 1.0 - smoothstep(0.35, 1.25, abs(coordinate) / derivative);
}

float grid_lod_alpha(float2 coordinate, float pixelWorld) {
    float lodValue = log10(max(pixelWorld * 64.0, 1.0e-5));
    float lodFloor = floor(lodValue);
    float lodBlend = saturate(lodValue - lodFloor);
    float fineSpacing = pow(10.0, lodFloor - 1.0);
    float mediumSpacing = pow(10.0, lodFloor);
    float coarseSpacing = pow(10.0, lodFloor + 1.0);

    float fineAlpha = grid_line_alpha(coordinate, fineSpacing) * (1.0 - lodBlend) * 0.10;
    float mediumAlpha = grid_line_alpha(coordinate, mediumSpacing) * lerp(0.30, 0.10, lodBlend);
    float coarseAlpha = grid_line_alpha(coordinate, coarseSpacing) * lodBlend * 0.30;
    return max(max(fineAlpha, mediumAlpha), coarseAlpha);
}

[shader("fragment")]
FragmentOutput fragment_main(VertexOutput input) {
    float4 nearPosition = mul(float4(input.ndc, 0.0, 1.0), camera.clipToWorld);
    float4 farPosition = mul(float4(input.ndc, 1.0, 1.0), camera.clipToWorld);
    if (abs(nearPosition.w) <= 1.0e-6 || abs(farPosition.w) <= 1.0e-6) discard;

    float3 nearWorld = nearPosition.xyz / nearPosition.w;
    float3 farWorld = farPosition.xyz / farPosition.w;
    float3 ray = farWorld - nearWorld;
    if (abs(ray.y) <= 1.0e-6) discard;

    float planeT = -nearWorld.y / ray.y;
    if (planeT <= 0.0) discard;

    float3 worldPosition = nearWorld + ray * planeT;
    float4 clipPosition = mul(float4(worldPosition, 1.0), camera.worldToClip);
    if (clipPosition.w <= 0.0) discard;

    float depth = clipPosition.z / clipPosition.w;
    if (depth < 0.0 || depth > 1.0) discard;

    float2 coordinate = worldPosition.xz;
    float pixelWorld = max(length(ddx(coordinate)), length(ddy(coordinate)));
    pixelWorld = max(pixelWorld, 1.0e-5);
    float xAxisAlpha = axis_line_alpha(coordinate.y) * 0.78;
    float zAxisAlpha = axis_line_alpha(coordinate.x) * 0.72;

    float3 gridColor = float3(0.105, 0.115, 0.128);
    float alpha = grid_lod_alpha(coordinate, pixelWorld);
    float3 color = gridColor;
    if (xAxisAlpha > alpha) {
        alpha = xAxisAlpha;
        color = float3(0.64, 0.12, 0.10);
    }
    if (zAxisAlpha > alpha) {
        alpha = zAxisAlpha;
        color = float3(0.12, 0.28, 0.72);
    }

    float3 rayDirection = normalize(ray);
    float grazingFade = saturate(abs(rayDirection.y) * 8.0);
    float distanceFade = saturate(1.0 - length(worldPosition - camera.cameraPosition) / max(camera.viewport.w * 18.0, 96.0));
    alpha *= grazingFade * distanceFade;
    if (alpha <= 0.002) discard;

    FragmentOutput output;
    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
