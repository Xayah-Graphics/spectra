#ifndef SPECTRA_PATHTRACER_BASE_BSSRDF_H
#define SPECTRA_PATHTRACER_BASE_BSSRDF_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <string>

namespace spectra {
    struct BSSRDFSample;
    struct BSSRDFProbeSegment;
    struct SubsurfaceInteraction;
    struct BSSRDFTable;

    // BSSRDF Definition
    class TabulatedBSSRDF;

    class BSSRDF : public TaggedPointer<TabulatedBSSRDF> {
    public:
        // BSSRDF Interface
        using TaggedPointer::TaggedPointer;

        __host__ __device__ pstd::optional<BSSRDFProbeSegment> SampleSp(Float u1, Point2f u2) const;

        BSSRDFSample ProbeIntersectionToSample(const SubsurfaceInteraction& si, ScratchBuffer& scratchBuffer) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_BSSRDF_H
