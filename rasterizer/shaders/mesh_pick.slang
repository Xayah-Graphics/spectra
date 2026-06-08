struct CameraUniform {
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
    float3 cameraPosition;
    float _pad0;
    float3 lightDirection;
    float _pad1;
    float3 lightColor;
    float lightIntensity;
    float3 cameraRight;
    float _pad2;
    float3 cameraUp;
    float _pad3;
    float4 viewport;
};

struct SelectionPushConstants {
    row_major float4x4 model;
    float4 color;
    uint objectId;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::push_constant]] ConstantBuffer<SelectionPushConstants> selection;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
};

[shader("vertex")]
float4 vertex_main(VertexInput input) : SV_Position {
    float4 world = mul(float4(input.position, 1.0), selection.model);
    return mul(world, camera.viewProjection);
}

[shader("fragment")]
uint fragment_main() : SV_Target {
    return selection.objectId;
}
