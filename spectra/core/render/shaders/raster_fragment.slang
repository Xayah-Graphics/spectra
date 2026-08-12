module raster_fragment;

import raster_scene;
import raster_texture;

struct RasterFragmentOutput {
    float4 color : SV_Target;
    float depth : SV_Depth;
}

static const float raster_preview_pi = 3.14159265358979323846;
static const float3 raster_preview_light_direction = normalize(float3(-0.35, 0.8, 0.45));
static const float3 raster_wire_light_color = float3(0.55, 0.65, 0.72);

[vk::push_constant]
uniform RasterPushData push_data;

RasterFragmentOutput raster_wireframe_output(float fragment_depth, float alpha) {
    return {float4(raster_wire_light_color, alpha), fragment_depth};
}

float raster_float_parameter(float value, uint texture, RasterSceneBindings bindings, RasterTextureContext context) {
    if (texture != 0xffffffff) return evaluate_raster_texture(bindings, texture, context).x;
    return value;
}

float3 raster_spectrum_parameter(float4 value, uint texture, RasterSceneBindings bindings, RasterTextureContext context) {
    if (texture != 0xffffffff) return evaluate_raster_texture(bindings, texture, context).xyz;
    return value.xyz;
}

float dielectric_fresnel(float cosine, float eta) {
    cosine = clamp(cosine, -1.0, 1.0);
    if (cosine < 0.0) {
        eta = 1.0 / eta;
        cosine = -cosine;
    }
    const float sine_squared = max(0.0, 1.0 - cosine * cosine);
    const float transmitted_sine_squared = sine_squared / (eta * eta);
    if (transmitted_sine_squared >= 1.0) return 1.0;
    const float transmitted_cosine = sqrt(1.0 - transmitted_sine_squared);
    const float parallel = (eta * cosine - transmitted_cosine) / (eta * cosine + transmitted_cosine);
    const float perpendicular = (cosine - eta * transmitted_cosine) / (cosine + eta * transmitted_cosine);
    return 0.5 * (parallel * parallel + perpendicular * perpendicular);
}

float3 conductor_fresnel(float cosine, float3 eta, float3 k) {
    cosine = clamp(abs(cosine), 0.0, 1.0);
    const float cosine_squared = cosine * cosine;
    const float sine_squared = 1.0 - cosine_squared;
    const float3 eta_squared = eta * eta;
    const float3 k_squared = k * k;
    const float3 t0 = eta_squared - k_squared - sine_squared;
    const float3 a2_plus_b2 = sqrt(t0 * t0 + 4.0 * eta_squared * k_squared);
    const float3 t1 = a2_plus_b2 + cosine_squared;
    const float3 a = sqrt(0.5 * (a2_plus_b2 + t0));
    const float3 t2 = 2.0 * cosine * a;
    const float3 rs = (t1 - t2) / (t1 + t2);
    const float3 t3 = cosine_squared * a2_plus_b2 + sine_squared * sine_squared;
    const float3 t4 = t2 * sine_squared;
    const float3 rp = rs * (t3 - t4) / (t3 + t4);
    return 0.5 * (rp + rs);
}

float raster_alpha(float roughness, bool remap) {
    return max(remap ? sqrt(max(roughness, 0.0)) : roughness, 1.0e-3);
}

float anisotropic_distribution(float3 half_vector, float3 tangent, float3 bitangent, float3 normal, float alpha_x, float alpha_y) {
    const float3 local = float3(dot(half_vector, tangent), dot(half_vector, bitangent), dot(half_vector, normal));
    const float denominator = local.x * local.x / (alpha_x * alpha_x) + local.y * local.y / (alpha_y * alpha_y) + local.z * local.z;
    return 1.0 / (raster_preview_pi * alpha_x * alpha_y * denominator * denominator);
}

float anisotropic_lambda(float3 direction, float3 tangent, float3 bitangent, float3 normal, float alpha_x, float alpha_y) {
    const float x = dot(direction, tangent);
    const float y = dot(direction, bitangent);
    const float z = dot(direction, normal);
    return 0.5 * (sqrt(1.0 + (alpha_x * alpha_x * x * x + alpha_y * alpha_y * y * y) / max(z * z, 1.0e-8)) - 1.0);
}

float microfacet_geometry(float3 outgoing, float3 incoming, float3 tangent, float3 bitangent, float3 normal, float alpha_x, float alpha_y) {
    return 1.0 / (1.0 + anisotropic_lambda(outgoing, tangent, bitangent, normal, alpha_x, alpha_y) + anisotropic_lambda(incoming, tangent, bitangent, normal, alpha_x, alpha_y));
}

float microfacet_scale(float3 outgoing, float3 incoming, float3 half_vector, float3 tangent, float3 bitangent, float3 normal, float alpha_x, float alpha_y) {
    const float no_v = max(dot(normal, outgoing), 1.0e-5);
    const float no_l = max(dot(normal, incoming), 1.0e-5);
    return anisotropic_distribution(half_vector, tangent, bitangent, normal, alpha_x, alpha_y) * microfacet_geometry(outgoing, incoming, tangent, bitangent, normal, alpha_x, alpha_y) / (4.0 * no_v * no_l);
}

void raster_preview_frame(RasterMaterial material, RasterSceneBindings bindings, RasterTextureContext context, float3 world_tangent, float3 outgoing, out float3 normal, out float3 tangent, out float3 bitangent) {
    normal = context.normal;
    tangent = normalize(world_tangent - dot(world_tangent, normal) * normal);
    bitangent = normalize(cross(normal, tangent));
    if (material.metadata.y != 0xffffffff) {
        const float3 tangent_normal = normalize(2.0 * evaluate_raster_normal_map(bindings, material.metadata.y, context) - 1.0);
        normal = normalize(tangent_normal.x * tangent + tangent_normal.y * bitangent + tangent_normal.z * normal);
    } else if (material.metadata.z != 0xffffffff) {
        float du = 0.5 * (abs(context.texture_coordinates_dx.x) + abs(context.texture_coordinates_dy.x));
        float dv = 0.5 * (abs(context.texture_coordinates_dx.y) + abs(context.texture_coordinates_dy.y));
        if (du == 0.0) du = 0.0005;
        if (dv == 0.0) dv = 0.0005;
        RasterTextureContext u_context = context;
        RasterTextureContext v_context = context;
        u_context.position += tangent * du;
        u_context.texture_coordinates.x += du;
        v_context.position += bitangent * dv;
        v_context.texture_coordinates.y += dv;
        const float displacement = evaluate_raster_texture(bindings, material.metadata.z, context).x;
        const float displacement_u = evaluate_raster_texture(bindings, material.metadata.z, u_context).x;
        const float displacement_v = evaluate_raster_texture(bindings, material.metadata.z, v_context).x;
        normal = normalize(normal - tangent * (displacement_u - displacement) / du - bitangent * (displacement_v - displacement) / dv);
    }
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    bitangent = normalize(cross(normal, tangent));
    if (dot(normal, outgoing) < 0.0) {
        normal = -normal;
        bitangent = -bitangent;
    }
}

float3 raster_preview_conductor_fresnel(RasterMaterial material, RasterSceneBindings bindings, RasterTextureContext context, float cosine) {
    const float3 eta_or_reflectance = raster_spectrum_parameter(material.spectrum_value_0, material.spectrum_data_0.x, bindings, context);
    if ((material.metadata.w & 2) != 0) return clamp(eta_or_reflectance, float3(0.0), float3(1.0));
    const float3 k = raster_spectrum_parameter(material.spectrum_value_1, material.spectrum_data_1.x, bindings, context);
    return conductor_fresnel(cosine, eta_or_reflectance, k);
}

float3 raster_preview_leaf(RasterMaterial material, RasterSceneBindings bindings, RasterTextureContext context, float3 world_tangent, float3 outgoing) {
    float3 normal;
    float3 tangent;
    float3 bitangent;
    raster_preview_frame(material, bindings, context, world_tangent, outgoing, normal, tangent, bitangent);
    context.normal = normal;
    const float3 incoming = raster_preview_light_direction;
    const float no_l = max(dot(normal, incoming), 0.0);
    const float no_v = max(dot(normal, outgoing), 0.0);
    const float3 half_vector = normalize(incoming + outgoing);
    const bool remap = (material.metadata.w & 1) != 0;
    const float alpha_x = raster_alpha(raster_float_parameter(material.scalar_values_0.x, material.scalar_textures_0.x, bindings, context), remap);
    const float alpha_y = raster_alpha(raster_float_parameter(material.scalar_values_0.y, material.scalar_textures_0.y, bindings, context), remap);
    const float microfacet = microfacet_scale(outgoing, incoming, half_vector, tangent, bitangent, normal, alpha_x, alpha_y);

    if (material.metadata.x == raster_material_diffuse) {
        const float3 reflectance = raster_spectrum_parameter(material.spectrum_value_0, material.spectrum_data_0.x, bindings, context);
        return 0.08 * reflectance + 3.0 * no_l * reflectance / raster_preview_pi;
    }
    if (material.metadata.x == raster_material_diffuse_transmission) {
        const float3 reflectance = raster_spectrum_parameter(material.spectrum_value_0, material.spectrum_data_0.x, bindings, context);
        const float3 transmittance = raster_spectrum_parameter(material.spectrum_value_1, material.spectrum_data_1.x, bindings, context);
        const float3 preview = (reflectance + transmittance) * material.scalar_values_0.z;
        return 0.08 * preview + 3.0 * no_l * preview / raster_preview_pi;
    }
    if (material.metadata.x == raster_material_conductor || material.metadata.x == raster_material_coated_conductor) {
        const float3 fresnel = raster_preview_conductor_fresnel(material, bindings, context, dot(outgoing, half_vector));
        return 0.025 * raster_preview_conductor_fresnel(material, bindings, context, no_v) + 3.0 * no_l * microfacet * fresnel;
    }
    const float eta = max(raster_spectrum_parameter(material.spectrum_value_1, material.spectrum_data_1.x, bindings, context).x, 1.0e-3);
    const float fresnel_v = dielectric_fresnel(no_v, eta);
    const float specular = microfacet * dielectric_fresnel(dot(outgoing, half_vector), eta);
    if (material.metadata.x == raster_material_coated_diffuse) {
        const float3 reflectance = raster_spectrum_parameter(material.spectrum_value_0, material.spectrum_data_0.x, bindings, context);
        const float fresnel_l = dielectric_fresnel(no_l, eta);
        const float3 diffuse = reflectance * (1.0 - fresnel_v) * (1.0 - fresnel_l) / raster_preview_pi;
        return 0.08 * reflectance + 3.0 * no_l * (diffuse + specular);
    }
    return 0.025 * fresnel_v + 3.0 * no_l * specular;
}

[shader("fragment")]
RasterFragmentOutput raster_fragment(RasterVertex input) {
    float fragment_depth = input.position.z;
    float wire_coverage = 0.0;
    if (input.sphere == 0 && push_data.attributes.y == raster_display_wireframe) {
        const float3 wire_width = max(fwidth(input.wire_coordinates), float3(1.0e-6));
        const float wire_distance = min(input.wire_coordinates.x / wire_width.x, min(input.wire_coordinates.y / wire_width.y, input.wire_coordinates.z / wire_width.z));
        wire_coverage = 1.0 - smoothstep(0.65, 1.35, wire_distance);
    }
    if (push_data.attributes.y == raster_display_wireframe) {
        if (wire_coverage <= 0.0) discard;
        return raster_wireframe_output(fragment_depth, wire_coverage);
    }

    if (input.sphere != 0) {
        StructuredBuffer<RasterSceneBindings> sphere_binding_table = push_data.bindings;
        const RasterSceneBindings sphere_bindings = sphere_binding_table[0];
        StructuredBuffer<RasterPrimitive> sphere_primitives = sphere_bindings.primitives;
        StructuredBuffer<RasterTransform> sphere_transforms = sphere_bindings.transforms;
        const RasterPrimitive sphere_primitive = sphere_primitives[push_data.scene_primitive_index];
        const RasterTransform sphere_transform = sphere_transforms[sphere_primitive.transform_index];
        const bool perspective = push_data.camera_position.w != 0.0;
        const float3 ray_direction = perspective ? normalize(input.world_position - push_data.camera_position.xyz) : push_data.camera_forward.xyz;
        const float3 ray_origin = perspective ? push_data.camera_position.xyz : input.world_position + dot(push_data.camera_position.xyz - input.world_position, ray_direction) * ray_direction;
        const float3 object_ray_origin = raster_inverse_transform_position(sphere_transform, ray_origin);
        const float3 object_ray_direction = raster_inverse_transform_vector(sphere_transform, ray_direction);
        const float3 offset = object_ray_origin - input.sphere_center;
        const float direction_length_squared = dot(object_ray_direction, object_ray_direction);
        const float half_b = dot(offset, object_ray_direction);
        const float discriminant = half_b * half_b - direction_length_squared * (dot(offset, offset) - input.sphere_radius * input.sphere_radius);
        if (discriminant < 0.0) discard;
        const float distance = (-half_b - sqrt(discriminant)) / direction_length_squared;
        if (distance <= 0.0) discard;
        input.world_position = ray_origin + distance * ray_direction;
        const float3 object_normal = normalize(raster_inverse_transform_position(sphere_transform, input.world_position) - input.sphere_center);
        input.world_normal = raster_transform_normal(sphere_transform, object_normal);
        input.world_tangent = normalize(abs(input.world_normal.z) < 0.999 ? cross(float3(0.0, 0.0, 1.0), input.world_normal) : float3(1.0, 0.0, 0.0));
        const float phi = atan2(object_normal.y, object_normal.x);
        input.texture_coordinates = float2((phi < 0.0 ? phi + 2.0 * raster_preview_pi : phi) / (2.0 * raster_preview_pi), acos(clamp(object_normal.z, -1.0, 1.0)) / raster_preview_pi);
        const float4 homogeneous = float4(input.world_position, 1.0);
        const float4 clip = float4(dot(push_data.view_projection_row_0, homogeneous), dot(push_data.view_projection_row_1, homogeneous), dot(push_data.view_projection_row_2, homogeneous), dot(push_data.view_projection_row_3, homogeneous));
        fragment_depth = clip.z / clip.w;
    }

    StructuredBuffer<RasterSceneBindings> binding_table = push_data.bindings;
    const RasterSceneBindings bindings = binding_table[0];
    StructuredBuffer<RasterMaterial> materials = bindings.materials;
    RasterTextureContext texture_context = {
        input.world_position,
        normalize(input.world_normal),
        input.texture_coordinates,
        ddx(input.world_position),
        ddy(input.world_position),
        ddx(input.texture_coordinates),
        ddy(input.texture_coordinates),
    };
    StructuredBuffer<RasterPrimitive> primitives = bindings.primitives;
    const RasterPrimitive primitive = primitives[push_data.scene_primitive_index];
    if (primitive.alpha_texture != 0xffffffff && evaluate_raster_texture(bindings, primitive.alpha_texture, texture_context).x < 0.5) {
        if (wire_coverage <= 0.0) discard;
        return raster_wireframe_output(fragment_depth, 1.0);
    }

    const float3 outgoing = push_data.camera_position.w != 0.0 ? normalize(push_data.camera_position.xyz - input.world_position) : -push_data.camera_forward.xyz;
    StructuredBuffer<RasterMaterialRange> material_ranges = bindings.material_ranges;
    StructuredBuffer<RasterMaterialTerm> material_terms = bindings.material_terms;
    StructuredBuffer<RasterMaterialFactor> material_factors = bindings.material_factors;
    const RasterMaterialRange material_range = material_ranges[input.material];
    if (material_range.range.y == 0) {
        if (wire_coverage <= 0.0) discard;
        return raster_wireframe_output(fragment_depth, 1.0);
    }

    float3 color = float3(0.0);
    if (input.area_light != 0xffffffff) {
        StructuredBuffer<RasterAreaLight> area_lights = bindings.area_lights;
        const RasterAreaLight area_light = area_lights[input.area_light];
        const bool front_facing = dot(input.world_normal, outgoing) * ((area_light.metadata.y & 2) == 0 ? 1.0 : -1.0) > 0.0;
        if ((area_light.metadata.y & 1) != 0 || front_facing) {
            color = area_light.emission.xyz;
            if (area_light.metadata.x != 0xffffffff) color *= evaluate_raster_texture(bindings, area_light.metadata.x, texture_context).xyz;
        } else color = float3(0.0);
    }

    for (uint term_index = 0; term_index != material_range.range.y; ++term_index) {
        const RasterMaterialTerm term = material_terms[material_range.range.x + term_index];
        float weight = 1.0;
        for (uint factor_index = 0; factor_index != term.data.z; ++factor_index) {
            const RasterMaterialFactor factor = material_factors[term.data.y + factor_index];
            float amount = clamp(raster_float_parameter(factor.parameters.x, factor.metadata.x, bindings, texture_context), 0.0, 1.0);
            if (factor.metadata.y != 0) amount = 1.0 - amount;
            weight *= amount;
        }
        const RasterMaterial material = materials[term.data.x];
        if (material.metadata.x != raster_material_interface) color += weight * raster_preview_leaf(material, bindings, texture_context, input.world_tangent, outgoing);
    }

    return {float4(max(color, float3(0.0)), 1.0), fragment_depth};
}
