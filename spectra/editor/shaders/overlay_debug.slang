module overlay_debug;

struct DebugVertex {
    float4 position;
    float4 color;
}
struct DebugPushData {
    DescriptorHandle<StructuredBuffer<DebugVertex>> vertices;
    uint2 reserved;
    float4 view_projection_row_0;
    float4 view_projection_row_1;
    float4 view_projection_row_2;
    float4 view_projection_row_3;
}

struct DebugVertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR;
}

[vk::push_constant]
uniform DebugPushData push_data;

[shader("vertex")]
DebugVertexOutput overlay_debug_vertex(
    uint vertex_id : SV_VertexID) {
    StructuredBuffer<DebugVertex> vertices =
        push_data.vertices;
    const DebugVertex vertex =
        vertices[vertex_id];
    DebugVertexOutput output;
    output.position = float4(
        dot(
            push_data.view_projection_row_0,
            vertex.position),
        dot(
            push_data.view_projection_row_1,
            vertex.position),
        dot(
            push_data.view_projection_row_2,
            vertex.position),
        dot(
            push_data.view_projection_row_3,
            vertex.position));
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 overlay_debug_fragment(
    DebugVertexOutput input) : SV_Target {
    return input.color;
}
