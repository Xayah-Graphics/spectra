module neural_field;

struct NeuralFieldPushData {
    uint2 output;
    DescriptorHandle<Texture2D<float>> depth;
    DescriptorHandle<ByteAddressBuffer> hash_grid;
    DescriptorHandle<ByteAddressBuffer> density_input;
    DescriptorHandle<ByteAddressBuffer> density_output;
    DescriptorHandle<ByteAddressBuffer> rgb_input;
    DescriptorHandle<ByteAddressBuffer> rgb_hidden;
    DescriptorHandle<ByteAddressBuffer> rgb_output;
    DescriptorHandle<StructuredBuffer<uint>> occupancy;
    uint4 metadata;
    float4 camera_clip;
    float4 inverse_transform_row_0;
    float4 inverse_transform_row_1;
    float4 inverse_transform_row_2;
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
}

[vk::push_constant]
uniform NeuralFieldPushData push_data;

static const uint grid_offsets[9] = {0, 4096, 36864, 299008, 823296, 1347584, 1871872, 2396160, 2920448};

uint grid_index(uint size, uint resolution, uint3 coordinate) {
    const uint64_t dense = uint64_t(resolution) * uint64_t(resolution) * uint64_t(resolution);
    if (dense <= uint64_t(size)) return (coordinate.z * resolution + coordinate.y) * resolution + coordinate.x;
    return (coordinate.x ^ coordinate.y * 2654435761u ^ coordinate.z * 805459861u) & (size - 1u);
}

CoopVec<float16_t, 32> encode_position(float3 position) {
    ByteAddressBuffer grid = push_data.hash_grid;
    CoopVec<float16_t, 32> encoded;
    [ForceUnroll]
    for (uint level = 0; level != 8; ++level) {
        const uint resolution = 16u << level;
        const uint level_offset = grid_offsets[level];
        const uint size = grid_offsets[level + 1] - level_offset;
        const float3 scaled = fma(float3(resolution - 1u), position, 0.5);
        const uint3 base = uint3(floor(scaled));
        const float3 fraction = scaled - float3(base);
        [ForceUnroll]
        for (uint feature = 0; feature != 4; ++feature) {
            float16_t value = float16_t(0.0);
            [ForceUnroll]
            for (uint corner = 0; corner != 8; ++corner) {
                const uint3 high = uint3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
                const float3 weight = lerp(1.0 - fraction, fraction, float3(high));
                const uint index = (level_offset + grid_index(size, resolution, base + high)) * 4u + feature;
                value = fma(float16_t(weight.x * weight.y * weight.z), grid.Load<float16_t>(index * 2u), value);
            }
            encoded[level * 4u + feature] = value;
        }
    }
    return encoded;
}

CoopVec<float16_t, 16> density_features(float3 position) {
    CoopVec<float16_t, 64> hidden = coopVecMatMul<float16_t, 64, 32>(
        encode_position(position),
        CoopVecComponentType::Float16,
        push_data.density_input,
        0,
        CoopVecComponentType::Float16,
        CoopVecMatrixLayout::InferencingOptimal,
        false,
        0);
    [ForceUnroll]
    for (uint index = 0; index != 64; ++index) hidden[index] = max(hidden[index], float16_t(0.0));
    return coopVecMatMul<float16_t, 16, 64>(
        hidden,
        CoopVecComponentType::Float16,
        push_data.density_output,
        0,
        CoopVecComponentType::Float16,
        CoopVecMatrixLayout::InferencingOptimal,
        false,
        0);
}

CoopVec<float16_t, 16> encode_direction(float3 direction) {
    const float3 warped = (direction + 1.0) * 0.5;
    const float x = warped.x * 2.0 - 1.0;
    const float y = warped.y * 2.0 - 1.0;
    const float z = warped.z * 2.0 - 1.0;
    const float xy = x * y;
    const float xz = x * z;
    const float yz = y * z;
    const float x2 = x * x;
    const float y2 = y * y;
    const float z2 = z * z;
    CoopVec<float16_t, 16> encoded;
    encoded[0] = float16_t(0.28209479177387814);
    encoded[1] = float16_t(-0.48860251190291987 * y);
    encoded[2] = float16_t(0.48860251190291987 * z);
    encoded[3] = float16_t(-0.48860251190291987 * x);
    encoded[4] = float16_t(1.0925484305920792 * xy);
    encoded[5] = float16_t(-1.0925484305920792 * yz);
    encoded[6] = float16_t(0.94617469575755997 * z2 - 0.31539156525251999);
    encoded[7] = float16_t(-1.0925484305920792 * xz);
    encoded[8] = float16_t(0.54627421529603959 * x2 - 0.54627421529603959 * y2);
    encoded[9] = float16_t(0.59004358992664352 * y * (-3.0 * x2 + y2));
    encoded[10] = float16_t(2.8906114426405538 * xy * z);
    encoded[11] = float16_t(0.45704579946446572 * y * (1.0 - 5.0 * z2));
    encoded[12] = float16_t(0.3731763325901154 * z * (5.0 * z2 - 3.0));
    encoded[13] = float16_t(0.45704579946446572 * x * (1.0 - 5.0 * z2));
    encoded[14] = float16_t(1.4453057213202769 * z * (x2 - y2));
    encoded[15] = float16_t(0.59004358992664352 * x * (-x2 + 3.0 * y2));
    return encoded;
}

float4 evaluate_field(float3 position, float3 direction) {
    const CoopVec<float16_t, 16> density = density_features(position);
    CoopVec<float16_t, 32> rgb_features;
    [ForceUnroll]
    for (uint index = 0; index != 16; ++index) rgb_features[index] = density[index];
    const CoopVec<float16_t, 16> direction_features = encode_direction(direction);
    [ForceUnroll]
    for (uint index = 0; index != 16; ++index) rgb_features[index + 16] = direction_features[index];
    CoopVec<float16_t, 64> hidden = coopVecMatMul<float16_t, 64, 32>(
        rgb_features,
        CoopVecComponentType::Float16,
        push_data.rgb_input,
        0,
        CoopVecComponentType::Float16,
        CoopVecMatrixLayout::InferencingOptimal,
        false,
        0);
    [ForceUnroll]
    for (uint index = 0; index != 64; ++index) hidden[index] = max(hidden[index], float16_t(0.0));
    hidden = coopVecMatMul<float16_t, 64, 64>(
        hidden,
        CoopVecComponentType::Float16,
        push_data.rgb_hidden,
        0,
        CoopVecComponentType::Float16,
        CoopVecMatrixLayout::InferencingOptimal,
        false,
        0);
    [ForceUnroll]
    for (uint index = 0; index != 64; ++index) hidden[index] = max(hidden[index], float16_t(0.0));
    const CoopVec<float16_t, 16> output = coopVecMatMul<float16_t, 16, 64>(
        hidden,
        CoopVecComponentType::Float16,
        push_data.rgb_output,
        0,
        CoopVecComponentType::Float16,
        CoopVecMatrixLayout::InferencingOptimal,
        false,
        0);
    return float4(
        1.0 / (1.0 + exp(-float(output[0]))),
        1.0 / (1.0 + exp(-float(output[1]))),
        1.0 / (1.0 + exp(-float(output[2]))),
        exp(float(density[0])));
}

float3 transform_point(float3 point) {
    const float4 homogeneous = float4(point, 1.0);
    return float3(
        dot(push_data.inverse_transform_row_0, homogeneous),
        dot(push_data.inverse_transform_row_1, homogeneous),
        dot(push_data.inverse_transform_row_2, homogeneous));
}

float3 transform_direction(float3 direction) {
    return float3(
        dot(push_data.inverse_transform_row_0.xyz, direction),
        dot(push_data.inverse_transform_row_1.xyz, direction),
        dot(push_data.inverse_transform_row_2.xyz, direction));
}

bool intersect_domain(float3 origin, float3 direction, out float minimum, out float maximum) {
    const float3 local_origin = transform_point(origin);
    const float3 local_direction = transform_direction(direction);
    const float3 inverse_direction = 1.0 / local_direction;
    const float3 first = -local_origin * inverse_direction;
    const float3 second = (1.0 - local_origin) * inverse_direction;
    const float3 lower = min(first, second);
    const float3 upper = max(first, second);
    minimum = max(max(lower.x, lower.y), lower.z);
    maximum = min(min(upper.x, upper.y), upper.z);
    return maximum > max(minimum, 0.0);
}

float3 srgb_eotf(float3 value) {
    return float3(
        value.x <= 0.04045 ? value.x / 12.92 : pow((value.x + 0.055) / 1.055, 2.4),
        value.y <= 0.04045 ? value.y / 12.92 : pow((value.y + 0.055) / 1.055, 2.4),
        value.z <= 0.04045 ? value.z / 12.92 : pow((value.z + 0.055) / 1.055, 2.4));
}

float3 output_color_space(float3 linear_srgb) {
    if (push_data.metadata.z == 1)
        return float3(
            dot(float3(0.627404, 0.329283, 0.043313), linear_srgb),
            dot(float3(0.069097, 0.919540, 0.011362), linear_srgb),
            dot(float3(0.016391, 0.088013, 0.895595), linear_srgb));
    if (push_data.metadata.z == 2)
        return float3(
            dot(float3(0.439586, 0.383929, 0.176533), linear_srgb),
            dot(float3(0.089540, 0.814750, 0.095684), linear_srgb),
            dot(float3(0.017387, 0.108739, 0.873821), linear_srgb));
    return linear_srgb;
}

uint morton_component(uint value) {
    value = (value * 0x00010001u) & 0xFF0000FFu;
    value = (value * 0x00000101u) & 0x0F00F00Fu;
    value = (value * 0x00000011u) & 0xC30C30C3u;
    return (value * 0x00000005u) & 0x49249249u;
}

float advance_to_next_occupancy_cell(float distance, float3 position, float3 direction, float3 inverse_direction, float step) {
    const float3 cell = (position - 0.5) * 128.0;
    const float3 direction_sign = float3(direction.x >= 0.0 ? 1.0 : -1.0, direction.y >= 0.0 ? 1.0 : -1.0, direction.z >= 0.0 ? 1.0 : -1.0);
    const float3 boundary = floor(cell + 0.5 + 0.5 * direction_sign);
    const float3 cell_distance = (boundary - cell) * inverse_direction;
    const float target = distance + max(min(min(cell_distance.x, cell_distance.y), cell_distance.z) / 128.0, 0.0);
    return distance + ceil(max((target - distance) / step, 0.5)) * step;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void render_neural_field(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel = dispatch_thread_id.xy;
    if (any(pixel >= push_data.metadata.xy)) return;
    const float2 camera_plane = (float2(pixel) + 0.5 - float2(push_data.camera_up.w, push_data.camera_forward.w)) / float2(push_data.camera_position.w, push_data.camera_right.w);
    const bool perspective = push_data.metadata.w != 0u;
    const float3 ray_origin = push_data.camera_position.xyz + (perspective ? 0.0 : push_data.camera_right.xyz * camera_plane.x - push_data.camera_up.xyz * camera_plane.y);
    const float3 ray_direction = perspective
        ? normalize(fma(push_data.camera_right.xyz, camera_plane.x, fma(-push_data.camera_up.xyz, camera_plane.y, push_data.camera_forward.xyz)))
        : push_data.camera_forward.xyz;
    Texture2D<float> depth_texture = push_data.depth;
    const float depth = depth_texture.Load(int3(pixel, 0));
    float surface_distance = 1.0e20;
    if (depth < 1.0) {
        const float near_plane = push_data.camera_clip.x;
        const float far_plane = push_data.camera_clip.y;
        const float view_depth = perspective
            ? far_plane * near_plane / (far_plane + depth * (near_plane - far_plane))
            : lerp(near_plane, far_plane, depth);
        surface_distance = perspective ? view_depth / dot(ray_direction, push_data.camera_forward.xyz) : view_depth;
    }
    float minimum;
    float maximum;
    if (!intersect_domain(ray_origin, ray_direction, minimum, maximum)) return;
    minimum = max(minimum, 0.0);
    maximum = min(maximum, surface_distance);
    if (maximum <= minimum) return;
    const float step = 1.7320508075688772 / 1024.0;
    const float3 local_origin = transform_point(ray_origin);
    const float3 local_direction = transform_direction(ray_direction);
    const float3 inverse_local_direction = 1.0 / local_direction;
    const float3 direction = normalize(local_direction);
    float3 radiance = 0.0;
    float transmittance = 1.0;
    float distance = minimum + 0.5 * step;
    StructuredBuffer<uint> occupancy = push_data.occupancy;
    while (distance < maximum) {
        float3 position = fma(local_direction, distance, local_origin);
        while (distance < maximum) {
            const uint3 coordinate = min(uint3(position * 128.0), 127u);
            const uint occupancy_index = morton_component(coordinate.x) | (morton_component(coordinate.y) << 1u) | (morton_component(coordinate.z) << 2u);
            if ((occupancy[occupancy_index >> 5u] & (1u << (occupancy_index & 31u))) != 0u) break;
            distance = advance_to_next_occupancy_cell(distance, position, local_direction, inverse_local_direction, step);
            position = fma(local_direction, distance, local_origin);
        }
        if (distance >= maximum) break;
        const float4 value = evaluate_field(position, direction);
        const float alpha = 1.0 - exp(-value.w * step);
        radiance += transmittance * alpha * value.xyz;
        transmittance *= 1.0 - alpha;
        if (transmittance < 1.0e-4) break;
        distance += step;
    }
    RWTexture2D<float4> output = DescriptorHandle<RWTexture2D<float4>>(push_data.output);
    const float4 base = output[pixel];
    output[pixel] = float4(output_color_space(srgb_eotf(radiance)) + transmittance * base.xyz, 1.0);
}
