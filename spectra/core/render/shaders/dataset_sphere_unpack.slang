module dataset_sphere_unpack;

import plugin_dataset;

struct SphereAabb {
    float3 minimum;
    float3 maximum;
}

struct SphereUnpackPushData {
    DescriptorHandle<StructuredBuffer<SpectraPluginSphere>> source;
    DescriptorHandle<RWStructuredBuffer<float3>> positions;
    DescriptorHandle<RWStructuredBuffer<float>> radii;
    DescriptorHandle<RWStructuredBuffer<SphereAabb>> axis_aligned_boxes;
    uint sphere_count;
    uint reserved;
}

[vk::push_constant]
uniform SphereUnpackPushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void unpack_dataset_spheres(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint sphere_index = dispatch_thread_id.x;
    if (sphere_index >= push_data.sphere_count) return;
    StructuredBuffer<SpectraPluginSphere> source = push_data.source;
    RWStructuredBuffer<float3> positions = push_data.positions;
    RWStructuredBuffer<float> radii = push_data.radii;
    RWStructuredBuffer<SphereAabb> axis_aligned_boxes = push_data.axis_aligned_boxes;
    const SpectraPluginSphere sphere = source[sphere_index];
    positions[sphere_index] = sphere.position;
    radii[sphere_index] = sphere.radius;
    axis_aligned_boxes[sphere_index] = {sphere.position - sphere.radius, sphere.position + sphere.radius};
}
