struct VoxelGridCompactionPushConstants {
    uint4 dimensions;
    uint4 counts;
};

[[vk::binding(0, 0)]] RWStructuredBuffer<uint> compactedCellIndices;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> occupiedCounter;
[[vk::binding(2, 0)]] StructuredBuffer<uint> bitfieldWords;
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> indirectCommand;
[[vk::push_constant]] ConstantBuffer<VoxelGridCompactionPushConstants> voxelGrid;

uint decode_morton_axis_bits(uint value) {
    value &= 0x09249249u;
    value = (value ^ (value >> 2u)) & 0x030c30c3u;
    value = (value ^ (value >> 4u)) & 0x0300f00fu;
    value = (value ^ (value >> 8u)) & 0xff0000ffu;
    value = (value ^ (value >> 16u)) & 0x000003ffu;
    return value;
}

uint3 decode_morton3d(uint value) {
    return uint3(decode_morton_axis_bits(value), decode_morton_axis_bits(value >> 1u), decode_morton_axis_bits(value >> 2u));
}

bool decode_cell_index(uint sourceIndex, out uint linearIndex) {
    uint dimX = voxelGrid.dimensions.x;
    uint dimY = voxelGrid.dimensions.y;
    uint dimZ = voxelGrid.dimensions.z;
    uint cellCount = voxelGrid.counts.y;
    if (sourceIndex >= cellCount) return false;
    if (voxelGrid.dimensions.w == 0u) {
        linearIndex = sourceIndex;
        return true;
    }

    uint3 cell = decode_morton3d(sourceIndex);
    if (cell.x >= dimX || cell.y >= dimY || cell.z >= dimZ) return false;
    linearIndex = cell.x + dimX * (cell.y + dimY * cell.z);
    return true;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void compute_main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint byteIndex = dispatchThreadID.x;
    uint bitfieldByteCount = voxelGrid.counts.x;
    if (byteIndex >= bitfieldByteCount) return;

    uint word = bitfieldWords[byteIndex >> 2u];
    uint byteValue = (word >> ((byteIndex & 3u) * 8u)) & 0xffu;
    if (byteValue == 0u) return;

    for (uint bitIndex = 0u; bitIndex < 8u; ++bitIndex) {
        if ((byteValue & (1u << bitIndex)) == 0u) continue;
        uint linearIndex = 0u;
        if (!decode_cell_index(byteIndex * 8u + bitIndex, linearIndex)) continue;

        uint writeIndex = 0u;
        InterlockedAdd(occupiedCounter[0], 1u, writeIndex);
        if (writeIndex >= voxelGrid.counts.y) continue;
        compactedCellIndices[writeIndex] = linearIndex;
        InterlockedMax(indirectCommand[1], writeIndex + 1u);
    }
}
