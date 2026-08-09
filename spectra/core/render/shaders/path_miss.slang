module path_miss;

import path_queue;

[shader("miss")]
void radiance_miss(in out SurfacePayload payload) {
    payload.hit = 0;
}

[shader("miss")]
void shadow_miss(in out TransmittancePayload payload) {
    payload.hit = 0;
}
