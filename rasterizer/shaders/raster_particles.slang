module raster_particles;

import scene;

struct RasterVertex {
    float4 position : SV_Position;
    float3 world_position : POSITION;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float2 texture_coordinates : TEXCOORD;
    float3 emission : COLOR0;
    float3 color_modifier : COLOR1;
    nointerpolation uint material : MATERIAL;
}

struct RasterParticlePushData {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<float3>> colors;
    DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    uint instance_index;
    uint element_count;
    uint2 attributes;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
    float4 camera_position;
}

[vk::push_constant]
uniform RasterParticlePushData push_data;

float3 transform_position(
    RasterTransform transform,
    float3 position) {
    const float4 homogeneous =
        float4(position, 1.0);
    return float3(
        dot(
            transform.transform_row_0,
            homogeneous),
        dot(
            transform.transform_row_1,
            homogeneous),
        dot(
            transform.transform_row_2,
            homogeneous));
}

float3 transform_normal(
    RasterTransform transform,
    float3 normal) {
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
    return normalize(
        cross(column_1, column_2) *
                normal.x +
            cross(column_2, column_0) *
                normal.y +
            cross(column_0, column_1) *
                normal.z);
}

float2 sphere_texture_coordinates(
    float3 direction) {
    float phi =
        atan2(
            direction.y,
            direction.x);
    if (phi < 0.0)
        phi +=
            2.0 *
            3.14159265358979323846;
    return float2(
        phi /
            (
                2.0 *
                3.14159265358979323846
            ),
        acos(
            clamp(
                direction.z,
                -1.0,
                1.0)) /
            3.14159265358979323846);
}

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void raster_particles(
    uint3 group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    out vertices RasterVertex vertices[192],
    out indices uint3 triangles[256]) {
    const uint first_particle =
        group_id.x * 32;
    const uint emitted_particles =
        min(
            32,
            push_data.element_count -
                first_particle);
    SetMeshOutputCounts(
        emitted_particles * 6,
        emitted_particles * 8);
    if (thread_id >= emitted_particles) return;

    StructuredBuffer<float3> positions =
        push_data.positions;
    StructuredBuffer<float> radii =
        push_data.radii;
    StructuredBuffer<float3> colors =
        push_data.colors;
    StructuredBuffer<RasterSceneBindings> binding_table =
        push_data.bindings;
    const RasterSceneBindings bindings =
        binding_table[0];
    StructuredBuffer<RasterPrimitive> primitives =
        bindings.primitives;
    StructuredBuffer<RasterTransform> transforms =
        bindings.transforms;
    StructuredBuffer<uint> particle_materials =
        bindings.face_materials;
    StructuredBuffer<RasterAreaLight> area_lights =
        bindings.area_lights;
    const RasterPrimitive primitive =
        primitives[
            push_data.instance_index];
    const RasterTransform transform =
        transforms[
            primitive.transform];
    const uint particle =
        first_particle +
        thread_id;
    const uint material =
        primitive.face_material_count == 0
            ? primitive.material
            : particle_materials[
                  primitive.face_material_offset +
                  particle];
    const float3 center =
        positions[particle];
    const float radius =
        radii[particle];
    float3 color_modifier =
        float3(1.0);
    if (
        (
            push_data.attributes.x &
            2
        ) != 0)
        color_modifier =
            colors[particle];
    const float3 directions[6] = {
        float3(1.0, 0.0, 0.0),
        float3(-1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, -1.0, 0.0),
        float3(0.0, 0.0, 1.0),
        float3(0.0, 0.0, -1.0),
    };
    const uint first_vertex =
        thread_id * 6;
    for (uint vertex = 0;
         vertex != 6;
         ++vertex) {
        const float3 direction =
            directions[vertex];
        const float3 world_position =
            transform_position(
                transform,
                center +
                    radius *
                        direction);
        const float3 world_normal =
            transform_normal(
                transform,
                direction);
        const float3 world_tangent =
            transform_normal(
                transform,
                normalize(
                    abs(direction.z) < 0.999
                        ? cross(
                              float3(
                                  0.0,
                                  0.0,
                                  1.0),
                              direction)
                        : float3(
                              1.0,
                              0.0,
                              0.0)));
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
            sphere_texture_coordinates(
                direction);
        output.emission =
            primitive.area_light ==
                    0xffffffff
                ? float3(0.0)
                : area_lights[
                      primitive.area_light]
                      .emission.xyz;
        output.color_modifier =
            color_modifier;
        output.material =
            material;
        vertices[
            first_vertex +
            vertex] = output;
    }
    const uint3 local_triangles[8] = {
        uint3(4, 0, 2),
        uint3(4, 2, 1),
        uint3(4, 1, 3),
        uint3(4, 3, 0),
        uint3(5, 2, 0),
        uint3(5, 1, 2),
        uint3(5, 3, 1),
        uint3(5, 0, 3),
    };
    const uint first_triangle =
        thread_id * 8;
    for (uint triangle = 0;
         triangle != 8;
         ++triangle)
        triangles[
            first_triangle +
            triangle] =
                local_triangles[triangle] +
                first_vertex;
}
