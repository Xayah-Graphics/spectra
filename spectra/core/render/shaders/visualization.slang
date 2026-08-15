module visualization;

import sdk_output;

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
    float4 value = pixels.Load<float4>(pixel_index * 16);
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

float srgb_eotf(float value) {
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

float4 load_reference_pixel(ByteAddressBuffer pixels, uint pixel_index) {
    const uint packed = pixels.Load<uint>(pixel_index * 4);
    const float4 encoded = float4(packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u, packed >> 24u) * (1.0 / 255.0);
    const float3 linear = float3(srgb_eotf(encoded.r), srgb_eotf(encoded.g), srgb_eotf(encoded.b));
    return float4(linear * encoded.a, 1.0);
}

float4 element_visualization_color(float4 element_color, float scalar) {
    if (push_data.detail.z == 1) return push_data.color;
    if (push_data.detail.z == 2) return float4(visualization_color_map((scalar - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.w), element_color.a * push_data.color.a);
    return element_color * push_data.color;
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

uint volume_index(uint3 coordinate, uint3 resolution) {
    return (coordinate.z * resolution.y + coordinate.y) * resolution.x + coordinate.x;
}

float sample_float_field(ByteAddressBuffer field, float3 position, uint3 resolution, float3 offset, bool vertex_sampled) {
    const float3 coordinate = vertex_sampled
        ? clamp(position * float3(resolution - 1), 0.0, float3(resolution - 1))
        : clamp(position * float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x) - offset, 0.0, float3(resolution - 1));
    const uint3 lower = uint3(floor(coordinate));
    const uint3 upper = min(lower + 1, resolution - 1);
    const float3 weight = coordinate - float3(lower);
    const float c000 = field.Load<float>(volume_index(lower, resolution) * 4);
    const float c100 = field.Load<float>(volume_index(uint3(upper.x, lower.y, lower.z), resolution) * 4);
    const float c010 = field.Load<float>(volume_index(uint3(lower.x, upper.y, lower.z), resolution) * 4);
    const float c110 = field.Load<float>(volume_index(uint3(upper.x, upper.y, lower.z), resolution) * 4);
    const float c001 = field.Load<float>(volume_index(uint3(lower.x, lower.y, upper.z), resolution) * 4);
    const float c101 = field.Load<float>(volume_index(uint3(upper.x, lower.y, upper.z), resolution) * 4);
    const float c011 = field.Load<float>(volume_index(uint3(lower.x, upper.y, upper.z), resolution) * 4);
    const float c111 = field.Load<float>(volume_index(upper, resolution) * 4);
    return lerp(lerp(lerp(c000, c100, weight.x), lerp(c010, c110, weight.x), weight.y), lerp(lerp(c001, c101, weight.x), lerp(c011, c111, weight.x), weight.y), weight.z);
}

float3 sample_float3_field(ByteAddressBuffer field, float3 position, bool vertex_sampled) {
    const uint3 resolution = uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
    const float3 coordinate = vertex_sampled
        ? clamp(position * float3(resolution - 1), 0.0, float3(resolution - 1))
        : clamp(position * float3(resolution) - 0.5, 0.0, float3(resolution - 1));
    const uint3 lower = uint3(floor(coordinate));
    const uint3 upper = min(lower + 1, resolution - 1);
    const float3 weight = coordinate - float3(lower);
    const float3 c000 = field.Load<float3>(volume_index(lower, resolution) * 12);
    const float3 c100 = field.Load<float3>(volume_index(uint3(upper.x, lower.y, lower.z), resolution) * 12);
    const float3 c010 = field.Load<float3>(volume_index(uint3(lower.x, upper.y, lower.z), resolution) * 12);
    const float3 c110 = field.Load<float3>(volume_index(uint3(upper.x, upper.y, lower.z), resolution) * 12);
    const float3 c001 = field.Load<float3>(volume_index(uint3(lower.x, lower.y, upper.z), resolution) * 12);
    const float3 c101 = field.Load<float3>(volume_index(uint3(upper.x, lower.y, upper.z), resolution) * 12);
    const float3 c011 = field.Load<float3>(volume_index(uint3(lower.x, upper.y, upper.z), resolution) * 12);
    const float3 c111 = field.Load<float3>(volume_index(upper, resolution) * 12);
    return lerp(lerp(lerp(c000, c100, weight.x), lerp(c010, c110, weight.x), weight.y), lerp(lerp(c001, c101, weight.x), lerp(c011, c111, weight.x), weight.y), weight.z);
}

float sample_volume_scalar(float3 position) {
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
    return sample_float_field(primary, position, uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x), 0.5, (push_data.metadata.y >> 8) != 0);
}

float3 sample_volume_vector(float3 position) {
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
    if ((push_data.metadata.y & 255) == 1) return sample_float3_field(primary, position, (push_data.metadata.y >> 8) != 0);
    ByteAddressBuffer secondary = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
    ByteAddressBuffer tertiary = DescriptorHandle<ByteAddressBuffer>(uint2(push_data.color_space, push_data.reserved));
    const uint3 base_resolution = uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
    return float3(
        sample_float_field(primary, position, base_resolution + uint3(1, 0, 0), float3(0.0, 0.5, 0.5), false),
        sample_float_field(secondary, position, base_resolution + uint3(0, 1, 0), float3(0.5, 0.0, 0.5), false),
        sample_float_field(tertiary, position, base_resolution + uint3(0, 0, 1), float3(0.5, 0.5, 0.0), false));
}

float3 vector_to_grid(float3 vector) {
    return float3(dot(push_data.transform_row_0.xyz, vector), dot(push_data.transform_row_1.xyz, vector), dot(push_data.transform_row_2.xyz, vector));
}

float volume_mapping(float3 position) {
    if ((push_data.metadata.y & 255) == 0) return sample_volume_scalar(position);
    const float3 value = sample_volume_vector(position);
    if (push_data.metadata.z == 0 || push_data.metadata.z == 1) return length(value);
    if (push_data.metadata.z == 2) return value.x;
    if (push_data.metadata.z == 3) return value.y;
    if (push_data.metadata.z == 4) return value.z;
    const float3 delta = 1.0 / float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
    const float3 dx = (sample_volume_vector(position + float3(delta.x, 0.0, 0.0)) - sample_volume_vector(position - float3(delta.x, 0.0, 0.0))) / (2.0 * delta.x);
    const float3 dy = (sample_volume_vector(position + float3(0.0, delta.y, 0.0)) - sample_volume_vector(position - float3(0.0, delta.y, 0.0))) / (2.0 * delta.y);
    const float3 dz = (sample_volume_vector(position + float3(0.0, 0.0, delta.z)) - sample_volume_vector(position - float3(0.0, 0.0, delta.z))) / (2.0 * delta.z);
    if (push_data.metadata.z == 5) return dx.x + dy.y + dz.z;
    if (push_data.metadata.z == 6) return length(float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x));
    const float3x3 gradient = float3x3(dx, dy, dz);
    const float3x3 strain = 0.5 * (gradient + transpose(gradient));
    const float3x3 rotation = 0.5 * (gradient - transpose(gradient));
    return 0.5 * (dot(rotation[0], rotation[0]) + dot(rotation[1], rotation[1]) + dot(rotation[2], rotation[2]) - dot(strain[0], strain[0]) - dot(strain[1], strain[1]) - dot(strain[2], strain[2]));
}

float4 volume_color(float value) {
    const float normalized = (value - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z);
    return float4(visualization_color_map(normalized, (uint)push_data.screen_rect.w) * push_data.color.rgb, push_data.color.w);
}

float3 volume_slice_position(float2 uv) {
    if (push_data.detail.y == 0) return float3(push_data.screen_rect.x, uv.x, uv.y);
    if (push_data.detail.y == 1) return float3(uv.x, push_data.screen_rect.x, uv.y);
    return float3(uv, push_data.screen_rect.x);
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

VisualizationVertexOutput make_volume_line(float3 first, float3 second, float width, float4 color, uint triangle_vertex) {
    VisualizationVertexOutput output;
    output.position = wide_line_position(project_position(float4(first, 1.0)), project_position(float4(second, 1.0)), triangle_vertex, width);
    output.color = color;
    output.texture_coordinates = 0.0;
    output.image_layer = 0;
    output.sample_image = 0;
    output.depth_data = 0.0;
    return output;
}

float3 integrate_volume_vector(float3 position, float direction) {
    const float step = push_data.parameters.y * direction;
    const float3 first = vector_to_grid(sample_volume_vector(position));
    const float3 middle = vector_to_grid(sample_volume_vector(clamp(position + first * step * 0.5, 0.0, 1.0)));
    return clamp(position + middle * step, 0.0, 1.0);
}

[shader("vertex")]
VisualizationVertexOutput visualization_vertex(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    const uint kind = push_data.metadata.x;
    if (kind == 0) {
        StructuredBuffer<SdkPoint> points = DescriptorHandle<StructuredBuffer<SdkPoint>>(push_data.primary);
        const SdkPoint point = points[instance_id];
        const float3 center = sdk_float3(point.position);
        float radius = point.radius * push_data.parameters.x;
        const float transform_scale = max(length(push_data.transform_row_0.xyz), max(length(push_data.transform_row_1.xyz), length(push_data.transform_row_2.xyz)));
        if (push_data.detail.x == 1 || push_data.detail.x == 2) radius *= transform_scale;
        const float4 element_color = sdk_float4(point.color);
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
        StructuredBuffer<SdkLine> lines = DescriptorHandle<StructuredBuffer<SdkLine>>(push_data.primary);
        const SdkLine line = lines[instance_id];
        return make_line(sdk_float3(line.first_position), sdk_float3(line.second_position), line.width * push_data.parameters.x, element_visualization_color(sdk_float4(line.color), line.scalar), vertex_id);
    }
    if (kind == 2) {
        StructuredBuffer<SdkVector> vectors = DescriptorHandle<StructuredBuffer<SdkVector>>(push_data.primary);
        const SdkVector element = vectors[instance_id];
        const float3 origin = sdk_float3(element.origin);
        const float3 vector = sdk_float3(element.vector) * push_data.parameters.y;
        const float width = element.width * push_data.parameters.x;
        const float4 color = element_visualization_color(sdk_float4(element.color), element.scalar);
        const float length_value = length(vector);
        const float3 direction = length_value == 0.0 ? float3(1.0, 0.0, 0.0) : vector / length_value;
        const float3 reference = abs(direction.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        const float3 side = normalize(cross(direction, reference)) * length_value * 0.18;
        const uint part = vertex_id / 6;
        const float3 first = part == 0 ? origin : origin + vector;
        const float3 second = part == 0 ? origin + vector : origin + vector - direction * length_value * 0.28 + (part == 1 ? side : -side);
        return make_line(first, second, width, color, vertex_id % 6);
    }
    if (kind == 3) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = push_data.screen_rect.xy + uv * push_data.screen_rect.zw;
        VisualizationVertexOutput output = {float4(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0, 0.0, 1.0), push_data.color, uv, 0, 1, 0.0};
        return output;
    }
    if (kind == 15) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = push_data.screen_rect.xy + uv * push_data.screen_rect.zw;
        VisualizationVertexOutput output = {float4(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0, 0.0, 1.0), push_data.color, uv, push_data.detail.x, 3, 0.0};
        return output;
    }
    if (kind == 16) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = float2(lerp(push_data.screen_rect.x, push_data.screen_rect.z, uv.x), lerp(push_data.screen_rect.w, push_data.screen_rect.y, uv.y));
        const float distance = push_data.parameters.y;
        const float3 local = float3(screen * push_data.parameters.x * distance, -distance);
        VisualizationVertexOutput output = {project_position(transform_position(local)), push_data.color, uv, push_data.detail.x, 3, 0.0};
        return output;
    }
    if (kind == 8 || kind == 14) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        VisualizationVertexOutput output = {project_position(float4(volume_slice_position(uv), 1.0)), push_data.color, uv, 0, 0, 0.0};
        return output;
    }
    if (kind >= 9 && kind <= 11) {
        const float2 positions[3] = {float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0)};
        const float2 ndc = positions[vertex_id];
        VisualizationVertexOutput output = {float4(ndc, 0.0, 1.0), push_data.color, ndc * 0.5 + 0.5, 0, 0, 0.0};
        return output;
    }
    if (kind == 12) {
        const uint sampling = push_data.detail.z;
        const uint z = instance_id / (sampling * sampling);
        const uint y = (instance_id / sampling) % sampling;
        const uint x = instance_id % sampling;
        const float3 origin = (float3(x, y, z) + 0.5) / float(sampling);
        const float3 vector = vector_to_grid(sample_volume_vector(origin)) * push_data.parameters.y;
        const float length_value = length(vector);
        const float3 direction = length_value == 0.0 ? float3(1.0, 0.0, 0.0) : vector / length_value;
        const float3 reference = abs(direction.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        const float3 side = normalize(cross(direction, reference)) * length_value * 0.18;
        const uint part = vertex_id / 6;
        const float3 first = part == 0 ? origin : origin + vector;
        const float3 second = part == 0 ? origin + vector : origin + vector - direction * length_value * 0.28 + (part == 1 ? side : -side);
        return make_volume_line(first, second, push_data.parameters.x, volume_color(volume_mapping(origin)), vertex_id % 6);
    }
    if (kind == 13) {
        const uint segment = instance_id % push_data.detail.w;
        const uint seed_index = instance_id / push_data.detail.w;
        const uint sampling = push_data.detail.z;
        const uint z = seed_index / (sampling * sampling);
        const uint y = (seed_index / sampling) % sampling;
        const uint x = seed_index % sampling;
        const float3 seed = (float3(x, y, z) + 0.5) / float(sampling);
        const uint half_steps = push_data.detail.w / 2;
        const float direction = segment < half_steps ? -1.0 : 1.0;
        const uint offset = segment < half_steps ? half_steps - segment - 1 : segment - half_steps;
        float3 first = seed;
        for (uint step = 0; step != offset; ++step) first = integrate_volume_vector(first, direction);
        const float3 second = integrate_volume_vector(first, direction);
        return make_volume_line(first, second, push_data.parameters.x, volume_color(volume_mapping(first)), vertex_id);
    }
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
    ByteAddressBuffer indices = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
    const uint position_index = push_data.metadata.z == 0 ? vertex_id : asuint(indices.Load<float>(vertex_id * 4));
    float4 surface_color = push_data.color;
    if (push_data.detail.z == 0) {
        StructuredBuffer<float4> colors = DescriptorHandle<StructuredBuffer<float4>>(uint2(push_data.color_space, push_data.reserved));
        surface_color = element_visualization_color(colors[position_index], 0.0);
    } else if (push_data.detail.z == 2) {
        StructuredBuffer<float> scalars = DescriptorHandle<StructuredBuffer<float>>(uint2(push_data.color_space, push_data.reserved));
        surface_color = element_visualization_color(float4(1.0), scalars[position_index]);
    }
    VisualizationVertexOutput surface = {project_position(transform_position(primary.Load<float3>(position_index * 12))), surface_color, 0.0, 0, 0, 0.0};
    return surface;
}

float3 project_homogeneous(float4 value) {
    const float4 transformed = project_position(value);
    return transformed.xyz / transformed.w;
}

bool intersect_unit_volume(float3 origin, float3 direction, out float minimum, out float maximum) {
    const float3 first = (0.0 - origin) / direction;
    const float3 second = (1.0 - origin) / direction;
    const float3 lower = min(first, second);
    const float3 upper = max(first, second);
    minimum = max(max(lower.x, lower.y), lower.z);
    maximum = min(min(upper.x, upper.y), upper.z);
    return maximum >= max(minimum, 0.0);
}

float volume_noise(float3 position) {
    return frac(sin(dot(floor(position * float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x)), float3(12.9898, 78.233, 37.719))) * 43758.5453);
}

[shader("fragment")]
float4 visualization_fragment(VisualizationVertexOutput input) : SV_Target0 {
    float4 color = input.color;
    float visualization_depth = input.position.z;
    const uint kind = push_data.metadata.x;
    if (kind == 8) {
        color = volume_color(volume_mapping(volume_slice_position(input.texture_coordinates)));
        color.a *= push_data.screen_rect.y;
    }
    if (kind == 14) {
        const float3 origin = volume_slice_position(input.texture_coordinates);
        const float spacing = 1.0 / max(max(push_data.dimensions.z, push_data.dimensions.w), push_data.detail.x);
        float convolution = volume_noise(origin);
        float weight = 1.0;
        for (int direction = -1; direction <= 1; direction += 2) {
            float3 position = origin;
            for (uint step = 1; step <= push_data.detail.w; ++step) {
                float3 velocity = vector_to_grid(sample_volume_vector(position));
                if (push_data.detail.y == 0) velocity.x = 0.0;
                else if (push_data.detail.y == 1) velocity.y = 0.0;
                else velocity.z = 0.0;
                const float magnitude = length(velocity);
                if (magnitude == 0.0) break;
                position = clamp(position + velocity / magnitude * spacing * float(direction), 0.0, 1.0);
                const float kernel = 0.5 + 0.5 * cos(3.14159265 * float(step) / float(push_data.detail.w + 1));
                convolution += volume_noise(position) * kernel;
                weight += kernel;
            }
        }
        const float lic = convolution / weight;
        const float4 mapped = volume_color(volume_mapping(origin));
        color = float4(mapped.rgb * (0.25 + 0.75 * lic), mapped.a * push_data.screen_rect.y);
    }
    if (kind >= 9 && kind <= 11) {
        const float2 ndc = input.texture_coordinates * 2.0 - 1.0;
        const float3 near_grid = project_homogeneous(float4(ndc, 0.0, 1.0));
        const float3 far_grid = project_homogeneous(float4(ndc, 1.0, 1.0));
        const float3 direction = normalize(far_grid - near_grid);
        float ray_minimum;
        float ray_maximum;
        if (!intersect_unit_volume(near_grid, direction, ray_minimum, ray_maximum)) discard;
        ray_minimum = max(ray_minimum, 0.0);
        float opaque_distance = ray_maximum;
        if (push_data.metadata.w != 2) {
            Texture2D<float> depth = push_data.depth;
            const float scene_depth = depth.Load(int3(uint2(input.position.xy), 0));
            const float3 opaque_grid = project_homogeneous(float4(ndc, scene_depth, 1.0));
            opaque_distance = dot(opaque_grid - near_grid, direction);
        }
        if (push_data.metadata.w == 0) ray_maximum = min(ray_maximum, opaque_distance);
        if (ray_maximum <= ray_minimum) discard;
        const uint step_count = max(push_data.detail.w, 8u);
        const float step_length = (ray_maximum - ray_minimum) / float(step_count);
        if (kind == 9) {
            float4 accumulated = 0.0;
            for (uint step = 0; step != step_count; ++step) {
                const float3 position = near_grid + direction * (ray_minimum + (float(step) + 0.5) * step_length);
                const float value = volume_mapping(position);
                float4 sample_color = volume_color(value);
                const float normalized = saturate((value - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z));
                sample_color.a = (1.0 - exp(-normalized * push_data.screen_rect.y * step_length * float(step_count))) * push_data.color.w;
                if (push_data.metadata.w == 1 && ray_minimum + (float(step) + 0.5) * step_length > opaque_distance) sample_color.a *= 0.22;
                accumulated.rgb += (1.0 - accumulated.a) * sample_color.a * sample_color.rgb;
                accumulated.a += (1.0 - accumulated.a) * sample_color.a;
                if (accumulated.a > 0.995) break;
            }
            color = accumulated;
        } else if (kind == 10) {
            float maximum = push_data.parameters.z;
            float maximum_distance = ray_minimum;
            for (uint step = 0; step != step_count; ++step) {
                const float distance = ray_minimum + (float(step) + 0.5) * step_length;
                const float value = volume_mapping(near_grid + direction * distance);
                if (value > maximum) maximum = value, maximum_distance = distance;
            }
            color = volume_color(maximum);
            color.a *= push_data.screen_rect.y * saturate((maximum - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z));
            if (push_data.metadata.w == 1 && maximum_distance > opaque_distance) color.a *= 0.22;
        } else {
            bool hit = false;
            float3 hit_position = 0.0;
            float hit_distance = ray_minimum;
            float previous = volume_mapping(near_grid + direction * ray_minimum) - push_data.screen_rect.z;
            for (uint step = 1; step <= step_count; ++step) {
                const float3 position = near_grid + direction * (ray_minimum + float(step) * step_length);
                const float current = volume_mapping(position) - push_data.screen_rect.z;
                if (previous * current <= 0.0) {
                    hit = true;
                    hit_position = position;
                    hit_distance = ray_minimum + float(step) * step_length;
                    break;
                }
                previous = current;
            }
            if (!hit) discard;
            const float3 delta = 1.0 / float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
            const float3 normal = normalize(float3(
                volume_mapping(hit_position + float3(delta.x, 0.0, 0.0)) - volume_mapping(hit_position - float3(delta.x, 0.0, 0.0)),
                volume_mapping(hit_position + float3(0.0, delta.y, 0.0)) - volume_mapping(hit_position - float3(0.0, delta.y, 0.0)),
                volume_mapping(hit_position + float3(0.0, 0.0, delta.z)) - volume_mapping(hit_position - float3(0.0, 0.0, delta.z))));
            color = volume_color(push_data.screen_rect.z);
            color.rgb *= 0.25 + 0.75 * abs(dot(normal, -direction));
            color.a *= push_data.screen_rect.y;
            if (push_data.metadata.w == 1 && hit_distance > opaque_distance) color.a *= 0.22;
        }
        if (color.a == 0.0) discard;
    }
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
    if (input.sample_image == 3) {
        ByteAddressBuffer pixels = DescriptorHandle<ByteAddressBuffer>(push_data.primary);
        const uint2 coordinate = min(uint2(input.texture_coordinates * float2(push_data.dimensions.z, push_data.dimensions.w)), uint2(push_data.dimensions.z - 1, push_data.dimensions.w - 1));
        const uint layer_offset = input.image_layer * push_data.dimensions.z * push_data.dimensions.w;
        color *= load_reference_pixel(pixels, layer_offset + coordinate.y * push_data.dimensions.z + coordinate.x);
    }
    if (push_data.metadata.w != 2 && !(kind >= 9 && kind <= 11)) {
        Texture2D<float> depth = push_data.depth;
        const uint2 pixel = uint2(input.position.xy);
        const bool hidden = visualization_depth > depth.Load(int3(pixel, 0)) + 0.00001;
        if (hidden && push_data.metadata.w == 0) discard;
        if (hidden) color.a *= 0.22;
    }
    return color;
}
