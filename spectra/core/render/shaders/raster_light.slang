module raster_light;

import raster_scene;
import raster_texture;
import sphere_mapping;

static const float raster_light_pi = 3.14159265358979323846;
static const uint raster_invalid_index = 0xffffffff;

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure raster_acceleration_structure;

public struct RasterLightSample {
    public float3 direction;
    public float distance;
    public float3 weight;
    public uint valid;
}

public uint sample_raster_light_index(StructuredBuffer<RasterLight> lights, uint count, float sample, out float probability) {
    uint first = 0;
    uint length = count;
    while (length != 0) {
        const uint half = length / 2;
        const uint middle = first + half;
        if (sample >= lights[middle].selection.x) {
            first = middle + 1;
            length -= half + 1;
        }
        else length = half;
    }
    const uint index = min(first, count - 1);
    const float previous = index == 0 ? 0.0 : lights[index - 1].selection.x;
    probability = lights[index].selection.x - previous;
    return index;
}

float3 raster_light_transform_point(RasterLight light, float3 point) {
    const float4 homogeneous = float4(point, 1.0);
    return float3(
        dot(light.transform_row_0, homogeneous),
        dot(light.transform_row_1, homogeneous),
        dot(light.transform_row_2, homogeneous));
}

float3 raster_light_transform_vector(RasterLight light, float3 vector) {
    return float3(
        dot(light.transform_row_0.xyz, vector),
        dot(light.transform_row_1.xyz, vector),
        dot(light.transform_row_2.xyz, vector));
}

float3 raster_light_transform_normal(RasterLight light, float3 normal) {
    const float3 column_0 = float3(light.transform_row_0.x, light.transform_row_1.x, light.transform_row_2.x);
    const float3 column_1 = float3(light.transform_row_0.y, light.transform_row_1.y, light.transform_row_2.y);
    const float3 column_2 = float3(light.transform_row_0.z, light.transform_row_1.z, light.transform_row_2.z);
    return cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z;
}

float3 raster_ray_transform_point(RasterRayPrimitive primitive, RasterSceneBindings bindings, float3 point) {
    StructuredBuffer<RasterTransform> transforms = bindings.transforms;
    const RasterTransform transform = transforms[primitive.scene_primitive_index];
    const float4 homogeneous = float4(point, 1.0);
    return float3(
        dot(transform.transform_row_0, homogeneous),
        dot(transform.transform_row_1, homogeneous),
        dot(transform.transform_row_2, homogeneous));
}

float3 raster_ray_transform_normal(RasterRayPrimitive primitive, RasterSceneBindings bindings, float3 normal) {
    StructuredBuffer<RasterTransform> transforms = bindings.transforms;
    const RasterTransform transform = transforms[primitive.scene_primitive_index];
    const float3 column_0 = float3(transform.transform_row_0.x, transform.transform_row_1.x, transform.transform_row_2.x);
    const float3 column_1 = float3(transform.transform_row_0.y, transform.transform_row_1.y, transform.transform_row_2.y);
    const float3 column_2 = float3(transform.transform_row_0.z, transform.transform_row_1.z, transform.transform_row_2.z);
    return normalize(cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z);
}

float raster_positive_phi(float3 position) {
    if (position.x == 0.0 && position.y == 0.0) return 0.0;
    float phi = atan2(position.y, position.x);
    if (phi < 0.0) phi += 2.0 * raster_light_pi;
    return phi;
}

float raster_procedural_hit_distance(RasterRayPrimitive primitive, float3 origin, float3 direction, float minimum, float maximum, uint primitive_index, out float3 object_position, out float3 object_normal, out float2 texture_coordinates) {
    object_position = float3(0.0);
    object_normal = float3(0.0);
    texture_coordinates = float2(0.0);
    uint kind = primitive.metadata.x;
    float4 parameters = primitive.parameters;
    float3 center = float3(0.0);
    if (kind == 4) {
        StructuredBuffer<float3> positions = primitive.positions;
        StructuredBuffer<float> radii = primitive.radii;
        center = positions[primitive_index];
        const float radius = radii[primitive_index];
        origin -= center;
        parameters = float4(radius, -radius, radius, 2.0 * raster_light_pi);
        kind = 1;
    }
    if (kind == 2) {
        if (direction.z == 0.0) return -1.0;
        const float distance = (parameters.x - origin.z) / direction.z;
        if (distance <= minimum || distance >= maximum) return -1.0;
        const float3 position = origin + distance * direction;
        const float radius_squared = dot(position.xy, position.xy);
        const float phi = raster_positive_phi(position);
        if (radius_squared > parameters.y * parameters.y || radius_squared < parameters.z * parameters.z || phi > parameters.w) return -1.0;
        const float radius = sqrt(radius_squared);
        object_position = position + center;
        object_normal = float3(0.0, 0.0, 1.0);
        texture_coordinates = float2(phi / parameters.w, (parameters.y - radius) / (parameters.y - parameters.z));
        return distance;
    }
    const float a = kind == 1 ? dot(direction, direction) : dot(direction.xy, direction.xy);
    const float half_b = kind == 1 ? dot(origin, direction) : dot(origin.xy, direction.xy);
    const float c = kind == 1 ? dot(origin, origin) - parameters.x * parameters.x : dot(origin.xy, origin.xy) - parameters.x * parameters.x;
    const float discriminant = half_b * half_b - a * c;
    if (discriminant < 0.0) return -1.0;
    const float root = sqrt(discriminant);
    const float distances[2] = {(-half_b - root) / a, (-half_b + root) / a};
    for (uint index = 0; index != 2; ++index) {
        const float distance = distances[index];
        if (distance <= minimum || distance >= maximum) continue;
        float3 position = origin + distance * direction;
        if (kind == 1) position *= parameters.x / length(position);
        else position.xy *= parameters.x / length(position.xy);
        const float phi = raster_positive_phi(position);
        if (position.z < parameters.y || position.z > parameters.z || phi > parameters.w) continue;
        object_position = position + center;
        object_normal = kind == 1 ? normalize(position) : normalize(float3(position.xy, 0.0));
        if (kind == 1) {
            const float theta = acos(clamp(position.z / parameters.x, -1.0, 1.0));
            const float theta_min = acos(clamp(parameters.y / parameters.x, -1.0, 1.0));
            const float theta_max = acos(clamp(parameters.z / parameters.x, -1.0, 1.0));
            texture_coordinates = float2(phi / parameters.w, (theta - theta_min) / (theta_max - theta_min));
        }
        else texture_coordinates = float2(phi / parameters.w, (position.z - parameters.y) / (parameters.z - parameters.y));
        return distance;
    }
    return -1.0;
}

bool raster_alpha_visible(RasterRayPrimitive primitive, RasterSceneBindings bindings, float3 object_position, float3 object_normal, float2 texture_coordinates) {
    if (primitive.metadata.y == raster_invalid_index) return true;
    RasterTextureContext context = {
        raster_ray_transform_point(primitive, bindings, object_position),
        raster_ray_transform_normal(primitive, bindings, primitive.metadata.w == 0 ? object_normal : -object_normal),
        texture_coordinates,
        float3(0.0),
        float3(0.0),
        float2(0.0),
        float2(0.0),
    };

    return evaluate_raster_texture(bindings, primitive.metadata.y, context).x >= 0.5;
}

public bool raster_surface_visible(RasterSceneBindings bindings, float3 origin, float3 direction, float maximum) {
    RayDesc ray;
    ray.Origin = origin;
    ray.TMin = 1.0e-3;
    ray.Direction = direction;
    ray.TMax = maximum;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(raster_acceleration_structure, RAY_FLAG_NONE, 0x7f, ray);
    StructuredBuffer<RasterRayPrimitive> ray_primitives = bindings.ray_primitives;
    while (query.Proceed()) {
        const RasterRayPrimitive primitive = ray_primitives[query.CandidateInstanceID()];
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) {
            bool opaque = primitive.metadata.y == raster_invalid_index;
            if (!opaque) {
                StructuredBuffer<float3> positions = primitive.positions;
                StructuredBuffer<float2> texture_coordinates = primitive.texture_coordinates;
                StructuredBuffer<uint> indices = primitive.indices;
                const uint triangle = query.CandidatePrimitiveIndex();
                const uint3 vertex_indices = uint3(indices[triangle * 3], indices[triangle * 3 + 1], indices[triangle * 3 + 2]);
                const float2 barycentrics = query.CandidateTriangleBarycentrics();
                const float3 weights = float3(1.0 - barycentrics.x - barycentrics.y, barycentrics.x, barycentrics.y);
                const float3 first = positions[vertex_indices.x];
                const float3 second = positions[vertex_indices.y];
                const float3 third = positions[vertex_indices.z];
                const float3 object_position = first * weights.x + second * weights.y + third * weights.z;
                const float3 object_normal = normalize(cross(second - first, third - first));
                const float2 uv = (primitive.metadata.z & 4) != 0 ? texture_coordinates[vertex_indices.x] * weights.x + texture_coordinates[vertex_indices.y] * weights.y + texture_coordinates[vertex_indices.z] * weights.z : float2(0.0);
                opaque = raster_alpha_visible(primitive, bindings, object_position, object_normal, uv);
            }
            if (opaque) query.CommitNonOpaqueTriangleHit();
        }
        else {
            float3 object_position;
            float3 object_normal;
            float2 texture_coordinates;
            const float distance = raster_procedural_hit_distance(primitive, query.CandidateObjectRayOrigin(), query.CandidateObjectRayDirection(), ray.TMin, query.CommittedRayT(), query.CandidatePrimitiveIndex(), object_position, object_normal, texture_coordinates);
            if (distance > 0.0 && raster_alpha_visible(primitive, bindings, object_position, object_normal, texture_coordinates)) query.CommitProceduralPrimitiveHit(distance);
        }
    }
    return query.CommittedStatus() == COMMITTED_NOTHING;
}

float3 raster_light_emission(RasterLight light, RasterSceneBindings bindings, float3 position, float3 normal, float2 texture_coordinates) {
    float3 radiance = light.radiance.xyz;
    if (light.metadata.z == raster_invalid_index) return radiance;
    RasterTextureContext context = {position, normal, texture_coordinates, float3(0.0), float3(0.0), float2(0.0), float2(0.0)};
    return radiance * evaluate_raster_texture(bindings, light.metadata.z, context).xyz;
}

public float3 raster_environment_emission(RasterLight light, RasterSceneBindings bindings, float3 direction) {
    float3 radiance = light.radiance.xyz;
    if (light.metadata.z == raster_invalid_index) return radiance;
    const float3 local_direction = normalize(raster_light_transform_vector(light, direction));
    RasterTextureContext context = {float3(0.0), local_direction, equal_area_sphere_to_square(local_direction), float3(0.0), float3(0.0), float2(0.0), float2(0.0)};
    return radiance * evaluate_raster_texture(bindings, light.metadata.z, context).xyz;
}

public RasterLightSample sample_raster_light(RasterLight light, RasterSceneBindings bindings, float3 position, float2 sample) {
    RasterLightSample result = {float3(0.0), 0.0, float3(0.0), 0};
    if (light.metadata.x <= 1) {
        const float3 offset = light.position.xyz - position;
        result.distance = length(offset);
        result.direction = offset / result.distance;
        result.weight = light.radiance.xyz / max(result.distance * result.distance, 1.0e-6);
        if (light.metadata.x == 1) {
            const float cosine = dot(-result.direction, normalize(light.direction.xyz));
            result.weight *= smoothstep(light.parameters.x, light.parameters.y, cosine);
        }
        result.valid = 1;
        return result;
    }
    if (light.metadata.x == 2) {
        result.direction = normalize(-light.direction.xyz);
        result.distance = 1.0e20;
        result.weight = light.radiance.xyz;
        result.valid = 1;
        return result;
    }
    if (light.metadata.x == 7) {
        result.direction = equal_area_square_to_sphere(sample);
        result.distance = 1.0e20;
        result.weight = raster_environment_emission(light, bindings, result.direction) * (4.0 * raster_light_pi);
        result.valid = 1;
        return result;
    }
    float3 light_position;
    float3 light_normal;
    float2 texture_coordinates;
    float inverse_area_pdf;
    if (light.metadata.x == 3 || light.metadata.x == 8) {
        if (light.metadata.x == 3) {
            const float root = sqrt(sample.x);
            const float2 barycentrics = float2(root * (1.0 - sample.y), root * sample.y);
            light_position = light.position.xyz + barycentrics.x * light.direction.xyz + barycentrics.y * light.parameters.xyz;
            texture_coordinates = light.texture_coordinates_0.xy + barycentrics.x * light.texture_coordinates_0.zw + barycentrics.y * light.texture_coordinates_1.xy;
        }
        else {
            light_position = light.position.xyz + sample.x * light.direction.xyz + sample.y * light.parameters.xyz;
            texture_coordinates = float2(0.0);
        }
        light_normal = normalize(cross(light.direction.xyz, light.parameters.xyz));
        inverse_area_pdf = light.parameters.w;
    }
    else {
        float3 object_position;
        float3 object_normal;
        float object_area;
        const float phi = sample.y * light.parameters.w;
        if (light.metadata.x == 4) {
            const float z = lerp(light.parameters.y, light.parameters.z, sample.x);
            const float radial = sqrt(max(0.0, light.parameters.x * light.parameters.x - z * z));
            object_position = light.position.xyz + float3(radial * cos(phi), radial * sin(phi), z);
            object_normal = normalize(object_position - light.position.xyz);
            object_area = light.parameters.w * light.parameters.x * (light.parameters.z - light.parameters.y);
            texture_coordinates = float2(sample.y, (acos(clamp(z / light.parameters.x, -1.0, 1.0)) - acos(clamp(light.parameters.y / light.parameters.x, -1.0, 1.0))) / (acos(clamp(light.parameters.z / light.parameters.x, -1.0, 1.0)) - acos(clamp(light.parameters.y / light.parameters.x, -1.0, 1.0))));
        }
        else if (light.metadata.x == 5) {
            const float radius = sqrt(lerp(light.parameters.z * light.parameters.z, light.parameters.y * light.parameters.y, sample.x));
            object_position = light.position.xyz + float3(radius * cos(phi), radius * sin(phi), light.parameters.x);
            object_normal = float3(0.0, 0.0, 1.0);
            object_area = 0.5 * light.parameters.w * (light.parameters.y * light.parameters.y - light.parameters.z * light.parameters.z);
            texture_coordinates = float2(sample.y, (light.parameters.y - radius) / (light.parameters.y - light.parameters.z));
        }
        else {
            const float z = lerp(light.parameters.y, light.parameters.z, sample.x);
            object_position = light.position.xyz + float3(light.parameters.x * cos(phi), light.parameters.x * sin(phi), z);
            object_normal = float3(cos(phi), sin(phi), 0.0);
            object_area = light.parameters.w * light.parameters.x * (light.parameters.z - light.parameters.y);
            texture_coordinates = float2(sample.y, sample.x);
        }
        const float3 transformed_normal = raster_light_transform_normal(light, object_normal);
        inverse_area_pdf = object_area * length(transformed_normal);
        light_position = raster_light_transform_point(light, object_position);
        light_normal = normalize(transformed_normal);
    }
    if ((light.metadata.y & 2) != 0) light_normal = -light_normal;
    const float3 offset = light_position - position;
    result.distance = length(offset);
    result.direction = offset / result.distance;
    const float cosine = dot(light_normal, -result.direction);
    const float sided_cosine = (light.metadata.y & 1) != 0 || light.metadata.x == 8 ? abs(cosine) : max(cosine, 0.0);
    if (sided_cosine == 0.0) return result;
    const float3 emission = light.metadata.x == 8 ? raster_environment_emission(light, bindings, result.direction) : raster_light_emission(light, bindings, light_position, light_normal, texture_coordinates);
    result.weight = emission * sided_cosine * inverse_area_pdf / max(result.distance * result.distance, 1.0e-6);
    result.valid = 1;
    return result;
}
