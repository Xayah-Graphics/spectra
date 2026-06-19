static const uint MaxViewportDirectLights = 8;

struct CameraUniform {
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float3 cameraPosition;
    float _pad0;
    float3 environmentColor;
    float environmentIntensity;
    float4 lightDirections[MaxViewportDirectLights];
    float4 lightPositions[MaxViewportDirectLights];
    float4 lightColorIntensities[MaxViewportDirectLights];
    uint4 lightCounts;
    float3 cameraForward;
    float _pad1;
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
    output.position = mul(world, camera.worldToClip);
    output.worldPosition = world.xyz;
    output.worldNormal = normalize(mul(float4(input.normal, 0.0), draw.model).xyz);
    return output;
}

static const uint PreviewSurfaceKindUnlitSurfaceValue = 1;
static const uint PreviewSurfaceKindEmissiveSurfaceValue = 2;
static const uint PreviewAlphaModeMaskedValue = 1;
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

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    if (draw.flags.y == PreviewAlphaModeMaskedValue && draw.base_color.a < draw.material.z) discard;
    uint previewSurfaceKind = draw.flags.x;
    if (previewSurfaceKind == PreviewSurfaceKindUnlitSurfaceValue) return float4(draw.base_color.rgb + draw.emission.rgb, draw.base_color.a);
    if (previewSurfaceKind == PreviewSurfaceKindEmissiveSurfaceValue) return float4(draw.base_color.rgb + draw.emission.rgb, draw.base_color.a);

    float3 normal = normalize(input.worldNormal);
    float3 viewDirection = normalize(camera.cameraPosition - input.worldPosition);
    float roughness = max(saturate(draw.material.x), 0.045);
    float metallic = saturate(draw.material.y);
    float normalDotView = max(dot(normal, viewDirection), 0.0);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), draw.base_color.rgb, metallic);
    float3 color = draw.base_color.rgb * camera.environmentColor * camera.environmentIntensity * (1.0 - metallic);
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
        color += (diffuseWeight * draw.base_color.rgb / Pi + specular) * radiance * normalDotLight;
    }
    color += draw.emission.rgb;
    return float4(color, draw.base_color.a);
}
