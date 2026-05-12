struct CameraPushConstants {
    float4x4 view_projection;
};

[[vk::push_constant]]
ConstantBuffer<CameraPushConstants> camera;

struct VertexOutput {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

static const int grid_radius = 20;
static const int grid_width = grid_radius * 2 + 1;
static const int grid_vertex_count = grid_width * 4;
static const float axis_length = 20.0;

[shader("vertex")]
VertexOutput vertex_main(uint vertex_index : SV_VertexID) {
    float3 position = float3(0.0, 0.0, 0.0);
    float3 color = float3(0.28, 0.31, 0.36);

    if (vertex_index < grid_vertex_count) {
        const int line = int(vertex_index) / 2;
        const int endpoint = int(vertex_index) % 2;
        const int local_line = line % grid_width;
        const float coordinate = float(local_line - grid_radius);
        const float extent = endpoint == 0 ? -float(grid_radius) : float(grid_radius);

        if (line < grid_width)
            position = float3(extent, 0.0, coordinate);
        else
            position = float3(coordinate, 0.0, extent);

        if (abs(coordinate) < 0.001) color = float3(0.42, 0.45, 0.52);
    } else {
        const int axis_vertex = int(vertex_index) - grid_vertex_count;
        const int axis = axis_vertex / 2;
        const int endpoint = axis_vertex % 2;
        const float coordinate = endpoint == 0 ? -axis_length : axis_length;

        if (axis == 0) {
            position = float3(coordinate, 0.0, 0.0);
            color = float3(0.95, 0.22, 0.28);
        } else if (axis == 1) {
            position = float3(0.0, coordinate, 0.0);
            color = float3(0.28, 0.86, 0.40);
        } else {
            position = float3(0.0, 0.0, coordinate);
            color = float3(0.25, 0.55, 1.0);
        }
    }

    VertexOutput output;
    output.position = mul(camera.view_projection, float4(position, 1.0));
    output.color = color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return float4(input.color, 1.0);
}
