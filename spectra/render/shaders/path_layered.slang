module path_layered;

import path_bxdf;
import path_phase;
import path_random;

public static const uint layered_bottom_diffuse = 0;
public static const uint layered_bottom_conductor = 1;

public struct LayeredBxdf {
    public TrowbridgeReitzDistribution interface_distribution;
    public TrowbridgeReitzDistribution conductor_distribution;
    public float interface_eta;
    public float4 diffuse_reflectance;
    public float4 conductor_eta;
    public float4 conductor_k;
    public float thickness;
    public float4 albedo;
    public float anisotropy;
    public uint max_depth;
    public uint sample_count;
    public uint bottom_kind;
}

float layered_max_component(float4 value) {
    return max(
        value.x,
        max(
            value.y,
            max(
                value.z,
                value.w)));
}

bool layered_same_hemisphere(
    float3 first,
    float3 second) {
    return first.z * second.z > 0.0;
}

bool layered_nonzero(float4 value) {
    return any(value != float4(0.0));
}

uint layered_opposite_transport(uint mode) {
    return mode == transport_radiance
        ? transport_importance
        : transport_radiance;
}

bool layered_interface_specular(
    LayeredBxdf layered,
    bool top) {
    const TrowbridgeReitzDistribution distribution =
        top
            ? layered.interface_distribution
            : layered.conductor_distribution;
    if (
        !top &&
        layered.bottom_kind ==
            layered_bottom_diffuse)
        return false;
    return
        max(
            distribution.alpha_x,
            distribution.alpha_y) <
        1.0e-3;
}

bool layered_interface_non_specular(
    LayeredBxdf layered,
    bool top) {
    if (
        !top &&
        layered.bottom_kind ==
            layered_bottom_diffuse)
        return layered_nonzero(
            layered.diffuse_reflectance);
    return !layered_interface_specular(
        layered,
        top);
}

BxdfEvaluation layered_interface_evaluate(
    LayeredBxdf layered,
    bool top,
    float3 outgoing,
    float3 incident,
    uint transport_mode,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (top)
        return evaluate_dielectric(
            layered.interface_distribution,
            layered.interface_eta,
            outgoing,
            incident,
            transport_mode,
            sample_flags);
    if (
        layered.bottom_kind ==
        layered_bottom_diffuse)
        return evaluate_diffuse(
            layered.diffuse_reflectance,
            outgoing,
            incident,
            transport_mode,
            sample_flags);
    return evaluate_conductor(
        layered.conductor_distribution,
        layered.conductor_eta,
        layered.conductor_k,
        outgoing,
        incident,
        transport_mode,
        sample_flags);
}

BxdfSample layered_interface_sample(
    LayeredBxdf layered,
    bool top,
    float3 outgoing,
    float component_sample,
    float2 direction_sample,
    uint transport_mode,
    uint sample_flags =
        bxdf_reflection_transmission) {
    if (top)
        return sample_dielectric(
            layered.interface_distribution,
            layered.interface_eta,
            outgoing,
            component_sample,
            direction_sample,
            transport_mode,
            sample_flags);
    if (
        layered.bottom_kind ==
        layered_bottom_diffuse)
        return sample_diffuse(
            layered.diffuse_reflectance,
            outgoing,
            direction_sample,
            transport_mode,
            sample_flags);
    return sample_conductor(
        layered.conductor_distribution,
        layered.conductor_eta,
        layered.conductor_k,
        outgoing,
        direction_sample,
        transport_mode,
        sample_flags);
}

float layered_interface_pdf(
    LayeredBxdf layered,
    bool top,
    float3 outgoing,
    float3 incident,
    uint transport_mode,
    uint sample_flags =
        bxdf_reflection_transmission) {
    return layered_interface_evaluate(
        layered,
        top,
        outgoing,
        incident,
        transport_mode,
        sample_flags)
        .pdf;
}

float layered_power_heuristic(
    float first_pdf,
    float second_pdf) {
    const float first_squared =
        first_pdf *
        first_pdf;
    const float second_squared =
        second_pdf *
        second_pdf;
    if (isinf(first_squared))
        return 1.0;
    return first_squared /
        (
            first_squared +
            second_squared
        );
}

float layered_transmittance(
    float distance_z,
    float3 direction) {
    if (abs(distance_z) <= 1.175494351e-38)
        return 1.0;
    const float exponent =
        -abs(
            distance_z /
            direction.z);
    const float base_two_exponent =
        exponent *
        1.442695041;
    const float integer_exponent =
        floor(base_two_exponent);
    const float fraction =
        base_two_exponent -
        integer_exponent;
    const float two_to_fraction =
        (
            (
                0.0781455737 *
                fraction +
                0.226173572
            ) *
            fraction +
            0.695556856
        ) *
        fraction +
        1.0;
    const int result_exponent =
        int(
            (
                asuint(
                    two_to_fraction) >>
                23
            ) &
            255u) -
        127 +
        int(integer_exponent);
    if (result_exponent < -126)
        return 0.0;
    if (result_exponent > 127)
        return 3.402823466e+38;
    uint bits =
        asuint(
            two_to_fraction) &
        0x807fffffu;
    bits |=
        uint(
            result_exponent +
            127) <<
        23;
    return asfloat(bits);
}

float layered_sample_exponential(
    float sample,
    float rate) {
    return -log(
        max(
            1.0 - sample,
            1.175494351e-38)) /
        rate;
}

Pcg32 layered_evaluation_pcg(
    float3 outgoing,
    float3 incident) {
    return make_seeded_pcg(
        hash_4(
            0,
            asuint(outgoing.x),
            asuint(outgoing.y),
            asuint(outgoing.z)),
        hash_3(
            asuint(incident.x),
            asuint(incident.y),
            asuint(incident.z)));
}

Pcg32 layered_sampling_pcg(
    float3 outgoing,
    float component_sample,
    float2 direction_sample) {
    return make_seeded_pcg(
        hash_4(
            0,
            asuint(outgoing.x),
            asuint(outgoing.y),
            asuint(outgoing.z)),
        hash_3(
            asuint(component_sample),
            asuint(direction_sample.x),
            asuint(direction_sample.y)));
}

Pcg32 layered_pdf_pcg(
    float3 outgoing,
    float3 incident) {
    return make_seeded_pcg(
        hash_4(
            0,
            asuint(incident.x),
            asuint(incident.y),
            asuint(incident.z)),
        hash_3(
            asuint(outgoing.x),
            asuint(outgoing.y),
            asuint(outgoing.z)));
}

float4 layered_f(
    LayeredBxdf layered,
    float3 outgoing,
    float3 incident,
    uint transport_mode) {
    float4 result = float4(0.0);
    if (outgoing.z < 0.0) {
        outgoing = -outgoing;
        incident = -incident;
    }

    const bool same_hemisphere =
        layered_same_hemisphere(
            outgoing,
            incident);
    const bool exit_top =
        same_hemisphere;
    const bool non_exit_top =
        !same_hemisphere;
    const float exit_z =
        same_hemisphere
            ? layered.thickness
            : 0.0;

    if (same_hemisphere)
        result =
            float(layered.sample_count) *
            layered_interface_evaluate(
                layered,
                true,
                outgoing,
                incident,
                transport_mode)
                .f;

    Pcg32 generator =
        layered_evaluation_pcg(
            outgoing,
            incident);
    const bool scattering =
        layered_nonzero(
            layered.albedo);

    [loop]
    for (uint sample_index = 0;
         sample_index != layered.sample_count;
         ++sample_index) {
        const float outgoing_component_sample =
            pcg_float(generator);
        const float outgoing_direction_y =
            pcg_float(generator);
        const float outgoing_direction_x =
            pcg_float(generator);
        const BxdfSample outgoing_sample =
            layered_interface_sample(
                layered,
                true,
                outgoing,
                outgoing_component_sample,
                float2(
                    outgoing_direction_x,
                    outgoing_direction_y),
                transport_mode,
                bxdf_transmission);
        if (
            !outgoing_sample.valid ||
            !layered_nonzero(
                outgoing_sample.f) ||
            outgoing_sample.pdf == 0.0 ||
            outgoing_sample.wi.z == 0.0)
            continue;

        const float incident_component_sample =
            pcg_float(generator);
        const float incident_direction_y =
            pcg_float(generator);
        const float incident_direction_x =
            pcg_float(generator);
        const BxdfSample incident_sample =
            layered_interface_sample(
                layered,
                exit_top,
                incident,
                incident_component_sample,
                float2(
                    incident_direction_x,
                    incident_direction_y),
                layered_opposite_transport(
                    transport_mode),
                bxdf_transmission);
        if (
            !incident_sample.valid ||
            !layered_nonzero(
                incident_sample.f) ||
            incident_sample.pdf == 0.0 ||
            incident_sample.wi.z == 0.0)
            continue;

        float4 beta =
            outgoing_sample.f *
            absolute_cosine(
                outgoing_sample.wi) /
            outgoing_sample.pdf;
        float z = layered.thickness;
        float3 direction =
            outgoing_sample.wi;

        [loop]
        for (uint depth = 0;
             depth != layered.max_depth;
             ++depth) {
            if (
                depth > 3 &&
                layered_max_component(beta) <
                    0.25) {
                const float termination =
                    max(
                        0.0,
                        1.0 -
                            layered_max_component(
                                beta));
                if (
                    pcg_float(generator) <
                    termination)
                    break;
                beta /=
                    1.0 -
                    termination;
            }

            if (!scattering) {
                z =
                    z == layered.thickness
                        ? 0.0
                        : layered.thickness;
                beta *=
                    layered_transmittance(
                        layered.thickness,
                        direction);
            }
            else {
                const float distance_z =
                    layered_sample_exponential(
                        pcg_float(generator),
                        1.0 /
                            abs(direction.z));
                const float scattered_z =
                    direction.z > 0.0
                        ? z + distance_z
                        : z - distance_z;
                if (z == scattered_z)
                    continue;
                if (
                    scattered_z > 0.0 &&
                    scattered_z <
                        layered.thickness) {
                    float weight = 1.0;
                    if (!layered_interface_specular(
                            layered,
                            exit_top))
                        weight =
                            layered_power_heuristic(
                                incident_sample.pdf,
                                henyey_greenstein(
                                    -direction,
                                    -incident_sample.wi,
                                    layered.anisotropy));
                    result +=
                        beta *
                        layered.albedo *
                        henyey_greenstein(
                            -direction,
                            -incident_sample.wi,
                            layered.anisotropy) *
                        weight *
                        layered_transmittance(
                            scattered_z -
                                exit_z,
                            incident_sample.wi) *
                        incident_sample.f /
                        incident_sample.pdf;

                    float phase_pdf;
                    const float3 phase_direction =
                        sample_henyey_greenstein(
                            -direction,
                            layered.anisotropy,
                            float2(
                                pcg_float(generator),
                                pcg_float(generator)),
                            phase_pdf);
                    if (
                        phase_pdf == 0.0 ||
                        phase_direction.z == 0.0)
                        continue;
                    beta *=
                        layered.albedo *
                        henyey_greenstein(
                            -direction,
                            phase_direction,
                            layered.anisotropy) /
                        phase_pdf;
                    direction =
                        phase_direction;
                    z = scattered_z;

                    if (
                        (
                            (
                                z < exit_z &&
                                direction.z > 0.0
                            ) ||
                            (
                                z > exit_z &&
                                direction.z < 0.0
                            )
                        ) &&
                        !layered_interface_specular(
                            layered,
                            exit_top)) {
                        const BxdfEvaluation exit_evaluation =
                            layered_interface_evaluate(
                                layered,
                                exit_top,
                                -direction,
                                incident,
                                transport_mode);
                        if (layered_nonzero(
                                exit_evaluation.f)) {
                            const float exit_pdf =
                                layered_interface_pdf(
                                    layered,
                                    exit_top,
                                    -direction,
                                    incident,
                                    transport_mode,
                                    bxdf_transmission);
                            const float exit_weight =
                                layered_power_heuristic(
                                    phase_pdf,
                                    exit_pdf);
                            result +=
                                beta *
                                layered_transmittance(
                                    scattered_z -
                                        exit_z,
                                    phase_direction) *
                                exit_evaluation.f *
                                exit_weight;
                        }
                    }
                    continue;
                }
                z =
                    clamp(
                        scattered_z,
                        0.0,
                        layered.thickness);
            }

            if (z == exit_z) {
                const float boundary_component_sample =
                    pcg_float(generator);
                const float boundary_direction_y =
                    pcg_float(generator);
                const float boundary_direction_x =
                    pcg_float(generator);
                const BxdfSample boundary_sample =
                    layered_interface_sample(
                        layered,
                        exit_top,
                        -direction,
                        boundary_component_sample,
                        float2(
                            boundary_direction_x,
                            boundary_direction_y),
                        transport_mode,
                        bxdf_reflection);
                if (
                    !boundary_sample.valid ||
                    !layered_nonzero(
                        boundary_sample.f) ||
                    boundary_sample.pdf == 0.0 ||
                    boundary_sample.wi.z == 0.0)
                    break;
                beta *=
                    boundary_sample.f *
                    absolute_cosine(
                        boundary_sample.wi) /
                    boundary_sample.pdf;
                direction =
                    boundary_sample.wi;
            }
            else {
                if (!layered_interface_specular(
                        layered,
                        non_exit_top)) {
                    float weight = 1.0;
                    if (!layered_interface_specular(
                            layered,
                            exit_top))
                        weight =
                            layered_power_heuristic(
                                incident_sample.pdf,
                                layered_interface_pdf(
                                    layered,
                                    non_exit_top,
                                    -direction,
                                    -incident_sample.wi,
                                    transport_mode));
                    result +=
                        beta *
                        layered_interface_evaluate(
                            layered,
                            non_exit_top,
                            -direction,
                            -incident_sample.wi,
                            transport_mode)
                            .f *
                        absolute_cosine(
                            incident_sample.wi) *
                        weight *
                        layered_transmittance(
                            layered.thickness,
                            incident_sample.wi) *
                        incident_sample.f /
                        incident_sample.pdf;
                }

                const float boundary_component_sample =
                    pcg_float(generator);
                const float boundary_direction_y =
                    pcg_float(generator);
                const float boundary_direction_x =
                    pcg_float(generator);
                const BxdfSample boundary_sample =
                    layered_interface_sample(
                        layered,
                        non_exit_top,
                        -direction,
                        boundary_component_sample,
                        float2(
                            boundary_direction_x,
                            boundary_direction_y),
                        transport_mode,
                        bxdf_reflection);
                if (
                    !boundary_sample.valid ||
                    !layered_nonzero(
                        boundary_sample.f) ||
                    boundary_sample.pdf == 0.0 ||
                    boundary_sample.wi.z == 0.0)
                    break;
                beta *=
                    boundary_sample.f *
                    absolute_cosine(
                        boundary_sample.wi) /
                    boundary_sample.pdf;
                direction =
                    boundary_sample.wi;

                if (!layered_interface_specular(
                        layered,
                        exit_top)) {
                    const BxdfEvaluation exit_evaluation =
                        layered_interface_evaluate(
                            layered,
                            exit_top,
                            -direction,
                            incident,
                            transport_mode);
                    if (layered_nonzero(
                            exit_evaluation.f)) {
                        float weight = 1.0;
                        if (!layered_interface_specular(
                                layered,
                                non_exit_top)) {
                            const float exit_pdf =
                                layered_interface_pdf(
                                    layered,
                                    exit_top,
                                    -direction,
                                    incident,
                                    transport_mode,
                                    bxdf_transmission);
                            weight =
                                layered_power_heuristic(
                                    boundary_sample.pdf,
                                    exit_pdf);
                        }
                        result +=
                            beta *
                            layered_transmittance(
                                layered.thickness,
                                boundary_sample.wi) *
                            exit_evaluation.f *
                            weight;
                    }
                }
            }
        }
    }
    return result /
        float(layered.sample_count);
}

public float layered_pdf(
    LayeredBxdf layered,
    float3 outgoing,
    float3 incident,
    uint transport_mode) {
    if (outgoing.z < 0.0) {
        outgoing = -outgoing;
        incident = -incident;
    }
    Pcg32 generator =
        layered_pdf_pcg(
            outgoing,
            incident);
    const bool same_hemisphere =
        layered_same_hemisphere(
            outgoing,
            incident);
    float pdf_sum = 0.0;
    if (same_hemisphere)
        pdf_sum +=
            float(layered.sample_count) *
            layered_interface_pdf(
                layered,
                true,
                outgoing,
                incident,
                transport_mode,
                bxdf_reflection);

    [loop]
    for (uint sample_index = 0;
         sample_index != layered.sample_count;
         ++sample_index) {
        if (same_hemisphere) {
            const float2 outgoing_direction_sample =
                float2(
                    pcg_float(generator),
                    pcg_float(generator));
            const float outgoing_component_sample =
                pcg_float(generator);
            const BxdfSample outgoing_sample =
                layered_interface_sample(
                    layered,
                    true,
                    outgoing,
                    outgoing_component_sample,
                    outgoing_direction_sample,
                    transport_mode,
                    bxdf_transmission);
            const float2 incident_direction_sample =
                float2(
                    pcg_float(generator),
                    pcg_float(generator));
            const float incident_component_sample =
                pcg_float(generator);
            const BxdfSample incident_sample =
                layered_interface_sample(
                    layered,
                    true,
                    incident,
                    incident_component_sample,
                    incident_direction_sample,
                    layered_opposite_transport(
                        transport_mode),
                    bxdf_transmission);
            if (
                outgoing_sample.valid &&
                layered_nonzero(
                    outgoing_sample.f) &&
                outgoing_sample.pdf > 0.0 &&
                incident_sample.valid &&
                layered_nonzero(
                    incident_sample.f) &&
                incident_sample.pdf > 0.0) {
                if (!layered_interface_non_specular(
                        layered,
                        true))
                    pdf_sum +=
                        layered_interface_pdf(
                            layered,
                            false,
                            -outgoing_sample.wi,
                            -incident_sample.wi,
                            transport_mode);
                else {
                    const float2 reflection_direction_sample =
                        float2(
                            pcg_float(generator),
                            pcg_float(generator));
                    const float reflection_component_sample =
                        pcg_float(generator);
                    const BxdfSample reflection_sample =
                        layered_interface_sample(
                            layered,
                            false,
                            -outgoing_sample.wi,
                            reflection_component_sample,
                            reflection_direction_sample,
                            transport_mode);
                    if (
                        reflection_sample.valid &&
                        layered_nonzero(
                            reflection_sample.f) &&
                        reflection_sample.pdf > 0.0) {
                        if (!layered_interface_non_specular(
                                layered,
                                false))
                            pdf_sum +=
                                layered_interface_pdf(
                                    layered,
                                    true,
                                    -reflection_sample.wi,
                                    incident,
                                    transport_mode);
                        else {
                            const float reflection_pdf =
                                layered_interface_pdf(
                                    layered,
                                    false,
                                    -outgoing_sample.wi,
                                    -incident_sample.wi,
                                    transport_mode);
                            pdf_sum +=
                                layered_power_heuristic(
                                    incident_sample.pdf,
                                    reflection_pdf) *
                                reflection_pdf;

                            const float transmission_pdf =
                                layered_interface_pdf(
                                    layered,
                                    true,
                                    -reflection_sample.wi,
                                    incident,
                                    transport_mode);
                            pdf_sum +=
                                layered_power_heuristic(
                                    reflection_sample.pdf,
                                    transmission_pdf) *
                                transmission_pdf;
                        }
                    }
                }
            }
        }
        else {
            const float outgoing_component_sample =
                pcg_float(generator);
            const float outgoing_direction_y =
                pcg_float(generator);
            const float outgoing_direction_x =
                pcg_float(generator);
            const BxdfSample outgoing_sample =
                layered_interface_sample(
                    layered,
                    true,
                    outgoing,
                    outgoing_component_sample,
                    float2(
                        outgoing_direction_x,
                        outgoing_direction_y),
                    transport_mode);
            if (
                !outgoing_sample.valid ||
                !layered_nonzero(
                    outgoing_sample.f) ||
                outgoing_sample.pdf == 0.0 ||
                outgoing_sample.wi.z == 0.0 ||
                (
                    outgoing_sample.flags &
                    bxdf_reflection
                ) !=
                    0)
                continue;

            const float incident_component_sample =
                pcg_float(generator);
            const float incident_direction_y =
                pcg_float(generator);
            const float incident_direction_x =
                pcg_float(generator);
            const BxdfSample incident_sample =
                layered_interface_sample(
                    layered,
                    false,
                    incident,
                    incident_component_sample,
                    float2(
                        incident_direction_x,
                        incident_direction_y),
                    layered_opposite_transport(
                        transport_mode));
            if (
                !incident_sample.valid ||
                !layered_nonzero(
                    incident_sample.f) ||
                incident_sample.pdf == 0.0 ||
                incident_sample.wi.z == 0.0 ||
                (
                    incident_sample.flags &
                    bxdf_reflection
                ) !=
                    0)
                continue;

            if (layered_interface_specular(
                    layered,
                    true))
                pdf_sum +=
                    layered_interface_pdf(
                        layered,
                        false,
                        -outgoing_sample.wi,
                        incident,
                        transport_mode);
            else if (layered_interface_specular(
                    layered,
                    false))
                pdf_sum +=
                    layered_interface_pdf(
                        layered,
                        true,
                        outgoing,
                        -incident_sample.wi,
                        transport_mode);
            else
                pdf_sum +=
                    0.5 *
                    (
                        layered_interface_pdf(
                            layered,
                            true,
                            outgoing,
                            -incident_sample.wi,
                            transport_mode) +
                        layered_interface_pdf(
                            layered,
                            false,
                            -outgoing_sample.wi,
                            incident,
                            transport_mode)
                    );
        }
    }
    const float estimate =
        pdf_sum /
        float(layered.sample_count);
    return lerp(
        1.0 /
            (
                4.0 *
                path_pi
            ),
        estimate,
        0.9);
}

public BxdfEvaluation evaluate_layered(
    LayeredBxdf layered,
    float3 outgoing,
    float3 incident,
    uint transport_mode =
        transport_radiance) {
    BxdfEvaluation result;
    result.f =
        layered_f(
            layered,
            outgoing,
            incident,
            transport_mode);
    result.pdf =
        layered_pdf(
            layered,
            outgoing,
            incident,
            transport_mode);
    return result;
}

public BxdfSample sample_layered(
    LayeredBxdf layered,
    float3 outgoing,
    float component_sample,
    float2 direction_sample,
    uint transport_mode =
        transport_radiance) {
    bool flip_incident = false;
    if (outgoing.z < 0.0) {
        outgoing = -outgoing;
        flip_incident = true;
    }

    BxdfSample initial =
        layered_interface_sample(
            layered,
            true,
            outgoing,
            component_sample,
            direction_sample,
            transport_mode);
    if (
        !initial.valid ||
        !layered_nonzero(initial.f) ||
        initial.pdf == 0.0 ||
        initial.wi.z == 0.0)
        return invalid_bxdf_sample();
    if (
        (
            initial.flags &
            bxdf_reflection
        ) !=
        0) {
        if (flip_incident)
            initial.wi =
                -initial.wi;
        initial.pdf_is_proportional = true;
        return initial;
    }

    float3 direction = initial.wi;
    bool specular_path =
        (
            initial.flags &
            bxdf_specular
        ) !=
        0;
    Pcg32 generator =
        layered_sampling_pcg(
            outgoing,
            component_sample,
            direction_sample);
    float4 value =
        initial.f *
        absolute_cosine(
            initial.wi);
    float pdf = initial.pdf;
    float z = layered.thickness;
    const bool scattering =
        layered_nonzero(
            layered.albedo);

    [loop]
    for (uint depth = 0;
         depth != layered.max_depth;
         ++depth) {
        const float roulette_beta =
            layered_max_component(value) /
            pdf;
        if (
            depth > 3 &&
            roulette_beta < 0.25) {
            const float termination =
                max(
                    0.0,
                    1.0 -
                        roulette_beta);
            if (
                pcg_float(generator) <
                termination)
                return invalid_bxdf_sample();
            pdf *=
                1.0 -
                termination;
        }
        if (direction.z == 0.0)
            return invalid_bxdf_sample();

        if (scattering) {
            const float distance_z =
                layered_sample_exponential(
                    pcg_float(generator),
                    1.0 /
                        absolute_cosine(
                            direction));
            const float scattered_z =
                direction.z > 0.0
                    ? z + distance_z
                    : z - distance_z;
            if (scattered_z == z)
                return invalid_bxdf_sample();
            if (
                scattered_z > 0.0 &&
                scattered_z <
                    layered.thickness) {
                float phase_pdf;
                const float phase_direction_y =
                    pcg_float(generator);
                const float phase_direction_x =
                    pcg_float(generator);
                const float3 phase_direction =
                    sample_henyey_greenstein(
                        -direction,
                        layered.anisotropy,
                        float2(
                            phase_direction_x,
                            phase_direction_y),
                        phase_pdf);
                if (
                    phase_pdf == 0.0 ||
                    phase_direction.z == 0.0)
                    return invalid_bxdf_sample();
                value *=
                    layered.albedo *
                    henyey_greenstein(
                        -direction,
                        phase_direction,
                        layered.anisotropy);
                pdf *=
                    phase_pdf;
                specular_path = false;
                direction =
                    phase_direction;
                z = scattered_z;
                continue;
            }
            z =
                clamp(
                    scattered_z,
                    0.0,
                    layered.thickness);
        }
        else {
            z =
                z == layered.thickness
                    ? 0.0
                    : layered.thickness;
            value *=
                layered_transmittance(
                    layered.thickness,
                    direction);
        }

        const bool interface_top =
            z != 0.0;
        const float interface_component_sample =
            pcg_float(generator);
        const float interface_direction_y =
            pcg_float(generator);
        const float interface_direction_x =
            pcg_float(generator);
        const BxdfSample interface_sample =
            layered_interface_sample(
                layered,
                interface_top,
                -direction,
                interface_component_sample,
                float2(
                    interface_direction_x,
                    interface_direction_y),
                transport_mode);
        if (
            !interface_sample.valid ||
            !layered_nonzero(
                interface_sample.f) ||
            interface_sample.pdf == 0.0 ||
            interface_sample.wi.z == 0.0)
            return invalid_bxdf_sample();
        value *=
            interface_sample.f;
        pdf *=
            interface_sample.pdf;
        specular_path =
            specular_path &&
            (
                (
                    interface_sample.flags &
                    bxdf_specular
                ) !=
                0
            );
        direction =
            interface_sample.wi;

        if (
            (
                interface_sample.flags &
                bxdf_transmission
            ) !=
            0) {
            BxdfSample result;
            result.f = value;
            result.wi =
                flip_incident
                    ? -direction
                    : direction;
            result.pdf = pdf;
            result.eta = 1.0;
            result.flags =
                (
                    layered_same_hemisphere(
                        outgoing,
                        direction)
                        ? bxdf_reflection
                        : bxdf_transmission
                ) |
                (
                    specular_path
                        ? bxdf_specular
                        : bxdf_glossy
                );
            result.valid = true;
            result.terminate_secondary = false;
            result.pdf_is_proportional = true;
            return result;
        }
        value *=
            absolute_cosine(
                interface_sample.wi);
    }
    return invalid_bxdf_sample();
}
