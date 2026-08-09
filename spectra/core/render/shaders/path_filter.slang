module path_filter;

import path_queue;

public struct FilterSample {
    public float2 offset;
    public float weight;
}

uint sample_filter_interval(
    StructuredBuffer<float> distribution,
    uint offset,
    uint count,
    float sample,
    out float remainder) {
    uint first = 0;
    uint length = count;
    while (length > 0) {
        const uint half = length >> 1;
        const uint middle = first + half;
        if (
            distribution[
                offset +
                middle +
                1] <=
            sample) {
            first = middle + 1;
            length -= half + 1;
        }
        else
            length = half;
    }
    const uint interval =
        min(first, count - 1);
    const float minimum =
        distribution[
            offset +
            interval];
    const float maximum =
        distribution[
            offset +
            interval +
            1];
    remainder =
        maximum > minimum
            ? (
                  sample -
                  minimum
              ) /
                  (
                      maximum -
                      minimum
                  )
            : 0.0;
    return interval;
}

float sample_tent(
    float sample,
    float radius) {
    return sample < 0.5
        ? radius *
              (
                  sqrt(
                      2.0 *
                      sample) -
                  1.0
              )
        : radius *
              (
                  1.0 -
                  sqrt(
                      2.0 -
                      2.0 *
                      sample)
              );
}

public FilterSample sample_filter(
    float2 sample,
    WavefrontParameters parameters) {
    FilterSample result;
    const float2 radius =
        parameters.filter_parameters_0.xy;
    if (parameters.filter_metadata.x == 0) {
        result.offset =
            lerp(
                -radius,
                radius,
                sample);
        result.weight = 1.0;
        return result;
    }
    if (parameters.filter_metadata.x == 4) {
        result.offset =
            float2(
                sample_tent(
                    sample.x,
                    radius.x),
                sample_tent(
                    sample.y,
                    radius.y));
        result.weight = 1.0;
        return result;
    }
    StructuredBuffer<float> distribution =
        parameters.filter_distribution;
    const uint x_resolution =
        parameters.filter_metadata.y;
    const uint y_resolution =
        parameters.filter_metadata.z;
    const uint value_count =
        x_resolution *
        y_resolution;
    const uint conditional_offset =
        value_count;
    const uint marginal_offset =
        conditional_offset +
        y_resolution *
            (
                x_resolution +
                1
            );
    float y_remainder;
    const uint y =
        sample_filter_interval(
            distribution,
            marginal_offset,
            y_resolution,
            sample.y,
            y_remainder);
    float x_remainder;
    const uint x =
        sample_filter_interval(
            distribution,
            conditional_offset +
                y *
                    (
                        x_resolution +
                        1
                    ),
            x_resolution,
            sample.x,
            x_remainder);
    result.offset =
        lerp(
            -radius,
            radius,
            (
                float2(
                    x,
                    y) +
                float2(
                    x_remainder,
                    y_remainder)
            ) /
                float2(
                    x_resolution,
                    y_resolution));
    const float value =
        distribution[
            y *
                x_resolution +
            x];
    result.weight =
        value == 0.0
            ? 0.0
            : (
                  value > 0.0
                      ? 1.0
                      : -1.0
              ) *
                  parameters
                      .filter_parameters_1
                      .w;
    return result;
}
