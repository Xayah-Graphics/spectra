module particle_material;

struct ParticleMaterialPushData {
    DescriptorHandle<StructuredBuffer<uint2>> source;
    DescriptorHandle<RWStructuredBuffer<uint>> destination;
    DescriptorHandle<StructuredBuffer<uint4>> lookup;
    uint2 reserved;
    uint particle_count;
    uint material_lookup_count;
    uint2 reserved_metadata;
}

[vk::push_constant]
uniform ParticleMaterialPushData push_data;

[shader("compute")]
[numthreads(256, 1, 1)]
void map_particle_materials(
    uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint particle = dispatch_thread_id.x;
    if (particle >= push_data.particle_count) return;
    StructuredBuffer<uint2> source = push_data.source;
    StructuredBuffer<uint4> lookup = push_data.lookup;
    RWStructuredBuffer<uint> destination =
        push_data.destination;
    const uint2 id = source[particle];
    uint material = 0;
    for (uint index = 0; index != push_data.material_lookup_count; ++index)
        if (all(lookup[index].xy == id)) {
            material = lookup[index].z;
            break;
        }
    destination[particle] = material;
}
