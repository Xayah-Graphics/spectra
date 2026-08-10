module raster_spheres;

import raster_scene;

struct RasterVertex {
    float4 position : SV_Position;
    float3 world_position : POSITION;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float2 texture_coordinates : TEXCOORD;
    float3 emission : COLOR0;
    float3 color_modifier : COLOR1;
    nointerpolation uint material : MATERIAL;
    nointerpolation uint area_light : AREA_LIGHT;
    nointerpolation uint sphere : SPHERE;
    nointerpolation float3 sphere_center : POSITION1;
    nointerpolation float sphere_radius : RADIUS;
    noperspective float3 wire_coordinates : TEXCOORD1;
}

struct RasterSpherePushData {
    uint64_t acceleration_structure_address;
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<float3>> colors;
    DescriptorHandle<StructuredBuffer<float>> temperatures;
    DescriptorHandle<StructuredBuffer<uint>> materials;
    DescriptorHandle<StructuredBuffer<RasterSceneBindings>> bindings;
    uint scene_primitive_index;
    uint element_count;
    uint2 attributes;
    DescriptorHandle<StructuredBuffer<uint>> sampling_tables;
    uint4 sampler_metadata;
    uint4 sampler_parameters;
    uint2 extent;
    uint sample_index;
    uint reserved;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
    float4 camera_position;
    float4 camera_right;
    float4 camera_up;
    float4 camera_forward;
}

[vk::push_constant]
uniform RasterSpherePushData push_data;

float3 blackbody(float temperature) {
    temperature =
        clamp(
            temperature,
            1000.0,
            40000.0) /
        100.0;
    float3 result;
    if (temperature <= 66.0) {
        result.x = 1.0;
        result.y =
            clamp(
                0.3900815788 *
                        log(temperature) -
                    0.6318414438,
                0.0,
                1.0);
    } else {
        result.x =
            clamp(
                1.2929361861 *
                    pow(
                        temperature - 60.0,
                        -0.1332047592),
                0.0,
                1.0);
        result.y =
            clamp(
                1.129890861 *
                    pow(
                        temperature - 60.0,
                        -0.0755148492),
                0.0,
                1.0);
    }
    if (temperature >= 66.0)
        result.z = 1.0;
    else if (temperature <= 19.0)
        result.z = 0.0;
    else
        result.z =
            clamp(
                0.5432067891 *
                        log(
                            temperature -
                            10.0) -
                    1.1962540891,
                0.0,
                1.0);
    return result;
}

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
    StructuredBuffer<float3> colors =
        push_data.colors;
    StructuredBuffer<float> temperatures =
        push_data.temperatures;
    StructuredBuffer<RasterSceneBindings> binding_table =
        push_data.bindings;
    const RasterSceneBindings bindings =
        binding_table[0];
    StructuredBuffer<RasterPrimitive> primitives =
        bindings.primitives;
    StructuredBuffer<RasterTransform> transforms =
        bindings.transforms;
    StructuredBuffer<uint> sphere_materials =
        push_data.materials;
    const RasterPrimitive primitive =
        primitives[
            push_data.scene_primitive_index];
    const RasterTransform transform =
        transforms[
            primitive.transform_index];
    const uint sphere =
        first_sphere +
        thread_id;
    const uint material =
        (push_data.attributes.x & 8) == 0
            ? primitive.material_index
            : sphere_materials[sphere];
    const float3 center =
        positions[sphere];
    const float radius =
        radii[sphere];
    float3 color_modifier =
        float3(1.0);
    if (
        (
            push_data.attributes.x &
            2
        ) != 0)
        color_modifier =
            colors[sphere];
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
        output.emission =
            (
                push_data.attributes.x &
                4
            ) != 0
                ? blackbody(
                      temperatures[
                          sphere]) *
                      color_modifier
                : float3(0.0);
        output.color_modifier =
            color_modifier;
        output.material =
            material;
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
