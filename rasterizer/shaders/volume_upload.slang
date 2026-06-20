struct VolumeUploadPushConstants {
    uint4 dimensions;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> sourceValues;
[[vk::binding(1, 0)]] RWTexture3D<float> outputTexture;
[[vk::push_constant]] ConstantBuffer<VolumeUploadPushConstants> volumeUpload;

uint part1by2(uint value) {
    value &= 0x000003ffu;
    value = (value | (value << 16u)) & 0x030000ffu;
    value = (value | (value << 8u)) & 0x0300f00fu;
    value = (value | (value << 4u)) & 0x030c30c3u;
    value = (value | (value << 2u)) & 0x09249249u;
    return value;
}

uint encode_morton3d(uint3 value) {
    return part1by2(value.x) | (part1by2(value.y) << 1u) | (part1by2(value.z) << 2u);
}

[shader("compute")]
[numthreads(8, 8, 4)]
void compute_main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint dimX = volumeUpload.dimensions.x;
    uint dimY = volumeUpload.dimensions.y;
    uint dimZ = volumeUpload.dimensions.z;
    if (dispatchThreadID.x >= dimX || dispatchThreadID.y >= dimY || dispatchThreadID.z >= dimZ) return;

    uint linearIndex = dispatchThreadID.x + dimX * (dispatchThreadID.y + dimY * dispatchThreadID.z);
    uint sourceIndex = volumeUpload.dimensions.w == 1u ? encode_morton3d(dispatchThreadID) : linearIndex;
    outputTexture[int3(dispatchThreadID)] = sourceValues[sourceIndex];
}
