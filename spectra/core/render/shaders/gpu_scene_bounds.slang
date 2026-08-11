module gpu_scene_bounds;

struct GpuSceneBoundsPushData {
    DescriptorHandle<ByteAddressBuffer> positions;
    DescriptorHandle<ByteAddressBuffer> radii;
    DescriptorHandle<RWByteAddressBuffer> bounds;
    DescriptorHandle<ByteAddressBuffer> transforms;
    uint4 metadata;
}

[vk::push_constant]
uniform GpuSceneBoundsPushData push_data;

groupshared uint3 group_minimum[256];
groupshared uint3 group_maximum[256];

uint float_order(float value) {
    const uint bits = asuint(value);
    return (bits & 0x80000000) != 0 ? ~bits : bits ^ 0x80000000;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void clear_instance_bounds(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (dispatch_thread_id.x >= push_data.metadata.x) return;
    RWByteAddressBuffer bounds = push_data.bounds;
    bounds.Store4(dispatch_thread_id.x * 32, uint4(0xffffffff, 0xffffffff, 0xffffffff, 0));
    bounds.Store4(dispatch_thread_id.x * 32 + 16, uint4(0, 0, 0, 0));
}

[shader("compute")]
[numthreads(256, 1, 1)]
void accumulate_instance_bounds(uint3 dispatch_thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID) {
    const uint index = dispatch_thread_id.x;
    uint3 minimum = uint3(0xffffffff);
    uint3 maximum = uint3(0);
    if (index < push_data.metadata.x) {
        ByteAddressBuffer positions = push_data.positions;
        ByteAddressBuffer transforms = push_data.transforms;
        const uint transform_offset = push_data.metadata.w * 64;
        const float4 row_0 = transforms.Load<float4>(transform_offset);
        const float4 row_1 = transforms.Load<float4>(transform_offset + 16);
        const float4 row_2 = transforms.Load<float4>(transform_offset + 32);
        const float4 local = float4(positions.Load<float3>(index * 12), 1.0);
        const float3 position = float3(dot(row_0, local), dot(row_1, local), dot(row_2, local));
        float3 extent = float3(0.0);
        if (push_data.metadata.z == 1) {
            ByteAddressBuffer radii = push_data.radii;
            const float radius = radii.Load<float>(index * 4);
            extent = radius * float3(length(row_0.xyz), length(row_1.xyz), length(row_2.xyz));
        }
        const float3 lower = position - extent;
        const float3 upper = position + extent;
        minimum = uint3(float_order(lower.x), float_order(lower.y), float_order(lower.z));
        maximum = uint3(float_order(upper.x), float_order(upper.y), float_order(upper.z));
    }
    group_minimum[group_thread_id.x] = minimum;
    group_maximum[group_thread_id.x] = maximum;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 128; stride != 0; stride >>= 1) {
        if (group_thread_id.x < stride) {
            group_minimum[group_thread_id.x] = min(group_minimum[group_thread_id.x], group_minimum[group_thread_id.x + stride]);
            group_maximum[group_thread_id.x] = max(group_maximum[group_thread_id.x], group_maximum[group_thread_id.x + stride]);
        }
        GroupMemoryBarrierWithGroupSync();
    }
    if (group_thread_id.x != 0) return;
    RWByteAddressBuffer bounds = push_data.bounds;
    const uint offset = push_data.metadata.y * 32;
    bounds.InterlockedMin(offset, group_minimum[0].x);
    bounds.InterlockedMin(offset + 4, group_minimum[0].y);
    bounds.InterlockedMin(offset + 8, group_minimum[0].z);
    bounds.InterlockedMax(offset + 16, group_maximum[0].x);
    bounds.InterlockedMax(offset + 20, group_maximum[0].y);
    bounds.InterlockedMax(offset + 24, group_maximum[0].z);
}
