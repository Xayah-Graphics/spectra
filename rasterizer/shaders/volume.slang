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

struct VolumePushConstants {
    float4 originDensityScale;
    float4 extentStepScale;
    float4 base_color;
    float4 emission;
    float4 material;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::binding(1, 0)]] Texture3D<float> densityTexture;
[[vk::binding(2, 0)]] Texture3D<float> temperatureTexture;
[[vk::binding(3, 0)]] SamplerState volumeSampler;
[[vk::push_constant]] ConstantBuffer<VolumePushConstants> volume;

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 localPosition : TEXCOORD0;
    [[vk::location(1)]] float3 worldPosition : TEXCOORD1;
};

uint cube_index(uint vertexIndex) {
    uint index = vertexIndex % 36;
    if (index == 0) return 0;
    if (index == 1) return 1;
    if (index == 2) return 2;
    if (index == 3) return 0;
    if (index == 4) return 2;
    if (index == 5) return 3;
    if (index == 6) return 5;
    if (index == 7) return 4;
    if (index == 8) return 7;
    if (index == 9) return 5;
    if (index == 10) return 7;
    if (index == 11) return 6;
    if (index == 12) return 4;
    if (index == 13) return 0;
    if (index == 14) return 3;
    if (index == 15) return 4;
    if (index == 16) return 3;
    if (index == 17) return 7;
    if (index == 18) return 1;
    if (index == 19) return 5;
    if (index == 20) return 6;
    if (index == 21) return 1;
    if (index == 22) return 6;
    if (index == 23) return 2;
    if (index == 24) return 3;
    if (index == 25) return 2;
    if (index == 26) return 6;
    if (index == 27) return 3;
    if (index == 28) return 6;
    if (index == 29) return 7;
    if (index == 30) return 4;
    if (index == 31) return 5;
    if (index == 32) return 1;
    if (index == 33) return 4;
    if (index == 34) return 1;
    return 0;
}

float3 cube_corner(uint cornerIndex) {
    if (cornerIndex == 0) return float3(0.0, 0.0, 0.0);
    if (cornerIndex == 1) return float3(1.0, 0.0, 0.0);
    if (cornerIndex == 2) return float3(1.0, 1.0, 0.0);
    if (cornerIndex == 3) return float3(0.0, 1.0, 0.0);
    if (cornerIndex == 4) return float3(0.0, 0.0, 1.0);
    if (cornerIndex == 5) return float3(1.0, 0.0, 1.0);
    if (cornerIndex == 6) return float3(1.0, 1.0, 1.0);
    return float3(0.0, 1.0, 1.0);
}

[shader("vertex")]
VertexOutput vertex_main(uint vertexIndex : SV_VertexID) {
    float3 localPosition = cube_corner(cube_index(vertexIndex));
    float3 origin = volume.originDensityScale.xyz;
    float3 extent = volume.extentStepScale.xyz;
    float3 worldPosition = origin + localPosition * extent;
    VertexOutput output;
    output.position = mul(float4(worldPosition, 1.0), camera.viewProjection);
    output.localPosition = localPosition;
    output.worldPosition = worldPosition;
    return output;
}

float2 intersect_unit_box(float3 rayOrigin, float3 rayDirection) {
    float3 inverseDirection = 1.0 / rayDirection;
    float3 t0 = (float3(0.0, 0.0, 0.0) - rayOrigin) * inverseDirection;
    float3 t1 = (float3(1.0, 1.0, 1.0) - rayOrigin) * inverseDirection;
    float3 nearT = min(t0, t1);
    float3 farT = max(t0, t1);
    float enterT = max(max(nearT.x, nearT.y), nearT.z);
    float exitT = min(min(farT.x, farT.y), farT.z);
    return float2(enterT, exitT);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    if (!isFrontFace) discard;
    float3 origin = volume.originDensityScale.xyz;
    float densityScale = volume.originDensityScale.w;
    float temperatureScale = volume.material.x;
    float3 extent = volume.extentStepScale.xyz;
    float stepScale = volume.extentStepScale.w;
    float3 cameraLocal = (camera.cameraPosition.xyz - origin) / extent;
    float3 rayDirection = normalize(input.localPosition - cameraLocal);
    float2 interval = intersect_unit_box(cameraLocal, rayDirection);
    if (interval.y <= max(interval.x, 0.0)) discard;

    float startT = max(interval.x, 0.0);
    float endT = interval.y;
    float stepLength = max((endT - startT) / 96.0 * stepScale, 0.001);
    float4 accumulated = float4(0.0, 0.0, 0.0, 0.0);
    for (int stepIndex = 0; stepIndex < 160; ++stepIndex) {
        float t = startT + (float(stepIndex) + 0.5) * stepLength;
        if (t > endT || accumulated.a > 0.985) break;
        float3 samplePosition = cameraLocal + rayDirection * t;
        float density = max(densityTexture.SampleLevel(volumeSampler, samplePosition, 0.0), 0.0) * densityScale;
        float temperature = max(temperatureTexture.SampleLevel(volumeSampler, samplePosition, 0.0), 0.0);
        float alpha = saturate(1.0 - exp(-density));
        float3 smokeColor = volume.base_color.rgb + volume.emission.rgb * saturate(temperature * temperatureScale);
        float contribution = (1.0 - accumulated.a) * alpha;
        accumulated.rgb += smokeColor * contribution;
        accumulated.a += contribution;
    }
    if (accumulated.a <= 0.001) discard;
    accumulated.a *= volume.base_color.a;
    return accumulated;
}
