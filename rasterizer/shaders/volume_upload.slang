#include "volume_upload.slangh"

struct VolumeUploadPushConstants {
    uint4 dimensions;
    uint4 format;
};

[[vk::binding(0, 0)]] StructuredBuffer<float> sourceValues;
[[vk::binding(1, 0)]] RWTexture3D<float> outputTexture;
[[vk::push_constant]] ConstantBuffer<VolumeUploadPushConstants> volumeUpload;

[shader("compute")]
[numthreads(8, 8, 4)]
void compute_main(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint dimX = volumeUpload.dimensions.x;
    uint dimY = volumeUpload.dimensions.y;
    uint dimZ = volumeUpload.dimensions.z;
    if (dispatchThreadID.x >= dimX || dispatchThreadID.y >= dimY || dispatchThreadID.z >= dimZ) return;

    uint sourceIndex = volume_source_index(dispatchThreadID, volumeUpload.dimensions.xyz, volumeUpload.dimensions.w == 1u);
    outputTexture[int3(dispatchThreadID)] = sourceValues[sourceIndex];
}
