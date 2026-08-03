module overlay_mesh;

struct ViewportPrimitive {
    uint transform_index;
    uint reserved_0;
    uint reserved_1;
    uint reserved_2;
    uint reserved_3;
    uint reserved_4;
    uint reserved_5;
    uint reserved_6;
}

struct ViewportTransform {
    float4 transform_row_0;
    float4 transform_row_1;
    float4 transform_row_2;
    float4 transform_row_3;
}

struct PackedFloat3 {
    float x;
    float y;
    float z;
}

struct MaskPushData {
    DescriptorHandle<StructuredBuffer<PackedFloat3>> positions;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<ViewportPrimitive>> primitives;
    DescriptorHandle<StructuredBuffer<ViewportTransform>> transforms;
    uint scene_primitive_index;
    uint element_count;
    uint draw_kind;
    uint reserved_0;
    uint2 reserved_1;
    float4 color;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
}

struct MaskVertex {
    float4 position : SV_Position;
    float4 color : COLOR0;
}

[vk::push_constant]
uniform MaskPushData push_data;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void overlay_mesh(
    uint3 group_id : SV_GroupID,
    uint thread_id : SV_GroupThreadID,
    out vertices MaskVertex vertices[192],
    out indices uint3 triangles[256]) {
    const uint first_element =
        group_id.x * 32;
    const uint emitted_elements =
        min(
            32,
            push_data.element_count -
                first_element);
    SetMeshOutputCounts(
        push_data.draw_kind == 0
            ? emitted_elements * 3
            : emitted_elements * 6,
        push_data.draw_kind == 0
            ? emitted_elements
            : emitted_elements * 8);
    if (thread_id >= emitted_elements)
        return;
    StructuredBuffer<PackedFloat3> positions = push_data.positions;
    StructuredBuffer<ViewportPrimitive> primitives = push_data.primitives;
    StructuredBuffer<ViewportTransform> transforms = push_data.transforms;
    const ViewportPrimitive primitive_data = primitives[push_data.scene_primitive_index];
    const ViewportTransform transform = transforms[primitive_data.transform_index];
    if (push_data.draw_kind == 0) {
        StructuredBuffer<uint> indices =
            push_data.indices;
        const uint triangle =
            first_element + thread_id;
        const uint first_vertex =
            thread_id * 3;
        for (uint vertex = 0;
             vertex < 3;
             ++vertex) {
            const PackedFloat3 packed =
                positions[
                    indices[
                        triangle * 3 +
                        vertex]];
            const float4 local =
                float4(
                    packed.x,
                    packed.y,
                    packed.z,
                    1.0);
            const float4 world = float4(
                dot(
                    transform.transform_row_0,
                    local),
                dot(
                    transform.transform_row_1,
                    local),
                dot(
                    transform.transform_row_2,
                    local),
                1.0);
            MaskVertex output;
            output.position = float4(
                dot(
                    push_data
                        .view_projection_row_0,
                    world),
                dot(
                    push_data
                        .view_projection_row_1,
                    world),
                dot(
                    push_data
                        .view_projection_row_2,
                    world),
                dot(
                    push_data
                        .view_projection_row_3,
                    world));
            output.color =
                push_data.color;
            vertices[
                first_vertex +
                vertex] = output;
        }
        triangles[thread_id] =
            uint3(
                first_vertex,
                first_vertex + 1,
                first_vertex + 2);
        return;
    }

    StructuredBuffer<float> radii =
        push_data.radii;
    const uint particle =
        first_element + thread_id;
    const PackedFloat3 packed_center =
        positions[particle];
    const float3 center =
        float3(
            packed_center.x,
            packed_center.y,
            packed_center.z);
    const float radius =
        radii[particle];
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
        const float3 local_position =
            center +
            radius *
                directions[vertex];
        const float4 local =
            float4(
                local_position,
                1.0);
        const float4 world = float4(
            dot(transform.transform_row_0, local),
            dot(transform.transform_row_1, local),
            dot(transform.transform_row_2, local),
            1.0);
        MaskVertex output;
        output.position = float4(
            dot(push_data.view_projection_row_0, world),
            dot(push_data.view_projection_row_1, world),
            dot(push_data.view_projection_row_2, world),
            dot(push_data.view_projection_row_3, world));
        output.color = push_data.color;
        vertices[first_vertex + vertex] = output;
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
