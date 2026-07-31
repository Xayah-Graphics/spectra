module picker;

import scene;

struct PickPushData {
    uint64_t acceleration_structure_address;
    DescriptorHandle<RWStructuredBuffer<uint>> result;
    DescriptorHandle<StructuredBuffer<GpuPickPrimitive>> primitives;
    uint2 camera_metadata;
    float4 camera_transform_row_0;
    float4 camera_transform_row_1;
    float4 camera_transform_row_2;
    float2 screen;
    float near_plane;
    float far_plane;
}

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure acceleration_structure;

[vk::push_constant]
uniform PickPushData push_data;

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
        phi +=
            2.0 *
            3.14159265358979323846;
    return phi;
}

float procedural_hit_distance(
    GpuPickPrimitive primitive,
    uint primitive_index,
    float3 origin,
    float3 direction,
    float minimum,
    float maximum) {
    uint kind =
        primitive.metadata.x;
    float4 parameters =
        primitive.parameters;
    if (kind == 6) {
        StructuredBuffer<float3> positions =
            primitive.positions;
        StructuredBuffer<float> radii =
            primitive.radii;
        const float radius =
            radii[primitive_index];
        origin -=
            positions[primitive_index];
        parameters =
            float4(
                radius,
                -radius,
                radius,
                2.0 *
                    3.14159265358979323846);
        kind = 1;
    }
    if (kind == 2) {
        if (direction.z == 0.0)
            return -1.0;
        const float distance =
            (
                parameters.x -
                origin.z
            ) /
            direction.z;
        if (
            distance <= minimum ||
            distance >= maximum)
            return -1.0;
        const float3 position =
            origin +
            distance * direction;
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
                    parameters.z ||
            positive_phi(position) >
                parameters.w)
            return -1.0;
        return distance;
    }
    const float a =
        kind == 1
            ? dot(direction, direction)
            : dot(
                  direction.xy,
                  direction.xy);
    const float half_b =
        kind == 1
            ? dot(origin, direction)
            : dot(
                  origin.xy,
                  direction.xy);
    const float c =
        kind == 1
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
        return -1.0;
    const float root =
        sqrt(discriminant);
    float distances[2] = {
        (-half_b - root) / a,
        (-half_b + root) / a,
    };
    for (uint index = 0;
         index != 2;
         ++index) {
        const float distance =
            distances[index];
        if (
            distance <= minimum ||
            distance >= maximum)
            continue;
        const float3 position =
            origin +
            distance * direction;
        float3 refined_position =
            position;
        if (kind == 1)
            refined_position *=
                parameters.x /
                length(refined_position);
        else
            refined_position.xy *=
                parameters.x /
                length(refined_position.xy);
        if (
            refined_position.z >=
                parameters.y -
                    1.0e-5 &&
            refined_position.z <=
                parameters.z +
                    1.0e-5 &&
            positive_phi(
                refined_position) <=
                parameters.w +
                    1.0e-5)
            return distance;
    }
    return -1.0;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void pick() {
    RayDesc ray;
    const float3 camera_origin = push_data.camera_metadata.x == 0
        ? float3(0.0)
        : float3(push_data.screen, 0.0);
    ray.Origin = float3(
        dot(push_data.camera_transform_row_0.xyz, camera_origin) + push_data.camera_transform_row_0.w,
        dot(push_data.camera_transform_row_1.xyz, camera_origin) + push_data.camera_transform_row_1.w,
        dot(push_data.camera_transform_row_2.xyz, camera_origin) + push_data.camera_transform_row_2.w);
    ray.TMin = push_data.near_plane;
    const float3 camera_direction = push_data.camera_metadata.x == 0
        ? normalize(float3(push_data.screen, -1.0))
        : float3(0.0, 0.0, -1.0);
    ray.Direction = normalize(float3(
        dot(push_data.camera_transform_row_0.xyz, camera_direction),
        dot(push_data.camera_transform_row_1.xyz, camera_direction),
        dot(push_data.camera_transform_row_2.xyz, camera_direction)));
    ray.TMax = push_data.far_plane;
    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(acceleration_structure, RAY_FLAG_NONE, 0xff, ray);
    StructuredBuffer<GpuPickPrimitive> primitives =
        push_data.primitives;
    RWStructuredBuffer<uint> result =
        push_data.result;
    while (query.Proceed())
        if (
            query.CandidateType() ==
            CANDIDATE_PROCEDURAL_PRIMITIVE) {
            const GpuPickPrimitive candidate =
                primitives[
                    query.CandidateInstanceID()];
            const float distance =
                procedural_hit_distance(
                    candidate,
                    query.CandidatePrimitiveIndex(),
                    query.CandidateObjectRayOrigin(),
                    query.CandidateObjectRayDirection(),
                    ray.TMin,
                    query.CommittedRayT());
            if (distance > 0.0)
                query.CommitProceduralPrimitiveHit(
                    distance);
        }
    if (
        query.CommittedStatus() ==
            COMMITTED_TRIANGLE_HIT ||
        query.CommittedStatus() ==
            COMMITTED_PROCEDURAL_PRIMITIVE_HIT) {
        result[0] =
            query.CommittedInstanceID();
    }
}
