#include "camera_uniform.slangh"

struct DrawPushConstants {
    row_major float4x4 model;
    float4 base_color;
    float4 emission;
    float4 material;
    uint4 flags;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::binding(0, 1)]] Texture2D<float4> baseColorTexture;
[[vk::binding(1, 1)]] Texture2D<float4> emissionTexture;
[[vk::binding(2, 1)]] Texture2D<float4> roughnessTexture;
[[vk::binding(3, 1)]] Texture2D<float4> normalTexture;
[[vk::binding(4, 1)]] SamplerState materialSampler;
[[vk::push_constant]] ConstantBuffer<DrawPushConstants> draw;

struct VertexInput {
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 uv : TEXCOORD;
};

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 worldNormal : NORMAL;
    [[vk::location(1)]] float3 worldPosition : POSITION;
    [[vk::location(2)]] float2 uv : TEXCOORD;
};

[shader("vertex")]
VertexOutput vertex_main(VertexInput input) {
    VertexOutput output;
    float4 world = mul(float4(input.position, 1.0), draw.model);
    output.position = mul(world, camera.worldToClip);
    output.worldPosition = world.xyz;
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), draw.model).xyz);
    output.uv = input.uv;
    return output;
}

static const uint PreviewSurfaceKindUnlitSurfaceValue = 1;
static const uint PreviewSurfaceKindEmissiveSurfaceValue = 2;
static const uint PreviewAlphaModeMaskedValue = 1;
static const uint BaseColorTextureFlag = 1;
static const uint EmissionTextureFlag = 2;
static const uint RoughnessTextureFlag = 4;
static const uint NormalTextureFlag = 8;
static const float PreviewLightKindDirectionalValue = 0.0;
static const float PreviewLightKindSpotValue = 2.0;
static const float PreviewLightKindAreaValue = 3.0;
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

float3 mapped_normal(VertexOutput input, float3 geometricNormal) {
    float3 positionDx = ddx(input.worldPosition);
    float3 positionDy = ddy(input.worldPosition);
    float2 textureUv = float2(input.uv.x, 1.0 - input.uv.y);
    float2 uvDx = ddx(textureUv);
    float2 uvDy = ddy(textureUv);
    float3 positionDyPerpendicular = cross(positionDy, geometricNormal);
    float3 positionDxPerpendicular = cross(geometricNormal, positionDx);
    float3 tangent = positionDyPerpendicular * uvDx.x + positionDxPerpendicular * uvDy.x;
    float3 bitangent = positionDyPerpendicular * uvDx.y + positionDxPerpendicular * uvDy.y;
    float scale = rsqrt(max(max(dot(tangent, tangent), dot(bitangent, bitangent)), 0.000001));
    float3 sampled = normalTexture.Sample(materialSampler, textureUv).xyz * 2.0 - 1.0;
    return normalize(tangent * scale * sampled.x + bitangent * scale * sampled.y + geometricNormal * sampled.z);
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    float2 textureUv = float2(input.uv.x, 1.0 - input.uv.y);
    float4 sampledBaseColor = baseColorTexture.Sample(materialSampler, textureUv);
    float4 baseColor = (draw.flags.z & BaseColorTextureFlag) != 0 ? float4(sampledBaseColor.rgb, sampledBaseColor.a * draw.base_color.a) : draw.base_color;
    float3 emission = ((draw.flags.z & EmissionTextureFlag) != 0 ? emissionTexture.Sample(materialSampler, textureUv).rgb : draw.emission.rgb) * draw.emission.a;
    if (draw.flags.y == PreviewAlphaModeMaskedValue && baseColor.a < draw.material.z) discard;
    uint previewSurfaceKind = draw.flags.x;
    if (previewSurfaceKind == PreviewSurfaceKindUnlitSurfaceValue) return float4(baseColor.rgb + emission, baseColor.a);
    if (previewSurfaceKind == PreviewSurfaceKindEmissiveSurfaceValue) return float4(baseColor.rgb + emission, baseColor.a);

    float3 normal = normalize(input.worldNormal);
    if ((draw.flags.z & NormalTextureFlag) != 0) normal = mapped_normal(input, normal);
    float3 viewDirection = normalize(camera.cameraPosition - input.worldPosition);
    float roughness = max(saturate((draw.flags.z & RoughnessTextureFlag) != 0 ? roughnessTexture.Sample(materialSampler, textureUv).r : draw.material.x), 0.045);
    float metallic = saturate(draw.material.y);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor.rgb, metallic);
    float3 color = baseColor.rgb * camera.environmentColor * camera.environmentIntensity * (1.0 - metallic);
    uint lightCount = min(camera.lightCounts.x, MaxViewportDirectLights);
    for (uint lightIndex = 0; lightIndex < lightCount; ++lightIndex) {
        float lightKind = camera.lightDirections[lightIndex].w;
        float3 surfaceToLight;
        float3 radiance = camera.lightColorIntensities[lightIndex].rgb * camera.lightColorIntensities[lightIndex].a;
        if (lightKind == PreviewLightKindDirectionalValue) {
            surfaceToLight = normalize(-camera.lightDirections[lightIndex].xyz);
        } else {
            float3 lightOffset = camera.lightPositions[lightIndex].xyz - input.worldPosition;
            float distanceSquared = max(dot(lightOffset, lightOffset), 0.0001);
            surfaceToLight = lightOffset * rsqrt(distanceSquared);
            if (lightKind == PreviewLightKindSpotValue) {
                float spotCosine = dot(-surfaceToLight, normalize(camera.lightDirections[lightIndex].xyz));
                if (spotCosine <= camera.lightPositions[lightIndex].w) continue;
            }
            if (lightKind == PreviewLightKindAreaValue) {
                float facing = max(dot(-surfaceToLight, normalize(camera.lightDirections[lightIndex].xyz)), 0.0);
                if (facing <= 0.0) continue;
                radiance *= facing;
            }
            radiance /= distanceSquared;
        }
        float3 halfDirection = normalize(viewDirection + surfaceToLight);
        float normalDotLight = max(dot(normal, surfaceToLight), 0.0);
        float normalDotHalf = max(dot(normal, halfDirection), 0.0);
        float halfDotView = max(dot(halfDirection, viewDirection), 0.0);
        float normalDistribution = distribution_ggx(normalDotHalf, roughness);
        float geometry = geometry_smith(normalDotView, normalDotLight, roughness);
        float3 fresnel = fresnel_schlick(halfDotView, f0);
        float3 numerator = normalDistribution * geometry * fresnel;
        float denominator = max(4.0 * normalDotView * normalDotLight, 0.0001);
        float3 specular = numerator / denominator;
        float3 diffuseWeight = (float3(1.0, 1.0, 1.0) - fresnel) * (1.0 - metallic);
        color += (diffuseWeight * baseColor.rgb / Pi + specular) * radiance * normalDotLight;
    }
    color += emission;
    return float4(color, baseColor.a);
}
