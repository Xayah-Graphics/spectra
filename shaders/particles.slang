struct Particle {
    float4 position_radius;
    float4 color;
};

struct ParticleParameters {
    float4x4 view_projection;
    float4 camera_right_radius_scale;
    float4 camera_up_unused;
};

[[vk::binding(0, 0)]]
StructuredBuffer<Particle> particles;

[[vk::binding(1, 0)]]
StructuredBuffer<ParticleParameters> parameters_buffer;

struct VertexOutput {
    float4 position : SV_Position;
    float2 local_position : TEXCOORD0;
    float3 color : COLOR0;
};

[shader("vertex")]
VertexOutput vertex_main(uint vertex_index : SV_VertexID) {
    const uint particle_index = vertex_index / 6;
    const uint corner_index = vertex_index - particle_index * 6;
    const float2 corners[6] = {
        float2(-1.0, -1.0),
        float2(1.0, -1.0),
        float2(1.0, 1.0),
        float2(-1.0, -1.0),
        float2(1.0, 1.0),
        float2(-1.0, 1.0),
    };

    const Particle particle = particles[particle_index];
    const ParticleParameters parameters = parameters_buffer[0];
    const float2 local_position = corners[corner_index];
    const float radius = particle.position_radius.w * parameters.camera_right_radius_scale.w;
    const float3 world_position = particle.position_radius.xyz + parameters.camera_right_radius_scale.xyz * local_position.x * radius + parameters.camera_up_unused.xyz * local_position.y * radius;

    VertexOutput output;
    output.position = mul(parameters.view_projection, float4(world_position, 1.0));
    output.local_position = local_position;
    output.color = saturate(particle.color.xyz);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    if (dot(input.local_position, input.local_position) > 1.0) discard;
    return float4(input.color, 1.0);
}
