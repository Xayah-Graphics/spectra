#ifndef SPECTRA_PATHTRACER_CORE_BSDF_H
#define SPECTRA_PATHTRACER_CORE_BSDF_H

#include <spectra/pathtracer/core/bxdfs.cuh>
#include <spectra/pathtracer/core/interaction.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>

namespace spectra {
    // BSDF Definition
    class BSDF {
    public:
        // BSDF Public Methods
        BSDF() = default;
        __host__ __device__ BSDF(Normal3f ns, Vector3f dpdus, BxDF bxdf) : bxdf(bxdf), shadingFrame(Frame::FromXZ(Normalize(dpdus), Vector3f(ns))) {}

        __host__ __device__ operator bool() const {
            return (bool) bxdf;
        }

        __host__ __device__ BxDFFlags Flags() const {
            return bxdf.Flags();
        }

        __host__ __device__ Vector3f RenderToLocal(Vector3f v) const {
            return shadingFrame.ToLocal(v);
        }

        __host__ __device__ Vector3f LocalToRender(Vector3f v) const {
            return shadingFrame.FromLocal(v);
        }

        __host__ __device__ SampledSpectrum f(Vector3f woRender, Vector3f wiRender, TransportMode mode = TransportMode::Radiance) const {
            Vector3f wi = RenderToLocal(wiRender), wo = RenderToLocal(woRender);
            if (wo.z == 0) return {};
            return bxdf.f(wo, wi, mode);
        }

        template <typename BxDF>
        __host__ __device__ SampledSpectrum f(Vector3f woRender, Vector3f wiRender, TransportMode mode = TransportMode::Radiance) const {
            Vector3f wi = RenderToLocal(wiRender), wo = RenderToLocal(woRender);
            if (wo.z == 0) return {};
            const BxDF* specificBxDF = bxdf.CastOrNullptr<BxDF>();
            return specificBxDF->f(wo, wi, mode);
        }

        __host__ __device__ pstd::optional<BSDFSample> Sample_f(Vector3f woRender, Float u, Point2f u2, TransportMode mode = TransportMode::Radiance, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
            Vector3f wo = RenderToLocal(woRender);
            if (wo.z == 0 || !(bxdf.Flags() & sampleFlags)) return {};
            // Sample _bxdf_ and return _BSDFSample_
            pstd::optional<BSDFSample> bs = bxdf.Sample_f(wo, u, u2, mode, sampleFlags);
            if (bs) DCHECK_GE(bs->pdf, 0);
            if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0) return {};
            bs->wi = LocalToRender(bs->wi);
            return bs;
        }

        __host__ __device__ Float PDF(Vector3f woRender, Vector3f wiRender, TransportMode mode = TransportMode::Radiance, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
            Vector3f wo = RenderToLocal(woRender), wi = RenderToLocal(wiRender);
            if (wo.z == 0) return 0;
            return bxdf.PDF(wo, wi, mode, sampleFlags);
        }

        template <typename BxDF>
        __host__ __device__ pstd::optional<BSDFSample> Sample_f(Vector3f woRender, Float u, Point2f u2, TransportMode mode = TransportMode::Radiance, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
            Vector3f wo = RenderToLocal(woRender);
            if (wo.z == 0) return {};

            const BxDF* specificBxDF = bxdf.Cast<BxDF>();
            if (!(specificBxDF->Flags() & sampleFlags)) return {};

            pstd::optional<BSDFSample> bs = specificBxDF->Sample_f(wo, u, u2, mode, sampleFlags);
            if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0) return {};
            DCHECK_GT(bs->pdf, 0);

            bs->wi = LocalToRender(bs->wi);

            return bs;
        }

        template <typename BxDF>
        __host__ __device__ Float PDF(Vector3f woRender, Vector3f wiRender, TransportMode mode = TransportMode::Radiance, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
            Vector3f wo = RenderToLocal(woRender), wi = RenderToLocal(wiRender);
            if (wo.z == 0) return 0;
            const BxDF* specificBxDF = bxdf.Cast<BxDF>();
            return specificBxDF->PDF(wo, wi, mode, sampleFlags);
        }


        __host__ __device__ SampledSpectrum rho(pstd::span<const Point2f> u1, pstd::span<const Float> uc, pstd::span<const Point2f> u2) const {
            return bxdf.rho(u1, uc, u2);
        }

        __host__ __device__ SampledSpectrum rho(Vector3f woRender, pstd::span<const Float> uc, pstd::span<const Point2f> u) const {
            Vector3f wo = RenderToLocal(woRender);
            return bxdf.rho(wo, uc, u);
        }

        __host__ __device__ void Regularize() {
            bxdf.Regularize();
        }

    private:
        // BSDF Private Members
        BxDF bxdf;
        Frame shadingFrame;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_BSDF_H
