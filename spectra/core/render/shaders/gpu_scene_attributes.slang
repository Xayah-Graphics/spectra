module gpu_scene_attributes;

static const uint gpu_geometry_attribute_normal = 1u << 0u;
static const uint gpu_geometry_attribute_tangent = 1u << 1u;
static const uint gpu_geometry_attribute_texture_coordinate = 1u << 2u;

struct GpuSceneAttributePushData {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<RWStructuredBuffer<float3>> normals;
    DescriptorHandle<RWStructuredBuffer<float3>> tangents;
    DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    uint2 reserved;
    uint vertex_count;
    uint index_count;
    uint attribute_flags;
    uint reserved_metadata;
}

[vk::push_constant]
uniform GpuSceneAttributePushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void clear_missing_attributes(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint vertex = dispatch_thread_id.x;
    if (vertex >= push_data.vertex_count) return;
    RWStructuredBuffer<float3> normals =
        push_data.normals;
    RWStructuredBuffer<float3> tangents =
        push_data.tangents;
    if ((push_data.attribute_flags & gpu_geometry_attribute_normal) != 0)
        normals[vertex] = float3(0.0);
    if ((push_data.attribute_flags & gpu_geometry_attribute_tangent) != 0)
        tangents[vertex] = float3(0.0);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void accumulate_missing_attributes(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint triangle = dispatch_thread_id.x;
    if (triangle * 3u >= push_data.index_count) return;
    StructuredBuffer<float3> positions =
        push_data.positions;
    StructuredBuffer<float2> texture_coordinates =
        push_data.texture_coordinates;
    StructuredBuffer<uint> indices =
        push_data.indices;
    RWStructuredBuffer<float3> normals =
        push_data.normals;
    RWStructuredBuffer<float3> tangents =
        push_data.tangents;
    const uint3 vertices = uint3(
        indices[triangle * 3u],
        indices[triangle * 3u + 1u],
        indices[triangle * 3u + 2u]);
    const float3 first_position = positions[vertices.x];
    const float3 edge_1 =
        positions[vertices.y] - first_position;
    const float3 edge_2 =
        positions[vertices.z] - first_position;
    if ((push_data.attribute_flags & gpu_geometry_attribute_normal) != 0) {
        const float3 normal = cross(edge_1, edge_2);
        InterlockedAdd(normals[vertices.x].x, normal.x);
        InterlockedAdd(normals[vertices.x].y, normal.y);
        InterlockedAdd(normals[vertices.x].z, normal.z);
        InterlockedAdd(normals[vertices.y].x, normal.x);
        InterlockedAdd(normals[vertices.y].y, normal.y);
        InterlockedAdd(normals[vertices.y].z, normal.z);
        InterlockedAdd(normals[vertices.z].x, normal.x);
        InterlockedAdd(normals[vertices.z].y, normal.y);
        InterlockedAdd(normals[vertices.z].z, normal.z);
    }
    if (
        (push_data.attribute_flags & gpu_geometry_attribute_tangent) != 0 &&
        (push_data.attribute_flags & gpu_geometry_attribute_texture_coordinate) != 0) {
        const float2 first_uv =
            texture_coordinates[vertices.x];
        const float2 uv_1 =
            texture_coordinates[vertices.y] -
            first_uv;
        const float2 uv_2 =
            texture_coordinates[vertices.z] -
            first_uv;
        const float determinant =
            uv_1.x * uv_2.y -
            uv_1.y * uv_2.x;
        if (abs(determinant) > 1.0e-12) {
            const float3 tangent =
                (edge_1 * uv_2.y -
                 edge_2 * uv_1.y) /
                determinant;
            InterlockedAdd(
                tangents[vertices.x].x,
                tangent.x);
            InterlockedAdd(
                tangents[vertices.x].y,
                tangent.y);
            InterlockedAdd(
                tangents[vertices.x].z,
                tangent.z);
            InterlockedAdd(
                tangents[vertices.y].x,
                tangent.x);
            InterlockedAdd(
                tangents[vertices.y].y,
                tangent.y);
            InterlockedAdd(
                tangents[vertices.y].z,
                tangent.z);
            InterlockedAdd(
                tangents[vertices.z].x,
                tangent.x);
            InterlockedAdd(
                tangents[vertices.z].y,
                tangent.y);
            InterlockedAdd(
                tangents[vertices.z].z,
                tangent.z);
        }
    }
}

[shader("compute")]
[numthreads(256, 1, 1)]
void normalize_missing_attributes(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint vertex = dispatch_thread_id.x;
    if (vertex >= push_data.vertex_count) return;
    RWStructuredBuffer<float3> normals =
        push_data.normals;
    RWStructuredBuffer<float3> tangents =
        push_data.tangents;
    float3 normal = normals[vertex];
    if ((push_data.attribute_flags & gpu_geometry_attribute_normal) != 0) {
        const float length_squared = dot(normal, normal);
        normal =
            length_squared > 1.0e-20
                ? normal * rsqrt(length_squared)
                : float3(0.0, 1.0, 0.0);
        normals[vertex] = normal;
    }
    if ((push_data.attribute_flags & gpu_geometry_attribute_tangent) != 0) {
        float3 tangent = tangents[vertex];
        tangent -= normal * dot(normal, tangent);
        if (dot(tangent, tangent) <= 1.0e-20)
            tangent =
                abs(normal.y) < 0.99
                    ? cross(
                          float3(0.0, 1.0, 0.0),
                          normal)
                    : cross(
                          float3(1.0, 0.0, 0.0),
                          normal);
        tangents[vertex] =
            normalize(tangent);
    }
}
