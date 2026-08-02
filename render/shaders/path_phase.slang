module path_phase;

static const float phase_pi =
    3.14159265358979323846;

public float henyey_greenstein(
    float3 outgoing,
    float3 incident,
    float anisotropy) {
    const float clamped =
        clamp(
            anisotropy,
            -0.99,
            0.99);
    const float denominator =
        1.0 +
        clamped *
            clamped +
        2.0 *
            clamped *
            dot(
                outgoing,
                incident);
    return
        (
            1.0 -
            clamped *
                clamped
        ) /
        (
            4.0 *
            phase_pi *
            denominator *
            sqrt(
                max(
                    0.0,
                    denominator))
        );
}

float3 phase_frame_x(float3 z) {
    const float sign =
        z.z >= 0.0
            ? 1.0
            : -1.0;
    const float reciprocal =
        -1.0 /
        (
            sign +
            z.z
        );
    return float3(
        1.0 +
            sign *
                z.x *
                z.x *
                reciprocal,
        sign *
            z.x *
            z.y *
            reciprocal,
        -sign *
            z.x);
}

public float3 sample_henyey_greenstein(
    float3 outgoing,
    float anisotropy,
    float2 sample,
    out float pdf) {
    const float clamped =
        clamp(
            anisotropy,
            -0.99,
            0.99);
    float cosine;
    if (abs(clamped) < 0.001)
        cosine =
            1.0 -
            2.0 *
                sample.x;
    else {
        const float ratio =
            (
                1.0 -
                clamped *
                    clamped
            ) /
            (
                1.0 +
                clamped -
                2.0 *
                    clamped *
                    sample.x
            );
        cosine =
            -(
                1.0 +
                clamped *
                    clamped -
                ratio *
                    ratio
            ) /
            (
                2.0 *
                clamped
            );
    }
    const float sine =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine *
                        cosine));
    const float angle =
        2.0 *
        phase_pi *
        sample.y;
    const float3 frame_x =
        phase_frame_x(
            outgoing);
    const float3 frame_y =
        cross(
            outgoing,
            frame_x);
    const float3 incident =
        sine *
            cos(angle) *
            frame_x +
        sine *
            sin(angle) *
            frame_y +
        cosine *
            outgoing;
    pdf =
        henyey_greenstein(
            outgoing,
            incident,
            clamped);
    return incident;
}
