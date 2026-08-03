module presenter;

import aces_2;

struct PresenterPushData {
    DescriptorHandle<Texture2D<float4>> source;
    DescriptorHandle<SamplerState> sampler;
    float exposure;
    uint reserved;
}

[vk::push_constant]
uniform PresenterPushData push_data;

struct PresenterVertexOutput {
    float4 position : SV_Position;
    float2 texture_coordinate : TEXCOORD0;
}

[shader("vertex")]
PresenterVertexOutput presenter_vertex(uint vertex_index : SV_VertexID) {
    float2 coordinate = float2((vertex_index << 1) & 2, vertex_index & 2);
    PresenterVertexOutput output;
    output.position = float4(coordinate * 2.0f - 1.0f, 0.0f, 1.0f);
    output.texture_coordinate = coordinate;
    return output;
}

[shader("fragment")]
float4 presenter_fragment(PresenterVertexOutput input) : SV_Target0 {
    Texture2D<float4> source = push_data.source;
    SamplerState sampler = push_data.sampler;
    float3 acescg = source.SampleLevel(sampler, input.texture_coordinate, 0.0f).xyz * exp2(push_data.exposure);
    return float4(aces2_display_linear_from_acescg(acescg), 1.0f);
}
