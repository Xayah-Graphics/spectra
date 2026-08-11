module overlay_axes;

struct AxesPushData {
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
    float4 inverse_view_projection_row_0;
    float4 inverse_view_projection_row_1;
    float4 inverse_view_projection_row_2;
    float4 inverse_view_projection_row_3;
    uint4 metadata;
}
struct AxesVertex {
    float4 position : SV_Position;
    float2 normalized_device_coordinate : TEXCOORD0;
}

struct AxesFragment {
    float4 color : SV_Target0;
    float depth : SV_Depth;
}

[vk::push_constant]
uniform AxesPushData push_data;

float4 view_projection(float4 value) {
    return float4(
        dot(push_data.view_projection_row_0, value),
        dot(push_data.view_projection_row_1, value),
        dot(push_data.view_projection_row_2, value),
        dot(push_data.view_projection_row_3, value));
}

float4 inverse_view_projection(float4 value) {
    return float4(
        dot(push_data.inverse_view_projection_row_0, value),
        dot(push_data.inverse_view_projection_row_1, value),
        dot(push_data.inverse_view_projection_row_2, value),
        dot(push_data.inverse_view_projection_row_3, value));
}

float3 pixel_homogeneous(float4 clip_position) {
    const float2 extent =
        float2(
            push_data.metadata.y,
            push_data.metadata.z);
    return float3(
        (clip_position.x + clip_position.w) *
            extent.x *
            0.5,
        (clip_position.w - clip_position.y) *
            extent.y *
            0.5,
        clip_position.w);
}

float axis_alpha(
    float3 direction,
    float2 pixel) {
    const float3 origin =
        pixel_homogeneous(
            view_projection(
                float4(
                    0.0,
                    0.0,
                    0.0,
                    1.0)));
    const float3 point_at_infinity =
        pixel_homogeneous(
            view_projection(
                float4(
                    direction,
                    0.0)));
    const float3 line =
        cross(
            origin,
            point_at_infinity);
    const float normalization =
        length(line.xy);
    if (normalization <= 1.0e-8)
        return 0.0;
    const float distance =
        abs(
            dot(
                line,
                float3(
                    pixel,
                    1.0))) /
        normalization;
    return 1.0 -
        smoothstep(
            0.35,
            1.35,
            distance);
}

[shader("vertex")]
AxesVertex overlay_axes_vertex(uint vertex_id : SV_VertexID) {
    const float2 coordinate =
        float2((vertex_id << 1) & 2, vertex_id & 2);
    const float2 normalized_device_coordinate =
        coordinate * 2.0 - 1.0;
    AxesVertex output;
    output.position =
        float4(normalized_device_coordinate, 0.0, 1.0);
    output.normalized_device_coordinate =
        normalized_device_coordinate;
    return output;
}

[shader("fragment")]
AxesFragment overlay_axes_fragment(AxesVertex input) {
    float4 near_homogeneous =
        inverse_view_projection(
            float4(
                input.normalized_device_coordinate,
                0.0,
                1.0));
    float4 far_homogeneous =
        inverse_view_projection(
            float4(
                input.normalized_device_coordinate,
                1.0,
                1.0));
    const float3 near_position =
        near_homogeneous.xyz / near_homogeneous.w;
    const float3 far_position =
        far_homogeneous.xyz / far_homogeneous.w;
    const float3 ray_direction =
        far_position - near_position;
    const uint plane = push_data.metadata.x;
    const float3 plane_normal =
        plane == 1
            ? float3(0.0, 0.0, 1.0)
            : plane == 2
                  ? float3(1.0, 0.0, 0.0)
                  : float3(0.0, 1.0, 0.0);
    const float denominator =
        dot(ray_direction, plane_normal);
    if (abs(denominator) <= 1.0e-8) discard;
    const float ray_parameter =
        -dot(near_position, plane_normal) /
        denominator;
    if (ray_parameter <= 0.0 || ray_parameter >= 1.0)
        discard;
    const float3 world_position =
        near_position +
        ray_direction * ray_parameter;
    const float3 first_direction =
        plane == 2
            ? float3(0.0, 1.0, 0.0)
            : float3(1.0, 0.0, 0.0);
    const float3 second_direction =
        plane == 1
            ? float3(0.0, 1.0, 0.0)
            : float3(0.0, 0.0, 1.0);
    const float first_axis =
        axis_alpha(
            first_direction,
            input.position.xy) *
        0.78;
    const float second_axis =
        axis_alpha(
            second_direction,
            input.position.xy) *
        0.72;
    float alpha = first_axis;
    float3 color =
        plane == 2
            ? float3(0.30, 0.80, 0.38)
            : float3(0.95, 0.30, 0.25);
    if (second_axis > first_axis) {
        alpha = second_axis;
        color =
            plane == 1
                ? float3(0.30, 0.80, 0.38)
                : float3(0.30, 0.52, 0.95);
    }
    alpha *= saturate(
        abs(
            dot(
                normalize(ray_direction),
                plane_normal)) *
        8.0);
    const float4 clip_position =
        view_projection(
            float4(world_position, 1.0));
    const float depth =
        clip_position.z / clip_position.w;
    if (
        clip_position.w <= 0.0 ||
        depth <= 0.0 ||
        depth >= 1.0 ||
        alpha <= 1.0 / 255.0)
        discard;
    AxesFragment output;
    output.color = float4(color, alpha);
    output.depth = depth;
    return output;
}
