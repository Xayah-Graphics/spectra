module raster_mesh;

import raster_scene;

struct RasterVertex {
    float4 position : SV_Position;
    float3 world_position : POSITION;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float2 texture_coordinates : TEXCOORD;
    nointerpolation uint material : MATERIAL;
    nointerpolation uint area_light : AREA_LIGHT;
    nointerpolation uint sphere : SPHERE;
    nointerpolation float3 sphere_center : POSITION1;
    nointerpolation float sphere_radius : RADIUS;
    noperspective float3 wire_coordinates : TEXCOORD1;
}

[vk::push_constant]
uniform RasterPushData push_data;

float3 orthogonal(float3 normal) {
    if (abs(normal.x) > abs(normal.y))
        return normalize(
            float3(
                -normal.z,
                0.0,
                normal.x));
    return normalize(
        float3(
            0.0,
            normal.z,
            -normal.y));
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void raster_mesh(
    uint3 group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    out vertices RasterVertex vertices[96],
    out indices uint3 triangles[32]) {
    const uint first_triangle =
        group_id.x * 32;
    const uint emitted_triangles =
        min(
            32,
            push_data.element_count -
                first_triangle);
    SetMeshOutputCounts(
        emitted_triangles * 3,
        emitted_triangles);
    if (thread_id >= emitted_triangles) return;

    StructuredBuffer<float3> positions =
        push_data.positions;
    StructuredBuffer<float3> normals =
        push_data.normals;
    StructuredBuffer<float3> tangents =
        push_data.tangents;
    StructuredBuffer<float2> texture_coordinates =
        push_data.texture_coordinates;
    StructuredBuffer<uint> indices =
        push_data.indices;
    StructuredBuffer<RasterSceneBindings> binding_table =
        push_data.bindings;
    const RasterSceneBindings bindings =
        binding_table[0];
    StructuredBuffer<RasterPrimitive> primitives =
        bindings.primitives;
    StructuredBuffer<RasterTransform> transforms =
        bindings.transforms;
    StructuredBuffer<uint> face_materials =
        bindings.face_materials;
    const RasterPrimitive primitive =
        primitives[
            push_data.scene_primitive_index];
    const RasterTransform transform =
        transforms[
            primitive.transform_index];
    const uint triangle =
        first_triangle +
        thread_id;
    const uint material =
        primitive.face_material_count == 0
            ? primitive.material_index
            : face_materials[
                  primitive.face_material_offset +
                  triangle];
    const uint first_vertex =
        thread_id * 3;
    const float3 wire_coordinates[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0),
    };
    for (uint vertex = 0;
         vertex != 3;
         ++vertex) {
        const uint source_index =
            indices[
                triangle * 3 +
                vertex];
        const float3 world_position =
            raster_transform_position(
                transform,
                positions[source_index]);
        const float3 world_normal =
            raster_transform_normal(
                transform,
                normals[source_index]);
        const float3 transformed_tangent =
            (
                push_data.attributes.x &
                gpu_geometry_attribute_tangent
            ) != 0
                ? float3(
                      dot(
                          transform
                              .transform_row_0
                              .xyz,
                          tangents[source_index]),
                      dot(
                          transform
                              .transform_row_1
                              .xyz,
                          tangents[source_index]),
                      dot(
                          transform
                              .transform_row_2
                              .xyz,
                          tangents[source_index]))
                : orthogonal(world_normal);
        const float3 world_tangent =
            normalize(
                transformed_tangent -
                dot(
                    transformed_tangent,
                    world_normal) *
                    world_normal);
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
            world_normal;
        output.world_tangent =
            world_tangent;
        output.texture_coordinates =
            (
                push_data.attributes.x &
                gpu_geometry_attribute_texture_coordinate
            ) != 0
                ? texture_coordinates[
                      source_index]
                : float2(0.0);
        output.material =
            material;
        output.area_light =
            primitive.area_light_index;
        output.sphere = 0;
        output.sphere_center = float3(0.0);
        output.sphere_radius = 0.0;
        output.wire_coordinates =
            wire_coordinates[vertex];
        vertices[
            first_vertex +
            vertex] = output;
    }
    triangles[thread_id] =
        uint3(
            first_vertex,
            first_vertex + 1,
            first_vertex + 2);
}
