module path_sampler;

import path_random;
import queue;

static const uint sampler_independent = 0;
static const uint sampler_stratified = 1;
static const uint sampler_halton = 2;
static const uint sampler_sobol = 3;
static const uint sampler_padded_sobol = 4;
static const uint sampler_z_sobol = 5;
static const uint sampler_pmj02bn = 6;

static const uint randomization_none = 0;
static const uint randomization_permute_digits = 1;
static const uint randomization_fast_owen = 2;

Pcg32 pixel_pcg(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    Pcg32 generator =
        make_pcg(
            hash_3(
                pixel.x,
                pixel.y,
                parameters
                    .sampler_metadata
                    .w));
    advance_pcg(
        generator,
        uint64_t(
            parameters.sample_index) *
                uint64_t(65536) +
            dimension);
    return generator;
}

uint table_word(
    StructuredBuffer<uint> table,
    uint index) {
    return table[index];
}

uint64_t table_uint64(
    StructuredBuffer<uint> table,
    uint word_index) {
    return
        uint64_t(
            table_word(
                table,
                word_index)) |
        (
            uint64_t(
                table_word(
                    table,
                    word_index + 1)) <<
            32
        );
}

uint table_offset(
    StructuredBuffer<uint> table,
    uint header_index) {
    return table_word(
        table,
        header_index);
}

uint prime(
    StructuredBuffer<uint> table,
    uint dimension) {
    return table_word(
        table,
        table_offset(
            table,
            10) +
        dimension);
}

float radical_inverse(
    StructuredBuffer<uint> table,
    uint dimension,
    uint64_t value) {
    const uint base =
        prime(
            table,
            dimension);
    const uint64_t limit =
        ~uint64_t(0) /
            base -
        base;
    const float reciprocal_base =
        1.0 /
        float(base);
    float reciprocal_power = 1.0;
    uint64_t reversed = 0;
    while (
        value != 0 &&
        reversed < limit) {
        const uint64_t next =
            value /
            base;
        const uint64_t digit =
            value -
            next *
                base;
        reversed =
            reversed *
                base +
            digit;
        reciprocal_power *=
            reciprocal_base;
        value = next;
    }
    return min(
        float(reversed) *
            reciprocal_power,
        sampler_one_minus_epsilon);
}

float scrambled_radical_inverse(
    StructuredBuffer<uint> table,
    uint dimension,
    uint64_t value,
    uint seed) {
    const uint base =
        prime(
            table,
            dimension);
    const uint64_t limit =
        ~uint64_t(0) /
            base -
        base;
    const float reciprocal_base =
        1.0 /
        float(base);
    float reciprocal_power = 1.0;
    uint64_t reversed = 0;
    uint digit_index = 0;
    while (
        1.0 -
            float(base - 1) *
                reciprocal_power <
            1.0 &&
        reversed < limit) {
        const uint64_t next =
            value /
            base;
        const uint digit =
            uint(
                value -
                next *
                    base);
        reversed =
            reversed *
                base +
            permutation_element(
                digit,
                base,
                uint(
                    hash_3(
                        base,
                        digit_index,
                        seed)));
        reciprocal_power *=
            reciprocal_base;
        ++digit_index;
        value = next;
    }
    return min(
        float(reversed) *
            reciprocal_power,
        sampler_one_minus_epsilon);
}

float owen_radical_inverse(
    StructuredBuffer<uint> table,
    uint dimension,
    uint64_t value,
    uint hash) {
    const uint base =
        prime(
            table,
            dimension);
    const uint64_t limit =
        ~uint64_t(0) /
            base -
        base;
    const float reciprocal_base =
        1.0 /
        float(base);
    float reciprocal_power = 1.0;
    uint64_t reversed = 0;
    while (
        1.0 -
            reciprocal_power <
            1.0 &&
        reversed < limit) {
        const uint64_t next =
            value /
            base;
        uint digit =
            uint(
                value -
                next *
                    base);
        digit =
            permutation_element(
                digit,
                base,
                uint(
                    mix_bits(
                        uint64_t(hash) ^
                        reversed)));
        reversed =
            reversed *
                base +
            digit;
        reciprocal_power *=
            reciprocal_base;
        value = next;
    }
    return min(
        float(reversed) *
            reciprocal_power,
        sampler_one_minus_epsilon);
}

uint inverse_radical_inverse(
    uint64_t inverse,
    uint base,
    uint digit_count) {
    uint64_t index = 0;
    for (
        uint digit_index = 0;
        digit_index < digit_count;
        ++digit_index) {
        const uint64_t digit =
            inverse %
            base;
        inverse /=
            base;
        index =
            index *
                base +
            digit;
    }
    return uint(index);
}

uint multiplicative_inverse(
    uint value,
    uint modulus) {
    for (
        uint candidate = 1;
        candidate < modulus;
        ++candidate)
        if (
            (
                value *
                candidate
            ) %
                modulus ==
            1)
            return candidate;
    return 0;
}

uint64_t halton_index(
    WavefrontParameters parameters,
    uint2 pixel) {
    uint2 base_scales =
        uint2(1);
    uint2 base_exponents =
        uint2(0);
    [ForceUnroll]
    for (
        uint axis = 0;
        axis < 2;
        ++axis) {
        const uint base =
            axis == 0
                ? 2
                : 3;
        const uint resolution =
            min(
                parameters.extent[axis],
                128);
        while (
            base_scales[axis] <
            resolution) {
            base_scales[axis] *=
                base;
            ++base_exponents[axis];
        }
    }
    const uint stride =
        base_scales.x *
        base_scales.y;
    uint64_t index = 0;
    if (stride > 1) {
        const uint2 local_pixel =
            pixel %
            uint2(128);
        const uint2 inverses =
            uint2(
                multiplicative_inverse(
                    base_scales.y,
                    base_scales.x),
                multiplicative_inverse(
                    base_scales.x,
                    base_scales.y));
        const uint2 bases =
            uint2(2, 3);
        [ForceUnroll]
        for (
            uint axis = 0;
            axis < 2;
            ++axis) {
            const uint offset =
                inverse_radical_inverse(
                    local_pixel[axis],
                    bases[axis],
                    base_exponents[axis]);
            index +=
                uint64_t(offset) *
                uint64_t(
                    stride /
                    base_scales[axis]) *
                uint64_t(inverses[axis]);
        }
        index %=
            stride;
    }
    return
        index +
        uint64_t(
            parameters.sample_index) *
            stride;
}

float halton_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    dimension =
        max(
            dimension,
            2);
    if (dimension >= 1000)
        dimension = 2;
    const uint64_t index =
        halton_index(
            parameters,
            pixel);
    const uint randomization =
        parameters
            .sampler_metadata
            .y;
    if (
        randomization ==
        randomization_none)
        return radical_inverse(
            table,
            dimension,
            index);
    if (
        randomization ==
        randomization_permute_digits)
        return scrambled_radical_inverse(
            table,
            dimension,
            index,
            parameters
                .sampler_metadata
                .w);
    return owen_radical_inverse(
        table,
        dimension,
        index,
        uint(
            mix_bits(
                1 +
                (
                    uint64_t(dimension) <<
                    4
                ))));
}

float2 halton_pixel_sample(
    WavefrontParameters parameters,
    uint2 pixel) {
    uint2 base_scales =
        uint2(1);
    uint2 base_exponents =
        uint2(0);
    [ForceUnroll]
    for (
        uint axis = 0;
        axis < 2;
        ++axis) {
        const uint base =
            axis == 0
                ? 2
                : 3;
        const uint resolution =
            min(
                parameters.extent[axis],
                128);
        while (
            base_scales[axis] <
            resolution) {
            base_scales[axis] *=
                base;
            ++base_exponents[axis];
        }
    }
    const uint64_t index =
        halton_index(
            parameters,
            pixel);
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return float2(
        radical_inverse(
            table,
            0,
            index >>
                base_exponents.x),
        radical_inverse(
            table,
            1,
            index /
                base_scales.y));
}

uint reverse_bits(uint value) {
    return reversebits(value);
}

uint fast_owen(
    uint value,
    uint seed) {
    value =
        reverse_bits(value);
    value ^=
        value *
        0x3d20adea;
    value +=
        seed;
    value *=
        (
            seed >>
            16
        ) |
        1;
    value ^=
        value *
        0x05526c56;
    value ^=
        value *
        0x53a22864;
    return reverse_bits(value);
}

uint full_owen(
    uint value,
    uint seed) {
    if ((seed & 1) != 0)
        value ^=
            1u <<
            31;
    for (
        uint bit = 1;
        bit < 32;
        ++bit) {
        const uint mask =
            ~uint(0) <<
            (
                32 -
                bit
            );
        if (
            (
                uint(
                    mix_bits(
                        (
                            value &
                            mask
                        ) ^
                        seed)) &
                (
                    1u <<
                    bit
                )
            ) !=
            0)
            value ^=
                1u <<
                (
                    31 -
                    bit
                );
    }
    return value;
}

uint randomize_sobol(
    uint value,
    uint randomization,
    uint seed) {
    if (
        randomization ==
        randomization_none)
        return value;
    if (
        randomization ==
        randomization_permute_digits)
        return value ^ seed;
    if (
        randomization ==
        randomization_fast_owen)
        return fast_owen(
            value,
            seed);
    return full_owen(
        value,
        seed);
}

float sobol_sample(
    StructuredBuffer<uint> table,
    uint64_t index,
    uint dimension,
    uint randomization,
    uint seed) {
    uint value = 0;
    uint column =
        table_offset(
            table,
            4) +
        dimension *
            52;
    while (index != 0) {
        if ((index & 1) != 0)
            value ^=
                table_word(
                    table,
                    column);
        index >>=
            1;
        ++column;
    }
    value =
        randomize_sobol(
            value,
            randomization,
            seed);
    return min(
        float(value) *
            sampler_uint_scale,
        sampler_one_minus_epsilon);
}

uint round_up_power_of_two(uint value) {
    uint result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

uint integer_log_2(uint value) {
    uint result = 0;
    while (value > 1) {
        value >>=
            1;
        ++result;
    }
    return result;
}

uint64_t sobol_interval_index(
    StructuredBuffer<uint> table,
    uint log_scale,
    uint sample_index,
    uint2 pixel) {
    if (log_scale == 0)
        return sample_index;
    const uint twice_scale =
        log_scale <<
        1;
    uint64_t index =
        uint64_t(sample_index) <<
        twice_scale;
    uint64_t frame =
        sample_index;
    uint64_t delta = 0;
    uint column = 0;
    const uint matrix_offset =
        table_offset(
            table,
            6);
    while (frame != 0) {
        if ((frame & 1) != 0)
            delta ^=
                table_uint64(
                    table,
                    matrix_offset +
                        (
                            (
                                log_scale -
                                1
                            ) *
                            52 +
                            column
                        ) *
                        2);
        frame >>=
            1;
        ++column;
    }
    uint64_t value =
        (
            (
                uint64_t(pixel.x) <<
                log_scale
            ) |
            pixel.y
        ) ^
        delta;
    column = 0;
    const uint inverse_offset =
        table_offset(
            table,
            8);
    while (value != 0) {
        if ((value & 1) != 0)
            index ^=
                table_uint64(
                    table,
                    inverse_offset +
                        (
                            (
                                log_scale -
                                1
                            ) *
                            52 +
                            column
                        ) *
                        2);
        value >>=
            1;
        ++column;
    }
    return index;
}

uint64_t sobol_index(
    WavefrontParameters parameters,
    uint2 pixel) {
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    const uint scale =
        round_up_power_of_two(
            max(
                parameters.extent.x,
                parameters.extent.y));
    return sobol_interval_index(
        table,
        integer_log_2(scale),
        parameters.sample_index,
        pixel);
}

float sobol_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    dimension =
        max(
            dimension,
            2);
    if (dimension >= 1024)
        dimension = 2;
    const uint randomization =
        parameters
            .sampler_metadata
            .y;
    const uint seed =
        uint(
            hash_2(
                dimension,
                parameters
                    .sampler_metadata
                    .w));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return sobol_sample(
        table,
        sobol_index(
            parameters,
            pixel),
        dimension,
        randomization,
        seed);
}

float2 sobol_pixel_sample(
    WavefrontParameters parameters,
    uint2 pixel) {
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    const uint scale =
        round_up_power_of_two(
            max(
                parameters.extent.x,
                parameters.extent.y));
    const uint64_t index =
        sobol_interval_index(
            table,
            integer_log_2(scale),
            parameters.sample_index,
            pixel);
    return clamp(
        float2(
            sobol_sample(
                table,
                index,
                0,
                randomization_none,
                0),
            sobol_sample(
                table,
                index,
                1,
                randomization_none,
                0)) *
            scale -
            float2(pixel),
        0.0,
        sampler_one_minus_epsilon);
}

float padded_sobol_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint64_t hash =
        hash_4(
            pixel.x,
            pixel.y,
            dimension,
            parameters
                .sampler_metadata
                .w);
    const uint index =
        permutation_element(
            parameters.sample_index,
            parameters
                .sampler_metadata
                .z,
            uint(hash));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return sobol_sample(
        table,
        index,
        0,
        parameters
            .sampler_metadata
            .y,
        uint(hash >> 32));
}

float2 padded_sobol_dimension_2(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint64_t hash =
        hash_4(
            pixel.x,
            pixel.y,
            dimension,
            parameters
                .sampler_metadata
                .w);
    const uint index =
        permutation_element(
            parameters.sample_index,
            parameters
                .sampler_metadata
                .z,
            uint(hash));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return float2(
        sobol_sample(
            table,
            index,
            0,
            parameters
                .sampler_metadata
                .y,
            uint(hash)),
        sobol_sample(
            table,
            index,
            1,
            parameters
                .sampler_metadata
                .y,
            uint(hash >> 32)));
}

uint64_t morton_2(uint2 pixel) {
    uint64_t result = 0;
    [ForceUnroll]
    for (
        uint bit = 0;
        bit < 32;
        ++bit) {
        result |=
            uint64_t(
                (
                    pixel.x >>
                    bit
                ) &
                1) <<
            (
                2 *
                bit
            );
        result |=
            uint64_t(
                (
                    pixel.y >>
                    bit
                ) &
                1) <<
            (
                2 *
                    bit +
                1
            );
    }
    return result;
}

uint z_permutation(
    uint permutation,
    uint digit) {
    static const uint4 permutations[24] = {
        uint4(0, 1, 2, 3),
        uint4(0, 1, 3, 2),
        uint4(0, 2, 1, 3),
        uint4(0, 2, 3, 1),
        uint4(0, 3, 2, 1),
        uint4(0, 3, 1, 2),
        uint4(1, 0, 2, 3),
        uint4(1, 0, 3, 2),
        uint4(1, 2, 0, 3),
        uint4(1, 2, 3, 0),
        uint4(1, 3, 2, 0),
        uint4(1, 3, 0, 2),
        uint4(2, 1, 0, 3),
        uint4(2, 1, 3, 0),
        uint4(2, 0, 1, 3),
        uint4(2, 0, 3, 1),
        uint4(2, 3, 0, 1),
        uint4(2, 3, 1, 0),
        uint4(3, 1, 2, 0),
        uint4(3, 1, 0, 2),
        uint4(3, 2, 1, 0),
        uint4(3, 2, 0, 1),
        uint4(3, 0, 2, 1),
        uint4(3, 0, 1, 2)
    };
    return permutations[permutation][digit];
}

uint64_t z_sobol_index(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint log_samples =
        integer_log_2(
            parameters
                .sampler_metadata
                .z);
    const uint resolution =
        round_up_power_of_two(
            max(
                parameters.extent.x,
                parameters.extent.y));
    const uint base_4_digits =
        integer_log_2(
            resolution) +
        (
            log_samples +
            1
        ) /
        2;
    const uint64_t morton =
        (
            morton_2(pixel) <<
            log_samples
        ) |
        parameters.sample_index;
    uint64_t index = 0;
    const bool power_of_two_samples =
        (
            log_samples &
            1
        ) !=
        0;
    const uint last_digit =
        power_of_two_samples
            ? 1
            : 0;
    for (
        int digit_index =
            int(base_4_digits) -
            1;
        digit_index >=
            int(last_digit);
        --digit_index) {
        const uint shift =
            2 *
                uint(digit_index) -
            (
                power_of_two_samples
                    ? 1
                    : 0
            );
        uint digit =
            uint(
                (
                    morton >>
                    shift
                ) &
                3);
        const uint64_t higher_digits =
            morton >>
            (
                shift +
                2
            );
        const uint permutation =
            uint(
                (
                    mix_bits(
                        higher_digits ^
                        uint64_t(
                            uint(0x55555555) *
                            dimension)) >>
                    24
                ) %
                uint64_t(24));
        digit =
            z_permutation(
                permutation,
                digit);
        index |=
            uint64_t(digit) <<
            shift;
    }
    if (power_of_two_samples) {
        const uint digit =
            uint(morton & 1);
        index |=
            digit ^
            uint(
                mix_bits(
                    (
                        morton >>
                        1
                    ) ^
                    uint64_t(
                        uint(0x55555555) *
                        dimension)) &
                1);
    }
    return index;
}

float z_sobol_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint64_t index =
        z_sobol_index(
            parameters,
            pixel,
            dimension);
    ++dimension;
    const uint seed =
        uint(
            hash_2(
                dimension,
                parameters
                    .sampler_metadata
                    .w));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return sobol_sample(
        table,
        index,
        0,
        parameters
            .sampler_metadata
            .y,
        seed);
}

float2 z_sobol_dimension_2(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint64_t index =
        z_sobol_index(
            parameters,
            pixel,
            dimension);
    dimension += 2;
    const uint64_t hash =
        hash_2(
            dimension,
            parameters
                .sampler_metadata
                .w);
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return float2(
        sobol_sample(
            table,
            index,
            0,
            parameters
                .sampler_metadata
                .y,
            uint(hash)),
        sobol_sample(
            table,
            index,
            1,
            parameters
                .sampler_metadata
                .y,
            uint(hash >> 32)));
}

float blue_noise(
    StructuredBuffer<uint> table,
    uint texture,
    uint2 pixel) {
    texture %=
        48;
    pixel %=
        uint2(128);
    return
        float(
            table_word(
                table,
                table_offset(
                    table,
                    14) +
                    texture *
                        128 *
                        128 +
                    pixel.x *
                        128 +
                    pixel.y)) /
        65535.0;
}

float2 pmj_sample(
    StructuredBuffer<uint> table,
    uint set_index,
    uint sample_index) {
    const uint offset =
        table_offset(
            table,
            12) +
        (
            (
                (
                    set_index %
                    5
                ) *
                65536
            ) +
            (
                sample_index %
                65536
            )
        ) *
        2;
    return float2(
        table_word(
            table,
            offset),
        table_word(
            table,
            offset + 1)) *
        sampler_uint_scale;
}

float pmj_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint64_t hash =
        hash_4(
            pixel.x,
            pixel.y,
            dimension,
            parameters
                .sampler_metadata
                .w);
    const uint index =
        permutation_element(
            parameters.sample_index,
            parameters
                .sampler_metadata
                .z,
            uint(hash));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    return min(
        (
            float(index) +
            blue_noise(
                table,
                dimension,
                pixel)
        ) /
            parameters
                .sampler_metadata
                .z,
        sampler_one_minus_epsilon);
}

float2 pmj_dimension_2(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    uint index =
        parameters.sample_index;
    const uint instance =
        dimension /
        2;
    if (instance >= 5)
        index =
            permutation_element(
                parameters.sample_index,
                parameters
                    .sampler_metadata
                    .z,
                uint(
                    hash_4(
                        pixel.x,
                        pixel.y,
                        dimension,
                        parameters
                            .sampler_metadata
                            .w)));
    StructuredBuffer<uint> table =
        parameters.sampling_tables;
    float2 sample =
        pmj_sample(
            table,
            instance,
            index) +
        float2(
            blue_noise(
                table,
                dimension,
                pixel),
            blue_noise(
                table,
                dimension + 1,
                pixel));
    if (sample.x >= 1.0)
        sample.x -= 1.0;
    if (sample.y >= 1.0)
        sample.y -= 1.0;
    return min(
        sample,
        sampler_one_minus_epsilon);
}

float independent_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    Pcg32 generator =
        pixel_pcg(
            parameters,
            pixel,
            dimension);
    return pcg_float(
        generator);
}

float2 independent_dimension_2(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    Pcg32 generator =
        pixel_pcg(
            parameters,
            pixel,
            dimension);
    return float2(
        pcg_float(generator),
        pcg_float(generator));
}

float stratified_dimension(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint samples =
        parameters
            .sampler_metadata
            .z;
    const uint stratum =
        permutation_element(
            parameters.sample_index,
            samples,
            uint(
                hash_4(
                    pixel.x,
                    pixel.y,
                    dimension,
                    parameters
                        .sampler_metadata
                        .w)));
    Pcg32 generator =
        pixel_pcg(
            parameters,
            pixel,
            dimension);
    const float delta =
        parameters
                    .sampler_parameters
                    .z !=
                0
            ? pcg_float(generator)
            : 0.5;
    return
        (
            float(stratum) +
            delta
        ) /
        samples;
}

float2 stratified_dimension_2(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint samples =
        parameters
            .sampler_metadata
            .z;
    const uint stratum =
        permutation_element(
            parameters.sample_index,
            samples,
            uint(
                hash_4(
                    pixel.x,
                    pixel.y,
                    dimension,
                    parameters
                        .sampler_metadata
                        .w)));
    Pcg32 generator =
        pixel_pcg(
            parameters,
            pixel,
            dimension);
    const float2 delta =
        parameters
                    .sampler_parameters
                    .z !=
                0
            ? float2(
                pcg_float(generator),
                pcg_float(generator))
            : float2(0.5);
    const uint x_strata =
        parameters
            .sampler_parameters
            .x;
    const uint y_strata =
        parameters
            .sampler_parameters
            .y;
    return
        (
            float2(
                stratum %
                    x_strata,
                stratum /
                    x_strata) +
            delta
        ) /
        float2(
            x_strata,
            y_strata);
}

public float path_sample_1d(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint kind =
        parameters
            .sampler_metadata
            .x;
    if (kind == sampler_independent)
        return independent_dimension(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_stratified)
        return stratified_dimension(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_halton)
        return halton_dimension(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_sobol)
        return sobol_dimension(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_padded_sobol)
        return padded_sobol_dimension(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_z_sobol)
        return z_sobol_dimension(
            parameters,
            pixel,
            dimension);
    return pmj_dimension(
        parameters,
        pixel,
        max(
            dimension,
            2));
}

public float2 path_sample_2d(
    WavefrontParameters parameters,
    uint2 pixel,
    uint dimension) {
    const uint kind =
        parameters
            .sampler_metadata
            .x;
    if (kind == sampler_independent)
        return independent_dimension_2(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_stratified)
        return stratified_dimension_2(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_halton)
        return float2(
            halton_dimension(
                parameters,
                pixel,
                dimension),
            halton_dimension(
                parameters,
                pixel,
                dimension + 1));
    if (kind == sampler_sobol)
        return float2(
            sobol_dimension(
                parameters,
                pixel,
                dimension),
            sobol_dimension(
                parameters,
                pixel,
                dimension + 1));
    if (kind == sampler_padded_sobol)
        return padded_sobol_dimension_2(
            parameters,
            pixel,
            dimension);
    if (kind == sampler_z_sobol)
        return z_sobol_dimension_2(
            parameters,
            pixel,
            dimension);
    return pmj_dimension_2(
        parameters,
        pixel,
        max(
            dimension,
            2));
}

public float2 path_pixel_sample(
    WavefrontParameters parameters,
    uint2 pixel) {
    const uint kind =
        parameters
            .sampler_metadata
            .x;
    if (kind == sampler_halton)
        return halton_pixel_sample(
            parameters,
            pixel);
    if (kind == sampler_sobol)
        return sobol_pixel_sample(
            parameters,
            pixel);
    if (kind == sampler_pmj02bn) {
        StructuredBuffer<float2>
            pixel_samples =
                parameters
                    .pmj_pixel_samples;
        const uint tile_size =
            parameters
                .sampler_parameters
                .w;
        const uint2 local_pixel =
            pixel %
            tile_size;
        return pixel_samples[
            (
                local_pixel.x +
                local_pixel.y *
                    tile_size
            ) *
                parameters
                    .sampler_metadata
                    .z +
            parameters.sample_index];
    }
    return path_sample_2d(
        parameters,
        pixel,
        1);
}

public uint path_bounce_dimension(
    uint bounce,
    uint offset) {
    return
        6 +
        7 *
            bounce +
        offset;
}
