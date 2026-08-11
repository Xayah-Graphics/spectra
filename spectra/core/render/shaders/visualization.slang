module visualization;

import plugin_dataset;

struct VisualizationPushData {
    uint2 primary;
    uint2 secondary;
    DescriptorHandle<Texture2D<float>> depth;
    uint color_space;
    uint reserved;
    uint4 metadata;
    uint4 dimensions;
    uint4 detail;
    float4 parameters;
    float4 color;
    float4 screen_rect;
    float4 transform_row_0;
    float4 transform_row_1;
    float4 transform_row_2;
    float4 transform_row_3;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
}

struct VisualizationVertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texture_coordinates : TEXCOORD;
    nointerpolation uint image_layer : LAYER;
    nointerpolation uint sample_image : SAMPLE_IMAGE;
    nointerpolation float4 depth_data : DEPTH_DATA;
}

[vk::push_constant]
uniform VisualizationPushData push_data;

float4 transform_position(float3 position) {
    const float4 homogeneous = float4(position, 1.0);
    return float4(
        dot(push_data.transform_row_0, homogeneous),
        dot(push_data.transform_row_1, homogeneous),
        dot(push_data.transform_row_2, homogeneous),
        dot(push_data.transform_row_3, homogeneous));
}

float4 project_position(float4 position) {
    return float4(
        dot(push_data.view_projection_row_0, position),
        dot(push_data.view_projection_row_1, position),
        dot(push_data.view_projection_row_2, position),
        dot(push_data.view_projection_row_3, position));
}

float4 load_image_pixel(ByteAddressBuffer pixels, uint pixel_index) {
    float4 value;
    const uint format = push_data.reserved & 0xffff;
    if (format == 0) {
        const uint packed = asuint(pixels.Load<float>(pixel_index * 4));
        value = float4(float(packed & 255), float((packed >> 8) & 255), float((packed >> 16) & 255), float((packed >> 24) & 255)) / 255.0;
    } else if (format == 1) {
        const uint2 packed = asuint(pixels.Load<float2>(pixel_index * 8));
        value = float4(f16tof32(packed.x & 65535), f16tof32(packed.x >> 16), f16tof32(packed.y & 65535), f16tof32(packed.y >> 16));
    } else
        value = pixels.Load<float4>(pixel_index * 16);
    if ((push_data.reserved >> 16) == 1)
        value.rgb = select(value.rgb <= 0.04045, value.rgb / 12.92, pow((value.rgb + 0.055) / 1.055, 2.4));
    if (push_data.color_space == 1)
        value.rgb = float3(
            dot(float3(1.660491, -0.587641, -0.072850), value.rgb),
            dot(float3(-0.124550, 1.132900, -0.008349), value.rgb),
            dot(float3(-0.018151, -0.100579, 1.118730), value.rgb));
    else if (push_data.color_space == 2)
        value.rgb = float3(
            dot(float3(2.521686, -1.134130, -0.387556), value.rgb),
            dot(float3(-0.276479, 1.372719, -0.096240), value.rgb),
            dot(float3(-0.015378, -0.152975, 1.168353), value.rgb));
    if (push_data.detail.x == 1)
        value.rgb = float3(
            dot(float3(0.627404, 0.329283, 0.043313), value.rgb),
            dot(float3(0.069097, 0.919540, 0.011362), value.rgb),
            dot(float3(0.016391, 0.088013, 0.895595), value.rgb));
    else if (push_data.detail.x == 2)
        value.rgb = float3(
            dot(float3(0.439586, 0.383929, 0.176533), value.rgb),
            dot(float3(0.089540, 0.814750, 0.095684), value.rgb),
            dot(float3(0.017387, 0.108739, 0.873821), value.rgb));
    return value;
}

float4 element_visualization_color(float4 element_color, float scalar) {
    if (push_data.detail.z == 1) return push_data.color;
    if (push_data.detail.z == 2) return float4(visualization_color_map((scalar - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.w), element_color.a * push_data.color.a);
    return element_color * push_data.color;
}

float4 field_color(float3 value) {
    if ((uint)push_data.screen_rect.z != 0) return float4(max(value * push_data.parameters.y, 0.0), push_data.color.w);
    const float scalar = value.x * push_data.parameters.y;
    return float4(visualization_color_map((scalar - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.w), push_data.color.w);
}

float3 visualization_color_map(float value, uint color_map) {
    value = saturate(value);
    if (color_map == 1) {
        const float4 k_red = float4(0.13572138, 4.61539260, -42.66032258, 132.13108234);
        const float2 k_red_tail = float2(-152.94239396, 59.28637943);
        const float4 k_green = float4(0.09140261, 2.19418839, 4.84296658, -14.18503333);
        const float2 k_green_tail = float2(4.27729857, 2.82956604);
        const float4 k_blue = float4(0.10667330, 12.64194608, -60.58204836, 110.36276771);
        const float2 k_blue_tail = float2(-89.90310912, 27.34824973);
        const float4 powers = float4(1.0, value, value * value, value * value * value);
        const float2 tail = float2(powers.w * value, powers.w * value * value);
        return saturate(float3(dot(k_red, powers) + dot(k_red_tail, tail), dot(k_green, powers) + dot(k_green_tail, tail), dot(k_blue, powers) + dot(k_blue_tail, tail)));
    }
    if (color_map == 2) return lerp(float3(0.23, 0.30, 0.75), float3(0.71, 0.02, 0.15), value);
    if (color_map == 3) return value.xxx;
    const float3 c0 = float3(0.267, 0.005, 0.329);
    const float3 c1 = float3(0.128, 0.567, 0.551);
    const float3 c2 = float3(0.993, 0.906, 0.144);
    return value < 0.5 ? lerp(c0, c1, value * 2.0) : lerp(c1, c2, value * 2.0 - 1.0);
}

float3 load_field(ByteAddressBuffer field, uint3 coordinate) {
    const uint index = (coordinate.z * push_data.dimensions.w + coordinate.y) * push_data.dimensions.z + coordinate.x;
    if ((uint)push_data.screen_rect.z == 0) return float3(field.Load<float>(index * 4), 0.0, 0.0);
    return field.Load<float3>(index * 12);
}

float4 wide_line_position(float4 first, float4 second, uint triangle_vertex, float width) {
    const float first_planes[7] = {first.x + first.w, first.w - first.x, first.y + first.w, first.w - first.y, first.z, first.w - first.z, first.w - 0.000001};
    const float second_planes[7] = {second.x + second.w, second.w - second.x, second.y + second.w, second.w - second.y, second.z, second.w - second.z, second.w - 0.000001};
    float enter = 0.0;
    float exit = 1.0;
    for (uint plane = 0; plane != 7; ++plane) {
        if (first_planes[plane] < 0.0 && second_planes[plane] < 0.0) return float4(0.0);
        const float intersection = first_planes[plane] / (first_planes[plane] - second_planes[plane]);
        if (first_planes[plane] < 0.0) enter = max(enter, intersection);
        if (second_planes[plane] < 0.0) exit = min(exit, intersection);
    }
    if (enter > exit) return float4(0.0);
    const float4 difference = second - first;
    second = first + difference * exit;
    first += difference * enter;
    const uint corners[6] = {0, 1, 2, 0, 2, 3};
    const uint corner = corners[triangle_vertex];
    const bool use_second = corner == 1 || corner == 2;
    const float side = corner == 0 || corner == 1 ? -1.0 : 1.0;
    const float2 first_ndc = first.xy / first.w;
    const float2 second_ndc = second.xy / second.w;
    const float2 direction_pixels = (second_ndc - first_ndc) * float2(push_data.dimensions.xy);
    const float2 normal = dot(direction_pixels, direction_pixels) == 0.0 ? float2(0.0, 1.0) : normalize(float2(-direction_pixels.y, direction_pixels.x));
    float4 position = use_second ? second : first;
    position.xy += normal * width * side / float2(push_data.dimensions.xy) * position.w;
    return position;
}

VisualizationVertexOutput make_line(float3 first, float3 second, float width, float4 color, uint triangle_vertex) {
    VisualizationVertexOutput output;
    output.position = wide_line_position(project_position(transform_position(first)), project_position(transform_position(second)), triangle_vertex, width);
    output.color = color;
    output.texture_coordinates = 0.0;
    output.image_layer = 0;
    output.sample_image = 0;
    output.depth_data = 0.0;
    return output;
}

float3 bezier(float3 a, float3 b, float3 c, float3 d, float t) {
    const float s = 1.0 - t;
    return s * s * s * a + 3.0 * s * s * t * b + 3.0 * s * t * t * c + t * t * t * d;
}

float3 camera_corner(float2 pixel, float4 intrinsics, SpectraPluginCameraDistortion distortion, float depth) {
    const float2 distorted = (pixel - intrinsics.zw) / intrinsics.xy;
    float2 undistorted = distorted;
    for (uint iteration = 0; iteration != (uint)push_data.screen_rect.y; ++iteration) {
        const float radius_squared = dot(undistorted, undistorted);
        const float radial = 1.0 + distortion.radial_1 * radius_squared + distortion.radial_2 * radius_squared * radius_squared;
        const float2 tangential = float2(
            2.0 * distortion.tangential_1 * undistorted.x * undistorted.y + distortion.tangential_2 * (radius_squared + 2.0 * undistorted.x * undistorted.x),
            distortion.tangential_1 * (radius_squared + 2.0 * undistorted.y * undistorted.y) + 2.0 * distortion.tangential_2 * undistorted.x * undistorted.y);
        const float2 residual = undistorted * radial + tangential - distorted;
        const float radial_derivative = distortion.radial_1 + 2.0 * distortion.radial_2 * radius_squared;
        const float radial_x = 2.0 * undistorted.x * radial_derivative;
        const float radial_y = 2.0 * undistorted.y * radial_derivative;
        const float jacobian_00 = radial + undistorted.x * radial_x + 2.0 * distortion.tangential_1 * undistorted.y + 6.0 * distortion.tangential_2 * undistorted.x;
        const float jacobian_01 = undistorted.x * radial_y + 2.0 * distortion.tangential_1 * undistorted.x + 2.0 * distortion.tangential_2 * undistorted.y;
        const float jacobian_10 = undistorted.y * radial_x + 2.0 * distortion.tangential_1 * undistorted.x + 2.0 * distortion.tangential_2 * undistorted.y;
        const float jacobian_11 = radial + undistorted.y * radial_y + 6.0 * distortion.tangential_1 * undistorted.y + 2.0 * distortion.tangential_2 * undistorted.x;
        const float determinant = jacobian_00 * jacobian_11 - jacobian_01 * jacobian_10;
        const float2 delta = float2(jacobian_11 * residual.x - jacobian_01 * residual.y, -jacobian_10 * residual.x + jacobian_00 * residual.y) / determinant;
        undistorted -= delta;
        if (dot(delta, delta) <= push_data.screen_rect.x * push_data.screen_rect.x) break;
    }
    return float3(undistorted * depth, depth);
}

[shader("vertex")]
VisualizationVertexOutput visualization_vertex(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    const uint kind = push_data.metadata.x;
    if (kind == 0) {
        StructuredBuffer<SpectraPluginPoint> points = DescriptorHandle<StructuredBuffer<SpectraPluginPoint>>(push_data.primary);
        const SpectraPluginPoint point = points[instance_id];
        const float3 center = plugin_float3(point.position);
        float radius = point.radius * push_data.parameters.x;
        const float transform_scale = max(length(push_data.transform_row_0.xyz), max(length(push_data.transform_row_1.xyz), length(push_data.transform_row_2.xyz)));
        if (push_data.detail.x == 1 || push_data.detail.x == 2) radius *= transform_scale;
        const float4 element_color = plugin_float4(point.color);
        const float scalar = point.scalar;
        const float4 color = element_visualization_color(element_color, scalar);
        const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
        float2 corner = corners[vertex_id % 6];
        if (push_data.detail.x == 3) corner *= vertex_id < 6 ? float2(1.0, 0.14) : float2(0.14, 1.0);
        float4 position = project_position(transform_position(center));
        if (push_data.detail.x == 0 || push_data.detail.x == 3)
            position.xy += corner * radius * 2.0 / float2(push_data.dimensions.xy) * position.w;
        else {
            const float horizontal_scale = length(float3(push_data.view_projection_row_0.x, push_data.view_projection_row_0.y, push_data.view_projection_row_0.z));
            const float vertical_scale = length(float3(push_data.view_projection_row_1.x, push_data.view_projection_row_1.y, push_data.view_projection_row_1.z));
            position.xy += corner * radius * float2(horizontal_scale, vertical_scale);
        }
        VisualizationVertexOutput output = {position, color, corner * 0.5 + 0.5, push_data.detail.x | (push_data.detail.y << 8), 2, float4(project_position(transform_position(center)).zw, radius, 0.0)};
        return output;
    }
    if (kind == 1) {
        StructuredBuffer<SpectraPluginSegment> segments = DescriptorHandle<StructuredBuffer<SpectraPluginSegment>>(push_data.primary);
        const SpectraPluginSegment segment = segments[instance_id];
        return make_line(plugin_float3(segment.first_position), plugin_float3(segment.second_position), segment.width * push_data.parameters.x, element_visualization_color(plugin_float4(segment.color), segment.scalar), vertex_id);
    }
    if (kind == 2) {
        StructuredBuffer<SpectraPluginCurve> curves = DescriptorHandle<StructuredBuffer<SpectraPluginCurve>>(push_data.primary);
        const SpectraPluginCurve curve = curves[instance_id];
        const uint segment = vertex_id / 6;
        const float first_t = float(segment) / 16.0;
        const float second_t = float(segment + 1) / 16.0;
        const float3 control_0 = plugin_float3(curve.control_0);
        const float3 control_1 = plugin_float3(curve.control_1);
        const float3 control_2 = plugin_float3(curve.control_2);
        const float3 control_3 = plugin_float3(curve.control_3);
        return make_line(bezier(control_0, control_1, control_2, control_3, first_t), bezier(control_0, control_1, control_2, control_3, second_t), curve.width * push_data.parameters.x, element_visualization_color(plugin_float4(curve.color), curve.scalar), vertex_id % 6);
    }
    if (kind == 3 || kind == 5) {
        float3 origin;
        float3 vector;
        float width;
        float4 color;
        if (kind == 3) {
            StructuredBuffer<SpectraPluginVector> vectors = DescriptorHandle<StructuredBuffer<SpectraPluginVector>>(push_data.primary);
            const SpectraPluginVector element = vectors[instance_id];
            origin = plugin_float3(element.origin);
            vector = plugin_float3(element.vector) * push_data.parameters.y;
            width = element.width * push_data.parameters.x;
            color = element_visualization_color(plugin_float4(element.color), element.scalar);
        } else {
            ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
            const uint sampling = push_data.detail.y;
            const uint z = instance_id / (sampling * sampling);
            const uint y = (instance_id / sampling) % sampling;
            const uint x = instance_id % sampling;
            const float3 normalized = (float3(x, y, z) + 0.5) / float(sampling);
            const uint3 coordinate = min(uint3(normalized * float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x)), uint3(push_data.dimensions.z - 1, push_data.dimensions.w - 1, push_data.detail.x - 1));
            origin = normalized;
            vector = load_field(primary, coordinate) * push_data.parameters.y;
            width = push_data.parameters.x;
            color = push_data.color;
        }
        const float length_value = length(vector);
        if (kind == 5) color = element_visualization_color(float4(1.0), length_value);
        const float3 direction = length_value == 0.0 ? float3(1.0, 0.0, 0.0) : vector / length_value;
        const float3 reference = abs(direction.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        const float3 side = normalize(cross(direction, reference)) * length_value * 0.18;
        const uint part = vertex_id / 6;
        const float3 first = part == 0 ? origin : origin + vector;
        const float3 second = part == 0 ? origin + vector : origin + vector - direction * length_value * 0.28 + (part == 1 ? side : -side);
        return make_line(first, second, width, color, vertex_id % 6);
    }
    if (kind == 4) {
        ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        float3 position;
        if ((uint)push_data.screen_rect.x == 0) position = float3(push_data.screen_rect.y, uv.x, uv.y);
        else if ((uint)push_data.screen_rect.x == 1) position = float3(uv.x, push_data.screen_rect.y, uv.y);
        else position = float3(uv, push_data.screen_rect.y);
        const uint3 coordinate = min(uint3(position * float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x)), uint3(push_data.dimensions.z - 1, push_data.dimensions.w - 1, push_data.detail.x - 1));
        VisualizationVertexOutput output = {project_position(transform_position(position)), field_color(load_field(primary, coordinate)), uv, 0, 0, 0.0};
        return output;
    }
    if (kind == 6) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = push_data.screen_rect.xy + uv * push_data.screen_rect.zw;
        VisualizationVertexOutput output = {float4(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0, 0.0, 1.0), push_data.color, uv, 0, 1, 0.0};
        return output;
    }
    if (kind == 7) {
        StructuredBuffer<SpectraPluginCameraObservation> cameras = DescriptorHandle<StructuredBuffer<SpectraPluginCameraObservation>>(push_data.primary);
        const SpectraPluginCameraObservation camera = cameras[instance_id];
        const float4 row_0 = float4(camera.world_from_camera.matrix[0], camera.world_from_camera.matrix[1], camera.world_from_camera.matrix[2], camera.world_from_camera.matrix[3]);
        const float4 row_1 = float4(camera.world_from_camera.matrix[4], camera.world_from_camera.matrix[5], camera.world_from_camera.matrix[6], camera.world_from_camera.matrix[7]);
        const float4 row_2 = float4(camera.world_from_camera.matrix[8], camera.world_from_camera.matrix[9], camera.world_from_camera.matrix[10], camera.world_from_camera.matrix[11]);
        const float4 row_3 = float4(camera.world_from_camera.matrix[12], camera.world_from_camera.matrix[13], camera.world_from_camera.matrix[14], camera.world_from_camera.matrix[15]);
        const float4 intrinsics = plugin_float4(camera.intrinsics);
        const float depth = push_data.parameters.y;
        const float3 camera_corners[5] = {
            float3(0.0),
            camera_corner(float2(0.0, 0.0), intrinsics, camera.distortion, depth),
            camera_corner(float2(push_data.dimensions.z, 0.0), intrinsics, camera.distortion, depth),
            camera_corner(float2(push_data.dimensions.z, push_data.dimensions.w), intrinsics, camera.distortion, depth),
            camera_corner(float2(0.0, push_data.dimensions.w), intrinsics, camera.distortion, depth)};
        const uint edges[16] = {0,1, 0,2, 0,3, 0,4, 1,2, 2,3, 3,4, 4,1};
        if (vertex_id < 48) {
            const uint edge = vertex_id / 6;
            const float3 local_first = camera_corners[edges[edge * 2]];
            const float3 local_second = camera_corners[edges[edge * 2 + 1]];
            const float4 first = float4(dot(row_0, float4(local_first, 1.0)), dot(row_1, float4(local_first, 1.0)), dot(row_2, float4(local_first, 1.0)), dot(row_3, float4(local_first, 1.0)));
            const float4 second = float4(dot(row_0, float4(local_second, 1.0)), dot(row_1, float4(local_second, 1.0)), dot(row_2, float4(local_second, 1.0)), dot(row_3, float4(local_second, 1.0)));
            VisualizationVertexOutput output = {wide_line_position(project_position(transform_position(first.xyz)), project_position(transform_position(second.xyz)), vertex_id % 6, push_data.parameters.x), push_data.color, 0.0, 0, 0, 0.0};
            return output;
        }
        const uint image_vertex = vertex_id - 48;
        const uint image_corner[6] = {1,2,3,1,3,4};
        const float2 image_uv[4] = {float2(0,0), float2(1,0), float2(1,1), float2(0,1)};
        const uint corner = image_corner[image_vertex];
        const float3 local = camera_corners[corner];
        const float4 world = float4(dot(row_0, float4(local, 1.0)), dot(row_1, float4(local, 1.0)), dot(row_2, float4(local, 1.0)), dot(row_3, float4(local, 1.0)));
        VisualizationVertexOutput output = {project_position(transform_position(world.xyz)), push_data.color, image_uv[corner - 1], camera.image_layer, 1, 0.0};
        return output;
    }
    if (kind == 8) {
        ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
        const uint offset = instance_id * 64;
        const float4 rows[4] = {primary.Load<float4>(offset), primary.Load<float4>(offset + 16), primary.Load<float4>(offset + 32), primary.Load<float4>(offset + 48)};
        const uint axis = vertex_id / 6;
        const float3 local_end = axis == 0 ? float3(push_data.parameters.y,0,0) : axis == 1 ? float3(0,push_data.parameters.y,0) : float3(0,0,push_data.parameters.y);
        const float3 first = float3(rows[0].w, rows[1].w, rows[2].w);
        const float3 second = float3(dot(rows[0], float4(local_end, 1.0)), dot(rows[1], float4(local_end, 1.0)), dot(rows[2], float4(local_end, 1.0)));
        const float4 color = axis == 0 ? float4(1,0.15,0.12,1) : axis == 1 ? float4(0.2,0.9,0.25,1) : float4(0.18,0.48,1,1);
        return make_line(first, second, push_data.parameters.x, color * push_data.color, vertex_id % 6);
    }
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
    ByteAddressBuffer indices = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
    const uint position_index = push_data.metadata.z == 0 ? vertex_id : asuint(indices.Load<float>(vertex_id * 4));
    float4 surface_color = push_data.color;
    if (push_data.detail.z == 2) {
        StructuredBuffer<float> scalars = DescriptorHandle<StructuredBuffer<float>>(uint2(push_data.color_space, push_data.reserved));
        surface_color = element_visualization_color(float4(1.0), scalars[position_index]);
    }
    VisualizationVertexOutput surface = {project_position(transform_position(primary.Load<float3>(position_index * 12))), surface_color, 0.0, 0, 0, 0.0};
    return surface;
}

[shader("fragment")]
float4 visualization_fragment(VisualizationVertexOutput input) : SV_Target0 {
    float4 color = input.color;
    float visualization_depth = input.position.z;
    if (input.sample_image == 1) {
        ByteAddressBuffer pixels = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
        const uint2 coordinate = min(uint2(input.texture_coordinates * float2(push_data.dimensions.z, push_data.dimensions.w)), uint2(push_data.dimensions.z - 1, push_data.dimensions.w - 1));
        const uint layer_offset = input.image_layer * push_data.dimensions.z * push_data.dimensions.w;
        color *= load_image_pixel(pixels, layer_offset + coordinate.y * push_data.dimensions.z + coordinate.x);
    }
    if (input.sample_image == 2) {
        const uint glyph = input.image_layer & 255;
        const uint shading = input.image_layer >> 8;
        const float2 point = input.texture_coordinates * 2.0 - 1.0;
        const float radius_squared = dot(point, point);
        if (glyph != 3 && radius_squared > 1.0) discard;
        if (glyph == 2 && shading != 0) color.rgb *= 0.24 + 0.76 * sqrt(max(1.0 - radius_squared, 0.0));
        if (glyph == 2) {
            const float surface_offset = input.depth_data.z * sqrt(max(1.0 - radius_squared, 0.0));
            const float near_plane = push_data.screen_rect.x;
            const float far_plane = push_data.screen_rect.y;
            const bool perspective = push_data.screen_rect.z != 0.0;
            const float projection_z = perspective ? far_plane / (near_plane - far_plane) : 1.0 / (near_plane - far_plane);
            const float clip_z = input.depth_data.x + projection_z * surface_offset;
            const float clip_w = input.depth_data.y - (perspective ? surface_offset : 0.0);
            visualization_depth = clip_z / clip_w;
        }
    }
    if (push_data.metadata.w != 2) {
        Texture2D<float> depth = push_data.depth;
        const uint2 pixel = uint2(input.position.xy);
        const bool hidden = visualization_depth > depth.Load(int3(pixel, 0)) + 0.00001;
        if (hidden && push_data.metadata.w == 0) discard;
        if (hidden) color.a *= 0.22;
    }
    return color;
}
