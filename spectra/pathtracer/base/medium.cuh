#ifndef SPECTRA_PATHTRACER_BASE_MEDIUM_H
#define SPECTRA_PATHTRACER_BASE_MEDIUM_H

#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/rng.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <string>
#include <vector>

namespace spectra {
    class ParameterDictionary;
    class Ray;
    class Transform;
    struct FileLoc;

    // PhaseFunctionSample Definition
    struct PhaseFunctionSample {
        Float p;
        Vector3f wi;
        Float pdf;
    };

    // PhaseFunction Definition
    class HGPhaseFunction;

    class PhaseFunction : public TaggedPointer<HGPhaseFunction> {
    public:
        // PhaseFunction Interface
        using TaggedPointer::TaggedPointer;


        __host__ __device__ Float p(Vector3f wo, Vector3f wi) const;

        __host__ __device__ pstd::optional<PhaseFunctionSample> Sample_p(Vector3f wo, Point2f u) const;

        __host__ __device__ Float PDF(Vector3f wo, Vector3f wi) const;
    };

    class HomogeneousMedium;
    class GridMedium;
    class RGBGridMedium;
    class CloudMedium;
    class NanoVDBMedium;

    struct MediumProperties;

    // RayMajorantSegment Definition
    struct RayMajorantSegment {
        Float tMin, tMax;
        SampledSpectrum sigma_maj;
    };

    // RayMajorantIterator Definition
    class HomogeneousMajorantIterator;
    class DDAMajorantIterator;

    class RayMajorantIterator : public TaggedPointer<HomogeneousMajorantIterator, DDAMajorantIterator> {
    public:
        using TaggedPointer::TaggedPointer;

        __host__ __device__ pstd::optional<RayMajorantSegment> Next();
    };

    // Medium Definition
    class Medium : public TaggedPointer< // Medium Types
                       HomogeneousMedium, GridMedium, RGBGridMedium, CloudMedium, NanoVDBMedium

                       > {
    public:
        // Medium Interface
        using TaggedPointer::TaggedPointer;

        static Medium Create(const std::string& name, const ParameterDictionary& parameters, const Transform& renderFromMedium, const FileLoc* loc, Allocator alloc);


        __host__ __device__ bool IsEmissive() const;

        __host__ __device__ MediumProperties SamplePoint(Point3f p, const SampledWavelengths& lambda) const;

        // Medium Public Methods
        RayMajorantIterator SampleRay(Ray ray, Float tMax, const SampledWavelengths& lambda, ScratchBuffer& buf) const;
    };

    // MediumInterface Definition
    struct MediumInterface {
        // MediumInterface Public Methods

        MediumInterface() = default;
        __host__ __device__ MediumInterface(Medium medium) : inside(medium), outside(medium) {}

        __host__ __device__ MediumInterface(Medium inside, Medium outside) : inside(inside), outside(outside) {}

        __host__ __device__ bool IsMediumTransition() const {
            return inside != outside;
        }

        // MediumInterface Public Members
        Medium inside, outside;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_MEDIUM_H
