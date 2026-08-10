module raster_dynamic_light;

import raster_scene;

static const float raster_dynamic_pi = 3.14159265358979323846;
static const uint raster_dynamic_slot = 2;
static const uint raster_dynamic_active = 1;

struct RasterDynamicAreaLightPushData {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    DescriptorHandle<StructuredBuffer<RasterTransform>> transforms;
    DescriptorHandle<RWStructuredBuffer<RasterLight>> lights;
    DescriptorHandle<RWStructuredBuffer<RasterAreaLight>> area_lights;
    DescriptorHandle<StructuredBuffer<uint>> reserved;
    uint4 range;
    uint4 metadata;
    float4 geometry_parameters;
    float4 emission_parameters;
    float4 source_radiance;
}

[vk::push_constant]
uniform RasterDynamicAreaLightPushData dynamic_push;

float3 dynamic_raster_transform_point(RasterTransform transform, float3 point) {
    const float4 homogeneous = float4(point, 1.0);
    return float3(dot(transform.transform_row_0, homogeneous), dot(transform.transform_row_1, homogeneous), dot(transform.transform_row_2, homogeneous));
}

void set_dynamic_raster_transform(inout RasterLight light, RasterTransform transform) {
    light.transform_row_0 = transform.transform_row_0;
    light.transform_row_1 = transform.transform_row_1;
    light.transform_row_2 = transform.transform_row_2;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void update_raster_dynamic_area_light_shapes(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint element = dispatch_thread_id.x;
    if (element >= dynamic_push.range.w) return;
    const uint light_index = dynamic_push.range.y + element;
    RWStructuredBuffer<RasterLight> lights = dynamic_push.lights;
    RasterLight light = lights[light_index];
    light.metadata.w = raster_dynamic_slot;
    if (element >= dynamic_push.range.z) {
        light.radiance = float4(0.0);
        light.selection = float4(0.0);
        lights[light_index] = light;
        return;
    }
    light.metadata = uint4(dynamic_push.metadata.y, dynamic_push.metadata.z, dynamic_push.metadata.w, raster_dynamic_slot | raster_dynamic_active);
    const RasterTransform transform = dynamic_push.transforms[dynamic_push.range.x];
    const uint source_kind = dynamic_push.metadata.x & 0xff;
    if (source_kind == 0) {
        StructuredBuffer<uint> indices = dynamic_push.indices;
        StructuredBuffer<float3> positions = dynamic_push.positions;
        const uint index_0 = indices[element * 3];
        const uint index_1 = indices[element * 3 + 1];
        const uint index_2 = indices[element * 3 + 2];
        const float3 position_0 = dynamic_raster_transform_point(transform, positions[index_0]);
        const float3 position_1 = dynamic_raster_transform_point(transform, positions[index_1]);
        const float3 position_2 = dynamic_raster_transform_point(transform, positions[index_2]);
        light.position = float4(position_0, 0.0);
        light.direction = float4(position_1 - position_0, 0.0);
        light.parameters = float4(position_2 - position_0, length(cross(position_1 - position_0, position_2 - position_0)) * 0.5);
        if (((dynamic_push.metadata.x >> 8) & 8) != 0) {
            StructuredBuffer<float2> texture_coordinates = dynamic_push.texture_coordinates;
            const float2 uv_0 = texture_coordinates[index_0];
            const float2 uv_1 = texture_coordinates[index_1];
            const float2 uv_2 = texture_coordinates[index_2];
            light.texture_coordinates_0 = float4(uv_0, uv_1 - uv_0);
            light.texture_coordinates_1 = float4(uv_2 - uv_0, 0.0, 0.0);
        }
        else {
            light.texture_coordinates_0 = float4(0.0, 0.0, 1.0, 0.0);
            light.texture_coordinates_1 = float4(1.0, 1.0, 0.0, 0.0);
        }
    }
    else if (source_kind == 1) {
        StructuredBuffer<float3> positions = dynamic_push.positions;
        StructuredBuffer<float> radii = dynamic_push.radii;
        const float radius = radii[element];
        light.position = float4(positions[element], 0.0);
        light.parameters = float4(radius, -radius, radius, 2.0 * raster_dynamic_pi);
        set_dynamic_raster_transform(light, transform);
    }
    else {
        light.position = float4(0.0);
        light.parameters = dynamic_push.geometry_parameters;
        set_dynamic_raster_transform(light, transform);
    }
    lights[light_index] = light;
}

static const float raster_gauss_nodes[16] = {
    0.048307665687738316, 0.14447196158279649, 0.23928736225213707, 0.33186860228212765,
    0.42135127613063535, 0.50689990893222939, 0.58771575724076233, 0.66304426693021520,
    0.73218211874028968, 0.79448379596794241, 0.84936761373256997, 0.89632115576605212,
    0.93490607593773969, 0.96476225558750643, 0.98561151154526834, 0.99726386184948156,
};

static const float raster_gauss_weights[16] = {
    0.09654008851472780, 0.09563872007927486, 0.09384439908080457, 0.09117387869576388,
    0.08765209300440381, 0.08331192422694676, 0.07819389578707031, 0.07234579410884851,
    0.06582222277636185, 0.05868409347853555, 0.05099805926237618, 0.04283589802222668,
    0.03427386291302143, 0.02539206530926206, 0.01627439473090567, 0.00701861000947010,
};

float raster_dynamic_area_scale(RasterLight light, float3 normal) {
    const float3 column_0 = float3(light.transform_row_0.x, light.transform_row_1.x, light.transform_row_2.x);
    const float3 column_1 = float3(light.transform_row_0.y, light.transform_row_1.y, light.transform_row_2.y);
    const float3 column_2 = float3(light.transform_row_0.z, light.transform_row_1.z, light.transform_row_2.z);
    return length(cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z);
}

float raster_dynamic_world_area(RasterLight light) {
    if (light.metadata.x == 3) return light.parameters.w;
    if (light.metadata.x == 5) return 0.5 * light.parameters.w * (light.parameters.y * light.parameters.y - light.parameters.z * light.parameters.z) * raster_dynamic_area_scale(light, float3(0.0, 0.0, 1.0));
    if (light.metadata.x == 4) {
        float sum = 0.0;
        for (uint x = 0; x != 16; ++x)
            for (uint sign_x = 0; sign_x != 2; ++sign_x) {
                const float phi = ((sign_x == 0 ? -raster_gauss_nodes[x] : raster_gauss_nodes[x]) * 0.5 + 0.5) * light.parameters.w;
                for (uint y = 0; y != 16; ++y)
                    for (uint sign_y = 0; sign_y != 2; ++sign_y) {
                        const float height = lerp(light.parameters.y, light.parameters.z, (sign_y == 0 ? -raster_gauss_nodes[y] : raster_gauss_nodes[y]) * 0.5 + 0.5);
                        const float radial = sqrt(max(0.0, light.parameters.x * light.parameters.x - height * height)) / light.parameters.x;
                        sum += raster_gauss_weights[x] * raster_gauss_weights[y] * raster_dynamic_area_scale(light, float3(radial * cos(phi), radial * sin(phi), height / light.parameters.x));
                    }
            }
        return light.parameters.x * light.parameters.w * (light.parameters.z - light.parameters.y) * 0.25 * sum;
    }
    float sum = 0.0;
    for (uint x = 0; x != 16; ++x)
        for (uint sign_x = 0; sign_x != 2; ++sign_x) {
            const float phi = ((sign_x == 0 ? -raster_gauss_nodes[x] : raster_gauss_nodes[x]) * 0.5 + 0.5) * light.parameters.w;
            sum += raster_gauss_weights[x] * raster_dynamic_area_scale(light, float3(cos(phi), sin(phi), 0.0));
        }
    return light.parameters.x * (light.parameters.z - light.parameters.y) * light.parameters.w * 0.5 * sum;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void finalize_raster_dynamic_area_light_range() {
    RWStructuredBuffer<RasterLight> lights = dynamic_push.lights;
    float total_area = 0.0;
    for (uint element = 0; element != dynamic_push.range.z; ++element) total_area += raster_dynamic_world_area(lights[dynamic_push.range.y + element]);
    float scale = dynamic_push.emission_parameters.x;
    if (dynamic_push.emission_parameters.y > 0.0) scale *= dynamic_push.emission_parameters.y / (dynamic_push.emission_parameters.z * total_area * raster_dynamic_pi * dynamic_push.emission_parameters.w);
    const float3 radiance = dynamic_push.source_radiance.xyz * scale;
    const float selection_luminance = dynamic_push.source_radiance.w * scale * dynamic_push.emission_parameters.w;
    for (uint element = 0; element != dynamic_push.range.w; ++element) {
        const uint light_index = dynamic_push.range.y + element;
        RasterLight light = lights[light_index];
        if (element < dynamic_push.range.z) {
            light.radiance = float4(radiance, 0.0);
            light.selection.y = selection_luminance * raster_dynamic_world_area(light);
        }
        else {
            light.radiance = float4(0.0);
            light.selection.y = 0.0;
        }
        lights[light_index] = light;
    }
    RWStructuredBuffer<RasterAreaLight> area_lights = dynamic_push.area_lights;
    RasterAreaLight area_light = area_lights[dynamic_push.range.x];
    area_light.emission = float4(radiance, 0.0);
    area_lights[dynamic_push.range.x] = area_light;
}

struct RasterDynamicSelectionPushData {
    DescriptorHandle<RWStructuredBuffer<RasterLight>> lights;
    uint light_count;
}

[vk::push_constant]
uniform RasterDynamicSelectionPushData selection_push;

[shader("compute")]
[numthreads(1, 1, 1)]
void rebuild_raster_dynamic_light_selection() {
    RWStructuredBuffer<RasterLight> lights = selection_push.lights;
    float total_weight = 0.0;
    uint eligible_count = 0;
    for (uint index = 0; index != selection_push.light_count; ++index) {
        const RasterLight light = lights[index];
        const bool eligible = (light.metadata.w & raster_dynamic_slot) == 0 || (light.metadata.w & raster_dynamic_active) != 0;
        if (eligible) {
            total_weight += light.selection.y;
            ++eligible_count;
        }
    }
    if (eligible_count == 0) {
        for (uint index = 0; index != selection_push.light_count; ++index) {
            RasterLight light = lights[index];
            light.selection.x = 0.0;
            lights[index] = light;
        }
        return;
    }
    const bool zero_weight = total_weight == 0.0;
    float cumulative = 0.0;
    for (uint index = 0; index != selection_push.light_count; ++index) {
        RasterLight light = lights[index];
        const bool eligible = (light.metadata.w & raster_dynamic_slot) == 0 || (light.metadata.w & raster_dynamic_active) != 0;
        if (eligible) cumulative += zero_weight ? 1.0 / float(eligible_count) : light.selection.y / total_weight;
        light.selection.x = cumulative;
        lights[index] = light;
    }
}
