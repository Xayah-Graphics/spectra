struct OutlinePushConstants {
    float2 inverseExtent;
    float2 _pad0;
};

[[vk::binding(0, 0)]] Texture2D<float4> selectionMask;
[[vk::binding(1, 0)]] SamplerState selectionSampler;
[[vk::push_constant]] ConstantBuffer<OutlinePushConstants> outline;

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertexIndex : SV_VertexID) {
    float2 position = vertexIndex == 0 ? float2(-1.0, -1.0) : vertexIndex == 1 ? float2(3.0, -1.0) : float2(-1.0, 3.0);
    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.uv = position * 0.5 + 0.5;
    return output;
}

float mask_strength(float4 value) {
    return max(max(value.r, value.g), value.b);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    float2 texel = outline.inverseExtent;
    float4 center = selectionMask.SampleLevel(selectionSampler, input.uv, 0.0);
    float4 nearMask = center;
    nearMask = max(nearMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(texel.x, 0.0), 0.0));
    nearMask = max(nearMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(-texel.x, 0.0), 0.0));
    nearMask = max(nearMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(0.0, texel.y), 0.0));
    nearMask = max(nearMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(0.0, -texel.y), 0.0));
    float4 farMask = nearMask;
    farMask = max(farMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(texel.x, texel.y) * 2.0, 0.0));
    farMask = max(farMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(-texel.x, texel.y) * 2.0, 0.0));
    farMask = max(farMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(texel.x, -texel.y) * 2.0, 0.0));
    farMask = max(farMask, selectionMask.SampleLevel(selectionSampler, input.uv + float2(-texel.x, -texel.y) * 2.0, 0.0));

    float centerStrength = mask_strength(center);
    float nearStrength = mask_strength(nearMask);
    float farStrength = mask_strength(farMask);
    float edge = saturate((nearStrength - centerStrength) * 3.0);
    float glow = saturate((farStrength - centerStrength) * 0.7);
    if (edge <= 0.001 && glow <= 0.001 && centerStrength <= 0.001) discard;

    float selected = max(farMask.g, center.g);
    float active = max(farMask.b, center.b);
    float hover = max(farMask.r, center.r) * (1.0 - selected);
    float3 color = float3(0.72, 0.82, 0.92);
    if (selected > 0.001) color = float3(0.05, 0.84, 1.0);
    if (active > 0.001) color = lerp(float3(0.05, 0.84, 1.0), float3(1.0, 0.62, 0.20), 0.45);
    color *= max(max(hover, selected), active);
    float fill = centerStrength * (active > 0.001 ? 0.12 : 0.08);
    float alpha = saturate(edge * 0.92 + glow * 0.28 + fill);
    return float4(color, alpha);
}
