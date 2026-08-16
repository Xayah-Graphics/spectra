module color;

#include "shader_semantics.h"

public float3 color_to_linear_srgb(float3 value, uint color_space) {
    if (color_space == SPECTRA_COLOR_SPACE_REC2020)
        return float3(
            dot(float3(1.660491, -0.587641, -0.072850), value),
            dot(float3(-0.124550, 1.132900, -0.008349), value),
            dot(float3(-0.018151, -0.100579, 1.118730), value));
    if (color_space == SPECTRA_COLOR_SPACE_ACES2065_1)
        return float3(
            dot(float3(2.521686, -1.134130, -0.387556), value),
            dot(float3(-0.276479, 1.372719, -0.096240), value),
            dot(float3(-0.015378, -0.152975, 1.168353), value));
    return value;
}

public float3 color_from_linear_srgb(float3 value, uint color_space) {
    if (color_space == SPECTRA_COLOR_SPACE_REC2020)
        return float3(
            dot(float3(0.627404, 0.329283, 0.043313), value),
            dot(float3(0.069097, 0.919540, 0.011362), value),
            dot(float3(0.016391, 0.088013, 0.895595), value));
    if (color_space == SPECTRA_COLOR_SPACE_ACES2065_1)
        return float3(
            dot(float3(0.439701, 0.382978, 0.177335), value),
            dot(float3(0.089792, 0.813423, 0.096762), value),
            dot(float3(0.017544, 0.111544, 0.870704), value));
    return value;
}

public float srgb_eotf(float value) {
    return value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4);
}

public float3 srgb_eotf(float3 value) {
    return float3(srgb_eotf(value.x), srgb_eotf(value.y), srgb_eotf(value.z));
}
