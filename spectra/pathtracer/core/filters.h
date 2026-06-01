#ifndef SPECTRA_PATHTRACER_CORE_FILTERS_H
#define SPECTRA_PATHTRACER_CORE_FILTERS_H

#include <cmath>
#include <memory>
#include <spectra/pathtracer/base/filter.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/math.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/sampling.h>
#include <string>

namespace spectra {
    // FilterSample Definition
    struct FilterSample {
        Point2f p;
        Float weight;
    };

    class FilterSampler {
    public:
        // FilterSampler Public Methods
        FilterSampler(Filter filter, Allocator alloc = {});

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            Float pdf;
            Point2i pi;
            Point2f p = distrib.Sample(u, &pdf, &pi);
            return FilterSample{p, f[pi] / pdf};
        }

    private:
        // FilterSampler Private Members
        Bounds2f domain;
        Array2D<Float> f;
        PiecewiseConstant2D distrib;
    };

    // BoxFilter Definition
    class BoxFilter {
    public:
        // BoxFilter Public Methods
        BoxFilter(Vector2f radius = Vector2f(0.5, 0.5)) : radius(radius) {}

        static BoxFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU
        Vector2f Radius() const {
            return radius;
        }


        SPECTRA_CPU_GPU
        Float Evaluate(Point2f p) const {
            return (std::abs(p.x) <= radius.x && std::abs(p.y) <= radius.y) ? 1 : 0;
        }

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            Point2f p(Lerp(u[0], -radius.x, radius.x), Lerp(u[1], -radius.y, radius.y));
            return {p, Float(1)};
        }

        SPECTRA_CPU_GPU
        Float Integral() const {
            return 2 * radius.x * 2 * radius.y;
        }

    private:
        Vector2f radius;
    };

    // GaussianFilter Definition
    class GaussianFilter {
    public:
        // GaussianFilter Public Methods
        GaussianFilter(Vector2f radius, Float sigma = 0.5f, Allocator alloc = {}) : radius(radius), sigma(sigma), expX(Gaussian(radius.x, 0, sigma)), expY(Gaussian(radius.y, 0, sigma)), sampler(this, alloc) {}

        static GaussianFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU
        Vector2f Radius() const {
            return radius;
        }


        SPECTRA_CPU_GPU
        Float Evaluate(Point2f p) const {
            return (std::max<Float>(0, Gaussian(p.x, 0, sigma) - expX) * std::max<Float>(0, Gaussian(p.y, 0, sigma) - expY));
        }

        SPECTRA_CPU_GPU
        Float Integral() const {
            return ((GaussianIntegral(-radius.x, radius.x, 0, sigma) - 2 * radius.x * expX) * (GaussianIntegral(-radius.y, radius.y, 0, sigma) - 2 * radius.y * expY));
        }

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            return sampler.Sample(u);
        }

    private:
        // GaussianFilter Private Members
        Vector2f radius;
        Float sigma, expX, expY;
        FilterSampler sampler;
    };

    // MitchellFilter Definition
    class MitchellFilter {
    public:
        // MitchellFilter Public Methods
        MitchellFilter(Vector2f radius, Float b = 1.f / 3.f, Float c = 1.f / 3.f, Allocator alloc = {}) : radius(radius), b(b), c(c), sampler(this, alloc) {}

        static MitchellFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU
        Vector2f Radius() const {
            return radius;
        }


        SPECTRA_CPU_GPU
        Float Evaluate(Point2f p) const {
            return Mitchell1D(2 * p.x / radius.x) * Mitchell1D(2 * p.y / radius.y);
        }

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            return sampler.Sample(u);
        }

        SPECTRA_CPU_GPU
        Float Integral() const {
            return radius.x * radius.y / 4;
        }

    private:
        // MitchellFilter Private Methods
        SPECTRA_CPU_GPU
        Float Mitchell1D(Float x) const {
            x = std::abs(x);
            if (x <= 1)
                return ((12 - 9 * b - 6 * c) * x * x * x + (-18 + 12 * b + 6 * c) * x * x + (6 - 2 * b)) * (1.f / 6.f);
            else if (x <= 2)
                return ((-b - 6 * c) * x * x * x + (6 * b + 30 * c) * x * x + (-12 * b - 48 * c) * x + (8 * b + 24 * c)) * (1.f / 6.f);
            else
                return 0;
        }

        // MitchellFilter Private Members
        Vector2f radius;
        Float b, c;
        FilterSampler sampler;
    };

    // LanczosSincFilter Definition
    class LanczosSincFilter {
    public:
        // LanczosSincFilter Public Methods
        LanczosSincFilter(Vector2f radius, Float tau = 3.f, Allocator alloc = {}) : radius(radius), tau(tau), sampler(this, alloc) {}

        static LanczosSincFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU
        Vector2f Radius() const {
            return radius;
        }


        SPECTRA_CPU_GPU
        Float Evaluate(Point2f p) const {
            return WindowedSinc(p.x, radius.x, tau) * WindowedSinc(p.y, radius.y, tau);
        }

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            return sampler.Sample(u);
        }

        SPECTRA_CPU_GPU
        Float Integral() const;

    private:
        // LanczosSincFilter Private Members
        Vector2f radius;
        Float tau;
        FilterSampler sampler;
    };

    // TriangleFilter Definition
    class TriangleFilter {
    public:
        // TriangleFilter Public Methods
        TriangleFilter(Vector2f radius) : radius(radius) {}

        static TriangleFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        SPECTRA_CPU_GPU
        Vector2f Radius() const {
            return radius;
        }


        SPECTRA_CPU_GPU
        Float Evaluate(Point2f p) const {
            return std::max<Float>(0, radius.x - std::abs(p.x)) * std::max<Float>(0, radius.y - std::abs(p.y));
        }

        SPECTRA_CPU_GPU
        FilterSample Sample(Point2f u) const {
            return {Point2f(SampleTent(u[0], radius.x), SampleTent(u[1], radius.y)), Float(1)};
        }

        SPECTRA_CPU_GPU
        Float Integral() const {
            return Sqr(radius.x) * Sqr(radius.y);
        }

    private:
        Vector2f radius;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_FILTERS_H
