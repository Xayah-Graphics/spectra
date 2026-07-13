#include "volume_upload.slangh"

struct VolumeVectorUploadPushConstants {
    uint4 dimensions;
    uint4 format;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> sourceValues;
[[vk::binding(1, 0)]] RWTexture3D<float4> outputTexture;
[[vk::push_constant]] ConstantBuffer<VolumeVectorUploadPushConstants> volumeVectorUpload;

[shader("compute")]
[numthreads(8, 8, 4)]
void compute_main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint dimX = volumeVectorUpload.dimensions.x;
    uint dimY = volumeVectorUpload.dimensions.y;
    uint dimZ = volumeVectorUpload.dimensions.z;
    if (dispatchThreadID.x >= dimX || dispatchThreadID.y >= dimY || dispatchThreadID.z >= dimZ) return;

    uint sourceIndex = volume_source_index(dispatchThreadID, volumeVectorUpload.dimensions.xyz, volumeVectorUpload.dimensions.w == 1u);
    uint componentCount = volumeVectorUpload.format.x;
    uint sourceValueIndex = sourceIndex * componentCount;
    float4 value = float4(0.0, 0.0, 0.0, 1.0);
    if (componentCount > 0u) value.x = sourceValues[sourceValueIndex + 0u];
    if (componentCount > 1u) value.y = sourceValues[sourceValueIndex + 1u];
    if (componentCount > 2u) value.z = sourceValues[sourceValueIndex + 2u];
    if (componentCount > 3u) value.w = sourceValues[sourceValueIndex + 3u];
    outputTexture[int3(dispatchThreadID)] = value;
}
