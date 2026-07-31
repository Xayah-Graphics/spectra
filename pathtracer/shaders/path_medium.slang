module path_medium;

import path_random;
import path_scene;
import spectrum;

#define PNANOVDB_HLSL
#include <nanovdb/PNanoVDB.h>

static const int noise_permutation[256] = {
    151, 160, 137, 91, 90, 15, 131, 13,
    201, 95, 96, 53, 194, 233, 7, 225,
    140, 36, 103, 30, 69, 142, 8, 99,
    37, 240, 21, 10, 23, 190, 6, 148,
    247, 120, 234, 75, 0, 26, 197, 62,
    94, 252, 219, 203, 117, 35, 11, 32,
    57, 177, 33, 88, 237, 149, 56, 87,
    174, 20, 125, 136, 171, 168, 68, 175,
    74, 165, 71, 134, 139, 48, 27, 166,
    77, 146, 158, 231, 83, 111, 229, 122,
    60, 211, 133, 230, 220, 105, 92, 41,
    55, 46, 245, 40, 244, 102, 143, 54,
    65, 25, 63, 161, 1, 216, 80, 73,
    209, 76, 132, 187, 208, 89, 18, 169,
    200, 196, 135, 130, 116, 188, 159, 86,
    164, 100, 109, 198, 173, 186, 3, 64,
    52, 217, 226, 250, 124, 123, 5, 202,
    38, 147, 118, 126, 255, 82, 85, 212,
    207, 206, 59, 227, 47, 16, 58, 17,
    182, 189, 28, 42, 223, 183, 170, 213,
    119, 248, 152, 2, 44, 154, 163, 70,
    221, 153, 101, 155, 167, 43, 172, 9,
    129, 22, 39, 253, 19, 98, 108, 110,
    79, 113, 224, 232, 178, 185, 112, 104,
    218, 246, 97, 228, 251, 34, 242, 193,
    238, 210, 144, 12, 191, 179, 162, 241,
    81, 51, 145, 235, 249, 14, 239, 107,
    49, 192, 214, 31, 181, 199, 106, 157,
    184, 84, 204, 176, 115, 121, 50, 45,
    127, 4, 150, 254, 138, 236, 205, 93,
    222, 114, 67, 29, 24, 72, 243, 141,
    128, 195, 78, 66, 215, 61, 156, 180,
};

float noise_gradient(
    int x,
    int y,
    int z,
    float dx,
    float dy,
    float dz) {
    int hash =
        noise_permutation[
            (
                noise_permutation[
                    (
                        noise_permutation[
                            x & 255] +
                        y
                    ) &
                    255] +
                z
            ) &
            255] &
        15;
    const float u =
        hash < 8 ||
        hash == 12 ||
        hash == 13
            ? dx
            : dy;
    const float v =
        hash < 4 ||
        hash == 12 ||
        hash == 13
            ? dy
            : dz;
    return
        (
            (hash & 1) != 0
                ? -u
                : u
        ) +
        (
            (hash & 2) != 0
                ? -v
                : v
        );
}

float noise_weight(float value) {
    const float square =
        value * value;
    const float cube =
        square * value;
    return
        cube *
        (
            value *
                (
                    value * 6.0 -
                    15.0
                ) +
            10.0
        );
}

float gradient_noise(float3 point) {
    point = fmod(
        point,
        float3(1073741824.0));
    const int3 cell =
        int3(floor(point));
    const float3 delta =
        point -
        float3(cell);
    const float weight_000 =
        noise_gradient(
            cell.x,
            cell.y,
            cell.z,
            delta.x,
            delta.y,
            delta.z);
    const float weight_100 =
        noise_gradient(
            cell.x + 1,
            cell.y,
            cell.z,
            delta.x - 1.0,
            delta.y,
            delta.z);
    const float weight_010 =
        noise_gradient(
            cell.x,
            cell.y + 1,
            cell.z,
            delta.x,
            delta.y - 1.0,
            delta.z);
    const float weight_110 =
        noise_gradient(
            cell.x + 1,
            cell.y + 1,
            cell.z,
            delta.x - 1.0,
            delta.y - 1.0,
            delta.z);
    const float weight_001 =
        noise_gradient(
            cell.x,
            cell.y,
            cell.z + 1,
            delta.x,
            delta.y,
            delta.z - 1.0);
    const float weight_101 =
        noise_gradient(
            cell.x + 1,
            cell.y,
            cell.z + 1,
            delta.x - 1.0,
            delta.y,
            delta.z - 1.0);
    const float weight_011 =
        noise_gradient(
            cell.x,
            cell.y + 1,
            cell.z + 1,
            delta.x,
            delta.y - 1.0,
            delta.z - 1.0);
    const float weight_111 =
        noise_gradient(
            cell.x + 1,
            cell.y + 1,
            cell.z + 1,
            delta.x - 1.0,
            delta.y - 1.0,
            delta.z - 1.0);
    const float3 weight =
        float3(
            noise_weight(delta.x),
            noise_weight(delta.y),
            noise_weight(delta.z));
    return
        lerp(
            lerp(
                lerp(
                    weight_000,
                    weight_100,
                    weight.x),
                lerp(
                    weight_010,
                    weight_110,
                    weight.x),
                weight.y),
            lerp(
                lerp(
                    weight_001,
                    weight_101,
                    weight.x),
                lerp(
                    weight_011,
                    weight_111,
                    weight.x),
                weight.y),
            weight.z);
}

float3 gradient_noise_derivative(float3 point) {
    static const float delta = 0.01;
    const float value =
        gradient_noise(point);
    return
        (
            float3(
                gradient_noise(
                    point +
                    float3(delta, 0.0, 0.0)),
                gradient_noise(
                    point +
                    float3(0.0, delta, 0.0)),
                gradient_noise(
                    point +
                    float3(0.0, 0.0, delta))) -
            value
        ) /
        delta;
}

float cloud_density(
    PathVolume volume,
    float3 point) {
    float3 cloud_point =
        volume.procedural_parameters.z *
        point;
    if (
        volume.procedural_parameters.y >
        0.0) {
        float omega =
            0.05 *
            volume.procedural_parameters.y;
        float frequency = 10.0;
        [ForceUnroll]
        for (uint index = 0;
             index != 2;
             ++index) {
            cloud_point +=
                omega *
                gradient_noise_derivative(
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
            gradient_noise(
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
                volume
                    .procedural_parameters
                    .x *
                density,
            0.0,
            1.0);
    density +=
        2.0 *
        max(
            0.0,
            0.5 -
                point.y);
    return
        clamp(
            density,
            0.0,
            1.0);
}

public float spectrum_average(float4 value) {
    return
        (
            value.x +
            value.y +
            value.z +
            value.w
        ) *
        0.25;
}

public bool spectrum_nonzero(float4 value) {
    return any(value != float4(0.0));
}

float3 volume_transform_point(
    PathVolume volume,
    float3 point) {
    return float3(
        dot(
            volume.inverse_transform_row_0,
            float4(point, 1.0)),
        dot(
            volume.inverse_transform_row_1,
            float4(point, 1.0)),
        dot(
            volume.inverse_transform_row_2,
            float4(point, 1.0)));
}

float3 volume_transform_vector(
    PathVolume volume,
    float3 vector) {
    return float3(
        dot(
            volume
                .inverse_transform_row_0
                .xyz,
            vector),
        dot(
            volume
                .inverse_transform_row_1
                .xyz,
            vector),
        dot(
            volume
                .inverse_transform_row_2
                .xyz,
            vector));
}

bool intersect_volume_bounds(
    PathVolume volume,
    float3 origin,
    float3 direction,
    float maximum,
    out float minimum,
    out float clipped_maximum) {
    const float3 inverse_direction =
        1.0 /
        direction;
    const float3 first =
        (
            volume.bounds_minimum.xyz -
            origin
        ) *
        inverse_direction;
    const float3 second =
        (
            volume.bounds_maximum.xyz -
            origin
        ) *
        inverse_direction;
    const float3 near_values =
        min(first, second);
    const float3 far_values =
        max(first, second);
    minimum =
        max(
            0.0,
            max(
                near_values.x,
                max(
                    near_values.y,
                    near_values.z)));
    clipped_maximum =
        min(
            maximum,
            min(
                far_values.x,
                min(
                    far_values.y,
                    far_values.z)));
    return
        minimum <=
        clipped_maximum;
}

float dense_float_value(
    StructuredBuffer<float> values,
    uint3 resolution,
    int3 coordinate) {
    if (
        any(coordinate < int3(0)) ||
        any(
            coordinate >=
            int3(resolution)))
        return 0.0;
    return
        values[
            (
                uint(coordinate.z) *
                    resolution.y +
                uint(coordinate.y)
            ) *
                resolution.x +
            uint(coordinate.x)];
}

float sample_dense_float(
    StructuredBuffer<float> values,
    uint3 resolution,
    float3 point) {
    const float3 sample_position =
        point *
            float3(resolution) -
        0.5;
    const int3 base =
        int3(
            floor(
                sample_position));
    const float3 delta =
        sample_position -
        float3(base);
    return
        lerp(
            lerp(
                lerp(
                    dense_float_value(
                        values,
                        resolution,
                        base),
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(1, 0, 0)),
                    delta.x),
                lerp(
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(0, 1, 0)),
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(1, 1, 0)),
                    delta.x),
                delta.y),
            lerp(
                lerp(
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(0, 0, 1)),
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(1, 0, 1)),
                    delta.x),
                lerp(
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(0, 1, 1)),
                    dense_float_value(
                        values,
                        resolution,
                        base +
                            int3(1, 1, 1)),
                    delta.x),
                delta.y),
            delta.z);
}

float3 dense_rgb_value(
    StructuredBuffer<PathPackedFloat3> values,
    uint3 resolution,
    int3 coordinate) {
    if (
        any(coordinate < int3(0)) ||
        any(
            coordinate >=
            int3(resolution)))
        return float3(0.0);
    const PathPackedFloat3 value =
        values[
            (
                uint(coordinate.z) *
                    resolution.y +
                uint(coordinate.y)
            ) *
                resolution.x +
            uint(coordinate.x)];
    return
        float3(
            value.x,
            value.y,
            value.z);
}

float3 sample_dense_rgb(
    StructuredBuffer<PathPackedFloat3> values,
    uint3 resolution,
    float3 point) {
    const float3 sample_position =
        point *
            float3(resolution) -
        0.5;
    const int3 base =
        int3(
            floor(
                sample_position));
    const float3 delta =
        sample_position -
        float3(base);
    return
        lerp(
            lerp(
                lerp(
                    dense_rgb_value(
                        values,
                        resolution,
                        base),
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(1, 0, 0)),
                    delta.x),
                lerp(
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(0, 1, 0)),
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(1, 1, 0)),
                    delta.x),
                delta.y),
            lerp(
                lerp(
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(0, 0, 1)),
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(1, 0, 1)),
                    delta.x),
                lerp(
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(0, 1, 1)),
                    dense_rgb_value(
                        values,
                        resolution,
                        base +
                            int3(1, 1, 1)),
                    delta.x),
                delta.y),
            delta.z);
}

float nanovdb_value(
    StructuredBuffer<uint> data,
    inout pnanovdb_readaccessor_t accessor,
    int3 coordinate) {
    pnanovdb_coord_t voxel =
        coordinate;
    return
        pnanovdb_read_float(
            data,
            pnanovdb_readaccessor_get_value_address(
                PNANOVDB_GRID_TYPE_FLOAT,
                data,
                accessor,
                voxel));
}

float sample_nanovdb(
    StructuredBuffer<uint> data,
    float3 point) {
    pnanovdb_grid_handle_t grid;
    grid.address =
        pnanovdb_address_null();
    const pnanovdb_tree_handle_t tree =
        pnanovdb_grid_get_tree(
            data,
            grid);
    const pnanovdb_root_handle_t root =
        pnanovdb_tree_get_root(
            data,
            tree);
    pnanovdb_readaccessor_t accessor;
    pnanovdb_readaccessor_init(
        accessor,
        root);
    const float3 index_position =
        pnanovdb_grid_world_to_indexf(
            data,
            grid,
            point);
    const int3 base =
        int3(
            floor(
                index_position));
    const float3 delta =
        index_position -
        float3(base);
    return
        lerp(
            lerp(
                lerp(
                    nanovdb_value(
                        data,
                        accessor,
                        base),
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(1, 0, 0)),
                    delta.x),
                lerp(
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(0, 1, 0)),
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(1, 1, 0)),
                    delta.x),
                delta.y),
            lerp(
                lerp(
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(0, 0, 1)),
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(1, 0, 1)),
                    delta.x),
                lerp(
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(0, 1, 1)),
                    nanovdb_value(
                        data,
                        accessor,
                        base +
                            int3(1, 1, 1)),
                    delta.x),
                delta.y),
            delta.z);
}

float4 normalized_blackbody(
    float temperature,
    float4 wavelengths) {
    if (temperature <= 0.0)
        return float4(0.0);
    const float maximum_wavelength =
        2.8977721e6 /
        temperature;
    const float normalization =
        1.0 /
        blackbody_radiance(
            maximum_wavelength,
            temperature);
    return normalization *
        float4(
            blackbody_radiance(
                wavelengths.x,
                temperature),
            blackbody_radiance(
                wavelengths.y,
                temperature),
            blackbody_radiance(
                wavelengths.z,
                temperature),
            blackbody_radiance(
                wavelengths.w,
                temperature));
}

public struct PathMediumProperties {
    public float4 sigma_a;
    public float4 sigma_s;
    public float4 emission;
    public float anisotropy;
}

float4 medium_spectrum(
    PathSceneBindings bindings,
    uint spectrum_index,
    float4 wavelengths) {
    StructuredBuffer<PathSpectrum> spectra =
        bindings.spectra;
    return
        evaluate_spectrum(
            spectra[spectrum_index],
            bindings.piecewise_spectra,
            bindings.cie_spectra,
            wavelengths);
}

public PathMediumProperties
evaluate_medium(
    PathSceneBindings bindings,
    uint medium_index,
    float3 world_point,
    float4 wavelengths) {
    StructuredBuffer<PathMedium> media =
        bindings.media;
    const PathMedium medium =
        media[medium_index];
    PathMediumProperties result = {
        medium_spectrum(
            bindings,
            medium.spectra.x,
            wavelengths) *
            medium.scales.x,
        medium_spectrum(
            bindings,
            medium.spectra.y,
            wavelengths) *
            medium.scales.x,
        medium_spectrum(
            bindings,
            medium.spectra.z,
            wavelengths) *
            medium.scales.y,
        medium.scales.z,
    };
    if (medium.metadata.x == medium_homogeneous)
        return result;
    StructuredBuffer<PathVolume> volumes =
        bindings.volumes;
    const PathVolume volume =
        volumes[medium.spectra.w];
    const float3 point =
        volume_transform_point(
            volume,
            world_point);
    const float3 normalized_point =
        (
            point -
            volume.bounds_minimum.xyz
        ) /
        (
            volume.bounds_maximum.xyz -
            volume.bounds_minimum.xyz
        );
    if (volume.metadata.x == volume_density_grid) {
        const uint3 resolution =
            volume.metadata.yzw;
        StructuredBuffer<float> density =
            volume.density;
        const float density_value =
            sample_dense_float(
                density,
                resolution,
                normalized_point);
        result.sigma_a *=
            density_value;
        result.sigma_s *=
            density_value;
        float emission_scale = 1.0;
        if (
            (
                volume.majorant_metadata.w &
                2
            ) != 0) {
            StructuredBuffer<float>
                emission_scale_grid =
                    volume.emission_scale;
            emission_scale =
                sample_dense_float(
                    emission_scale_grid,
                    resolution,
                    normalized_point);
        }
        if (
            (
                volume.majorant_metadata.w &
                1
            ) != 0) {
            StructuredBuffer<float> temperature =
                volume.temperature;
            const float value =
                (
                    sample_dense_float(
                        temperature,
                        resolution,
                        normalized_point) -
                    medium.temperature.x
                ) *
                medium.scales.w;
            result.emission =
                value >
                    medium.temperature.y
                    ? emission_scale *
                        medium.scales.y *
                        normalized_blackbody(
                            value,
                            wavelengths)
                    : float4(0.0);
        }
        else
            result.emission *=
                emission_scale;
        return result;
    }
    if (volume.metadata.x == volume_rgb_grid) {
        const uint3 resolution =
            volume.metadata.yzw;
        const uint flags =
            volume.majorant_metadata.w &
            255;
        const uint color_space =
            volume.majorant_metadata.w >>
            8;
        result.sigma_a =
            (
                (flags & 1) != 0
                    ? evaluate_rgb_spectrum(
                          sample_dense_rgb(
                              volume.sigma_a,
                              resolution,
                              normalized_point),
                          1,
                          color_space,
                          bindings
                              .rgb_to_spectrum_tables,
                          bindings.cie_spectra,
                          wavelengths)
                    : float4(1.0)
            ) *
            medium.scales.x;
        result.sigma_s =
            (
                (flags & 2) != 0
                    ? evaluate_rgb_spectrum(
                          sample_dense_rgb(
                              volume.sigma_s,
                              resolution,
                              normalized_point),
                          1,
                          color_space,
                          bindings
                              .rgb_to_spectrum_tables,
                          bindings.cie_spectra,
                          wavelengths)
                    : float4(1.0)
            ) *
            medium.scales.x;
        result.emission =
            (flags & 4) != 0
                ? evaluate_rgb_spectrum(
                      sample_dense_rgb(
                          volume.emission,
                          resolution,
                          normalized_point),
                      2,
                      color_space,
                      bindings
                          .rgb_to_spectrum_tables,
                      bindings.cie_spectra,
                      wavelengths) *
                    medium.scales.y
                : float4(0.0);
        return result;
    }
    if (volume.metadata.x == volume_nanovdb) {
        const float density =
            sample_nanovdb(
                volume.nanovdb_density,
                point);
        result.sigma_a *=
            density;
        result.sigma_s *=
            density;
        result.emission =
            float4(0.0);
        if (
            (
                volume.majorant_metadata.w &
                1
            ) != 0) {
            const float temperature =
                (
                    sample_nanovdb(
                        volume
                            .nanovdb_temperature,
                        point) -
                    medium.temperature.x
                ) *
                medium.scales.w;
            if (
                temperature >
                medium.temperature.y)
                result.emission =
                    medium.scales.y *
                    normalized_blackbody(
                        temperature,
                        wavelengths);
        }
        return result;
    }
    const float density =
        cloud_density(
            volume,
            point);
    result.sigma_a *=
        density;
    result.sigma_s *=
        density;
    result.emission =
        float4(0.0);
    return result;
}

public struct PathMajorantSegment {
    public float minimum;
    public float maximum;
    public float4 sigma;
    public bool valid;
}

public struct PathMajorantIterator {
    public PathVolume volume;
    public float4 sigma_t;
    public float3 local_origin;
    public float3 local_direction;
    public float minimum;
    public float maximum;
    public float3 next_crossing;
    public float3 delta;
    public int3 voxel;
    public int3 step;
    public int3 limit;
    public uint kind;
    public bool exhausted;
}

public PathMajorantIterator
make_majorant_iterator(
    PathSceneBindings bindings,
    uint medium_index,
    float3 world_origin,
    float3 world_direction,
    float maximum,
    float4 wavelengths) {
    StructuredBuffer<PathMedium> media =
        bindings.media;
    const PathMedium medium =
        media[medium_index];
    StructuredBuffer<PathVolume> volumes =
        bindings.volumes;
    PathMajorantIterator result;
    result.volume =
        volumes[0];
    result.sigma_t =
        (
            medium_spectrum(
                bindings,
                medium.spectra.x,
                wavelengths) +
            medium_spectrum(
                bindings,
                medium.spectra.y,
                wavelengths)
        ) *
        medium.scales.x;
    result.local_origin =
        world_origin;
    result.local_direction =
        world_direction;
    result.minimum = 0.0;
    result.maximum =
        maximum;
    result.next_crossing =
        float3(0.0);
    result.delta =
        float3(0.0);
    result.voxel =
        int3(0);
    result.step =
        int3(0);
    result.limit =
        int3(0);
    result.kind =
        medium.metadata.x ==
                medium_volume
            ? volume_procedural_cloud
            : medium_homogeneous;
    result.exhausted = false;
    if (
        medium.metadata.x ==
        medium_homogeneous)
        return result;
    result.volume =
        volumes[medium.spectra.w];
    result.kind =
        result.volume.metadata.x;
    result.local_origin =
        volume_transform_point(
            result.volume,
            world_origin);
    result.local_direction =
        volume_transform_vector(
            result.volume,
            world_direction);
    if (!intersect_volume_bounds(
            result.volume,
            result.local_origin,
            result.local_direction,
            maximum,
            result.minimum,
            result.maximum)) {
        result.exhausted = true;
        return result;
    }
    if (
        result.kind ==
        volume_rgb_grid)
        result.sigma_t =
            float4(
                medium.scales.x);
    if (
        result.kind ==
        volume_procedural_cloud)
        return result;
    const uint3 resolution =
        result.volume
            .majorant_metadata
            .xyz;
    const float3 extent =
        result.volume
            .bounds_maximum
            .xyz -
        result.volume
            .bounds_minimum
            .xyz;
    const float3 grid_origin =
        (
            result.local_origin -
            result.volume
                .bounds_minimum
                .xyz
        ) /
        extent;
    const float3 grid_direction =
        result.local_direction /
        extent;
    const float3 intersection =
        grid_origin +
        result.minimum *
            grid_direction;
    result.voxel =
        int3(
            min(
                uint3(
                    max(
                        intersection,
                        float3(0.0)) *
                    float3(resolution)),
                resolution -
                    uint3(1)));
    [ForceUnroll]
    for (uint axis = 0;
         axis != 3;
         ++axis) {
        if (grid_direction[axis] == 0.0) {
            result.delta[axis] =
                3.402823466e38;
            result.next_crossing[axis] =
                3.402823466e38;
            result.step[axis] = 0;
            result.limit[axis] =
                result.voxel[axis];
            continue;
        }
        result.delta[axis] =
            1.0 /
            (
                abs(
                    grid_direction[axis]) *
                float(
                    resolution[axis])
            );
        if (
            grid_direction[axis] >=
            0.0) {
            const float next_position =
                float(
                    result.voxel[axis] +
                    1) /
                float(
                    resolution[axis]);
            result.next_crossing[axis] =
                result.minimum +
                (
                    next_position -
                    intersection[axis]
                ) /
                grid_direction[axis];
            result.step[axis] = 1;
            result.limit[axis] =
                int(
                    resolution[axis]);
        }
        else {
            const float next_position =
                float(
                    result.voxel[axis]) /
                float(
                    resolution[axis]);
            result.next_crossing[axis] =
                result.minimum +
                (
                    next_position -
                    intersection[axis]
                ) /
                grid_direction[axis];
            result.step[axis] = -1;
            result.limit[axis] = -1;
        }
    }
    return result;
}

public PathMajorantSegment
next_majorant_segment(
    inout PathMajorantIterator iterator) {
    PathMajorantSegment result = {
        0.0,
        0.0,
        float4(0.0),
        false,
    };
    if (
        iterator.exhausted ||
        iterator.minimum >=
            iterator.maximum)
        return result;
    if (
        iterator.kind ==
            medium_homogeneous ||
        iterator.kind ==
            volume_procedural_cloud) {
        result = {
            iterator.minimum,
            iterator.maximum,
            iterator.sigma_t,
            true,
        };
        iterator.exhausted = true;
        return result;
    }
    uint axis = 0;
    if (
        iterator.next_crossing.y <
        iterator.next_crossing.x)
        axis = 1;
    if (
        iterator.next_crossing.z <
        iterator.next_crossing[axis])
        axis = 2;
    const float exit =
        min(
            iterator.maximum,
            iterator.next_crossing[axis]);
    StructuredBuffer<float> majorant =
        iterator.volume.majorant;
    const uint3 resolution =
        iterator.volume
            .majorant_metadata
            .xyz;
    const float density =
        majorant[
            (
                uint(iterator.voxel.z) *
                    resolution.y +
                uint(iterator.voxel.y)
            ) *
                resolution.x +
            uint(iterator.voxel.x)];
    result = {
        iterator.minimum,
        exit,
        iterator.sigma_t *
            density,
        true,
    };
    iterator.minimum = exit;
    if (
        iterator.next_crossing[axis] >
        iterator.maximum)
        iterator.minimum =
            iterator.maximum;
    iterator.voxel[axis] +=
        iterator.step[axis];
    if (
        iterator.voxel[axis] ==
        iterator.limit[axis])
        iterator.minimum =
            iterator.maximum;
    iterator.next_crossing[axis] +=
        iterator.delta[axis];
    return result;
}

public struct PathMediumSample {
    public float4 beta;
    public float4 r_u;
    public float4 r_l;
    public float4 radiance;
    public float3 position;
    public float anisotropy;
    public bool scattered;
    public bool terminated;
}

public PathMediumSample sample_path_medium(
    PathSceneBindings bindings,
    uint medium_index,
    float3 origin,
    float3 direction,
    float maximum,
    float4 wavelengths,
    uint depth,
    uint maximum_depth,
    float4 beta,
    float4 r_u,
    float4 r_l,
    float initial_sample,
    inout Pcg32 generator) {
    PathMediumSample result = {
        beta,
        r_u,
        r_l,
        float4(0.0),
        float3(0.0),
        0.0,
        false,
        false,
    };
    const float direction_length =
        length(direction);
    const float3 normalized_direction =
        direction /
        direction_length;
    PathMajorantIterator iterator =
        make_majorant_iterator(
            bindings,
            medium_index,
            origin,
            normalized_direction,
            maximum *
                direction_length,
            wavelengths);
    float4 transmittance =
        float4(1.0);
    float sample =
        initial_sample;
    float mode_sample =
        pcg_float(generator);
    while (true) {
        const PathMajorantSegment segment =
            next_majorant_segment(
                iterator);
        if (!segment.valid) break;
        if (segment.sigma.x == 0.0) {
            transmittance *=
                exp(
                    -(
                        segment.maximum -
                        segment.minimum
                    ) *
                    segment.sigma);
            continue;
        }
        float minimum =
            segment.minimum;
        while (true) {
            const float distance =
                minimum -
                log(
                    1.0 -
                    sample) /
                    segment.sigma.x;
            sample =
                pcg_float(generator);
            if (
                distance >=
                segment.maximum) {
                transmittance *=
                    exp(
                        -(
                            segment.maximum -
                            minimum
                        ) *
                        segment.sigma);
                break;
            }
            transmittance *=
                exp(
                    -(
                        distance -
                        minimum
                    ) *
                    segment.sigma);
            const float3 point =
                origin +
                distance *
                    normalized_direction;
            const PathMediumProperties
                properties =
                    evaluate_medium(
                        bindings,
                        medium_index,
                        point,
                        wavelengths);
            if (
                !spectrum_nonzero(
                    result.beta)) {
                result.terminated = true;
                return result;
            }
            if (
                depth <
                    maximum_depth &&
                spectrum_nonzero(
                    properties.emission)) {
                const float pdf =
                    segment.sigma.x *
                    transmittance.x;
                const float4 emission_probability =
                    result.r_u *
                    segment.sigma *
                    transmittance /
                    pdf;
                const float denominator =
                    spectrum_average(
                        emission_probability);
                if (denominator != 0.0)
                    result.radiance +=
                        result.beta *
                        transmittance /
                        pdf *
                        properties.sigma_a *
                        properties.emission /
                        denominator;
            }
            const float absorption_probability =
                properties.sigma_a.x /
                segment.sigma.x;
            const float scattering_probability =
                properties.sigma_s.x /
                segment.sigma.x;
            if (
                mode_sample <
                absorption_probability) {
                result.terminated = true;
                return result;
            }
            if (
                mode_sample <
                absorption_probability +
                    scattering_probability) {
                if (
                    depth >=
                    maximum_depth) {
                    result.terminated = true;
                    return result;
                }
                const float pdf =
                    transmittance.x *
                    properties.sigma_s.x;
                result.beta *=
                    transmittance *
                    properties.sigma_s /
                    pdf;
                result.r_u *=
                    transmittance *
                    properties.sigma_s /
                    pdf;
                result.position = point;
                result.anisotropy =
                    properties.anisotropy;
                result.scattered = true;
                return result;
            }
            const float4 null_coefficient =
                max(
                    segment.sigma -
                        properties.sigma_a -
                        properties.sigma_s,
                    float4(0.0));
            const float pdf =
                transmittance.x *
                null_coefficient.x;
            if (pdf == 0.0) {
                result.beta =
                    float4(0.0);
                result.r_u =
                    float4(0.0);
                result.terminated = true;
                return result;
            }
            result.beta *=
                transmittance *
                null_coefficient /
                pdf;
            result.r_u *=
                transmittance *
                null_coefficient /
                pdf;
            result.r_l *=
                transmittance *
                segment.sigma /
                pdf;
            if (
                !spectrum_nonzero(
                    result.beta) ||
                !spectrum_nonzero(
                    result.r_u)) {
                result.terminated = true;
                return result;
            }
            transmittance =
                float4(1.0);
            minimum = distance;
            mode_sample =
                pcg_float(generator);
        }
    }
    if (transmittance.x != 0.0) {
        result.beta *=
            transmittance /
            transmittance.x;
        result.r_u *=
            transmittance /
            transmittance.x;
        result.r_l *=
            transmittance /
            transmittance.x;
    }
    else {
        result.beta =
            float4(0.0);
        result.r_u =
            float4(0.0);
        result.r_l =
            float4(0.0);
        result.terminated = true;
    }
    return result;
}

public struct PathTransmittance {
    public float4 transmittance;
    public float4 r_l;
    public float4 r_u;
}

public PathTransmittance
sample_medium_transmittance(
    PathSceneBindings bindings,
    uint medium_index,
    float3 origin,
    float3 direction,
    float maximum,
    float4 wavelengths,
    float initial_sample,
    inout Pcg32 generator) {
    PathTransmittance result = {
        float4(1.0),
        float4(1.0),
        float4(1.0),
    };
    const float direction_length =
        length(direction);
    const float3 normalized_direction =
        direction /
        direction_length;
    PathMajorantIterator iterator =
        make_majorant_iterator(
            bindings,
            medium_index,
            origin,
            normalized_direction,
            maximum *
                direction_length,
            wavelengths);
    float4 segment_transmittance =
        float4(1.0);
    float sample =
        initial_sample;
    while (true) {
        const PathMajorantSegment segment =
            next_majorant_segment(
                iterator);
        if (!segment.valid) break;
        if (segment.sigma.x == 0.0) {
            segment_transmittance *=
                exp(
                    -(
                        segment.maximum -
                        segment.minimum
                    ) *
                    segment.sigma);
            continue;
        }
        float minimum =
            segment.minimum;
        while (true) {
            const float distance =
                minimum -
                log(
                    1.0 -
                    sample) /
                    segment.sigma.x;
            sample =
                pcg_float(generator);
            if (
                distance >=
                segment.maximum) {
                segment_transmittance *=
                    exp(
                        -(
                            segment.maximum -
                            minimum
                        ) *
                        segment.sigma);
                break;
            }
            segment_transmittance *=
                exp(
                    -(
                        distance -
                        minimum
                    ) *
                    segment.sigma);
            const PathMediumProperties
                properties =
                    evaluate_medium(
                        bindings,
                        medium_index,
                        origin +
                            distance *
                                normalized_direction,
                        wavelengths);
            const float4 null_coefficient =
                max(
                    segment.sigma -
                        properties.sigma_a -
                        properties.sigma_s,
                    float4(0.0));
            const float pdf =
                segment_transmittance.x *
                segment.sigma.x;
            result.transmittance *=
                segment_transmittance *
                null_coefficient /
                pdf;
            result.r_l *=
                segment_transmittance *
                segment.sigma /
                pdf;
            result.r_u *=
                segment_transmittance *
                null_coefficient /
                pdf;
            const float denominator =
                spectrum_average(
                    result.r_l +
                    result.r_u);
            const float maximum_component =
                max(
                    result.transmittance.x,
                    max(
                        result.transmittance.y,
                        max(
                            result
                                .transmittance
                                .z,
                            result
                                .transmittance
                                .w))) /
                denominator;
            if (
                maximum_component <
                0.05) {
                static const float
                    termination_probability =
                        0.75;
                if (
                    pcg_float(generator) <
                    termination_probability) {
                    result.transmittance =
                        float4(0.0);
                    return result;
                }
                result.transmittance /=
                    1.0 -
                    termination_probability;
            }
            if (
                !spectrum_nonzero(
                    result.transmittance))
                return result;
            segment_transmittance =
                float4(1.0);
            minimum = distance;
        }
    }
    if (
        segment_transmittance.x !=
        0.0) {
        result.transmittance *=
            segment_transmittance /
            segment_transmittance.x;
        result.r_l *=
            segment_transmittance /
            segment_transmittance.x;
        result.r_u *=
            segment_transmittance /
            segment_transmittance.x;
    }
    else
        result.transmittance =
            float4(0.0);
    return result;
}
