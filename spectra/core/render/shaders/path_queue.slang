module path_queue;

import path_scene;

public struct WavefrontParameters {
    public float4 camera_transform_row_0;
    public float4 camera_transform_row_1;
    public float4 camera_transform_row_2;
    public float4 camera_inverse_row_0;
    public float4 camera_inverse_row_1;
    public float4 camera_inverse_row_2;
    public float4 camera_screen;
    public float4 camera_lens;
    public uint4 camera_metadata;
    public float4 camera_projection;
    public float4 filter_parameters_0;
    public float4 filter_parameters_1;
    public uint4 filter_metadata;
    public DescriptorHandle<StructuredBuffer<float>>
        filter_distribution;
    public DescriptorHandle<StructuredBuffer<uint>>
        sampling_tables;
    public uint4 sampler_metadata;
    public uint4 sampler_parameters;
    public uint4 film_bounds;
    public DescriptorHandle<StructuredBuffer<float2>>
        pmj_pixel_samples;
    public DescriptorHandle<StructuredBuffer<float>>
        film_sensor_response;
    public float4 film_sensor_to_output_row_0;
    public float4 film_sensor_to_output_row_1;
    public float4 film_sensor_to_output_row_2;
    public float4 film_parameters;
    public uint4 film_metadata;
    public uint2 extent;
    public uint sample_index;
    public uint max_depth;
    public uint light_count;
    public uint reserved;
}

public struct WavefrontBindings {
    public DescriptorHandle<RWTexture2D<float4>> output;
    public DescriptorHandle<RWTexture2D<float>> depth;
    public DescriptorHandle<RWStructuredBuffer<uint>> queue_counts;
    public DescriptorHandle<RWStructuredBuffer<uint>> ray_queue_0;
    public DescriptorHandle<RWStructuredBuffer<uint>> ray_queue_1;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_origins;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_directions;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_origin_dx;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_origin_dy;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_direction_dx;
    public DescriptorHandle<RWStructuredBuffer<float4>> ray_direction_dy;
    public DescriptorHandle<RWStructuredBuffer<float4>> throughputs;
    public DescriptorHandle<RWStructuredBuffer<float4>> radiances;
    public DescriptorHandle<RWStructuredBuffer<float4>> wavelengths;
    public DescriptorHandle<RWStructuredBuffer<float4>> wavelength_pdfs;
    public DescriptorHandle<RWStructuredBuffer<float4>> path_r_u;
    public DescriptorHandle<RWStructuredBuffer<float4>> path_r_l;
    public DescriptorHandle<RWStructuredBuffer<uint>> current_media;
    public DescriptorHandle<RWStructuredBuffer<float4>> light_context_normals;
    public DescriptorHandle<RWStructuredBuffer<uint>> path_flags;
    public DescriptorHandle<RWStructuredBuffer<float>> eta_scales;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_normal_distances;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_geometric_normal_u;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_tangent_v;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_dpdu;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_dpdv;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_dndu;
    public DescriptorHandle<RWStructuredBuffer<float4>> hit_dndv;
    public DescriptorHandle<RWStructuredBuffer<uint4>> hit_identifiers;
    public DescriptorHandle<RWStructuredBuffer<uint>> shadow_path_ids;
    public DescriptorHandle<RWStructuredBuffer<float4>> shadow_origins;
    public DescriptorHandle<RWStructuredBuffer<float4>> shadow_directions;
    public DescriptorHandle<RWStructuredBuffer<float4>> shadow_contributions;
    public DescriptorHandle<RWStructuredBuffer<float4>> shadow_r_p;
    public DescriptorHandle<RWStructuredBuffer<float4>> shadow_pdfs;
    public DescriptorHandle<RWStructuredBuffer<uint>> shadow_media;
    public DescriptorHandle<RWStructuredBuffer<float4>> texture_evaluation_stack;
    public DescriptorHandle<RWStructuredBuffer<float4>> evaluated_texture_values;
    public DescriptorHandle<RWStructuredBuffer<float>> filter_weights;
    public DescriptorHandle<RWStructuredBuffer<float4>> film_rgb_sums;
    public DescriptorHandle<RWStructuredBuffer<float>> film_weight_sums;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_sample_albedo;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_sample_shading_normal;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_sample_geometric_normal;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_sample_position_depth;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_sample_uv;
    public DescriptorHandle<RWStructuredBuffer<uint4>> gbuffer_sample_identity_0;
    public DescriptorHandle<RWStructuredBuffer<uint4>> gbuffer_sample_identity_1;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_albedo_sums;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_shading_normal_sums;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_geometric_normal_sums;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_position_depth_sums;
    public DescriptorHandle<RWStructuredBuffer<float4>> gbuffer_uv_weight_sums;
    public DescriptorHandle<RWStructuredBuffer<uint4>> gbuffer_identity_0;
    public DescriptorHandle<RWStructuredBuffer<uint4>> gbuffer_identity_1;
}

public struct WavefrontPushData {
    public uint64_t acceleration_structure_address;
    public DescriptorHandle<StructuredBuffer<WavefrontBindings>> bindings;
    public DescriptorHandle<StructuredBuffer<WavefrontParameters>> parameters;
    public DescriptorHandle<StructuredBuffer<PathSceneBindings>> scene_bindings;
    public uint bounce;
    public uint read_queue;
}

public struct SurfacePayload {
    public float3 shading_normal;
    public float hit_distance;
    public float3 geometric_normal;
    public float texture_u;
    public float3 tangent;
    public float texture_v;
    public float3 dpdu;
    public float alpha_origin_x;
    public float3 dpdv;
    public float alpha_origin_y;
    public float3 dndu;
    public float alpha_origin_z;
    public float3 dndv;
    public float padding_3;
    public uint instance_index;
    public uint primitive_index;
    public uint front_face;
    public uint hit;
}

public struct TransmittancePayload {
    public float3 alpha_origin;
    public float hit_distance;
    public float3 geometric_normal;
    public uint instance_index;
    public uint primitive_index;
    public uint front_face;
    public uint hit;
    public uint padding;
}
