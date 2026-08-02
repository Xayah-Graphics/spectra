module volume_update;

struct MajorantPushData {
    DescriptorHandle<StructuredBuffer<float>> density;
    DescriptorHandle<StructuredBuffer<float3>> sigma_a;
    DescriptorHandle<StructuredBuffer<float3>> sigma_s;
    DescriptorHandle<RWStructuredBuffer<float>> majorant;
    uint4 resolution;
    uint4 brick_minimum;
    uint4 brick_maximum;
    uint4 metadata;
}

[vk::push_constant]
uniform MajorantPushData push_data;

[shader("compute")]
[numthreads(64, 1, 1)]
void build_majorant(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint linear = dispatch_thread_id.x;
    const uint3 extent =
        push_data.brick_maximum.xyz -
        push_data.brick_minimum.xyz;
    const uint count =
        extent.x * extent.y * extent.z;
    if (linear >= count) return;
    const uint3 coordinate =
        push_data.brick_minimum.xyz +
        uint3(
            linear % extent.x,
            (linear / extent.x) % extent.y,
            linear / (extent.x * extent.y));
    const int3 minimum = max(
        int3(
            floor(
                float3(coordinate) *
                    float3(push_data.resolution.xyz) /
                    16.0 -
                0.5)),
        int3(0));
    const int3 maximum = min(
        int3(
            floor(
                float3(coordinate + 1) *
                    float3(push_data.resolution.xyz) /
                    16.0 -
                0.5)) +
            1,
        int3(push_data.resolution.xyz) - 1);
    float value = 0.0;
    if (push_data.metadata.x == 0) {
        StructuredBuffer<float> density =
            push_data.density;
        for (int z = minimum.z; z <= maximum.z; ++z)
            for (int y = minimum.y; y <= maximum.y; ++y)
                for (int x = minimum.x; x <= maximum.x; ++x)
                    value = max(
                        value,
                        density[
                            (z * push_data.resolution.y + y) *
                                push_data.resolution.x +
                            x]);
    }
    else {
        StructuredBuffer<float3> sigma_a =
            push_data.sigma_a;
        StructuredBuffer<float3> sigma_s =
            push_data.sigma_s;
        for (int z = minimum.z; z <= maximum.z; ++z)
            for (int y = minimum.y; y <= maximum.y; ++y)
                for (int x = minimum.x; x <= maximum.x; ++x) {
                    const uint index =
                        (z * push_data.resolution.y + y) *
                            push_data.resolution.x +
                        x;
                    const float3 extinction =
                        (
                            (push_data.metadata.y & 1u) != 0
                                ? max(
                                      sigma_a[index],
                                      0.0)
                                : float3(1.0)
                        ) +
                        (
                            (push_data.metadata.y & 2u) != 0
                                ? max(
                                      sigma_s[index],
                                      0.0)
                                : float3(1.0)
                        );
                    value = max(
                        value,
                        max(
                            extinction.x,
                            max(
                                extinction.y,
                                extinction.z)) *
                            4.0);
                }
    }
    RWStructuredBuffer<float> majorant = push_data.majorant;
    majorant[
        (coordinate.z * 16u + coordinate.y) *
            16u +
        coordinate.x] = value;
}
