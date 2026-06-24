static const uint MaxViewportDirectLights = 8;

struct CameraUniform {
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float3 cameraPosition;
    float _pad0;
    float3 environmentColor;
    float environmentIntensity;
    float4 lightDirections[MaxViewportDirectLights];
    float4 lightPositions[MaxViewportDirectLights];
    float4 lightColorIntensities[MaxViewportDirectLights];
    uint4 lightCounts;
    float3 cameraForward;
    float _pad1;
    float3 cameraRight;
    float _pad2;
    float3 cameraUp;
    float _pad3;
    float4 viewport;
};

struct VoxelGridPushConstants {
    float4 originCellScale;
    float4 voxelSize;
    float4 color;
    uint4 dimensions;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUniform> camera;
[[vk::binding(0, 1)]] StructuredBuffer<uint> occupiedCellIndices;
[[vk::push_constant]] ConstantBuffer<VoxelGridPushConstants> voxelGrid;

struct VertexOutput {
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color : COLOR0;
};

uint cube_corner_index(uint vertexIndex) {
    switch (vertexIndex % 36u) {
    case 0u: return 0u;
    case 1u: return 1u;
    case 2u: return 2u;
    case 3u: return 0u;
    case 4u: return 2u;
    case 5u: return 3u;
    case 6u: return 4u;
    case 7u: return 6u;
    case 8u: return 5u;
    case 9u: return 4u;
    case 10u: return 7u;
    case 11u: return 6u;
    case 12u: return 0u;
    case 13u: return 4u;
    case 14u: return 5u;
    case 15u: return 0u;
    case 16u: return 5u;
    case 17u: return 1u;
    case 18u: return 1u;
    case 19u: return 5u;
    case 20u: return 6u;
    case 21u: return 1u;
    case 22u: return 6u;
    case 23u: return 2u;
    case 24u: return 2u;
    case 25u: return 6u;
    case 26u: return 7u;
    case 27u: return 2u;
    case 28u: return 7u;
    case 29u: return 3u;
    case 30u: return 3u;
    case 31u: return 7u;
    case 32u: return 4u;
    case 33u: return 3u;
    case 34u: return 4u;
    default: return 0u;
    }
}

float3 cube_corner(uint vertexIndex) {
    switch (cube_corner_index(vertexIndex)) {
    case 1u: return float3(1.0, 0.0, 0.0);
    case 2u: return float3(1.0, 1.0, 0.0);
    case 3u: return float3(0.0, 1.0, 0.0);
    case 4u: return float3(0.0, 0.0, 1.0);
    case 5u: return float3(1.0, 0.0, 1.0);
    case 6u: return float3(1.0, 1.0, 1.0);
    case 7u: return float3(0.0, 1.0, 1.0);
    default: return float3(0.0, 0.0, 0.0);
    }
}

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

uint3 decode_linear_index(uint cellIndex, uint dimX, uint dimY) {
    uint slice = dimX * dimY;
    uint z = cellIndex / slice;
    uint remainder = cellIndex - z * slice;
    uint y = remainder / dimX;
    uint x = remainder - y * dimX;
    return uint3(x, y, z);
}

[shader("vertex")]
VertexOutput vertex_main(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID) {
    uint cellIndex = occupiedCellIndices[instanceIndex];
    uint dimX = max(voxelGrid.dimensions.x, 1u);
    uint dimY = max(voxelGrid.dimensions.y, 1u);
    uint3 decoded = voxelGrid.dimensions.w == 1u ? decode_morton3d(cellIndex) : decode_linear_index(cellIndex, dimX, dimY);

    float3 cell = float3(float(decoded.x), float(decoded.y), float(decoded.z));
    float3 corner = cube_corner(vertexIndex);
    float cellScale = saturate(voxelGrid.originCellScale.w);
    float3 inset = (float3(1.0, 1.0, 1.0) - float3(cellScale, cellScale, cellScale)) * 0.5;
    float3 local = voxelGrid.originCellScale.xyz + (cell + inset + corner * cellScale) * voxelGrid.voxelSize.xyz;
    float4 world = float4(local, 1.0);

    VertexOutput output;
    output.position = mul(world, camera.worldToClip);
    output.color = voxelGrid.color;
    return output;
}

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    return input.color;
}
