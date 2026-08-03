module path_procedural;

import path_scene;
import path_random;
import path_texture;
import path_queue;
import path_sampling;

[vk::push_constant]
uniform WavefrontPushData push_data;

struct ProceduralIntersectionAttributes {
    float2 texture_coordinates;
}

float positive_phi(float3 position) {
    if (
        position.x == 0.0 &&
        position.y == 0.0)
        return 0.0;
    float phi =
        atan2(
            position.y,
            position.x);
    if (phi < 0.0)
        phi += 2.0 * 3.14159265358979323846;
    return phi;
}

bool valid_shape_hit(
    uint kind,
    float4 parameters,
    float3 position,
    out float2 texture_coordinates) {
    const float phi =
        positive_phi(position);
    if (kind == geometry_sphere) {
        if (
            position.z < parameters.y ||
            position.z > parameters.z ||
            phi > parameters.w)
            return false;
        const float theta =
            acos(
                clamp(
                    position.z /
                        parameters.x,
                    -1.0,
                    1.0));
        const float theta_min =
            acos(
                clamp(
                    parameters.y /
                        parameters.x,
                    -1.0,
                    1.0));
        const float theta_max =
            acos(
                clamp(
                    parameters.z /
                        parameters.x,
                    -1.0,
                    1.0));
        texture_coordinates =
            float2(
                phi / parameters.w,
                (
                    theta -
                    theta_min
                ) /
                    (
                        theta_max -
                        theta_min
                    ));
        return true;
    }
    if (kind == geometry_cylinder) {
        if (
            position.z < parameters.y ||
            position.z > parameters.z ||
            phi > parameters.w)
            return false;
        texture_coordinates =
            float2(
                phi / parameters.w,
                (
                    position.z -
                    parameters.y
                ) /
                    (
                        parameters.z -
                        parameters.y
                    ));
        return true;
    }
    return false;
}

void report_quadric_hit(
    uint kind,
    float4 parameters,
    float3 origin,
    float3 direction,
    float t) {
    if (
        t <= RayTMin() ||
        t >= RayTCurrent())
        return;
    float3 position =
        origin +
        t * direction;
    if (kind == geometry_sphere)
        position *=
            parameters.x /
            length(position);
    else {
        const float radial =
            length(position.xy);
        position.xy *=
            parameters.x /
            radial;
    }
    ProceduralIntersectionAttributes
        attributes;
    if (
        valid_shape_hit(
            kind,
            parameters,
            position,
            attributes.texture_coordinates))
        ReportHit(
            t,
            0,
            attributes);
}

[shader("intersection")]
void procedural_intersection() {
    StructuredBuffer<PathSceneBindings>
        binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings bindings =
        binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        bindings.primitives;
    const PathPrimitive primitive =
        primitives[InstanceID()];
    uint kind =
        primitive.attributes.z;
    float4 parameters =
        primitive.geometry_parameters;
    float3 origin =
        ObjectRayOrigin();
    const float3 direction =
        ObjectRayDirection();
    if (kind == geometry_disk) {
        if (direction.z == 0.0)
            return;
        const float t =
            (
                parameters.x -
                origin.z
            ) /
            direction.z;
        if (
            t <= RayTMin() ||
            t >= RayTCurrent())
            return;
        const float3 position =
            origin +
            t * direction;
        const float radius_squared =
            dot(
                position.xy,
                position.xy);
        if (
            radius_squared >
                parameters.y *
                    parameters.y ||
            radius_squared <
                parameters.z *
                    parameters.z)
            return;
        const float phi =
            positive_phi(position);
        if (phi > parameters.w)
            return;
        const float radius =
            sqrt(radius_squared);
        ProceduralIntersectionAttributes
            attributes;
        attributes.texture_coordinates =
            float2(
                phi / parameters.w,
                (
                    parameters.y -
                    radius
                ) /
                    (
                        parameters.y -
                        parameters.z
                    ));
        ReportHit(
            t,
            0,
            attributes);
        return;
    }
    const float a =
        kind == geometry_sphere
            ? dot(direction, direction)
            : dot(
                  direction.xy,
                  direction.xy);
    const float half_b =
        kind == geometry_sphere
            ? dot(origin, direction)
            : dot(
                  origin.xy,
                  direction.xy);
    const float c =
        kind == geometry_sphere
            ? dot(origin, origin) -
                  parameters.x *
                      parameters.x
            : dot(
                  origin.xy,
                  origin.xy) -
                  parameters.x *
                      parameters.x;
    const float discriminant =
        half_b * half_b -
        a * c;
    if (discriminant < 0.0)
        return;
    const float root =
        sqrt(discriminant);
    const float q =
        -half_b -
        (
            half_b < 0.0
                ? -root
                : root
        );
    float first;
    float second;
    if (q == 0.0) {
        first =
            -half_b / a;
        second = first;
    }
    else {
        first = q / a;
        second = c / q;
    }
    if (first > second) {
        const float temporary = first;
        first = second;
        second = temporary;
    }
    report_quadric_hit(
        kind,
        parameters,
        origin,
        direction,
        first);
    report_quadric_hit(
        kind,
        parameters,
        origin,
        direction,
        second);
}

bool procedural_alpha_killed(
    ProceduralIntersectionAttributes attributes,
    in out float3 alpha_origin) {
    StructuredBuffer<PathSceneBindings>
        binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontBindings>
        wavefront_binding_buffer =
            push_data.bindings;
    const WavefrontBindings wavefront_bindings =
        wavefront_binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        bindings.primitives;
    const PathPrimitive primitive =
        primitives[InstanceID()];
    if (primitive.attributes.y == 0xffffffff)
        return false;
    uint kind =
        primitive.attributes.z;
    float4 parameters =
        primitive.geometry_parameters;
    const float3 object_position =
        ObjectRayOrigin() +
        RayTCurrent() *
            ObjectRayDirection();
    float3 local_position =
        object_position;
    float3 object_normal =
        kind == geometry_sphere
            ? normalize(local_position)
        : kind == geometry_disk
            ? float3(0.0, 0.0, 1.0)
            : normalize(
                  float3(
                      local_position.xy,
                      0.0));
    const float3x3 normal_transform =
        transpose(
            (float3x3)
                WorldToObject3x4());
    float3 world_normal =
        normalize(
            mul(
                normal_transform,
                object_normal));
    if ((primitive.flags & 1) != 0)
        world_normal =
            -world_normal;
    const float3 world_position =
        mul(
            ObjectToWorld3x4(),
            float4(
                object_position,
                1.0));
    const TextureEvaluationContext context = {
        world_position,
        world_normal,
        attributes.texture_coordinates,
        float3(0.0),
        float3(0.0),
        float2(0.0),
        float2(0.0),
        uint2(0),
        wavefront_bindings
            .evaluated_texture_values,
        DispatchRaysIndex().x *
            bindings.metadata.w,
        uint2(0),
    };
    DescriptorHandle<RWStructuredBuffer<float4>>
        texture_evaluation_stack =
            wavefront_bindings
                .texture_evaluation_stack;
    const float alpha =
        evaluate_texture_program(
            bindings,
            primitive.attributes.y,
            context,
            float4(0.0),
            texture_evaluation_stack,
            DispatchRaysIndex().x *
                bindings.metadata.y)
            .x;
    const float3 ray_direction =
        WorldRayDirection();
    if (!(
        alpha <= 0.0 ||
        (
            alpha < 1.0 &&
            hash_float_6(
                asuint(
                    alpha_origin.x),
                asuint(
                    alpha_origin.y),
                asuint(
                    alpha_origin.z),
                asuint(
                    ray_direction.x),
                asuint(
                    ray_direction.y),
                asuint(
                    ray_direction.z)) >
                alpha
        )))
        return false;
    alpha_origin =
        offset_ray_origin(
            world_position,
            dot(
                world_normal,
                ray_direction) >
                    0.0
                ? world_normal
                : -world_normal);
    return true;
}

[shader("anyhit")]
void procedural_alpha_any_hit_surface(
    ProceduralIntersectionAttributes attributes,
    in out SurfacePayload payload) {
    float3 alpha_origin =
        float3(
            payload.alpha_origin_x,
            payload.alpha_origin_y,
            payload.alpha_origin_z);
    if (
        procedural_alpha_killed(
            attributes,
            alpha_origin)) {
        payload.alpha_origin_x =
            alpha_origin.x;
        payload.alpha_origin_y =
            alpha_origin.y;
        payload.alpha_origin_z =
            alpha_origin.z;
        IgnoreHit();
    }
}

[shader("anyhit")]
void procedural_alpha_any_hit_shadow(
    ProceduralIntersectionAttributes attributes,
    in out TransmittancePayload payload) {
    if (
        procedural_alpha_killed(
            attributes,
            payload.alpha_origin))
        IgnoreHit();
}

[shader("closesthit")]
void procedural_closest_hit(
    ProceduralIntersectionAttributes attributes,
    in out SurfacePayload payload) {
    StructuredBuffer<PathSceneBindings>
        binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings bindings =
        binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        bindings.primitives;
    const PathPrimitive primitive =
        primitives[InstanceID()];
    uint kind =
        primitive.attributes.z;
    float4 parameters =
        primitive.geometry_parameters;
    float3 position =
        ObjectRayOrigin() +
        RayTCurrent() *
            ObjectRayDirection();
    float3 object_normal;
    float3 object_dpdu;
    float3 object_dpdv;
    float3 object_dndu;
    float3 object_dndv;
    if (kind == geometry_sphere) {
        position *=
            parameters.x /
            length(position);
        object_normal =
            position /
            parameters.x;
        const float phi =
            positive_phi(position);
        const float cosine_phi =
            cos(phi);
        const float sine_phi =
            sin(phi);
        const float cosine_theta =
            clamp(
                position.z /
                    parameters.x,
                -1.0,
                1.0);
        const float sine_theta =
            sqrt(
                max(
                    0.0,
                    1.0 -
                        cosine_theta *
                            cosine_theta));
        const float theta_min =
            acos(
                clamp(
                    parameters.y /
                        parameters.x,
                    -1.0,
                    1.0));
        const float theta_max =
            acos(
                clamp(
                    parameters.z /
                        parameters.x,
                    -1.0,
                    1.0));
        object_dpdu =
            float3(
                -parameters.w *
                    position.y,
                parameters.w *
                    position.x,
                0.0);
        object_dpdv =
            (
                theta_max -
                theta_min
            ) *
            float3(
                position.z *
                    cosine_phi,
                position.z *
                    sine_phi,
                -parameters.x *
                    sine_theta);
        object_dndu =
            object_dpdu /
            parameters.x;
        object_dndv =
            object_dpdv /
            parameters.x;
    }
    else if (kind == geometry_disk) {
        position.z = parameters.x;
        object_normal =
            float3(0.0, 0.0, 1.0);
        object_dpdu =
            float3(
                -parameters.w *
                    position.y,
                parameters.w *
                    position.x,
                0.0);
        const float radius =
            length(position.xy);
        object_dpdv =
            radius == 0.0
                ? float3(
                      parameters.z -
                          parameters.y,
                      0.0,
                      0.0)
                : float3(
                      position.xy,
                      0.0) *
                      (
                          parameters.z -
                          parameters.y
                      ) /
                      radius;
        object_dndu =
            float3(0.0);
        object_dndv =
            float3(0.0);
    }
    else {
        const float radial =
            length(position.xy);
        position.xy *=
            parameters.x /
            radial;
        object_normal =
            float3(
                position.xy /
                    parameters.x,
                0.0);
        object_dpdu =
            float3(
                -parameters.w *
                    position.y,
                parameters.w *
                    position.x,
                0.0);
        object_dpdv =
            float3(
                0.0,
                0.0,
                parameters.z -
                    parameters.y);
        object_dndu =
            object_dpdu /
            parameters.x;
        object_dndv =
            float3(0.0);
    }
    const float3x4 object_to_world =
        ObjectToWorld3x4();
    const float3x3 normal_transform =
        transpose(
            (float3x3)
                WorldToObject3x4());
    const float3 geometric_normal =
        normalize(
            mul(
                normal_transform,
                object_normal));
    const float3 dpdu =
        mul(
            (float3x3)
                object_to_world,
            object_dpdu);
    const float3 dpdv =
        mul(
            (float3x3)
                object_to_world,
            object_dpdv);
    payload.front_face =
        dot(
            (
                (primitive.flags & 1) != 0
                    ? -geometric_normal
                    : geometric_normal
            ),
            WorldRayDirection()) <
        0.0;
    payload.shading_normal =
        geometric_normal;
    payload.geometric_normal =
        geometric_normal;
    payload.tangent =
        normalize(dpdu);
    payload.dpdu = dpdu;
    payload.dpdv = dpdv;
    payload.dndu =
        mul(
            normal_transform,
            object_dndu);
    payload.dndv =
        mul(
            normal_transform,
            object_dndv);
    payload.texture_u =
        attributes.texture_coordinates.x;
    payload.texture_v =
        attributes.texture_coordinates.y;
    payload.hit_distance =
        RayTCurrent();
    payload.instance_index =
        InstanceID();
    payload.primitive_index =
        PrimitiveIndex();
    payload.hit = 1;
}

[shader("closesthit")]
void procedural_shadow_closest_hit(
    ProceduralIntersectionAttributes attributes,
    in out TransmittancePayload payload) {
    StructuredBuffer<PathSceneBindings>
        binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings bindings =
        binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        bindings.primitives;
    const PathPrimitive primitive =
        primitives[InstanceID()];
    uint kind =
        primitive.attributes.z;
    float4 parameters =
        primitive.geometry_parameters;
    float3 position =
        ObjectRayOrigin() +
        RayTCurrent() *
            ObjectRayDirection();
    float3 object_normal;
    if (kind == geometry_sphere)
        object_normal =
            normalize(position);
    else if (kind == geometry_disk)
        object_normal =
            float3(0.0, 0.0, 1.0);
    else
        object_normal =
            normalize(
                float3(
                    position.xy,
                    0.0));
    const float3x3 normal_transform =
        transpose(
            (float3x3)
                WorldToObject3x4());
    float3 geometric_normal =
        normalize(
            mul(
                normal_transform,
                object_normal));
    if ((primitive.flags & 1) != 0)
        geometric_normal =
            -geometric_normal;
    payload.hit_distance =
        RayTCurrent();
    payload.geometric_normal =
        geometric_normal;
    payload.instance_index =
        InstanceID();
    payload.primitive_index =
        PrimitiveIndex();
    payload.front_face =
        dot(
            geometric_normal,
            WorldRayDirection()) <
        0.0;
    payload.hit = 1;
}
