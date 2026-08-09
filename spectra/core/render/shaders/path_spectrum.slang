module path_spectrum;

import path_scene;

public static const uint sampled_spectrum_size = 4;
public static const float minimum_wavelength = 360.0;
public static const float maximum_wavelength = 830.0;
public static const float cie_y_integral = 106.856895;

public float visible_wavelength_pdf(float wavelength) {
    if (wavelength < minimum_wavelength ||
        wavelength > maximum_wavelength)
        return 0.0;
    const float value =
        cosh(
            0.0072 *
            (wavelength - 538.0));
    return 0.0039398042 /
        (value * value);
}

public float sample_visible_wavelength(float sample) {
    return clamp(
        538.0 -
            138.888889 *
                atanh(
                    0.85691062 -
                    1.82750197 * sample),
        minimum_wavelength,
        maximum_wavelength);
}

public float4 sample_visible_wavelengths(
    float sample) {
    float4 result;
    [ForceUnroll]
    for (uint index = 0;
         index < sampled_spectrum_size;
         ++index) {
        float offset_sample =
            sample +
            float(index) /
                float(sampled_spectrum_size);
        if (offset_sample > 1.0)
            offset_sample -= 1.0;
        result[index] =
            sample_visible_wavelength(
                offset_sample);
    }
    return result;
}

public float4 wavelength_probabilities(
    float4 wavelengths) {
    return float4(
        visible_wavelength_pdf(
            wavelengths.x),
        visible_wavelength_pdf(
            wavelengths.y),
        visible_wavelength_pdf(
            wavelengths.z),
        visible_wavelength_pdf(
            wavelengths.w));
}

public float blackbody_radiance(
    float wavelength,
    float temperature) {
    if (temperature <= 0.0) return 0.0;
    static const float speed_of_light =
        299792458.0;
    static const float planck_constant =
        6.62606957e-34;
    static const float boltzmann_constant =
        1.3806488e-23;
    const float wavelength_meters =
        wavelength * 1.0e-9;
    return
        (
            2.0 *
            planck_constant *
            speed_of_light *
            speed_of_light
        ) /
        (
            pow(
                wavelength_meters,
                5.0) *
            (
                exp(
                    (
                        planck_constant *
                        speed_of_light
                    ) /
                    (
                        wavelength_meters *
                        boltzmann_constant *
                        temperature
                    )) -
                1.0
            )
        );
}

float sigmoid(float value) {
    if (isinf(value))
        return value > 0.0
            ? 1.0
            : 0.0;
    return 0.5 +
        value /
            (
                2.0 *
                sqrt(
                    1.0 +
                    value * value)
            );
}

float evaluate_polynomial(
    float4 parameters,
    float wavelength) {
    return sigmoid(
        (parameters.x * wavelength +
         parameters.y) *
            wavelength +
        parameters.z);
}

float spectral_table_value(
    ByteAddressBuffer table,
    uint curve,
    float wavelength) {
    const float position =
        clamp(
            wavelength,
            minimum_wavelength,
            maximum_wavelength) -
        minimum_wavelength;
    const uint lower =
        min(
            uint(position),
            469);
    const float offset =
        position -
        float(lower);
    const uint first_byte =
        16 +
        (curve * 471 + lower) *
            sizeof(float);
    const float first =
        table.Load<float>(
            first_byte);
    const float second =
        table.Load<float>(
            first_byte +
            sizeof(float));
    return lerp(
        first,
        second,
        offset);
}

float evaluate_piecewise_spectrum(
    StructuredBuffer<float2> samples,
    uint offset,
    uint count,
    float wavelength) {
    const float2 first = samples[offset];
    const float2 last =
        samples[offset + count - 1];
    if (wavelength < first.x ||
        wavelength > last.x)
        return 0.0;
    uint upper = 1;
    while (
        upper + 1 < count &&
        samples[offset + upper].x <
            wavelength)
        ++upper;
    const float2 begin =
        samples[offset + upper - 1];
    const float2 end =
        samples[offset + upper];
    return lerp(
        begin.y,
        end.y,
        (wavelength - begin.x) /
            (end.x - begin.x));
}

float evaluate_spectrum_component(
    PathSpectrum spectrum,
    StructuredBuffer<float2> piecewise_samples,
    ByteAddressBuffer cie_spectra,
    float wavelength) {
    float value = 0.0;
    if (spectrum.metadata.x == 0) {
        value =
            spectrum.parameters.w *
            evaluate_polynomial(
                spectrum.parameters,
                wavelength);
        if (spectrum.metadata.y != 0)
            value *=
                spectral_table_value(
                    cie_spectra,
                    spectrum.metadata.y == 1
                        ? 3
                        : 4,
                    wavelength);
    }
    else if (spectrum.metadata.x == 1)
        value = spectrum.parameters.x;
    else if (spectrum.metadata.x == 2)
        value =
            blackbody_radiance(
                wavelength,
                spectrum.parameters.x) *
            spectrum.parameters.y;
    else
        value =
            evaluate_piecewise_spectrum(
                piecewise_samples,
                spectrum.metadata.z,
                spectrum.metadata.w,
                wavelength);
    return value;
}

public float4 evaluate_spectrum(
    PathSpectrum spectrum,
    StructuredBuffer<float2> piecewise_samples,
    ByteAddressBuffer cie_spectra,
    float4 wavelengths) {
    return float4(
        evaluate_spectrum_component(
            spectrum,
            piecewise_samples,
            cie_spectra,
            wavelengths.x),
        evaluate_spectrum_component(
            spectrum,
            piecewise_samples,
            cie_spectra,
            wavelengths.y),
        evaluate_spectrum_component(
            spectrum,
            piecewise_samples,
            cie_spectra,
            wavelengths.z),
        evaluate_spectrum_component(
            spectrum,
            piecewise_samples,
            cie_spectra,
            wavelengths.w));
}

public float4 evaluate_rgb_polynomial(
    float3 coefficients,
    float scale,
    uint spectrum_type,
    float illuminant_curve,
    ByteAddressBuffer cie_spectra,
    float4 wavelengths) {
    float4 result = scale * float4(
        evaluate_polynomial(
            float4(
                coefficients,
                0.0),
            wavelengths.x),
        evaluate_polynomial(
            float4(
                coefficients,
                0.0),
            wavelengths.y),
        evaluate_polynomial(
            float4(
                coefficients,
                0.0),
            wavelengths.z),
        evaluate_polynomial(
            float4(
                coefficients,
                0.0),
            wavelengths.w));
    if (spectrum_type == 2)
        result *= float4(
            spectral_table_value(
                cie_spectra,
                uint(illuminant_curve),
                wavelengths.x),
            spectral_table_value(
                cie_spectra,
                uint(illuminant_curve),
                wavelengths.y),
            spectral_table_value(
                cie_spectra,
                uint(illuminant_curve),
                wavelengths.z),
            spectral_table_value(
                cie_spectra,
                uint(illuminant_curve),
                wavelengths.w));
    return result;
}

float3 rgb_to_spectrum_polynomial(
    ByteAddressBuffer tables,
    uint color_space,
    float3 rgb) {
    if (rgb.x == rgb.y &&
        rgb.y == rgb.z)
        return float3(
            0.0,
            0.0,
            (rgb.x - 0.5) /
                sqrt(
                    rgb.x *
                    (1.0 - rgb.x)));
    static const uint resolution = 64;
    static const uint table_size = 9437456;
    static const uint scale_offset = 16;
    static const uint coefficient_offset =
        scale_offset +
        resolution * sizeof(float);
    const uint table_offset =
        color_space * table_size;
    const uint maximum_component =
        rgb.x > rgb.y
            ? (
                  rgb.x > rgb.z
                      ? 0
                      : 2
              )
            : (
                  rgb.y > rgb.z
                      ? 1
                      : 2
              );
    const float z = rgb[maximum_component];
    const float x =
        rgb[(maximum_component + 1) % 3] *
        float(resolution - 1) /
        z;
    const float y =
        rgb[(maximum_component + 2) % 3] *
        float(resolution - 1) /
        z;
    const uint x_index =
        min(
            uint(x),
            resolution - 2);
    const uint y_index =
        min(
            uint(y),
            resolution - 2);
    uint upper = 1;
    while (
        upper + 1 < resolution &&
        tables.Load<float>(
            table_offset +
            scale_offset +
            upper * sizeof(float)) <
            z)
        ++upper;
    const uint z_index = upper - 1;
    const float z_first =
        tables.Load<float>(
            table_offset +
            scale_offset +
            z_index * sizeof(float));
    const float z_second =
        tables.Load<float>(
            table_offset +
            scale_offset +
            (z_index + 1) * sizeof(float));
    const float x_offset =
        x - float(x_index);
    const float y_offset =
        y - float(y_index);
    const float z_offset =
        (z - z_first) /
        (z_second - z_first);
    float3 result;
    [ForceUnroll]
    for (uint coefficient = 0;
         coefficient != 3;
         ++coefficient) {
        float values[8];
        [ForceUnroll]
        for (uint z_delta = 0;
             z_delta != 2;
             ++z_delta)
            [ForceUnroll]
            for (uint y_delta = 0;
                 y_delta != 2;
                 ++y_delta)
                [ForceUnroll]
                for (uint x_delta = 0;
                     x_delta != 2;
                     ++x_delta) {
                    const uint value_index =
                        (
                            (
                                (
                                    (
                                        maximum_component *
                                            resolution +
                                        z_index +
                                        z_delta
                                    ) *
                                        resolution +
                                    y_index +
                                    y_delta
                                ) *
                                    resolution +
                                x_index +
                                x_delta
                            ) *
                                3 +
                            coefficient
                        );
                    values[
                        z_delta * 4 +
                        y_delta * 2 +
                        x_delta] =
                            tables.Load<float>(
                                table_offset +
                                coefficient_offset +
                                value_index *
                                    sizeof(float));
                }
        result[coefficient] =
            lerp(
                lerp(
                    lerp(
                        values[0],
                        values[1],
                        x_offset),
                    lerp(
                        values[2],
                        values[3],
                        x_offset),
                    y_offset),
                lerp(
                    lerp(
                        values[4],
                        values[5],
                        x_offset),
                    lerp(
                        values[6],
                        values[7],
                        x_offset),
                    y_offset),
                z_offset);
    }
    return result;
}

public float4 evaluate_rgb_spectrum(
    float3 rgb,
    uint spectrum_type,
    uint color_space,
    ByteAddressBuffer rgb_to_spectrum_tables,
    ByteAddressBuffer cie_spectra,
    float4 wavelengths) {
    rgb = max(
        rgb,
        float3(0.0));
    float scale = 1.0;
    if (spectrum_type == 0)
        rgb = min(
            rgb,
            float3(1.0));
    else {
        scale =
            2.0 *
            max(
                rgb.x,
                max(
                    rgb.y,
                    rgb.z));
        if (scale != 0.0)
            rgb /= scale;
    }
    return evaluate_rgb_polynomial(
        rgb_to_spectrum_polynomial(
            rgb_to_spectrum_tables,
            color_space,
            rgb),
        scale,
        spectrum_type,
        color_space == 2
            ? 4.0
            : 3.0,
        cie_spectra,
        wavelengths);
}

float film_sensor_value(
    StructuredBuffer<float> response,
    uint channel,
    float wavelength) {
    const int wavelength_index =
        int(
            floor(
                wavelength +
                0.5)) -
        int(minimum_wavelength);
    if (
        wavelength_index < 0 ||
        wavelength_index >= 471)
        return 0.0;
    return response[
        channel * 471 +
        uint(wavelength_index)];
}

public float3 spectrum_to_film_sensor(
    float4 spectrum,
    float4 wavelengths,
    float4 probabilities,
    StructuredBuffer<float> response,
    float imaging_ratio,
    float maximum_component_value) {
    float3 sensor_rgb = float3(0.0);
    [ForceUnroll]
    for (uint wavelength_index = 0;
         wavelength_index < sampled_spectrum_size;
         ++wavelength_index) {
        const float weighted =
            probabilities[wavelength_index] != 0.0
                ? spectrum[wavelength_index] /
                      probabilities[wavelength_index] /
                      float(sampled_spectrum_size)
                : 0.0;
        [ForceUnroll]
        for (uint channel = 0;
             channel != 3;
             ++channel)
            sensor_rgb[channel] +=
                film_sensor_value(
                    response,
                    channel,
                    wavelengths[
                        wavelength_index]) *
                weighted;
    }
    sensor_rgb *= imaging_ratio;
    const float maximum =
        max(
            sensor_rgb.x,
            max(
                sensor_rgb.y,
                sensor_rgb.z));
    if (maximum > maximum_component_value)
        sensor_rgb *=
            maximum_component_value /
            maximum;
    return sensor_rgb;
}

public float3 spectrum_to_output_albedo(
    float4 spectrum,
    float4 wavelengths,
    float4 probabilities,
    ByteAddressBuffer cie_spectra,
    uint color_space) {
    float3 xyz = float3(0.0);
    const uint illuminant_curve =
        color_space == 2
            ? 4
            : 3;
    [ForceUnroll]
    for (uint wavelength_index = 0;
         wavelength_index <
            sampled_spectrum_size;
         ++wavelength_index) {
        const float weighted =
            probabilities[
                        wavelength_index] !=
                    0.0
                ? spectrum[
                      wavelength_index] *
                      spectral_table_value(
                          cie_spectra,
                          illuminant_curve,
                          wavelengths[
                              wavelength_index]) /
                      probabilities[
                          wavelength_index] /
                      float(
                          sampled_spectrum_size) /
                      cie_y_integral
                : 0.0;
        xyz.x +=
            spectral_table_value(
                cie_spectra,
                0,
                wavelengths[
                    wavelength_index]) *
            weighted;
        xyz.y +=
            spectral_table_value(
                cie_spectra,
                1,
                wavelengths[
                    wavelength_index]) *
            weighted;
        xyz.z +=
            spectral_table_value(
                cie_spectra,
                2,
                wavelengths[
                    wavelength_index]) *
            weighted;
    }
    if (color_space == 1)
        return float3(
            1.716651188 * xyz.x -
                0.355670784 * xyz.y -
                0.253366281 * xyz.z,
            -0.666684352 * xyz.x +
                1.616481237 * xyz.y +
                0.015768546 * xyz.z,
            0.017639857 * xyz.x -
                0.042770613 * xyz.y +
                0.942103121 * xyz.z);
    if (color_space == 2)
        return float3(
            1.049811018 * xyz.x -
                0.000097485 * xyz.z,
            -0.495903023 * xyz.x +
                1.373313046 * xyz.y +
                0.098240036 * xyz.z,
            0.991252018 * xyz.z);
    return float3(
        3.240479 * xyz.x -
            1.537150 * xyz.y -
            0.498535 * xyz.z,
        -0.969256 * xyz.x +
            1.875991 * xyz.y +
            0.041556 * xyz.z,
        0.055648 * xyz.x -
            0.204043 * xyz.y +
            1.057311 * xyz.z);
}
