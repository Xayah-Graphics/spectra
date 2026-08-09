module path_texture;

import path_scene;
import path_spectrum;

struct PathTextureCoordinates {
    float2 st;
    float2 st_dx;
    float2 st_dy;
    float3 point;
    float3 point_dx;
    float3 point_dy;
}

float3 transform_point(
    PathTextureMapping mapping,
    float3 point) {
    return float3(
        dot(
            mapping.transform_row_0,
            float4(
                point,
                1.0)),
        dot(
            mapping.transform_row_1,
            float4(
                point,
                1.0)),
        dot(
            mapping.transform_row_2,
            float4(
                point,
                1.0)));
}

float3 transform_vector(
    PathTextureMapping mapping,
    float3 vector) {
    return float3(
        dot(
            mapping.transform_row_0.xyz,
            vector),
        dot(
            mapping.transform_row_1.xyz,
            vector),
        dot(
            mapping.transform_row_2.xyz,
            vector));
}

PathTextureCoordinates map_texture(
    PathTextureMapping mapping,
    TextureEvaluationContext context) {
    PathTextureCoordinates result = {
        float2(0.0),
        float2(0.0),
        float2(0.0),
        float3(0.0),
        float3(0.0),
        float3(0.0),
    };
    if (mapping.metadata.x == 0) {
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
        transform_point(
            mapping,
            context.position);
    result.point_dx =
        transform_vector(
            mapping,
            context.position_dx);
    result.point_dy =
        transform_vector(
            mapping,
            context.position_dy);
    if (mapping.metadata.x == 1) {
        result.st = float2(
            dot(
                result.point,
                mapping.parameter_0.xyz) +
                mapping.parameter_0.w,
            dot(
                result.point,
                mapping.parameter_1.xyz) +
                mapping.parameter_1.w);
        result.st_dx = float2(
            dot(
                result.point_dx,
                mapping.parameter_0.xyz),
            dot(
                result.point_dx,
                mapping.parameter_1.xyz));
        result.st_dy = float2(
            dot(
                result.point_dy,
                mapping.parameter_0.xyz),
            dot(
                result.point_dy,
                mapping.parameter_1.xyz));
        return result;
    }
    if (mapping.metadata.x == 4) return result;
    const float x2_y2 =
        result.point.x * result.point.x +
        result.point.y * result.point.y;
    if (mapping.metadata.x == 2) {
        const float radial =
            sqrt(
                max(
                    x2_y2,
                    1.0e-20));
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
        result.st = float2(
            acos(
                clamp(
                    direction.z,
                    -1.0,
                    1.0)) /
                3.14159265358979323846,
            atan2(
                direction.y,
                direction.x) <
                    0.0
                ? (
                      atan2(
                          direction.y,
                          direction.x) +
                      2.0 *
                          3.14159265358979323846) /
                      (
                          2.0 *
                          3.14159265358979323846)
                : atan2(
                      direction.y,
                      direction.x) /
                      (
                          2.0 *
                          3.14159265358979323846));
        result.st_dx = float2(
            dot(
                ds_dp,
                result.point_dx),
            dot(
                dt_dp,
                result.point_dx));
        result.st_dy = float2(
            dot(
                ds_dp,
                result.point_dy),
            dot(
                dt_dp,
                result.point_dy));
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
                result.point.x)) /
            (
                2.0 *
                3.14159265358979323846),
        result.point.z);
    result.st_dx = float2(
        dot(
            ds_dp,
            result.point_dx),
        result.point_dx.z);
    result.st_dy = float2(
        dot(
            ds_dp,
            result.point_dy),
        result.point_dy.z);
    return result;
}

float checkerboard_integral(float x) {
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
                    abs(y));
}

float checkerboard_filter(
    float x,
    float radius) {
    if (radius == 0.0)
        return
            (int(floor(x)) & 1) == 0
                ? 1.0
                : -1.0;
    if (
        floor(x - radius) ==
        floor(x + radius))
        return
            (int(floor(x)) & 1) == 0
                ? 1.0
                : -1.0;
    return (
        checkerboard_integral(
            x + radius) -
        2.0 *
            checkerboard_integral(x) +
        checkerboard_integral(
            x - radius)) /
        (
            radius *
            radius);
}

float checkerboard_weight(
    PathTextureMapping mapping,
    TextureEvaluationContext context) {
    const PathTextureCoordinates coordinates =
        map_texture(
            mapping,
            context);
    if (mapping.metadata.x == 4) {
        const float3 radius =
            1.5 *
            max(
                abs(coordinates.point_dx),
                abs(coordinates.point_dy));
        return
            0.5 -
            0.5 *
                checkerboard_filter(
                    coordinates.point.x,
                    radius.x) *
                checkerboard_filter(
                    coordinates.point.y,
                    radius.y) *
                checkerboard_filter(
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
            checkerboard_filter(
                coordinates.st.x,
                radius.x) *
            checkerboard_filter(
                coordinates.st.y,
                radius.y);
}

float4 image_texel(
    Texture2D<float4> image,
    int2 coordinate,
    uint level,
    uint wrap_mode) {
    uint width;
    uint height;
    uint level_count;
    image.GetDimensions(
        level,
        width,
        height,
        level_count);
    if (wrap_mode == 0) {
        coordinate.x =
            (
                coordinate.x %
                    int(width) +
                int(width)
            ) %
            int(width);
        coordinate.y =
            (
                coordinate.y %
                    int(height) +
                int(height)
            ) %
            int(height);
    }
    else if (wrap_mode == 1)
        coordinate =
            clamp(
                coordinate,
                int2(0),
                int2(
                    int(width) - 1,
                    int(height) - 1));
    else if (wrap_mode == 3) {
        if (coordinate.x < 0) {
            coordinate.x =
                -coordinate.x;
            coordinate.y =
                int(height) -
                1 -
                coordinate.y;
        }
        else if (
            coordinate.x >=
            int(width)) {
            coordinate.x =
                2 *
                    int(width) -
                1 -
                coordinate.x;
            coordinate.y =
                int(height) -
                1 -
                coordinate.y;
        }
        if (coordinate.y < 0) {
            coordinate.x =
                int(width) -
                1 -
                coordinate.x;
            coordinate.y =
                -coordinate.y;
        }
        else if (
            coordinate.y >=
            int(height)) {
            coordinate.x =
                int(width) -
                1 -
                coordinate.x;
            coordinate.y =
                2 *
                    int(height) -
                1 -
                coordinate.y;
        }
    }
    else if (
        coordinate.x < 0 ||
        coordinate.y < 0 ||
        coordinate.x >= int(width) ||
        coordinate.y >= int(height))
        return float4(0.0);
    return image.Load(
        int3(
            coordinate,
            int(level)));
}

float4 image_bilerp(
    Texture2D<float4> image,
    SamplerState sampler,
    float2 st,
    uint level) {
    return image.SampleLevel(
        sampler,
        st,
        float(level));
}

float4 image_ewa(
    Texture2D<float4> image,
    float2 st,
    float2 derivative_0,
    float2 derivative_1,
    uint level,
    uint wrap_mode) {
    uint base_width;
    uint base_height;
    uint level_count;
    image.GetDimensions(
        0,
        base_width,
        base_height,
        level_count);
    if (level >= level_count)
        return image_texel(
            image,
            int2(0),
            level_count - 1,
            wrap_mode);
    uint width;
    uint height;
    image.GetDimensions(
        level,
        width,
        height,
        level_count);
    st =
        st *
            float2(
                float(width),
                float(height)) -
        0.5;
    derivative_0 *=
        float2(
            float(width),
            float(height));
    derivative_1 *=
        float2(
            float(width),
            float(height));
    float a =
        derivative_0.y *
            derivative_0.y +
        derivative_1.y *
            derivative_1.y +
        1.0;
    float b =
        -2.0 *
        (
            derivative_0.x *
                derivative_0.y +
            derivative_1.x *
                derivative_1.y
        );
    float c =
        derivative_0.x *
            derivative_0.x +
        derivative_1.x *
            derivative_1.x +
        1.0;
    const float inverse =
        1.0 /
        (
            a * c -
            b * b * 0.25
        );
    a *= inverse;
    b *= inverse;
    c *= inverse;
    const float determinant =
        -b * b +
        4.0 * a * c;
    const float inverse_determinant =
        1.0 /
        determinant;
    const float u_radius =
        2.0 *
        inverse_determinant *
        sqrt(
            max(
                0.0,
                determinant * c));
    const float v_radius =
        2.0 *
        inverse_determinant *
        sqrt(
            max(
                0.0,
                a * determinant));
    const int s_0 =
        int(ceil(st.x - u_radius));
    const int s_1 =
        int(floor(st.x + u_radius));
    const int t_0 =
        int(ceil(st.y - v_radius));
    const int t_1 =
        int(floor(st.y + v_radius));
    float4 sum = float4(0.0);
    float weight_sum = 0.0;
    [loop]
    for (int t = t_0; t <= t_1; ++t) {
        const float tt =
            float(t) -
            st.y;
        [loop]
        for (int s = s_0; s <= s_1; ++s) {
            const float ss =
                float(s) -
                st.x;
            const float radius_squared =
                a * ss * ss +
                b * ss * tt +
                c * tt * tt;
            if (radius_squared >= 1.0)
                continue;
            const uint index =
                min(
                    uint(
                        radius_squared *
                        128.0),
                    127);
            const float weight =
                exp(
                    -2.0 *
                    (
                        float(index) /
                        127.0
                    )) -
                exp(-2.0);
            sum +=
                weight *
                image_texel(
                    image,
                    int2(s, t),
                    level,
                    wrap_mode);
            weight_sum += weight;
        }
    }
    return sum /
        weight_sum;
}

float4 image_texture_value(
    PathImageTexture texture,
    PathTextureMapping mapping,
    TextureEvaluationContext context) {
    Texture2D<float4> image = texture.image;
    SamplerState sampler = texture.sampler;
    PathTextureCoordinates coordinates =
        map_texture(
            mapping,
            context);
    coordinates.st.y =
        1.0 -
        coordinates.st.y;
    coordinates.st_dx.y =
        -coordinates.st_dx.y;
    coordinates.st_dy.y =
        -coordinates.st_dy.y;
    uint width;
    uint height;
    uint level_count;
    image.GetDimensions(
        0,
        width,
        height,
        level_count);
    float4 value;
    if (texture.metadata.z != 3) {
        const float footprint =
            2.0 *
            max(
                max(
                    abs(
                        coordinates
                            .st_dx.x),
                    abs(
                        coordinates
                            .st_dx.y)),
                max(
                    abs(
                        coordinates
                            .st_dy.x),
                    abs(
                        coordinates
                            .st_dy.y)));
        const float level =
            float(level_count - 1) +
            log2(
                max(
                    footprint,
                    1.0e-8));
        if (level >=
            float(level_count - 1))
            value =
                image_texel(
                    image,
                    int2(0),
                    level_count - 1,
                    uint(
                        texture
                            .parameters.w));
        else {
            const uint integer_level =
                uint(
                    max(
                        0.0,
                        floor(level)));
            if (texture.metadata.z == 0 ||
                texture.metadata.z == 1)
                value =
                    image_bilerp(
                        image,
                        sampler,
                        coordinates.st,
                        integer_level);
            else if (integer_level == 0)
                value =
                    image_bilerp(
                        image,
                        sampler,
                        coordinates.st,
                        0);
            else
                value =
                    lerp(
                        image_bilerp(
                            image,
                            sampler,
                            coordinates.st,
                            integer_level),
                        image_bilerp(
                            image,
                            sampler,
                            coordinates.st,
                            integer_level + 1),
                        level -
                            float(
                                integer_level));
        }
    }
    else {
        float2 longer =
            coordinates.st_dx;
        float2 shorter =
            coordinates.st_dy;
        if (dot(longer, longer) <
            dot(shorter, shorter)) {
            const float2 swap = longer;
            longer = shorter;
            shorter = swap;
        }
        const float longer_length =
            length(longer);
        float shorter_length =
            length(shorter);
        if (shorter_length *
                texture.parameters.z <
                longer_length &&
            shorter_length > 0.0) {
            const float scale =
                longer_length /
                (
                    shorter_length *
                    texture.parameters.z
                );
            shorter *= scale;
            shorter_length *= scale;
        }
        if (shorter_length == 0.0)
            value =
                image_bilerp(
                    image,
                    sampler,
                    coordinates.st,
                    0);
        else {
            const float level =
                max(
                    0.0,
                    float(level_count - 1) +
                        log2(
                            shorter_length));
            const uint integer_level =
                uint(floor(level));
            value =
                lerp(
                    image_ewa(
                        image,
                        coordinates.st,
                        longer,
                        shorter,
                        integer_level,
                        uint(
                            texture
                                .parameters.w)),
                    image_ewa(
                        image,
                        coordinates.st,
                        longer,
                        shorter,
                        integer_level + 1,
                        uint(
                            texture
                                .parameters.w)),
                    level -
                        float(
                            integer_level));
        }
    }
    value *= texture.parameters.x;
    if (texture.metadata.w != 0)
        value =
            max(
                float4(0.0),
                float4(1.0) -
                    value);
    return value;
}

public float4 evaluate_environment_image(
    PathSceneBindings bindings,
    uint image_index,
    float2 uv,
    float4 wavelengths,
    bool bilerp) {
    StructuredBuffer<PathImageTexture> images =
        bindings.image_textures;
    const PathImageTexture texture =
        images[image_index];
    Texture2D<float4> image =
        texture.image;
    uint width;
    uint height;
    uint level_count;
    image.GetDimensions(
        0,
        width,
        height,
        level_count);
    float4 rgb;
    if (bilerp) {
        const float2 coordinate =
            uv *
                float2(
                    float(width),
                    float(height)) -
            0.5;
        const int2 first =
            int2(floor(coordinate));
        const float2 offset =
            coordinate -
            float2(first);
        rgb =
            lerp(
                lerp(
                    image_texel(
                        image,
                        first,
                        0,
                        3),
                    image_texel(
                        image,
                        first +
                            int2(1, 0),
                        0,
                        3),
                    offset.x),
                lerp(
                    image_texel(
                        image,
                        first +
                            int2(0, 1),
                        0,
                        3),
                    image_texel(
                        image,
                        first +
                            int2(1, 1),
                        0,
                        3),
                    offset.x),
                offset.y);
    }
    else
        rgb =
            image_texel(
                image,
                int2(
                    min(
                        uint(uv.x *
                            float(width)),
                        width - 1),
                    min(
                        uint(uv.y *
                            float(height)),
                        height - 1)),
                0,
                3);
    return
        evaluate_rgb_spectrum(
            rgb.xyz *
                texture.parameters.x,
            2,
            uint(texture.parameters.y),
            bindings.rgb_to_spectrum_tables,
            bindings.cie_spectra,
            wavelengths);
}

public float4 evaluate_normal_map(
    PathSceneBindings bindings,
    uint handle,
    TextureEvaluationContext context) {
    StructuredBuffer<PathTextureHeader> headers =
        bindings.texture_headers;
    StructuredBuffer<PathTextureMapping> mappings =
        bindings.texture_mappings;
    StructuredBuffer<PathImageTexture> images =
        bindings.image_textures;
    const PathTextureHeader header =
        headers[handle];
    const PathImageTexture texture =
        images[header.local_index];
    const PathTextureCoordinates coordinates =
        map_texture(
            mappings[texture.metadata.x],
            context);
    Texture2D<float4> image = texture.image;
    uint width;
    uint height;
    uint level_count;
    image.GetDimensions(
        0,
        width,
        height,
        level_count);
    const float2 texel =
        float2(
            coordinates.st.x,
            1.0 -
                coordinates.st.y) *
            float2(
                float(width),
                float(height)) -
        0.5;
    const int2 texel_0 =
        int2(floor(texel));
    const float2 weight =
        texel -
        float2(texel_0);
    return lerp(
        lerp(
            image_texel(
                image,
                texel_0,
                0,
                0),
            image_texel(
                image,
                texel_0 +
                    int2(1, 0),
                0,
                0),
            weight.x),
        lerp(
            image_texel(
                image,
                texel_0 +
                    int2(0, 1),
                0,
                0),
            image_texel(
                image,
                texel_0 +
                    int2(1, 1),
                0,
                0),
            weight.x),
        weight.y);
}

[noinline]
public float4 evaluate_texture_program(
    PathSceneBindings bindings,
    uint handle,
    TextureEvaluationContext context,
    float4 wavelengths,
    DescriptorHandle<RWStructuredBuffer<float4>> value_stack,
    uint value_base) {
    if (handle == 0xffffffff) return float4(0.0);
    RWStructuredBuffer<float4> values =
        value_stack;
    StructuredBuffer<PathTextureHeader> headers =
        bindings.texture_headers;
    StructuredBuffer<PathTextureMapping> mappings =
        bindings.texture_mappings;
    StructuredBuffer<PathConstantTexture> constants =
        bindings.constant_textures;
    StructuredBuffer<PathImageTexture> images =
        bindings.image_textures;
    StructuredBuffer<PathCheckerboardTexture>
        checkerboards =
            bindings.checkerboard_textures;
    StructuredBuffer<PathScaleTexture> scales =
        bindings.scale_textures;
    StructuredBuffer<PathMixTexture> mixes =
        bindings.mix_textures;
    StructuredBuffer<PathDirectionMixTexture>
        direction_mixes =
            bindings.direction_mix_textures;
    StructuredBuffer<PathBilerpTexture> bilerps =
        bindings.bilerp_textures;
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    StructuredBuffer<float2> piecewise_spectra =
        bindings.piecewise_spectra;
    ByteAddressBuffer cie_spectra =
        bindings.cie_spectra;
    uint value_count = 0;
    const PathTextureHeader root =
        headers[handle];
    [loop]
    for (uint program_index = 0;
         program_index != root.program.y;
         ++program_index) {
        const PathTextureHeader header =
            headers[
                root.program.x +
                program_index];
        float4 value = float4(0.0);
        if (header.kind == texture_constant) {
            const PathConstantTexture texture =
                constants[header.local_index];
            value =
                header.value_kind == 0
                    ? float4(texture.scalar)
                    : evaluate_spectrum(
                          spectra[texture.spectrum],
                          piecewise_spectra,
                          cie_spectra,
                          wavelengths);
        }
        else if (header.kind == texture_image) {
            const PathImageTexture texture =
                images[header.local_index];
            const float4 sample_value =
                image_texture_value(
                    texture,
                    mappings[texture.metadata.x],
                    context);
            if (header.value_kind == 0) {
                const uint channel =
                    texture.metadata.y;
                const float scalar =
                    channel == 0
                        ? sample_value.x
                    : channel == 1
                        ? sample_value.y
                    : channel == 2
                        ? sample_value.z
                    : channel == 3
                        ? sample_value.w
                    : channel == 4
                        ? (
                              sample_value.x +
                              sample_value.y +
                              sample_value.z
                          ) /
                          3.0
                        : dot(
                              sample_value.xyz,
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
            else
                value =
                    evaluate_rgb_spectrum(
                        sample_value.xyz,
                        header.spectrum_type,
                        uint(texture.parameters.y),
                        bindings.rgb_to_spectrum_tables,
                        cie_spectra,
                        wavelengths);
        }
        else if (
            header.kind ==
            texture_checkerboard) {
            const PathCheckerboardTexture texture =
                checkerboards[header.local_index];
            const float amount =
                checkerboard_weight(
                    mappings[texture.data.z],
                    context);
            const float4 second =
                values[
                    value_base +
                    --value_count];
            const float4 first =
                values[
                    value_base +
                    --value_count];
            value =
                lerp(
                    first,
                    second,
                    amount);
        }
        else if (header.kind == texture_scale) {
            const float4 second =
                values[
                    value_base +
                    --value_count];
            const float4 first =
                values[
                    value_base +
                    --value_count];
            value =
                first *
                second;
        }
        else if (header.kind == texture_mix) {
            const float amount =
                values[
                    value_base +
                    --value_count]
                    .x;
            const float4 second =
                values[
                    value_base +
                    --value_count];
            const float4 first =
                values[
                    value_base +
                    --value_count];
            value =
                lerp(
                    first,
                    second,
                    amount);
        }
        else if (
            header.kind ==
            texture_direction_mix) {
            const PathDirectionMixTexture texture =
                direction_mixes[
                    header.local_index];
            const float amount =
                abs(
                    dot(
                        context.normal,
                        texture.direction.xyz));
            const float4 second =
                values[
                    value_base +
                    --value_count];
            const float4 first =
                values[
                    value_base +
                    --value_count];
            value =
                amount *
                    first +
                (
                    1.0 -
                    amount) *
                    second;
        }
        else {
            const PathBilerpTexture texture =
                bilerps[header.local_index];
            const float2 st =
                map_texture(
                    mappings[texture.data.x],
                    context)
                    .st;
            if (header.value_kind == 0)
                value = float4(
                    (
                        1.0 -
                        st.x) *
                        (
                            1.0 -
                            st.y) *
                        texture.scalars.x +
                    st.x *
                        (
                            1.0 -
                            st.y) *
                        texture.scalars.z +
                    (
                        1.0 -
                        st.x) *
                        st.y *
                        texture.scalars.y +
                    st.x *
                        st.y *
                        texture.scalars.w);
            else
                value =
                    (
                        1.0 -
                        st.x) *
                        (
                            1.0 -
                            st.y) *
                        evaluate_spectrum(
                            spectra[
                                texture.spectra.x],
                            piecewise_spectra,
                            cie_spectra,
                            wavelengths) +
                    st.x *
                        (
                            1.0 -
                            st.y) *
                        evaluate_spectrum(
                            spectra[
                                texture.spectra.z],
                            piecewise_spectra,
                            cie_spectra,
                            wavelengths) +
                    (
                        1.0 -
                        st.x) *
                        st.y *
                        evaluate_spectrum(
                            spectra[
                                texture.spectra.y],
                            piecewise_spectra,
                            cie_spectra,
                            wavelengths) +
                    st.x *
                        st.y *
                        evaluate_spectrum(
                            spectra[
                                texture.spectra.w],
                            piecewise_spectra,
                            cie_spectra,
                            wavelengths);
        }
        values[
            value_base +
            value_count++] =
            value;
    }
    return values[value_base];
}
