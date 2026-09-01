module openvdb_volume;

#define PNANOVDB_HLSL
#include <nanovdb/PNanoVDB.h>

float openvdb_float_value(
    StructuredBuffer<uint> buffer,
    inout pnanovdb_readaccessor_t accessor,
    int3 coordinate) {
    const pnanovdb_address_t address =
        pnanovdb_readaccessor_get_value_address(
            PNANOVDB_GRID_TYPE_FLOAT,
            buffer,
            accessor,
            coordinate);
    return pnanovdb_read_float(buffer, address);
}

float3 openvdb_float3_value(
    StructuredBuffer<uint> buffer,
    inout pnanovdb_readaccessor_t accessor,
    int3 coordinate) {
    const pnanovdb_address_t address =
        pnanovdb_readaccessor_get_value_address(
            PNANOVDB_GRID_TYPE_VEC3F,
            buffer,
            accessor,
            coordinate);
    return pnanovdb_read_vec3(buffer, address);
}

public float sample_openvdb_float(
    DescriptorHandle<StructuredBuffer<uint>> descriptor,
    float3 position) {
    StructuredBuffer<uint> buffer = descriptor;
    pnanovdb_grid_handle_t grid = {pnanovdb_address_null()};
    const float3 index_position =
        pnanovdb_grid_world_to_indexf(
            buffer,
            grid,
            position);
    const int3 minimum = int3(floor(index_position));
    const float3 fraction = index_position - float3(minimum);
    pnanovdb_readaccessor_t accessor;
    pnanovdb_readaccessor_init(
        accessor,
        pnanovdb_tree_get_root(
            buffer,
            pnanovdb_grid_get_tree(buffer, grid)));
    const float v000 = openvdb_float_value(buffer, accessor, minimum);
    const float v100 = openvdb_float_value(buffer, accessor, minimum + int3(1, 0, 0));
    const float v010 = openvdb_float_value(buffer, accessor, minimum + int3(0, 1, 0));
    const float v110 = openvdb_float_value(buffer, accessor, minimum + int3(1, 1, 0));
    const float v001 = openvdb_float_value(buffer, accessor, minimum + int3(0, 0, 1));
    const float v101 = openvdb_float_value(buffer, accessor, minimum + int3(1, 0, 1));
    const float v011 = openvdb_float_value(buffer, accessor, minimum + int3(0, 1, 1));
    const float v111 = openvdb_float_value(buffer, accessor, minimum + int3(1, 1, 1));
    return lerp(
        lerp(lerp(v000, v100, fraction.x), lerp(v010, v110, fraction.x), fraction.y),
        lerp(lerp(v001, v101, fraction.x), lerp(v011, v111, fraction.x), fraction.y),
        fraction.z);
}

public float3 sample_openvdb_float3(
    DescriptorHandle<StructuredBuffer<uint>> descriptor,
    float3 position) {
    StructuredBuffer<uint> buffer = descriptor;
    pnanovdb_grid_handle_t grid = {pnanovdb_address_null()};
    const float3 index_position =
        pnanovdb_grid_world_to_indexf(
            buffer,
            grid,
            position);
    const int3 minimum = int3(floor(index_position));
    const float3 fraction = index_position - float3(minimum);
    pnanovdb_readaccessor_t accessor;
    pnanovdb_readaccessor_init(
        accessor,
        pnanovdb_tree_get_root(
            buffer,
            pnanovdb_grid_get_tree(buffer, grid)));
    const float3 v000 = openvdb_float3_value(buffer, accessor, minimum);
    const float3 v100 = openvdb_float3_value(buffer, accessor, minimum + int3(1, 0, 0));
    const float3 v010 = openvdb_float3_value(buffer, accessor, minimum + int3(0, 1, 0));
    const float3 v110 = openvdb_float3_value(buffer, accessor, minimum + int3(1, 1, 0));
    const float3 v001 = openvdb_float3_value(buffer, accessor, minimum + int3(0, 0, 1));
    const float3 v101 = openvdb_float3_value(buffer, accessor, minimum + int3(1, 0, 1));
    const float3 v011 = openvdb_float3_value(buffer, accessor, minimum + int3(0, 1, 1));
    const float3 v111 = openvdb_float3_value(buffer, accessor, minimum + int3(1, 1, 1));
    return lerp(
        lerp(lerp(v000, v100, fraction.x), lerp(v010, v110, fraction.x), fraction.y),
        lerp(lerp(v001, v101, fraction.x), lerp(v011, v111, fraction.x), fraction.y),
        fraction.z);
}
