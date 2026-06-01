#ifndef SPECTRA_PATHTRACER_BASE_BSSRDF_H
#define SPECTRA_PATHTRACER_BASE_BSSRDF_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/taggedptr.h>
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

        SPECTRA_CPU_GPU pstd::optional<BSSRDFProbeSegment> SampleSp(Float u1, Point2f u2) const;

        BSSRDFSample ProbeIntersectionToSample(const SubsurfaceInteraction& si, ScratchBuffer& scratchBuffer) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_BSSRDF_H
