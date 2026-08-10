module raster_accumulate;

struct RasterAccumulationPushData {
    DescriptorHandle<RWTexture2D<float4>> sample;
    DescriptorHandle<RWTexture2D<float4>> output;
    uint2 extent;
    uint sample_index;
    uint reserved;
}

[vk::push_constant]
uniform RasterAccumulationPushData push_data;

[shader("compute")]
[numthreads(8, 8, 1)]
void raster_accumulate(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel = dispatch_thread_id.xy;
    if (any(pixel >= push_data.extent)) return;
    RWTexture2D<float4> sample = push_data.sample;
    RWTexture2D<float4> output = push_data.output;
    const float4 value = sample[pixel];
    output[pixel] = push_data.sample_index == 0 ? value : (output[pixel] * float(push_data.sample_index) + value) / float(push_data.sample_index + 1);
}
