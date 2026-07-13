#include "point_cloud_selection.slangh"

[shader("fragment")]
uint fragment_main(VertexOutput input) : SV_Target {
    apply_point_cloud_disc_mask(input);
    return selection.flags.x;
}
