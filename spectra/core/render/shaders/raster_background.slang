module raster_background;

import raster_scene;
import raster_light;

struct RasterBackgroundPushData {
    DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    uint2 extent;
    uint2 reserved;
    uint4 film_bounds;
    float4 inverse_view_projection_row_0;
    float4 inverse_view_projection_row_1;
    float4 inverse_view_projection_row_2;
    float4 inverse_view_projection_row_3;
}

[vk::push_constant]
uniform RasterBackgroundPushData push_data;

[shader("compute")]
[numthreads(8, 8, 1)]
void raster_background(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel = dispatch_thread_id.xy;
    if (any(pixel >= push_data.extent)) return;
    RWTexture2D<float4> output = push_data.output;
    if (pixel.x < push_data.film_bounds.x || pixel.y < push_data.film_bounds.y || pixel.x >= push_data.film_bounds.z || pixel.y >= push_data.film_bounds.w) {
        output[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    const float2 coordinate = (float2(pixel) + 0.5) / float2(push_data.extent);
    const float2 ndc = float2(coordinate.x * 2.0 - 1.0, 1.0 - coordinate.y * 2.0);
    const float4 near_clip = float4(ndc, 0.0, 1.0);
    const float4 far_clip = float4(ndc, 1.0, 1.0);
    const float4 near_homogeneous = float4(
        dot(push_data.inverse_view_projection_row_0, near_clip),
        dot(push_data.inverse_view_projection_row_1, near_clip),
        dot(push_data.inverse_view_projection_row_2, near_clip),
        dot(push_data.inverse_view_projection_row_3, near_clip));
    const float4 far_homogeneous = float4(
        dot(push_data.inverse_view_projection_row_0, far_clip),
        dot(push_data.inverse_view_projection_row_1, far_clip),
        dot(push_data.inverse_view_projection_row_2, far_clip),
        dot(push_data.inverse_view_projection_row_3, far_clip));
    const float3 direction = normalize(far_homogeneous.xyz / far_homogeneous.w - near_homogeneous.xyz / near_homogeneous.w);
    StructuredBuffer<RasterSceneBindings> binding_table = push_data.bindings;
    const RasterSceneBindings bindings = binding_table[0];
    StructuredBuffer<RasterLight> lights = bindings.lights;
    float3 radiance = float3(0.0);
    bool portal_environment = false;
    for (uint light_index = 0; light_index != bindings.metadata.z; ++light_index) {
        const uint kind = lights[light_index].metadata.x;
        if (kind == 7 || (kind == 8 && !portal_environment)) radiance += raster_environment_emission(lights[light_index], bindings, direction);
        if (kind == 8) portal_environment = true;
    }
    output[pixel] = float4(radiance, 1.0);
}
