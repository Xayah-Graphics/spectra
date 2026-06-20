struct VolumeColorUploadPushConstants {
    uint4 dimensions;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> sourceValues;
[[vk::binding(1, 0)]] RWTexture3D<float4> outputTexture;
[[vk::push_constant]] ConstantBuffer<VolumeColorUploadPushConstants> volumeColorUpload;

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
    uint dimX = volumeColorUpload.dimensions.x;
    uint dimY = volumeColorUpload.dimensions.y;
    uint dimZ = volumeColorUpload.dimensions.z;
    if (dispatchThreadID.x >= dimX || dispatchThreadID.y >= dimY || dispatchThreadID.z >= dimZ) return;

    uint linearIndex = dispatchThreadID.x + dimX * (dispatchThreadID.y + dimY * dispatchThreadID.z);
    uint sourceIndex = volumeColorUpload.dimensions.w == 1u ? encode_morton3d(dispatchThreadID) : linearIndex;
    uint sourceValueIndex = sourceIndex * 3u;
    float3 color = float3(sourceValues[sourceValueIndex + 0u], sourceValues[sourceValueIndex + 1u], sourceValues[sourceValueIndex + 2u]);
    outputTexture[int3(dispatchThreadID)] = float4(max(color, float3(0.0, 0.0, 0.0)), 1.0);
}
