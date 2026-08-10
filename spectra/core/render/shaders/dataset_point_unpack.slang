module dataset_point_unpack;

import plugin_dataset;

struct SphereAabb {
    float3 minimum;
    float3 maximum;
}

struct PointUnpackPushData {
    DescriptorHandle<StructuredBuffer<SpectraPluginPoint>> source;
    DescriptorHandle<RWStructuredBuffer<float3>> positions;
    DescriptorHandle<RWStructuredBuffer<float>> radii;
    DescriptorHandle<RWStructuredBuffer<SphereAabb>> axis_aligned_boxes;
    uint point_count;
    uint reserved;
}

[vk::push_constant]
uniform PointUnpackPushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void unpack_dataset_points(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint point_index = dispatch_thread_id.x;
    if (point_index >= push_data.point_count) return;
    StructuredBuffer<SpectraPluginPoint> source = push_data.source;
    RWStructuredBuffer<float3> positions = push_data.positions;
    RWStructuredBuffer<float> radii = push_data.radii;
    RWStructuredBuffer<SphereAabb> axis_aligned_boxes = push_data.axis_aligned_boxes;
    const SpectraPluginPoint point = source[point_index];
    positions[point_index] = point.position;
    radii[point_index] = point.radius;
    axis_aligned_boxes[point_index] = {point.position - point.radius, point.position + point.radius};
}
