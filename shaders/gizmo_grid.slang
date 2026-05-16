static const uint GRID_FLAG_SHOW_GRID = 1u << 0u;
static const uint GRID_FLAG_AXIS_X = 1u << 1u;
static const uint GRID_FLAG_AXIS_Y = 1u << 2u;
static const uint GRID_FLAG_AXIS_Z = 1u << 3u;

struct GridPushConstants {
    uint grid_flag;
};

struct GridParameters {
    float4x4 view_projection;
    float4 camera_position_far_clip;
    float4 viewport_size_perspective;
    float4 steps;
    float4 offset_clip_rect;
    float4 level_num_lines;
    float4 minor_color;
    float4 major_color;
    float4 axis_color_x;
    float4 axis_color_y;
    float4 axis_color_z;
};

[[vk::push_constant]]
ConstantBuffer<GridPushConstants> push_constants;

[[vk::binding(0, 0)]]
StructuredBuffer<GridParameters> parameters_buffer;

static const float NaN = asfloat(0x7FC00000u);
static const float3 NaN3 = float3(NaN, NaN, NaN);
static const float4 NaN4 = float4(NaN, NaN, NaN, 1.0);

struct GridVertexOutput {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float2 coordinate : TEXCOORD1;
    nointerpolation float alpha : TEXCOORD2;
    nointerpolation float emphasis : TEXCOORD3;
    nointerpolation float axis_kind : TEXCOORD4;
    nointerpolation float3 axis_color : TEXCOORD5;
    nointerpolation float grid_iteration : TEXCOORD6;
};

float3 axis_color(uint axis, GridParameters parameters) {
    if (axis == 0u) return parameters.axis_color_x.xyz;
    if (axis == 1u) return parameters.axis_color_y.xyz;
    return parameters.axis_color_z.xyz;
}

uint axis_flag(uint axis) {
    if (axis == 0u) return GRID_FLAG_AXIS_X;
    if (axis == 1u) return GRID_FLAG_AXIS_Y;
    return GRID_FLAG_AXIS_Z;
}

[shader("vertex")]
GridVertexOutput vertex_main(uint vertex_id : SV_VertexID) {
    const GridParameters parameters = parameters_buffer[0];
    const uint num_lines = max(1u, uint(parameters.level_num_lines.y));
    const float level_fraction = frac(parameters.level_num_lines.x);
    const int level_base = int(floor(parameters.level_num_lines.x));

    GridVertexOutput output;
    output.position = NaN4;
    output.world_position = float3(0.0, 0.0, 0.0);
    output.coordinate = float2(0.0, 0.0);
    output.alpha = 1.0;
    output.emphasis = 0.0;
    output.axis_kind = 0.0;
    output.axis_color = float3(0.0, 0.0, 0.0);
    output.grid_iteration = 0.0;

    if ((push_constants.grid_flag & GRID_FLAG_SHOW_GRID) != 0u) {
        const uint line_index = vertex_id / 2u;
        const uint side = vertex_id % 2u;
        const uint block = line_index / num_lines;
        const uint line_in_block = line_index % num_lines;
        const uint level = block / 2u;
        const uint axis = block % 2u;

        const int actual_level = clamp(level_base + 1 - int(level), 0, 3);
        const float step_size = parameters.steps[actual_level];
        const float2 grid_offset = parameters.offset_clip_rect.xy;
        const float2 snap_offset = round(grid_offset / step_size) * step_size;
        const float half_lines = float(num_lines - 1u) * 0.5;
        const float line_position = float(line_in_block) - half_lines;
        const float level_clip = step_size * half_lines;

        const uint fixed_index = 2u * (1u - axis);
        const uint varying_index = 2u * axis;
        const float fixed_position = snap_offset[1u - axis] + step_size * line_position;
        const float varying_position = snap_offset[axis] + (side == 0u ? -level_clip : level_clip);

        float3 world_position = float3(0.0, 0.0, 0.0);
        world_position[fixed_index] = fixed_position;
        world_position[varying_index] = varying_position;

        if (level >= 1u) {
            const float next_step = parameters.steps[min(uint(actual_level + 1), 3u)];
            const float next_position = abs(fixed_position) / next_step;
            if (abs(next_position - round(next_position)) < 0.0001) world_position = NaN3;
        }

        const float axis_epsilon = step_size * 0.01;
        const bool on_axis = abs(fixed_position) < axis_epsilon;
        if (on_axis) world_position = NaN3;

        output.world_position = world_position;
        output.coordinate = (world_position.xz - snap_offset) / max(level_clip, 0.000001);
        output.alpha = saturate(3.0 - float(level) - level_fraction);
        output.emphasis = saturate(2.0 - float(level) - level_fraction);
        output.grid_iteration = float(level);
        output.position = on_axis ? NaN4 : mul(parameters.view_projection, float4(world_position, 1.0));
        return output;
    }

    const uint side = vertex_id & 1u;
    const uint axis = vertex_id >> 1u;
    if ((push_constants.grid_flag & axis_flag(axis)) == 0u) return output;

    const float extension = axis == 2u ? parameters.offset_clip_rect.w : parameters.offset_clip_rect.z;
    const float signed_extension = side == 0u ? -extension : extension;
    float3 world_position = float3(0.0, 0.0, 0.0);
    world_position[axis] = signed_extension;

    const float normalized_position = side == 0u ? -1.0 : 1.0;
    output.world_position = world_position;
    output.coordinate = axis == 2u ? float2(0.0, normalized_position) : float2(normalized_position, 0.0);
    output.axis_kind = float(axis + 1u);
    output.axis_color = axis_color(axis, parameters);
    output.position = mul(parameters.view_projection, float4(world_position, 1.0));
    return output;
}

[shader("fragment")]
float4 fragment_main(GridVertexOutput input) : SV_Target0 {
    const GridParameters parameters = parameters_buffer[0];
    float3 color = input.axis_kind < 0.5 ? lerp(parameters.minor_color.xyz, parameters.major_color.xyz, input.emphasis) : input.axis_color;
    float alpha = input.alpha;

    if (input.axis_kind < 0.5) {
        color *= lerp(0.75, 1.0, input.emphasis);
        alpha *= 1.0 - min(1.0, length(input.coordinate));

        if (abs(input.world_position.y) < 0.0001) {
            const float3 view_direction = normalize(parameters.camera_position_far_clip.xyz - input.world_position);
            alpha *= 1.0 - pow(1.0 - abs(view_direction.y), 3.0);
        }

        if (parameters.viewport_size_perspective.z != 0.0) {
            const float distance_to_camera = length(parameters.camera_position_far_clip.xyz - input.world_position);
            const float half_far = parameters.camera_position_far_clip.w * 0.5;
            alpha *= 1.0 - smoothstep(0.0, half_far, distance_to_camera - half_far);

            static const float additive_alpha[3] = {0.4, 0.3, 0.2};
            alpha *= additive_alpha[clamp(int(input.grid_iteration), 0, 2)];
        }
    }

    return float4(color, alpha);
}
