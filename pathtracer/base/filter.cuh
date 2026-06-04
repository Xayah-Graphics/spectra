#ifndef SPECTRA_PATHTRACER_BASE_FILTER_H
#define SPECTRA_PATHTRACER_BASE_FILTER_H

#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <pathtracer/util/vecmath.cuh>
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

        __host__ __device__ Vector2f Radius() const;

        __host__ __device__ Float Evaluate(Point2f p) const;

        __host__ __device__ Float Integral() const;

        __host__ __device__ FilterSample Sample(Point2f u) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_FILTER_H
