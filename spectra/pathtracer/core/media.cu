#include <spectra/pathtracer/core/media.cuh>

namespace spectra {
    __host__ __device__ Float PhaseFunction::p(Vector3f wo, Vector3f wi) const {
        auto p = [&](auto ptr) { return ptr->p(wo, wi); };
        return Dispatch(p);
    }

    __host__ __device__ pstd::optional<PhaseFunctionSample> PhaseFunction::Sample_p(Vector3f wo, Point2f u) const {
        auto sample = [&](auto ptr) { return ptr->Sample_p(wo, u); };
        return Dispatch(sample);
    }

    __host__ __device__ Float PhaseFunction::PDF(Vector3f wo, Vector3f wi) const {
        auto pdf = [&](auto ptr) { return ptr->PDF(wo, wi); };
        return Dispatch(pdf);
    }

    __host__ __device__ pstd::optional<RayMajorantSegment> RayMajorantIterator::Next() {
        auto next = [](auto ptr) { return ptr->Next(); };
        return Dispatch(next);
    }

    __host__ __device__ MediumProperties Medium::SamplePoint(Point3f p, const SampledWavelengths& lambda) const {
        auto sample = [&](auto ptr) { return ptr->SamplePoint(p, lambda); };
        return Dispatch(sample);
    }
} // namespace spectra
