struct CameraPushConstants {
    float4x4 view_projection;
};

[[vk::push_constant]]
ConstantBuffer<CameraPushConstants> camera;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

struct ViewportVertex {
    float3 position;
    float3 color;
};

static const uint GRID_RADIUS = 20;
static const uint GRID_WIDTH = GRID_RADIUS * 2 + 1;
static const uint GRID_VERTEX_COUNT = GRID_WIDTH * 4;
static const float AXIS_LENGTH = 20.0;

ViewportVertex make_vertex(float3 position, float3 color) {
    ViewportVertex vertex;
    vertex.position = position;
    vertex.color = color;
    return vertex;
}

ViewportVertex grid_vertex(uint vertex_id) {
    const uint line = vertex_id / 4;
    const uint endpoint = vertex_id % 4;
    const float radius = float(GRID_RADIUS);
    const float coordinate = float(int(line) - int(GRID_RADIUS));
    const float3 color = abs(coordinate) < 0.001 ? float3(0.42, 0.45, 0.52) : float3(0.28, 0.31, 0.36);
    if (endpoint == 0) return make_vertex(float3(-radius, 0.0, coordinate), color);
    if (endpoint == 1) return make_vertex(float3(radius, 0.0, coordinate), color);
    if (endpoint == 2) return make_vertex(float3(coordinate, 0.0, -radius), color);
    return make_vertex(float3(coordinate, 0.0, radius), color);
}

ViewportVertex axis_vertex(uint vertex_id) {
    switch (vertex_id) {
    case 0: return make_vertex(float3(-AXIS_LENGTH, 0.0, 0.0), float3(0.95, 0.22, 0.28));
    case 1: return make_vertex(float3(AXIS_LENGTH, 0.0, 0.0), float3(0.95, 0.22, 0.28));
    case 2: return make_vertex(float3(0.0, -AXIS_LENGTH, 0.0), float3(0.28, 0.86, 0.40));
    case 3: return make_vertex(float3(0.0, AXIS_LENGTH, 0.0), float3(0.28, 0.86, 0.40));
    case 4: return make_vertex(float3(0.0, 0.0, -AXIS_LENGTH), float3(0.25, 0.55, 1.0));
    default: return make_vertex(float3(0.0, 0.0, AXIS_LENGTH), float3(0.25, 0.55, 1.0));
    }
}

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id : SV_VertexID) {
    const ViewportVertex vertex = vertex_id < GRID_VERTEX_COUNT ? grid_vertex(vertex_id) : axis_vertex(vertex_id - GRID_VERTEX_COUNT);
    VertexOutput output;
    output.position = mul(camera.view_projection, float4(vertex.position, 1.0));
    output.color = vertex.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
