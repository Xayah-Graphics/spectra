#include "mesh_selection.slangh"

[shader("fragment")]
uint fragment_main(VertexOutput input) : SV_Target {
    apply_mesh_alpha_mask(input);
    return selection.flags.x;
}
