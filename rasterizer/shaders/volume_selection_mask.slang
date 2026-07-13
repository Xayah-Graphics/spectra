#include "volume_selection.slangh"

[shader("fragment")]
float4 fragment_main(VertexOutput input, bool isFrontFace : SV_IsFrontFace) : SV_Target {
    if (!isFrontFace || raymarch_selection_opacity(input) <= 0.025) discard;
    return volume.color;
}
