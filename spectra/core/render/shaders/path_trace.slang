module path_trace;

import path_light;
import path_interaction;
import path_medium;
import path_phase;
import path_random;
import path_sampler;
import path_scene;
import path_queue;
import path_sampling;

[[vk::binding(0, 0)]]
RaytracingAccelerationStructure acceleration_structure;

[vk::push_constant]
uniform WavefrontPushData push_data;

uint primitive_material(
    PathSceneBindings bindings,
    PathPrimitive primitive,
    uint primitive_index) {
    if (primitive.material_binding.y == 0)
        return primitive.material;
    StructuredBuffer<uint> face_materials =
        bindings.face_materials;
    return
        face_materials[
            primitive.material_binding.x +
            primitive_index];
}

uint boundary_medium(
    PathPrimitive primitive,
    uint current_medium,
    bool front_face) {
    if (
        primitive.media.x ==
        primitive.media.y)
        return current_medium;
    return
        front_face
            ? primitive.media.x
            : primitive.media.y;
}

[shader("raygeneration")]
void surface_ray_generation() {
    StructuredBuffer<WavefrontBindings>
        binding_buffer =
            push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontParameters>
        parameter_buffer =
            push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
    StructuredBuffer<PathSceneBindings>
        scene_binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        scene_binding_buffer[0];
    const uint queue_index =
        DispatchRaysIndex().x;
    RWStructuredBuffer<uint> queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_0
            : bindings.ray_queue_1;
    RWStructuredBuffer<uint> write_queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_1
            : bindings.ray_queue_0;
    const uint write_queue_index =
        1 -
        push_data.read_queue;
    const uint path_id =
        queue[queue_index];
    RWStructuredBuffer<float4> ray_origins =
        bindings.ray_origins;
    RWStructuredBuffer<float4> ray_directions =
        bindings.ray_directions;
    RWStructuredBuffer<float4> throughputs =
        bindings.throughputs;
    RWStructuredBuffer<float4> radiances =
        bindings.radiances;
    RWStructuredBuffer<float4> path_r_u =
        bindings.path_r_u;
    RWStructuredBuffer<float4> path_r_l =
        bindings.path_r_l;
    RWStructuredBuffer<uint> current_media =
        bindings.current_media;
    RWStructuredBuffer<uint> path_flags =
        bindings.path_flags;
    RWStructuredBuffer<float> eta_scales =
        bindings.eta_scales;
    RWStructuredBuffer<float4> wavelengths =
        bindings.wavelengths;
    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    float4 origin =
        ray_origins[path_id];
    float4 direction =
        ray_directions[path_id];
    direction.xyz =
        normalize(
            direction.xyz);
    float4 beta =
        throughputs[path_id];
    float4 radiance =
        radiances[path_id];
    float4 r_u =
        path_r_u[path_id];
    float4 r_l =
        path_r_l[path_id];
    uint medium =
        current_media[path_id];
    const float4 path_wavelengths =
        wavelengths[path_id];
    const uint2 pixel =
        uint2(
            path_id %
                parameters.extent.x,
            path_id /
                parameters.extent.x);
    Pcg32 generator =
        ray_segment_pcg(
            origin.xyz,
            direction.w,
            direction.xyz);
    float distance_sample =
        pcg_float(generator);
    SurfacePayload payload = {};
    while (true) {
        RayDesc ray;
        ray.Origin =
            origin.xyz;
        ray.TMin =
            origin.w;
        ray.Direction =
            direction.xyz;
        ray.TMax =
            direction.w;
        payload = SurfacePayload();
        payload.alpha_origin_x =
            origin.x;
        payload.alpha_origin_y =
            origin.y;
        payload.alpha_origin_z =
            origin.z;
        TraceRay(
            acceleration_structure,
            RAY_FLAG_NONE,
            0xff,
            0,
            0,
            0,
            ray,
            payload);
        if (medium != 0xffffffff) {
            const PathMediumSample sample =
                sample_path_medium(
                    scene_bindings,
                    medium,
                    origin.xyz,
                    direction.xyz,
                    payload.hit != 0
                        ? payload.hit_distance
                        : direction.w,
                    path_wavelengths,
                    push_data.bounce,
                    parameters.max_depth,
                    beta,
                    r_u,
                    r_l,
                    distance_sample,
                    generator);
            beta =
                sample.beta;
            r_u =
                sample.r_u;
            r_l =
                sample.r_l;
            radiance +=
                sample.radiance;
            if (sample.terminated) {
                payload.hit = 2;
                break;
            }
            if (sample.scattered) {
                if (
                    parameters.light_count !=
                    0) {
                    const PathSampledLight
                        sampled_light =
                            sample_light_sampler(
                                scene_bindings,
                                parameters
                                    .light_count,
                                sample.position,
                                float3(0.0),
                                path_sample_1d(
                                    parameters,
                                    pixel,
                                        path_bounce_dimension(
                                            push_data
                                                .bounce,
                                            0)));
                    const PathLightLiSample
                        light_sample =
                            sampled_light.valid
                                ? sample_light_li(
                                      scene_bindings,
                                      sampled_light
                                          .index,
                                      sample.position,
                                      float3(0.0),
                                      path_sample_2d(
                                          parameters,
                                          pixel,
                                              path_bounce_dimension(
                                                  push_data
                                                      .bounce,
                                              1)),
                                      path_wavelengths,
                                      bindings
                                          .texture_evaluation_stack,
                                      queue_index *
                                          scene_bindings
                                              .metadata
                                              .y,
                                      true)
                                : PathLightLiSample();
                    const float phase =
                        light_sample.valid
                            ? henyey_greenstein(
                                  -direction.xyz,
                                  light_sample
                                      .direction,
                                  sample
                                      .anisotropy)
                            : 0.0;
                    if (
                        light_sample.valid &&
                        phase > 0.0) {
                        uint shadow_index;
                        InterlockedAdd(
                            queue_counts[2],
                            1,
                            shadow_index);
                        const float3
                            shadow_target =
                                any(
                                    light_sample
                                            .normal !=
                                    float3(0.0))
                                    ? offset_ray_origin(
                                          light_sample
                                              .position,
                                          dot(
                                              light_sample
                                                  .normal,
                                              -light_sample
                                                   .direction) >
                                                  0.0
                                              ? light_sample
                                                    .normal
                                              : -light_sample
                                                     .normal)
                                    : light_sample
                                          .position;
                        RWStructuredBuffer<uint>
                            shadow_path_ids =
                                bindings
                                    .shadow_path_ids;
                        RWStructuredBuffer<float4>
                            shadow_origins =
                                bindings
                                    .shadow_origins;
                        RWStructuredBuffer<float4>
                            shadow_directions =
                                bindings
                                    .shadow_directions;
                        RWStructuredBuffer<float4>
                            shadow_contributions =
                                bindings
                                    .shadow_contributions;
                        RWStructuredBuffer<float4>
                            shadow_r_p =
                                bindings.shadow_r_p;
                        RWStructuredBuffer<float4>
                            shadow_pdfs =
                                bindings.shadow_pdfs;
                        RWStructuredBuffer<uint>
                            shadow_media =
                                bindings.shadow_media;
                        shadow_path_ids[
                            shadow_index] =
                                path_id;
                        shadow_origins[
                            shadow_index] =
                                float4(
                                    sample.position,
                                    0.0);
                        shadow_directions[
                            shadow_index] =
                                float4(
                                    shadow_target -
                                        sample.position,
                                    0.9999);
                        shadow_contributions[
                            shadow_index] =
                                beta *
                                light_sample
                                    .radiance *
                                phase;
                        shadow_r_p[
                            shadow_index] =
                                r_u;
                        shadow_pdfs[
                            shadow_index] = {
                                sampled_light.pmf *
                                    light_sample.pdf,
                                phase,
                                light_sample.delta
                                    ? 1.0
                                    : 0.0,
                                0.0,
                            };
                        shadow_media[
                            shadow_index] =
                                medium;
                    }
                }
                float phase_pdf;
                const float3 incident =
                    sample_henyey_greenstein(
                        -direction.xyz,
                        sample.anisotropy,
                        path_sample_2d(
                            parameters,
                            pixel,
                            path_bounce_dimension(
                                push_data.bounce,
                                4)),
                        phase_pdf);
                if (phase_pdf == 0.0) {
                    payload.hit = 2;
                    break;
                }
                const float phase =
                    henyey_greenstein(
                        -direction.xyz,
                        incident,
                        sample.anisotropy);
                beta *=
                    phase /
                    phase_pdf;
                r_l =
                    r_u /
                    phase_pdf;
                const RussianRouletteResult
                    roulette =
                        apply_volpath_russian_roulette(
                            beta,
                            r_u,
                            eta_scales[path_id],
                            push_data.bounce,
                            path_sample_1d(
                                parameters,
                                pixel,
                                path_bounce_dimension(
                                    push_data.bounce,
                                    6)));
                beta =
                    roulette.throughput;
                path_flags[path_id] =
                    any_non_specular_bounce;
                bindings
                    .light_context_normals[
                        path_id] =
                            float4(0.0);
                if (
                    roulette
                        .continue_path) {
                    uint next_index;
                    InterlockedAdd(
                        queue_counts[
                            write_queue_index],
                        1,
                        next_index);
                    write_queue[next_index] =
                        path_id;
                }
                origin =
                    float4(
                        sample.position,
                        0.0);
                direction =
                    float4(
                        incident,
                        parameters.camera_lens.w);
                payload.hit = 2;
                break;
            }
        }
        if (payload.hit == 0)
            break;
        StructuredBuffer<PathPrimitive>
            primitives =
                scene_bindings.primitives;
        const PathPrimitive primitive =
            primitives[
                payload.instance_index];
        StructuredBuffer<PathMaterialHeader>
            material_headers =
                scene_bindings
                    .material_headers;
        const uint material_index =
            primitive_material(
                scene_bindings,
                primitive,
                payload.primitive_index);
        if (
            material_headers[
                material_index]
                .kind !=
            material_interface)
            break;
        medium =
            boundary_medium(
                primitive,
                medium,
                payload.front_face != 0);
        RWStructuredBuffer<float4>
            ray_origin_dx =
                bindings.ray_origin_dx;
        RWStructuredBuffer<float4>
            ray_origin_dy =
                bindings.ray_origin_dy;
        RWStructuredBuffer<float4>
            ray_direction_dx =
                bindings.ray_direction_dx;
        RWStructuredBuffer<float4>
            ray_direction_dy =
                bindings.ray_direction_dy;
        if (
            (
                path_flags[path_id] &
                ray_differential_valid
            ) != 0) {
            ray_origin_dx[path_id].xyz +=
                payload.hit_distance *
                ray_direction_dx[
                    path_id]
                    .xyz;
            ray_origin_dy[path_id].xyz +=
                payload.hit_distance *
                ray_direction_dy[
                    path_id]
                    .xyz;
        }
        const float3 position =
            origin.xyz +
            payload.hit_distance *
                direction.xyz;
        origin =
            float4(
                offset_ray_origin(
                    position,
                    dot(
                        payload
                            .geometric_normal,
                        direction.xyz) >
                            0.0
                        ? payload
                              .geometric_normal
                        : -payload
                               .geometric_normal),
                0.0);
        direction.w =
            max(
                0.0,
                direction.w -
                    payload.hit_distance);
        generator =
            ray_segment_pcg(
                origin.xyz,
                direction.w,
                direction.xyz);
        distance_sample =
            pcg_float(generator);
    }
    ray_origins[path_id] =
        origin;
    ray_directions[path_id] =
        direction;
    throughputs[path_id] =
        beta;
    radiances[path_id] =
        radiance;
    path_r_u[path_id] =
        r_u;
    path_r_l[path_id] =
        r_l;
    current_media[path_id] =
        medium;
    RWStructuredBuffer<float4>
        hit_normal_distances =
            bindings.hit_normal_distances;
    RWStructuredBuffer<float4>
        hit_geometric_normal_u =
            bindings.hit_geometric_normal_u;
    RWStructuredBuffer<float4>
        hit_tangent_v =
            bindings.hit_tangent_v;
    RWStructuredBuffer<float4> hit_dpdu =
        bindings.hit_dpdu;
    RWStructuredBuffer<float4> hit_dpdv =
        bindings.hit_dpdv;
    RWStructuredBuffer<float4> hit_dndu =
        bindings.hit_dndu;
    RWStructuredBuffer<float4> hit_dndv =
        bindings.hit_dndv;
    RWStructuredBuffer<uint4> hit_identifiers =
        bindings.hit_identifiers;
    hit_normal_distances[path_id] =
        float4(
            payload.shading_normal,
            payload.hit_distance);
    hit_geometric_normal_u[path_id] =
        float4(
            payload.geometric_normal,
            payload.texture_u);
    hit_tangent_v[path_id] =
        float4(
            payload.tangent,
            payload.texture_v);
    hit_dpdu[path_id] =
        float4(
            payload.dpdu,
            0.0);
    hit_dpdv[path_id] =
        float4(
            payload.dpdv,
            0.0);
    hit_dndu[path_id] =
        float4(
            payload.dndu,
            0.0);
    hit_dndv[path_id] =
        float4(
            payload.dndv,
            0.0);
    hit_identifiers[path_id] =
        uint4(
            payload.instance_index,
            payload.primitive_index,
            payload.front_face,
            payload.hit);
}

[shader("raygeneration")]
void shadow_ray_generation() {
    StructuredBuffer<WavefrontBindings>
        binding_buffer =
            push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<PathSceneBindings>
        scene_binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        scene_binding_buffer[0];
    const uint shadow_index =
        DispatchRaysIndex().x;
    RWStructuredBuffer<uint> shadow_path_ids =
        bindings.shadow_path_ids;
    RWStructuredBuffer<float4> shadow_origins =
        bindings.shadow_origins;
    RWStructuredBuffer<float4> shadow_directions =
        bindings.shadow_directions;
    RWStructuredBuffer<float4> shadow_contributions =
        bindings.shadow_contributions;
    RWStructuredBuffer<float4> shadow_r_p =
        bindings.shadow_r_p;
    RWStructuredBuffer<float4> shadow_pdfs =
        bindings.shadow_pdfs;
    RWStructuredBuffer<uint> shadow_media =
        bindings.shadow_media;
    const uint path_id =
        shadow_path_ids[shadow_index];
    float3 origin =
        shadow_origins[
            shadow_index]
            .xyz;
    const float3 target =
        origin +
        shadow_directions[
            shadow_index]
            .xyz;
    uint medium =
        shadow_media[
            shadow_index];
    const float4 wavelengths =
        bindings.wavelengths[
            path_id];
    Pcg32 generator =
        ray_pcg(
            origin,
            target -
                origin);
    float4 transmittance =
        float4(1.0);
    float4 r_l =
        float4(1.0);
    float4 r_u =
        float4(1.0);
    bool blocked = false;
    while (true) {
        const float3 direction =
            target -
            origin;
        if (
            dot(direction, direction) ==
            0.0)
            break;
        RayDesc ray;
        ray.Origin = origin;
        ray.TMin = 0.0;
        ray.Direction = direction;
        ray.TMax = 0.9999;
        TransmittancePayload payload = {
            origin,
            0.0,
            float3(0.0),
            0xffffffff,
            0xffffffff,
            0,
            0,
            0,
        };
        TraceRay(
            acceleration_structure,
            RAY_FLAG_NONE,
            0xff,
            2,
            0,
            1,
            ray,
            payload);
        if (medium != 0xffffffff) {
            const PathTransmittance
                segment =
                    sample_medium_transmittance(
                        scene_bindings,
                        medium,
                        origin,
                        direction,
                        payload.hit != 0
                            ? payload
                                  .hit_distance
                            : 0.9999,
                        wavelengths,
                        pcg_float(generator),
                        generator);
            transmittance *=
                segment.transmittance;
            r_l *=
                segment.r_l;
            r_u *=
                segment.r_u;
            if (
                !spectrum_nonzero(
                    transmittance)) {
                blocked = true;
                break;
            }
        }
        if (payload.hit == 0)
            break;
        StructuredBuffer<PathPrimitive>
            primitives =
                scene_bindings.primitives;
        const PathPrimitive primitive =
            primitives[
                payload.instance_index];
        StructuredBuffer<PathMaterialHeader>
            material_headers =
                scene_bindings
                    .material_headers;
        const uint material_index =
            primitive_material(
                scene_bindings,
                primitive,
                payload.primitive_index);
        if (
            material_headers[
                material_index]
                .kind !=
            material_interface) {
            blocked = true;
            break;
        }
        medium =
            boundary_medium(
                primitive,
                medium,
                payload.front_face != 0);
        const float3 position =
            origin +
            payload.hit_distance *
                direction;
        origin =
            offset_ray_origin(
                position,
                dot(
                    payload.geometric_normal,
                    direction) >
                        0.0
                    ? payload
                          .geometric_normal
                    : -payload
                           .geometric_normal);
    }
    if (blocked) {
        shadow_contributions[
            shadow_index] =
                float4(0.0);
        return;
    }
    const float4 probabilities =
        shadow_pdfs[
            shadow_index];
    r_l *=
        shadow_r_p[
            shadow_index] *
        probabilities.x;
    r_u *=
        shadow_r_p[
            shadow_index] *
        probabilities.y;
    const float denominator =
        probabilities.z != 0.0
            ? spectrum_average(r_l)
            : spectrum_average(
                  r_l +
                  r_u);
    shadow_contributions[
        shadow_index] =
            denominator != 0.0
                ? shadow_contributions[
                      shadow_index] *
                    transmittance /
                    denominator
                : float4(0.0);
}
