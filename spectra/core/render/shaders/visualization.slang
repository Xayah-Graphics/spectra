module visualization;

#include "shader_semantics.h"

import color;
import shader_descriptor;

struct VisualizationPushData {
    UntypedDescriptorHandle primary;
    UntypedDescriptorHandle secondary;
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
    value.rgb = color_from_linear_srgb(color_to_linear_srgb(value.rgb, push_data.color_space), push_data.detail.x);
    return value;
}

float4 load_reference_pixel(ByteAddressBuffer pixels, uint pixel_index) {
    const uint packed = pixels.Load<uint>(pixel_index * 4);
    const float4 encoded = float4(packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u, packed >> 24u) * (1.0 / 255.0);
    const float3 linear = srgb_eotf(encoded.rgb);
    return float4(linear * encoded.a, 1.0);
}

float4 element_visualization_color(float4 element_color, float scalar) {
    if (push_data.detail.z == SPECTRA_COLOR_SOURCE_UNIFORM) return push_data.color;
    if (push_data.detail.z == SPECTRA_COLOR_SOURCE_SCALAR) return float4(visualization_color_map((scalar - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.w), element_color.a * push_data.color.a);
    return element_color * push_data.color;
}

float3 visualization_color_map(float value, uint color_map) {
    value = saturate(value);
    if (color_map == SPECTRA_COLOR_MAP_TURBO) {
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
    if (color_map == SPECTRA_COLOR_MAP_COOL_WARM) return lerp(float3(0.23, 0.30, 0.75), float3(0.71, 0.02, 0.15), value);
    if (color_map == SPECTRA_COLOR_MAP_GRAYSCALE) return value.xxx;
    const float3 c0 = float3(0.267, 0.005, 0.329);
    const float3 c1 = float3(0.128, 0.567, 0.551);
    const float3 c2 = float3(0.993, 0.906, 0.144);
    return value < 0.5 ? lerp(c0, c1, value * 2.0) : lerp(c1, c2, value * 2.0 - 1.0);
}

float3 categorical_color(uint category) {
    const float hue = frac(float(category) * 0.61803398875);
    const float3 value = abs(frac(hue + float3(0.0, 0.6666667, 0.3333333)) * 6.0 - 3.0) - 1.0;
    return lerp(1.0.xxx, saturate(value), 0.78);
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
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
    return sample_float_field(primary, position, uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x), 0.5, (push_data.metadata.y >> 8) != 0);
}

float3 sample_volume_vector(float3 position) {
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
    if ((push_data.metadata.y & 255) == SPECTRA_FIELD_FLOAT3) return sample_float3_field(primary, position, (push_data.metadata.y >> 8) != 0);
    ByteAddressBuffer secondary = DescriptorHandle<ByteAddressBuffer>(push_data.secondary.value);
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
    const uint field_kind = push_data.metadata.y & 255;
    if (field_kind == SPECTRA_FIELD_FLOAT) return sample_volume_scalar(position);
    if (field_kind == SPECTRA_FIELD_UINT32) {
        ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
        const uint3 resolution = uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
        const uint3 coordinate = min(uint3(position * float3(resolution)), resolution - 1);
        return float(primary.Load<uint>(volume_index(coordinate, resolution) * 4));
    }
    const float3 value = sample_volume_vector(position);
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_VALUE || push_data.metadata.z == SPECTRA_FIELD_MAPPING_MAGNITUDE) return length(value);
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_X) return value.x;
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_Y) return value.y;
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_Z) return value.z;
    const float3 delta = 1.0 / float3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
    const float3 dx = (sample_volume_vector(position + float3(delta.x, 0.0, 0.0)) - sample_volume_vector(position - float3(delta.x, 0.0, 0.0))) / (2.0 * delta.x);
    const float3 dy = (sample_volume_vector(position + float3(0.0, delta.y, 0.0)) - sample_volume_vector(position - float3(0.0, delta.y, 0.0))) / (2.0 * delta.y);
    const float3 dz = (sample_volume_vector(position + float3(0.0, 0.0, delta.z)) - sample_volume_vector(position - float3(0.0, 0.0, delta.z))) / (2.0 * delta.z);
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_DIVERGENCE) return dx.x + dy.y + dz.z;
    if (push_data.metadata.z == SPECTRA_FIELD_MAPPING_CURL_MAGNITUDE) return length(float3(dy.z - dz.y, dz.x - dx.z, dx.y - dy.x));
    const float3x3 gradient = float3x3(dx, dy, dz);
    const float3x3 strain = 0.5 * (gradient + transpose(gradient));
    const float3x3 rotation = 0.5 * (gradient - transpose(gradient));
    return 0.5 * (dot(rotation[0], rotation[0]) + dot(rotation[1], rotation[1]) + dot(rotation[2], rotation[2]) - dot(strain[0], strain[0]) - dot(strain[1], strain[1]) - dot(strain[2], strain[2]));
}

float4 volume_color(float value) {
    if ((push_data.metadata.y & 255) == SPECTRA_FIELD_UINT32) return float4(categorical_color((uint)value) * push_data.color.rgb, push_data.color.w);
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
    if (kind == SPECTRA_VISUALIZATION_PARTICLES) {
        ByteAddressBuffer positions = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
        ByteAddressBuffer field = DescriptorHandle<ByteAddressBuffer>(push_data.secondary.value);
        const float3 center = positions.Load<float3>(instance_id * 12);
        float4 color = push_data.color;
        if (push_data.metadata.y == SPECTRA_PARTICLE_POINTS) {
            const float value = field.Load<float>(instance_id * 4);
            color.rgb = visualization_color_map((value - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.z);
        } else if (push_data.metadata.y == SPECTRA_PARTICLE_DISCS) {
            const float3 value = field.Load<float3>(instance_id * 12);
            const float scalar = push_data.detail.y <= 1 ? length(value) : push_data.detail.y == 2 ? value.x : push_data.detail.y == 3 ? value.y : value.z;
            color.rgb = visualization_color_map((scalar - push_data.parameters.z) / (push_data.parameters.w - push_data.parameters.z), push_data.detail.z);
        } else if (push_data.metadata.y == SPECTRA_PARTICLE_SPHERES)
            color.rgb = categorical_color(field.Load<uint>(instance_id * 4));
        const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
        const float2 corner = corners[vertex_id];
        float radius = push_data.parameters.x;
        float4 position = project_position(transform_position(center));
        if (push_data.detail.x == 0)
            position.xy += corner * radius * 2.0 / float2(push_data.dimensions.xy) * position.w;
        else {
            const float transform_scale = max(length(push_data.transform_row_0.xyz), max(length(push_data.transform_row_1.xyz), length(push_data.transform_row_2.xyz)));
            radius *= transform_scale;
            const float horizontal_scale = length(float3(push_data.view_projection_row_0.x, push_data.view_projection_row_0.y, push_data.view_projection_row_0.z));
            const float vertical_scale = length(float3(push_data.view_projection_row_1.x, push_data.view_projection_row_1.y, push_data.view_projection_row_1.z));
            position.xy += corner * radius * float2(horizontal_scale, vertical_scale);
        }
        const uint glyph = push_data.detail.x == 2 ? 2u : push_data.detail.x == 1 ? 1u : 0u;
        const uint shading = push_data.detail.x == 2 ? 1u : 0u;
        VisualizationVertexOutput output = {position, color, corner * 0.5 + 0.5, glyph | (shading << 8), 2, float4(project_position(transform_position(center)).zw, radius, 0.0)};
        return output;
    }
    if (kind == SPECTRA_VISUALIZATION_SEGMENTS) {
        ByteAddressBuffer lines = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
        const uint offset = instance_id * 48u;
        return make_line(lines.Load<float3>(offset), lines.Load<float3>(offset + 16u), lines.Load<float>(offset + 12u) * push_data.parameters.x, element_visualization_color(lines.Load<float4>(offset + 28u), lines.Load<float>(offset + 44u)), vertex_id);
    }
    if (kind == SPECTRA_VISUALIZATION_VECTORS) {
        ByteAddressBuffer vectors = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
        const uint offset = instance_id * 48u;
        const float3 origin = vectors.Load<float3>(offset);
        const float3 vector = vectors.Load<float3>(offset + 16u) * push_data.parameters.y;
        const float width = vectors.Load<float>(offset + 12u) * push_data.parameters.x;
        const float4 color = element_visualization_color(vectors.Load<float4>(offset + 28u), vectors.Load<float>(offset + 44u));
        const float length_value = length(vector);
        const float3 direction = length_value == 0.0 ? float3(1.0, 0.0, 0.0) : vector / length_value;
        const float3 reference = abs(direction.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(0.0, 1.0, 0.0);
        const float3 side = normalize(cross(direction, reference)) * length_value * 0.18;
        const uint part = vertex_id / 6;
        const float3 first = part == 0 ? origin : origin + vector;
        const float3 second = part == 0 ? origin + vector : origin + vector - direction * length_value * 0.28 + (part == 1 ? side : -side);
        return make_line(first, second, width, color, vertex_id % 6);
    }
    if (kind == SPECTRA_VISUALIZATION_IMAGE) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = push_data.screen_rect.xy + uv * push_data.screen_rect.zw;
        VisualizationVertexOutput output = {float4(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0, 0.0, 1.0), push_data.color, uv, 0, 1, 0.0};
        return output;
    }
    if (kind == SPECTRA_VISUALIZATION_REFERENCE_OVERLAY) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = push_data.screen_rect.xy + uv * push_data.screen_rect.zw;
        VisualizationVertexOutput output = {float4(screen.x * 2.0 - 1.0, 1.0 - screen.y * 2.0, 0.0, 1.0), push_data.color, uv, push_data.detail.x, 3, 0.0};
        return output;
    }
    if (kind == SPECTRA_VISUALIZATION_REFERENCE_PLANE) {
        const float2 corners[6] = {float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 0), float2(1, 1), float2(0, 1)};
        const float2 uv = corners[vertex_id];
        const float2 screen = float2(lerp(push_data.screen_rect.x, push_data.screen_rect.z, uv.x), lerp(push_data.screen_rect.w, push_data.screen_rect.y, uv.y));
        const float distance = push_data.parameters.y;
        const float3 local = float3(screen * push_data.parameters.x * distance, -distance);
        VisualizationVertexOutput output = {project_position(transform_position(local)), push_data.color, uv, push_data.detail.x, 3, 0.0};
        return output;
    }
    if (kind == SPECTRA_VISUALIZATION_VOLUME_SLICE || kind == SPECTRA_VISUALIZATION_VOLUME_LIC) {
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
    if (kind == SPECTRA_VISUALIZATION_VOLUME_GLYPHS) {
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
    if (kind == SPECTRA_VISUALIZATION_VOLUME_STREAMLINES) {
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
    if (kind == SPECTRA_VISUALIZATION_VOLUME_CELLS) {
        const uint3 resolution = uint3(push_data.dimensions.z, push_data.dimensions.w, push_data.detail.x);
        const uint z = instance_id / (resolution.x * resolution.y);
        const uint y = (instance_id / resolution.x) % resolution.y;
        const uint x = instance_id % resolution.x;
        ByteAddressBuffer values = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
        const uint category = values.Load<uint>(instance_id * 4);
        const uint edges[24] = {0, 1, 1, 3, 3, 2, 2, 0, 4, 5, 5, 7, 7, 6, 6, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        const float3 corners[8] = {float3(0, 0, 0), float3(1, 0, 0), float3(0, 1, 0), float3(1, 1, 0), float3(0, 0, 1), float3(1, 0, 1), float3(0, 1, 1), float3(1, 1, 1)};
        const uint edge = vertex_id / 6;
        const float3 origin = float3(x, y, z);
        const float3 first = (origin + corners[edges[edge * 2]]) / float3(resolution);
        const float3 second = (origin + corners[edges[edge * 2 + 1]]) / float3(resolution);
        const bool visible = category < 32 && (push_data.color_space & (1u << category)) != 0;
        const float4 color = float4(categorical_color(category) * push_data.color.rgb, visible ? push_data.color.a : 0.0);
        return make_volume_line(first, second, push_data.parameters.x, color, vertex_id % 6);
    }
    ByteAddressBuffer primary = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
    ByteAddressBuffer indices = DescriptorHandle<ByteAddressBuffer>(push_data.secondary.value);
    const uint position_index = push_data.metadata.z == 0 ? vertex_id : asuint(indices.Load<float>(vertex_id * 4));
    float4 surface_color = push_data.color;
    if (push_data.detail.z == SPECTRA_COLOR_SOURCE_ELEMENT) {
        StructuredBuffer<float4> colors = DescriptorHandle<StructuredBuffer<float4>>(uint2(push_data.color_space, push_data.reserved));
        surface_color = element_visualization_color(colors[position_index], 0.0);
    } else if (push_data.detail.z == SPECTRA_COLOR_SOURCE_SCALAR) {
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
    if (kind == SPECTRA_VISUALIZATION_VOLUME_SLICE) {
        color = volume_color(volume_mapping(volume_slice_position(input.texture_coordinates)));
        color.a *= push_data.screen_rect.y;
    }
    if (kind == SPECTRA_VISUALIZATION_VOLUME_LIC) {
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
        if (kind == SPECTRA_VISUALIZATION_VOLUME_RAY_MARCH) {
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
        } else if (kind == SPECTRA_VISUALIZATION_VOLUME_MIP) {
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
        ByteAddressBuffer pixels = DescriptorHandle<ByteAddressBuffer>(push_data.secondary.value);
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
        ByteAddressBuffer pixels = DescriptorHandle<ByteAddressBuffer>(push_data.primary.value);
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
