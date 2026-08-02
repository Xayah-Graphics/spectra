module path_light;

import path_scene;
import path_random;
import path_texture;
import sampling;
import spectrum;

static const float light_pi =
    3.14159265358979323846;
static const uint invalid_light_index =
    0xffffffff;

float3 sample_uniform_sphere(
    float2 sample) {
    const float z =
        1.0 -
        2.0 *
            sample.x;
    const float radius =
        sqrt(
            max(
                0.0,
                1.0 -
                    z * z));
    const float phi =
        2.0 *
        light_pi *
        sample.y;
    return float3(
        radius * cos(phi),
        radius * sin(phi),
        z);
}

float2 sample_concentric_disk(
    float2 sample) {
    const float2 offset =
        2.0 *
            sample -
        1.0;
    if (
        offset.x == 0.0 &&
        offset.y == 0.0)
        return float2(0.0);
    float radius;
    float theta;
    if (
        abs(offset.x) >
        abs(offset.y)) {
        radius = offset.x;
        theta =
            light_pi /
            4.0 *
            (
                offset.y /
                offset.x
            );
    }
    else {
        radius = offset.y;
        theta =
            light_pi /
                2.0 -
            light_pi /
                4.0 *
                (
                    offset.x /
                    offset.y
                );
    }
    return
        radius *
        float2(
            cos(theta),
            sin(theta));
}

float3 sample_cosine_hemisphere(
    float2 sample) {
    const float2 disk =
        sample_concentric_disk(sample);
    return float3(
        disk,
        sqrt(
            max(
                0.0,
                1.0 -
                    dot(
                        disk,
                        disk))));
}

void frame_from_z(
    float3 z,
    out float3 x,
    out float3 y) {
    z = normalize(z);
    const float sign_z =
        z.z >= 0.0
            ? 1.0
            : -1.0;
    const float a =
        -1.0 /
        (
            sign_z +
            z.z
        );
    const float b =
        z.x *
        z.y *
        a;
    x =
        float3(
            1.0 +
                sign_z *
                    z.x *
                    z.x *
                    a,
            sign_z * b,
            -sign_z * z.x);
    y =
        float3(
            b,
            sign_z +
                z.y *
                    z.y *
                    a,
            -z.y);
}

float angle_between(
    float3 first,
    float3 second) {
    return
        dot(
            first,
            second) < 0.0
            ? light_pi -
                  2.0 *
                      asin(
                          min(
                              length(
                                  first +
                                  second) *
                                  0.5,
                              1.0))
            : 2.0 *
                  asin(
                      min(
                          length(
                              first -
                              second) *
                              0.5,
                          1.0));
}

float spherical_triangle_area(
    float3 first,
    float3 second,
    float3 third) {
    return
        abs(
            2.0 *
            atan2(
                dot(
                    first,
                    cross(
                        second,
                        third)),
                1.0 +
                    dot(
                        first,
                        second) +
                    dot(
                        first,
                        third) +
                    dot(
                        second,
                        third)));
}

float sample_linear(
    float sample,
    float first,
    float second) {
    if (
        sample == 0.0 &&
        first == 0.0)
        return 0.0;
    return
        min(
            sample *
                (
                    first +
                    second
                ) /
                (
                    first +
                    sqrt(
                        lerp(
                            first *
                                first,
                            second *
                                second,
                            sample))
                ),
            0.99999994);
}

float2 sample_bilinear(
    float2 sample,
    float4 weights) {
    const float y =
        sample_linear(
            sample.y,
            weights.x +
                weights.y,
            weights.z +
                weights.w);
    return float2(
        sample_linear(
            sample.x,
            lerp(
                weights.x,
                weights.z,
                y),
            lerp(
                weights.y,
                weights.w,
                y)),
        y);
}

float bilinear_pdf(
    float2 point,
    float4 weights) {
    const float sum =
        weights.x +
        weights.y +
        weights.z +
        weights.w;
    if (sum == 0.0)
        return 1.0;
    return
        4.0 *
        (
            (
                1.0 -
                point.x
            ) *
                (
                    1.0 -
                    point.y
                ) *
                weights.x +
            point.x *
                (
                    1.0 -
                    point.y
                ) *
                weights.y +
            (
                1.0 -
                point.x
            ) *
                point.y *
                weights.z +
            point.x *
                point.y *
                weights.w
        ) /
        sum;
}

float3 sample_spherical_triangle(
    float3 vertex_0,
    float3 vertex_1,
    float3 vertex_2,
    float3 position,
    float2 sample,
    out float pdf) {
    const float3 a =
        normalize(
            vertex_0 -
            position);
    const float3 b =
        normalize(
            vertex_1 -
            position);
    const float3 c =
        normalize(
            vertex_2 -
            position);
    const float3 n_ab =
        normalize(
            cross(
                a,
                b));
    const float3 n_bc =
        normalize(
            cross(
                b,
                c));
    const float3 n_ca =
        normalize(
            cross(
                c,
                a));
    const float alpha =
        angle_between(
            n_ab,
            -n_ca);
    const float beta =
        angle_between(
            n_bc,
            -n_ab);
    const float gamma =
        angle_between(
            n_ca,
            -n_bc);
    const float area_pi =
        alpha +
        beta +
        gamma;
    const float area =
        area_pi -
        light_pi;
    pdf =
        area <= 0.0
            ? 0.0
            : 1.0 /
                area;
    const float sampled_area_pi =
        lerp(
            light_pi,
            area_pi,
            sample.x);
    const float cosine_alpha =
        cos(alpha);
    const float sine_alpha =
        sin(alpha);
    const float sine_phi =
        sin(
            sampled_area_pi) *
            cosine_alpha -
        cos(
            sampled_area_pi) *
            sine_alpha;
    const float cosine_phi =
        cos(
            sampled_area_pi) *
            cosine_alpha +
        sin(
            sampled_area_pi) *
            sine_alpha;
    const float k_1 =
        cosine_phi +
        cosine_alpha;
    const float k_2 =
        sine_phi -
        sine_alpha *
            dot(
                a,
                b);
    const float cosine_b_prime =
        clamp(
            (
                k_2 +
                (
                    k_2 *
                        cosine_phi -
                    k_1 *
                        sine_phi
                ) *
                    cosine_alpha
            ) /
            (
                (
                    k_2 *
                        sine_phi +
                    k_1 *
                        cosine_phi
                ) *
                sine_alpha
            ),
            -1.0,
            1.0);
    const float sine_b_prime =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine_b_prime *
                        cosine_b_prime));
    const float3 c_prime =
        cosine_b_prime *
            a +
        sine_b_prime *
            normalize(
                c -
                dot(
                    c,
                    a) *
                    a);
    const float cosine_theta =
        1.0 -
        sample.y *
            (
                1.0 -
                dot(
                    c_prime,
                    b)
            );
    const float sine_theta =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine_theta *
                        cosine_theta));
    const float3 direction =
        cosine_theta *
            b +
        sine_theta *
            normalize(
                c_prime -
                dot(
                    c_prime,
                    b) *
                    b);
    const float3 edge_0 =
        vertex_1 -
        vertex_0;
    const float3 edge_1 =
        vertex_2 -
        vertex_0;
    const float3 s_1 =
        cross(
            direction,
            edge_1);
    const float divisor =
        dot(
            s_1,
            edge_0);
    if (divisor == 0.0)
        return float3(
            1.0 /
                3.0);
    const float inverse_divisor =
        1.0 /
        divisor;
    const float3 translated =
        position -
        vertex_0;
    float b_1 =
        clamp(
            dot(
                translated,
                s_1) *
                inverse_divisor,
            0.0,
            1.0);
    float b_2 =
        clamp(
            dot(
                direction,
                cross(
                    translated,
                    edge_0)) *
                inverse_divisor,
            0.0,
            1.0);
    if (
        b_1 +
            b_2 >
        1.0) {
        const float sum =
            b_1 +
            b_2;
        b_1 /= sum;
        b_2 /= sum;
    }
    return float3(
        1.0 -
            b_1 -
            b_2,
        b_1,
        b_2);
}

float2 invert_spherical_triangle_sample(
    float3 vertex_0,
    float3 vertex_1,
    float3 vertex_2,
    float3 position,
    float3 direction) {
    const float3 a =
        normalize(
            vertex_0 -
            position);
    const float3 b =
        normalize(
            vertex_1 -
            position);
    const float3 c =
        normalize(
            vertex_2 -
            position);
    const float3 n_ab =
        normalize(
            cross(
                a,
                b));
    const float3 n_bc =
        normalize(
            cross(
                b,
                c));
    const float3 n_ca =
        normalize(
            cross(
                c,
                a));
    const float alpha =
        angle_between(
            n_ab,
            -n_ca);
    const float beta =
        angle_between(
            n_bc,
            -n_ab);
    const float gamma =
        angle_between(
            n_ca,
            -n_bc);
    float3 c_prime =
        normalize(
            cross(
                cross(
                    b,
                    direction),
                cross(
                    c,
                    a)));
    if (
        dot(
            c_prime,
            a +
                c) < 0.0)
        c_prime =
            -c_prime;
    float first_sample = 0.0;
    if (
        dot(
            a,
            c_prime) <=
        0.99999847691) {
        const float3 n_cpb =
            normalize(
                cross(
                    c_prime,
                    b));
        const float3 n_acp =
            normalize(
                cross(
                    a,
                    c_prime));
        const float partial_area =
            alpha +
            angle_between(
                n_ab,
                n_cpb) +
            angle_between(
                n_acp,
                -n_cpb) -
            light_pi;
        first_sample =
            partial_area /
            (
                alpha +
                beta +
                gamma -
                light_pi
            );
    }
    const float second_sample =
        (
            1.0 -
            dot(
                direction,
                b)
        ) /
        (
            1.0 -
            dot(
                c_prime,
                b)
        );
    return
        clamp(
            float2(
                first_sample,
                second_sample),
            float2(0.0),
            float2(1.0));
}

public struct PathSampledLight {
    public uint index;
    public float pmf;
    public bool valid;
}

public struct PathLightLiSample {
    public float4 radiance;
    public float3 direction;
    public float pdf;
    public float3 position;
    public float3 normal;
    public float2 texture_coordinates;
    public bool delta;
    public bool valid;
}

struct PathShapeSample {
    float3 position;
    float3 normal;
    float2 texture_coordinates;
    float pdf;
}

struct PathShapeIntersection {
    float3 position;
    float3 normal;
    bool valid;
}

struct PathDistributionSample {
    float2 value;
    float pdf;
    bool valid;
}

float3 transform_light_point(
    PathLightShape shape,
    float3 position) {
    const float4 homogeneous =
        float4(position, 1.0);
    return float3(
        dot(
            shape.transform_row_0,
            homogeneous),
        dot(
            shape.transform_row_1,
            homogeneous),
        dot(
            shape.transform_row_2,
            homogeneous));
}

float3x3 light_linear_transform(
    PathLightShape shape) {
    return float3x3(
        shape.transform_row_0.xyz,
        shape.transform_row_1.xyz,
        shape.transform_row_2.xyz);
}

float light_area_pdf(
    PathLightShape shape,
    float3 world_normal) {
    if (
        shape.identifiers.z ==
        geometry_triangle)
        return
            1.0 /
            shape.position_0_and_area.w;
    const float3x3 linear =
        light_linear_transform(shape);
    return
        length(
            mul(
                transpose(linear),
                world_normal)) /
        (
            abs(determinant(linear)) *
            shape.position_0_and_area.w
    );
}

PathShapeSample sample_light_triangle(
    PathLightShape shape,
    float3 barycentric,
    float pdf) {
    const float3 position =
        barycentric.x *
            shape.position_0_and_area.xyz +
        barycentric.y *
            shape.position_1.xyz +
        barycentric.z *
            shape.position_2.xyz;
    float3 normal =
        normalize(
            cross(
                shape.position_1.xyz -
                    shape.position_0_and_area.xyz,
                shape.position_2.xyz -
                    shape.position_0_and_area.xyz));
    if (shape.normal_0.w != 0.0) {
        const float3 shading_normal =
            normalize(
                barycentric.x *
                    shape.normal_0.xyz +
                barycentric.y *
                    shape.normal_1.xyz +
                barycentric.z *
                    shape.normal_2.xyz);
        if (
            dot(
                normal,
                shading_normal) < 0.0)
            normal = -normal;
    }
    else if (shape.identifiers.y != 0)
        normal = -normal;
    return {
        position,
        normal,
        barycentric.x *
                shape.texture_coordinates_0_1.xy +
            barycentric.y *
                shape.texture_coordinates_0_1.zw +
            barycentric.z *
                shape.texture_coordinates_2.xy,
        pdf};
}

bool full_similarity_sphere(
    PathLightShape shape,
    out float3 center,
    out float radius,
    out float scale) {
    center =
        transform_light_point(
            shape,
            float3(0.0));
    radius = 0.0;
    scale = 0.0;
    const float object_radius =
        shape.geometry_parameters.x;
    const float parameter_tolerance =
        0.000001 *
        max(
            1.0,
            object_radius);
    if (
        shape.identifiers.z !=
            geometry_sphere ||
        abs(
            shape.geometry_parameters.y +
            object_radius) >
            parameter_tolerance ||
        abs(
            shape.geometry_parameters.z -
            object_radius) >
            parameter_tolerance ||
        abs(
            shape.geometry_parameters.w -
            2.0 *
                light_pi) >
            0.000001)
        return false;
    const float3 column_0 =
        float3(
            shape.transform_row_0.x,
            shape.transform_row_1.x,
            shape.transform_row_2.x);
    const float3 column_1 =
        float3(
            shape.transform_row_0.y,
            shape.transform_row_1.y,
            shape.transform_row_2.y);
    const float3 column_2 =
        float3(
            shape.transform_row_0.z,
            shape.transform_row_1.z,
            shape.transform_row_2.z);
    const float scale_squared_0 =
        dot(
            column_0,
            column_0);
    const float scale_squared_1 =
        dot(
            column_1,
            column_1);
    const float scale_squared_2 =
        dot(
            column_2,
            column_2);
    const float transform_tolerance =
        0.00001 *
        max(
            1.0,
            max(
                scale_squared_0,
                max(
                    scale_squared_1,
                    scale_squared_2)));
    if (
        scale_squared_0 == 0.0 ||
        abs(
            scale_squared_0 -
            scale_squared_1) >
            transform_tolerance ||
        abs(
            scale_squared_0 -
            scale_squared_2) >
            transform_tolerance ||
        abs(
            dot(
                column_0,
                column_1)) >
            transform_tolerance ||
        abs(
            dot(
                column_0,
                column_2)) >
            transform_tolerance ||
        abs(
            dot(
                column_1,
                column_2)) >
            transform_tolerance)
        return false;
    scale =
        sqrt(scale_squared_0);
    radius =
        object_radius *
        scale;
    return true;
}

float positive_light_phi(
    float3 position) {
    float phi =
        atan2(
            position.y,
            position.x);
    if (phi < 0.0)
        phi +=
            2.0 *
            light_pi;
    return phi;
}

PathShapeIntersection intersect_light_shape(
    PathLightShape shape,
    float3 origin,
    float3 direction) {
    if (
        shape.identifiers.z ==
        geometry_triangle) {
        const float3 edge_0 =
            shape.position_1.xyz -
            shape.position_0_and_area.xyz;
        const float3 edge_1 =
            shape.position_2.xyz -
            shape.position_0_and_area.xyz;
        const float3 p =
            cross(
                direction,
                edge_1);
        const float determinant_value =
            dot(
                edge_0,
                p);
        if (determinant_value == 0.0)
            return {
                float3(0.0),
                float3(0.0),
                false};
        const float inverse_determinant =
            1.0 /
            determinant_value;
        const float3 translated =
            origin -
            shape.position_0_and_area.xyz;
        const float u =
            dot(
                translated,
                p) *
            inverse_determinant;
        if (
            u < 0.0 ||
            u > 1.0)
            return {
                float3(0.0),
                float3(0.0),
                false};
        const float3 q =
            cross(
                translated,
                edge_0);
        const float v =
            dot(
                direction,
                q) *
            inverse_determinant;
        const float distance =
            dot(
                edge_1,
                q) *
            inverse_determinant;
        if (
            v < 0.0 ||
            u + v > 1.0 ||
            distance <= 0.0)
            return {
                float3(0.0),
                float3(0.0),
                false};
        float3 normal =
            normalize(
                cross(
                    edge_0,
                    edge_1));
        if (shape.identifiers.y != 0)
            normal = -normal;
        return {
            origin +
                distance *
                    direction,
            normal,
            true};
    }
    const float3x3 linear =
        light_linear_transform(shape);
    const float3 translation =
        float3(
            shape.transform_row_0.w,
            shape.transform_row_1.w,
            shape.transform_row_2.w);
    const float3 column_0 =
        float3(
            shape.transform_row_0.x,
            shape.transform_row_1.x,
            shape.transform_row_2.x);
    const float3 column_1 =
        float3(
            shape.transform_row_0.y,
            shape.transform_row_1.y,
            shape.transform_row_2.y);
    const float3 column_2 =
        float3(
            shape.transform_row_0.z,
            shape.transform_row_1.z,
            shape.transform_row_2.z);
    const float3 cofactor_0 =
        cross(
            column_1,
            column_2);
    const float3 cofactor_1 =
        cross(
            column_2,
            column_0);
    const float3 cofactor_2 =
        cross(
            column_0,
            column_1);
    const float inverse_determinant =
        1.0 /
        determinant(linear);
    const float3 object_origin =
        float3(
            dot(
                cofactor_0,
                origin -
                    translation),
            dot(
                cofactor_1,
                origin -
                    translation),
            dot(
                cofactor_2,
                origin -
                    translation)) *
        inverse_determinant;
    const float3 object_direction =
        float3(
            dot(
                cofactor_0,
                direction),
            dot(
                cofactor_1,
                direction),
            dot(
                cofactor_2,
                direction)) *
        inverse_determinant;
    float distance =
        1.0e30;
    float3 object_normal =
        float3(0.0);
    if (
        shape.identifiers.z ==
            geometry_sphere ||
        shape.identifiers.z ==
            geometry_cylinder) {
        const float a =
            shape.identifiers.z ==
                    geometry_sphere
                ? dot(
                      object_direction,
                      object_direction)
                : dot(
                      object_direction.xy,
                      object_direction.xy);
        const float half_b =
            shape.identifiers.z ==
                    geometry_sphere
                ? dot(
                      object_origin,
                      object_direction)
                : dot(
                      object_origin.xy,
                      object_direction.xy);
        const float c =
            shape.identifiers.z ==
                    geometry_sphere
                ? dot(
                      object_origin,
                      object_origin) -
                      shape.geometry_parameters.x *
                          shape.geometry_parameters.x
                : dot(
                      object_origin.xy,
                      object_origin.xy) -
                      shape.geometry_parameters.x *
                          shape.geometry_parameters.x;
        const float discriminant =
            half_b *
                half_b -
            a *
                c;
        if (discriminant < 0.0)
            return {
                float3(0.0),
                float3(0.0),
                false};
        const float root =
            sqrt(discriminant);
        const float candidates[2] = {
            (
                -half_b -
                root
            ) /
                a,
            (
                -half_b +
                root
            ) /
                a,
        };
        [ForceUnroll]
        for (uint candidate_index = 0;
             candidate_index != 2;
             ++candidate_index) {
            const float candidate =
                candidates[
                    candidate_index];
            if (
                candidate <= 0.0 ||
                candidate >= distance)
                continue;
            const float3 point =
                object_origin +
                candidate *
                    object_direction;
            const float phi =
                positive_light_phi(
                    point);
            if (
                point.z <
                    shape.geometry_parameters.y ||
                point.z >
                    shape.geometry_parameters.z ||
                phi >
                    shape.geometry_parameters.w)
                continue;
            distance = candidate;
            object_normal =
                shape.identifiers.z ==
                        geometry_sphere
                    ? normalize(point)
                    : normalize(
                          float3(
                              point.xy,
                              0.0));
        }
    }
    else if (
        shape.identifiers.z ==
        geometry_disk) {
        if (object_direction.z != 0.0) {
            const float candidate =
                (
                    shape.geometry_parameters.x -
                    object_origin.z
                ) /
                object_direction.z;
            const float3 point =
                object_origin +
                candidate *
                    object_direction;
            const float radius_squared =
                dot(
                    point.xy,
                    point.xy);
            if (
                candidate > 0.0 &&
                radius_squared <=
                    shape.geometry_parameters.y *
                        shape.geometry_parameters.y &&
                radius_squared >=
                    shape.geometry_parameters.z *
                        shape.geometry_parameters.z &&
                positive_light_phi(
                    point) <=
                    shape.geometry_parameters.w) {
                distance = candidate;
                object_normal =
                    float3(
                        0.0,
                        0.0,
                        1.0);
            }
        }
    }
    else if (
        shape.identifiers.z ==
        geometry_rectangle) {
        if (object_direction.z != 0.0) {
            const float candidate =
                -object_origin.z /
                object_direction.z;
            const float3 point =
                object_origin +
                candidate *
                    object_direction;
            if (
                candidate > 0.0 &&
                point.x >=
                    shape.position_0_and_area.x &&
                point.x <=
                    shape.position_1.x &&
                point.y >=
                    shape.position_0_and_area.y &&
                point.y <=
                    shape.position_1.y) {
                distance = candidate;
                object_normal =
                    float3(
                        0.0,
                        0.0,
                        1.0);
            }
        }
    }
    else if (
        shape.identifiers.z ==
        geometry_box) {
        const float3 minimum =
            shape.position_0_and_area.xyz;
        const float3 maximum =
            shape.position_1.xyz;
        const float3 inverse_direction =
            1.0 /
            object_direction;
        const float3 first =
            (
                minimum -
                object_origin
            ) *
            inverse_direction;
        const float3 second =
            (
                maximum -
                object_origin
            ) *
            inverse_direction;
        const float3 near_values =
            min(
                first,
                second);
        const float3 far_values =
            max(
                first,
                second);
        const float near_distance =
            max(
                near_values.x,
                max(
                    near_values.y,
                    near_values.z));
        const float far_distance =
            min(
                far_values.x,
                min(
                    far_values.y,
                    far_values.z));
        if (
            far_distance >=
                max(
                    near_distance,
                    0.0)) {
            distance =
                near_distance > 0.0
                    ? near_distance
                    : far_distance;
            const float3 point =
                object_origin +
                distance *
                    object_direction;
            const float3 distances =
                min(
                    abs(
                        point -
                        minimum),
                    abs(
                        point -
                        maximum));
            if (
                distances.x <=
                    distances.y &&
                distances.x <=
                    distances.z)
                object_normal =
                    float3(
                        point.x <
                                (
                                    minimum.x +
                                    maximum.x
                                ) *
                                    0.5
                            ? -1.0
                            : 1.0,
                        0.0,
                        0.0);
            else if (
                distances.y <=
                distances.z)
                object_normal =
                    float3(
                        0.0,
                        point.y <
                                (
                                    minimum.y +
                                    maximum.y
                                ) *
                                    0.5
                            ? -1.0
                            : 1.0,
                        0.0);
            else
                object_normal =
                    float3(
                        0.0,
                        0.0,
                        point.z <
                                (
                                    minimum.z +
                                    maximum.z
                                ) *
                                    0.5
                            ? -1.0
                            : 1.0);
        }
    }
    if (distance == 1.0e30)
        return {
            float3(0.0),
            float3(0.0),
            false};
    float3 world_normal =
        normalize(
            (
                cofactor_0 *
                    object_normal.x +
                cofactor_1 *
                    object_normal.y +
                cofactor_2 *
                    object_normal.z
            ) *
            inverse_determinant);
    if (shape.identifiers.y != 0)
        world_normal =
            -world_normal;
    return {
        origin +
            distance *
                direction,
        world_normal,
        true};
}

PathShapeSample sample_light_shape(
    PathLightShape shape,
    float2 sample) {
    if (
        shape.identifiers.z ==
        geometry_triangle) {
        const float square_root =
            sqrt(sample.x);
        const float3 barycentric =
            float3(
                1.0 - square_root,
                square_root *
                    (1.0 - sample.y),
                square_root *
                    sample.y);
        return
            sample_light_triangle(
                shape,
                barycentric,
                1.0 /
                    shape
                        .position_0_and_area.w);
    }
    float3 object_position;
    float3 object_normal;
    float2 uv;
    if (
        shape.identifiers.z ==
        geometry_sphere) {
        const float z =
            lerp(
                shape.geometry_parameters.y,
                shape.geometry_parameters.z,
                sample.x);
        const float phi =
            shape.geometry_parameters.w *
            sample.y;
        const float radial =
            sqrt(
                max(
                    0.0,
                    shape.geometry_parameters.x *
                            shape.geometry_parameters.x -
                        z * z));
        object_position =
            float3(
                radial * cos(phi),
                radial * sin(phi),
                z);
        object_normal =
            object_position /
            shape.geometry_parameters.x;
        const float theta =
            acos(
                clamp(
                    z /
                        shape.geometry_parameters.x,
                    -1.0,
                    1.0));
        const float theta_min =
            acos(
                clamp(
                    shape.geometry_parameters.y /
                        shape.geometry_parameters.x,
                    -1.0,
                    1.0));
        const float theta_max =
            acos(
                clamp(
                    shape.geometry_parameters.z /
                        shape.geometry_parameters.x,
                    -1.0,
                    1.0));
        uv =
            float2(
                phi /
                    shape.geometry_parameters.w,
                (
                    theta -
                    theta_min
                ) /
                (
                    theta_max -
                    theta_min
                ));
    }
    else if (
        shape.identifiers.z ==
        geometry_disk) {
        const float radius =
            sqrt(
                lerp(
                    shape.geometry_parameters.z *
                        shape.geometry_parameters.z,
                    shape.geometry_parameters.y *
                        shape.geometry_parameters.y,
                    sample.x));
        const float phi =
            shape.geometry_parameters.w *
            sample.y;
        object_position =
            float3(
                radius * cos(phi),
                radius * sin(phi),
                shape.geometry_parameters.x);
        object_normal =
            float3(0.0, 0.0, 1.0);
        uv =
            float2(
                phi /
                    shape.geometry_parameters.w,
                (
                    shape.geometry_parameters.y -
                    radius
                ) /
                (
                    shape.geometry_parameters.y -
                    shape.geometry_parameters.z
                ));
    }
    else if (
        shape.identifiers.z ==
        geometry_cylinder) {
        const float phi =
            shape.geometry_parameters.w *
            sample.x;
        object_position =
            float3(
                shape.geometry_parameters.x *
                    cos(phi),
                shape.geometry_parameters.x *
                    sin(phi),
                lerp(
                    shape.geometry_parameters.y,
                    shape.geometry_parameters.z,
                    sample.y));
        object_normal =
            float3(
                cos(phi),
                sin(phi),
                0.0);
        uv = sample;
    }
    else if (
        shape.identifiers.z ==
        geometry_rectangle) {
        object_position =
            float3(
                lerp(
                    shape.position_0_and_area.x,
                    shape.position_1.x,
                    sample.x),
                lerp(
                    shape.position_0_and_area.y,
                    shape.position_1.y,
                    sample.y),
                0.0);
        object_normal =
            float3(0.0, 0.0, 1.0);
        uv = sample;
    }
    else if (
        shape.identifiers.z ==
        geometry_box) {
        const float3 minimum =
            shape.position_0_and_area.xyz;
        const float3 maximum =
            shape.position_1.xyz;
        const float3 extent =
            maximum - minimum;
        const float face_areas[6] = {
            extent.x * extent.y,
            extent.x * extent.y,
            extent.x * extent.z,
            extent.x * extent.z,
            extent.y * extent.z,
            extent.y * extent.z,
        };
        float target =
            sample.x *
            shape.position_0_and_area.w;
        uint face = 0;
        while (
            face < 5 &&
            target > face_areas[face]) {
            target -= face_areas[face];
            ++face;
        }
        const float coordinate =
            target /
            face_areas[face];
        uv =
            float2(
                coordinate,
                sample.y);
        if (face == 0) {
            object_position =
                float3(
                    lerp(
                        minimum.x,
                        maximum.x,
                        coordinate),
                    lerp(
                        minimum.y,
                        maximum.y,
                        sample.y),
                    minimum.z);
            object_normal =
                float3(0.0, 0.0, -1.0);
        }
        else if (face == 1) {
            object_position =
                float3(
                    lerp(
                        minimum.x,
                        maximum.x,
                        coordinate),
                    lerp(
                        minimum.y,
                        maximum.y,
                        sample.y),
                    maximum.z);
            object_normal =
                float3(0.0, 0.0, 1.0);
        }
        else if (face == 2) {
            object_position =
                float3(
                    lerp(
                        minimum.x,
                        maximum.x,
                        coordinate),
                    minimum.y,
                    lerp(
                        minimum.z,
                        maximum.z,
                        sample.y));
            object_normal =
                float3(0.0, -1.0, 0.0);
        }
        else if (face == 3) {
            object_position =
                float3(
                    lerp(
                        minimum.x,
                        maximum.x,
                        coordinate),
                    maximum.y,
                    lerp(
                        minimum.z,
                        maximum.z,
                        sample.y));
            object_normal =
                float3(0.0, 1.0, 0.0);
        }
        else if (face == 4) {
            object_position =
                float3(
                    minimum.x,
                    lerp(
                        minimum.y,
                        maximum.y,
                        coordinate),
                    lerp(
                        minimum.z,
                        maximum.z,
                        sample.y));
            object_normal =
                float3(-1.0, 0.0, 0.0);
        }
        else {
            object_position =
                float3(
                    maximum.x,
                    lerp(
                        minimum.y,
                        maximum.y,
                        coordinate),
                    lerp(
                        minimum.z,
                        maximum.z,
                        sample.y));
            object_normal =
                float3(1.0, 0.0, 0.0);
        }
    }
    const float3x3 linear =
        light_linear_transform(shape);
    const float3 column_0 =
        float3(
            shape.transform_row_0.x,
            shape.transform_row_1.x,
            shape.transform_row_2.x);
    const float3 column_1 =
        float3(
            shape.transform_row_0.y,
            shape.transform_row_1.y,
            shape.transform_row_2.y);
    const float3 column_2 =
        float3(
            shape.transform_row_0.z,
            shape.transform_row_1.z,
            shape.transform_row_2.z);
    float3 world_normal =
        normalize(
            (
                cross(
                    column_1,
                    column_2) *
                    object_normal.x +
                cross(
                    column_2,
                    column_0) *
                    object_normal.y +
                cross(
                    column_0,
                    column_1) *
                    object_normal.z
            ) /
            determinant(linear));
    if (shape.identifiers.y != 0)
        world_normal = -world_normal;
    return {
        transform_light_point(
            shape,
            object_position),
        world_normal,
        uv,
        light_area_pdf(
            shape,
            world_normal)};
}

PathShapeSample sample_light_shape_from_context(
    PathLightShape shape,
    float3 position,
    float3 normal,
    float2 sample) {
    if (
        shape.identifiers.z ==
        geometry_triangle) {
        const float3 vector_0 =
            shape.position_0_and_area.xyz -
            position;
        const float3 vector_1 =
            shape.position_1.xyz -
            position;
        const float3 vector_2 =
            shape.position_2.xyz -
            position;
        if (
            dot(
                vector_0,
                vector_0) == 0.0 ||
            dot(
                vector_1,
                vector_1) == 0.0 ||
            dot(
                vector_2,
                vector_2) == 0.0)
            return PathShapeSample();
        const float3 direction_0 =
            normalize(vector_0);
        const float3 direction_1 =
            normalize(vector_1);
        const float3 direction_2 =
            normalize(vector_2);
        const float solid_angle =
            spherical_triangle_area(
                direction_0,
                direction_1,
                direction_2);
        if (
            solid_angle >=
                0.0003 &&
            solid_angle <=
                6.22) {
            float pdf_scale = 1.0;
            float2 warped_sample =
                sample;
            if (
                dot(
                    normal,
                    normal) != 0.0) {
                const float4 weights =
                    float4(
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_1))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_1))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_0))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_2))));
                warped_sample =
                    sample_bilinear(
                        sample,
                        weights);
                pdf_scale =
                    bilinear_pdf(
                        warped_sample,
                        weights);
            }
            float triangle_pdf;
            const float3 barycentric =
                sample_spherical_triangle(
                    shape
                        .position_0_and_area.xyz,
                    shape.position_1.xyz,
                    shape.position_2.xyz,
                    position,
                    warped_sample,
                    triangle_pdf);
            if (triangle_pdf == 0.0)
                return PathShapeSample();
            return
                sample_light_triangle(
                    shape,
                    barycentric,
                    pdf_scale *
                        triangle_pdf);
        }
    }
    if (
        shape.identifiers.z ==
        geometry_sphere) {
        float3 center;
        float world_radius;
        float scale;
        if (
            full_similarity_sphere(
                shape,
                center,
                world_radius,
                scale)) {
            float3 offset_position =
                position;
            if (
                dot(
                    normal,
                    normal) != 0.0)
                offset_position =
                    offset_ray_origin(
                        position,
                        dot(
                            center -
                                position,
                            normal) < 0.0
                            ? -normal
                            : normal);
            const float3 center_vector =
                center -
                position;
            const float distance_squared =
                dot(
                    center_vector,
                    center_vector);
            if (
                dot(
                    offset_position -
                        center,
                    offset_position -
                        center) >
                world_radius *
                    world_radius) {
                const float sine_theta_max =
                    world_radius /
                    sqrt(distance_squared);
                const float sine_squared_theta_max =
                    sine_theta_max *
                    sine_theta_max;
                float cosine_theta_max =
                    sqrt(
                        max(
                            0.0,
                            1.0 -
                                sine_squared_theta_max));
                float one_minus_cosine_theta_max =
                    1.0 -
                    cosine_theta_max;
                float cosine_theta =
                    (
                        cosine_theta_max -
                        1.0
                    ) *
                        sample.x +
                    1.0;
                float sine_squared_theta =
                    1.0 -
                    cosine_theta *
                        cosine_theta;
                if (
                    sine_squared_theta_max <
                    0.00068523) {
                    sine_squared_theta =
                        sine_squared_theta_max *
                        sample.x;
                    cosine_theta =
                        sqrt(
                            max(
                                0.0,
                                1.0 -
                                    sine_squared_theta));
                    one_minus_cosine_theta_max =
                        sine_squared_theta_max *
                        0.5;
                }
                const float cosine_alpha =
                    sine_squared_theta /
                        sine_theta_max +
                    cosine_theta *
                        sqrt(
                            max(
                                0.0,
                                1.0 -
                                    sine_squared_theta /
                                        sine_squared_theta_max));
                const float sine_alpha =
                    sqrt(
                        max(
                            0.0,
                            1.0 -
                                cosine_alpha *
                                    cosine_alpha));
                const float phi =
                    sample.y *
                    2.0 *
                    light_pi;
                const float3 local_normal =
                    float3(
                        sine_alpha *
                            cos(phi),
                        sine_alpha *
                            sin(phi),
                        cosine_alpha);
                const float3 frame_z =
                    normalize(
                        center_vector);
                float3 frame_x;
                float3 frame_y;
                frame_from_z(
                    frame_z,
                    frame_x,
                    frame_y);
                const float3 outward_normal =
                    -normalize(
                        frame_x *
                                local_normal.x +
                            frame_y *
                                local_normal.y +
                            frame_z *
                                local_normal.z);
                const float3 column_0 =
                    float3(
                        shape.transform_row_0.x,
                        shape.transform_row_1.x,
                        shape.transform_row_2.x);
                const float3 column_1 =
                    float3(
                        shape.transform_row_0.y,
                        shape.transform_row_1.y,
                        shape.transform_row_2.y);
                const float3 column_2 =
                    float3(
                        shape.transform_row_0.z,
                        shape.transform_row_1.z,
                        shape.transform_row_2.z);
                const float3 object_normal =
                    float3(
                        dot(
                            outward_normal,
                            column_0),
                        dot(
                            outward_normal,
                            column_1),
                        dot(
                            outward_normal,
                            column_2)) /
                    scale;
                float sphere_phi =
                    atan2(
                        object_normal.y,
                        object_normal.x);
                if (sphere_phi < 0.0)
                    sphere_phi +=
                        2.0 *
                        light_pi;
                const float theta =
                    acos(
                        clamp(
                            object_normal.z,
                            -1.0,
                            1.0));
                return {
                    center +
                        world_radius *
                            outward_normal,
                    shape.identifiers.y != 0
                        ? -outward_normal
                        : outward_normal,
                    float2(
                        sphere_phi /
                            shape
                                .geometry_parameters.w,
                        (
                            theta -
                            light_pi
                        ) /
                        -light_pi),
                    1.0 /
                        (
                            2.0 *
                            light_pi *
                            one_minus_cosine_theta_max
                        )};
            }
        }
    }
    PathShapeSample result =
        sample_light_shape(
            shape,
            sample);
    const float3 vector =
        result.position -
        position;
    const float distance_squared =
        dot(
            vector,
            vector);
    if (distance_squared == 0.0) {
        result.pdf = 0.0;
        return result;
    }
    const float3 direction =
        vector /
        sqrt(distance_squared);
    const float cosine =
        abs(
            dot(
                result.normal,
                -direction));
    result.pdf =
        cosine == 0.0
            ? 0.0
            : result.pdf *
                  distance_squared /
                  cosine;
    if (isinf(result.pdf))
        result.pdf = 0.0;
    return result;
}

float light_shape_pdf_from_context(
    PathLightShape shape,
    float3 position,
    float3 normal,
    float3 direction) {
    if (
        shape.identifiers.z ==
        geometry_triangle) {
        const float3 vector_0 =
            shape.position_0_and_area.xyz -
            position;
        const float3 vector_1 =
            shape.position_1.xyz -
            position;
        const float3 vector_2 =
            shape.position_2.xyz -
            position;
        if (
            dot(
                vector_0,
                vector_0) == 0.0 ||
            dot(
                vector_1,
                vector_1) == 0.0 ||
            dot(
                vector_2,
                vector_2) == 0.0)
            return 0.0;
        const float3 direction_0 =
            normalize(vector_0);
        const float3 direction_1 =
            normalize(vector_1);
        const float3 direction_2 =
            normalize(vector_2);
        const float solid_angle =
            spherical_triangle_area(
                direction_0,
                direction_1,
                direction_2);
        if (
            solid_angle >=
                0.0003 &&
            solid_angle <=
                6.22) {
            float pdf =
                1.0 /
                solid_angle;
            if (
                dot(
                    normal,
                    normal) != 0.0) {
                const float4 weights =
                    float4(
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_1))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_1))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_0))),
                        max(
                            0.01,
                            abs(
                                dot(
                                    normal,
                                    direction_2))));
                pdf *=
                    bilinear_pdf(
                        invert_spherical_triangle_sample(
                            shape
                                .position_0_and_area.xyz,
                            shape
                                .position_1.xyz,
                            shape
                                .position_2.xyz,
                            position,
                            direction),
                        weights);
            }
            return pdf;
        }
    }
    if (
        shape.identifiers.z ==
        geometry_sphere) {
        float3 center;
        float world_radius;
        float scale;
        if (
            full_similarity_sphere(
                shape,
                center,
                world_radius,
                scale)) {
            float3 offset_position =
                position;
            if (
                dot(
                    normal,
                    normal) != 0.0)
                offset_position =
                    offset_ray_origin(
                        position,
                        dot(
                            center -
                                position,
                            normal) < 0.0
                            ? -normal
                            : normal);
            if (
                dot(
                    offset_position -
                        center,
                    offset_position -
                        center) >
                world_radius *
                    world_radius) {
                const float distance_squared =
                    dot(
                        center -
                            position,
                        center -
                            position);
                const float sine_squared_theta_max =
                    world_radius *
                        world_radius /
                    distance_squared;
                float one_minus_cosine_theta_max =
                    1.0 -
                    sqrt(
                        max(
                            0.0,
                            1.0 -
                                sine_squared_theta_max));
                if (
                    sine_squared_theta_max <
                    0.00068523)
                    one_minus_cosine_theta_max =
                        sine_squared_theta_max *
                        0.5;
                return
                    1.0 /
                    (
                        2.0 *
                        light_pi *
                        one_minus_cosine_theta_max
                    );
            }
        }
    }
    const PathShapeIntersection intersection =
        intersect_light_shape(
            shape,
            position,
            direction);
    if (!intersection.valid)
        return 0.0;
    const float cosine =
        abs(
            dot(
                intersection.normal,
                -direction));
    const float pdf =
        cosine == 0.0
            ? 0.0
            : light_area_pdf(
                  shape,
                  intersection.normal) *
                  dot(
                      intersection.position -
                          position,
                      intersection.position -
                          position) /
                  cosine;
    return
        isinf(pdf)
            ? 0.0
            : pdf;
}

bool area_light_alpha_masked(
    PathSceneBindings bindings,
    PathLightShape shape,
    PathShapeSample sample,
    DescriptorHandle<
        RWStructuredBuffer<float4>>
            texture_stack,
    uint texture_base) {
    if (
        shape.identifiers.w ==
        invalid_light_index)
        return false;
    const TextureEvaluationContext context = {
        sample.position,
        sample.normal,
        sample.texture_coordinates,
        float3(0.0),
        float3(0.0),
        float2(0.0),
        float2(0.0),
        uint2(0),
        texture_stack,
        texture_base,
        uint2(0),
    };
    const float alpha =
        evaluate_texture_program(
            bindings,
            shape.identifiers.w,
            context,
            float4(0.0),
            texture_stack,
            texture_base)
            .x;
    if (alpha >= 1.0)
        return false;
    if (alpha <= 0.0)
        return true;
    return
        hash_float_3(
            asuint(
                sample.position.x),
            asuint(
                sample.position.y),
            asuint(
                sample.position.z)) >
        alpha;
}

uint sample_cdf(
    StructuredBuffer<float> data,
    uint offset,
    uint count,
    float sample,
    out float remapped) {
    uint first = 0;
    uint last = count;
    while (first + 1 < last) {
        const uint middle =
            (first + last) / 2;
        if (
            data[offset + middle] <=
            sample)
            first = middle;
        else
            last = middle;
    }
    const float beginning =
        data[offset + first];
    const float ending =
        data[offset + first + 1];
    remapped =
        ending > beginning
            ? (
                  sample -
                  beginning
              ) /
              (
                  ending -
                  beginning
              )
            : 0.0;
    return min(first, count - 1);
}

PathDistributionSample sample_distribution(
    PathSceneBindings bindings,
    uint distribution_index,
    float2 sample) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    float v_offset;
    const uint v =
        sample_cdf(
            data,
            distribution.offsets.x,
            distribution.metadata.y,
            sample.y,
            v_offset);
    float u_offset;
    const uint u =
        sample_cdf(
            data,
            distribution.metadata.w +
                v *
                    (
                        distribution.metadata.x +
                        1
                    ),
            distribution.metadata.x,
            sample.x,
            u_offset);
    const float function =
        data[
            distribution.metadata.z +
            v * distribution.metadata.x +
            u];
    return {
        (
            float2(
                float(u),
                float(v)) +
            float2(
                u_offset,
                v_offset)
        ) /
            float2(
                float(distribution.metadata.x),
                float(distribution.metadata.y)),
        distribution.parameters.x > 0.0
            ? function /
                  distribution.parameters.x
            : 0.0,
        distribution.parameters.x > 0.0};
}

float distribution_pdf(
    PathSceneBindings bindings,
    uint distribution_index,
    float2 value) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    const uint2 coordinate =
        min(
            uint2(
                value *
                float2(
                    float(distribution.metadata.x),
                    float(distribution.metadata.y))),
            distribution.metadata.xy -
                uint2(1));
    return
        distribution.parameters.x > 0.0
            ? data[
                  distribution.metadata.z +
                  coordinate.y *
                      distribution.metadata.x +
                  coordinate.x] /
                  distribution.parameters.x
            : 0.0;
}

float summed_area_lookup(
    StructuredBuffer<float> data,
    PathLightDistribution distribution,
    float2 value) {
    const float2 coordinate =
        value *
        float2(
            float(distribution.metadata.x),
            float(distribution.metadata.y));
    const int2 first =
        int2(coordinate);
    const float2 offset =
        coordinate -
        float2(first);
    float values[4];
    [ForceUnroll]
    for (uint corner = 0;
         corner != 4;
         ++corner) {
        int2 p =
            first +
            int2(
                corner & 1,
                corner >> 1);
        if (p.x == 0 || p.y == 0)
            values[corner] = 0.0;
        else {
            p -= int2(1);
            p =
                min(
                    p,
                    int2(
                        int(distribution.metadata.x) -
                            1,
                        int(distribution.metadata.y) -
                            1));
            values[corner] =
                data[
                    distribution.offsets.y +
                    uint(p.y) *
                        distribution.metadata.x +
                    uint(p.x)];
        }
    }
    return
        lerp(
            lerp(
                values[0],
                values[1],
                offset.x),
            lerp(
                values[2],
                values[3],
                offset.x),
            offset.y);
}

float window_integral(
    PathSceneBindings bindings,
    uint distribution_index,
    float4 bounds) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    return
        max(
            0.0,
            (
                summed_area_lookup(
                    data,
                    distribution,
                    bounds.zw) -
                summed_area_lookup(
                    data,
                    distribution,
                    float2(
                        bounds.x,
                        bounds.w)) +
                summed_area_lookup(
                    data,
                    distribution,
                    bounds.xy) -
                summed_area_lookup(
                    data,
                    distribution,
                    float2(
                        bounds.z,
                        bounds.y))
            ) /
            (
                float(distribution.metadata.x) *
                float(distribution.metadata.y)
            ));
}

float sample_window_axis(
    PathSceneBindings bindings,
    uint distribution_index,
    float4 bounds,
    float sample,
    bool x_axis) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    const float integral =
        window_integral(
            bindings,
            distribution_index,
            bounds);
    float minimum =
        x_axis
            ? bounds.x
            : bounds.y;
    float maximum =
        x_axis
            ? bounds.z
            : bounds.w;
    while (
        ceil(
            float(
                x_axis
                    ? distribution.metadata.x
                    : distribution.metadata.y) *
            maximum) -
            floor(
                float(
                    x_axis
                        ? distribution.metadata.x
                        : distribution.metadata.y) *
                minimum) >
        1.0) {
        const float middle =
            (
                minimum +
                maximum
            ) *
            0.5;
        float4 partial = bounds;
        if (x_axis)
            partial.z = middle;
        else
            partial.w = middle;
        if (
            window_integral(
                bindings,
                distribution_index,
                partial) /
                integral >
            sample)
            maximum = middle;
        else
            minimum = middle;
    }
    float4 lower = bounds;
    float4 upper = bounds;
    if (x_axis) {
        lower.z = minimum;
        upper.z = maximum;
    }
    else {
        lower.w = minimum;
        upper.w = maximum;
    }
    const float first =
        window_integral(
            bindings,
            distribution_index,
            lower) /
        integral;
    const float second =
        window_integral(
            bindings,
            distribution_index,
            upper) /
        integral;
    return clamp(
        lerp(
            minimum,
            maximum,
            (
                sample -
                first
            ) /
            (
                second -
                first
            )),
        minimum,
        maximum);
}

PathDistributionSample sample_window_distribution(
    PathSceneBindings bindings,
    uint distribution_index,
    float4 bounds,
    float2 sample) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    const float integral =
        window_integral(
            bindings,
            distribution_index,
            bounds);
    if (integral == 0.0)
        return {
            float2(0.0),
            0.0,
            false};
    float2 value;
    value.x =
        sample_window_axis(
            bindings,
            distribution_index,
            bounds,
            sample.x,
            true);
    float4 conditional_bounds =
        float4(
            floor(
                value.x *
                float(distribution.metadata.x)) /
                float(distribution.metadata.x),
            bounds.y,
            ceil(
                value.x *
                float(distribution.metadata.x)) /
                float(distribution.metadata.x),
            bounds.w);
    if (
        conditional_bounds.x ==
        conditional_bounds.z)
        conditional_bounds.z +=
            1.0 /
            float(distribution.metadata.x);
    value.y =
        sample_window_axis(
            bindings,
            distribution_index,
            conditional_bounds,
            sample.y,
            false);
    const uint2 coordinate =
        min(
            uint2(
                value *
                float2(
                    float(distribution.metadata.x),
                    float(distribution.metadata.y))),
            distribution.metadata.xy -
                uint2(1));
    return {
        value,
        data[
            distribution.metadata.z +
            coordinate.y *
                distribution.metadata.x +
            coordinate.x] /
            integral,
        true};
}

float window_distribution_pdf(
    PathSceneBindings bindings,
    uint distribution_index,
    float4 bounds,
    float2 value) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[distribution_index];
    const float integral =
        window_integral(
            bindings,
            distribution_index,
            bounds);
    if (integral == 0.0)
        return 0.0;
    const uint2 coordinate =
        min(
            uint2(
                value *
                float2(
                    float(distribution.metadata.x),
                    float(distribution.metadata.y))),
            distribution.metadata.xy -
                uint2(1));
    return
        data[
            distribution.metadata.z +
            coordinate.y *
                distribution.metadata.x +
            coordinate.x] /
        integral;
}

float3 equal_area_square_to_sphere(
    float2 point) {
    const float2 uv =
        2.0 * point -
        1.0;
    const float2 absolute =
        abs(uv);
    const float signed_distance =
        1.0 -
        (
            absolute.x +
            absolute.y
        );
    const float radius =
        1.0 -
        abs(signed_distance);
    const float phi =
        (
            radius == 0.0
                ? 1.0
                : (
                      absolute.y -
                      absolute.x
                  ) /
                      radius +
                    1.0
        ) *
        light_pi /
        4.0;
    const float z =
        sign(signed_distance) *
        (
            1.0 -
            radius * radius
        );
    return float3(
        sign(uv.x) *
            cos(phi) *
            radius *
            sqrt(
                max(
                    0.0,
                    2.0 -
                        radius *
                            radius)),
        sign(uv.y) *
            sin(phi) *
            radius *
            sqrt(
                max(
                    0.0,
                    2.0 -
                        radius *
                            radius)),
        z);
}

float2 equal_area_sphere_to_square(
    float3 direction) {
    direction = normalize(direction);
    const float3 absolute =
        abs(direction);
    const float radius =
        sqrt(
            max(
                0.0,
                1.0 -
                    absolute.z));
    const float a =
        max(
            absolute.x,
            absolute.y);
    const float b =
        a == 0.0
            ? 0.0
            : min(
                  absolute.x,
                  absolute.y) /
                  a;
    float phi =
        0.4067585662467884896e-5 +
        b *
            (
                0.6362265452740161349 +
                b *
                    (
                        0.6157201789828021349e-2 +
                        b *
                            (
                                -0.2473337332812689442 +
                                b *
                                    (
                                        0.8817706647753162947e-1 +
                                        b *
                                            (
                                                0.4190388180291657359e-1 +
                                                b *
                                                    -0.2513909723434835093e-1
                                            )
                                    )
                            )
                    )
            );
    if (absolute.x < absolute.y)
        phi = 1.0 - phi;
    float v = phi * radius;
    float u = radius - v;
    if (direction.z < 0.0) {
        const float swap = u;
        u = 1.0 - v;
        v = 1.0 - swap;
    }
    u = sign(direction.x) * u;
    v = sign(direction.y) * v;
    return
        0.5 *
        (
            float2(u, v) +
            1.0
        );
}

float3 transform_light_vector(
    PathLight light,
    float3 vector) {
    return float3(
        dot(
            light.transform_row_0.xyz,
            vector),
        dot(
            light.transform_row_1.xyz,
            vector),
        dot(
            light.transform_row_2.xyz,
            vector));
}

float3 inverse_transform_light_vector(
    PathLight light,
    float3 vector) {
    return float3(
        dot(
            light.inverse_transform_row_0.xyz,
            vector),
        dot(
            light.inverse_transform_row_1.xyz,
            vector),
        dot(
            light.inverse_transform_row_2.xyz,
            vector));
}

float4 evaluate_untextured_light(
    PathSceneBindings bindings,
    PathLight light,
    float4 wavelengths) {
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise =
        bindings.piecewise_spectra;
    return
        evaluate_spectrum(
            spectra[
                light.identifiers.z],
            piecewise,
            bindings.cie_spectra,
            wavelengths) *
        light.position_and_scale.w;
}

public float4 evaluate_area_light(
    PathSceneBindings bindings,
    PathLight light,
    float3 position,
    float3 normal,
    float2 texture_coordinates,
    float3 outgoing,
    float4 wavelengths,
    DescriptorHandle<
        RWStructuredBuffer<float4>>
            texture_stack,
    uint texture_base) {
    if (
        (
            light.identifiers.y &
            light_two_sided
        ) == 0 &&
        dot(
            normal,
            outgoing) <
            0.0)
        return float4(0.0);
    StructuredBuffer<PathLightShape> shapes =
        bindings.light_shapes;
    if (
        area_light_alpha_masked(
            bindings,
            shapes[light.references.x],
            {
                position,
                normal,
                texture_coordinates,
                0.0},
            texture_stack,
            texture_base))
        return float4(0.0);
    if (
        (
            light.identifiers.y &
            light_textured
        ) == 0)
        return
            evaluate_untextured_light(
                bindings,
                light,
                wavelengths);
    const TextureEvaluationContext context = {
        position,
        normal,
        texture_coordinates,
        float3(0.0),
        float3(0.0),
        float2(0.0),
        float2(0.0),
        uint2(0),
        texture_stack,
        0,
        uint2(0),
    };
    return
        evaluate_texture_program(
            bindings,
            light.identifiers.w,
            context,
            wavelengths,
            texture_stack,
            texture_base) *
        light.position_and_scale.w;
}

float spot_falloff(
    PathLight light,
    float3 direction_from_light) {
    const float3 local =
        normalize(
            inverse_transform_light_vector(
                light,
                direction_from_light));
    return smoothstep(
        light.parameters.y,
        light.parameters.x,
        local.z);
}

bool portal_image_from_render(
    PathPortal portal,
    float3 direction,
    out float2 uv,
    out float jacobian) {
    uv = float2(0.0);
    jacobian = 0.0;
    const float3 local =
        float3(
            dot(
                direction,
                portal.frame_x.xyz),
            dot(
                direction,
                portal.frame_y.xyz),
            dot(
                direction,
                portal.frame_z.xyz));
    if (local.z <= 0.0)
        return false;
    jacobian =
        light_pi *
        light_pi *
        (
            1.0 -
            local.x * local.x
        ) *
        (
            1.0 -
            local.y * local.y
        ) /
        local.z;
    uv =
        clamp(
            float2(
                (
                    atan2(
                        local.x,
                        local.z) +
                    light_pi /
                        2.0
                ) /
                    light_pi,
                (
                    atan2(
                        local.y,
                        local.z) +
                    light_pi /
                        2.0
                ) /
                    light_pi),
            0.0,
            1.0);
    return true;
}

float3 portal_render_from_image(
    PathPortal portal,
    float2 uv,
    out float jacobian) {
    const float alpha =
        -light_pi /
            2.0 +
        uv.x *
            light_pi;
    const float beta =
        -light_pi /
            2.0 +
        uv.y *
            light_pi;
    const float3 local =
        normalize(
            float3(
                tan(alpha),
                tan(beta),
                1.0));
    jacobian =
        light_pi *
        light_pi *
        (
            1.0 -
            local.x * local.x
        ) *
        (
            1.0 -
            local.y * local.y
        ) /
        local.z;
    return
        local.x *
            portal.frame_x.xyz +
        local.y *
            portal.frame_y.xyz +
        local.z *
            portal.frame_z.xyz;
}

bool portal_image_bounds(
    PathPortal portal,
    float3 position,
    out float4 bounds) {
    bounds = float4(0.0);
    float2 first;
    float2 second;
    float ignored;
    if (
        !portal_image_from_render(
            portal,
            normalize(
                portal.point_0.xyz -
                position),
            first,
            ignored) ||
        !portal_image_from_render(
            portal,
            normalize(
                portal.point_2.xyz -
                position),
            second,
            ignored))
        return false;
    bounds =
        float4(
            min(first, second),
            max(first, second));
    return true;
}

float4 evaluate_portal_light_image(
    PathSceneBindings bindings,
    PathLight light,
    float2 uv,
    float4 wavelengths) {
    StructuredBuffer<PathLightDistribution>
        distributions =
            bindings.light_distributions;
    StructuredBuffer<float> data =
        bindings.light_distribution_data;
    const PathLightDistribution distribution =
        distributions[light.references.y];
    const uint2 resolution =
        distribution.metadata.xy;
    const uint2 pixel =
        min(
            uint2(
                uv *
                float2(resolution)),
            resolution - 1);
    const uint offset =
        distribution.offsets.z +
        (
            pixel.y *
                resolution.x +
            pixel.x
        ) *
            4;
    const float4 coefficients =
        float4(
            data[offset],
            data[offset + 1],
            data[offset + 2],
            data[offset + 3]);
    return
        evaluate_rgb_polynomial(
            coefficients.xyz,
            coefficients.w *
                distribution.parameters.z *
                light.position_and_scale.w,
            2,
            float(
                distribution.offsets.w),
            bindings.cie_spectra,
            wavelengths);
}

float4 evaluate_infinite_light(
    PathSceneBindings bindings,
    PathLight light,
    float3 origin,
    float3 direction,
    float4 wavelengths) {
    if (
        (
            light.identifiers.y &
            light_textured
        ) == 0)
        return
            evaluate_untextured_light(
                bindings,
                light,
                wavelengths);
    if (
        light.identifiers.x ==
        light_portal_infinite) {
        StructuredBuffer<PathPortal> portals =
            bindings.portals;
        const PathPortal portal =
            portals[light.references.w];
        float2 portal_uv;
        float jacobian;
        float4 visible_bounds;
        if (
            !portal_image_from_render(
                portal,
                normalize(direction),
                portal_uv,
                jacobian) ||
            !portal_image_bounds(
                portal,
                origin,
                visible_bounds) ||
            any(
                portal_uv <
                visible_bounds.xy) ||
            any(
                portal_uv >
                visible_bounds.zw))
            return float4(0.0);
        return
            evaluate_portal_light_image(
                bindings,
                light,
                portal_uv,
                wavelengths);
    }
    const float3 light_direction =
        normalize(
            inverse_transform_light_vector(
                light,
                direction));
    return
        evaluate_environment_image(
            bindings,
            light.identifiers.w,
            equal_area_sphere_to_square(
                light_direction),
            wavelengths,
            false) *
        light.position_and_scale.w;
}

float light_bound_importance(
    PathLightBvhNode node,
    float3 position,
    float3 normal) {
    const float3 minimum =
        node.bounds_minimum_and_phi.xyz;
    const float3 maximum =
        node.bounds_maximum_and_cos_theta_o.xyz;
    const float3 center =
        (
            minimum +
            maximum
        ) *
        0.5;
    float distance_squared =
        dot(
            position - center,
            position - center);
    distance_squared =
        max(
            distance_squared,
            length(
                maximum -
                minimum) /
                2.0);
    const float3 to_reference =
        normalize(
            position -
            center);
    float cosine_direction =
        dot(
            node.direction_and_cos_theta_e.xyz,
            to_reference);
    if (node.metadata.z != 0)
        cosine_direction =
            abs(cosine_direction);
    const float sine_direction =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine_direction *
                        cosine_direction));
    const float radius =
        length(
            maximum -
            minimum) *
        0.5;
    const float center_distance =
        length(
            center -
            position);
    const float cosine_bounds =
        center_distance < radius
            ? -1.0
            : sqrt(
                  max(
                      0.0,
                      1.0 -
                          radius *
                              radius /
                              (
                                  center_distance *
                                  center_distance
                              )));
    const float sine_bounds =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine_bounds *
                        cosine_bounds));
    const float cosine_o =
        node
            .bounds_maximum_and_cos_theta_o
            .w;
    const float sine_o =
        sqrt(
            max(
                0.0,
                1.0 -
                    cosine_o *
                        cosine_o));
    float cosine_x;
    float sine_x;
    if (cosine_direction > cosine_o) {
        cosine_x = 1.0;
        sine_x = 0.0;
    }
    else {
        cosine_x =
            cosine_direction *
                cosine_o +
            sine_direction *
                sine_o;
        sine_x =
            sine_direction *
                cosine_o -
            cosine_direction *
                sine_o;
    }
    const float cosine =
        cosine_x > cosine_bounds
            ? 1.0
            : cosine_x *
                  cosine_bounds +
              sine_x *
                  sine_bounds;
    if (
        cosine <=
        node
            .direction_and_cos_theta_e
            .w)
        return 0.0;
    float importance =
        node.bounds_minimum_and_phi.w *
        cosine /
        distance_squared;
    if (any(normal != float3(0.0))) {
        const float cosine_incident =
            abs(
                dot(
                    to_reference,
                    normal));
        const float sine_incident =
            sqrt(
                max(
                    0.0,
                    1.0 -
                        cosine_incident *
                            cosine_incident));
        const float incident =
            cosine_incident >
                    cosine_bounds
                ? 1.0
                : cosine_incident *
                      cosine_bounds +
                  sine_incident *
                      sine_bounds;
        importance *= incident;
    }
    return max(importance, 0.0);
}

public PathSampledLight sample_light_sampler(
    PathSceneBindings bindings,
    uint light_count,
    float3 position,
    float3 normal,
    float sample) {
    if (light_count == 0)
        return {
            0,
            0.0,
            false};
    StructuredBuffer<PathLight> lights =
        bindings.lights;
    const uint sampler_kind =
        bindings.light_metadata.x;
    if (sampler_kind != 2) {
        uint index = 0;
        while (
            index + 1 < light_count &&
            sample >
                lights[index]
                    .selection.z)
            ++index;
        return {
            index,
            lights[index].selection.y,
            lights[index].selection.y >
                0.0};
    }
    const uint infinite_count =
        bindings.light_metadata.z;
    const uint node_count =
        bindings.light_metadata.y;
    const float infinite_probability =
        float(infinite_count) /
        float(
            infinite_count +
            (
                node_count == 0
                    ? 0
                    : 1
            ));
    if (sample < infinite_probability) {
        const uint target =
            min(
                uint(
                    sample /
                    infinite_probability *
                    float(infinite_count)),
                infinite_count - 1);
        uint rank = 0;
        for (uint index = 0;
             index != light_count;
             ++index)
            if (
                lights[index]
                        .identifiers.x ==
                        light_distant ||
                lights[index]
                        .identifiers.x ==
                        light_infinite ||
                lights[index]
                        .identifiers.x ==
                        light_portal_infinite) {
                if (rank == target)
                    return {
                        index,
                        infinite_probability /
                            float(
                                infinite_count),
                        true};
                ++rank;
            }
    }
    if (node_count == 0)
        return {
            0,
            0.0,
            false};
    StructuredBuffer<PathLightBvhNode> nodes =
        bindings.light_bvh_nodes;
    sample =
        min(
            (
                sample -
                infinite_probability
            ) /
            (
                1.0 -
                infinite_probability
            ),
            0.99999994);
    float pmf =
        1.0 -
        infinite_probability;
    uint node_index = 0;
    while (true) {
        const PathLightBvhNode node =
            nodes[node_index];
        if (node.metadata.y != 0)
            return {
                node.metadata.x,
                pmf,
                light_bound_importance(
                    node,
                    position,
                    normal) >
                    0.0 ||
                    node_index == 0};
        const uint second_index =
            node.metadata.x;
        const float first_importance =
            light_bound_importance(
                nodes[node_index + 1],
                position,
                normal);
        const float second_importance =
            light_bound_importance(
                nodes[second_index],
                position,
                normal);
        const float sum =
            first_importance +
            second_importance;
        if (sum == 0.0)
            return {
                0,
                0.0,
                false};
        const float first_probability =
            first_importance /
            sum;
        if (sample < first_probability) {
            sample /=
                first_probability;
            pmf *=
                first_probability;
            ++node_index;
        }
        else {
            sample =
                (
                    sample -
                    first_probability
                ) /
                (
                    1.0 -
                    first_probability
                );
            pmf *=
                1.0 -
                first_probability;
            node_index =
                second_index;
        }
    }
}

public float light_sampler_pmf(
    PathSceneBindings bindings,
    uint light_count,
    float3 position,
    float3 normal,
    uint light_index) {
    if (light_count == 0)
        return 0.0;
    StructuredBuffer<PathLight> lights =
        bindings.lights;
    if (bindings.light_metadata.x != 2)
        return
            lights[light_index]
                .selection.y;
    const uint infinite_count =
        bindings.light_metadata.z;
    const uint node_count =
        bindings.light_metadata.y;
    const float infinite_probability =
        float(infinite_count) /
        float(
            infinite_count +
            (
                node_count == 0
                    ? 0
                    : 1
            ));
    if (
        lights[light_index]
                .identifiers.x ==
                light_distant ||
        lights[light_index]
                .identifiers.x ==
                light_infinite ||
        lights[light_index]
                .identifiers.x ==
                light_portal_infinite)
        return
            infinite_probability /
            float(infinite_count);
    if (node_count == 0)
        return 0.0;
    StructuredBuffer<PathLightBvhNode> nodes =
        bindings.light_bvh_nodes;
    StructuredBuffer<uint> bit_trails =
        bindings.light_bvh_bit_trails;
    uint bit_trail =
        bit_trails[light_index];
    float pmf =
        1.0 -
        infinite_probability;
    uint node_index = 0;
    while (true) {
        const PathLightBvhNode node =
            nodes[node_index];
        if (node.metadata.y != 0)
            return pmf;
        const uint second_index =
            node.metadata.x;
        const float importance[2] = {
            light_bound_importance(
                nodes[node_index + 1],
                position,
                normal),
            light_bound_importance(
                nodes[second_index],
                position,
                normal),
        };
        const uint child =
            bit_trail & 1;
        const float sum =
            importance[0] +
            importance[1];
        if (
            sum == 0.0 ||
            importance[child] == 0.0)
            return 0.0;
        pmf *=
            importance[child] /
            sum;
        node_index =
            child == 0
                ? node_index + 1
                : second_index;
        bit_trail >>= 1;
    }
}

public PathLightLiSample sample_light_li(
    PathSceneBindings bindings,
    uint light_index,
    float3 position,
    float3 normal,
    float2 sample,
    float4 wavelengths,
    DescriptorHandle<
        RWStructuredBuffer<float4>>
            texture_stack,
    uint texture_base,
    bool allow_incomplete_pdf) {
    StructuredBuffer<PathLight> lights =
        bindings.lights;
    const PathLight light =
        lights[light_index];
    if (
        light.identifiers.x ==
            light_point ||
        light.identifiers.x ==
            light_spot) {
        const float3 vector =
            light.position_and_scale.xyz -
            position;
        const float distance_squared =
            dot(vector, vector);
        const float3 direction =
            vector /
            sqrt(distance_squared);
        float4 radiance =
            evaluate_untextured_light(
                bindings,
                light,
                wavelengths) /
            distance_squared;
        if (
            light.identifiers.x ==
            light_spot)
            radiance *=
                spot_falloff(
                    light,
                    -direction);
        return {
            radiance,
            direction,
            1.0,
            light.position_and_scale.xyz,
            float3(0.0),
            float2(0.0),
            (
                light.identifiers.y &
                (
                    light_delta_position |
                    light_delta_direction
                )
            ) != 0,
            any(radiance > float4(0.0))};
    }
    if (
        light.identifiers.x ==
        light_distant) {
        const float3 direction =
            normalize(
                light.position_and_scale.xyz);
        return {
            evaluate_untextured_light(
                bindings,
                light,
                wavelengths),
            direction,
            1.0,
            position +
                direction *
                    (
                        2.0 *
                        light.parameters.w
                    ),
            float3(0.0),
            float2(0.0),
            (
                light.identifiers.y &
                light_delta_direction
            ) != 0,
            true};
    }
    if (
        light.identifiers.x ==
        light_diffuse_area) {
        StructuredBuffer<PathLightShape> shapes =
            bindings.light_shapes;
        const PathShapeSample shape_sample =
            sample_light_shape_from_context(
                shapes[
                    light.references.x],
                position,
                normal,
                sample);
        const float3 vector =
            shape_sample.position -
            position;
        const float distance_squared =
            dot(vector, vector);
        if (distance_squared == 0.0)
            return {
                float4(0.0),
                float3(0.0),
                0.0,
                float3(0.0),
                float3(0.0),
                float2(0.0),
                false,
                false};
        const float3 direction =
            vector /
            sqrt(distance_squared);
        const float pdf =
            shape_sample.pdf;
        const float4 radiance =
            evaluate_area_light(
                bindings,
                light,
                shape_sample.position,
                shape_sample.normal,
                shape_sample
                    .texture_coordinates,
                -direction,
                wavelengths,
                texture_stack,
                texture_base);
        return {
            radiance,
            direction,
            pdf,
            shape_sample.position,
            shape_sample.normal,
            shape_sample
                .texture_coordinates,
            (
                light.identifiers.y &
                light_delta_position
            ) != 0,
            pdf > 0.0 &&
                any(
                    radiance >
                    float4(0.0))};
    }
    if (
        light.identifiers.x ==
        light_infinite) {
        if (
            (
                light.identifiers.y &
                light_textured
            ) == 0) {
            if (allow_incomplete_pdf)
                return {
                    float4(0.0),
                    float3(0.0),
                    0.0,
                    float3(0.0),
                    float3(0.0),
                    float2(0.0),
                    false,
                    false};
            const float3 direction =
                sample_uniform_sphere(
                    sample);
            return {
                evaluate_untextured_light(
                    bindings,
                    light,
                    wavelengths),
                direction,
                1.0 /
                    (
                        4.0 *
                        light_pi
                    ),
                position +
                    direction *
                        (
                            2.0 *
                            light.parameters.w
                        ),
                float3(0.0),
                float2(0.0),
                false,
                true};
        }
        const PathDistributionSample
            distribution_sample =
                sample_distribution(
                    bindings,
                    allow_incomplete_pdf
                        ? light.references.z
                        : light.references.y,
                    sample);
        if (!distribution_sample.valid)
            return {
                float4(0.0),
                float3(0.0),
                0.0,
                float3(0.0),
                float3(0.0),
                float2(0.0),
                false,
                false};
        const float3 light_direction =
            equal_area_square_to_sphere(
                distribution_sample.value);
        const float3 direction =
            normalize(
                transform_light_vector(
                    light,
                    light_direction));
        return {
            evaluate_environment_image(
                bindings,
                light.identifiers.w,
                distribution_sample.value,
                wavelengths,
                false) *
                light.position_and_scale.w,
            direction,
            distribution_sample.pdf /
                (
                    4.0 *
                    light_pi
                ),
            position +
                direction *
                    (
                        2.0 *
                        light.parameters.w
                    ),
            float3(0.0),
            distribution_sample.value,
            false,
            true};
    }
    StructuredBuffer<PathPortal> portals =
        bindings.portals;
    const PathPortal portal =
        portals[light.references.w];
    float4 visible_bounds;
    if (
        !portal_image_bounds(
            portal,
            position,
            visible_bounds))
        return {
            float4(0.0),
            float3(0.0),
            0.0,
            float3(0.0),
            float3(0.0),
            float2(0.0),
            false,
            false};
    const PathDistributionSample
        distribution_sample =
            sample_window_distribution(
                bindings,
                light.references.y,
                visible_bounds,
                sample);
    if (!distribution_sample.valid)
        return {
            float4(0.0),
            float3(0.0),
            0.0,
            float3(0.0),
            float3(0.0),
            float2(0.0),
            false,
            false};
    float jacobian;
    const float3 direction =
        portal_render_from_image(
            portal,
            distribution_sample.value,
            jacobian);
    return {
        evaluate_portal_light_image(
            bindings,
            light,
            distribution_sample.value,
            wavelengths),
        direction,
        distribution_sample.pdf /
            jacobian,
        position +
            direction *
                (
                    2.0 *
                    light.parameters.w
                ),
        float3(0.0),
        distribution_sample.value,
        false,
        jacobian != 0.0};
}

public float light_pdf_li(
    PathSceneBindings bindings,
    uint light_index,
    float3 position,
    float3 normal,
    float3 direction,
    bool allow_incomplete_pdf) {
    StructuredBuffer<PathLight> lights =
        bindings.lights;
    const PathLight light =
        lights[light_index];
    if (
        light.identifiers.x ==
            light_point ||
        light.identifiers.x ==
            light_spot ||
        light.identifiers.x ==
            light_distant)
        return 0.0;
    if (
        light.identifiers.x ==
        light_infinite) {
        if (
            (
                light.identifiers.y &
                light_textured
            ) == 0)
            return
                allow_incomplete_pdf
                    ? 0.0
                    : 1.0 /
                          (
                              4.0 *
                              light_pi
                          );
        const float3 local =
            normalize(
                inverse_transform_light_vector(
                    light,
                    direction));
        return
            distribution_pdf(
                bindings,
                allow_incomplete_pdf
                    ? light.references.z
                    : light.references.y,
                equal_area_sphere_to_square(
                    local)) /
            (
                4.0 *
                light_pi
            );
    }
    if (
        light.identifiers.x ==
        light_portal_infinite) {
        StructuredBuffer<PathPortal> portals =
            bindings.portals;
        const PathPortal portal =
            portals[light.references.w];
        float2 uv;
        float jacobian;
        float4 visible_bounds;
        if (
            !portal_image_from_render(
                portal,
                direction,
                uv,
                jacobian) ||
            jacobian == 0.0 ||
            !portal_image_bounds(
                portal,
                position,
                visible_bounds))
            return 0.0;
        return
            window_distribution_pdf(
                bindings,
                light.references.y,
                visible_bounds,
                uv) /
            jacobian;
    }
    StructuredBuffer<PathLightShape> shapes =
        bindings.light_shapes;
    return
        light_shape_pdf_from_context(
            shapes[
                light.references.x],
            position,
            normal,
            direction);
}

public float area_light_pdf_for_hit(
    PathSceneBindings bindings,
    uint light_index,
    float selection_pmf,
    float3 position,
    float3 normal,
    float3 direction) {
    return
        selection_pmf *
        light_pdf_li(
            bindings,
            light_index,
            position,
            normal,
            direction,
            false);
}

public float4 escaped_volpath_radiance(
    PathSceneBindings bindings,
    uint light_count,
    float3 origin,
    float3 normal,
    float3 direction,
    float4 wavelengths,
    float4 beta,
    float4 r_u,
    float4 r_l,
    bool specular,
    bool primary) {
    StructuredBuffer<PathLight> lights =
        bindings.lights;
    float4 radiance =
        float4(0.0);
    for (uint light_index = 0;
         light_index != light_count;
         ++light_index) {
        const PathLight light =
            lights[light_index];
        if (
            (
                light.identifiers.y &
                light_infinite_flag
            ) == 0)
            continue;
        const float4 emission =
            evaluate_infinite_light(
                bindings,
                light,
                origin,
                direction,
                wavelengths);
        if (
            primary ||
            specular) {
            const float denominator =
                (
                    r_u.x +
                    r_u.y +
                    r_u.z +
                    r_u.w
                ) *
                0.25;
            if (denominator != 0.0)
                radiance +=
                    beta *
                    emission /
                    denominator;
        }
        else {
            const float light_pdf =
                light_sampler_pmf(
                    bindings,
                    light_count,
                    origin,
                    normal,
                    light_index) *
                light_pdf_li(
                    bindings,
                    light_index,
                    origin,
                    normal,
                    direction,
                    true);
            const float4 denominator_spectrum =
                r_u +
                r_l *
                    light_pdf;
            const float denominator =
                (
                    denominator_spectrum.x +
                    denominator_spectrum.y +
                    denominator_spectrum.z +
                    denominator_spectrum.w
                ) *
                0.25;
            if (denominator != 0.0)
                radiance +=
                    beta *
                    emission /
                    denominator;
        }
    }
    return radiance;
}
