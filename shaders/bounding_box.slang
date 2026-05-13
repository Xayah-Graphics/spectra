struct BoundingBoxParameters {
    float4x4 model_view_projection;
    float4 bounds_min;
    float4 bounds_max;
    float4 color;
};

[[vk::push_constant]]
BoundingBoxParameters parameters;

struct VertexOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float3 bounding_box_vertex(uint vertex_id) {
    switch (vertex_id) {
    case 0: return float3(0.0, 0.0, 0.0);
    case 1: return float3(1.0, 0.0, 0.0);
    case 2: return float3(1.0, 0.0, 0.0);
    case 3: return float3(1.0, 1.0, 0.0);
    case 4: return float3(1.0, 1.0, 0.0);
    case 5: return float3(0.0, 1.0, 0.0);
    case 6: return float3(0.0, 1.0, 0.0);
    case 7: return float3(0.0, 0.0, 0.0);
    case 8: return float3(0.0, 0.0, 1.0);
    case 9: return float3(1.0, 0.0, 1.0);
    case 10: return float3(1.0, 0.0, 1.0);
    case 11: return float3(1.0, 1.0, 1.0);
    case 12: return float3(1.0, 1.0, 1.0);
    case 13: return float3(0.0, 1.0, 1.0);
    case 14: return float3(0.0, 1.0, 1.0);
    case 15: return float3(0.0, 0.0, 1.0);
    case 16: return float3(0.0, 0.0, 0.0);
    case 17: return float3(0.0, 0.0, 1.0);
    case 18: return float3(1.0, 0.0, 0.0);
    case 19: return float3(1.0, 0.0, 1.0);
    case 20: return float3(0.0, 1.0, 0.0);
    case 21: return float3(0.0, 1.0, 1.0);
    case 22: return float3(1.0, 1.0, 0.0);
    default: return float3(1.0, 1.0, 1.0);
    }
}

[shader("vertex")]
VertexOutput vertex_main(uint vertex_id : SV_VertexID) {
    VertexOutput output;
    const float3 local_position = bounding_box_vertex(vertex_id);
    const float3 object_position = parameters.bounds_min.xyz + local_position * (parameters.bounds_max.xyz - parameters.bounds_min.xyz);
    output.position = mul(parameters.model_view_projection, float4(object_position, 1.0));
    output.color = parameters.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target0 {
    return input.color;
}
