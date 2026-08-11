module path_opaque;

import path_scene;
import path_random;
import path_texture;
import path_queue;
import path_sampling;

[vk::push_constant]
uniform WavefrontPushData push_data;

float3 unpack_float3(PathPackedFloat3 value) {
    return float3(
        value.x,
        value.y,
        value.z);
}

float3 orthogonal_tangent(float3 normal) {
    return abs(normal.z) < 0.999
        ? normalize(
            cross(
                float3(0.0, 0.0, 1.0),
                normal))
        : float3(1.0, 0.0, 0.0);
}

bool triangle_alpha_killed(
    BuiltInTriangleIntersectionAttributes attributes,
    in out float3 alpha_origin) {
    StructuredBuffer<PathSceneBindings>
        binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontBindings>
        wavefront_binding_buffer =
            push_data.bindings;
    const WavefrontBindings wavefront_bindings =
        wavefront_binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        scene_bindings.primitives;
    const PathPrimitive primitive =
        primitives[InstanceID()];
    if (primitive.attributes.y == 0xffffffff)
        return false;
    StructuredBuffer<uint> indices =
        primitive.indices;
    const uint first_index =
        PrimitiveIndex() * 3;
    const uint3 vertex_indices =
        uint3(
            indices[first_index],
            indices[first_index + 1],
            indices[first_index + 2]);
    const float3 barycentric =
        float3(
            1.0 -
                attributes.barycentrics.x -
                attributes.barycentrics.y,
            attributes.barycentrics.x,
            attributes.barycentrics.y);
    const float3x4 object_to_world =
        ObjectToWorld3x4();
    const float3 world_position_0 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(0),
                1.0));
    const float3 world_position_1 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(1),
                1.0));
    const float3 world_position_2 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(2),
                1.0));
    float3 geometric_normal =
        normalize(
            cross(
                world_position_1 -
                    world_position_0,
                world_position_2 -
                    world_position_0));
    if ((primitive.flags & 1) != 0)
        geometric_normal =
            -geometric_normal;
    float2 texture_coordinates =
        float2(0.0);
    if (
        (
            primitive.attributes.x &
            gpu_geometry_attribute_texture_coordinate
        ) != 0) {
        StructuredBuffer<float2> coordinates =
            primitive.texture_coordinates;
        texture_coordinates =
            barycentric.x *
                coordinates[vertex_indices.x] +
            barycentric.y *
                coordinates[vertex_indices.y] +
            barycentric.z *
                coordinates[vertex_indices.z];
    }
    const TextureEvaluationContext context = {
        barycentric.x *
                world_position_0 +
            barycentric.y *
                world_position_1 +
            barycentric.z *
                world_position_2,
        geometric_normal,
        texture_coordinates,
        float3(0.0),
        float3(0.0),
        float2(0.0),
        float2(0.0),
        uint2(0),
        wavefront_bindings
            .evaluated_texture_values,
        DispatchRaysIndex().x *
            scene_bindings.metadata.w,
        uint2(0),
    };
    DescriptorHandle<RWStructuredBuffer<float4>>
        texture_evaluation_stack =
            wavefront_bindings
                .texture_evaluation_stack;
    const float alpha =
        evaluate_texture_program(
            scene_bindings,
            primitive.attributes.y,
            context,
            float4(0.0),
            texture_evaluation_stack,
            DispatchRaysIndex().x *
                scene_bindings.metadata.y)
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
    const float3 world_position =
        context.position;
    alpha_origin =
        offset_ray_origin(
            world_position,
            dot(
                geometric_normal,
                ray_direction) >
                    0.0
                ? geometric_normal
                : -geometric_normal);
    return true;
}

[shader("anyhit")]
void alpha_any_hit_surface(
    BuiltInTriangleIntersectionAttributes attributes,
    in out SurfacePayload payload) {
    float3 alpha_origin =
        float3(
            payload.alpha_origin_x,
            payload.alpha_origin_y,
            payload.alpha_origin_z);
    if (
        triangle_alpha_killed(
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
void alpha_any_hit_shadow(
    BuiltInTriangleIntersectionAttributes attributes,
    in out TransmittancePayload payload) {
    if (
        triangle_alpha_killed(
            attributes,
            payload.alpha_origin))
        IgnoreHit();
}

[shader("closesthit")]
void closest_hit(
    BuiltInTriangleIntersectionAttributes attributes,
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
    StructuredBuffer<uint> indices =
        primitive.indices;
    const uint first_index =
        PrimitiveIndex() * 3;
    const uint3 vertex_indices =
        uint3(
            indices[first_index],
            indices[first_index + 1],
            indices[first_index + 2]);
    const float3 object_position_0 =
        HitTriangleVertexPosition(0);
    const float3 object_position_1 =
        HitTriangleVertexPosition(1);
    const float3 object_position_2 =
        HitTriangleVertexPosition(2);
    const float3x4 object_to_world =
        ObjectToWorld3x4();
    const float3 world_position_0 =
        mul(
            object_to_world,
            float4(
                object_position_0,
                1.0));
    const float3 world_position_1 =
        mul(
            object_to_world,
            float4(
                object_position_1,
                1.0));
    const float3 world_position_2 =
        mul(
            object_to_world,
            float4(
                object_position_2,
                1.0));
    const float3 geometric_normal =
        normalize(
            cross(
                world_position_1 -
                    world_position_0,
                world_position_2 -
                    world_position_0));
    const float3 barycentric =
        float3(
            1.0 -
                attributes.barycentrics.x -
                attributes.barycentrics.y,
            attributes.barycentrics.x,
            attributes.barycentrics.y);
    float3 shading_normal =
        geometric_normal;
    if (
        (
            primitive.attributes.x &
            gpu_geometry_attribute_normal
        ) != 0) {
        StructuredBuffer<PathPackedFloat3> normals =
            primitive.normals;
        const float3 object_normal =
            normalize(
                barycentric.x *
                    unpack_float3(
                        normals[
                            vertex_indices.x]) +
                barycentric.y *
                    unpack_float3(
                        normals[
                            vertex_indices.y]) +
                barycentric.z *
                    unpack_float3(
                        normals[
                            vertex_indices.z]));
        const float3x3 normal_transform =
            transpose(
                (float3x3)
                    WorldToObject3x4());
        shading_normal =
            normalize(
                mul(
                    normal_transform,
                    object_normal));
        if (dot(
                shading_normal,
                geometric_normal) < 0.0)
            shading_normal =
                -shading_normal;
    }
    float2 texture_coordinates =
        float2(0.0);
    float3 tangent =
        float3(0.0);
    float3 dpdu =
        float3(0.0);
    float3 dpdv =
        float3(0.0);
    float3 dndu =
        float3(0.0);
    float3 dndv =
        float3(0.0);
    if (
        (
            primitive.attributes.x &
            gpu_geometry_attribute_texture_coordinate
        ) != 0) {
        StructuredBuffer<float2> coordinates =
            primitive.texture_coordinates;
        const float2 coordinate_0 =
            coordinates[vertex_indices.x];
        const float2 coordinate_1 =
            coordinates[vertex_indices.y];
        const float2 coordinate_2 =
            coordinates[vertex_indices.z];
        texture_coordinates =
            barycentric.x * coordinate_0 +
            barycentric.y * coordinate_1 +
            barycentric.z * coordinate_2;
        const float2 delta_0 =
            coordinate_1 -
            coordinate_0;
        const float2 delta_1 =
            coordinate_2 -
            coordinate_0;
        const float determinant =
            delta_0.x *
                delta_1.y -
            delta_0.y *
                delta_1.x;
        if (determinant != 0.0) {
            const float inverse_determinant =
                1.0 /
                determinant;
            const float3 object_dpdu =
                (
                    delta_1.y *
                        (
                            object_position_1 -
                            object_position_0
                        ) -
                    delta_0.y *
                        (
                            object_position_2 -
                            object_position_0
                        )
                ) *
                inverse_determinant;
            const float3 object_dpdv =
                (
                    -delta_1.x *
                        (
                            object_position_1 -
                            object_position_0
                        ) +
                    delta_0.x *
                        (
                            object_position_2 -
                            object_position_0
                        )
                ) *
                inverse_determinant;
            dpdu =
                mul(
                    (float3x3)
                        object_to_world,
                    object_dpdu);
            dpdv =
                mul(
                    (float3x3)
                        object_to_world,
                    object_dpdv);
            tangent =
                dpdu;
            if (
                (
                    primitive.attributes.x &
                    gpu_geometry_attribute_normal
                ) != 0) {
                StructuredBuffer<
                    PathPackedFloat3> normals =
                        primitive.normals;
                const float3 normal_0 =
                    unpack_float3(
                        normals[
                            vertex_indices.x]);
                const float3 normal_1 =
                    unpack_float3(
                        normals[
                            vertex_indices.y]);
                const float3 normal_2 =
                    unpack_float3(
                        normals[
                            vertex_indices.z]);
                const float3 object_dndu =
                    (
                        delta_1.y *
                            (
                                normal_1 -
                                normal_0
                            ) -
                        delta_0.y *
                            (
                                normal_2 -
                                normal_0
                            )
                    ) *
                    inverse_determinant;
                const float3 object_dndv =
                    (
                        -delta_1.x *
                            (
                                normal_1 -
                                normal_0
                            ) +
                        delta_0.x *
                            (
                                normal_2 -
                                normal_0
                            )
                    ) *
                    inverse_determinant;
                const float3x3 normal_transform =
                    transpose(
                        (float3x3)
                            WorldToObject3x4());
                dndu =
                    mul(
                        normal_transform,
                        object_dndu);
                dndv =
                    mul(
                        normal_transform,
                        object_dndv);
            }
        }
    }
    if (
        (
            primitive.attributes.x &
            gpu_geometry_attribute_tangent
        ) != 0) {
        StructuredBuffer<PathPackedFloat3> tangents =
            primitive.tangents;
        const float3 object_tangent =
            barycentric.x *
                unpack_float3(
                    tangents[
                        vertex_indices.x]) +
            barycentric.y *
                unpack_float3(
                    tangents[
                        vertex_indices.y]) +
            barycentric.z *
                unpack_float3(
                    tangents[
                        vertex_indices.z]);
        tangent =
            mul(
                (float3x3)
                    object_to_world,
                object_tangent);
    }
    tangent -=
        dot(
            tangent,
            shading_normal) *
        shading_normal;
    if (dot(tangent, tangent) == 0.0)
        tangent =
            orthogonal_tangent(
                shading_normal);
    else
        tangent =
            normalize(tangent);
    if (dot(
            cross(
                dpdu,
                dpdv),
            cross(
                dpdu,
                dpdv)) ==
        0.0) {
        dpdu =
            orthogonal_tangent(
                geometric_normal);
        dpdv =
            cross(
                geometric_normal,
                dpdu);
    }
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
        shading_normal;
    payload.geometric_normal =
        geometric_normal;
    payload.tangent = tangent;
    payload.dpdu = dpdu;
    payload.dpdv = dpdv;
    payload.dndu = dndu;
    payload.dndv = dndv;
    payload.texture_u =
        texture_coordinates.x;
    payload.texture_v =
        texture_coordinates.y;
    payload.hit_distance =
        RayTCurrent();
    payload.instance_index =
        InstanceID();
    payload.primitive_index =
        PrimitiveIndex();
    payload.hit = 1;
}

[shader("closesthit")]
void shadow_closest_hit(
    BuiltInTriangleIntersectionAttributes attributes,
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
    const float3x4 object_to_world =
        ObjectToWorld3x4();
    const float3 position_0 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(0),
                1.0));
    const float3 position_1 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(1),
                1.0));
    const float3 position_2 =
        mul(
            object_to_world,
            float4(
                HitTriangleVertexPosition(2),
                1.0));
    float3 geometric_normal =
        normalize(
            cross(
                position_1 -
                    position_0,
                position_2 -
                    position_0));
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
