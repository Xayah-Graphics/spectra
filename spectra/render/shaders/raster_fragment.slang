module raster_fragment;

import raster_scene;
import raster_texture;

struct RasterVertex {
    float4 position : SV_Position;
    float3 world_position : POSITION;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float2 texture_coordinates : TEXCOORD;
    float3 emission : COLOR0;
    float3 color_modifier : COLOR1;
    nointerpolation uint material : MATERIAL;
    nointerpolation uint particle : PARTICLE;
    nointerpolation float3 particle_center : POSITION1;
    nointerpolation float particle_radius : RADIUS;
}

struct RasterFragmentOutput {
    float4 color : SV_Target;
    float depth : SV_Depth;
}

[vk::push_constant]
uniform RasterPushData push_data;

float raster_float_parameter(
    float value,
    uint texture,
    RasterSceneBindings bindings,
    RasterTextureContext context) {
    if (texture != 0xffffffff)
        return
            evaluate_raster_texture(
                bindings,
                texture,
                context)
                .x;
    return value;
}

float3 raster_spectrum_parameter(
    float4 value,
    uint texture,
    RasterSceneBindings bindings,
    RasterTextureContext context) {
    if (texture != 0xffffffff)
        return
            evaluate_raster_texture(
                bindings,
                texture,
                context)
                .xyz;
    return value.xyz;
}

uint raster_hash(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

float raster_material_choice(
    float3 position,
    float3 outgoing,
    uint material) {
    uint hash =
        raster_hash(
            asuint(position.x) ^
            raster_hash(
                asuint(position.y)) ^
            raster_hash(
                asuint(position.z)) ^
            raster_hash(
                asuint(outgoing.x)) ^
            raster_hash(
                asuint(outgoing.y)) ^
            raster_hash(
                asuint(outgoing.z)) ^
            raster_hash(material));
    return
        float(hash) *
        (1.0 / 4294967296.0);
}

uint resolve_raster_material(
    StructuredBuffer<RasterMaterial> materials,
    uint material,
    RasterSceneBindings bindings,
    RasterTextureContext context,
    float3 outgoing) {
    [loop]
    for (uint depth = 0;
         depth != 32;
         ++depth) {
        const RasterMaterial current =
            materials[material];
        if (current.metadata.x != 8)
            return material;
        const float amount =
            clamp(
                raster_float_parameter(
                    current.scalar_values_0.x,
                    current.scalar_textures_0.x,
                    bindings,
                    context),
                0.0,
                1.0);
        material =
            raster_material_choice(
                context.position,
                outgoing,
                material) < amount
                ? current.materials.y
                : current.materials.x;
    }
    return material;
}

float dielectric_fresnel(
    float cosine,
    float eta) {
    cosine =
        clamp(
            cosine,
            -1.0,
            1.0);
    if (cosine < 0.0) {
        eta =
            1.0 /
            eta;
        cosine =
            -cosine;
    }
    const float sine_squared =
        max(
            0.0,
            1.0 -
                cosine *
                    cosine);
    const float transmitted_sine_squared =
        sine_squared /
        (
            eta *
            eta
        );
    if (transmitted_sine_squared >= 1.0)
        return 1.0;
    const float transmitted_cosine =
        sqrt(
            1.0 -
            transmitted_sine_squared);
    const float parallel =
        (
            eta *
                cosine -
            transmitted_cosine
        ) /
        (
            eta *
                cosine +
            transmitted_cosine
        );
    const float perpendicular =
        (
            cosine -
            eta *
                transmitted_cosine
        ) /
        (
            cosine +
            eta *
                transmitted_cosine
        );
    return
        0.5 *
        (
            parallel *
                parallel +
            perpendicular *
                perpendicular
        );
}

float3 conductor_fresnel(
    float cosine,
    float3 eta,
    float3 k) {
    cosine =
        clamp(
            abs(cosine),
            0.0,
            1.0);
    const float cosine_squared =
        cosine *
        cosine;
    const float sine_squared =
        1.0 -
        cosine_squared;
    const float3 eta_squared =
        eta *
        eta;
    const float3 k_squared =
        k *
        k;
    const float3 t0 =
        eta_squared -
        k_squared -
        sine_squared;
    const float3 a2_plus_b2 =
        sqrt(
            t0 *
                t0 +
            4.0 *
                eta_squared *
                k_squared);
    const float3 t1 =
        a2_plus_b2 +
        cosine_squared;
    const float3 a =
        sqrt(
            0.5 *
            (
                a2_plus_b2 +
                t0
            ));
    const float3 t2 =
        2.0 *
        cosine *
        a;
    const float3 rs =
        (
            t1 -
            t2
        ) /
        (
            t1 +
            t2
        );
    const float3 t3 =
        cosine_squared *
            a2_plus_b2 +
        sine_squared *
            sine_squared;
    const float3 t4 =
        t2 *
        sine_squared;
    const float3 rp =
        rs *
        (
            t3 -
            t4
        ) /
        (
            t3 +
            t4
        );
    return
        0.5 *
        (
            rp +
            rs
        );
}

float raster_alpha(
    float roughness,
    bool remap) {
    return
        max(
            remap
                ? sqrt(
                      max(
                          roughness,
                          0.0))
                : roughness,
            1.0e-3);
}

float anisotropic_distribution(
    float3 half_vector,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float alpha_x,
    float alpha_y) {
    const float3 local =
        float3(
            dot(
                half_vector,
                tangent),
            dot(
                half_vector,
                bitangent),
            dot(
                half_vector,
                normal));
    const float denominator =
        local.x *
            local.x /
            (
                alpha_x *
                alpha_x
            ) +
        local.y *
            local.y /
            (
                alpha_y *
                alpha_y
            ) +
        local.z *
            local.z;
    return
        1.0 /
        (
            3.14159265358979323846 *
            alpha_x *
            alpha_y *
            denominator *
            denominator
        );
}

float anisotropic_lambda(
    float3 direction,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float alpha_x,
    float alpha_y) {
    const float x =
        dot(
            direction,
            tangent);
    const float y =
        dot(
            direction,
            bitangent);
    const float z =
        dot(
            direction,
            normal);
    return
        0.5 *
        (
            sqrt(
                1.0 +
                (
                    alpha_x *
                        alpha_x *
                        x *
                        x +
                    alpha_y *
                        alpha_y *
                        y *
                        y
                ) /
                max(
                    z *
                        z,
                    1.0e-8)) -
            1.0
        );
}

float microfacet_geometry(
    float3 outgoing,
    float3 incoming,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float alpha_x,
    float alpha_y) {
    return
        1.0 /
        (
            1.0 +
            anisotropic_lambda(
                outgoing,
                tangent,
                bitangent,
                normal,
                alpha_x,
                alpha_y) +
            anisotropic_lambda(
                incoming,
                tangent,
                bitangent,
                normal,
                alpha_x,
                alpha_y)
        );
}

float3 dielectric_microfacet(
    float3 outgoing,
    float3 incoming,
    float3 half_vector,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float eta,
    float alpha_x,
    float alpha_y) {
    const float no_v =
        max(
            dot(
                normal,
                outgoing),
            1.0e-5);
    const float no_l =
        max(
            dot(
                normal,
                incoming),
            1.0e-5);
    const float distribution =
        anisotropic_distribution(
            half_vector,
            tangent,
            bitangent,
            normal,
            alpha_x,
            alpha_y);
    const float geometry =
        microfacet_geometry(
            outgoing,
            incoming,
            tangent,
            bitangent,
            normal,
            alpha_x,
            alpha_y);
    return
        float3(
            distribution *
            geometry *
            dielectric_fresnel(
                dot(
                    outgoing,
                    half_vector),
                eta) /
            (
                4.0 *
                no_v *
                no_l
            ));
}

float3 conductor_microfacet(
    float3 outgoing,
    float3 incoming,
    float3 half_vector,
    float3 tangent,
    float3 bitangent,
    float3 normal,
    float3 eta,
    float3 k,
    float alpha_x,
    float alpha_y) {
    const float no_v =
        max(
            dot(
                normal,
                outgoing),
            1.0e-5);
    const float no_l =
        max(
            dot(
                normal,
                incoming),
            1.0e-5);
    const float distribution =
        anisotropic_distribution(
            half_vector,
            tangent,
            bitangent,
            normal,
            alpha_x,
            alpha_y);
    const float geometry =
        microfacet_geometry(
            outgoing,
            incoming,
            tangent,
            bitangent,
            normal,
            alpha_x,
            alpha_y);
    return
        distribution *
        geometry *
        conductor_fresnel(
            dot(
                outgoing,
                half_vector),
            eta,
            k) /
        (
            4.0 *
            no_v *
            no_l
        );
}

float3 effective_layer_transmittance(
    float3 albedo,
    float thickness,
    float g,
    float no_v,
    float no_l) {
    const float directional_scale =
        max(
            0.05,
            1.0 -
                0.5 *
                    clamp(
                        g,
                        -1.0,
                        1.0));
    const float3 effective_extinction =
        max(
            float3(0.0),
            float3(1.0) -
                clamp(
                    albedo,
                    float3(0.0),
                    float3(1.0))) *
        directional_scale;
    return exp(
        -max(
            thickness,
            0.0) *
        effective_extinction *
        (
            1.0 /
                max(
                    no_v,
                    0.05) +
            1.0 /
                max(
                    no_l,
                    0.05)
        ));
}

void conductor_parameters(
    RasterMaterial material,
    RasterSceneBindings bindings,
    RasterTextureContext context,
    uint parameter,
    float interface_eta,
    out float3 eta,
    out float3 k) {
    const float4 eta_or_reflectance =
        parameter == 0
            ? material.spectrum_value_0
            : material.spectrum_value_1;
    const uint eta_or_reflectance_texture =
        parameter == 0
            ? material.spectrum_data_0.x
            : material.spectrum_data_1.x;
    const float4 extinction =
        parameter == 0
            ? material.spectrum_value_1
            : material.spectrum_value_2;
    const uint extinction_texture =
        parameter == 0
            ? material.spectrum_data_1.x
            : material.spectrum_data_2.x;
    if ((material.metadata.w & 2) == 0) {
        eta =
            raster_spectrum_parameter(
                eta_or_reflectance,
                eta_or_reflectance_texture,
                bindings,
                context) /
            interface_eta;
        k =
            raster_spectrum_parameter(
                extinction,
                extinction_texture,
                bindings,
                context) /
            interface_eta;
        return;
    }
    const float3 reflectance =
        clamp(
            raster_spectrum_parameter(
                eta_or_reflectance,
                eta_or_reflectance_texture,
                bindings,
                context),
            float3(0.0),
            float3(0.9999));
    eta =
        float3(1.0) /
        interface_eta;
    k =
        2.0 *
        sqrt(reflectance) /
        sqrt(
            max(
                float3(1.0) -
                    reflectance,
                float3(1.0e-8))) /
        interface_eta;
}

[shader("fragment")]
RasterFragmentOutput raster_fragment(
    RasterVertex input) {
    float fragment_depth = input.position.z;
    if (input.particle != 0) {
        const float3 ray_origin =
            push_data.camera_position.xyz;
        const float3 ray_direction =
            normalize(
                input.world_position -
                ray_origin);
        const float3 offset =
            ray_origin -
            input.particle_center;
        const float half_b =
            dot(
                offset,
                ray_direction);
        const float discriminant =
            half_b *
                half_b -
            (
                dot(
                    offset,
                    offset) -
                input.particle_radius *
                    input.particle_radius
            );
        if (discriminant < 0.0) discard;
        const float distance =
            -half_b -
            sqrt(discriminant);
        if (distance <= 0.0) discard;
        input.world_position =
            ray_origin +
            distance *
                ray_direction;
        input.world_normal =
            normalize(
                input.world_position -
                input.particle_center);
        input.world_tangent =
            normalize(
                abs(input.world_normal.z) < 0.999
                    ? cross(
                          float3(0.0, 0.0, 1.0),
                          input.world_normal)
                    : float3(1.0, 0.0, 0.0));
        const float phi =
            atan2(
                input.world_normal.y,
                input.world_normal.x);
        input.texture_coordinates = float2(
            (
                phi < 0.0
                    ? phi +
                        2.0 *
                            3.14159265358979323846
                    : phi
            ) /
                (
                    2.0 *
                    3.14159265358979323846
                ),
            acos(
                clamp(
                    input.world_normal.z,
                    -1.0,
                    1.0)) /
                3.14159265358979323846);
        const float4 homogeneous =
            float4(
                input.world_position,
                1.0);
        const float4 clip = float4(
            dot(
                push_data.view_projection_row_0,
                homogeneous),
            dot(
                push_data.view_projection_row_1,
                homogeneous),
            dot(
                push_data.view_projection_row_2,
                homogeneous),
            dot(
                push_data.view_projection_row_3,
                homogeneous));
        fragment_depth =
            clip.z /
            clip.w;
    }
    StructuredBuffer<RasterSceneBindings> binding_table =
        push_data.bindings;
    const RasterSceneBindings bindings =
        binding_table[0];
    StructuredBuffer<RasterMaterial> materials =
        bindings.materials;
    RasterTextureContext texture_context = {
        input.world_position,
        normalize(input.world_normal),
        input.texture_coordinates,
        ddx(input.world_position),
        ddy(input.world_position),
        ddx(input.texture_coordinates),
        ddy(input.texture_coordinates),
    };
    StructuredBuffer<RasterPrimitive> primitives =
        bindings.primitives;
    const RasterPrimitive primitive =
        primitives[
            push_data.scene_primitive_index];
    if (
        primitive.alpha_texture != 0xffffffff &&
        evaluate_raster_texture(
            bindings,
            primitive.alpha_texture,
            texture_context)
                .x < 0.5)
        discard;
    float3 outgoing =
        normalize(
            push_data.camera_position.xyz -
            input.world_position);
    const uint material_index =
        resolve_raster_material(
            materials,
            input.material,
            bindings,
            texture_context,
            outgoing);
    const RasterMaterial material =
        materials[material_index];
    if (material.metadata.x == 0)
        discard;
    float3 normal =
        texture_context.normal;
    float3 tangent =
        normalize(
            input.world_tangent -
            dot(
                input.world_tangent,
                normal) *
                normal);
    float3 bitangent =
        normalize(
            cross(
                normal,
                tangent));
    if (material.metadata.y != 0xffffffff) {
        const float3 tangent_normal =
            normalize(
                2.0 *
                    evaluate_raster_normal_map(
                        bindings,
                        material.metadata.y,
                        texture_context) -
                1.0);
        normal =
            normalize(
                tangent_normal.x *
                    tangent +
                tangent_normal.y *
                    bitangent +
                tangent_normal.z *
                    normal);
        tangent =
            normalize(
                tangent -
                dot(
                    tangent,
                    normal) *
                    normal);
        bitangent =
            normalize(
                cross(
                    normal,
                    tangent));
    }
    else if (material.metadata.z != 0xffffffff) {
        const float du_candidate =
            0.5 *
            (
                abs(
                    texture_context
                        .texture_coordinates_dx.x) +
                abs(
                    texture_context
                        .texture_coordinates_dy.x)
            );
        const float dv_candidate =
            0.5 *
            (
                abs(
                    texture_context
                        .texture_coordinates_dx.y) +
                abs(
                    texture_context
                        .texture_coordinates_dy.y)
            );
        const float du =
            du_candidate == 0.0
                ? 0.0005
                : du_candidate;
        const float dv =
            dv_candidate == 0.0
                ? 0.0005
                : dv_candidate;
        RasterTextureContext u_context =
            texture_context;
        u_context.position +=
            tangent *
            du;
        u_context.texture_coordinates.x += du;
        RasterTextureContext v_context =
            texture_context;
        v_context.position +=
            bitangent *
            dv;
        v_context.texture_coordinates.y += dv;
        const float displacement =
            evaluate_raster_texture(
                bindings,
                material.metadata.z,
                texture_context)
                .x;
        const float displacement_u =
            evaluate_raster_texture(
                bindings,
                material.metadata.z,
                u_context)
                .x;
        const float displacement_v =
            evaluate_raster_texture(
                bindings,
                material.metadata.z,
                v_context)
                .x;
        normal =
            normalize(
                normal -
                tangent *
                    (
                        displacement_u -
                        displacement
                    ) /
                    du -
                bitangent *
                    (
                        displacement_v -
                        displacement
                    ) /
                    dv);
        tangent =
            normalize(
                tangent -
                dot(
                    tangent,
                    normal) *
                    normal);
        bitangent =
            normalize(
                cross(
                    normal,
                    tangent));
    }
    if (dot(normal, outgoing) < 0.0) {
        normal =
            -normal;
        bitangent =
            -bitangent;
    }
    texture_context.normal =
        normal;
    const float3 incoming =
        normalize(
            float3(
                -0.35,
                0.8,
                0.45));
    const float no_l =
        max(
            dot(
                normal,
                incoming),
            0.0);
    const float no_v =
        max(
            dot(
                normal,
                outgoing),
            0.0);
    const float3 half_vector =
        normalize(
            incoming +
            outgoing);
    const bool remap =
        (material.metadata.w & 1) != 0;
    float3 brdf =
        float3(0.0);
    float3 ambient =
        float3(0.0);
    if (material.metadata.x == 1) {
        const float3 reflectance =
            raster_spectrum_parameter(
                material.spectrum_value_0,
                material.spectrum_data_0.x,
                bindings,
                texture_context) *
            input.color_modifier;
        brdf =
            reflectance /
            3.14159265358979323846;
        ambient =
            0.08 *
            reflectance;
    }
    else if (material.metadata.x == 2) {
        const float3 reflectance =
            raster_spectrum_parameter(
                material.spectrum_value_0,
                material.spectrum_data_0.x,
                bindings,
                texture_context);
        const float3 transmittance =
            raster_spectrum_parameter(
                material.spectrum_value_1,
                material.spectrum_data_1.x,
                bindings,
                texture_context);
        const float3 preview =
            (
                reflectance +
                transmittance
            ) *
            material.scalar_values_0.x *
            input.color_modifier;
        brdf =
            preview /
            3.14159265358979323846;
        ambient =
            0.08 *
            preview;
    }
    else if (material.metadata.x == 3) {
        float3 eta;
        float3 k;
        conductor_parameters(
            material,
            bindings,
            texture_context,
            0,
            1.0,
            eta,
            k);
        const float alpha_x =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.x,
                    material.scalar_textures_0.x,
                    bindings,
                    texture_context),
                remap);
        const float alpha_y =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.y,
                    material.scalar_textures_0.y,
                    bindings,
                    texture_context),
                remap);
        brdf =
            conductor_microfacet(
                outgoing,
                incoming,
                half_vector,
                tangent,
                bitangent,
                normal,
                eta,
                k,
                alpha_x,
                alpha_y);
        ambient =
            0.025 *
            conductor_fresnel(
                no_v,
                eta,
                k);
    }
    else if (
        material.metadata.x == 4 ||
        material.metadata.x == 5) {
        const float eta =
            max(
                raster_spectrum_parameter(
                    material.spectrum_value_0,
                    material.spectrum_data_0.x,
                    bindings,
                    texture_context)
                    .x,
                1.0e-3);
        const float alpha_x =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.x,
                    material.scalar_textures_0.x,
                    bindings,
                    texture_context),
                remap);
        const float alpha_y =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.y,
                    material.scalar_textures_0.y,
                    bindings,
                    texture_context),
                remap);
        brdf =
            dielectric_microfacet(
                outgoing,
                incoming,
                half_vector,
                tangent,
                bitangent,
                normal,
                eta,
                alpha_x,
                alpha_y);
        ambient =
            0.025 *
            dielectric_fresnel(
                no_v,
                eta);
    }
    else if (material.metadata.x == 6) {
        const float3 reflectance =
            raster_spectrum_parameter(
                material.spectrum_value_0,
                material.spectrum_data_0.x,
                bindings,
                texture_context) *
            input.color_modifier;
        const float eta =
            max(
                raster_spectrum_parameter(
                    material.spectrum_value_1,
                    material.spectrum_data_1.x,
                    bindings,
                    texture_context)
                    .x,
                1.0e-3);
        const float3 albedo =
            raster_spectrum_parameter(
                material.spectrum_value_2,
                material.spectrum_data_2.x,
                bindings,
                texture_context);
        const float alpha_x =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.x,
                    material.scalar_textures_0.x,
                    bindings,
                    texture_context),
                remap);
        const float alpha_y =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.y,
                    material.scalar_textures_0.y,
                    bindings,
                    texture_context),
                remap);
        const float thickness =
            raster_float_parameter(
                material.scalar_values_0.z,
                material.scalar_textures_0.z,
                bindings,
                texture_context);
        const float g =
            raster_float_parameter(
                material.scalar_values_0.w,
                material.scalar_textures_0.w,
                bindings,
                texture_context);
        const float3 transmittance =
            effective_layer_transmittance(
                albedo,
                thickness,
                g,
                no_v,
                no_l);
        const float fresnel_v =
            dielectric_fresnel(
                no_v,
                eta);
        const float fresnel_l =
            dielectric_fresnel(
                no_l,
                eta);
        const float3 diffuse =
            reflectance /
            3.14159265358979323846 *
            (
                1.0 -
                fresnel_v
            ) *
            (
                1.0 -
                fresnel_l
            ) *
            transmittance;
        const float3 layer_scattering =
            albedo *
            (
                float3(1.0) -
                transmittance
            ) /
            3.14159265358979323846 *
            0.25;
        brdf =
            dielectric_microfacet(
                outgoing,
                incoming,
                half_vector,
                tangent,
                bitangent,
                normal,
                eta,
                alpha_x,
                alpha_y) +
            diffuse +
            layer_scattering;
        ambient =
            0.08 *
            (
                reflectance *
                    transmittance +
                albedo *
                    (
                        float3(1.0) -
                        transmittance
                    )
            );
    }
    else if (material.metadata.x == 7) {
        const float interface_eta =
            max(
                raster_spectrum_parameter(
                    material.spectrum_value_0,
                    material.spectrum_data_0.x,
                    bindings,
                    texture_context)
                    .x,
                1.0e-3);
        float3 eta;
        float3 k;
        conductor_parameters(
            material,
            bindings,
            texture_context,
            1,
            interface_eta,
            eta,
            k);
        const float3 albedo =
            raster_spectrum_parameter(
                material.spectrum_value_3,
                material.spectrum_data_3.x,
                bindings,
                texture_context);
        const float interface_alpha_x =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.x,
                    material.scalar_textures_0.x,
                    bindings,
                    texture_context),
                remap);
        const float interface_alpha_y =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.y,
                    material.scalar_textures_0.y,
                    bindings,
                    texture_context),
                remap);
        const float conductor_alpha_x =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.z,
                    material.scalar_textures_0.z,
                    bindings,
                    texture_context),
                remap);
        const float conductor_alpha_y =
            raster_alpha(
                raster_float_parameter(
                    material.scalar_values_0.w,
                    material.scalar_textures_0.w,
                    bindings,
                    texture_context),
                remap);
        const float thickness =
            raster_float_parameter(
                material.scalar_values_1.x,
                material.scalar_textures_1.x,
                bindings,
                texture_context);
        const float g =
            raster_float_parameter(
                material.scalar_values_1.y,
                material.scalar_textures_1.y,
                bindings,
                texture_context);
        const float3 transmittance =
            effective_layer_transmittance(
                albedo,
                thickness,
                g,
                no_v,
                no_l);
        const float fresnel_v =
            dielectric_fresnel(
                no_v,
                interface_eta);
        const float fresnel_l =
            dielectric_fresnel(
                no_l,
                interface_eta);
        const float3 bottom =
            conductor_microfacet(
                outgoing,
                incoming,
                half_vector,
                tangent,
                bitangent,
                normal,
                eta,
                k,
                conductor_alpha_x,
                conductor_alpha_y) *
            (
                1.0 -
                fresnel_v
            ) *
            (
                1.0 -
                fresnel_l
            ) *
            transmittance;
        const float3 layer_scattering =
            albedo *
            (
                float3(1.0) -
                transmittance
            ) /
            3.14159265358979323846 *
            0.25;
        brdf =
            dielectric_microfacet(
                outgoing,
                incoming,
                half_vector,
                tangent,
                bitangent,
                normal,
                interface_eta,
                interface_alpha_x,
                interface_alpha_y) +
            bottom +
            layer_scattering;
        ambient =
            0.025 *
            (
                conductor_fresnel(
                    no_v,
                    eta,
                    k) *
                    transmittance +
                albedo *
                    (
                        float3(1.0) -
                        transmittance
                    )
            );
    }
    const float3 color =
        input.emission +
        ambient +
        brdf *
            no_l *
            3.0;
    return {
        float4(
            max(
                color,
                float3(0.0)),
            1.0),
        fragment_depth,
    };
}
