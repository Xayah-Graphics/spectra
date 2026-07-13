#include "point_cloud_selection.slangh"

[shader("fragment")]
float4 fragment_main(VertexOutput input) : SV_Target {
    apply_point_cloud_disc_mask(input);
    return selection.color;
}
