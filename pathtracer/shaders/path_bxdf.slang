module path_bxdf;

public static const float path_pi = 3.14159265358979323846;
public static const float path_inverse_pi = 0.31830988618379067154;

public static const uint bxdf_reflection = 1;
public static const uint bxdf_transmission = 2;
public static const uint bxdf_diffuse = 4;
public static const uint bxdf_glossy = 8;
public static const uint bxdf_specular = 16;

public static const uint transport_radiance = 0;
public static const uint transport_importance = 1;
public static const uint bxdf_reflection_transmission =
    bxdf_reflection |
    bxdf_transmission;

public struct BxdfSample {
    public float4 f;
    public float3 wi;
    public float pdf;
    public float eta;
    public uint flags;
    public bool valid;
    public bool terminate_secondary;
    public bool pdf_is_proportional;
}

public struct BxdfEvaluation {
    public float4 f;
    public float pdf;
}

public struct TrowbridgeReitzDistribution {
    public float alpha_x;
    public float alpha_y;
}

float square(float value) {
    return value * value;
}

float4 square(float4 value) {
    return value * value;
}

float max_component(float4 value) {
    return max(
        value.x,
        max(
            value.y,
            max(
                value.z,
                value.w)));
}

bool same_hemisphere(
    float3 first,
    float3 second) {
    return first.z * second.z > 0.0;
}

public float absolute_cosine(float3 direction) {
    return abs(direction.z);
}

float3 reflect_direction(
    float3 outgoing,
    float3 normal) {
    return -outgoing +
        2.0 *
            dot(outgoing, normal) *
            normal;
}

bool refract_direction(
    float3 incident,
    float3 normal,
    float eta,
    out float relative_eta,
    out float3 transmitted) {
    float cosine_incident =
        dot(normal, incident);
    if (cosine_incident < 0.0) {
        eta = 1.0 / eta;
        cosine_incident =
            -cosine_incident;
        normal = -normal;
    }
    const float sine_squared_incident =
        max(
            0.0,
            1.0 -
                cosine_incident *
                    cosine_incident);
    const float sine_squared_transmitted =
        sine_squared_incident /
        (eta * eta);
    if (sine_squared_transmitted >= 1.0) {
        relative_eta = 1.0;
        transmitted = float3(0.0);
        return false;
    }
    const float cosine_transmitted =
        sqrt(
            1.0 -
            sine_squared_transmitted);
    transmitted =
        -incident / eta +
        (
            cosine_incident / eta -
            cosine_transmitted
        ) * normal;
    relative_eta = eta;
    return true;
}

float fresnel_dielectric(
    float cosine_incident,
    float eta) {
    cosine_incident =
        clamp(
            cosine_incident,
            -1.0,
            1.0);
    if (cosine_incident < 0.0) {
        eta = 1.0 / eta;
        cosine_incident =
            -cosine_incident;
    }
    const float sine_squared_incident =
        1.0 -
        cosine_incident *
            cosine_incident;
    const float sine_squared_transmitted =
        sine_squared_incident /
        (eta * eta);
    if (sine_squared_transmitted >= 1.0)
        return 1.0;
    const float cosine_transmitted =
        sqrt(
            max(
                0.0,
                1.0 -
                    sine_squared_transmitted));
    const float parallel =
        (
            eta * cosine_incident -
            cosine_transmitted
        ) /
        (
            eta * cosine_incident +
            cosine_transmitted
        );
    const float perpendicular =
        (
            cosine_incident -
            eta * cosine_transmitted
        ) /
        (
            cosine_incident +
            eta * cosine_transmitted
        );
    return 0.5 *
        (
            parallel * parallel +
            perpendicular * perpendicular
        );
}

float4 fresnel_conductor(
    float cosine_incident,
    float4 eta,
    float4 k) {
    cosine_incident =
        clamp(
            cosine_incident,
            0.0,
            1.0);
    const float cosine_squared =
        cosine_incident *
        cosine_incident;
    const float sine_squared =
        1.0 - cosine_squared;
    const float4 eta_squared =
        eta * eta;
    const float4 k_squared =
        k * k;
    const float4 first =
        eta_squared -
        k_squared -
        sine_squared;
    const float4 a_squared_plus_b_squared =
        sqrt(
            max(
                first * first +
                    4.0 *
                        eta_squared *
                        k_squared,
                float4(0.0)));
    const float4 a =
        sqrt(
            max(
                0.5 *
                    (
                        a_squared_plus_b_squared +
                        first
                    ),
                float4(0.0)));
    const float4 second =
        2.0 *
        cosine_incident *
        a;
    const float4 perpendicular =
        (
            a_squared_plus_b_squared +
            cosine_squared -
            second
        ) /
        (
            a_squared_plus_b_squared +
            cosine_squared +
            second
        );
    const float4 third =
        cosine_squared *
            a_squared_plus_b_squared +
        sine_squared *
            sine_squared;
    const float4 fourth =
        second *
        sine_squared;
    const float4 parallel =
        perpendicular *
        (
            third - fourth
        ) /
        (
            third + fourth
        );
    return 0.5 *
        (
            parallel +
            perpendicular
        );
}

float2 sample_uniform_disk_polar(float2 sample) {
    const float radius =
        sqrt(sample.x);
    const float angle =
        2.0 * path_pi * sample.y;
    return radius *
        float2(
            cos(angle),
            sin(angle));
}

float2 sample_uniform_disk_concentric(
    float2 sample) {
    const float2 offset =
        2.0 * sample -
        float2(1.0);
    if (
        offset.x == 0.0 &&
        offset.y == 0.0)
        return float2(0.0);
    float radius;
    float angle;
    if (abs(offset.x) > abs(offset.y)) {
        radius = offset.x;
        angle =
            0.25 * path_pi *
            offset.y / offset.x;
    }
    else {
        radius = offset.y;
        angle =
            0.5 * path_pi -
            0.25 * path_pi *
            offset.x / offset.y;
    }
    return radius *
        float2(
            cos(angle),
            sin(angle));
}

float3 sample_cosine_hemisphere_local(
    float2 sample) {
    const float2 disk =
        sample_uniform_disk_concentric(
            sample);
    return float3(
        disk,
        sqrt(
            max(
                0.0,
                1.0 -
                    dot(disk, disk))));
}

float cosine_hemisphere_pdf(float cosine) {
    return cosine *
        path_inverse_pi;
}

public TrowbridgeReitzDistribution
make_trowbridge_reitz(
    float alpha_x,
    float alpha_y) {
    TrowbridgeReitzDistribution result;
    if (max(alpha_x, alpha_y) >= 1.0e-3) {
        alpha_x =
            max(alpha_x, 1.0e-4);
        alpha_y =
            max(alpha_y, 1.0e-4);
    }
    result.alpha_x = alpha_x;
    result.alpha_y = alpha_y;
    return result;
}

bool effectively_smooth(
    TrowbridgeReitzDistribution distribution) {
    return max(
        distribution.alpha_x,
        distribution.alpha_y) <
        1.0e-3;
}

public float roughness_to_alpha(float roughness) {
    return sqrt(roughness);
}

public TrowbridgeReitzDistribution
regularize_distribution(
    TrowbridgeReitzDistribution distribution) {
    if (
        distribution.alpha_x <
            0.3 ||
        distribution.alpha_y <
            0.3) {
        distribution.alpha_x =
            clamp(
                2.0 *
                    distribution.alpha_x,
                0.1,
                0.3);
        distribution.alpha_y =
            clamp(
                2.0 *
                    distribution.alpha_y,
                0.1,
                0.3);
    }
    return distribution;
}

float microfacet_lambda(
    TrowbridgeReitzDistribution distribution,
    float3 direction) {
    const float cosine_squared =
        direction.z *
        direction.z;
    const float tangent_squared =
        cosine_squared > 0.0
            ? max(
                0.0,
                1.0 - cosine_squared) /
                cosine_squared
            : 3.402823466e+38;
    if (isinf(tangent_squared))
        return 0.0;
    const float xy_squared =
        direction.x * direction.x +
        direction.y * direction.y;
    const float cosine_phi_squared =
        xy_squared > 0.0
            ? direction.x *
                direction.x /
                xy_squared
            : 1.0;
    const float sine_phi_squared =
        xy_squared > 0.0
            ? direction.y *
                direction.y /
                xy_squared
            : 0.0;
    const float alpha_squared =
        cosine_phi_squared *
            distribution.alpha_x *
            distribution.alpha_x +
        sine_phi_squared *
            distribution.alpha_y *
            distribution.alpha_y;
    return 0.5 *
        (
            sqrt(
                1.0 +
                alpha_squared *
                    tangent_squared) -
            1.0
        );
}

float microfacet_g1(
    TrowbridgeReitzDistribution distribution,
    float3 direction) {
    return 1.0 /
        (
            1.0 +
            microfacet_lambda(
                distribution,
                direction)
        );
}

float microfacet_g(
    TrowbridgeReitzDistribution distribution,
    float3 outgoing,
    float3 incident) {
    return 1.0 /
        (
            1.0 +
            microfacet_lambda(
                distribution,
                outgoing) +
            microfacet_lambda(
                distribution,
                incident)
        );
}

float microfacet_d(
    TrowbridgeReitzDistribution distribution,
    float3 microfacet_normal) {
    const float cosine_squared =
        microfacet_normal.z *
        microfacet_normal.z;
    if (cosine_squared == 0.0)
        return 0.0;
    const float tangent_squared =
        max(
            0.0,
            1.0 - cosine_squared) /
        cosine_squared;
    if (isinf(tangent_squared))
        return 0.0;
    const float cosine_fourth =
        cosine_squared *
        cosine_squared;
    if (cosine_fourth < 1.0e-16)
        return 0.0;
    const float xy_squared =
        microfacet_normal.x *
            microfacet_normal.x +
        microfacet_normal.y *
            microfacet_normal.y;
    const float cosine_phi =
        xy_squared > 0.0
            ? microfacet_normal.x /
                sqrt(xy_squared)
            : 1.0;
    const float sine_phi =
        xy_squared > 0.0
            ? microfacet_normal.y /
                sqrt(xy_squared)
            : 0.0;
    const float exponent =
        tangent_squared *
        (
            square(
                cosine_phi /
                distribution.alpha_x) +
            square(
                sine_phi /
                distribution.alpha_y)
        );
    return 1.0 /
        (
            path_pi *
            distribution.alpha_x *
            distribution.alpha_y *
            cosine_fourth *
            square(
                1.0 + exponent)
        );
}

float microfacet_visible_d(
    TrowbridgeReitzDistribution distribution,
    float3 direction,
    float3 microfacet_normal) {
    return microfacet_g1(
        distribution,
        direction) /
        absolute_cosine(direction) *
        microfacet_d(
            distribution,
            microfacet_normal) *
        abs(
            dot(
                direction,
                microfacet_normal));
}

float3 sample_microfacet_normal(
    TrowbridgeReitzDistribution distribution,
    float3 direction,
    float2 sample) {
    float3 hemispherical =
        normalize(
            float3(
                distribution.alpha_x *
                    direction.x,
                distribution.alpha_y *
                    direction.y,
                direction.z));
    if (hemispherical.z < 0.0)
        hemispherical =
            -hemispherical;
    const float3 tangent =
        hemispherical.z < 0.99999
            ? normalize(
                cross(
                    float3(0.0, 0.0, 1.0),
                    hemispherical))
            : float3(1.0, 0.0, 0.0);
    const float3 bitangent =
        cross(
            hemispherical,
            tangent);
    float2 point =
        sample_uniform_disk_polar(sample);
    const float height =
        sqrt(
            max(
                0.0,
                1.0 -
                    point.x *
                        point.x));
    point.y =
        lerp(
            height,
            point.y,
            0.5 *
                (
                    1.0 +
                    hemispherical.z
                ));
    const float projected_z =
        sqrt(
            max(
                0.0,
                1.0 -
                    dot(point, point)));
    const float3 normal =
        point.x * tangent +
        point.y * bitangent +
        projected_z *
            hemispherical;
    return normalize(
        float3(
            distribution.alpha_x *
                normal.x,
            distribution.alpha_y *
                normal.y,
            max(
                1.0e-6,
                normal.z)));
}

public BxdfSample invalid_bxdf_sample() {
    BxdfSample result;
    result.f = float4(0.0);
    result.wi = float3(0.0);
    result.pdf = 0.0;
    result.eta = 1.0;
    result.flags = 0;
    result.valid = false;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}

public BxdfEvaluation empty_bxdf_evaluation() {
    BxdfEvaluation result;
    result.f = float4(0.0);
    result.pdf = 0.0;
    return result;
}

[ForceInline]
public BxdfEvaluation evaluate_diffuse(
    float4 reflectance,
    float3 outgoing,
    float3 incident,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    BxdfEvaluation result =
        empty_bxdf_evaluation();
    if (!same_hemisphere(
            outgoing,
            incident))
        return result;
    result.f =
        reflectance *
        path_inverse_pi;
    if (
        (
            sample_flags &
            bxdf_reflection
        ) !=
        0)
        result.pdf =
            cosine_hemisphere_pdf(
                absolute_cosine(incident));
    return result;
}

[ForceInline]
public BxdfSample sample_diffuse(
    float4 reflectance,
    float3 outgoing,
    float2 sample,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (
        (
            sample_flags &
            bxdf_reflection
        ) ==
        0)
        return invalid_bxdf_sample();
    BxdfSample result;
    result.wi =
        sample_cosine_hemisphere_local(
            sample);
    if (outgoing.z < 0.0)
        result.wi.z =
            -result.wi.z;
    result.f =
        reflectance *
        path_inverse_pi;
    result.pdf =
        cosine_hemisphere_pdf(
            absolute_cosine(result.wi));
    result.eta = 1.0;
    result.flags =
        bxdf_reflection |
        bxdf_diffuse;
    result.valid =
        result.pdf > 0.0;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}

[ForceInline]
public BxdfEvaluation
evaluate_diffuse_transmission(
    float4 reflectance,
    float4 transmittance,
    float3 outgoing,
    float3 incident,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    BxdfEvaluation result;
    float reflection_weight =
        max_component(reflectance);
    float transmission_weight =
        max_component(transmittance);
    if (
        (
            sample_flags &
            bxdf_reflection
        ) ==
        0)
        reflection_weight = 0.0;
    if (
        (
            sample_flags &
            bxdf_transmission
        ) ==
        0)
        transmission_weight = 0.0;
    const float sum =
        reflection_weight +
        transmission_weight;
    if (sum == 0.0)
        return empty_bxdf_evaluation();
    if (same_hemisphere(
            outgoing,
            incident)) {
        result.f =
            reflectance *
            path_inverse_pi;
        result.pdf =
            reflection_weight /
            sum *
            cosine_hemisphere_pdf(
                absolute_cosine(
                    incident));
    }
    else {
        result.f =
            transmittance *
            path_inverse_pi;
        result.pdf =
            transmission_weight /
            sum *
            cosine_hemisphere_pdf(
                absolute_cosine(
                    incident));
    }
    return result;
}

[ForceInline]
public BxdfSample sample_diffuse_transmission(
    float4 reflectance,
    float4 transmittance,
    float3 outgoing,
    float component_sample,
    float2 direction_sample,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    float reflection_weight =
        max_component(reflectance);
    float transmission_weight =
        max_component(transmittance);
    if (
        (
            sample_flags &
            bxdf_reflection
        ) ==
        0)
        reflection_weight = 0.0;
    if (
        (
            sample_flags &
            bxdf_transmission
        ) ==
        0)
        transmission_weight = 0.0;
    const float sum =
        reflection_weight +
        transmission_weight;
    if (sum == 0.0)
        return invalid_bxdf_sample();
    BxdfSample result;
    result.wi =
        sample_cosine_hemisphere_local(
            direction_sample);
    if (component_sample <
        reflection_weight / sum) {
        if (outgoing.z < 0.0)
            result.wi.z =
                -result.wi.z;
        result.f =
            reflectance *
            path_inverse_pi;
        result.pdf =
            reflection_weight /
            sum *
            cosine_hemisphere_pdf(
                absolute_cosine(
                    result.wi));
        result.flags =
            bxdf_reflection |
            bxdf_diffuse;
    }
    else {
        if (outgoing.z > 0.0)
            result.wi.z =
                -result.wi.z;
        result.f =
            transmittance *
            path_inverse_pi;
        result.pdf =
            transmission_weight /
            sum *
            cosine_hemisphere_pdf(
                absolute_cosine(
                    result.wi));
        result.flags =
            bxdf_transmission |
            bxdf_diffuse;
    }
    result.eta = 1.0;
    result.valid =
        result.pdf > 0.0;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}

[ForceInline]
public BxdfEvaluation evaluate_conductor(
    TrowbridgeReitzDistribution distribution,
    float4 eta,
    float4 k,
    float3 outgoing,
    float3 incident,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (!same_hemisphere(
            outgoing,
            incident) ||
        effectively_smooth(distribution))
        return empty_bxdf_evaluation();
    const float cosine_outgoing =
        absolute_cosine(outgoing);
    const float cosine_incident =
        absolute_cosine(incident);
    float3 microfacet_normal =
        incident + outgoing;
    if (cosine_incident == 0.0 ||
        cosine_outgoing == 0.0 ||
        dot(
            microfacet_normal,
            microfacet_normal) == 0.0)
        return empty_bxdf_evaluation();
    microfacet_normal =
        normalize(microfacet_normal);
    if (microfacet_normal.z < 0.0)
        microfacet_normal =
            -microfacet_normal;
    const float4 fresnel =
        fresnel_conductor(
            abs(
                dot(
                    outgoing,
                    microfacet_normal)),
            eta,
            k);
    BxdfEvaluation result;
    result.f =
        microfacet_d(
            distribution,
            microfacet_normal) *
        fresnel *
        microfacet_g(
            distribution,
            outgoing,
            incident) /
        (
            4.0 *
            cosine_incident *
            cosine_outgoing
        );
    result.pdf =
        (
            sample_flags &
            bxdf_reflection
        ) !=
            0
        ? microfacet_visible_d(
                distribution,
                outgoing,
                microfacet_normal) /
            (
                4.0 *
                abs(
                    dot(
                        outgoing,
                        microfacet_normal))
            )
        : 0.0;
    return result;
}

[ForceInline]
public BxdfSample sample_conductor(
    TrowbridgeReitzDistribution distribution,
    float4 eta,
    float4 k,
    float3 outgoing,
    float2 sample,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (
        (
            sample_flags &
            bxdf_reflection
        ) ==
        0)
        return invalid_bxdf_sample();
    if (effectively_smooth(distribution)) {
        BxdfSample result;
        result.wi =
            float3(
                -outgoing.x,
                -outgoing.y,
                outgoing.z);
        result.f =
            fresnel_conductor(
                absolute_cosine(
                    result.wi),
                eta,
                k) /
            absolute_cosine(
                result.wi);
        result.pdf = 1.0;
        result.eta = 1.0;
        result.flags =
            bxdf_reflection |
            bxdf_specular;
        result.valid = true;
        result.terminate_secondary = false;
        result.pdf_is_proportional = false;
        return result;
    }
    if (outgoing.z == 0.0)
        return invalid_bxdf_sample();
    const float3 microfacet_normal =
        sample_microfacet_normal(
            distribution,
            outgoing,
            sample);
    const float3 incident =
        reflect_direction(
            outgoing,
            microfacet_normal);
    if (!same_hemisphere(
            outgoing,
            incident))
        return invalid_bxdf_sample();
    const float cosine_outgoing =
        absolute_cosine(outgoing);
    const float cosine_incident =
        absolute_cosine(incident);
    if (
        cosine_incident == 0.0 ||
        cosine_outgoing == 0.0)
        return invalid_bxdf_sample();
    BxdfSample result;
    result.f =
        microfacet_d(
            distribution,
            microfacet_normal) *
        fresnel_conductor(
            abs(
                dot(
                    outgoing,
                    microfacet_normal)),
            eta,
            k) *
        microfacet_g(
            distribution,
            outgoing,
            incident) /
        (
            4.0 *
            cosine_incident *
            cosine_outgoing
        );
    result.wi = incident;
    result.pdf =
        microfacet_visible_d(
            distribution,
            outgoing,
            microfacet_normal) /
        (
            4.0 *
            abs(
                dot(
                    outgoing,
                    microfacet_normal))
        );
    result.eta = 1.0;
    result.flags =
        bxdf_reflection |
        bxdf_glossy;
    result.valid =
        result.pdf > 0.0;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}

[ForceInline]
public BxdfEvaluation evaluate_dielectric(
    TrowbridgeReitzDistribution distribution,
    float eta,
    float3 outgoing,
    float3 incident,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (eta == 1.0 ||
        effectively_smooth(distribution))
        return empty_bxdf_evaluation();
    const float cosine_outgoing =
        outgoing.z;
    const float cosine_incident =
        incident.z;
    const bool reflection =
        cosine_incident *
            cosine_outgoing >
        0.0;
    const float relative_eta =
        reflection
            ? 1.0
            : (
                cosine_outgoing > 0.0
                    ? eta
                    : 1.0 / eta
            );
    float3 microfacet_normal =
        incident *
            relative_eta +
        outgoing;
    if (cosine_incident == 0.0 ||
        cosine_outgoing == 0.0 ||
        dot(
            microfacet_normal,
            microfacet_normal) == 0.0)
        return empty_bxdf_evaluation();
    microfacet_normal =
        normalize(microfacet_normal);
    if (microfacet_normal.z < 0.0)
        microfacet_normal =
            -microfacet_normal;
    if (
        dot(
            microfacet_normal,
            incident) *
            cosine_incident <
            0.0 ||
        dot(
            microfacet_normal,
            outgoing) *
            cosine_outgoing <
            0.0)
        return empty_bxdf_evaluation();
    const float fresnel =
        fresnel_dielectric(
            dot(
                outgoing,
                microfacet_normal),
            eta);
    BxdfEvaluation result;
    if (reflection) {
        result.f =
            float4(
                microfacet_d(
                    distribution,
                    microfacet_normal) *
                microfacet_g(
                    distribution,
                    outgoing,
                    incident) *
                fresnel /
                abs(
                    4.0 *
                    cosine_incident *
                    cosine_outgoing));
        float reflection_probability =
            fresnel;
        const float transmission_probability =
            (
                sample_flags &
                bxdf_transmission
            ) !=
                0
            ? 1.0 - fresnel
            : 0.0;
        if (
            (
                sample_flags &
                bxdf_reflection
            ) ==
            0)
            reflection_probability = 0.0;
        const float probability_sum =
            reflection_probability +
            transmission_probability;
        result.pdf =
            probability_sum > 0.0
            ? microfacet_visible_d(
                    distribution,
                    outgoing,
                    microfacet_normal) /
                (
                    4.0 *
                    abs(
                        dot(
                            outgoing,
                            microfacet_normal))
                ) *
                reflection_probability /
                probability_sum
            : 0.0;
    }
    else {
        const float denominator_base =
            dot(
                incident,
                microfacet_normal) +
            dot(
                outgoing,
                microfacet_normal) /
                relative_eta;
        const float denominator =
            denominator_base *
            denominator_base;
        float transmission =
            (
                1.0 - fresnel
            ) *
            microfacet_d(
                distribution,
                microfacet_normal) *
            microfacet_g(
                distribution,
                outgoing,
                incident) *
            abs(
                dot(
                    incident,
                    microfacet_normal) *
                dot(
                    outgoing,
                    microfacet_normal) /
                (
                    cosine_incident *
                    cosine_outgoing *
                    denominator
                ));
        if (
            transport_mode ==
            transport_radiance)
            transmission /=
                relative_eta *
                relative_eta;
        result.f =
            float4(transmission);
        const float reflection_probability =
            (
                sample_flags &
                bxdf_reflection
            ) !=
                0
            ? fresnel
            : 0.0;
        float transmission_probability =
            1.0 - fresnel;
        if (
            (
                sample_flags &
                bxdf_transmission
            ) ==
            0)
            transmission_probability = 0.0;
        const float probability_sum =
            reflection_probability +
            transmission_probability;
        result.pdf =
            probability_sum > 0.0
            ? microfacet_visible_d(
                    distribution,
                    outgoing,
                    microfacet_normal) *
                abs(
                    dot(
                        incident,
                        microfacet_normal)) /
                denominator *
                transmission_probability /
                probability_sum
            : 0.0;
    }
    return result;
}

[ForceInline]
public BxdfSample sample_dielectric(
    TrowbridgeReitzDistribution distribution,
    float eta,
    float3 outgoing,
    float component_sample,
    float2 direction_sample,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (eta == 1.0 ||
        effectively_smooth(distribution)) {
        const float reflection =
            fresnel_dielectric(
                outgoing.z,
                eta);
        const float transmission =
            1.0 - reflection;
        const float reflection_probability =
            (
                sample_flags &
                bxdf_reflection
            ) !=
                0
            ? reflection
            : 0.0;
        const float transmission_probability =
            (
                sample_flags &
                bxdf_transmission
            ) !=
                0
            ? transmission
            : 0.0;
        const float probability_sum =
            reflection_probability +
            transmission_probability;
        if (probability_sum == 0.0)
            return invalid_bxdf_sample();
        if (
            component_sample <
            reflection_probability /
                probability_sum) {
            BxdfSample result;
            result.wi =
                float3(
                    -outgoing.x,
                    -outgoing.y,
                    outgoing.z);
            result.f =
                float4(
                    reflection /
                    absolute_cosine(
                        result.wi));
            result.pdf =
                reflection_probability /
                probability_sum;
            result.eta = 1.0;
            result.flags =
                bxdf_reflection |
                bxdf_specular;
            result.valid = true;
            result.terminate_secondary = false;
            result.pdf_is_proportional = false;
            return result;
        }
        float relative_eta;
        float3 incident;
        if (!refract_direction(
                outgoing,
                float3(0.0, 0.0, 1.0),
                eta,
                relative_eta,
                incident))
            return invalid_bxdf_sample();
        BxdfSample result;
        result.wi = incident;
        float transmitted =
            transmission /
            absolute_cosine(incident);
        if (
            transport_mode ==
            transport_radiance)
            transmitted /=
                relative_eta *
                relative_eta;
        result.f =
            float4(transmitted);
        result.pdf =
            transmission_probability /
            probability_sum;
        result.eta = relative_eta;
        result.flags =
            bxdf_transmission |
            bxdf_specular;
        result.valid =
            transmission > 0.0;
        result.terminate_secondary = false;
        result.pdf_is_proportional = false;
        return result;
    }
    const float3 microfacet_normal =
        sample_microfacet_normal(
            distribution,
            outgoing,
            direction_sample);
    const float reflection =
        fresnel_dielectric(
            dot(
                outgoing,
                microfacet_normal),
            eta);
    const float transmission =
        1.0 - reflection;
    const float reflection_probability =
        (
            sample_flags &
            bxdf_reflection
        ) !=
            0
        ? reflection
        : 0.0;
    const float transmission_probability =
        (
            sample_flags &
            bxdf_transmission
        ) !=
            0
        ? transmission
        : 0.0;
    const float probability_sum =
        reflection_probability +
        transmission_probability;
    if (probability_sum == 0.0)
        return invalid_bxdf_sample();
    if (
        component_sample <
        reflection_probability /
            probability_sum) {
        const float3 incident =
            reflect_direction(
                outgoing,
                microfacet_normal);
        if (!same_hemisphere(
                outgoing,
                incident))
            return invalid_bxdf_sample();
        BxdfSample result;
        result.f =
            float4(
                microfacet_d(
                    distribution,
                    microfacet_normal) *
                microfacet_g(
                    distribution,
                    outgoing,
                    incident) *
                reflection /
                (
                    4.0 *
                    incident.z *
                    outgoing.z
                ));
        result.wi = incident;
        result.pdf =
            microfacet_visible_d(
                distribution,
                outgoing,
                microfacet_normal) /
            (
                4.0 *
                abs(
                    dot(
                        outgoing,
                        microfacet_normal))
            ) *
            reflection_probability /
            probability_sum;
        result.eta = 1.0;
        result.flags =
            bxdf_reflection |
            bxdf_glossy;
        result.valid =
            result.pdf > 0.0;
        result.terminate_secondary = false;
        result.pdf_is_proportional = false;
        return result;
    }
    float relative_eta;
    float3 incident;
    if (!refract_direction(
            outgoing,
            microfacet_normal,
            eta,
            relative_eta,
            incident) ||
        same_hemisphere(
            outgoing,
            incident) ||
        incident.z == 0.0)
        return invalid_bxdf_sample();
    const float denominator_base =
        dot(
            incident,
            microfacet_normal) +
        dot(
            outgoing,
            microfacet_normal) /
            relative_eta;
    const float denominator =
        denominator_base *
        denominator_base;
    float transmitted =
        transmission *
        microfacet_d(
            distribution,
            microfacet_normal) *
        microfacet_g(
            distribution,
            outgoing,
            incident) *
        abs(
            dot(
                incident,
                microfacet_normal) *
            dot(
                outgoing,
                microfacet_normal) /
            (
                incident.z *
                outgoing.z *
                denominator
            ));
    if (
        transport_mode ==
        transport_radiance)
        transmitted /=
            relative_eta *
            relative_eta;
    BxdfSample result;
    result.f =
        float4(transmitted);
    result.wi = incident;
    result.pdf =
        microfacet_visible_d(
            distribution,
            outgoing,
            microfacet_normal) *
        abs(
            dot(
                incident,
                microfacet_normal)) /
        denominator *
        transmission_probability /
        probability_sum;
    result.eta = relative_eta;
    result.flags =
        bxdf_transmission |
        bxdf_glossy;
    result.valid =
        result.pdf > 0.0 &&
        transmission > 0.0;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}

[ForceInline]
public BxdfSample sample_thin_dielectric(
    float eta,
    float3 outgoing,
    float component_sample,
    uint transport_mode = transport_radiance,
    uint sample_flags =
        bxdf_reflection_transmission) {
    float reflection =
        fresnel_dielectric(
            absolute_cosine(outgoing),
            eta);
    float transmission =
        1.0 - reflection;
    if (reflection < 1.0) {
        reflection +=
            transmission *
            transmission *
            reflection /
            (
                1.0 -
                reflection *
                    reflection
            );
        transmission =
            1.0 - reflection;
    }
    const float reflection_probability =
        (
            sample_flags &
            bxdf_reflection
        ) !=
            0
        ? reflection
        : 0.0;
    const float transmission_probability =
        (
            sample_flags &
            bxdf_transmission
        ) !=
            0
        ? transmission
        : 0.0;
    const float probability_sum =
        reflection_probability +
        transmission_probability;
    if (probability_sum == 0.0)
        return invalid_bxdf_sample();
    BxdfSample result;
    if (
        component_sample <
        reflection_probability /
            probability_sum) {
        result.wi =
            float3(
                -outgoing.x,
                -outgoing.y,
                outgoing.z);
        result.f =
            float4(
                reflection /
                absolute_cosine(
                    result.wi));
        result.pdf =
            reflection_probability /
            probability_sum;
        result.flags =
            bxdf_reflection |
            bxdf_specular;
    }
    else {
        result.wi = -outgoing;
        result.f =
            float4(
                transmission /
                absolute_cosine(
                    result.wi));
        result.pdf =
            transmission_probability /
            probability_sum;
        result.flags =
            bxdf_transmission |
            bxdf_specular;
    }
    result.eta = 1.0;
    result.valid =
        result.pdf > 0.0;
    result.terminate_secondary = false;
    result.pdf_is_proportional = false;
    return result;
}
