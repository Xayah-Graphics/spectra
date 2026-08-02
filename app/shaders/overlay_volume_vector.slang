module overlay_volume_vector;

struct VolumeVectorPushData {
    DescriptorHandle<StructuredBuffer<float3>> velocity;
    uint2 reserved;
    uint4 resolution;
    float4 bounds_minimum;
    float4 bounds_maximum;
    float4 transform_row_0;
    float4 transform_row_1;
    float4 transform_row_2;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
    float4 parameters;
}

struct VolumeVectorOutput {
    float4 position : SV_Position;
    float4 color : COLOR;
}

[vk::push_constant]
uniform VolumeVectorPushData push_data;

[shader("vertex")]
VolumeVectorOutput overlay_volume_vector_vertex(
    uint vertex_id : SV_VertexID) {
    const uint sample_index = vertex_id / 6;
    const uint segment_vertex = vertex_id % 6;
    const uint samples_per_axis =
        uint(push_data.parameters.y);
    const uint3 sample = uint3(
        sample_index % samples_per_axis,
        (
            sample_index /
            samples_per_axis
        ) %
            samples_per_axis,
        sample_index /
            (
                samples_per_axis *
                samples_per_axis
            ));
    const float3 coordinate =
        (
            float3(sample) +
            0.5
        ) /
        float(samples_per_axis);
    const uint3 grid =
        min(
            uint3(
                coordinate *
                float3(
                    push_data.resolution.xyz)),
            push_data.resolution.xyz - 1);
    StructuredBuffer<float3> velocities =
        push_data.velocity;
    const float3 local_velocity =
        velocities[
            (
                grid.z *
                    push_data.resolution.y +
                grid.y
            ) *
                push_data.resolution.x +
            grid.x];
    const float4 local_position =
        float4(
            lerp(
                push_data.bounds_minimum.xyz,
                push_data.bounds_maximum.xyz,
                coordinate),
            1.0);
    const float3 position = float3(
        dot(
            push_data.transform_row_0,
            local_position),
        dot(
            push_data.transform_row_1,
            local_position),
        dot(
            push_data.transform_row_2,
            local_position));
    const float3 velocity = float3(
        dot(
            push_data.transform_row_0.xyz,
            local_velocity),
        dot(
            push_data.transform_row_1.xyz,
            local_velocity),
        dot(
            push_data.transform_row_2.xyz,
            local_velocity));
    const float magnitude =
        length(velocity);
    const float3 direction =
        magnitude > 1.0e-6
            ? velocity / magnitude
            : float3(1.0, 0.0, 0.0);
    const float arrow_length =
        push_data.parameters.x *
        min(magnitude, 2.0);
    const float3 endpoint =
        position +
        direction *
            arrow_length;
    const float3 side =
        normalize(
            abs(direction.y) < 0.9
                ? cross(
                      direction,
                      float3(0.0, 1.0, 0.0))
                : cross(
                      direction,
                      float3(1.0, 0.0, 0.0))) *
        arrow_length *
        0.16;
    const float3 back =
        endpoint -
        direction *
            arrow_length *
            0.24;
    float3 world_position;
    if (segment_vertex < 2)
        world_position =
            segment_vertex == 0
                ? position
                : endpoint;
    else if (segment_vertex < 4)
        world_position =
            segment_vertex == 2
                ? endpoint
                : back + side;
    else
        world_position =
            segment_vertex == 4
                ? endpoint
                : back - side;
    const float4 homogeneous =
        float4(
            world_position,
            1.0);
    VolumeVectorOutput output;
    output.position = float4(
        dot(
            push_data.view_projection_row_0,
            homogeneous),
        dot(
            push_data.view_projection_row_1,
            homogeneous),
        dot(
            push_data.view_projection_row_2,
            homogeneous),
        dot(
            push_data.view_projection_row_3,
            homogeneous));
    output.color = float4(
        lerp(
            float3(0.20, 0.75, 1.0),
            float3(1.0, 0.45, 0.08),
            saturate(magnitude * 0.25)),
        magnitude > 1.0e-6
            ? 0.9
            : 0.0);
    return output;
}
