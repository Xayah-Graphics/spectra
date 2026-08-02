module wavefront;

import camera;
import path_bxdf;
import path_filter;
import path_interaction;
import path_light;
import path_material;
import path_medium;
import path_sampler;
import path_scene;
import queue;
import sampling;
import spectrum;

[vk::push_constant]
uniform WavefrontPushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void generate_camera_rays(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontParameters> parameter_buffer =
        push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
    const uint path_count =
        parameters.extent.x *
        parameters.extent.y;
    const uint path_id =
        dispatch_thread_id.x;
    if (path_id >= path_count) return;
    const uint2 pixel =
        uint2(
            path_id % parameters.extent.x,
            path_id / parameters.extent.x);
    RWStructuredBuffer<uint4>
        gbuffer_sample_identity_1 =
            bindings
                .gbuffer_sample_identity_1;
    gbuffer_sample_identity_1[
        path_id] =
        uint4(0);
    if (
        pixel.x <
            parameters.film_bounds.x ||
        pixel.y <
            parameters.film_bounds.y ||
        pixel.x >=
            parameters.film_bounds.z ||
        pixel.y >=
            parameters.film_bounds.w) {
        RWStructuredBuffer<float4> radiances =
            bindings.radiances;
        RWStructuredBuffer<float> filter_weights =
            bindings.filter_weights;
        radiances[path_id] =
            float4(0.0);
        filter_weights[path_id] = 0.0;
        return;
    }
    const CameraParameters camera =
        CameraParameters(
            parameters.camera_transform_row_0,
            parameters.camera_transform_row_1,
            parameters.camera_transform_row_2,
            parameters.camera_inverse_row_0,
            parameters.camera_inverse_row_1,
            parameters.camera_inverse_row_2,
            parameters.camera_screen,
            parameters.camera_lens,
            parameters.camera_metadata,
            parameters
                .camera_projection
                .x);
    const FilterSample filter_sample =
        sample_filter(
            path_pixel_sample(
                parameters,
                pixel),
            parameters);
    const float2 pixel_sample =
        0.5 +
        filter_sample.offset;
    const float2 lens_sample =
        path_sample_2d(
            parameters,
            pixel,
            4);
    const RayDesc ray =
        make_primary_ray(
            pixel,
            parameters.extent,
            pixel_sample,
            lens_sample,
            camera);
    const RayDesc ray_dx =
        make_primary_ray(
            pixel +
                uint2(1, 0),
            parameters.extent,
            pixel_sample,
            lens_sample,
            camera);
    const RayDesc ray_dy =
        make_primary_ray(
            pixel +
                uint2(0, 1),
            parameters.extent,
            pixel_sample,
            lens_sample,
            camera);
    RWStructuredBuffer<uint> ray_queue =
        bindings.ray_queue_0;
    RWStructuredBuffer<float4> ray_origins =
        bindings.ray_origins;
    RWStructuredBuffer<float4> ray_directions =
        bindings.ray_directions;
    RWStructuredBuffer<float4> ray_origin_dx =
        bindings.ray_origin_dx;
    RWStructuredBuffer<float4> ray_origin_dy =
        bindings.ray_origin_dy;
    RWStructuredBuffer<float4> ray_direction_dx =
        bindings.ray_direction_dx;
    RWStructuredBuffer<float4> ray_direction_dy =
        bindings.ray_direction_dy;
    RWStructuredBuffer<float4> throughputs =
        bindings.throughputs;
    RWStructuredBuffer<float4> radiances =
        bindings.radiances;
    RWStructuredBuffer<float4> wavelengths =
        bindings.wavelengths;
    RWStructuredBuffer<float4> wavelength_pdfs =
        bindings.wavelength_pdfs;
    RWStructuredBuffer<float4> path_r_u =
        bindings.path_r_u;
    RWStructuredBuffer<float4> path_r_l =
        bindings.path_r_l;
    RWStructuredBuffer<uint> current_media =
        bindings.current_media;
    RWStructuredBuffer<float4> light_context_normals =
        bindings.light_context_normals;
    RWStructuredBuffer<uint> path_flags =
        bindings.path_flags;
    RWStructuredBuffer<float> eta_scales =
        bindings.eta_scales;
    RWStructuredBuffer<float> filter_weights =
        bindings.filter_weights;
    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    uint queue_index;
    InterlockedAdd(
        queue_counts[0],
        1,
        queue_index);
    ray_queue[queue_index] =
        path_id;
    ray_origins[path_id] =
        float4(ray.Origin, ray.TMin);
    ray_directions[path_id] =
        float4(ray.Direction, ray.TMax);
    ray_origin_dx[path_id] =
        float4(
            ray_dx.Origin,
            ray_dx.TMin);
    ray_origin_dy[path_id] =
        float4(
            ray_dy.Origin,
            ray_dy.TMin);
    ray_direction_dx[path_id] =
        float4(
            ray_dx.Direction,
            ray_dx.TMax);
    ray_direction_dy[path_id] =
        float4(
            ray_dy.Direction,
            ray_dy.TMax);
    throughputs[path_id] =
        float4(1.0);
    radiances[path_id] =
        float4(0.0);
    const float wavelength_sample =
        path_sample_1d(
            parameters,
            pixel,
            0);
    const float4 sampled_wavelengths =
        sample_visible_wavelengths(
            wavelength_sample);
    wavelengths[path_id] =
        sampled_wavelengths;
    wavelength_pdfs[path_id] =
        wavelength_probabilities(
            sampled_wavelengths);
    path_r_u[path_id] =
        float4(1.0);
    path_r_l[path_id] =
        float4(1.0);
    current_media[path_id] =
        parameters.camera_metadata.w;
    light_context_normals[path_id] =
        float4(0.0);
    path_flags[path_id] =
        ray_differential_valid;
    eta_scales[path_id] = 1.0;
    filter_weights[path_id] =
        filter_sample.weight;
}

[shader("compute")]
[numthreads(256, 1, 1)]
void record_surface_gbuffer(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontParameters> parameter_buffer =
        push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
    if (parameters.film_metadata.x == 0) return;

    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    const uint queue_index =
        dispatch_thread_id.x;
    const uint ray_count =
        queue_counts[push_data.read_queue];
    if (queue_index >= ray_count) return;
    RWStructuredBuffer<uint> read_queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_0
            : bindings.ray_queue_1;
    const uint path_id =
        read_queue[queue_index];
    RWStructuredBuffer<uint4> hit_identifiers =
        bindings.hit_identifiers;
    const uint4 identifiers =
        hit_identifiers[path_id];
    if (identifiers.w != 1) return;

    StructuredBuffer<PathSceneBindings>
        scene_binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        scene_binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        scene_bindings.primitives;
    StructuredBuffer<PathMaterialHeader>
        material_headers =
            scene_bindings.material_headers;
    StructuredBuffer<uint> face_materials =
        scene_bindings.face_materials;
    const PathPrimitive primitive =
        primitives[identifiers.x];
    const uint material_index =
        primitive.material_binding.y == 0
            ? primitive.material
            : face_materials[
                  primitive.material_binding.x +
                  identifiers.y];
    if (
        material_headers[material_index].kind ==
        material_interface) return;

    RWStructuredBuffer<float4> hit_normal_distances =
        bindings.hit_normal_distances;
    RWStructuredBuffer<float4> hit_geometric_normal_u =
        bindings.hit_geometric_normal_u;
    RWStructuredBuffer<float4> hit_tangent_v =
        bindings.hit_tangent_v;
    RWStructuredBuffer<float4> ray_origins =
        bindings.ray_origins;
    RWStructuredBuffer<float4> ray_directions =
        bindings.ray_directions;
    RWStructuredBuffer<uint> path_flags =
        bindings.path_flags;
    RWStructuredBuffer<float4> hit_dpdu =
        bindings.hit_dpdu;
    RWStructuredBuffer<float4> hit_dpdv =
        bindings.hit_dpdv;
    RWStructuredBuffer<float4> hit_dndu =
        bindings.hit_dndu;
    RWStructuredBuffer<float4> hit_dndv =
        bindings.hit_dndv;
    const float4 hit =
        hit_normal_distances[path_id];
    const float4 geometric_hit =
        hit_geometric_normal_u[path_id];
    const float4 tangent_hit =
        hit_tangent_v[path_id];
    const float4 ray_origin =
        ray_origins[path_id];
    const float4 ray_direction =
        ray_directions[path_id];
    const float3 position =
        ray_origin.xyz +
        hit.w *
            ray_direction.xyz;
    float3 surface_normal =
        hit.xyz;
    float3 geometric_normal =
        geometric_hit.xyz;
    if ((primitive.flags & 1) != 0) {
        surface_normal =
            -surface_normal;
        geometric_normal =
            -geometric_normal;
    }
    const float3 surface_dpdu =
        hit_dpdu[path_id].xyz;
    const float3 surface_dpdv =
        hit_dpdv[path_id].xyz;
    const TextureEvaluationContext texture_context =
        make_texture_evaluation_context(
            bindings,
            path_id,
            position,
            surface_normal,
            geometric_normal,
            float2(
                geometric_hit.w,
                tangent_hit.w),
            surface_dpdu,
            surface_dpdv,
            path_flags[path_id],
            material_headers[
                material_index]
                .texture_requests,
            scene_bindings.metadata.w);
    const MaterialGeometry material_geometry = {
        texture_context,
        geometric_normal,
        surface_dpdu,
        surface_dpdv,
        hit_dndu[path_id].xyz,
        hit_dndv[path_id].xyz,
    };
    RWStructuredBuffer<float4> wavelength_buffer =
        bindings.wavelengths;
    const MaterialAlbedo albedo =
        estimate_material_albedo(
            scene_bindings,
            material_index,
            material_geometry,
            -ray_direction.xyz,
            wavelength_buffer[path_id]);
    float3 gbuffer_position =
        position;
    float3 gbuffer_shading_normal =
        albedo.shading_normal;
    float3 gbuffer_geometric_normal =
        geometric_normal;
    const CameraParameters camera =
        CameraParameters(
            parameters
                .camera_transform_row_0,
            parameters
                .camera_transform_row_1,
            parameters
                .camera_transform_row_2,
            parameters
                .camera_inverse_row_0,
            parameters
                .camera_inverse_row_1,
            parameters
                .camera_inverse_row_2,
            parameters.camera_screen,
            parameters.camera_lens,
            parameters.camera_metadata,
            parameters
                .camera_projection
                .x);
    const float3 camera_position =
        world_to_camera_point(
            camera,
            position);
    if (
        parameters
                .film_metadata
                .y !=
            0) {
        gbuffer_position =
            camera_position;
        gbuffer_shading_normal =
            normalize(
                world_to_camera_vector(
                    camera,
                    gbuffer_shading_normal));
        gbuffer_geometric_normal =
            normalize(
                world_to_camera_vector(
                    camera,
                    gbuffer_geometric_normal));
    }
    RWStructuredBuffer<float4>
        gbuffer_sample_albedo =
            bindings
                .gbuffer_sample_albedo;
    RWStructuredBuffer<float4>
        gbuffer_sample_shading_normal =
            bindings
                .gbuffer_sample_shading_normal;
    RWStructuredBuffer<float4>
        gbuffer_sample_geometric_normal =
            bindings
                .gbuffer_sample_geometric_normal;
    RWStructuredBuffer<float4>
        gbuffer_sample_position_depth =
            bindings
                .gbuffer_sample_position_depth;
    RWStructuredBuffer<float4>
        gbuffer_sample_uv =
            bindings
                .gbuffer_sample_uv;
    RWStructuredBuffer<uint4>
        gbuffer_sample_identity_0 =
            bindings
                .gbuffer_sample_identity_0;
    RWStructuredBuffer<uint4>
        gbuffer_sample_identity_1 =
            bindings
                .gbuffer_sample_identity_1;
    gbuffer_sample_albedo[path_id] =
        albedo.spectrum;
    gbuffer_sample_shading_normal[
        path_id] =
        float4(
            gbuffer_shading_normal,
            0.0);
    gbuffer_sample_geometric_normal[
        path_id] =
        float4(
            gbuffer_geometric_normal,
            0.0);
    gbuffer_sample_position_depth[
        path_id] =
        float4(
            gbuffer_position,
            -camera_position.z);
    gbuffer_sample_uv[path_id] =
        float4(
            geometric_hit.w,
            tangent_hit.w,
            0.0,
            0.0);
    gbuffer_sample_identity_0[
        path_id] =
        uint4(
            primitive.object_id,
            identifiers.y,
            material_headers[
                material_index]
                .stable_id.x);
    gbuffer_sample_identity_1[
        path_id] =
        uint4(
            material_headers[
                material_index]
                .stable_id.y,
            0,
            1,
            asuint(
                -camera_position.z));
}

[shader("compute")]
[numthreads(256, 1, 1)]
void sample_direct_lighting(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontParameters> parameter_buffer =
        push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
    if (
        parameters.light_count == 0 ||
        push_data.bounce ==
            parameters.max_depth) return;

    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    const uint queue_index =
        dispatch_thread_id.x;
    const uint ray_count =
        queue_counts[push_data.read_queue];
    if (queue_index >= ray_count) return;
    RWStructuredBuffer<uint> read_queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_0
            : bindings.ray_queue_1;
    const uint path_id =
        read_queue[queue_index];
    RWStructuredBuffer<uint4> hit_identifiers =
        bindings.hit_identifiers;
    const uint4 identifiers =
        hit_identifiers[path_id];
    if (identifiers.w != 1) return;

    StructuredBuffer<PathSceneBindings>
        scene_binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        scene_binding_buffer[0];
    StructuredBuffer<PathPrimitive> primitives =
        scene_bindings.primitives;
    StructuredBuffer<PathMaterialHeader>
        material_headers =
            scene_bindings.material_headers;
    StructuredBuffer<uint> face_materials =
        scene_bindings.face_materials;
    const PathPrimitive primitive =
        primitives[identifiers.x];
    const uint material_index =
        primitive.material_binding.y == 0
            ? primitive.material
            : face_materials[
                  primitive.material_binding.x +
                  identifiers.y];
    RWStructuredBuffer<float4> hit_normal_distances =
        bindings.hit_normal_distances;
    RWStructuredBuffer<float4> hit_geometric_normal_u =
        bindings.hit_geometric_normal_u;
    RWStructuredBuffer<float4> hit_tangent_v =
        bindings.hit_tangent_v;
    RWStructuredBuffer<float4> ray_origins =
        bindings.ray_origins;
    RWStructuredBuffer<float4> ray_directions =
        bindings.ray_directions;
    RWStructuredBuffer<uint> path_flags =
        bindings.path_flags;
    RWStructuredBuffer<float4> hit_dpdu =
        bindings.hit_dpdu;
    RWStructuredBuffer<float4> hit_dpdv =
        bindings.hit_dpdv;
    RWStructuredBuffer<float4> hit_dndu =
        bindings.hit_dndu;
    RWStructuredBuffer<float4> hit_dndv =
        bindings.hit_dndv;
    const float4 hit =
        hit_normal_distances[path_id];
    const float4 geometric_hit =
        hit_geometric_normal_u[path_id];
    const float4 tangent_hit =
        hit_tangent_v[path_id];
    const float4 ray_origin =
        ray_origins[path_id];
    const float4 ray_direction =
        ray_directions[path_id];
    const float3 position =
        ray_origin.xyz +
        hit.w *
            ray_direction.xyz;
    float3 surface_normal =
        hit.xyz;
    float3 geometric_normal =
        geometric_hit.xyz;
    if ((primitive.flags & 1) != 0) {
        surface_normal =
            -surface_normal;
        geometric_normal =
            -geometric_normal;
    }
    const float3 surface_dpdu =
        hit_dpdu[path_id].xyz;
    const float3 surface_dpdv =
        hit_dpdv[path_id].xyz;
    const TextureEvaluationContext texture_context =
        make_texture_evaluation_context(
            bindings,
            path_id,
            position,
            surface_normal,
            geometric_normal,
            float2(
                geometric_hit.w,
                tangent_hit.w),
            surface_dpdu,
            surface_dpdv,
            path_flags[path_id],
            material_headers[
                material_index]
                .texture_requests,
            scene_bindings.metadata.w);
    const MaterialGeometry material_geometry = {
        texture_context,
        geometric_normal,
        surface_dpdu,
        surface_dpdv,
        hit_dndu[path_id].xyz,
        hit_dndv[path_id].xyz,
    };
    const bool regularize =
        parameters.reserved != 0 &&
        (
            path_flags[path_id] &
            any_non_specular_bounce
        ) != 0;
    const uint2 pixel =
        uint2(
            path_id % parameters.extent.x,
            path_id / parameters.extent.x);
    const float light_choice =
        path_sample_1d(
            parameters,
            pixel,
            path_bounce_dimension(
                push_data.bounce,
                0));
    const PathSampledLight sampled =
        sample_light_sampler(
            scene_bindings,
            parameters.light_count,
            position,
            surface_normal,
            light_choice);
    const float2 light_sample =
        path_sample_2d(
            parameters,
            pixel,
            path_bounce_dimension(
                push_data.bounce,
                1));
    RWStructuredBuffer<float4> wavelength_buffer =
        bindings.wavelengths;
    const float4 wavelengths =
        wavelength_buffer[path_id];
    const PathLightLiSample light_sample_result =
        sampled.valid
            ? sample_light_li(
                  scene_bindings,
                  sampled.index,
                  position,
                  surface_normal,
                  light_sample,
                  wavelengths,
                  bindings
                      .texture_evaluation_stack,
                  queue_index *
                      scene_bindings
                          .metadata.y,
                  true)
            : PathLightLiSample();
    const MaterialEvaluation material_evaluation =
        light_sample_result.valid
            ? evaluate_material(
                  scene_bindings,
                  material_index,
                  material_geometry,
                  -ray_direction.xyz,
                  light_sample_result
                      .direction,
                  wavelengths,
                  regularize)
            : MaterialEvaluation();
    if (
        !light_sample_result.valid ||
        material_evaluation.pdf <= 0.0 ||
        !any(
            material_evaluation.throughput >
            float4(0.0))) return;

    uint shadow_index;
    InterlockedAdd(
        queue_counts[2],
        1,
        shadow_index);
    const float3 shadow_origin =
        offset_ray_origin(
            position,
            dot(
                geometric_normal,
                light_sample_result
                    .direction) >
                0.0
                ? geometric_normal
                : -geometric_normal);
    const float3 shadow_target =
        any(
            light_sample_result.normal !=
            float3(0.0))
            ? offset_ray_origin(
                  light_sample_result
                      .position,
                  dot(
                      light_sample_result
                          .normal,
                      -light_sample_result
                           .direction) >
                          0.0
                      ? light_sample_result
                            .normal
                      : -light_sample_result
                             .normal)
            : light_sample_result
                  .position;
    const float3 shadow_vector =
        shadow_target -
        shadow_origin;
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
    shadow_path_ids[shadow_index] =
        path_id;
    shadow_origins[shadow_index] =
        float4(shadow_origin, 0.0);
    shadow_directions[shadow_index] =
        float4(
            shadow_vector,
            0.9999);
    const float light_pdf =
        sampled.pmf *
        light_sample_result.pdf;
    RWStructuredBuffer<float4> throughputs =
        bindings.throughputs;
    shadow_contributions[shadow_index] =
        throughputs[path_id] *
        light_sample_result.radiance *
        material_evaluation.throughput;
    RWStructuredBuffer<float4> path_r_u =
        bindings.path_r_u;
    shadow_r_p[shadow_index] =
        path_r_u[path_id];
    shadow_pdfs[shadow_index] = {
        light_pdf,
        material_evaluation.pdf,
        light_sample_result.delta
            ? 1.0
            : 0.0,
        0.0,
    };
    RWStructuredBuffer<uint> current_media =
        bindings.current_media;
    shadow_media[shadow_index] =
        primitive.media.x !=
                primitive.media.y
            ? (
                  dot(
                      geometric_normal,
                      light_sample_result
                          .direction) >
                          0.0
                      ? primitive.media.y
                      : primitive.media.x
              )
            : current_media[path_id];
}

[shader("compute")]
[numthreads(256, 1, 1)]
void shade_surfaces(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    const uint queue_index =
        dispatch_thread_id.x;
    const uint ray_count =
        queue_counts[push_data.read_queue];
    if (queue_index >= ray_count) return;
    RWStructuredBuffer<uint> read_queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_0
            : bindings.ray_queue_1;
    RWStructuredBuffer<uint> write_queue =
        push_data.read_queue == 0
            ? bindings.ray_queue_1
            : bindings.ray_queue_0;
    const uint write_queue_index =
        1 - push_data.read_queue;
    const uint path_id =
        read_queue[queue_index];
    RWStructuredBuffer<float4> hit_normal_distances =
        bindings.hit_normal_distances;
    RWStructuredBuffer<float4> hit_geometric_normal_u =
        bindings.hit_geometric_normal_u;
    RWStructuredBuffer<float4> hit_tangent_v =
        bindings.hit_tangent_v;
    RWStructuredBuffer<uint4> hit_identifiers =
        bindings.hit_identifiers;
    const float4 hit =
        hit_normal_distances[path_id];
    const float4 geometric_hit =
        hit_geometric_normal_u[path_id];
    const float4 tangent_hit =
        hit_tangent_v[path_id];
    const uint4 identifiers =
        hit_identifiers[path_id];
    if (identifiers.w == 2) return;

    StructuredBuffer<WavefrontParameters> parameter_buffer =
        push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
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
    const float4 ray_origin =
        ray_origins[path_id];
    const float4 ray_direction =
        ray_directions[path_id];
    const float3 position =
        ray_origin.xyz +
        hit.w *
            ray_direction.xyz;
    float4 throughput =
        throughputs[path_id];
    float4 radiance =
        radiances[path_id];
    RWStructuredBuffer<float4> wavelength_buffer =
        bindings.wavelengths;
    RWStructuredBuffer<float4> wavelength_pdf_buffer =
        bindings.wavelength_pdfs;
    const float4 wavelengths =
        wavelength_buffer[path_id];
    StructuredBuffer<PathSceneBindings>
        scene_binding_buffer =
            push_data.scene_bindings;
    const PathSceneBindings scene_bindings =
        scene_binding_buffer[0];
    if (identifiers.w == 0) {
        radiance +=
            escaped_volpath_radiance(
                scene_bindings,
                parameters.light_count,
                ray_origin.xyz,
                bindings
                    .light_context_normals[
                        path_id]
                    .xyz,
                ray_direction.xyz,
                wavelengths,
                throughput,
                path_r_u[path_id],
                path_r_l[path_id],
                (
                    path_flags[path_id] &
                    bxdf_specular
                ) != 0,
                push_data.bounce == 0);
        radiances[path_id] =
            radiance;
        return;
    }
    StructuredBuffer<PathPrimitive> primitives =
        scene_bindings.primitives;
    StructuredBuffer<PathMaterialHeader>
        material_headers =
            scene_bindings.material_headers;
    StructuredBuffer<uint> face_materials =
        scene_bindings.face_materials;
    StructuredBuffer<PathLight> scene_lights =
        scene_bindings.lights;
    const PathPrimitive primitive =
        primitives[identifiers.x];
    const uint material_index =
        primitive.material_binding.y == 0
            ? primitive.material
            : face_materials[
                  primitive.material_binding.x +
                  identifiers.y];
    float3 surface_normal = hit.xyz;
    float3 geometric_normal =
        geometric_hit.xyz;
    if ((primitive.flags & 1) != 0) {
        surface_normal = -surface_normal;
        geometric_normal =
            -geometric_normal;
    }
    RWStructuredBuffer<float4> hit_dpdu =
        bindings.hit_dpdu;
    RWStructuredBuffer<float4> hit_dpdv =
        bindings.hit_dpdv;
    RWStructuredBuffer<float4> hit_dndu =
        bindings.hit_dndu;
    RWStructuredBuffer<float4> hit_dndv =
        bindings.hit_dndv;
    const float3 surface_dpdu =
        hit_dpdu[path_id].xyz;
    const float3 surface_dpdv =
        hit_dpdv[path_id].xyz;
    const TextureEvaluationContext texture_context =
        make_texture_evaluation_context(
            bindings,
            path_id,
            position,
            surface_normal,
            geometric_normal,
            float2(
                geometric_hit.w,
                tangent_hit.w),
            surface_dpdu,
            surface_dpdv,
            path_flags[path_id],
            material_headers[
                material_index]
                .texture_requests,
            scene_bindings.metadata.w);
    const MaterialGeometry material_geometry = {
        texture_context,
        geometric_normal,
        surface_dpdu,
        surface_dpdv,
        hit_dndu[path_id].xyz,
        hit_dndv[path_id].xyz,
    };
    const bool regularize =
        parameters.reserved != 0 &&
        (
            path_flags[path_id] &
            any_non_specular_bounce
        ) != 0;
    if (
        primitive.area_light !=
        0xffffffff) {
        const uint hit_light_index =
            primitive.first_light_shape +
            (
                primitive.attributes.z ==
                        geometry_triangle
                    ? identifiers.y
                    : 0
            );
        const PathLight hit_light =
            scene_lights[
                hit_light_index];
        const float4 emission =
            evaluate_area_light(
                scene_bindings,
                hit_light,
                position,
                geometric_normal,
                float2(
                    geometric_hit.w,
                    tangent_hit.w),
                -ray_direction.xyz,
                wavelengths,
                bindings
                    .texture_evaluation_stack,
                queue_index *
                    scene_bindings
                        .metadata.y);
        if (
            push_data.bounce == 0 ||
            (
                path_flags[path_id] &
                bxdf_specular
            ) != 0) {
            const float denominator =
                spectrum_average(
                    path_r_u[
                        path_id]);
            if (denominator != 0.0)
                radiance +=
                    throughput *
                    emission /
                    denominator;
        }
        else {
            const float light_pdf =
                area_light_pdf_for_hit(
                    scene_bindings,
                    hit_light_index,
                    light_sampler_pmf(
                        scene_bindings,
                        parameters.light_count,
                        ray_origin.xyz,
                        bindings
                            .light_context_normals[
                                path_id]
                            .xyz,
                        hit_light_index),
                    ray_origin.xyz,
                    bindings
                        .light_context_normals[
                            path_id]
                        .xyz,
                    ray_direction.xyz);
            const float denominator =
                spectrum_average(
                    path_r_u[path_id] +
                    path_r_l[path_id] *
                        light_pdf);
            if (denominator != 0.0)
                radiance +=
                    throughput *
                    emission /
                    denominator;
        }
    }
    if (
        push_data.bounce ==
        parameters.max_depth) {
        radiances[path_id] =
            radiance;
        throughputs[path_id] =
            throughput;
        return;
    }
    if (
        material_terminates_secondary(
            scene_bindings,
            material_index,
            texture_context,
            -ray_direction.xyz,
            wavelengths) &&
        wavelength_pdf_buffer[path_id].y != 0.0) {
        float4 probabilities =
            wavelength_pdf_buffer[path_id];
        probabilities.x /=
            sampled_spectrum_size;
        probabilities.yzw =
            float3(0.0);
        wavelength_pdf_buffer[path_id] =
            probabilities;
    }

    const uint2 pixel =
        uint2(
            path_id % parameters.extent.x,
            path_id / parameters.extent.x);
    const MaterialSample material_sample =
        sample_material(
            scene_bindings,
            material_index,
            material_geometry,
            -ray_direction.xyz,
            wavelengths,
            path_sample_1d(
                parameters,
                pixel,
                path_bounce_dimension(
                    push_data.bounce,
                    3)),
            path_sample_2d(
                parameters,
                pixel,
                path_bounce_dimension(
                    push_data.bounce,
                    4)),
            regularize);
    if (!material_sample.valid) {
        radiances[path_id] =
            radiance;
        throughputs[path_id] =
            throughput;
        return;
    }
    throughput *=
        material_sample.throughput /
        material_sample.sampling_pdf;
    path_r_l[path_id] =
        path_r_u[path_id] /
        material_sample.mis_pdf;
    uint next_path_flags =
        material_sample.flags;
    if (
        (
            path_flags[path_id] &
            any_non_specular_bounce
        ) != 0 ||
        (
            material_sample.flags &
            bxdf_specular
        ) == 0)
        next_path_flags |=
            any_non_specular_bounce;
    if (
        (
            path_flags[path_id] &
            ray_differential_valid
        ) != 0 &&
        (
            material_sample.flags &
            bxdf_specular
        ) != 0) {
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
        const float3 dndu =
            hit_dndu[path_id].xyz;
        const float3 dndv =
            hit_dndv[path_id].xyz;
        float3 dndx =
            dndu *
                texture_context
                    .texture_coordinates_dx.x +
            dndv *
                texture_context
                    .texture_coordinates_dx.y;
        float3 dndy =
            dndu *
                texture_context
                    .texture_coordinates_dy.x +
            dndv *
                texture_context
                    .texture_coordinates_dy.y;
        const float3 outgoing =
            -ray_direction.xyz;
        const float3 outgoing_dx =
            -ray_direction_dx[path_id].xyz -
            outgoing;
        const float3 outgoing_dy =
            -ray_direction_dy[path_id].xyz -
            outgoing;
        float3 differential_direction_x;
        float3 differential_direction_y;
        if (
            (
                material_sample.flags &
                bxdf_reflection
            ) != 0) {
            const float outgoing_dot_normal =
                dot(
                    outgoing,
                    material_sample
                        .shading_normal);
            const float derivative_dot_normal_x =
                dot(
                    outgoing_dx,
                    material_sample
                        .shading_normal) +
                dot(
                    outgoing,
                    dndx);
            const float derivative_dot_normal_y =
                dot(
                    outgoing_dy,
                    material_sample
                        .shading_normal) +
                dot(
                    outgoing,
                    dndy);
            differential_direction_x =
                material_sample.wi -
                outgoing_dx +
                2.0 *
                (
                    outgoing_dot_normal *
                        dndx +
                    derivative_dot_normal_x *
                        material_sample
                            .shading_normal
                );
            differential_direction_y =
                material_sample.wi -
                outgoing_dy +
                2.0 *
                (
                    outgoing_dot_normal *
                        dndy +
                    derivative_dot_normal_y *
                        material_sample
                            .shading_normal
                );
        }
        else {
            float3 oriented_normal =
                material_sample
                    .shading_normal;
            if (dot(
                    outgoing,
                    oriented_normal) < 0.0) {
                oriented_normal =
                    -oriented_normal;
                dndx = -dndx;
                dndy = -dndy;
            }
            const float outgoing_dot_normal =
                dot(
                    outgoing,
                    oriented_normal);
            const float incident_dot_normal =
                dot(
                    material_sample.wi,
                    oriented_normal);
            const float derivative_dot_normal_x =
                dot(
                    outgoing_dx,
                    oriented_normal) +
                dot(
                    outgoing,
                    dndx);
            const float derivative_dot_normal_y =
                dot(
                    outgoing_dy,
                    oriented_normal) +
                dot(
                    outgoing,
                    dndy);
            const float eta =
                material_sample.eta;
            const float mu =
                outgoing_dot_normal /
                    eta -
                abs(
                    incident_dot_normal);
            const float eta_factor =
                1.0 /
                    eta +
                outgoing_dot_normal /
                    (
                        eta *
                        eta *
                        incident_dot_normal
                    );
            const float derivative_mu_x =
                derivative_dot_normal_x *
                eta_factor;
            const float derivative_mu_y =
                derivative_dot_normal_y *
                eta_factor;
            differential_direction_x =
                material_sample.wi -
                eta *
                    outgoing_dx +
                mu *
                    dndx +
                derivative_mu_x *
                    oriented_normal;
            differential_direction_y =
                material_sample.wi -
                eta *
                    outgoing_dy +
                mu *
                    dndy +
                derivative_mu_y *
                    oriented_normal;
        }
        const float3 differential_origin_x =
            position +
            texture_context.position_dx;
        const float3 differential_origin_y =
            position +
            texture_context.position_dy;
        if (
            dot(
                differential_direction_x,
                differential_direction_x) <=
                1.0e16 &&
            dot(
                differential_direction_y,
                differential_direction_y) <=
                1.0e16 &&
            dot(
                differential_origin_x,
                differential_origin_x) <=
                1.0e16 &&
            dot(
                differential_origin_y,
                differential_origin_y) <=
                1.0e16) {
            ray_origin_dx[path_id] =
                float4(
                    differential_origin_x,
                    0.0);
            ray_origin_dy[path_id] =
                float4(
                    differential_origin_y,
                    0.0);
            ray_direction_dx[path_id] =
                float4(
                    differential_direction_x,
                    parameters.camera_lens.w);
            ray_direction_dy[path_id] =
                float4(
                    differential_direction_y,
                    parameters.camera_lens.w);
            next_path_flags |=
                ray_differential_valid;
        }
    }
    path_flags[path_id] =
        next_path_flags;
    if (
        (
            material_sample.flags &
            bxdf_transmission
        ) != 0)
        eta_scales[path_id] *=
            material_sample.eta *
            material_sample.eta;
    const RussianRouletteResult
        roulette =
            apply_volpath_russian_roulette(
                throughput,
                path_r_u[path_id],
                eta_scales[path_id],
                push_data.bounce,
                path_sample_1d(
                    parameters,
                    pixel,
                    path_bounce_dimension(
                        push_data.bounce,
                        6)));
    throughput =
        roulette.throughput;
    const bool continue_path =
        roulette.continue_path;
    radiances[path_id] =
        radiance;
    throughputs[path_id] =
        throughput;
    RWStructuredBuffer<float4> light_context_normals =
        bindings.light_context_normals;
    light_context_normals[path_id] =
        float4(
            material_sample.shading_normal,
            0.0);
    if (
        primitive.media.x !=
        primitive.media.y)
        current_media[path_id] =
            dot(
                geometric_normal,
                material_sample.wi) >
                    0.0
                ? primitive.media.y
                : primitive.media.x;
    if (!continue_path) return;
    uint next_queue_index;
    InterlockedAdd(
        queue_counts[write_queue_index],
        1,
        next_queue_index);
    write_queue[next_queue_index] =
        path_id;
    ray_origins[path_id] =
        float4(
            offset_ray_origin(
                position,
                dot(
                    geometric_normal,
                    material_sample.wi) >
                    0.0
                    ? geometric_normal
                    : -geometric_normal),
            0.0);
    ray_directions[path_id] =
        float4(
            material_sample.wi,
            parameters.camera_lens.w);
}

[shader("compute")]
[numthreads(256, 1, 1)]
void resolve_visibility(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    RWStructuredBuffer<uint> queue_counts =
        bindings.queue_counts;
    const uint shadow_index =
        dispatch_thread_id.x;
    if (shadow_index >=
        queue_counts[2])
        return;
    RWStructuredBuffer<uint> shadow_path_ids =
        bindings.shadow_path_ids;
    RWStructuredBuffer<float4> shadow_contributions =
        bindings.shadow_contributions;
    RWStructuredBuffer<float4> radiances =
        bindings.radiances;
    const uint path_id =
        shadow_path_ids[shadow_index];
    radiances[path_id] +=
        shadow_contributions[shadow_index];
}

[shader("compute")]
[numthreads(256, 1, 1)]
void accumulate_film(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    StructuredBuffer<WavefrontBindings> binding_buffer =
        push_data.bindings;
    const WavefrontBindings bindings =
        binding_buffer[0];
    StructuredBuffer<WavefrontParameters> parameter_buffer =
        push_data.parameters;
    const WavefrontParameters parameters =
        parameter_buffer[0];
    const uint path_id =
        dispatch_thread_id.x;
    const uint path_count =
        parameters.extent.x *
        parameters.extent.y;
    if (path_id >= path_count) return;
    const uint2 pixel =
        uint2(
            path_id % parameters.extent.x,
            path_id / parameters.extent.x);
    RWTexture2D<float4> output =
        bindings.output;
    RWStructuredBuffer<float> filter_weights =
        bindings.filter_weights;
    RWStructuredBuffer<float4> film_rgb_sums =
        bindings.film_rgb_sums;
    RWStructuredBuffer<float> film_weight_sums =
        bindings.film_weight_sums;
    const float weight =
        filter_weights[path_id];
    float3 rgb_sum =
        float3(0.0);
    float weight_sum = 0.0;
    if (parameters.sample_index != 0) {
        rgb_sum =
            film_rgb_sums[path_id].xyz;
        weight_sum =
            film_weight_sums[path_id];
    }
    if (weight != 0.0) {
        RWStructuredBuffer<float4> radiances =
            bindings.radiances;
        RWStructuredBuffer<float4> wavelengths =
            bindings.wavelengths;
        RWStructuredBuffer<float4> wavelength_pdfs =
            bindings.wavelength_pdfs;
        StructuredBuffer<float> sensor_response =
            parameters.film_sensor_response;
        const float3 radiance =
            spectrum_to_film_sensor(
                radiances[path_id],
                wavelengths[path_id],
                wavelength_pdfs[path_id],
                sensor_response,
                parameters.film_parameters.x,
                parameters.film_parameters.y);
        rgb_sum +=
            radiance *
            weight;
        weight_sum +=
            weight;
    }
    film_rgb_sums[path_id] =
        float4(
            rgb_sum,
            0.0);
    film_weight_sums[path_id] =
        weight_sum;
    const float3 sensor_rgb =
        weight_sum != 0.0
            ? rgb_sum /
                  weight_sum
            : rgb_sum;
    const float3 output_rgb =
        float3(
            dot(
                parameters
                    .film_sensor_to_output_row_0
                    .xyz,
                sensor_rgb),
            dot(
                parameters
                    .film_sensor_to_output_row_1
                    .xyz,
                sensor_rgb),
            dot(
                parameters
                    .film_sensor_to_output_row_2
                    .xyz,
                sensor_rgb)) *
        exp2(
            parameters
                .filter_parameters_0
                .w);
    output[pixel] =
        float4(
            output_rgb,
            1.0);
    if (
        parameters.film_metadata.x != 0) {
        RWStructuredBuffer<float4>
            gbuffer_sample_albedo =
                bindings
                    .gbuffer_sample_albedo;
        RWStructuredBuffer<float4>
            gbuffer_sample_shading_normal =
                bindings
                    .gbuffer_sample_shading_normal;
        RWStructuredBuffer<float4>
            gbuffer_sample_geometric_normal =
                bindings
                    .gbuffer_sample_geometric_normal;
        RWStructuredBuffer<float4>
            gbuffer_sample_position_depth =
                bindings
                    .gbuffer_sample_position_depth;
        RWStructuredBuffer<float4>
            gbuffer_sample_uv =
                bindings
                    .gbuffer_sample_uv;
        RWStructuredBuffer<uint4>
            gbuffer_sample_identity_0 =
                bindings
                    .gbuffer_sample_identity_0;
        RWStructuredBuffer<uint4>
            gbuffer_sample_identity_1 =
                bindings
                    .gbuffer_sample_identity_1;
        RWStructuredBuffer<float4>
            gbuffer_albedo_sums =
                bindings
                    .gbuffer_albedo_sums;
        RWStructuredBuffer<float4>
            gbuffer_shading_normal_sums =
                bindings
                    .gbuffer_shading_normal_sums;
        RWStructuredBuffer<float4>
            gbuffer_geometric_normal_sums =
                bindings
                    .gbuffer_geometric_normal_sums;
        RWStructuredBuffer<float4>
            gbuffer_position_depth_sums =
                bindings
                    .gbuffer_position_depth_sums;
        RWStructuredBuffer<float4>
            gbuffer_uv_weight_sums =
                bindings
                    .gbuffer_uv_weight_sums;
        RWStructuredBuffer<uint4>
            gbuffer_identity_0 =
                bindings
                    .gbuffer_identity_0;
        RWStructuredBuffer<uint4>
            gbuffer_identity_1 =
                bindings
                    .gbuffer_identity_1;
        float4 albedo_sum =
            float4(0.0);
        float4 shading_normal_sum =
            float4(0.0);
        float4 geometric_normal_sum =
            float4(0.0);
        float4 position_depth_sum =
            float4(0.0);
        float4 uv_weight_sum =
            float4(0.0);
        uint4 identity_0 =
            uint4(0);
        uint4 identity_1 =
            uint4(0);
        if (
            parameters.sample_index != 0) {
            albedo_sum =
                gbuffer_albedo_sums[
                    path_id];
            shading_normal_sum =
                gbuffer_shading_normal_sums[
                    path_id];
            geometric_normal_sum =
                gbuffer_geometric_normal_sums[
                    path_id];
            position_depth_sum =
                gbuffer_position_depth_sums[
                    path_id];
            uv_weight_sum =
                gbuffer_uv_weight_sums[
                    path_id];
            identity_0 =
                gbuffer_identity_0[
                    path_id];
            identity_1 =
                gbuffer_identity_1[
                    path_id];
        }
        const uint4 sample_identity_1 =
            gbuffer_sample_identity_1[
                path_id];
        if (
            sample_identity_1.z != 0 &&
            weight != 0.0) {
            StructuredBuffer<
                PathSceneBindings>
                    scene_binding_buffer =
                        push_data
                            .scene_bindings;
            const PathSceneBindings
                scene_bindings =
                    scene_binding_buffer[0];
            RWStructuredBuffer<float4>
                gbuffer_wavelengths =
                    bindings.wavelengths;
            RWStructuredBuffer<float4>
                gbuffer_wavelength_pdfs =
                    bindings
                        .wavelength_pdfs;
            const float3 albedo =
                spectrum_to_output_albedo(
                    gbuffer_sample_albedo[
                        path_id],
                    gbuffer_wavelengths[
                        path_id],
                    gbuffer_wavelength_pdfs[
                        path_id],
                    scene_bindings
                        .cie_spectra,
                    parameters
                        .film_metadata
                        .z);
            albedo_sum.xyz +=
                weight *
                albedo;
            shading_normal_sum.xyz +=
                weight *
                gbuffer_sample_shading_normal[
                    path_id]
                    .xyz;
            geometric_normal_sum.xyz +=
                weight *
                gbuffer_sample_geometric_normal[
                    path_id]
                    .xyz;
            position_depth_sum +=
                weight *
                gbuffer_sample_position_depth[
                    path_id];
            uv_weight_sum.xy +=
                weight *
                gbuffer_sample_uv[
                    path_id]
                    .xy;
            uv_weight_sum.z +=
                weight;
            uv_weight_sum.w +=
                1.0;
            if (
                identity_1.z == 0 ||
                asfloat(
                    sample_identity_1.w) <
                    asfloat(
                        identity_1.w)) {
                identity_0 =
                    gbuffer_sample_identity_0[
                        path_id];
                identity_1 =
                    sample_identity_1;
            }
        }
        gbuffer_albedo_sums[
            path_id] =
            albedo_sum;
        gbuffer_shading_normal_sums[
            path_id] =
            shading_normal_sum;
        gbuffer_geometric_normal_sums[
            path_id] =
            geometric_normal_sum;
        gbuffer_position_depth_sums[
            path_id] =
            position_depth_sum;
        gbuffer_uv_weight_sums[
            path_id] =
            uv_weight_sum;
        gbuffer_identity_0[
            path_id] =
            identity_0;
        gbuffer_identity_1[
            path_id] =
            identity_1;
    }
}
