#ifndef SPECTRA_PATHTRACER_BASE_FILTER_H
#define SPECTRA_PATHTRACER_BASE_FILTER_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>
#include <string>

namespace spectra {
    class ParameterDictionary;
    struct FileLoc;

    // Filter Declarations
    struct FilterSample;
    class BoxFilter;
    class GaussianFilter;
    class MitchellFilter;
    class LanczosSincFilter;
    class TriangleFilter;

    // Filter Definition
    class Filter : public TaggedPointer<BoxFilter, GaussianFilter, MitchellFilter, LanczosSincFilter, TriangleFilter> {
    public:
        // Filter Interface
        using TaggedPointer::TaggedPointer;

        static Filter Create(const std::string& name, const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU Vector2f Radius() const;

        SPECTRA_CPU_GPU Float Evaluate(Point2f p) const;

        SPECTRA_CPU_GPU Float Integral() const;

        SPECTRA_CPU_GPU FilterSample Sample(Point2f u) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_FILTER_H
