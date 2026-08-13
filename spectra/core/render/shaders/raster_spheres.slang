module raster_spheres;

import raster_scene;

struct RasterSpherePushData {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<uint>> reserved_0;
    DescriptorHandle<StructuredBuffer<uint>> reserved_1;
    DescriptorHandle<StructuredBuffer<uint>> reserved_2;
    DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    uint scene_primitive_index;
    uint element_count;
    uint4 attributes;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
    float4 film;
}

[vk::push_constant]
uniform RasterSpherePushData push_data;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void raster_spheres(
    uint3 group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    out vertices RasterVertex vertices[128],
    out indices uint3 triangles[64]) {
    const uint first_sphere =
        group_id.x * 32;
    const uint emitted_spheres =
        min(
            32,
            push_data.element_count -
                first_sphere);
    SetMeshOutputCounts(
        emitted_spheres * 4,
        emitted_spheres * 2);
    if (thread_id >= emitted_spheres) return;

    StructuredBuffer<float3> positions =
        push_data.positions;
    StructuredBuffer<float> radii =
        push_data.radii;
    StructuredBuffer<RasterSceneBindings> binding_table =
        push_data.bindings;
    const RasterSceneBindings bindings =
        binding_table[0];
    StructuredBuffer<RasterPrimitive> primitives =
        bindings.primitives;
    StructuredBuffer<RasterTransform> transforms =
        bindings.transforms;
    const RasterPrimitive primitive =
        primitives[
            push_data.scene_primitive_index];
    const RasterTransform transform =
        transforms[
            primitive.transform_index];
    const uint sphere =
        first_sphere +
        thread_id;
    const float3 center =
        positions[sphere];
    const float radius =
        radii[sphere];
    const float3 world_center =
        raster_transform_position(
            transform,
            center);
    const float3 column_0 =
        float3(
            transform.transform_row_0.x,
            transform.transform_row_1.x,
            transform.transform_row_2.x);
    const float3 column_1 =
        float3(
            transform.transform_row_0.y,
            transform.transform_row_1.y,
            transform.transform_row_2.y);
    const float3 column_2 =
        float3(
            transform.transform_row_0.z,
            transform.transform_row_1.z,
            transform.transform_row_2.z);
    const float world_radius =
        radius *
        max(
            length(column_0),
            max(
                length(column_1),
                length(column_2)));
    const float2 corners[4] = {
        float2(-1.0, -1.0),
        float2(1.0, -1.0),
        float2(-1.0, 1.0),
        float2(1.0, 1.0),
    };
    const uint first_vertex =
        thread_id * 4;
    for (uint vertex = 0;
         vertex != 4;
         ++vertex) {
        const float3 world_position =
            world_center +
            world_radius *
                (
                    corners[vertex].x *
                        push_data.camera_right.xyz +
                    corners[vertex].y *
                        push_data.camera_up.xyz
                );
        const float4 homogeneous =
            float4(
                world_position,
                1.0);
        RasterVertex output;
        output.position = float4(
            dot(
                push_data
                    .view_projection_row_0,
                homogeneous),
            dot(
                push_data
                    .view_projection_row_1,
                homogeneous),
            dot(
                push_data
                    .view_projection_row_2,
                homogeneous),
            dot(
                push_data
                    .view_projection_row_3,
                homogeneous));
        output.world_position =
            world_position;
        output.world_normal =
            normalize(
                world_position -
                world_center);
        output.world_tangent =
            push_data.camera_right.xyz;
        output.texture_coordinates =
            0.5 *
            (
                corners[vertex] +
                1.0
            );
        output.material =
            primitive.material_index;
        output.area_light =
            primitive.area_light_index;
        output.sphere = 1;
        output.sphere_center =
            center;
        output.sphere_radius =
            radius;
        output.wire_coordinates =
            float3(1.0);
        vertices[
            first_vertex +
            vertex] = output;
    }
    const uint3 local_triangles[2] = {
        uint3(0, 1, 2),
        uint3(2, 1, 3),
    };
    const uint first_triangle =
        thread_id * 2;
    for (uint triangle = 0;
         triangle != 2;
         ++triangle)
        triangles[
            first_triangle +
            triangle] =
                local_triangles[triangle] +
                first_vertex;
}
