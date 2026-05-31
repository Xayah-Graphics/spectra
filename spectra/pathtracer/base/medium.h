#ifndef SPECTRA_PATHTRACER_BASE_MEDIUM_H
#define SPECTRA_PATHTRACER_BASE_MEDIUM_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/rng.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/taggedptr.h>

#include <string>
#include <vector>

namespace spectra
{
    class ParameterDictionary;
    class Ray;
    class Transform;
    struct FileLoc;

    // PhaseFunctionSample Definition
    struct PhaseFunctionSample
    {
        Float p;
        Vector3f wi;
        Float pdf;
    };

    // PhaseFunction Definition
    class HGPhaseFunction;

    class PhaseFunction : public TaggedPointer<HGPhaseFunction>
    {
    public:
        // PhaseFunction Interface
        using TaggedPointer::TaggedPointer;


        SPECTRA_CPU_GPU Float p(Vector3f wo, Vector3f wi) const;

        SPECTRA_CPU_GPU pstd::optional<PhaseFunctionSample> Sample_p(Vector3f wo,
                                                                     Point2f u) const;

        SPECTRA_CPU_GPU Float PDF(Vector3f wo, Vector3f wi) const;
    };

    class HomogeneousMedium;
    class GridMedium;
    class RGBGridMedium;
    class CloudMedium;
    class NanoVDBMedium;

    struct MediumProperties;

    // RayMajorantSegment Definition
    struct RayMajorantSegment
    {
        Float tMin, tMax;
        SampledSpectrum sigma_maj;
    };

    // RayMajorantIterator Definition
    class HomogeneousMajorantIterator;
    class DDAMajorantIterator;

    class RayMajorantIterator
        : public TaggedPointer<HomogeneousMajorantIterator, DDAMajorantIterator>
    {
    public:
        using TaggedPointer::TaggedPointer;

        SPECTRA_CPU_GPU
        pstd::optional<RayMajorantSegment> Next();
    };

    // Medium Definition
    class Medium
        : public TaggedPointer< // Medium Types
            HomogeneousMedium, GridMedium, RGBGridMedium, CloudMedium, NanoVDBMedium

        >
    {
    public:
        // Medium Interface
        using TaggedPointer::TaggedPointer;

        static Medium Create(const std::string& name, const ParameterDictionary& parameters,
                             const Transform& renderFromMedium, const FileLoc* loc,
                             Allocator alloc);


        SPECTRA_CPU_GPU
        bool IsEmissive() const;

        SPECTRA_CPU_GPU
        MediumProperties SamplePoint(Point3f p, const SampledWavelengths& lambda) const;

        // Medium Public Methods
        RayMajorantIterator SampleRay(Ray ray, Float tMax, const SampledWavelengths& lambda,
                                      ScratchBuffer& buf) const;
    };

    // MediumInterface Definition
    struct MediumInterface
    {
        // MediumInterface Public Methods

        MediumInterface() = default;
        SPECTRA_CPU_GPU
        MediumInterface(Medium medium) : inside(medium), outside(medium)
        {
        }

        SPECTRA_CPU_GPU
        MediumInterface(Medium inside, Medium outside) : inside(inside), outside(outside)
        {
        }

        SPECTRA_CPU_GPU
        bool IsMediumTransition() const { return inside != outside; }

        // MediumInterface Public Members
        Medium inside, outside;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_MEDIUM_H
