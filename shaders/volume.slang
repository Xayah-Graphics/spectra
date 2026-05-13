struct GridParameters {
    float4x4 view_projection;
    float4 camera_step;
    float4 origin_opacity;
    float4 spacing_value_min;
    uint4 resolution_kind;
    uint4 mode_options;
    float4 slice_value_max;
};

static const uint GRID_CENTERED_SCALAR = 0;
static const uint GRID_STAGGERED_VECTOR = 1;
static const uint MODE_DIRECT = 0;
static const uint MODE_SLICE = 1;
static const uint AXIS_X = 0;
static const uint AXIS_Y = 1;
static const uint COLORMAP_GRAYSCALE = 0;
static const uint COLORMAP_VIRIDIS = 1;
static const uint COLORMAP_TURBO = 2;

[[vk::binding(0, 0)]]
StructuredBuffer<float> x_buffer;

[[vk::binding(1, 0)]]
StructuredBuffer<float> y_buffer;

[[vk::binding(2, 0)]]
StructuredBuffer<float> z_buffer;

[[vk::binding(3, 0)]]
StructuredBuffer<GridParameters> parameters_buffer;

struct VertexOutput {
    float4 position : SV_Position;
    float3 world_position : POSITION0;
};

struct VertexInput {
    float3 local_position : POSITION0;
};

float3 slice_corner(float2 uv, GridParameters parameters) {
    const float slice = parameters.slice_value_max.x;
    if (parameters.mode_options.y == AXIS_X) return float3(slice, uv.x, uv.y);
    if (parameters.mode_options.y == AXIS_Y) return float3(uv.x, slice, uv.y);
    return float3(uv.x, uv.y, slice);
}

float scalar_at(uint x, uint y, uint z, GridParameters parameters) {
    const uint width = parameters.resolution_kind.x;
    const uint height = parameters.resolution_kind.y;
    return x_buffer[x + y * width + z * width * height];
}

float sample_centered_scalar(float3 position, GridParameters parameters) {
    const float3 origin = parameters.origin_opacity.xyz;
    const float3 spacing = parameters.spacing_value_min.xyz;
    const float3 grid_position = (position - origin) / spacing - 0.5;
    const float3 max_cell = float3(parameters.resolution_kind.x - 1, parameters.resolution_kind.y - 1, parameters.resolution_kind.z - 1);

    if (grid_position.x < 0.0 || grid_position.y < 0.0 || grid_position.z < 0.0) return 0.0;
    if (grid_position.x > max_cell.x || grid_position.y > max_cell.y || grid_position.z > max_cell.z) return 0.0;

    const uint x0 = min(uint(floor(grid_position.x)), parameters.resolution_kind.x - 2);
    const uint y0 = min(uint(floor(grid_position.y)), parameters.resolution_kind.y - 2);
    const uint z0 = min(uint(floor(grid_position.z)), parameters.resolution_kind.z - 2);
    const uint x1 = x0 + 1;
    const uint y1 = y0 + 1;
    const uint z1 = z0 + 1;
    const float3 fraction = grid_position - float3(x0, y0, z0);

    const float c000 = scalar_at(x0, y0, z0, parameters);
    const float c100 = scalar_at(x1, y0, z0, parameters);
    const float c010 = scalar_at(x0, y1, z0, parameters);
    const float c110 = scalar_at(x1, y1, z0, parameters);
    const float c001 = scalar_at(x0, y0, z1, parameters);
    const float c101 = scalar_at(x1, y0, z1, parameters);
    const float c011 = scalar_at(x0, y1, z1, parameters);
    const float c111 = scalar_at(x1, y1, z1, parameters);

    const float c00 = c000 + (c100 - c000) * fraction.x;
    const float c10 = c010 + (c110 - c010) * fraction.x;
    const float c01 = c001 + (c101 - c001) * fraction.x;
    const float c11 = c011 + (c111 - c011) * fraction.x;
    const float c0 = c00 + (c10 - c00) * fraction.y;
    const float c1 = c01 + (c11 - c01) * fraction.y;
    return c0 + (c1 - c0) * fraction.z;
}

float x_face_at(uint x, uint y, uint z, GridParameters parameters) {
    const uint width = parameters.resolution_kind.x + 1;
    const uint height = parameters.resolution_kind.y;
    return x_buffer[x + y * width + z * width * height];
}

float y_face_at(uint x, uint y, uint z, GridParameters parameters) {
    const uint width = parameters.resolution_kind.x;
    const uint height = parameters.resolution_kind.y + 1;
    return y_buffer[x + y * width + z * width * height];
}

float z_face_at(uint x, uint y, uint z, GridParameters parameters) {
    const uint width = parameters.resolution_kind.x;
    const uint height = parameters.resolution_kind.y;
    return z_buffer[x + y * width + z * width * height];
}

float sample_x_face(float3 grid_position, GridParameters parameters) {
    const float3 max_cell = float3(parameters.resolution_kind.x, parameters.resolution_kind.y - 1, parameters.resolution_kind.z - 1);
    if (grid_position.x < 0.0 || grid_position.y < 0.0 || grid_position.z < 0.0) return 0.0;
    if (grid_position.x > max_cell.x || grid_position.y > max_cell.y || grid_position.z > max_cell.z) return 0.0;

    const uint x0 = min(uint(floor(grid_position.x)), parameters.resolution_kind.x - 1);
    const uint y0 = min(uint(floor(grid_position.y)), parameters.resolution_kind.y - 2);
    const uint z0 = min(uint(floor(grid_position.z)), parameters.resolution_kind.z - 2);
    const uint x1 = x0 + 1;
    const uint y1 = y0 + 1;
    const uint z1 = z0 + 1;
    const float3 fraction = grid_position - float3(x0, y0, z0);

    const float c000 = x_face_at(x0, y0, z0, parameters);
    const float c100 = x_face_at(x1, y0, z0, parameters);
    const float c010 = x_face_at(x0, y1, z0, parameters);
    const float c110 = x_face_at(x1, y1, z0, parameters);
    const float c001 = x_face_at(x0, y0, z1, parameters);
    const float c101 = x_face_at(x1, y0, z1, parameters);
    const float c011 = x_face_at(x0, y1, z1, parameters);
    const float c111 = x_face_at(x1, y1, z1, parameters);

    const float c00 = c000 + (c100 - c000) * fraction.x;
    const float c10 = c010 + (c110 - c010) * fraction.x;
    const float c01 = c001 + (c101 - c001) * fraction.x;
    const float c11 = c011 + (c111 - c011) * fraction.x;
    const float c0 = c00 + (c10 - c00) * fraction.y;
    const float c1 = c01 + (c11 - c01) * fraction.y;
    return c0 + (c1 - c0) * fraction.z;
}

float sample_y_face(float3 grid_position, GridParameters parameters) {
    const float3 max_cell = float3(parameters.resolution_kind.x - 1, parameters.resolution_kind.y, parameters.resolution_kind.z - 1);
    if (grid_position.x < 0.0 || grid_position.y < 0.0 || grid_position.z < 0.0) return 0.0;
    if (grid_position.x > max_cell.x || grid_position.y > max_cell.y || grid_position.z > max_cell.z) return 0.0;

    const uint x0 = min(uint(floor(grid_position.x)), parameters.resolution_kind.x - 2);
    const uint y0 = min(uint(floor(grid_position.y)), parameters.resolution_kind.y - 1);
    const uint z0 = min(uint(floor(grid_position.z)), parameters.resolution_kind.z - 2);
    const uint x1 = x0 + 1;
    const uint y1 = y0 + 1;
    const uint z1 = z0 + 1;
    const float3 fraction = grid_position - float3(x0, y0, z0);

    const float c000 = y_face_at(x0, y0, z0, parameters);
    const float c100 = y_face_at(x1, y0, z0, parameters);
    const float c010 = y_face_at(x0, y1, z0, parameters);
    const float c110 = y_face_at(x1, y1, z0, parameters);
    const float c001 = y_face_at(x0, y0, z1, parameters);
    const float c101 = y_face_at(x1, y0, z1, parameters);
    const float c011 = y_face_at(x0, y1, z1, parameters);
    const float c111 = y_face_at(x1, y1, z1, parameters);

    const float c00 = c000 + (c100 - c000) * fraction.x;
    const float c10 = c010 + (c110 - c010) * fraction.x;
    const float c01 = c001 + (c101 - c001) * fraction.x;
    const float c11 = c011 + (c111 - c011) * fraction.x;
    const float c0 = c00 + (c10 - c00) * fraction.y;
    const float c1 = c01 + (c11 - c01) * fraction.y;
    return c0 + (c1 - c0) * fraction.z;
}

float sample_z_face(float3 grid_position, GridParameters parameters) {
    const float3 max_cell = float3(parameters.resolution_kind.x - 1, parameters.resolution_kind.y - 1, parameters.resolution_kind.z);
    if (grid_position.x < 0.0 || grid_position.y < 0.0 || grid_position.z < 0.0) return 0.0;
    if (grid_position.x > max_cell.x || grid_position.y > max_cell.y || grid_position.z > max_cell.z) return 0.0;

    const uint x0 = min(uint(floor(grid_position.x)), parameters.resolution_kind.x - 2);
    const uint y0 = min(uint(floor(grid_position.y)), parameters.resolution_kind.y - 2);
    const uint z0 = min(uint(floor(grid_position.z)), parameters.resolution_kind.z - 1);
    const uint x1 = x0 + 1;
    const uint y1 = y0 + 1;
    const uint z1 = z0 + 1;
    const float3 fraction = grid_position - float3(x0, y0, z0);

    const float c000 = z_face_at(x0, y0, z0, parameters);
    const float c100 = z_face_at(x1, y0, z0, parameters);
    const float c010 = z_face_at(x0, y1, z0, parameters);
    const float c110 = z_face_at(x1, y1, z0, parameters);
    const float c001 = z_face_at(x0, y0, z1, parameters);
    const float c101 = z_face_at(x1, y0, z1, parameters);
    const float c011 = z_face_at(x0, y1, z1, parameters);
    const float c111 = z_face_at(x1, y1, z1, parameters);

    const float c00 = c000 + (c100 - c000) * fraction.x;
    const float c10 = c010 + (c110 - c010) * fraction.x;
    const float c01 = c001 + (c101 - c001) * fraction.x;
    const float c11 = c011 + (c111 - c011) * fraction.x;
    const float c0 = c00 + (c10 - c00) * fraction.y;
    const float c1 = c01 + (c11 - c01) * fraction.y;
    return c0 + (c1 - c0) * fraction.z;
}

float3 sample_staggered_vector(float3 position, GridParameters parameters) {
    const float3 grid_position = (position - parameters.origin_opacity.xyz) / parameters.spacing_value_min.xyz;
    const float x = sample_x_face(float3(grid_position.x, grid_position.y - 0.5, grid_position.z - 0.5), parameters);
    const float y = sample_y_face(float3(grid_position.x - 0.5, grid_position.y, grid_position.z - 0.5), parameters);
    const float z = sample_z_face(float3(grid_position.x - 0.5, grid_position.y - 0.5, grid_position.z), parameters);
    return float3(x, y, z);
}

float sample_grid_value(float3 position, GridParameters parameters) {
    if (parameters.resolution_kind.w == GRID_CENTERED_SCALAR) return sample_centered_scalar(position, parameters);
    if (parameters.resolution_kind.w == GRID_STAGGERED_VECTOR) return length(sample_staggered_vector(position, parameters));
    return 0.0;
}

float normalized_value(float value, GridParameters parameters) {
    return clamp((value - parameters.spacing_value_min.w) / (parameters.slice_value_max.y - parameters.spacing_value_min.w), 0.0, 1.0);
}

float3 ramp5(float value, float3 c0, float3 c1, float3 c2, float3 c3, float3 c4) {
    const float scaled = clamp(value, 0.0, 1.0) * 4.0;
    if (scaled < 1.0) return c0 + (c1 - c0) * scaled;
    if (scaled < 2.0) return c1 + (c2 - c1) * (scaled - 1.0);
    if (scaled < 3.0) return c2 + (c3 - c2) * (scaled - 2.0);
    return c3 + (c4 - c3) * (scaled - 3.0);
}

float3 color_map(float value, uint map_id) {
    const float v = clamp(value, 0.0, 1.0);
    if (map_id == COLORMAP_GRAYSCALE) return float3(v, v, v);
    if (map_id == COLORMAP_VIRIDIS) return ramp5(v, float3(0.267, 0.005, 0.329), float3(0.231, 0.322, 0.545), float3(0.128, 0.567, 0.551), float3(0.369, 0.788, 0.383), float3(0.993, 0.906, 0.144));
    if (map_id == COLORMAP_TURBO) return ramp5(v, float3(0.190, 0.072, 0.232), float3(0.251, 0.430, 0.847), float3(0.111, 0.789, 0.618), float3(0.985, 0.782, 0.171), float3(0.761, 0.044, 0.143));
    return ramp5(v, float3(0.030, 0.030, 0.035), float3(0.520, 0.035, 0.060), float3(0.930, 0.260, 0.080), float3(1.000, 0.740, 0.120), float3(1.000, 1.000, 0.920));
}

bool intersect_box(float3 ray_origin, float3 ray_direction, float3 box_min, float3 box_max, out float t_near, out float t_far) {
    const float3 inv_direction = 1.0 / ray_direction;
    const float3 t0 = (box_min - ray_origin) * inv_direction;
    const float3 t1 = (box_max - ray_origin) * inv_direction;
    const float3 t_min = min(t0, t1);
    const float3 t_max = max(t0, t1);
    t_near = max(max(t_min.x, t_min.y), t_min.z);
    t_far = min(min(t_max.x, t_max.y), t_max.z);
    return t_far >= max(t_near, 0.0);
}

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    const GridParameters parameters = parameters_buffer[0];
    const float3 extent = float3(parameters.resolution_kind.x, parameters.resolution_kind.y, parameters.resolution_kind.z) * parameters.spacing_value_min.xyz;
    float3 local_position = float3(0.0, 0.0, 0.0);
    if (parameters.mode_options.x == MODE_DIRECT)
        local_position = input.local_position;
    else
        local_position = slice_corner(input.local_position.xy, parameters);

    const float3 world_position = parameters.origin_opacity.xyz + local_position * extent;
    VertexOutput output;
    output.position = mul(parameters.view_projection, float4(world_position, 1.0));
    output.world_position = world_position;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    const GridParameters parameters = parameters_buffer[0];

    if (parameters.mode_options.x == MODE_SLICE) {
        const float value = normalized_value(sample_grid_value(input.world_position, parameters), parameters);
        return float4(color_map(value, parameters.mode_options.z), value * parameters.origin_opacity.w);
    }

    const float3 box_min = parameters.origin_opacity.xyz;
    const float3 box_max = parameters.origin_opacity.xyz + float3(parameters.resolution_kind.x, parameters.resolution_kind.y, parameters.resolution_kind.z) * parameters.spacing_value_min.xyz;
    const float3 camera_position = parameters.camera_step.xyz;
    const float3 ray_direction = normalize(input.world_position - camera_position);

    float t_near = 0.0;
    float t_far = 0.0;
    if (!intersect_box(camera_position, ray_direction, box_min, box_max, t_near, t_far)) return float4(0.0, 0.0, 0.0, 0.0);

    float t = max(t_near, 0.0);
    float accumulated_alpha = 0.0;
    float3 accumulated_color = float3(0.0, 0.0, 0.0);
    const float step_size = parameters.camera_step.w;

    for (uint step = 0; step < 384 && t <= t_far && accumulated_alpha < 0.98; ++step) {
        const float3 sample_position = camera_position + ray_direction * t;
        const float value = normalized_value(sample_grid_value(sample_position, parameters), parameters);
        const float alpha = (1.0 - exp(-value * step_size * 4.0)) * parameters.origin_opacity.w;
        accumulated_color += (1.0 - accumulated_alpha) * alpha * color_map(value, parameters.mode_options.z);
        accumulated_alpha += (1.0 - accumulated_alpha) * alpha;
        t += step_size;
    }

    return float4(accumulated_color, accumulated_alpha);
}
