module raster_area_emission;

import raster_scene;

static const float raster_preview_pi = 3.14159265358979323846;
static const uint raster_preview_area_threads = 64;

struct RasterAreaEmissionPushData {
    DescriptorHandle<StructuredBuffer<float3>> positions;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<RasterTransform>> transforms;
    DescriptorHandle<RWStructuredBuffer<RasterAreaLight>> area_lights;
    uint64_t reserved;
    uint4 range;
    float4 geometry_parameters;
    float4 emission_parameters;
    float4 source_radiance;
}

[vk::push_constant]
uniform RasterAreaEmissionPushData push_data;

groupshared float areas[raster_preview_area_threads];

float3 transform_point(RasterTransform transform, float3 point) {
    const float4 homogeneous = float4(point, 1.0);
    return float3(
        dot(transform.transform_row_0, homogeneous),
        dot(transform.transform_row_1, homogeneous),
        dot(transform.transform_row_2, homogeneous));
}

float3 transform_vector(RasterTransform transform, float3 vector) {
    return float3(
        dot(transform.transform_row_0.xyz, vector),
        dot(transform.transform_row_1.xyz, vector),
        dot(transform.transform_row_2.xyz, vector));
}

float area_scale(RasterTransform transform, float3 normal) {
    const float3 column_0 = float3(transform.transform_row_0.x, transform.transform_row_1.x, transform.transform_row_2.x);
    const float3 column_1 = float3(transform.transform_row_0.y, transform.transform_row_1.y, transform.transform_row_2.y);
    const float3 column_2 = float3(transform.transform_row_0.z, transform.transform_row_1.z, transform.transform_row_2.z);
    return length(cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z);
}

[shader("compute")]
[numthreads(raster_preview_area_threads, 1, 1)]
void update_raster_area_emission(uint3 group_thread_id : SV_GroupThreadID) {
    const uint thread = group_thread_id.x;
    const uint source_kind = push_data.range.z;
    const uint geometry_kind = push_data.range.w;
    const RasterTransform transform = push_data.transforms[push_data.range.x];
    float area = 0.0;

    if (source_kind == area_source_triangle) {
        StructuredBuffer<float3> positions = push_data.positions;
        StructuredBuffer<uint> indices = push_data.indices;
        for (uint element = thread; element < push_data.range.y; element += raster_preview_area_threads) {
            const float3 position_0 = transform_point(transform, positions[indices[element * 3]]);
            const float3 position_1 = transform_point(transform, positions[indices[element * 3 + 1]]);
            const float3 position_2 = transform_point(transform, positions[indices[element * 3 + 2]]);
            area += 0.5 * length(cross(position_1 - position_0, position_2 - position_0));
        }
    } else if (source_kind == area_source_sphere_set) {
        StructuredBuffer<float> radii = push_data.radii;
        const float3 axis_x = transform_vector(transform, float3(1.0, 0.0, 0.0));
        const float3 axis_y = transform_vector(transform, float3(0.0, 1.0, 0.0));
        const float3 axis_z = transform_vector(transform, float3(0.0, 0.0, 1.0));
        static const float ellipsoid_power = 1.6075;
        for (uint element = thread; element < push_data.range.y; element += raster_preview_area_threads) {
            const float radius = radii[element];
            const float axis_a = radius * length(axis_x);
            const float axis_b = radius * length(axis_y);
            const float axis_c = radius * length(axis_z);
            area += 4.0 * raster_preview_pi * pow((pow(axis_a * axis_b, ellipsoid_power) + pow(axis_a * axis_c, ellipsoid_power) + pow(axis_b * axis_c, ellipsoid_power)) / 3.0, 1.0 / ellipsoid_power);
        }
    } else if (geometry_kind == geometry_sphere) {
        const uint height_index = thread & 7;
        const uint angular_index = thread >> 3;
        const float radius = push_data.geometry_parameters.x;
        const float z = lerp(push_data.geometry_parameters.y, push_data.geometry_parameters.z, (float(height_index) + 0.5) / 8.0);
        const float phi = push_data.geometry_parameters.w * (float(angular_index) + 0.5) / 8.0;
        const float radial = sqrt(max(0.0, radius * radius - z * z));
        const float3 normal = normalize(float3(radial * cos(phi), radial * sin(phi), z));
        area = radius * push_data.geometry_parameters.w * (push_data.geometry_parameters.z - push_data.geometry_parameters.y) * area_scale(transform, normal) / float(raster_preview_area_threads);
    } else if (geometry_kind == geometry_disk) {
        area = 0.5 * push_data.geometry_parameters.w * (push_data.geometry_parameters.y * push_data.geometry_parameters.y - push_data.geometry_parameters.z * push_data.geometry_parameters.z) * area_scale(transform, float3(0.0, 0.0, 1.0)) / float(raster_preview_area_threads);
    } else {
        const uint height_index = thread & 7;
        const uint angular_index = thread >> 3;
        const float phi = push_data.geometry_parameters.w * (float(angular_index) + 0.5) / 8.0;
        const float3 normal = float3(cos(phi), sin(phi), 0.0);
        area = push_data.geometry_parameters.x * push_data.geometry_parameters.w * (push_data.geometry_parameters.z - push_data.geometry_parameters.y) * area_scale(transform, normal) / float(raster_preview_area_threads);
    }

    areas[thread] = area;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = raster_preview_area_threads / 2; stride != 0; stride >>= 1) {
        if (thread < stride) areas[thread] += areas[thread + stride];
        GroupMemoryBarrierWithGroupSync();
    }
    if (thread != 0) return;

    float scale = push_data.emission_parameters.x;
    if (push_data.emission_parameters.y > 0.0 && areas[0] > 0.0) scale *= push_data.emission_parameters.y / (push_data.emission_parameters.z * areas[0] * raster_preview_pi * push_data.emission_parameters.w);
    RWStructuredBuffer<RasterAreaLight> area_lights = push_data.area_lights;
    RasterAreaLight area_light = area_lights[push_data.range.x];
    area_light.emission = float4(push_data.source_radiance.xyz * scale, 0.0);
    area_lights[push_data.range.x] = area_light;
}
