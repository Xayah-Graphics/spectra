#include <pathtracer/core/filters.cuh>
#include <pathtracer/core/paramdict.cuh>
#include <pathtracer/util/rng.cuh>
#include <vector>

namespace spectra {
    // Box Filter Method Definitions

    BoxFilter* BoxFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        Float xw = parameters.GetOneFloat("xradius", 0.5f);
        Float yw = parameters.GetOneFloat("yradius", 0.5f);
        return alloc.new_object<BoxFilter>(Vector2f(xw, yw));
    }

    // Gaussian Filter Method Definitions

    GaussianFilter* GaussianFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) {
        // Find common filter parameters
        Float xw    = parameters.GetOneFloat("xradius", 1.5f);
        Float yw    = parameters.GetOneFloat("yradius", 1.5f);
        Float sigma = parameters.GetOneFloat("sigma", 0.5f); // equivalent to old alpha = 2
        return alloc.new_object<GaussianFilter>(Vector2f(xw, yw), sigma, deviceArena, stream);
    }

    // Mitchell Filter Method Definitions

    MitchellFilter* MitchellFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) {
        // Find common filter parameters
        Float xw = parameters.GetOneFloat("xradius", 2.f);
        Float yw = parameters.GetOneFloat("yradius", 2.f);
        Float B  = parameters.GetOneFloat("B", 1.f / 3.f);
        Float C  = parameters.GetOneFloat("C", 1.f / 3.f);
        return alloc.new_object<MitchellFilter>(Vector2f(xw, yw), B, C, deviceArena, stream);
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


    LanczosSincFilter* LanczosSincFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) {
        Float xw  = parameters.GetOneFloat("xradius", 4.);
        Float yw  = parameters.GetOneFloat("yradius", 4.);
        Float tau = parameters.GetOneFloat("tau", 3.f);
        return alloc.new_object<LanczosSincFilter>(Vector2f(xw, yw), tau, deviceArena, stream);
    }

    // Triangle Filter Method Definitions

    TriangleFilter* TriangleFilter::Create(const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc) {
        // Find common filter parameters
        Float xw = parameters.GetOneFloat("xradius", 2.f);
        Float yw = parameters.GetOneFloat("yradius", 2.f);
        return alloc.new_object<TriangleFilter>(Vector2f(xw, yw));
    }

    Filter Filter::Create(const std::string& name, const ParameterDictionary& parameters, const FileLoc* loc, Allocator alloc, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) {
        Filter filter = nullptr;
        if (name == "box")
            filter = BoxFilter::Create(parameters, loc, alloc);
        else if (name == "gaussian")
            filter = GaussianFilter::Create(parameters, loc, alloc, deviceArena, stream);
        else if (name == "mitchell")
            filter = MitchellFilter::Create(parameters, loc, alloc, deviceArena, stream);
        else if (name == "sinc")
            filter = LanczosSincFilter::Create(parameters, loc, alloc, deviceArena, stream);
        else if (name == "triangle")
            filter = TriangleFilter::Create(parameters, loc, alloc);
        else
            throw std::runtime_error(diagnostics::Format(loc, "%s: filter type unknown.", name));

        if (!filter) throw std::runtime_error(diagnostics::Format(loc, "%s: unable to create filter.", name));

        parameters.ReportUnused();
        return filter;
    }

    // FilterSampler Method Definitions
    FilterSampler::FilterSampler(Filter filter, pathtracer::PathtracerDeviceArena& deviceArena, cudaStream_t stream) : domain(Point2f(-filter.Radius()), Point2f(filter.Radius())), nx(int(32 * filter.Radius().x)), ny(int(32 * filter.Radius().y)) {
        std::vector<Float> hostValues(static_cast<std::size_t>(nx) * ny);
        std::vector<Float> hostConditionalCdf(static_cast<std::size_t>(ny) * (nx + 1));
        std::vector<Float> rowIntegrals(ny);
        std::vector<Float> hostMarginalCdf(ny + 1);

        for (int y = 0; y < ny; ++y) {
            Float* rowCdf = hostConditionalCdf.data() + y * (nx + 1);
            rowCdf[0] = 0;
            for (int x = 0; x < nx; ++x) {
                Point2f p = domain.Lerp(Point2f((x + 0.5f) / nx, (y + 0.5f) / ny));
                const Float value = filter.Evaluate(p);
                hostValues[x + y * nx] = value;
                rowCdf[x + 1] = rowCdf[x] + std::abs(value) * (domain.pMax.x - domain.pMin.x) / nx;
            }
            rowIntegrals[y] = rowCdf[nx];
            if (rowIntegrals[y] == 0) for (int x = 1; x <= nx; ++x) rowCdf[x] = Float(x) / nx;
            else for (int x = 1; x <= nx; ++x) rowCdf[x] /= rowIntegrals[y];
        }

        hostMarginalCdf[0] = 0;
        for (int y = 0; y < ny; ++y) hostMarginalCdf[y + 1] = hostMarginalCdf[y] + rowIntegrals[y] * (domain.pMax.y - domain.pMin.y) / ny;
        integral = hostMarginalCdf[ny];
        if (integral == 0) for (int y = 1; y <= ny; ++y) hostMarginalCdf[y] = Float(y) / ny;
        else for (int y = 1; y <= ny; ++y) hostMarginalCdf[y] /= integral;

        values = deviceArena.StoreArray(hostValues.data(), hostValues.size(), stream);
        conditionalCdf = deviceArena.StoreArray(hostConditionalCdf.data(), hostConditionalCdf.size(), stream);
        marginalCdf = deviceArena.StoreArray(hostMarginalCdf.data(), hostMarginalCdf.size(), stream);
    }
} // namespace spectra
