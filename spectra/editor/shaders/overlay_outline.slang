module overlay_outline;

struct OutlinePushData {
    DescriptorHandle<Texture2D<float4>> mask;
    DescriptorHandle<SamplerState> sampler;
    float2 inverse_extent;
    uint2 reserved;
}
[vk::push_constant]
uniform OutlinePushData push_data;

struct OutlineVertexOutput {
    float4 position : SV_Position;
    float2 texture_coordinate : TEXCOORD0;
}

[shader("vertex")]
OutlineVertexOutput overlay_vertex(uint vertex_index : SV_VertexID) {
    const float2 coordinate = float2((vertex_index << 1) & 2, vertex_index & 2);
    OutlineVertexOutput output;
    output.position = float4(coordinate * 2.0 - 1.0, 0.0, 1.0);
    output.texture_coordinate = coordinate;
    return output;
}

[shader("fragment")]
float4 overlay_outline(OutlineVertexOutput input) : SV_Target0 {
    Texture2D<float4> mask = push_data.mask;
    SamplerState sampler = push_data.sampler;
    const float4 center = mask.SampleLevel(sampler, input.texture_coordinate, 0.0);
    float4 nearest = float4(0.0);
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) continue;
            const float4 sample_value = mask.SampleLevel(
                sampler,
                input.texture_coordinate + float2(x, y) * push_data.inverse_extent,
                0.0);
            if (sample_value.a > nearest.a) nearest = sample_value;
        }
    if (center.a > 0.0 || nearest.a == 0.0) discard;
    return float4(nearest.rgb, 0.92);
}
