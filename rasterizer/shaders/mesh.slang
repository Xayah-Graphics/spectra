struct CameraUniform {
    row_major float4x4 viewProjection;
    float3 cameraPosition;
    float _pad0;
    float3 lightDirection;
    float _pad1;
    float3 lightColor;
    float lightIntensity;
};

struct DrawPushConstants {
    row_major float4x4 model;
    float4 baseColor;
    float4 emission;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::push_constant]] ConstantBuffer<DrawPushConstants> draw;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL;
    [[vk::location(1)]] float3 worldPosition : POSITION;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    float4 world = mul(float4(input.position, 1.0), draw.model);
    output.position = mul(world, camera.viewProjection);
    output.worldPosition = world.xyz;
    output.worldNormal = normalize(input.normal);
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(-camera.lightDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float3 color = draw.baseColor.rgb * (0.12 + diffuse * camera.lightIntensity) * camera.lightColor + draw.emission.rgb;
    return float4(color, draw.baseColor.a);
}
