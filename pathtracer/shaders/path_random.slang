module path_random;

public static const float sampler_one_minus_epsilon =
    0.99999994039535522461;
public static const float sampler_uint_scale =
    2.32830643653869628906e-10;

public struct Pcg32 {
    uint64_t state;
    uint64_t increment;
}

public uint64_t mix_bits(uint64_t value) {
    value ^= value >> 31;
    value *= 0x7fb5d329728ea185ull;
    value ^= value >> 27;
    value *= 0x81dadef4bc2dd44dull;
    value ^= value >> 33;
    return value;
}

public uint64_t murmur_block(
    uint64_t hash,
    uint64_t key) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    key *= multiplier;
    key ^= key >> 47;
    key *= multiplier;
    hash ^= key;
    hash *= multiplier;
    return hash;
}

public uint64_t murmur_finish(uint64_t hash) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    hash ^= hash >> 47;
    hash *= multiplier;
    hash ^= hash >> 47;
    return hash;
}

public uint64_t hash_2(
    uint first,
    uint second) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    uint64_t hash =
        uint64_t(8) *
        multiplier;
    hash = murmur_block(
        hash,
        uint64_t(first) |
            (uint64_t(second) << 32));
    return murmur_finish(hash);
}

public uint64_t hash_3(
    uint first,
    uint second,
    uint third) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    uint64_t hash =
        uint64_t(12) *
        multiplier;
    hash = murmur_block(
        hash,
        uint64_t(first) |
            (uint64_t(second) << 32));
    hash ^= uint64_t(third);
    hash *= multiplier;
    return murmur_finish(hash);
}

public uint64_t hash_4(
    uint first,
    uint second,
    uint third,
    uint fourth) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    uint64_t hash =
        uint64_t(16) *
        multiplier;
    hash = murmur_block(
        hash,
        uint64_t(first) |
            (uint64_t(second) << 32));
    hash = murmur_block(
        hash,
        uint64_t(third) |
            (uint64_t(fourth) << 32));
    return murmur_finish(hash);
}

public float hash_float_3(
    uint first,
    uint second,
    uint third) {
    return
        float(
            uint(
                hash_3(
                    first,
                    second,
                    third))) *
        sampler_uint_scale;
}

public float hash_float_6(
    uint first,
    uint second,
    uint third,
    uint fourth,
    uint fifth,
    uint sixth) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    uint64_t hash =
        uint64_t(24) *
        multiplier;
    hash = murmur_block(
        hash,
        uint64_t(first) |
            (uint64_t(second) << 32));
    hash = murmur_block(
        hash,
        uint64_t(third) |
            (uint64_t(fourth) << 32));
    hash = murmur_block(
        hash,
        uint64_t(fifth) |
            (uint64_t(sixth) << 32));
    return
        float(
            uint(
                murmur_finish(hash))) *
        sampler_uint_scale;
}

public float hash_float_material_choice(
    float3 position,
    float3 outgoing,
    uint2 first_material,
    uint2 second_material) {
    static const uint64_t multiplier =
        0xc6a4a7935bd1e995ull;
    uint64_t hash =
        uint64_t(40) *
        multiplier;
    hash = murmur_block(
        hash,
        uint64_t(asuint(position.x)) |
            (
                uint64_t(asuint(position.y)) <<
                32
            ));
    hash = murmur_block(
        hash,
        uint64_t(asuint(position.z)) |
            (
                uint64_t(asuint(outgoing.x)) <<
                32
            ));
    hash = murmur_block(
        hash,
        uint64_t(asuint(outgoing.y)) |
            (
                uint64_t(asuint(outgoing.z)) <<
                32
            ));
    hash = murmur_block(
        hash,
        uint64_t(first_material.x) |
            (
                uint64_t(first_material.y) <<
                32
            ));
    hash = murmur_block(
        hash,
        uint64_t(second_material.x) |
            (
                uint64_t(second_material.y) <<
                32
            ));
    return
        float(
            uint(
                murmur_finish(hash))) *
        sampler_uint_scale;
}

public uint permutation_element(
    uint index,
    uint length,
    uint seed) {
    uint mask = length - 1;
    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask |= mask >> 8;
    mask |= mask >> 16;
    do {
        index ^= seed;
        index *= 0xe170893d;
        index ^= seed >> 16;
        index ^= (index & mask) >> 4;
        index ^= seed >> 8;
        index *= 0x0929eb3f;
        index ^= seed >> 23;
        index ^= (index & mask) >> 1;
        index *= 1 | seed >> 27;
        index *= 0x6935fa69;
        index ^= (index & mask) >> 11;
        index *= 0x74dcb303;
        index ^= (index & mask) >> 2;
        index *= 0x9e501cc3;
        index ^= (index & mask) >> 2;
        index *= 0xc860a3df;
        index &= mask;
        index ^= index >> 5;
    } while (index >= length);
    return (index + seed) % length;
}

public uint pcg_next(inout Pcg32 generator) {
    static const uint64_t multiplier =
        0x5851f42d4c957f2dull;
    const uint64_t previous =
        generator.state;
    generator.state =
        previous *
            multiplier +
        generator.increment;
    const uint shifted =
        uint(
            (
                (
                    previous >> 18
                ) ^
                previous
            ) >>
            27);
    const uint rotation =
        uint(previous >> 59);
    const uint inverse_rotation =
        (
            (
                ~rotation
            ) +
            1
        ) &
        31;
    return
        (shifted >> rotation) |
        (shifted << inverse_rotation);
}

public Pcg32 make_seeded_pcg(
    uint64_t sequence,
    uint64_t seed) {
    Pcg32 generator;
    generator.state = 0;
    generator.increment =
        (sequence << 1) |
        1;
    pcg_next(generator);
    generator.state +=
        seed;
    pcg_next(generator);
    return generator;
}

public Pcg32 make_pcg(uint64_t sequence) {
    return make_seeded_pcg(
        sequence,
        mix_bits(sequence));
}

public Pcg32 ray_segment_pcg(
    float3 origin,
    float maximum_distance,
    float3 direction) {
    return make_seeded_pcg(
        hash_4(
            asuint(origin.x),
            asuint(origin.y),
            asuint(origin.z),
            asuint(maximum_distance)),
        hash_3(
            asuint(direction.x),
            asuint(direction.y),
            asuint(direction.z)));
}

public Pcg32 ray_pcg(
    float3 origin,
    float3 direction) {
    return make_seeded_pcg(
        hash_3(
            asuint(origin.x),
            asuint(origin.y),
            asuint(origin.z)),
        hash_3(
            asuint(direction.x),
            asuint(direction.y),
            asuint(direction.z)));
}

public void advance_pcg(
    inout Pcg32 generator,
    uint64_t delta) {
    static const uint64_t multiplier =
        0x5851f42d4c957f2dull;
    uint64_t current_multiplier =
        multiplier;
    uint64_t current_increment =
        generator.increment;
    uint64_t accumulated_multiplier = 1;
    uint64_t accumulated_increment = 0;
    while (delta > 0) {
        if ((delta & 1) != 0) {
            accumulated_multiplier *=
                current_multiplier;
            accumulated_increment =
                accumulated_increment *
                    current_multiplier +
                current_increment;
        }
        current_increment =
            (
                current_multiplier +
                1
            ) *
            current_increment;
        current_multiplier *=
            current_multiplier;
        delta >>= 1;
    }
    generator.state =
        accumulated_multiplier *
            generator.state +
        accumulated_increment;
}

public float pcg_float(inout Pcg32 generator) {
    return min(
        float(pcg_next(generator)) *
            sampler_uint_scale,
        sampler_one_minus_epsilon);
}
