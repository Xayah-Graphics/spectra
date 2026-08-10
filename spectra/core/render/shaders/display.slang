module display;

import aces_2;

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
    float3 linear_srgb = source.SampleLevel(sampler, input.texture_coordinate, 0.0f).xyz;
    if (push_data.color_space == 1)
        linear_srgb = float3(
            dot(float3(1.660491, -0.587641, -0.072850), linear_srgb),
            dot(float3(-0.124550, 1.132900, -0.008349), linear_srgb),
            dot(float3(-0.018151, -0.100579, 1.118730), linear_srgb));
    else if (push_data.color_space == 2)
        linear_srgb = float3(
            dot(float3(2.521686, -1.134130, -0.387556), linear_srgb),
            dot(float3(-0.276479, 1.372719, -0.096240), linear_srgb),
            dot(float3(-0.015378, -0.152975, 1.168353), linear_srgb));
    const float3 acescg = float3(
        dot(float3(0.613132, 0.339538, 0.047417), linear_srgb),
        dot(float3(0.070124, 0.916394, 0.013452), linear_srgb),
        dot(float3(0.020588, 0.109575, 0.869785), linear_srgb)) * exp2(push_data.exposure);
    return float4(aces2_display_linear_from_acescg(acescg), 1.0f);
}
