module scene_diagnostic;

struct DiagnosticPushData {
    DescriptorHandle<ByteAddressBuffer> primary;
    uint2 secondary;
    uint2 tertiary;
    DescriptorHandle<Texture2D<float>> depth;
    uint4 metadata;
    uint4 detail;
    float4 parameters;
    float4 color;
    float4 transform_row_0;
    float4 transform_row_1;
    float4 transform_row_2;
    float4 transform_row_3;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
}

struct DiagnosticVertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR;
    float2 texture_coordinates : TEXCOORD;
    nointerpolation uint point : POINT;
    nointerpolation uint pick_index : PICK_INDEX;
    nointerpolation uint depth_mode : DEPTH_MODE;
}

struct DiagnosticFragmentOutput {
    float4 color : SV_Target0;
    uint pick_index : SV_Target1;
}

[vk::push_constant]
uniform DiagnosticPushData push_data;

float4 transform_position(float3 position) {
    const float4 value = float4(position, 1.0);
    if (push_data.detail.w != 0xffffffff) {
        ByteAddressBuffer transforms = DescriptorHandle<ByteAddressBuffer>(push_data.tertiary);
        const uint offset = push_data.detail.w * 64;
        return float4(dot(transforms.Load<float4>(offset), value), dot(transforms.Load<float4>(offset + 16), value), dot(transforms.Load<float4>(offset + 32), value), dot(transforms.Load<float4>(offset + 48), value));
    }
    return float4(dot(push_data.transform_row_0, value), dot(push_data.transform_row_1, value), dot(push_data.transform_row_2, value), dot(push_data.transform_row_3, value));
}

float3 transform_vector(float3 vector) {
    if (push_data.detail.w != 0xffffffff) {
        ByteAddressBuffer transforms = DescriptorHandle<ByteAddressBuffer>(push_data.tertiary);
        const uint offset = push_data.detail.w * 64;
        return float3(dot(transforms.Load<float4>(offset).xyz, vector), dot(transforms.Load<float4>(offset + 16).xyz, vector), dot(transforms.Load<float4>(offset + 32).xyz, vector));
    }
    return float3(dot(push_data.transform_row_0.xyz, vector), dot(push_data.transform_row_1.xyz, vector), dot(push_data.transform_row_2.xyz, vector));
}

float3 transform_normal(float3 normal) {
    float3 row_0 = push_data.transform_row_0.xyz;
    float3 row_1 = push_data.transform_row_1.xyz;
    float3 row_2 = push_data.transform_row_2.xyz;
    if (push_data.detail.w != 0xffffffff) {
        ByteAddressBuffer transforms = DescriptorHandle<ByteAddressBuffer>(push_data.tertiary);
        const uint offset = push_data.detail.w * 64;
        row_0 = transforms.Load<float4>(offset).xyz;
        row_1 = transforms.Load<float4>(offset + 16).xyz;
        row_2 = transforms.Load<float4>(offset + 32).xyz;
    }
    const float3 column_0 = float3(row_0.x, row_1.x, row_2.x);
    const float3 column_1 = float3(row_0.y, row_1.y, row_2.y);
    const float3 column_2 = float3(row_0.z, row_1.z, row_2.z);
    return cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z;
}

float4 project_position(float4 position) {
    return float4(dot(push_data.view_projection_row_0, position), dot(push_data.view_projection_row_1, position), dot(push_data.view_projection_row_2, position), dot(push_data.view_projection_row_3, position));
}

float4 wide_line_position(float4 first, float4 second, uint triangle_vertex, float width) {
    const uint corners[6] = {0, 1, 2, 0, 2, 3};
    const uint corner = corners[triangle_vertex];
    const bool use_second = corner == 1 || corner == 2;
    const float side = corner == 0 || corner == 1 ? -1.0 : 1.0;
    const float2 first_ndc = first.xy / first.w;
    const float2 second_ndc = second.xy / second.w;
    const float2 direction_pixels = (second_ndc - first_ndc) * float2(push_data.detail.xy);
    const float2 normal = dot(direction_pixels, direction_pixels) == 0.0 ? float2(0.0, 1.0) : normalize(float2(-direction_pixels.y, direction_pixels.x));
    float4 position = use_second ? second : first;
    position.xy += normal * width * side / float2(push_data.detail.xy) * position.w;
    return position;
}

DiagnosticVertexOutput make_line(float3 first, float3 second, float width, float4 color, uint pick_index, uint depth_mode, uint triangle_vertex) {
    DiagnosticVertexOutput output;
    output.position = wide_line_position(project_position(transform_position(first)), project_position(transform_position(second)), triangle_vertex, width);
    output.color = color;
    output.texture_coordinates = 0.0;
    output.point = 0;
    output.pick_index = pick_index;
    output.depth_mode = depth_mode;
    return output;
}

float ordered_float(uint value) {
    const uint bits = (value & 0x80000000) != 0 ? value ^ 0x80000000 : ~value;
    return asfloat(bits);
}

uint compact_morton_component(uint value) {
    value &= 0x49249249u;
    value = (value ^ (value >> 2u)) & 0xC30C30C3u;
    value = (value ^ (value >> 4u)) & 0x0F00F00Fu;
    value = (value ^ (value >> 8u)) & 0xFF0000FFu;
    return (value ^ (value >> 16u)) & 0x000003FFu;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void compact_scene_diagnostic_occupancy(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint word_index = dispatch_thread_id.x;
    if (word_index >= 65536u) return;
    ByteAddressBuffer occupancy = push_data.primary;
    RWByteAddressBuffer occupied_cells = DescriptorHandle<RWByteAddressBuffer>(push_data.secondary);
    RWByteAddressBuffer draw = DescriptorHandle<RWByteAddressBuffer>(push_data.tertiary);
    if (word_index == 0u) draw.Store(0u, 72u);
    uint bits = occupancy.Load<uint>(word_index * 4u);
    const uint cell_count = countbits(bits);
    if (cell_count == 0u) return;
    uint destination;
    draw.InterlockedAdd(4u, cell_count, destination);
    while (bits != 0u) {
        const uint bit = firstbitlow(bits);
        const uint morton_index = word_index * 32u + bit;
        const uint linear_index = compact_morton_component(morton_index) | (compact_morton_component(morton_index >> 1u) << 7u) | (compact_morton_component(morton_index >> 2u) << 14u);
        occupied_cells.Store(destination * 4u, linear_index);
        ++destination;
        bits &= bits - 1u;
    }
}

[shader("vertex")]
DiagnosticVertexOutput scene_diagnostic_vertex(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID) {
    const uint kind = push_data.metadata.x;
    if (kind == 0) {
        ByteAddressBuffer lines = push_data.primary;
        const uint offset = instance_id * 64;
        return make_line(lines.Load<float3>(offset), lines.Load<float3>(offset + 16), lines.Load<float>(offset + 12), lines.Load<float4>(offset + 32), asuint(lines.Load<float>(offset + 48)), asuint(lines.Load<float>(offset + 28)), vertex_id);
    }
    if (kind == 1) {
        ByteAddressBuffer boxes = push_data.primary;
        ByteAddressBuffer bounds = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
        const uint offset = instance_id * 128;
        const float4 transform_row_0 = boxes.Load<float4>(offset);
        const float4 transform_row_1 = boxes.Load<float4>(offset + 16);
        const float4 transform_row_2 = boxes.Load<float4>(offset + 32);
        const float3 stored_minimum = boxes.Load<float3>(offset + 64);
        const float3 stored_maximum = boxes.Load<float3>(offset + 80);
        const float4 color = boxes.Load<float4>(offset + 96);
        const uint4 metadata = asuint(boxes.Load<float4>(offset + 112));
        float3 minimum = stored_minimum;
        float3 maximum = stored_maximum;
        if (metadata.y != 0) {
            const uint4 computed_minimum = bounds.Load<uint4>(metadata.x * 32);
            const uint4 computed_maximum = bounds.Load<uint4>(metadata.x * 32 + 16);
            minimum = float3(ordered_float(computed_minimum.x), ordered_float(computed_minimum.y), ordered_float(computed_minimum.z));
            maximum = float3(ordered_float(computed_maximum.x), ordered_float(computed_maximum.y), ordered_float(computed_maximum.z));
        }
        const uint edges[24] = {0,1, 1,3, 3,2, 2,0, 4,5, 5,7, 7,6, 6,4, 0,4, 1,5, 2,6, 3,7};
        const uint edge = vertex_id / 6;
        const uint first_corner = edges[edge * 2];
        const uint second_corner = edges[edge * 2 + 1];
        const float3 first_local = float3((first_corner & 1) != 0 ? maximum.x : minimum.x, (first_corner & 2) != 0 ? maximum.y : minimum.y, (first_corner & 4) != 0 ? maximum.z : minimum.z);
        const float3 second_local = float3((second_corner & 1) != 0 ? maximum.x : minimum.x, (second_corner & 2) != 0 ? maximum.y : minimum.y, (second_corner & 4) != 0 ? maximum.z : minimum.z);
        const float4 first_value = float4(first_local, 1.0);
        const float4 second_value = float4(second_local, 1.0);
        const float3 first = float3(dot(transform_row_0, first_value), dot(transform_row_1, first_value), dot(transform_row_2, first_value));
        const float3 second = float3(dot(transform_row_0, second_value), dot(transform_row_1, second_value), dot(transform_row_2, second_value));
        return make_line(first, second, boxes.Load<float>(offset + 76), color, metadata.z, metadata.w, vertex_id % 6);
    }
    if (kind == 9) {
        ByteAddressBuffer occupied_cells = push_data.primary;
        const uint linear_index = occupied_cells.Load<uint>(instance_id * 4u);
        const uint3 coordinate = uint3(linear_index & 127u, (linear_index >> 7u) & 127u, linear_index >> 14u);
        const uint edges[24] = {0,1, 1,3, 3,2, 2,0, 4,5, 5,7, 7,6, 6,4, 0,4, 1,5, 2,6, 3,7};
        const uint edge = vertex_id / 6u;
        const uint first_corner = edges[edge * 2u];
        const uint second_corner = edges[edge * 2u + 1u];
        const float3 minimum = float3(coordinate) / 128.0;
        const float3 first = minimum + float3(first_corner & 1u, (first_corner >> 1u) & 1u, (first_corner >> 2u) & 1u) / 128.0;
        const float3 second = minimum + float3(second_corner & 1u, (second_corner >> 1u) & 1u, (second_corner >> 2u) & 1u) / 128.0;
        return make_line(first, second, push_data.parameters.x, push_data.color, 0u, 1u, vertex_id % 6u);
    }
    ByteAddressBuffer primary = push_data.primary;
    ByteAddressBuffer secondary = DescriptorHandle<ByteAddressBuffer>(push_data.secondary);
    if (kind == 8) {
        const uint line_vertex = vertex_id % 6;
        const uint segment_index = vertex_id / 6;
        const uint axis = segment_index / 32;
        const uint segment = segment_index % 32;
        const float first_angle = float(segment) * 2.0 * 3.14159265358979323846 / 32.0;
        const float second_angle = float(segment + 1) * 2.0 * 3.14159265358979323846 / 32.0;
        const float3 center = primary.Load<float3>(instance_id * 12);
        const float radius = secondary.Load<float>(instance_id * 4);
        const float3 first_offset = axis == 0 ? float3(0.0, cos(first_angle), sin(first_angle)) : axis == 1 ? float3(cos(first_angle), 0.0, sin(first_angle)) : float3(cos(first_angle), sin(first_angle), 0.0);
        const float3 second_offset = axis == 0 ? float3(0.0, cos(second_angle), sin(second_angle)) : axis == 1 ? float3(cos(second_angle), 0.0, sin(second_angle)) : float3(cos(second_angle), sin(second_angle), 0.0);
        return make_line(center + first_offset * radius, center + second_offset * radius, push_data.parameters.x, push_data.color, push_data.metadata.z, push_data.metadata.w, line_vertex);
    }
    if (kind == 2) {
        const uint edge_index = vertex_id / 6;
        const uint triangle = edge_index / 3;
        const uint edge = edge_index % 3;
        const uint first_index = asuint(secondary.Load<float>((triangle * 3 + edge) * 4));
        const uint second_index = asuint(secondary.Load<float>((triangle * 3 + (edge + 1) % 3) * 4));
        return make_line(primary.Load<float3>(first_index * 12), primary.Load<float3>(second_index * 12), push_data.parameters.x, push_data.color, push_data.metadata.z, push_data.metadata.w, vertex_id % 6);
    }
    if (kind == 4 || kind == 5) {
        const uint line_index = vertex_id / 6;
        const uint source_index = line_index * push_data.detail.z;
        const float3 first = primary.Load<float3>(source_index * 12);
        const float3 source_direction = secondary.Load<float3>(source_index * 12);
        const float3 direction = normalize(kind == 4 ? transform_normal(source_direction) : transform_vector(source_direction)) * push_data.parameters.z;
        const float3 world_first = transform_position(first).xyz;
        const float3 world_second = world_first + direction;
        DiagnosticVertexOutput output;
        output.position = wide_line_position(project_position(float4(world_first, 1.0)), project_position(float4(world_second, 1.0)), vertex_id % 6, push_data.parameters.x);
        output.color = push_data.color;
        output.texture_coordinates = 0.0;
        output.point = 0;
        output.pick_index = push_data.metadata.z;
        output.depth_mode = push_data.metadata.w;
        return output;
    }
    const uint point_index = vertex_id / 6;
    const uint point_vertex = vertex_id % 6;
    const float2 corners[6] = {float2(-1,-1), float2(1,-1), float2(1,1), float2(-1,-1), float2(1,1), float2(-1,1)};
    const float3 center = primary.Load<float3>(point_index * 12);
    float4 position = project_position(transform_position(center));
    position.xy += corners[point_vertex] * push_data.parameters.y * 2.0 / float2(push_data.detail.xy) * position.w;
    DiagnosticVertexOutput point_output = {position, push_data.color, corners[point_vertex] * 0.5 + 0.5, 1, push_data.metadata.z, push_data.metadata.w};
    return point_output;
}

[shader("fragment")]
DiagnosticFragmentOutput scene_diagnostic_fragment(DiagnosticVertexOutput input) {
    if (input.point != 0) {
        const float2 point = input.texture_coordinates * 2.0 - 1.0;
        if (dot(point, point) > 1.0) discard;
    }
    float4 color = input.color;
    if (input.depth_mode != 2) {
        Texture2D<float> depth = push_data.depth;
        const bool hidden = input.position.z > depth.Load(int3(uint2(input.position.xy), 0)) + 0.00001;
        if (hidden && input.depth_mode == 0) discard;
        if (hidden && push_data.metadata.x == 9u) color.rgb *= 0.22;
        else if (hidden) color.a *= 0.22;
    }
    if (push_data.metadata.x == 9u) color.a = 1.0;
    DiagnosticFragmentOutput output = {color, input.pick_index};
    return output;
}
