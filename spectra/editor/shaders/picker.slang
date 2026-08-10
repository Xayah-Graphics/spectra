module picker;

struct ViewportPickPrimitive {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<float>> radii;
    uint4 metadata;
    float4 parameters;
}

struct PickPushData {
    uint64_t acceleration_structure_address;
    DescriptorHandle<RWStructuredBuffer<uint>> result;
    DescriptorHandle<StructuredBuffer<ViewportPickPrimitive>> primitives;
    DescriptorHandle<Texture2D<float>> depth;
    uint2 camera_metadata;
    uint2 pixel;
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
    ViewportPickPrimitive primitive,
    float3 origin,
    float3 direction,
    float minimum,
    float maximum,
    uint primitive_index) {
    uint kind =
        primitive.metadata.x;
    float4 parameters =
        primitive.parameters;
    if (kind == 4) {
        StructuredBuffer<float3> positions = primitive.positions;
        StructuredBuffer<float> radii = primitive.radii;
        const float radius = radii[primitive_index];
        origin -= positions[primitive_index];
        parameters = float4(radius, -radius, radius, 2.0 * 3.14159265358979323846);
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
    Texture2D<float> depth =
        push_data.depth;
    const float visible_depth =
        depth.Load(
            int3(
                push_data.pixel,
                0));
    const float candidate_depth_scale =
        push_data.camera_metadata.x == 0
            ? -camera_direction.z
            : 1.0;
    RayQuery<RAY_FLAG_FORCE_NON_OPAQUE> query;
    query.TraceRayInline(acceleration_structure, RAY_FLAG_FORCE_NON_OPAQUE, 0xff, ray);
    StructuredBuffer<ViewportPickPrimitive> primitives =
        push_data.primitives;
    RWStructuredBuffer<uint> result =
        push_data.result;
    while (query.Proceed()) {
        float distance = -1.0;
        if (
            query.CandidateType() ==
            CANDIDATE_NON_OPAQUE_TRIANGLE)
            distance =
                query.CandidateTriangleRayT();
        else if (
            query.CandidateType() ==
            CANDIDATE_PROCEDURAL_PRIMITIVE) {
            const ViewportPickPrimitive candidate =
                primitives[
                    query.CandidateInstanceID()];
            distance =
                procedural_hit_distance(
                    candidate,
                    query.CandidateObjectRayOrigin(),
                    query.CandidateObjectRayDirection(),
                    ray.TMin,
                    query.CommittedRayT(),
                    query.CandidatePrimitiveIndex());
        }
        if (distance <= 0.0) continue;
        const float camera_depth =
            distance *
            candidate_depth_scale;
        const float candidate_device_depth =
            push_data.camera_metadata.x == 0
                ? push_data.far_plane /
                      (push_data.far_plane - push_data.near_plane) -
                      push_data.far_plane * push_data.near_plane /
                          ((push_data.far_plane - push_data.near_plane) * camera_depth)
                : (camera_depth - push_data.near_plane) /
                      (push_data.far_plane - push_data.near_plane);
        if (abs(candidate_device_depth - visible_depth) > 0.001) continue;
        if (
            query.CandidateType() ==
            CANDIDATE_NON_OPAQUE_TRIANGLE)
            query.CommitNonOpaqueTriangleHit();
        else
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
