module overlay_mask;

struct MaskVertex {
    float4 position : SV_Position;
    float4 color : COLOR0;
}

[shader("fragment")]
float4 overlay_mask(MaskVertex input) : SV_Target0 {
    return input.color;
}
