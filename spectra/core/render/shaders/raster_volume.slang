module raster_volume;

import raster_scene;
import raster_texture;

static const float cie_y_integral = 106.856895;

struct RasterVolumePushData {
    DescriptorHandle<RWTexture2D<float4>> output;
    DescriptorHandle<Texture2D<float>> depth;
    DescriptorHandle<StructuredBuffer<RasterVolume>> volumes;
    uint2 reserved;
    uint4 metadata;
    uint4 film_bounds;
    float4 inverse_view_projection_row_0;
    float4 inverse_view_projection_row_1;
    float4 inverse_view_projection_row_2;
    float4 inverse_view_projection_row_3;
}

[vk::push_constant]
uniform RasterVolumePushData push_data;

float3 transform_point(
    RasterVolume volume,
    float3 point) {
    const float4 homogeneous =
        float4(point, 1.0);
    return float3(
        dot(
            volume.inverse_transform_row_0,
            homogeneous),
        dot(
            volume.inverse_transform_row_1,
            homogeneous),
        dot(
            volume.inverse_transform_row_2,
            homogeneous));
}

float3 transform_direction(
    RasterVolume volume,
    float3 direction) {
    return float3(
        dot(
            volume.inverse_transform_row_0.xyz,
            direction),
        dot(
            volume.inverse_transform_row_1.xyz,
            direction),
        dot(
            volume.inverse_transform_row_2.xyz,
            direction));
}

bool intersect_bounds(
    RasterVolume volume,
    float3 origin,
    float3 direction,
    out float minimum,
    out float maximum) {
    const float3 local_origin =
        transform_point(
            volume,
            origin);
    const float3 local_direction =
        transform_direction(
            volume,
            direction);
    const float3 inverse_direction =
        1.0 /
        local_direction;
    const float3 first =
        (
            volume.bounds_minimum.xyz -
            local_origin
        ) *
        inverse_direction;
    const float3 second =
        (
            volume.bounds_maximum.xyz -
            local_origin
        ) *
        inverse_direction;
    const float3 lower =
        min(
            first,
            second);
    const float3 upper =
        max(
            first,
            second);
    minimum =
        max(
            max(
                lower.x,
                lower.y),
            lower.z);
    maximum =
        min(
            min(
                upper.x,
                upper.y),
            upper.z);
    return maximum > max(minimum, 0.0);
}

float sample_scalar(
    StructuredBuffer<float> values,
    uint3 resolution,
    float3 coordinate) {
    const float3 grid =
        saturate(coordinate) *
            float3(resolution - 1) +
        0.5;
    const uint3 lower =
        uint3(
            floor(grid));
    const uint3 upper =
        min(
            lower + 1,
            resolution - 1);
    const float3 amount =
        frac(grid);
    const uint lower_000 =
        (
            lower.z *
                resolution.y +
            lower.y
        ) *
            resolution.x +
        lower.x;
    const uint lower_100 =
        (
            lower.z *
                resolution.y +
            lower.y
        ) *
            resolution.x +
        upper.x;
    const uint lower_010 =
        (
            lower.z *
                resolution.y +
            upper.y
        ) *
            resolution.x +
        lower.x;
    const uint lower_110 =
        (
            lower.z *
                resolution.y +
            upper.y
        ) *
            resolution.x +
        upper.x;
    const uint upper_000 =
        (
            upper.z *
                resolution.y +
            lower.y
        ) *
            resolution.x +
        lower.x;
    const uint upper_100 =
        (
            upper.z *
                resolution.y +
            lower.y
        ) *
            resolution.x +
        upper.x;
    const uint upper_010 =
        (
            upper.z *
                resolution.y +
            upper.y
        ) *
            resolution.x +
        lower.x;
    const uint upper_110 =
        (
            upper.z *
                resolution.y +
            upper.y
        ) *
            resolution.x +
        upper.x;
    return lerp(
        lerp(
            lerp(
                values[lower_000],
                values[lower_100],
                amount.x),
            lerp(
                values[lower_010],
                values[lower_110],
                amount.x),
            amount.y),
        lerp(
            lerp(
                values[upper_000],
                values[upper_100],
                amount.x),
            lerp(
                values[upper_010],
                values[upper_110],
                amount.x),
            amount.y),
        amount.z);
}

float3 sample_vector(
    StructuredBuffer<float3> values,
    uint3 resolution,
    float3 coordinate) {
    const float3 grid =
        saturate(coordinate) *
            float3(resolution - 1) +
        0.5;
    const uint3 lower =
        uint3(
            floor(grid));
    const uint3 upper =
        min(
            lower + 1,
            resolution - 1);
    const float3 amount =
        frac(grid);
    const uint indices[8] = {
        (lower.z * resolution.y + lower.y) * resolution.x + lower.x,
        (lower.z * resolution.y + lower.y) * resolution.x + upper.x,
        (lower.z * resolution.y + upper.y) * resolution.x + lower.x,
        (lower.z * resolution.y + upper.y) * resolution.x + upper.x,
        (upper.z * resolution.y + lower.y) * resolution.x + lower.x,
        (upper.z * resolution.y + lower.y) * resolution.x + upper.x,
        (upper.z * resolution.y + upper.y) * resolution.x + lower.x,
        (upper.z * resolution.y + upper.y) * resolution.x + upper.x,
    };
    return lerp(
        lerp(
            lerp(
                values[indices[0]],
                values[indices[1]],
                amount.x),
            lerp(
                values[indices[2]],
                values[indices[3]],
                amount.x),
            amount.y),
        lerp(
            lerp(
                values[indices[4]],
                values[indices[5]],
                amount.x),
            lerp(
                values[indices[6]],
                values[indices[7]],
                amount.x),
            amount.y),
        amount.z);
}

float noise_hash(float3 point) {
    point =
        frac(
            point *
            0.1031);
    point +=
        dot(
            point,
            point.yzx +
                33.33);
    return frac(
        (
            point.x +
            point.y
        ) *
        point.z);
}

float noise(float3 point) {
    const float3 cell =
        floor(point);
    const float3 fraction =
        frac(point);
    const float3 weight =
        fraction *
        fraction *
        fraction *
        (
            fraction *
            (
                fraction *
                    6.0 -
                15.0
            ) +
            10.0
        );
    return lerp(
        lerp(
            lerp(noise_hash(cell), noise_hash(cell + float3(1.0, 0.0, 0.0)), weight.x),
            lerp(noise_hash(cell + float3(0.0, 1.0, 0.0)), noise_hash(cell + float3(1.0, 1.0, 0.0)), weight.x),
            weight.y),
        lerp(
            lerp(noise_hash(cell + float3(0.0, 0.0, 1.0)), noise_hash(cell + float3(1.0, 0.0, 1.0)), weight.x),
            lerp(noise_hash(cell + float3(0.0, 1.0, 1.0)), noise_hash(cell + float3(1.0, 1.0, 1.0)), weight.x),
            weight.y),
        weight.z);
}

float3 noise_derivative(float3 point) {
    static const float delta = 0.01;
    const float value =
        noise(point);
    return
        (
            float3(
                noise(point + float3(delta, 0.0, 0.0)),
                noise(point + float3(0.0, delta, 0.0)),
                noise(point + float3(0.0, 0.0, delta))) -
            value
        ) /
        delta;
}

float procedural_cloud_density(
    float3 point,
    float4 parameters) {
    float3 cloud_point =
        parameters.z *
        point;
    if (parameters.y > 0.0) {
        float omega =
            0.05 *
            parameters.y;
        float frequency = 10.0;
        [ForceUnroll]
        for (uint index = 0;
             index != 2;
             ++index) {
            cloud_point +=
                omega *
                noise_derivative(
                    frequency *
                    cloud_point);
            omega *= 0.5;
            frequency *= 1.99;
        }
    }
    float density = 0.0;
    float omega = 0.5;
    float frequency = 1.0;
    [ForceUnroll]
    for (uint index = 0;
         index != 5;
         ++index) {
        density +=
            omega *
            noise(
                frequency *
                cloud_point);
        omega *= 0.5;
        frequency *= 1.99;
    }
    density =
        clamp(
            (
                1.0 -
                point.y
            ) *
                4.5 *
                parameters.x *
                density,
            0.0,
            1.0);
    density +=
        2.0 *
        max(
            0.0,
            0.5 -
                point.y);
    return clamp(
        density,
        0.0,
        1.0);
}

float3 blackbody(float temperature) {
    if (temperature <= 0.0)
        return float3(0.0);
    temperature =
        clamp(
            temperature,
            1000.0,
            40000.0) /
        100.0;
    float red;
    float green;
    float blue;
    if (temperature <= 66.0) {
        red = 1.0;
        green =
            saturate(
                (
                    99.4708025861 *
                        log(temperature) -
                    161.1195681661
                ) /
                255.0);
    }
    else {
        red =
            saturate(
                (
                    329.698727446 *
                    pow(
                        temperature -
                            60.0,
                        -0.1332047592)
                ) /
                255.0);
        green =
            saturate(
                (
                    288.1221695283 *
                    pow(
                        temperature -
                            60.0,
                        -0.0755148492)
                ) /
                255.0);
    }
    if (temperature >= 66.0)
        blue = 1.0;
    else if (temperature <= 19.0)
        blue = 0.0;
    else
        blue =
            saturate(
                (
                    138.5177312231 *
                        log(
                            temperature -
                            10.0) -
                    305.0447927307
                ) /
                255.0);
    return float3(
        red,
        green,
        blue) *
        cie_y_integral;
}

float phase_henyey_greenstein(
    float cosine,
    float anisotropy) {
    const float denominator =
        1.0 +
        anisotropy *
            anisotropy +
        2.0 *
            anisotropy *
            cosine;
    return
        (
            1.0 -
            anisotropy *
                anisotropy
        ) /
        (
            4.0 *
            3.14159265358979323846 *
            denominator *
            sqrt(denominator)
        );
}

float volume_density(
    RasterVolume volume,
    float3 world_position,
    out float temperature,
    out float3 sigma_a,
    out float3 sigma_s,
    out float3 emission) {
    const float3 local_position =
        transform_point(
            volume,
            world_position);
    const float3 coordinate =
        (
            local_position -
            volume.bounds_minimum.xyz
        ) /
        (
            volume.bounds_maximum.xyz -
            volume.bounds_minimum.xyz
        );
    if (any(coordinate < 0.0) || any(coordinate > 1.0)) {
        temperature = 0.0;
        sigma_a = float3(0.0);
        sigma_s = float3(0.0);
        emission = float3(0.0);
        return 0.0;
    }
    if (volume.metadata.x == volume_density_grid) {
        StructuredBuffer<float> density_field =
            volume.density;
        const float density =
            sample_scalar(
                density_field,
                volume.metadata.yzw,
                coordinate) *
            volume.scales.x;
        temperature = 0.0;
        if (
            (
                asuint(volume.procedural_parameters.w) &
                1
            ) != 0) {
            StructuredBuffer<float> temperature_field =
                volume.temperature_field;
            temperature =
                sample_scalar(
                    temperature_field,
                    volume.metadata.yzw,
                    coordinate) *
                    volume.scales.w +
                volume.temperature.x;
        }
        sigma_a =
            density *
            volume.sigma_a.xyz;
        sigma_s =
            density *
            volume.sigma_s.xyz;
        float emission_multiplier = 1.0;
        if (
            (
                asuint(volume.procedural_parameters.w) &
                2
            ) != 0) {
            StructuredBuffer<float> emission_scale_field =
                volume.emission_scale_field;
            emission_multiplier =
                sample_scalar(
                    emission_scale_field,
                    volume.metadata.yzw,
                    coordinate);
        }
        emission =
            density *
            volume.emission.xyz *
            volume.scales.y *
            emission_multiplier;
        if (
            volume.temperature.z != 0.0 &&
            temperature >=
                volume.temperature.y)
            emission +=
                density *
                blackbody(temperature) *
                volume.scales.y *
                emission_multiplier;
        return density;
    }
    if (volume.metadata.x == volume_rgb_grid) {
        StructuredBuffer<float3> sigma_a_field =
            volume.sigma_a_field;
        StructuredBuffer<float3> sigma_s_field =
            volume.sigma_s_field;
        StructuredBuffer<float3> emission_field =
            volume.emission_field;
        const uint flags = asuint(volume.procedural_parameters.w);
        const uint color_space = flags >> 8;
        sigma_a = raster_to_linear_srgb(
            (flags & 1) != 0
                ? sample_vector(sigma_a_field, volume.metadata.yzw, coordinate)
                : float3(1.0),
            color_space) * volume.scales.x;
        sigma_s = raster_to_linear_srgb(
            (flags & 2) != 0
                ? sample_vector(sigma_s_field, volume.metadata.yzw, coordinate)
                : float3(1.0),
            color_space) * volume.scales.x;
        emission = (flags & 4) != 0
            ? raster_to_linear_srgb(sample_vector(emission_field, volume.metadata.yzw, coordinate), color_space) * volume.scales.y
            : float3(0.0);
        temperature = 0.0;
        return
            max(
                sigma_a.x +
                    sigma_s.x,
                max(
                    sigma_a.y +
                        sigma_s.y,
                    sigma_a.z +
                        sigma_s.z));
    }
    const float density =
        procedural_cloud_density(
            coordinate,
            volume.procedural_parameters) *
        volume.scales.x;
    temperature = 0.0;
    sigma_a =
        density *
        volume.sigma_a.xyz;
    sigma_s =
        density *
        volume.sigma_s.xyz;
    emission =
        density *
        volume.emission.xyz *
        volume.scales.y;
    return density;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void raster_volume(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel =
        dispatch_thread_id.xy;
    if (
        pixel.x >= push_data.metadata.x ||
        pixel.y >= push_data.metadata.y)
        return;
    if (pixel.x < push_data.film_bounds.x || pixel.y < push_data.film_bounds.y || pixel.x >= push_data.film_bounds.z || pixel.y >= push_data.film_bounds.w) return;
    RWTexture2D<float4> output =
        push_data.output;
    Texture2D<float> depth_texture =
        push_data.depth;
    StructuredBuffer<RasterVolume> volumes =
        push_data.volumes;
    const float2 coordinate =
        (
            float2(pixel) +
            0.5
        ) /
        float2(
            push_data.metadata.xy);
    const float2 ndc =
        float2(
            coordinate.x *
                    2.0 -
                1.0,
            1.0 -
                coordinate.y *
                    2.0);
    const float depth =
        depth_texture.Load(
            int3(
                pixel,
                0));
    const float4 near_clip =
        float4(
            ndc,
            0.0,
            1.0);
    const float4 far_clip =
        float4(
            ndc,
            1.0,
            1.0);
    const float4 surface_clip =
        float4(
            ndc,
            depth,
            1.0);
    const float4 near_homogeneous =
        float4(
            dot(
                push_data.inverse_view_projection_row_0,
                near_clip),
            dot(
                push_data.inverse_view_projection_row_1,
                near_clip),
            dot(
                push_data.inverse_view_projection_row_2,
                near_clip),
            dot(
                push_data.inverse_view_projection_row_3,
                near_clip));
    const float4 far_homogeneous =
        float4(
            dot(
                push_data.inverse_view_projection_row_0,
                far_clip),
            dot(
                push_data.inverse_view_projection_row_1,
                far_clip),
            dot(
                push_data.inverse_view_projection_row_2,
                far_clip),
            dot(
                push_data.inverse_view_projection_row_3,
                far_clip));
    const float4 surface_homogeneous =
        float4(
            dot(
                push_data.inverse_view_projection_row_0,
                surface_clip),
            dot(
                push_data.inverse_view_projection_row_1,
                surface_clip),
            dot(
                push_data.inverse_view_projection_row_2,
                surface_clip),
            dot(
                push_data.inverse_view_projection_row_3,
                surface_clip));
    const float3 ray_origin =
        near_homogeneous.xyz /
        near_homogeneous.w;
    const float3 ray_direction =
        normalize(
            far_homogeneous.xyz /
                far_homogeneous.w -
            ray_origin);
    const float surface_distance =
        depth < 1.0
            ? dot(
                  surface_homogeneous.xyz /
                          surface_homogeneous.w -
                      ray_origin,
                  ray_direction)
            : 1.0e20;
    float minimum = surface_distance;
    float maximum = 0.0;
    bool intersects = false;
    for (uint volume_index = 0;
         volume_index != push_data.metadata.z;
         ++volume_index) {
        float volume_minimum;
        float volume_maximum;
        if (
            !intersect_bounds(
                volumes[volume_index],
                ray_origin,
                ray_direction,
                volume_minimum,
                volume_maximum))
            continue;
        minimum =
            min(
                minimum,
                max(
                    volume_minimum,
                    0.0));
        maximum =
            max(
                maximum,
                min(
                    volume_maximum,
                    surface_distance));
        intersects = true;
    }
    if (!intersects || maximum <= minimum) return;
    static const uint preview_step_count = 64;
    const float step = (maximum - minimum) / float(preview_step_count);
    float3 radiance = float3(0.0);
    float3 transmittance = float3(1.0);
    const float3 incoming_direction = normalize(float3(-0.35, 0.8, 0.45));
    for (uint sample_index = 0; sample_index != preview_step_count; ++sample_index) {
        const float distance = minimum + (float(sample_index) + 0.5) * step;
        const float3 position =
            ray_origin +
            distance *
                ray_direction;
        float3 sigma_a =
            float3(0.0);
        float3 sigma_s =
            float3(0.0);
        float3 emission =
            float3(0.0);
        float anisotropy = 0.0;
        float weight = 0.0;
        for (uint volume_index = 0;
             volume_index != push_data.metadata.z;
             ++volume_index) {
            float temperature;
            float3 local_sigma_a;
            float3 local_sigma_s;
            float3 local_emission;
            const float density =
                volume_density(
                    volumes[volume_index],
                    position,
                    temperature,
                    local_sigma_a,
                    local_sigma_s,
                    local_emission);
            sigma_a +=
                local_sigma_a;
            sigma_s +=
                local_sigma_s;
            emission +=
                local_emission;
            anisotropy +=
                density *
                volumes[volume_index]
                    .scales.z;
            weight += density;
        }
        if (weight == 0.0) {
            continue;
        }
        anisotropy /= weight;
        const float direct = 0.12 + 2.5 * phase_henyey_greenstein(dot(-ray_direction, incoming_direction), anisotropy);
        const float3 source =
            emission +
            sigma_s *
                direct;
        const float3 extinction =
            sigma_a +
            sigma_s;
        const float3 segment_transmittance =
            exp(
                -extinction *
                step);
        radiance +=
            transmittance *
            source *
            (
                float3(1.0) -
                segment_transmittance
            ) /
            max(
                extinction,
                float3(1.0e-6));
        transmittance *=
            segment_transmittance;
        if (
            max(
                transmittance.x,
                max(
                    transmittance.y,
                    transmittance.z)) <
            1.0e-3)
            break;
    }
    const float4 surface =
        output[pixel];
    output[pixel] =
        float4(
            radiance +
                transmittance *
                    surface.xyz,
            surface.w);
}
