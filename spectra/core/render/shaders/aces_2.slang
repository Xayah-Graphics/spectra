module aces_2;

import aces_2_tables;

public float3 acescg_to_aces2065_1(float3 value) {
    return float3(
        dot(float3(0.695452241357f, 0.140678696470f, 0.163869062172f), value),
        dot(float3(0.044794563372f, 0.859671118456f, 0.095534318172f), value),
        dot(float3(-0.005525882558f, 0.004025210306f, 1.001500672252f), value));
}

float aces2_wrap_hue(float hue) {
    float result = hue - floor(hue / 360.0f) * 360.0f;
    return result < 0.0f ? result + 360.0f : result;
}

float aces2_reach(float hue) {
    float base = floor(hue);
    int lower = int(base) + 1;
    return lerp(aces2_reach_table[lower], aces2_reach_table[lower + 1], hue - base);
}

float3 aces2_gamut_cusp(float hue) {
    int index = int(hue) + 1;
    int lower = max(0, index);
    int upper = min(361, index + 2);
    while (lower + 1 < upper) {
        if (hue > aces2_gamut_cusp_table[index].w)
            lower = index;
        else
            upper = index;
        index = (lower + upper) / 2;
    }
    float4 lower_value = aces2_gamut_cusp_table[upper - 1];
    float4 upper_value = aces2_gamut_cusp_table[upper];
    return lerp(lower_value.xyz, upper_value.xyz, (hue - lower_value.w) / (upper_value.w - lower_value.w));
}

float3 aces2_rgb_to_jmh(float3 rgb) {
    float3 lms = float3(
        dot(float3(0.445181042f, 0.349649280f, -0.00112973212f), rgb),
        dot(float3(0.123734146f, 0.613643706f, 0.0563228019f), rgb),
        dot(float3(0.0117007261f, 0.0280607939f, 0.753939033f), rgb));
    float3 nonlinear = pow(abs(lms), 0.42f);
    float3 response = sign(lms) * nonlinear / (27.1299992f + nonlinear);
    float3 aab = float3(
        dot(float3(20.2588100f, 10.1294050f, 0.506470263f), response),
        dot(float3(15480.0000f, -16887.2734f, 1407.27271f), response),
        dot(float3(1720.00000f, 1720.00000f, -3440.00000f), response));
    if (aab.x <= 0.0f) return float3(0.0f);

    float J = 100.0f * pow(aab.x, 1.13705599f);
    float M = length(aab.yz);
    float hue = aab.y == 0.0f ? 0.0f : atan2(aab.z, aab.y) * 57.29577951308238f;
    return float3(J, M, aces2_wrap_hue(hue));
}

float aces2_tonescale(float J) {
    float A = 0.0323680267f * pow(abs(J) * 0.01f, 0.879464149f);
    float Y = pow(27.1299992f * A / (1.0f - A), 2.3809523809523809f);
    float f = 1.04710376f * pow(Y / (Y + 0.73009213709383403f), 1.14999998f);
    float Y_ts = max(0.0f, f * f / (f + 0.0399999991f));
    float F_L_Y = pow(0.79370057210326195f * Y_ts, 0.42f);
    float J_ts = 100.0f * pow(F_L_Y / (27.1299992f + F_L_Y) * 30.8946857f, 1.13705599f);
    return sign(J) * J_ts;
}

float aces2_toe(float value, float limit, float first_input, float second_input) {
    float second = max(second_input, 0.001f);
    float first = sqrt(first_input * first_input + second * second);
    float third = (limit + first) / (limit + second);
    if (value > limit) return value;
    float term = third * value - first;
    return 0.5f * (term + sqrt(term * term + 4.0f * second * third * value));
}

float aces2_chroma_norm(float cosine, float sine) {
    float cosine_2 = 2.0f * cosine * cosine - 1.0f;
    float sine_2 = 2.0f * cosine * sine;
    float cosine_3 = 4.0f * cosine * cosine * cosine - 3.0f * cosine;
    float sine_3 = 3.0f * sine - 4.0f * sine * sine * sine;
    return dot(
               float3(cosine, cosine_2, cosine_3),
               float3(11.341321604032515f, 16.469863649185896f, 7.8842182208776475f)) +
           dot(
               float3(sine, sine_2, sine_3),
               float3(14.665187919584513f, -6.3725780354404442f, 9.1941277054452897f)) +
           77.133051547393805f;
}

float3 aces2_tonescale_and_chroma_compress(float3 JMh, float J_ts, float reach_max, float cosine, float sine) {
    float compressed_M = JMh.y;
    if (JMh.y != 0.0f) {
        float normalized_J = J_ts / 100.0f;
        float inverse_normalized_J = max(0.0f, 1.0f - normalized_J);
        float normalization = aces2_chroma_norm(cosine, sine);
        float limit = pow(normalized_J, 0.879464149f) * reach_max / normalization;
        compressed_M *= pow(J_ts / JMh.x, 0.879464149f) / normalization;
        compressed_M = limit - aces2_toe(
            limit - compressed_M,
            limit - 0.001f,
            inverse_normalized_J * 1.29999995f,
            sqrt(normalized_J * normalized_J + 0.00499999989f));
        compressed_M = aces2_toe(
            compressed_M,
            limit,
            normalized_J * 2.4000001f,
            inverse_normalized_J);
        compressed_M *= normalization;
    }
    return float3(J_ts, compressed_M, JMh.z);
}

float aces2_focus_gain(float J, float cusp_J) {
    float threshold = lerp(cusp_J, 100.0f, 0.3f);
    if (J <= threshold) return 1.0f;
    float gain = (100.0f - threshold) / max(0.0001f, 100.0f - J);
    gain = log(gain) / log(10.0f);
    return gain * gain + 1.0f;
}

float aces2_solve_J_intersection(float J, float M, float focus_J, float slope_gain) {
    float scaled_M = M / slope_gain;
    float a = scaled_M / focus_J;
    if (J < focus_J) {
        float b = 1.0f - scaled_M;
        float c = -J;
        return -2.0f * c / (b + sqrt(b * b - 4.0f * a * c));
    }
    float b = -(1.0f + scaled_M + 100.0f * a);
    float c = 100.0f * scaled_M + J;
    return -2.0f * c / (b - sqrt(b * b - 4.0f * a * c));
}

float aces2_gamut_boundary(
    float2 cusp,
    float upper_gamma_inverse,
    float lower_gamma_inverse,
    float source_intersection,
    float cusp_intersection,
    float slope) {
    float lower = cusp_intersection *
                  pow(source_intersection / cusp_intersection, lower_gamma_inverse) /
                  (cusp.x / cusp.y - slope);
    float upper = cusp.y * (100.0f - cusp_intersection) *
                  pow((100.0f - source_intersection) / (100.0f - cusp_intersection), upper_gamma_inverse) /
                  (slope * cusp.y + 100.0f - cusp.x);
    float smoothing = 0.12f * cusp.y;
    float blend = max(smoothing - abs(lower - upper), 0.0f) / smoothing;
    return min(lower, upper) - blend * blend * blend * smoothing / 6.0f;
}

float aces2_remap_M(float M, float gamut_boundary, float reach_boundary) {
    float boundary_ratio = gamut_boundary / reach_boundary;
    float proportion = max(boundary_ratio, 0.75f);
    float threshold = proportion * gamut_boundary;
    if (proportion >= 1.0f || M <= threshold) return M;

    float offset = M - threshold;
    float gamut_offset = gamut_boundary - threshold;
    float reach_offset = reach_boundary - threshold;
    float scale = reach_offset / (reach_offset / gamut_offset - 1.0f);
    float normalized_distance = offset / scale;
    return threshold + scale * normalized_distance / (1.0f + normalized_distance);
}

float3 aces2_gamut_compress(float3 JMh, float3 gamut_cusp, float reach_max) {
    if (JMh.y <= 0.0f || JMh.x > 100.0f) return float3(JMh.x, 0.0f, JMh.z);

    float2 cusp = gamut_cusp.xy;
    float focus_J = lerp(cusp.x, 34.096539f, min(1.0f, 1.3f - cusp.x / 100.0f));
    float slope_gain = 135.0f * aces2_focus_gain(JMh.x, cusp.x);
    float source_intersection = aces2_solve_J_intersection(JMh.x, JMh.y, focus_J, slope_gain);
    float gamut_slope = source_intersection < focus_J ? source_intersection : 100.0f - source_intersection;
    gamut_slope *= (source_intersection - focus_J) / (focus_J * slope_gain);

    float cusp_intersection = aces2_solve_J_intersection(cusp.x, cusp.y, focus_J, slope_gain);
    float gamut_boundary = aces2_gamut_boundary(
        cusp,
        gamut_cusp.z,
        0.877192974f,
        source_intersection,
        cusp_intersection,
        gamut_slope);
    if (gamut_boundary <= 0.0f) return float3(JMh.x, 0.0f, JMh.z);

    float reach_boundary = 100.0f * pow(source_intersection / 100.0f, 0.879464149f);
    reach_boundary /= 100.0f / reach_max - gamut_slope;
    float remapped_M = aces2_remap_M(JMh.y, gamut_boundary, reach_boundary);
    return float3(source_intersection + remapped_M * gamut_slope, remapped_M, JMh.z);
}

float3 aces2_jmh_to_rec709(float3 JMh) {
    float radians = JMh.z * 0.0174532924f;
    float3 aab = float3(
        pow(JMh.x * 0.01f, 0.879464149f),
        JMh.y * cos(radians),
        JMh.y * sin(radians));
    float3 response = float3(
        dot(float3(0.0323680267f, 2.07657631e-05f, 1.32606210e-05f), aab),
        dot(float3(0.0323680267f, -4.10250432e-05f, -1.20174373e-05f), aab),
        dot(float3(0.0323680267f, -1.01296409e-05f, -0.000290076074f), aab));
    float3 limited = min(abs(response), 0.99f);
    float3 lms = sign(response) * pow(27.1299992f * limited / (1.0f - limited), 2.38095236f);
    return float3(
        dot(float3(7.45048571f, -6.13018370f, -0.0603808537f), lms),
        dot(float3(-1.47506750f, 3.11835742f, -0.383369029f), lms),
        dot(float3(0.0106288502f, -0.318572670f, 1.56786489f), lms));
}

public float3 aces2_output_transform_rec709_100_nit(float3 aces2065_1) {
    float3 JMh = aces2_rgb_to_jmh(aces2065_1);
    float radians = JMh.z * 0.0174532924f;
    float reach_max = aces2_reach(JMh.z);
    JMh = aces2_tonescale_and_chroma_compress(
        JMh,
        aces2_tonescale(JMh.x),
        reach_max,
        cos(radians),
        sin(radians));
    JMh = aces2_gamut_compress(JMh, aces2_gamut_cusp(JMh.z), reach_max);
    return aces2_jmh_to_rec709(JMh);
}

public float3 aces2_display_linear_from_acescg(float3 acescg) {
    return saturate(aces2_output_transform_rec709_100_nit(acescg_to_aces2065_1(acescg)));
}

