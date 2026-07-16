#ifndef SPECTRA_PATHTRACER_CORE_FILTERS_H
#define SPECTRA_PATHTRACER_CORE_FILTERS_H

#include <cmath>
#include <memory>
#include <pathtracer/base/filter.cuh>
#include <pathtracer/memory/memory.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/math.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/sampling.cuh>
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
        FilterSampler(Filter filter, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream);

        __host__ __device__ FilterSample Sample(Point2f u) const {
            int v = FindInterval(ny + 1, [&](int index) { return marginalCdf[index] <= u.y; });
            Float dv = u.y - marginalCdf[v];
            if (marginalCdf[v + 1] > marginalCdf[v]) dv /= marginalCdf[v + 1] - marginalCdf[v];

            const Float* rowCdf = conditionalCdf + v * (nx + 1);
            int x = FindInterval(nx + 1, [&](int index) { return rowCdf[index] <= u.x; });
            Float du = u.x - rowCdf[x];
            if (rowCdf[x + 1] > rowCdf[x]) du /= rowCdf[x + 1] - rowCdf[x];

            Point2f p(Lerp((x + du) / nx, domain.pMin.x, domain.pMax.x), Lerp((v + dv) / ny, domain.pMin.y, domain.pMax.y));
            Float pdf = std::abs(values[x + v * nx]) / integral;
            return FilterSample{p, values[x + v * nx] / pdf};
        }

    private:
        // FilterSampler Private Members
        Bounds2f domain;
        int nx = 0;
        int ny = 0;
        const Float* values = nullptr;
        const Float* conditionalCdf = nullptr;
        const Float* marginalCdf = nullptr;
        Float integral = 0;
    };

    // BoxFilter Definition
    class BoxFilter {
    public:
        // BoxFilter Public Methods
        BoxFilter(Vector2f radius = Vector2f(0.5, 0.5)) : radius(radius) {}

        static BoxFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc);

        __host__ __device__ Vector2f Radius() const {
            return radius;
        }


        __host__ __device__ Float Evaluate(Point2f p) const {
            return (std::abs(p.x) <= radius.x && std::abs(p.y) <= radius.y) ? 1 : 0;
        }

        __host__ __device__ FilterSample Sample(Point2f u) const {
            Point2f p(Lerp(u[0], -radius.x, radius.x), Lerp(u[1], -radius.y, radius.y));
            return {p, Float(1)};
        }

        __host__ __device__ Float Integral() const {
            return 2 * radius.x * 2 * radius.y;
        }

    private:
        Vector2f radius;
    };

    // GaussianFilter Definition
    class GaussianFilter {
    public:
        // GaussianFilter Public Methods
        GaussianFilter(Vector2f radius, Float sigma, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) : radius(radius), sigma(sigma), expX(Gaussian(radius.x, 0, sigma)), expY(Gaussian(radius.y, 0, sigma)), sampler(this, deviceArena, stream) {}

        static GaussianFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream);

        __host__ __device__ Vector2f Radius() const {
            return radius;
        }


        __host__ __device__ Float Evaluate(Point2f p) const {
            return (std::max<Float>(0, Gaussian(p.x, 0, sigma) - expX) * std::max<Float>(0, Gaussian(p.y, 0, sigma) - expY));
        }

        __host__ __device__ Float Integral() const {
            return ((GaussianIntegral(-radius.x, radius.x, 0, sigma) - 2 * radius.x * expX) * (GaussianIntegral(-radius.y, radius.y, 0, sigma) - 2 * radius.y * expY));
        }

        __host__ __device__ FilterSample Sample(Point2f u) const {
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
        MitchellFilter(Vector2f radius, Float b, Float c, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) : radius(radius), b(b), c(c), sampler(this, deviceArena, stream) {}

        static MitchellFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream);

        __host__ __device__ Vector2f Radius() const {
            return radius;
        }


        __host__ __device__ Float Evaluate(Point2f p) const {
            return Mitchell1D(2 * p.x / radius.x) * Mitchell1D(2 * p.y / radius.y);
        }

        __host__ __device__ FilterSample Sample(Point2f u) const {
            return sampler.Sample(u);
        }

        __host__ __device__ Float Integral() const {
            return radius.x * radius.y / 4;
        }

    private:
        // MitchellFilter Private Methods
        __host__ __device__ Float Mitchell1D(Float x) const {
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
        LanczosSincFilter(Vector2f radius, Float tau, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) : radius(radius), tau(tau), sampler(this, deviceArena, stream) {}

        static LanczosSincFilter* Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream);

        __host__ __device__ Vector2f Radius() const {
            return radius;
        }


        __host__ __device__ Float Evaluate(Point2f p) const {
            return WindowedSinc(p.x, radius.x, tau) * WindowedSinc(p.y, radius.y, tau);
        }

        __host__ __device__ FilterSample Sample(Point2f u) const {
            return sampler.Sample(u);
        }

        __host__ __device__ Float Integral() const;

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

        __host__ __device__ Vector2f Radius() const {
            return radius;
        }


        __host__ __device__ Float Evaluate(Point2f p) const {
            return std::max<Float>(0, radius.x - std::abs(p.x)) * std::max<Float>(0, radius.y - std::abs(p.y));
        }

        __host__ __device__ FilterSample Sample(Point2f u) const {
            return {Point2f(SampleTent(u[0], radius.x), SampleTent(u[1], radius.y)), Float(1)};
        }

        __host__ __device__ Float Integral() const {
            return Sqr(radius.x) * Sqr(radius.y);
        }

    private:
        Vector2f radius;
    };

    __host__ __device__ inline Float Filter::Evaluate(Point2f p) const {
        auto eval = [&](auto ptr) { return ptr->Evaluate(p); };
        return Dispatch(eval);
    }

    __host__ __device__ inline FilterSample Filter::Sample(Point2f u) const {
        auto sample = [&](auto ptr) { return ptr->Sample(u); };
        return Dispatch(sample);
    }

    __host__ __device__ inline Vector2f Filter::Radius() const {
        auto radius = [&](auto ptr) { return ptr->Radius(); };
        return Dispatch(radius);
    }

    __host__ __device__ inline Float Filter::Integral() const {
        auto integral = [&](auto ptr) { return ptr->Integral(); };
        return Dispatch(integral);
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_FILTERS_H
