module path_evaluate_textures;

import path_interaction;
import path_scene;
import path_texture;
import path_queue;

[vk::push_constant]
uniform WavefrontPushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void evaluate_surface_textures(
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
    StructuredBuffer<PathMaterialHeader>
        material_headers =
            scene_bindings.material_headers;
    const PathMaterialHeader material =
        material_headers[material_index];
    if (material.texture_requests.y == 0) return;

    RWStructuredBuffer<float4> hit_normal_distances =
        bindings.hit_normal_distances;
    RWStructuredBuffer<float4> hit_geometric_normal_u =
        bindings.hit_geometric_normal_u;
    RWStructuredBuffer<float4> hit_tangent_v =
        bindings.hit_tangent_v;
    RWStructuredBuffer<float4> hit_dpdu =
        bindings.hit_dpdu;
    RWStructuredBuffer<float4> hit_dpdv =
        bindings.hit_dpdv;
    RWStructuredBuffer<float4> ray_origins =
        bindings.ray_origins;
    RWStructuredBuffer<float4> ray_directions =
        bindings.ray_directions;
    RWStructuredBuffer<uint> path_flags =
        bindings.path_flags;
    const float4 hit =
        hit_normal_distances[path_id];
    const float4 geometric_hit =
        hit_geometric_normal_u[path_id];
    const float4 tangent_hit =
        hit_tangent_v[path_id];
    const float3 position =
        ray_origins[path_id].xyz +
        hit.w *
            ray_directions[path_id].xyz;
    float3 surface_normal =
        hit.xyz;
    float3 geometric_normal =
        geometric_hit.xyz;
    if ((primitive.flags & 1) != 0) {
        surface_normal = -surface_normal;
        geometric_normal =
            -geometric_normal;
    }
    const float3 dpdu =
        hit_dpdu[path_id].xyz;
    const float3 dpdv =
        hit_dpdv[path_id].xyz;
    const TextureEvaluationContext context =
        make_texture_evaluation_context(
            bindings,
            path_id,
            position,
            surface_normal,
            geometric_normal,
            float2(
                geometric_hit.w,
                tangent_hit.w),
            dpdu,
            dpdv,
            path_flags[path_id],
            material.texture_requests,
            scene_bindings.metadata.w);
    RWStructuredBuffer<float4> wavelengths =
        bindings.wavelengths;
    const float4 sampled_wavelengths =
        wavelengths[path_id];
    StructuredBuffer<PathMaterialTextureRequest>
        requests =
            scene_bindings
                .material_texture_requests;
    const DescriptorHandle<
        RWStructuredBuffer<float4>>
        texture_stack =
            bindings.texture_evaluation_stack;
    RWStructuredBuffer<float4>
        evaluated_values =
            bindings.evaluated_texture_values;
    const uint texture_stack_base =
        path_id *
        scene_bindings.metadata.y;
    const uint value_base =
        path_id *
        scene_bindings.metadata.w;
    const float du_candidate =
        0.5 *
        (
            abs(
                context
                    .texture_coordinates_dx.x) +
            abs(
                context
                    .texture_coordinates_dy.x)
        );
    const float dv_candidate =
        0.5 *
        (
            abs(
                context
                    .texture_coordinates_dx.y) +
            abs(
                context
                    .texture_coordinates_dy.y)
        );
    const float du =
        du_candidate == 0.0
            ? 0.0005
            : du_candidate;
    const float dv =
        dv_candidate == 0.0
            ? 0.0005
            : dv_candidate;
    [loop]
    for (uint request_index = 0;
         request_index !=
             material.texture_requests.y;
         ++request_index) {
        const PathMaterialTextureRequest request =
            requests[
                material.texture_requests.x +
                request_index];
        float4 value;
        if (
            request.data.y ==
            material_texture_normal)
            value =
                evaluate_normal_map(
                    scene_bindings,
                    request.data.x,
                    context);
        else {
            TextureEvaluationContext shifted =
                context;
            if (
                request.data.y ==
                material_texture_bump_u) {
                shifted.position +=
                    du * dpdu;
                shifted.texture_coordinates +=
                    float2(du, 0.0);
            }
            else if (
                request.data.y ==
                material_texture_bump_v) {
                shifted.position +=
                    dv * dpdv;
                shifted.texture_coordinates +=
                    float2(0.0, dv);
            }
            value =
                evaluate_texture_program(
                    scene_bindings,
                    request.data.x,
                    shifted,
                    sampled_wavelengths,
                    texture_stack,
                    texture_stack_base);
        }
        evaluated_values[
            value_base +
            request_index] =
            value;
    }
}
