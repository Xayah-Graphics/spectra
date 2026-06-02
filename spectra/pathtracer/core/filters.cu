#include <spectra/pathtracer/core/filters.cuh>
#include <spectra/pathtracer/core/paramdict.cuh>
#include <spectra/pathtracer/util/rng.cuh>

namespace spectra {
    __host__ __device__ Float Filter::Evaluate(Point2f p) const {
        auto eval = [&](auto ptr) { return ptr->Evaluate(p); };
        return Dispatch(eval);
    }

    __host__ __device__ FilterSample Filter::Sample(Point2f u) const {
        auto sample = [&](auto ptr) { return ptr->Sample(u); };
        return Dispatch(sample);
    }

    __host__ __device__ Vector2f Filter::Radius() const {
        auto radius = [&](auto ptr) { return ptr->Radius(); };
        return Dispatch(radius);
    }

    __host__ __device__ Float Filter::Integral() const {
        auto integral = [&](auto ptr) { return ptr->Integral(); };
        return Dispatch(integral);
    }


    // Box Filter Method Definitions

    BoxFilter* BoxFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        Float xw = parameters.GetOneFloat("xradius", 0.5f);
        Float yw = parameters.GetOneFloat("yradius", 0.5f);
        return alloc.new_object<BoxFilter>(Vector2f(xw, yw));
    }

    // Gaussian Filter Method Definitions

    GaussianFilter* GaussianFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        // Find common filter parameters
        Float xw    = parameters.GetOneFloat("xradius", 1.5f);
        Float yw    = parameters.GetOneFloat("yradius", 1.5f);
        Float sigma = parameters.GetOneFloat("sigma", 0.5f); // equivalent to old alpha = 2
        return alloc.new_object<GaussianFilter>(Vector2f(xw, yw), sigma, alloc);
    }

    // Mitchell Filter Method Definitions

    MitchellFilter* MitchellFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        // Find common filter parameters
        Float xw = parameters.GetOneFloat("xradius", 2.f);
        Float yw = parameters.GetOneFloat("yradius", 2.f);
        Float B  = parameters.GetOneFloat("B", 1.f / 3.f);
        Float C  = parameters.GetOneFloat("C", 1.f / 3.f);
        return alloc.new_object<MitchellFilter>(Vector2f(xw, yw), B, C, alloc);
    }

    // Sinc Filter Method Definitions
    __host__ __device__ Float LanczosSincFilter::Integral() const {
        Float sum       = 0;
        int sqrtSamples = 64;
        int nSamples    = sqrtSamples * sqrtSamples;
        Float area      = 2 * radius.x * 2 * radius.y;
        RNG rng;
        for (int y = 0; y < sqrtSamples; ++y) {
            for (int x = 0; x < sqrtSamples; ++x) {
                Point2f u((x + rng.Uniform<Float>()) / sqrtSamples, (y + rng.Uniform<Float>()) / sqrtSamples);
                Point2f p(Lerp(u.x, -radius.x, radius.x), Lerp(u.y, -radius.y, radius.y));
                sum += Evaluate(p);
            }
        }
        return sum / nSamples * area;
    }


    LanczosSincFilter* LanczosSincFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        Float xw  = parameters.GetOneFloat("xradius", 4.);
        Float yw  = parameters.GetOneFloat("yradius", 4.);
        Float tau = parameters.GetOneFloat("tau", 3.f);
        return alloc.new_object<LanczosSincFilter>(Vector2f(xw, yw), tau, alloc);
    }

    // Triangle Filter Method Definitions

    TriangleFilter* TriangleFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        // Find common filter parameters
        Float xw = parameters.GetOneFloat("xradius", 2.f);
        Float yw = parameters.GetOneFloat("yradius", 2.f);
        return alloc.new_object<TriangleFilter>(Vector2f(xw, yw));
    }

    Filter Filter::Create(const std::string& name, const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        Filter filter = nullptr;
        if (name == "box")
            filter = BoxFilter::Create(parameters, loc, alloc);
        else if (name == "gaussian")
            filter = GaussianFilter::Create(parameters, loc, alloc);
        else if (name == "mitchell")
            filter = MitchellFilter::Create(parameters, loc, alloc);
        else if (name == "sinc")
            filter = LanczosSincFilter::Create(parameters, loc, alloc);
        else if (name == "triangle")
            filter = TriangleFilter::Create(parameters, loc, alloc);
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: filter type unknown.", name));

        if (!filter) throw std::runtime_error(diagnostics::Format(loc, "%s: unable to create filter.", name));

        parameters.ReportUnused();
        return filter;
    }

    // FilterSampler Method Definitions
    FilterSampler::FilterSampler(Filter filter, Allocator alloc) : domain(Point2f(-filter.Radius()), Point2f(filter.Radius())), f(int(32 * filter.Radius().x), int(32 * filter.Radius().y), alloc), distrib(alloc) {
        // Tabularize unnormalized filter function in _f_
        for (int y = 0; y < f.YSize(); ++y)
            for (int x = 0; x < f.XSize(); ++x) {
                Point2f p = domain.Lerp(Point2f((x + 0.5f) / f.XSize(), (y + 0.5f) / f.YSize()));
                f(x, y)   = filter.Evaluate(p);
            }

        // Compute sampling distribution for filter
        distrib = PiecewiseConstant2D(f, domain, alloc);
    }
} // namespace spectra
