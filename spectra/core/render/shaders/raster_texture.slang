module raster_texture;

import raster_scene;

public struct RasterTextureContext {
    public float3 position;
    public float3 normal;
    public float2 texture_coordinates;
    public float3 position_dx;
    public float3 position_dy;
    public float2 texture_coordinates_dx;
    public float2 texture_coordinates_dy;
}

struct RasterTextureCoordinates {
    float2 st;
    float2 st_dx;
    float2 st_dy;
    float3 point;
    float3 point_dx;
    float3 point_dy;
}

float3 raster_transform_point(
    RasterTextureMapping mapping,
    float3 point) {
    return float3(
        dot(mapping.transform_row_0, float4(point, 1.0)),
        dot(mapping.transform_row_1, float4(point, 1.0)),
        dot(mapping.transform_row_2, float4(point, 1.0)));
}

float3 raster_transform_vector(
    RasterTextureMapping mapping,
    float3 vector) {
    return float3(
        dot(mapping.transform_row_0.xyz, vector),
        dot(mapping.transform_row_1.xyz, vector),
        dot(mapping.transform_row_2.xyz, vector));
}

RasterTextureCoordinates raster_map_texture(
    RasterTextureMapping mapping,
    RasterTextureContext context) {
    RasterTextureCoordinates result = {
        float2(0.0),
        float2(0.0),
        float2(0.0),
        float3(0.0),
        float3(0.0),
        float3(0.0),
    };
    if (mapping.metadata.x == mapping_uv) {
        result.st =
            context.texture_coordinates *
                mapping.parameter_0.xy +
            mapping.parameter_0.zw;
        result.st_dx =
            context.texture_coordinates_dx *
            mapping.parameter_0.xy;
        result.st_dy =
            context.texture_coordinates_dy *
            mapping.parameter_0.xy;
        return result;
    }
    result.point =
        raster_transform_point(
            mapping,
            context.position);
    result.point_dx =
        raster_transform_vector(
            mapping,
            context.position_dx);
    result.point_dy =
        raster_transform_vector(
            mapping,
            context.position_dy);
    if (mapping.metadata.x == mapping_planar) {
        result.st = float2(
            dot(result.point, mapping.parameter_0.xyz) +
                mapping.parameter_0.w,
            dot(result.point, mapping.parameter_1.xyz) +
                mapping.parameter_1.w);
        result.st_dx = float2(
            dot(result.point_dx, mapping.parameter_0.xyz),
            dot(result.point_dx, mapping.parameter_1.xyz));
        result.st_dy = float2(
            dot(result.point_dy, mapping.parameter_0.xyz),
            dot(result.point_dy, mapping.parameter_1.xyz));
        return result;
    }
    if (mapping.metadata.x == mapping_checkerboard_3d) return result;
    const float x2_y2 =
        result.point.x * result.point.x +
        result.point.y * result.point.y;
    if (mapping.metadata.x == mapping_spherical) {
        const float radial =
            sqrt(max(x2_y2, 1.0e-20));
        const float length_squared =
            x2_y2 +
            result.point.z * result.point.z;
        const float3 ds_dp =
            float3(
                -result.point.y,
                result.point.x,
                0.0) /
            max(
                2.0 *
                    3.14159265358979323846 *
                    x2_y2,
                1.0e-20);
        const float3 dt_dp =
            float3(
                result.point.x *
                    result.point.z /
                    radial,
                result.point.y *
                    result.point.z /
                    radial,
                -radial) /
            max(
                3.14159265358979323846 *
                    length_squared,
                1.0e-20);
        const float3 direction =
            normalize(result.point);
        float phi =
            atan2(
                direction.y,
                direction.x);
        if (phi < 0.0)
            phi +=
                2.0 *
                3.14159265358979323846;
        result.st = float2(
            phi /
                (
                    2.0 *
                    3.14159265358979323846
                ),
            acos(
                clamp(
                    direction.z,
                    -1.0,
                    1.0)) /
                3.14159265358979323846);
        result.st_dx = float2(
            dot(ds_dp, result.point_dx),
            dot(dt_dp, result.point_dx));
        result.st_dy = float2(
            dot(ds_dp, result.point_dy),
            dot(dt_dp, result.point_dy));
        return result;
    }
    const float3 ds_dp =
        float3(
            -result.point.y,
            result.point.x,
            0.0) /
        max(
            2.0 *
                3.14159265358979323846 *
                x2_y2,
            1.0e-20);
    result.st = float2(
        (
            3.14159265358979323846 +
            atan2(
                result.point.y,
                result.point.x)
        ) /
            (
                2.0 *
                3.14159265358979323846
            ),
        result.point.z);
    result.st_dx = float2(
        dot(ds_dp, result.point_dx),
        result.point_dx.z);
    result.st_dy = float2(
        dot(ds_dp, result.point_dy),
        result.point_dy.z);
    return result;
}

float raster_checkerboard_integral(float x) {
    const float y =
        x * 0.5 -
        floor(x * 0.5) -
        0.5;
    return
        x * 0.5 +
        y *
            (
                1.0 -
                2.0 *
                    abs(y)
            );
}

float raster_checkerboard_filter(
    float x,
    float radius) {
    if (radius == 0.0)
        return
            (int(floor(x)) & 1) == 0
                ? 1.0
                : -1.0;
    if (floor(x - radius) == floor(x + radius))
        return
            (int(floor(x)) & 1) == 0
                ? 1.0
                : -1.0;
    return
        (
            raster_checkerboard_integral(x + radius) -
            2.0 *
                raster_checkerboard_integral(x) +
            raster_checkerboard_integral(x - radius)
        ) /
        (
            radius *
            radius
        );
}

float raster_checkerboard_weight(
    RasterTextureMapping mapping,
    RasterTextureContext context) {
    const RasterTextureCoordinates coordinates =
        raster_map_texture(
            mapping,
            context);
    if (mapping.metadata.x == mapping_checkerboard_3d) {
        const float3 radius =
            1.5 *
            max(
                abs(coordinates.point_dx),
                abs(coordinates.point_dy));
        return
            0.5 -
            0.5 *
                raster_checkerboard_filter(
                    coordinates.point.x,
                    radius.x) *
                raster_checkerboard_filter(
                    coordinates.point.y,
                    radius.y) *
                raster_checkerboard_filter(
                    coordinates.point.z,
                    radius.z);
    }
    const float2 radius =
        1.5 *
        max(
            abs(coordinates.st_dx),
            abs(coordinates.st_dy));
    return
        0.5 -
        0.5 *
            raster_checkerboard_filter(
                coordinates.st.x,
                radius.x) *
            raster_checkerboard_filter(
                coordinates.st.y,
                radius.y);
}

public float3 raster_to_linear_srgb(
    float3 value,
    uint color_space) {
    if (color_space == 1)
        return float3(
            1.660491 * value.x -
                0.587641 * value.y -
                0.072850 * value.z,
            -0.124550 * value.x +
                1.132900 * value.y -
                0.008349 * value.z,
            -0.018151 * value.x -
                0.100579 * value.y +
                1.118730 * value.z);
    if (color_space == 2)
        return float3(
            2.521686 * value.x -
                1.134130 * value.y -
                0.387556 * value.z,
            -0.276479 * value.x +
                1.372719 * value.y -
                0.096240 * value.z,
            -0.015378 * value.x -
                0.152975 * value.y +
                1.168353 * value.z);
    return value;
}

public float3 raster_from_linear_srgb(
    float3 value,
    uint color_space) {
    if (color_space == 1)
        return float3(
            0.627404 * value.x + 0.329283 * value.y + 0.043313 * value.z,
            0.069097 * value.x + 0.919540 * value.y + 0.011362 * value.z,
            0.016391 * value.x + 0.088013 * value.y + 0.895595 * value.z);
    if (color_space == 2)
        return float3(
            0.439701 * value.x + 0.382978 * value.y + 0.177335 * value.z,
            0.089792 * value.x + 0.813423 * value.y + 0.096762 * value.z,
            0.017544 * value.x + 0.111544 * value.y + 0.870704 * value.z);
    return value;
}

float4 raster_image_value(
    RasterImageTexture texture,
    RasterTextureMapping mapping,
    RasterTextureContext context) {
    Texture2D<float4> image = texture.image;
    SamplerState sampler = texture.sampler;
    RasterTextureCoordinates coordinates =
        raster_map_texture(
            mapping,
            context);
    coordinates.st.y =
        1.0 -
        coordinates.st.y;
    coordinates.st_dx.y =
        -coordinates.st_dx.y;
    coordinates.st_dy.y =
        -coordinates.st_dy.y;
    float4 value =
        image.SampleGrad(
            sampler,
            coordinates.st,
            coordinates.st_dx,
            coordinates.st_dy);
    value *= texture.parameters.x;
    if (texture.metadata.w != 0)
        value =
            max(
                float4(0.0),
                float4(1.0) -
                    value);
    return value;
}

[noinline]
public float4 evaluate_raster_texture(
    RasterSceneBindings bindings,
    uint handle,
    RasterTextureContext context) {
    if (handle == 0xffffffff) return float4(0.0);
    StructuredBuffer<RasterTextureHeader> headers =
        bindings.texture_headers;
    StructuredBuffer<RasterTextureMapping> mappings =
        bindings.texture_mappings;
    StructuredBuffer<RasterConstantTexture> constants =
        bindings.constant_textures;
    StructuredBuffer<RasterImageTexture> images =
        bindings.image_textures;
    StructuredBuffer<RasterCompositeTexture> checkerboards =
        bindings.checkerboard_textures;
    StructuredBuffer<RasterCompositeTexture> scales =
        bindings.scale_textures;
    StructuredBuffer<RasterCompositeTexture> mixes =
        bindings.mix_textures;
    StructuredBuffer<RasterDirectionMixTexture> direction_mixes =
        bindings.direction_mix_textures;
    StructuredBuffer<RasterBilerpTexture> bilerps =
        bindings.bilerp_textures;
    float4 values[32];
    const RasterTextureHeader root =
        headers[handle];
    [loop]
    for (uint program_index = 0;
         program_index != root.program.y;
         ++program_index) {
        const RasterTextureHeader header =
            headers[
                root.program.x +
                program_index];
        float4 value = float4(0.0);
        if (header.metadata.x == texture_constant)
            value =
                constants[
                    header.metadata.y]
                    .value;
        else if (header.metadata.x == texture_image) {
            const RasterImageTexture texture =
                images[
                    header.metadata.y];
            const float4 sampled =
                raster_image_value(
                    texture,
                    mappings[
                        texture.metadata.x],
                    context);
            if (header.metadata.z == 0) {
                const uint channel =
                    texture.metadata.y;
                const float scalar =
                    channel == 0
                        ? sampled.x
                    : channel == 1
                        ? sampled.y
                    : channel == 2
                        ? sampled.z
                    : channel == 3
                        ? sampled.w
                    : channel == 4
                        ? (
                              sampled.x +
                              sampled.y +
                              sampled.z
                          ) /
                          3.0
                        : dot(
                              sampled.xyz,
                              uint(
                                  texture
                                      .parameters.y) == 1
                                  ? float3(
                                        0.262700,
                                        0.677998,
                                        0.059302)
                              : uint(
                                    texture
                                        .parameters.y) == 2
                                  ? float3(
                                        0.3439664498,
                                        0.7281660966,
                                        -0.0721325464)
                                  : float3(
                                        0.212671,
                                        0.715160,
                                        0.072169));
                value = float4(scalar);
            }
            else {
                value.xyz =
                    raster_to_linear_srgb(
                        sampled.xyz,
                        uint(
                            texture
                                .parameters.y));
                value.w = sampled.w;
            }
        }
        else if (header.metadata.x == texture_checkerboard) {
            const RasterCompositeTexture texture =
                checkerboards[
                    header.metadata.y];
            const float amount =
                raster_checkerboard_weight(
                    mappings[
                        texture.data.z],
                    context);
            const float4 second =
                values[header.program.y];
            const float4 first =
                values[header.program.x];
            value =
                lerp(
                    first,
                    second,
                    amount);
        }
        else if (header.metadata.x == texture_scale) {
            const float4 second =
                values[header.program.y];
            const float4 first =
                values[header.program.x];
            value =
                first *
                second;
        }
        else if (header.metadata.x == texture_mix) {
            const float amount =
                values[header.reserved.x]
                    .x;
            const float4 second =
                values[header.program.y];
            const float4 first =
                values[header.program.x];
            value =
                lerp(
                    first,
                    second,
                    amount);
        }
        else if (header.metadata.x == texture_direction_mix) {
            const RasterDirectionMixTexture texture =
                direction_mixes[
                    header.metadata.y];
            const float amount =
                abs(
                    dot(
                        context.normal,
                        texture.direction.xyz));
            const float4 second =
                values[header.program.y];
            const float4 first =
                values[header.program.x];
            value =
                amount *
                    first +
                (
                    1.0 -
                    amount
                ) *
                    second;
        }
        else {
            const RasterBilerpTexture texture =
                bilerps[
                    header.metadata.y];
            const float2 st =
                raster_map_texture(
                    mappings[
                        texture.data.x],
                    context)
                    .st;
            value =
                (
                    1.0 -
                    st.x
                ) *
                    (
                        1.0 -
                        st.y
                    ) *
                    texture.values[0] +
                st.x *
                    (
                        1.0 -
                        st.y
                    ) *
                    texture.values[2] +
                (
                    1.0 -
                    st.x
                ) *
                    st.y *
                    texture.values[1] +
                st.x *
                    st.y *
                    texture.values[3];
        }
        values[program_index] =
            value;
    }
    return values[root.program.y - 1];
}

[noinline]
public float3 evaluate_raster_normal_map(
    RasterSceneBindings bindings,
    uint handle,
    RasterTextureContext context) {
    StructuredBuffer<RasterTextureHeader> headers =
        bindings.texture_headers;
    StructuredBuffer<RasterTextureMapping> mappings =
        bindings.texture_mappings;
    StructuredBuffer<RasterImageTexture> images =
        bindings.image_textures;
    const RasterTextureHeader header =
        headers[handle];
    const RasterImageTexture texture =
        images[
            header.metadata.y];
    return
        raster_image_value(
            texture,
            mappings[
                texture.metadata.x],
            context)
            .xyz;
}
