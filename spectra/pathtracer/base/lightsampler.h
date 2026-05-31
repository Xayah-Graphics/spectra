#ifndef SPECTRA_PATHTRACER_BASE_LIGHTSAMPLER_H
#define SPECTRA_PATHTRACER_BASE_LIGHTSAMPLER_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/util/taggedptr.h>

#include <string>

namespace spectra
{
    // SampledLight Definition
    struct SampledLight
    {
        Light light;
        Float p = 0;
    };

    class UniformLightSampler;
    class PowerLightSampler;
    class BVHLightSampler;
    class ExhaustiveLightSampler;

    // LightSampler Definition
    class LightSampler : public TaggedPointer<UniformLightSampler, PowerLightSampler,
                                              ExhaustiveLightSampler, BVHLightSampler>
    {
    public:
        // LightSampler Interface
        using TaggedPointer::TaggedPointer;

        static LightSampler Create(const std::string& name, pstd::span<const Light> lights,
                                   Allocator alloc);


        SPECTRA_CPU_GPU pstd::optional<SampledLight> Sample(const LightSampleContext& ctx,
                                                            Float u) const;

        SPECTRA_CPU_GPU Float PMF(const LightSampleContext& ctx, Light light) const;

        SPECTRA_CPU_GPU pstd::optional<SampledLight> Sample(Float u) const;
        SPECTRA_CPU_GPU Float PMF(Light light) const;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_LIGHTSAMPLER_H
