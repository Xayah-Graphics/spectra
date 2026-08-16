module display;

import aces_2;
import color;

struct DisplayPushData {
    DescriptorHandle<Texture2D<float4>> source;
    DescriptorHandle<SamplerState> sampler;
    float exposure;
    uint color_space;
}

[vk::push_constant]
uniform DisplayPushData push_data;

struct DisplayVertexOutput {
    float4 position : SV_Position;
    float2 texture_coordinate : TEXCOORD0;
}

[shader("vertex")]
DisplayVertexOutput display_vertex(uint vertex_index : SV_VertexID) {
    float2 coordinate = float2((vertex_index << 1) & 2, vertex_index & 2);
    DisplayVertexOutput output;
    output.position = float4(coordinate * 2.0f - 1.0f, 0.0f, 1.0f);
    output.texture_coordinate = coordinate;
    return output;
}

[shader("fragment")]
float4 display_fragment(DisplayVertexOutput input) : SV_Target0 {
    Texture2D<float4> source = push_data.source;
    SamplerState sampler = push_data.sampler;
    const float3 linear_srgb = color_to_linear_srgb(source.SampleLevel(sampler, input.texture_coordinate, 0.0f).xyz, push_data.color_space);
    const float3 acescg = float3(
        dot(float3(0.613132, 0.339538, 0.047417), linear_srgb),
        dot(float3(0.070124, 0.916394, 0.013452), linear_srgb),
        dot(float3(0.020588, 0.109575, 0.869785), linear_srgb)) * exp2(push_data.exposure);
    return float4(aces2_display_linear_from_acescg(acescg), 1.0f);
}
