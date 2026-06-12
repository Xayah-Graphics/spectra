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

struct DrawPushConstants {
    row_major float4x4 model;
    float4 base_color;
    float4 emission;
    float4 material;
    uint4 flags;
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
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), draw.model).xyz);
    return output;
}

static const uint MaterialModelUnlitSurface = 1;
static const uint MaterialModelEmissiveSurface = 2;
static const uint MaterialAlphaModeMasked = 1;
static const float Pi = 3.14159265358979323846;

float distribution_ggx(float normalDotHalf, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = normalDotHalf * normalDotHalf * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(Pi * denominator * denominator, 0.0001);
}

float geometry_schlick_ggx(float normalDotVector, float roughness) {
    float adjusted = roughness + 1.0;
    float k = adjusted * adjusted * 0.125;
    return normalDotVector / max(normalDotVector * (1.0 - k) + k, 0.0001);
}

float geometry_smith(float normalDotView, float normalDotLight, float roughness) {
    return geometry_schlick_ggx(normalDotView, roughness) * geometry_schlick_ggx(normalDotLight, roughness);
}

float3 fresnel_schlick(float cosine, float3 f0) {
    return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(saturate(1.0 - cosine), 5.0);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    if (draw.flags.y == MaterialAlphaModeMasked && draw.base_color.a < draw.material.z) discard;
    uint materialModel = draw.flags.x;
    if (materialModel == MaterialModelUnlitSurface) return float4(draw.base_color.rgb + draw.emission.rgb, draw.base_color.a);
    if (materialModel == MaterialModelEmissiveSurface) return float4(draw.base_color.rgb + draw.emission.rgb, draw.base_color.a);

    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(-camera.lightDirection);
    float3 viewDirection = normalize(camera.cameraPosition - input.worldPosition);
    float3 halfDirection = normalize(viewDirection + lightDirection);
    float roughness = max(saturate(draw.material.x), 0.045);
    float metallic = saturate(draw.material.y);
    float normalDotLight = max(dot(normal, lightDirection), 0.0);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float normalDotHalf = max(dot(normal, halfDirection), 0.0);
    float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), draw.base_color.rgb, metallic);
    float normalDistribution = distribution_ggx(normalDotHalf, roughness);
    float geometry = geometry_smith(normalDotView, normalDotLight, roughness);
    float3 fresnel = fresnel_schlick(halfDotView, f0);
    float3 numerator = normalDistribution * geometry * fresnel;
    float denominator = max(4.0 * normalDotView * normalDotLight, 0.0001);
    float3 specular = numerator / denominator;
    float3 diffuseWeight = (float3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic);
    float3 radiance = camera.lightColor * camera.lightIntensity;
    float3 ambient = draw.base_color.rgb * 0.035;
    float3 color = ambient + (diffuseWeight * draw.base_color.rgb / Pi + specular) * radiance * normalDotLight + draw.emission.rgb;
    return float4(color, draw.base_color.a);
}
