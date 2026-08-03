module path_interaction;

import path_scene;
import path_queue;

public static const uint
    ray_differential_valid =
        0x80000000;
public static const uint
    any_non_specular_bounce =
        0x40000000;

public TextureEvaluationContext
make_texture_evaluation_context(
    WavefrontBindings bindings,
    uint path_id,
    float3 position,
    float3 shading_normal,
    float3 geometric_normal,
    float2 texture_coordinates,
    float3 dpdu,
    float3 dpdv,
    uint path_flags,
    uint2 texture_requests,
    uint texture_value_count) {
    float3 position_dx =
        float3(0.0);
    float3 position_dy =
        float3(0.0);
    float2 texture_coordinates_dx =
        float2(0.0);
    float2 texture_coordinates_dy =
        float2(0.0);
    if (
        (
            path_flags &
            ray_differential_valid
        ) != 0) {
        RWStructuredBuffer<float4>
            ray_origin_dx =
                bindings.ray_origin_dx;
        RWStructuredBuffer<float4>
            ray_origin_dy =
                bindings.ray_origin_dy;
        RWStructuredBuffer<float4>
            ray_direction_dx =
                bindings.ray_direction_dx;
        RWStructuredBuffer<float4>
            ray_direction_dy =
                bindings.ray_direction_dy;
        const float3 origin_dx =
            ray_origin_dx[path_id].xyz;
        const float3 origin_dy =
            ray_origin_dy[path_id].xyz;
        const float3 direction_dx =
            ray_direction_dx[path_id].xyz;
        const float3 direction_dy =
            ray_direction_dy[path_id].xyz;
        const float plane =
            dot(
                geometric_normal,
                position);
        const float denominator_x =
            dot(
                geometric_normal,
                direction_dx);
        const float denominator_y =
            dot(
                geometric_normal,
                direction_dy);
        if (
            denominator_x != 0.0 &&
            denominator_y != 0.0) {
            const float distance_x =
                (
                    plane -
                    dot(
                        geometric_normal,
                        origin_dx)
                ) /
                denominator_x;
            const float distance_y =
                (
                    plane -
                    dot(
                        geometric_normal,
                        origin_dy)
                ) /
                denominator_y;
            position_dx =
                origin_dx +
                distance_x *
                    direction_dx -
                position;
            position_dy =
                origin_dy +
                distance_y *
                    direction_dy -
                position;
        }
        const float ata_00 =
            dot(dpdu, dpdu);
        const float ata_01 =
            dot(dpdu, dpdv);
        const float ata_11 =
            dot(dpdv, dpdv);
        const float determinant =
            ata_00 * ata_11 -
            ata_01 * ata_01;
        const float inverse_determinant =
            determinant != 0.0
                ? 1.0 /
                    determinant
                : 0.0;
        const float atb_0_x =
            dot(
                dpdu,
                position_dx);
        const float atb_1_x =
            dot(
                dpdv,
                position_dx);
        const float atb_0_y =
            dot(
                dpdu,
                position_dy);
        const float atb_1_y =
            dot(
                dpdv,
                position_dy);
        texture_coordinates_dx =
            clamp(
                float2(
                    (
                        ata_11 *
                            atb_0_x -
                        ata_01 *
                            atb_1_x
                    ) *
                        inverse_determinant,
                    (
                        ata_00 *
                            atb_1_x -
                        ata_01 *
                            atb_0_x
                    ) *
                        inverse_determinant),
                float2(-1.0e8),
                float2(1.0e8));
        texture_coordinates_dy =
            clamp(
                float2(
                    (
                        ata_11 *
                            atb_0_y -
                        ata_01 *
                            atb_1_y
                    ) *
                        inverse_determinant,
                    (
                        ata_00 *
                            atb_1_y -
                        ata_01 *
                            atb_0_y
                    ) *
                        inverse_determinant),
                float2(-1.0e8),
                float2(1.0e8));
    }
    return {
        position,
        shading_normal,
        texture_coordinates,
        position_dx,
        position_dy,
        texture_coordinates_dx,
        texture_coordinates_dy,
        texture_requests,
        bindings.evaluated_texture_values,
        path_id *
            texture_value_count,
        uint2(0),
    };
}
