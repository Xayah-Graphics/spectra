module dataset_instance_apply;

struct SceneTransform {
    float4 row_0;
    float4 row_1;
    float4 row_2;
    float4 row_3;
}

struct InstanceBinding {
    uint2 instance_id;
    uint acceleration_index;
    uint reserved;
    SceneTransform primitive_from_instance;
}

struct InstanceApplyPushData {
    DescriptorHandle<ByteAddressBuffer> updates;
    DescriptorHandle<StructuredBuffer<InstanceBinding>> bindings;
    DescriptorHandle<RWStructuredBuffer<SceneTransform>> transforms;
    DescriptorHandle<RWByteAddressBuffer> acceleration_instances;
    uint update_count;
    uint primitive_count;
}

[vk::push_constant]
uniform InstanceApplyPushData push_data;

float4 transform_row(ByteAddressBuffer updates, uint offset, uint row) {
    return updates.Load<float4>(offset + row * 16u);
}

SceneTransform multiply_transform(ByteAddressBuffer updates, uint offset, SceneTransform child) {
    const float4 parent_0 = transform_row(updates, offset, 0u);
    const float4 parent_1 = transform_row(updates, offset, 1u);
    const float4 parent_2 = transform_row(updates, offset, 2u);
    const float4 parent_3 = transform_row(updates, offset, 3u);
    const float4 column_0 = float4(child.row_0.x, child.row_1.x, child.row_2.x, child.row_3.x);
    const float4 column_1 = float4(child.row_0.y, child.row_1.y, child.row_2.y, child.row_3.y);
    const float4 column_2 = float4(child.row_0.z, child.row_1.z, child.row_2.z, child.row_3.z);
    const float4 column_3 = float4(child.row_0.w, child.row_1.w, child.row_2.w, child.row_3.w);
    return {
        float4(dot(parent_0, column_0), dot(parent_0, column_1), dot(parent_0, column_2), dot(parent_0, column_3)),
        float4(dot(parent_1, column_0), dot(parent_1, column_1), dot(parent_1, column_2), dot(parent_1, column_3)),
        float4(dot(parent_2, column_0), dot(parent_2, column_1), dot(parent_2, column_2), dot(parent_2, column_3)),
        float4(dot(parent_3, column_0), dot(parent_3, column_1), dot(parent_3, column_2), dot(parent_3, column_3)),
    };
}

[shader("compute")]
[numthreads(64, 1, 1)]
void apply_instance_transforms(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint primitive_index = dispatch_thread_id.x;
    if (primitive_index >= push_data.primitive_count) return;
    ByteAddressBuffer updates = push_data.updates;
    StructuredBuffer<InstanceBinding> bindings = push_data.bindings;
    const InstanceBinding binding = bindings[primitive_index];
    for (uint update_index = 0; update_index < push_data.update_count; ++update_index) {
        const uint update_offset = update_index * 72u;
        const uint2 update_id = updates.Load<uint2>(update_offset);
        if (any(update_id != binding.instance_id)) continue;
        const SceneTransform transform = multiply_transform(updates, update_offset + 8u, binding.primitive_from_instance);
        RWStructuredBuffer<SceneTransform> transforms = push_data.transforms;
        transforms[primitive_index] = transform;
        if (binding.acceleration_index != 0xffffffff) {
            RWByteAddressBuffer acceleration_instances = push_data.acceleration_instances;
            const uint address = binding.acceleration_index * 64;
            acceleration_instances.Store4(address, asuint(transform.row_0));
            acceleration_instances.Store4(address + 16, asuint(transform.row_1));
            acceleration_instances.Store4(address + 32, asuint(transform.row_2));
        }
        break;
    }
}
