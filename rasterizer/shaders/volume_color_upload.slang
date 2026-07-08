struct VolumeVectorUploadPushConstants {
    uint4 dimensions;
    uint4 format;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> sourceValues;
[[vk::binding(1, 0)]] RWTexture3D<float4> outputTexture;
[[vk::push_constant]] ConstantBuffer<VolumeVectorUploadPushConstants> volumeVectorUpload;

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
    uint dimX = volumeVectorUpload.dimensions.x;
    uint dimY = volumeVectorUpload.dimensions.y;
    uint dimZ = volumeVectorUpload.dimensions.z;
    if (dispatchThreadID.x >= dimX || dispatchThreadID.y >= dimY || dispatchThreadID.z >= dimZ) return;

    uint linearIndex = dispatchThreadID.x + dimX * (dispatchThreadID.y + dimY * dispatchThreadID.z);
    uint sourceIndex = volumeVectorUpload.dimensions.w == 1u ? encode_morton3d(dispatchThreadID) : linearIndex;
    uint componentCount = volumeVectorUpload.format.x;
    uint sourceValueIndex = sourceIndex * componentCount;
    float4 value = float4(0.0, 0.0, 0.0, 1.0);
    if (componentCount > 0u) value.x = sourceValues[sourceValueIndex + 0u];
    if (componentCount > 1u) value.y = sourceValues[sourceValueIndex + 1u];
    if (componentCount > 2u) value.z = sourceValues[sourceValueIndex + 2u];
    if (componentCount > 3u) value.w = sourceValues[sourceValueIndex + 3u];
    outputTexture[int3(dispatchThreadID)] = value;
}
