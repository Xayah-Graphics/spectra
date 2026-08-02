module sampling;

public float2 sample_concentric_disk(
    float2 sample) {
    const float2 offset =
        2.0 * sample -
        1.0;
    if (
        offset.x == 0.0 &&
        offset.y == 0.0)
        return float2(0.0);
    float radius;
    float angle;
    if (abs(offset.x) >
        abs(offset.y)) {
        radius = offset.x;
        angle =
            0.78539816339744830962 *
            (
                offset.y /
                offset.x
            );
    }
    else {
        radius = offset.y;
        angle =
            1.57079632679489661923 -
            0.78539816339744830962 *
                (
                    offset.x /
                    offset.y
                );
    }
    return radius *
        float2(
            cos(angle),
            sin(angle));
}

public float3 sample_cosine_hemisphere(float2 sample, float3 normal) {
    const float radius = sqrt(sample.x);
    const float angle = 6.28318530717958647692 * sample.y;
    const float2 disk = radius * float2(cos(angle), sin(angle));
    const float sign_z = normal.z >= 0.0 ? 1.0 : -1.0;
    const float reciprocal = -1.0 / (sign_z + normal.z);
    const float product = normal.x * normal.y * reciprocal;
    const float3 tangent = float3(1.0 + sign_z * normal.x * normal.x * reciprocal, sign_z * product, -sign_z * normal.x);
    const float3 bitangent = float3(product, sign_z + normal.y * normal.y * reciprocal, -normal.y);
    return normalize(disk.x * tangent + disk.y * bitangent + sqrt(max(0.0, 1.0 - sample.x)) * normal);
}

public float3 offset_ray_origin(float3 position, float3 normal) {
    const int3 integer_offset = int3(256.0 * normal);
    const float3 integer_position = float3(
        asfloat(asint(position.x) + (position.x < 0.0 ? -integer_offset.x : integer_offset.x)),
        asfloat(asint(position.y) + (position.y < 0.0 ? -integer_offset.y : integer_offset.y)),
        asfloat(asint(position.z) + (position.z < 0.0 ? -integer_offset.z : integer_offset.z)));
    return float3(
        abs(position.x) < 0.03125 ? position.x + 0.0000152587890625 * normal.x : integer_position.x,
        abs(position.y) < 0.03125 ? position.y + 0.0000152587890625 * normal.y : integer_position.y,
        abs(position.z) < 0.03125 ? position.z + 0.0000152587890625 * normal.z : integer_position.z);
}

public struct RussianRouletteResult {
    public float4 throughput;
    public bool continue_path;
}

public RussianRouletteResult
apply_russian_roulette(
    float4 throughput,
    float eta_scale,
    uint depth,
    float sample) {
    RussianRouletteResult result;
    result.throughput = throughput;
    result.continue_path = true;
    if (depth == 0) return result;
    const float4 roulette_throughput =
        throughput * eta_scale;
    const float maximum_component =
        max(
            roulette_throughput.x,
            max(
                roulette_throughput.y,
                max(
                    roulette_throughput.z,
                    roulette_throughput.w)));
    if (maximum_component >= 1.0)
        return result;
    const float termination_probability =
        max(
            0.0,
            1.0 - maximum_component);
    if (sample < termination_probability) {
        result.continue_path = false;
        return result;
    }
    result.throughput /=
        1.0 - termination_probability;
    return result;
}

public RussianRouletteResult
apply_volpath_russian_roulette(
    float4 throughput,
    float4 r_u,
    float eta_scale,
    uint depth,
    float sample) {
    RussianRouletteResult result;
    result.throughput = throughput;
    result.continue_path = true;
    if (depth == 0) return result;
    const float probability =
        (
            r_u.x +
            r_u.y +
            r_u.z +
            r_u.w
        ) *
        0.25;
    if (probability == 0.0) {
        result.continue_path = false;
        return result;
    }
    const float4 roulette_throughput =
        throughput *
        eta_scale /
        probability;
    const float maximum_component =
        max(
            roulette_throughput.x,
            max(
                roulette_throughput.y,
                max(
                    roulette_throughput.z,
                    roulette_throughput.w)));
    if (maximum_component >= 1.0)
        return result;
    const float termination_probability =
        max(
            0.0,
            1.0 -
                maximum_component);
    if (sample < termination_probability) {
        result.continue_path = false;
        return result;
    }
    result.throughput /=
        1.0 -
        termination_probability;
    return result;
}
