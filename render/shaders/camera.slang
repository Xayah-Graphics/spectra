module camera;

import sampling;

public struct CameraParameters {
    public float4 transform_row_0;
    public float4 transform_row_1;
    public float4 transform_row_2;
    public float4 inverse_row_0;
    public float4 inverse_row_1;
    public float4 inverse_row_2;
    public float4 screen;
    public float4 lens;
    public uint4 metadata;
    public float tan_half_vertical_fov;
}

float3 camera_to_world_point(
    CameraParameters camera,
    float3 point) {
    return float3(
        dot(
            camera.transform_row_0,
            float4(point, 1.0)),
        dot(
            camera.transform_row_1,
            float4(point, 1.0)),
        dot(
            camera.transform_row_2,
            float4(point, 1.0)));
}

float3 camera_to_world_vector(
    CameraParameters camera,
    float3 vector) {
    return float3(
        dot(
            camera.transform_row_0.xyz,
            vector),
        dot(
            camera.transform_row_1.xyz,
            vector),
        dot(
            camera.transform_row_2.xyz,
            vector));
}

public float3 world_to_camera_point(
    CameraParameters camera,
    float3 point) {
    return float3(
        dot(
            camera.inverse_row_0,
            float4(point, 1.0)),
        dot(
            camera.inverse_row_1,
            float4(point, 1.0)),
        dot(
            camera.inverse_row_2,
            float4(point, 1.0)));
}

public float3 world_to_camera_vector(
    CameraParameters camera,
    float3 vector) {
    return float3(
        dot(
            camera.inverse_row_0.xyz,
            vector),
        dot(
            camera.inverse_row_1.xyz,
            vector),
        dot(
            camera.inverse_row_2.xyz,
            vector));
}

public RayDesc make_primary_ray(
    uint2 pixel,
    uint2 extent,
    float2 pixel_sample,
    float2 lens_sample,
    CameraParameters camera) {
    const float2 raster =
        (
            float2(pixel) +
            pixel_sample
        ) /
        float2(extent);
    const float2 screen =
        float2(
            lerp(
                camera.screen.x,
                camera.screen.z,
                raster.x),
            lerp(
                camera.screen.w,
                camera.screen.y,
                raster.y));
    float3 origin;
    float3 direction;
    if (camera.metadata.x == 0) {
        origin = float3(0.0);
        direction =
            normalize(
                float3(
                    screen *
                        camera
                            .tan_half_vertical_fov,
                    -1.0));
    }
    else {
        origin =
            float3(
                screen,
                0.0);
        direction =
            float3(
                0.0,
                0.0,
                -1.0);
    }
    if (camera.lens.x != 0.0) {
        const float2 lens =
            camera.lens.x *
            sample_concentric_disk(
                lens_sample);
        const float focus_distance =
            camera.lens.y /
            abs(direction.z);
        const float3 focus =
            origin +
            focus_distance *
                direction;
        origin =
            float3(
                lens,
                0.0);
        direction =
            normalize(
                focus -
                origin);
    }
    RayDesc ray;
    ray.Origin =
        camera_to_world_point(
            camera,
            origin);
    ray.TMin = camera.lens.z;
    ray.Direction =
        normalize(
            camera_to_world_vector(
                camera,
                direction));
    ray.TMax = camera.lens.w;
    return ray;
}
