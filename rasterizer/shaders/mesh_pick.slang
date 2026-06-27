#include "camera_uniform.slangh"

struct SelectionPushConstants {
    row_major float4x4 model;
    float4 color;
    float4 base_color;
    float4 material;
    uint4 flags;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::push_constant]] ConstantBuffer<SelectionPushConstants> selection;

static const uint PreviewAlphaModeMaskedValue = 1;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
};

[shader("vertex")]
float4 vertex_main(VertexInput input) : SV_Position {
    float4 world = mul(float4(input.position, 1.0), selection.model);
    return mul(world, camera.worldToClip);
}

[shader("fragment")]
uint fragment_main() : SV_Target {
    if (selection.flags.y == PreviewAlphaModeMaskedValue && selection.base_color.a < selection.material.z) discard;
    return selection.flags.x;
}
