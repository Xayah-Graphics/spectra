module path_dynamic_light;

import path_scene;

static const float dynamic_light_pi = 3.14159265358979323846;
static const uint dynamic_light_invalid = 0xffffffff;
static const uint dynamic_light_active = 32;
static const uint dynamic_light_slot = 64;

struct DynamicLightTransform {
    float4 row_0;
    float4 row_1;
    float4 row_2;
    float4 row_3;
}

struct DynamicAreaLightPushData {
    DescriptorHandle<StructuredBuffer<PathPackedFloat3>> positions;
    DescriptorHandle<StructuredBuffer<uint>> indices;
    DescriptorHandle<StructuredBuffer<float>> radii;
    DescriptorHandle<StructuredBuffer<PathPackedFloat3>> normals;
    DescriptorHandle<StructuredBuffer<float2>> texture_coordinates;
    DescriptorHandle<StructuredBuffer<DynamicLightTransform>> transforms;
    DescriptorHandle<RWStructuredBuffer<PathLight>> lights;
    DescriptorHandle<RWStructuredBuffer<PathLightShape>> shapes;
    uint4 range;
    uint4 metadata;
    float4 geometry_parameters;
    float4 emission_parameters;
    float4 selection_parameters;
}

[vk::push_constant]
uniform DynamicAreaLightPushData area_push;

float3 dynamic_transform_point(DynamicLightTransform transform, float3 point) {
    const float4 homogeneous = float4(point, 1.0);
    return float3(dot(transform.row_0, homogeneous), dot(transform.row_1, homogeneous), dot(transform.row_2, homogeneous));
}

float3 dynamic_transform_vector(DynamicLightTransform transform, float3 vector) {
    return float3(dot(transform.row_0.xyz, vector), dot(transform.row_1.xyz, vector), dot(transform.row_2.xyz, vector));
}

float3 dynamic_transform_normal(DynamicLightTransform transform, float3 normal) {
    const float3 column_0 = float3(transform.row_0.x, transform.row_1.x, transform.row_2.x);
    const float3 column_1 = float3(transform.row_0.y, transform.row_1.y, transform.row_2.y);
    const float3 column_2 = float3(transform.row_0.z, transform.row_1.z, transform.row_2.z);
    return normalize(cross(column_1, column_2) * normal.x + cross(column_2, column_0) * normal.y + cross(column_0, column_1) * normal.z);
}

DynamicLightTransform dynamic_sphere_transform(DynamicLightTransform transform, float3 center) {
    DynamicLightTransform result = transform;
    const float3 translation = dynamic_transform_point(transform, center);
    result.row_0.w = translation.x;
    result.row_1.w = translation.y;
    result.row_2.w = translation.z;
    return result;
}

void set_shape_transform(inout PathLightShape shape, DynamicLightTransform transform) {
    shape.transform_row_0 = transform.row_0;
    shape.transform_row_1 = transform.row_1;
    shape.transform_row_2 = transform.row_2;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void update_dynamic_area_light_shapes(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint element = dispatch_thread_id.x;
    if (element >= area_push.range.w) return;
    const uint light_index = area_push.range.y + element;
    RWStructuredBuffer<PathLight> lights = area_push.lights;
    RWStructuredBuffer<PathLightShape> shapes = area_push.shapes;
    PathLight light = lights[light_index];
    PathLightShape shape = shapes[light.references.x];
    light.identifiers.y |= dynamic_light_slot;
    if (element >= area_push.range.z) {
        light.identifiers.y &= ~dynamic_light_active;
        light.position_and_scale.w = 0.0;
        light.selection = float4(0.0);
        shape.position_0_and_area.w = 0.0;
        lights[light_index] = light;
        shapes[light.references.x] = shape;
        return;
    }
    light.identifiers.y |= dynamic_light_active;
    const DynamicLightTransform transform = area_push.transforms[area_push.range.x];
    const uint kind = area_push.metadata.x;
    shape.identifiers.y = area_push.metadata.y;
    shape.identifiers.z = kind == 0 ? geometry_triangle : kind == 1 ? geometry_sphere : area_push.metadata.z;
    shape.identifiers.w = area_push.metadata.w;
    if (kind == 0) {
        StructuredBuffer<uint> indices = area_push.indices;
        StructuredBuffer<PathPackedFloat3> positions = area_push.positions;
        const uint index_0 = indices[element * 3];
        const uint index_1 = indices[element * 3 + 1];
        const uint index_2 = indices[element * 3 + 2];
        const PathPackedFloat3 packed_0 = positions[index_0];
        const PathPackedFloat3 packed_1 = positions[index_1];
        const PathPackedFloat3 packed_2 = positions[index_2];
        const float3 position_0 = dynamic_transform_point(transform, float3(packed_0.x, packed_0.y, packed_0.z));
        const float3 position_1 = dynamic_transform_point(transform, float3(packed_1.x, packed_1.y, packed_1.z));
        const float3 position_2 = dynamic_transform_point(transform, float3(packed_2.x, packed_2.y, packed_2.z));
        shape.position_0_and_area = float4(position_0, length(cross(position_1 - position_0, position_2 - position_0)) * 0.5);
        shape.position_1 = float4(position_1, 0.0);
        shape.position_2 = float4(position_2, 0.0);
        shape.transform_row_0 = float4(1.0, 0.0, 0.0, 0.0);
        shape.transform_row_1 = float4(0.0, 1.0, 0.0, 0.0);
        shape.transform_row_2 = float4(0.0, 0.0, 1.0, 0.0);
        if ((area_push.metadata.z & 8) != 0) {
            StructuredBuffer<float2> texture_coordinates = area_push.texture_coordinates;
            const float2 uv_0 = texture_coordinates[index_0];
            const float2 uv_1 = texture_coordinates[index_1];
            const float2 uv_2 = texture_coordinates[index_2];
            shape.texture_coordinates_0_1 = float4(uv_0, uv_1);
            shape.texture_coordinates_2 = float4(uv_2, 0.0, 0.0);
        }
        else {
            shape.texture_coordinates_0_1 = float4(0.0, 0.0, 1.0, 0.0);
            shape.texture_coordinates_2 = float4(1.0, 1.0, 0.0, 0.0);
        }
        if ((area_push.metadata.z & 2) != 0) {
            StructuredBuffer<PathPackedFloat3> normals = area_push.normals;
            const PathPackedFloat3 packed_normal_0 = normals[index_0];
            const PathPackedFloat3 packed_normal_1 = normals[index_1];
            const PathPackedFloat3 packed_normal_2 = normals[index_2];
            const float direction = area_push.metadata.y == 0 ? 1.0 : -1.0;
            shape.normal_0 = float4(dynamic_transform_normal(transform, float3(packed_normal_0.x, packed_normal_0.y, packed_normal_0.z)) * direction, 1.0);
            shape.normal_1 = float4(dynamic_transform_normal(transform, float3(packed_normal_1.x, packed_normal_1.y, packed_normal_1.z)) * direction, 0.0);
            shape.normal_2 = float4(dynamic_transform_normal(transform, float3(packed_normal_2.x, packed_normal_2.y, packed_normal_2.z)) * direction, 0.0);
        }
        else {
            shape.normal_0 = float4(0.0);
            shape.normal_1 = float4(0.0);
            shape.normal_2 = float4(0.0);
        }
    }
    else if (kind == 1) {
        StructuredBuffer<PathPackedFloat3> positions = area_push.positions;
        StructuredBuffer<float> radii = area_push.radii;
        const PathPackedFloat3 packed = positions[element];
        const float radius = radii[element];
        shape.position_0_and_area = float4(0.0, 0.0, 0.0, 4.0 * dynamic_light_pi * radius * radius);
        shape.position_1 = float4(0.0);
        shape.position_2 = float4(0.0);
        shape.geometry_parameters = float4(radius, -radius, radius, 2.0 * dynamic_light_pi);
        set_shape_transform(shape, dynamic_sphere_transform(transform, float3(packed.x, packed.y, packed.z)));
        shape.texture_coordinates_0_1 = float4(0.0);
        shape.texture_coordinates_2 = float4(0.0);
        shape.normal_0 = float4(0.0);
        shape.normal_1 = float4(0.0);
        shape.normal_2 = float4(0.0);
    }
    else {
        shape.geometry_parameters = area_push.geometry_parameters;
        set_shape_transform(shape, transform);
    }
    shapes[light.references.x] = shape;
    lights[light_index] = light;
}

static const float gauss_nodes[16] = {
    0.048307665687738316, 0.14447196158279649, 0.23928736225213707, 0.33186860228212765,
    0.42135127613063535, 0.50689990893222939, 0.58771575724076233, 0.66304426693021520,
    0.73218211874028968, 0.79448379596794241, 0.84936761373256997, 0.89632115576605212,
    0.93490607593773969, 0.96476225558750643, 0.98561151154526834, 0.99726386184948156,
};

static const float gauss_weights[16] = {
    0.09654008851472780, 0.09563872007927486, 0.09384439908080457, 0.09117387869576388,
    0.08765209300440381, 0.08331192422694676, 0.07819389578707031, 0.07234579410884851,
    0.06582222277636185, 0.05868409347853555, 0.05099805926237618, 0.04283589802222668,
    0.03427386291302143, 0.02539206530926206, 0.01627439473090567, 0.00701861000947010,
};

float dynamic_area_scale(PathLightShape shape, float3 normal) {
    const float3 x = shape.transform_row_0.xyz;
    const float3 y = shape.transform_row_1.xyz;
    const float3 z = shape.transform_row_2.xyz;
    const float3 normal_x = cross(float3(x.y, y.y, z.y), float3(x.z, y.z, z.z));
    const float3 normal_y = cross(float3(x.z, y.z, z.z), float3(x.x, y.x, z.x));
    const float3 normal_z = cross(float3(x.x, y.x, z.x), float3(x.y, y.y, z.y));
    return length(normal_x * normal.x + normal_y * normal.y + normal_z * normal.z);
}

float dynamic_shape_world_area(PathLightShape shape) {
    const uint geometry = shape.identifiers.z;
    if (geometry == geometry_triangle) return shape.position_0_and_area.w;
    if (geometry == geometry_sphere) {
        float sum = 0.0;
        const float phi_max = shape.geometry_parameters.w;
        const float radius = shape.geometry_parameters.x;
        for (uint x = 0; x != 16; ++x)
            for (uint sign_x = 0; sign_x != 2; ++sign_x) {
                const float phi = ((sign_x == 0 ? -gauss_nodes[x] : gauss_nodes[x]) * 0.5 + 0.5) * phi_max;
                for (uint y = 0; y != 16; ++y)
                    for (uint sign_y = 0; sign_y != 2; ++sign_y) {
                        const float height = lerp(shape.geometry_parameters.y, shape.geometry_parameters.z, (sign_y == 0 ? -gauss_nodes[y] : gauss_nodes[y]) * 0.5 + 0.5);
                        const float radial = sqrt(max(0.0, radius * radius - height * height)) / radius;
                        sum += gauss_weights[x] * gauss_weights[y] * dynamic_area_scale(shape, float3(radial * cos(phi), radial * sin(phi), height / radius));
                    }
            }
        return radius * phi_max * (shape.geometry_parameters.z - shape.geometry_parameters.y) * 0.25 * sum;
    }
    const float3 row_0 = shape.transform_row_0.xyz;
    const float3 row_1 = shape.transform_row_1.xyz;
    const float3 row_2 = shape.transform_row_2.xyz;
    const float3 column_0 = float3(row_0.x, row_1.x, row_2.x);
    const float3 column_1 = float3(row_0.y, row_1.y, row_2.y);
    const float3 column_2 = float3(row_0.z, row_1.z, row_2.z);
    const float normal_x = length(cross(column_1, column_2));
    const float normal_y = length(cross(column_2, column_0));
    const float normal_z = length(cross(column_0, column_1));
    if (geometry == geometry_disk) return 0.5 * shape.geometry_parameters.w * (shape.geometry_parameters.y * shape.geometry_parameters.y - shape.geometry_parameters.z * shape.geometry_parameters.z) * normal_z;
    if (geometry == geometry_box) {
        const float3 extent = shape.position_1.xyz - shape.position_0_and_area.xyz;
        return 2.0 * (extent.x * extent.y * normal_z + extent.x * extent.z * normal_y + extent.y * extent.z * normal_x);
    }
    if (geometry == geometry_rectangle) return (shape.position_1.x - shape.position_0_and_area.x) * (shape.position_1.y - shape.position_0_and_area.y) * normal_z;
    float sum = 0.0;
    for (uint x = 0; x != 16; ++x)
        for (uint sign_x = 0; sign_x != 2; ++sign_x) {
            const float phi = ((sign_x == 0 ? -gauss_nodes[x] : gauss_nodes[x]) * 0.5 + 0.5) * shape.geometry_parameters.w;
            sum += gauss_weights[x] * dynamic_area_scale(shape, float3(cos(phi), sin(phi), 0.0));
        }
    return shape.geometry_parameters.x * (shape.geometry_parameters.z - shape.geometry_parameters.y) * shape.geometry_parameters.w * 0.5 * sum;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void finalize_dynamic_area_light_range() {
    RWStructuredBuffer<PathLight> lights = area_push.lights;
    RWStructuredBuffer<PathLightShape> shapes = area_push.shapes;
    float total_area = 0.0;
    for (uint element = 0; element != area_push.range.z; ++element) {
        const PathLight light = lights[area_push.range.y + element];
        total_area += dynamic_shape_world_area(shapes[light.references.x]);
    }
    float scale = area_push.emission_parameters.x;
    if (area_push.emission_parameters.y > 0.0)
        scale *= area_push.emission_parameters.y / (area_push.emission_parameters.z * total_area * dynamic_light_pi * area_push.emission_parameters.w);
    for (uint element = 0; element != area_push.range.w; ++element) {
        const uint light_index = area_push.range.y + element;
        PathLight light = lights[light_index];
        if (element >= area_push.range.z) {
            light.position_and_scale.w = 0.0;
            light.selection = float4(0.0);
            lights[light_index] = light;
            continue;
        }
        const float area = dynamic_shape_world_area(shapes[light.references.x]);
        light.position_and_scale.w = scale;
        light.selection.x = dynamic_light_pi * area_push.emission_parameters.z * area * scale * area_push.selection_parameters.x;
        light.selection.w = dynamic_light_pi * area * scale * area_push.selection_parameters.y;
        lights[light_index] = light;
    }
}

struct DynamicLightSelectionPushData {
    DescriptorHandle<RWStructuredBuffer<PathLight>> lights;
    uint light_count;
    uint sampler_kind;
}

[vk::push_constant]
uniform DynamicLightSelectionPushData selection_push;

[shader("compute")]
[numthreads(1, 1, 1)]
void rebuild_dynamic_light_selection() {
    if (selection_push.sampler_kind == 2) return;
    RWStructuredBuffer<PathLight> lights = selection_push.lights;
    uint eligible_count = 0;
    float total_power = 0.0;
    for (uint index = 0; index != selection_push.light_count; ++index) {
        const PathLight light = lights[index];
        const bool eligible = (light.identifiers.y & dynamic_light_slot) == 0 || (light.identifiers.y & dynamic_light_active) != 0;
        if (!eligible) continue;
        ++eligible_count;
        total_power += light.selection.x;
    }
    if (eligible_count == 0) {
        for (uint index = 0; index != selection_push.light_count; ++index) {
            PathLight light = lights[index];
            light.selection.yz = float2(0.0);
            lights[index] = light;
        }
        return;
    }
    const bool zero_power = total_power == 0.0;
    float cumulative = 0.0;
    for (uint index = 0; index != selection_push.light_count; ++index) {
        PathLight light = lights[index];
        const bool eligible = (light.identifiers.y & dynamic_light_slot) == 0 || (light.identifiers.y & dynamic_light_active) != 0;
        light.selection.y = eligible ? selection_push.sampler_kind == 0 || zero_power ? 1.0 / float(eligible_count) : light.selection.x / total_power : 0.0;
        cumulative += light.selection.y;
        light.selection.z = cumulative;
        lights[index] = light;
    }
}

struct DynamicLightBvhPushData {
    DescriptorHandle<StructuredBuffer<PathLight>> lights;
    DescriptorHandle<StructuredBuffer<PathLightShape>> shapes;
    DescriptorHandle<RWStructuredBuffer<PathLightBvhNode>> nodes;
    DescriptorHandle<RWStructuredBuffer<uint>> counters;
    uint node_count;
}

[vk::push_constant]
uniform DynamicLightBvhPushData bvh_push;

PathLightBvhNode dynamic_light_leaf(PathLight light, PathLightShape shape, uint4 metadata) {
    float3 minimum = float3(3.402823466e+38);
    float3 maximum = float3(-3.402823466e+38);
    float3 direction = float3(0.0, 0.0, 1.0);
    float cos_theta_o = -1.0;
    const uint kind = light.identifiers.x;
    if (kind == light_point || kind == light_spot) {
        minimum = light.position_and_scale.xyz;
        maximum = minimum;
        if (kind == light_spot) {
            direction = normalize(float3(light.transform_row_0.z, light.transform_row_1.z, light.transform_row_2.z));
            cos_theta_o = light.parameters.x;
        }
    }
    else if (shape.identifiers.z == geometry_triangle) {
        minimum = min(shape.position_0_and_area.xyz, min(shape.position_1.xyz, shape.position_2.xyz));
        maximum = max(shape.position_0_and_area.xyz, max(shape.position_1.xyz, shape.position_2.xyz));
        direction = normalize(cross(shape.position_1.xyz - shape.position_0_and_area.xyz, shape.position_2.xyz - shape.position_0_and_area.xyz));
        if (shape.identifiers.y != 0) direction = -direction;
        cos_theta_o = 1.0;
    }
    else {
        float3 local_minimum;
        float3 local_maximum;
        if (shape.identifiers.z == geometry_sphere || shape.identifiers.z == geometry_cylinder) {
            local_minimum = float3(-shape.geometry_parameters.x, -shape.geometry_parameters.x, shape.geometry_parameters.y);
            local_maximum = float3(shape.geometry_parameters.x, shape.geometry_parameters.x, shape.geometry_parameters.z);
        }
        else if (shape.identifiers.z == geometry_disk) {
            local_minimum = float3(-shape.geometry_parameters.y, -shape.geometry_parameters.y, shape.geometry_parameters.x);
            local_maximum = float3(shape.geometry_parameters.y, shape.geometry_parameters.y, shape.geometry_parameters.x);
        }
        else if (shape.identifiers.z == geometry_box) {
            local_minimum = shape.position_0_and_area.xyz;
            local_maximum = shape.position_1.xyz;
        }
        else {
            local_minimum = float3(shape.position_0_and_area.xy, 0.0);
            local_maximum = float3(shape.position_1.xy, 0.0);
        }
        [unroll]
        for (uint corner = 0; corner != 8; ++corner) {
            const float3 point = float3((corner & 1) == 0 ? local_minimum.x : local_maximum.x, (corner & 2) == 0 ? local_minimum.y : local_maximum.y, (corner & 4) == 0 ? local_minimum.z : local_maximum.z);
            const float3 world = float3(dot(shape.transform_row_0, float4(point, 1.0)), dot(shape.transform_row_1, float4(point, 1.0)), dot(shape.transform_row_2, float4(point, 1.0)));
            minimum = min(minimum, world);
            maximum = max(maximum, world);
        }
        if (shape.identifiers.z == geometry_disk || shape.identifiers.z == geometry_rectangle) {
            const float3 axis_x = float3(shape.transform_row_0.x, shape.transform_row_1.x, shape.transform_row_2.x);
            const float3 axis_y = float3(shape.transform_row_0.y, shape.transform_row_1.y, shape.transform_row_2.y);
            direction = normalize(cross(axis_x, axis_y));
            if (shape.identifiers.y != 0) direction = -direction;
            cos_theta_o = 1.0;
        }
    }
    if (light.selection.w == 0.0) {
        minimum = float3(0.0);
        maximum = float3(0.0);
    }
    return {float4(minimum, max(light.selection.w, 0.0)), float4(maximum, cos_theta_o), float4(direction, 0.0), uint4(metadata.x, metadata.y, (light.identifiers.y & light_two_sided) != 0, metadata.w)};
}

PathLightBvhNode union_dynamic_light_bounds(PathLightBvhNode first, PathLightBvhNode second, uint4 metadata) {
    if (first.bounds_minimum_and_phi.w == 0.0) { first.metadata = metadata; return first.bounds_minimum_and_phi.w == 0.0 && second.bounds_minimum_and_phi.w != 0.0 ? PathLightBvhNode(second.bounds_minimum_and_phi, second.bounds_maximum_and_cos_theta_o, second.direction_and_cos_theta_e, uint4(metadata.x, metadata.y, second.metadata.z, metadata.w)) : first; }
    if (second.bounds_minimum_and_phi.w == 0.0) { first.metadata = uint4(metadata.x, metadata.y, first.metadata.z, metadata.w); return first; }
    PathLightBvhNode result;
    result.bounds_minimum_and_phi = float4(min(first.bounds_minimum_and_phi.xyz, second.bounds_minimum_and_phi.xyz), first.bounds_minimum_and_phi.w + second.bounds_minimum_and_phi.w);
    result.bounds_maximum_and_cos_theta_o.xyz = max(first.bounds_maximum_and_cos_theta_o.xyz, second.bounds_maximum_and_cos_theta_o.xyz);
    result.direction_and_cos_theta_e.w = min(first.direction_and_cos_theta_e.w, second.direction_and_cos_theta_e.w);
    result.metadata = uint4(metadata.x, metadata.y, first.metadata.z | second.metadata.z, metadata.w);
    const float theta_first = acos(clamp(first.bounds_maximum_and_cos_theta_o.w, -1.0, 1.0));
    const float theta_second = acos(clamp(second.bounds_maximum_and_cos_theta_o.w, -1.0, 1.0));
    const float theta_direction = acos(clamp(dot(first.direction_and_cos_theta_e.xyz, second.direction_and_cos_theta_e.xyz), -1.0, 1.0));
    if (min(theta_direction + theta_second, dynamic_light_pi) <= theta_first) {
        result.direction_and_cos_theta_e.xyz = first.direction_and_cos_theta_e.xyz;
        result.bounds_maximum_and_cos_theta_o.w = first.bounds_maximum_and_cos_theta_o.w;
        return result;
    }
    if (min(theta_direction + theta_first, dynamic_light_pi) <= theta_second) {
        result.direction_and_cos_theta_e.xyz = second.direction_and_cos_theta_e.xyz;
        result.bounds_maximum_and_cos_theta_o.w = second.bounds_maximum_and_cos_theta_o.w;
        return result;
    }
    const float theta = (theta_first + theta_direction + theta_second) * 0.5;
    const float3 axis = cross(first.direction_and_cos_theta_e.xyz, second.direction_and_cos_theta_e.xyz);
    if (theta >= dynamic_light_pi || length(axis) == 0.0) {
        result.direction_and_cos_theta_e.xyz = float3(0.0, 0.0, 1.0);
        result.bounds_maximum_and_cos_theta_o.w = -1.0;
        return result;
    }
    const float3 normalized_axis = normalize(axis);
    const float rotation = theta - theta_first;
    const float3 first_direction = first.direction_and_cos_theta_e.xyz;
    result.direction_and_cos_theta_e.xyz = normalize(first_direction * cos(rotation) + cross(normalized_axis, first_direction) * sin(rotation) + normalized_axis * dot(normalized_axis, first_direction) * (1.0 - cos(rotation)));
    result.bounds_maximum_and_cos_theta_o.w = cos(theta);
    return result;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void rebuild_dynamic_light_bvh(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint node_index = dispatch_thread_id.x;
    if (node_index >= bvh_push.node_count) return;
    RWStructuredBuffer<PathLightBvhNode> nodes = bvh_push.nodes;
    const PathLightBvhNode topology = nodes[node_index];
    if (topology.metadata.y == 0) return;
    StructuredBuffer<PathLight> lights = bvh_push.lights;
    StructuredBuffer<PathLightShape> shapes = bvh_push.shapes;
    const PathLight light = lights[topology.metadata.x];
    const uint shape_index = light.identifiers.x == light_diffuse_area ? light.references.x : 0;
    nodes[node_index] = dynamic_light_leaf(light, shapes[shape_index], topology.metadata);
    DeviceMemoryBarrier();
    uint current = node_index;
    while (nodes[current].metadata.w != dynamic_light_invalid) {
        const uint parent = nodes[current].metadata.w;
        RWStructuredBuffer<uint> counters = bvh_push.counters;
        uint previous;
        InterlockedAdd(counters[parent], 1, previous);
        if (previous == 0) return;
        const PathLightBvhNode parent_topology = nodes[parent];
        nodes[parent] = union_dynamic_light_bounds(nodes[parent + 1], nodes[parent_topology.metadata.x], parent_topology.metadata);
        DeviceMemoryBarrier();
        current = parent;
    }
}
