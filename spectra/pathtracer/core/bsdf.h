#ifndef SPECTRA_PATHTRACER_CORE_BSDF_H
#define SPECTRA_PATHTRACER_CORE_BSDF_H

#include <spectra/pathtracer/util/float.h>

#include <spectra/pathtracer/core/bxdfs.h>
#include <spectra/pathtracer/core/interaction.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/vecmath.h>

namespace spectra
{
    // BSDF Definition
    class BSDF
    {
    public:
        // BSDF Public Methods
        BSDF() = default;
        SPECTRA_CPU_GPU
        BSDF(Normal3f ns, Vector3f dpdus, BxDF bxdf)
            : bxdf(bxdf), shadingFrame(Frame::FromXZ(Normalize(dpdus), Vector3f(ns)))
        {
        }

        SPECTRA_CPU_GPU
        operator bool() const { return (bool)bxdf; }

        SPECTRA_CPU_GPU
        BxDFFlags Flags() const { return bxdf.Flags(); }

        SPECTRA_CPU_GPU
        Vector3f RenderToLocal(Vector3f v) const { return shadingFrame.ToLocal(v); }

        SPECTRA_CPU_GPU
        Vector3f LocalToRender(Vector3f v) const { return shadingFrame.FromLocal(v); }

        SPECTRA_CPU_GPU
        SampledSpectrum f(Vector3f woRender, Vector3f wiRender,
                          TransportMode mode = TransportMode::Radiance) const
        {
            Vector3f wi = RenderToLocal(wiRender), wo = RenderToLocal(woRender);
            if (wo.z == 0)
                return {};
            return bxdf.f(wo, wi, mode);
        }

        template <typename BxDF>
        SPECTRA_CPU_GPU SampledSpectrum f(Vector3f woRender, Vector3f wiRender,
                                          TransportMode mode = TransportMode::Radiance) const
        {
            Vector3f wi = RenderToLocal(wiRender), wo = RenderToLocal(woRender);
            if (wo.z == 0)
                return {};
            const BxDF* specificBxDF = bxdf.CastOrNullptr<BxDF>();
            return specificBxDF->f(wo, wi, mode);
        }

        SPECTRA_CPU_GPU
        pstd::optional<BSDFSample> Sample_f(
            Vector3f woRender, Float u, Point2f u2,
            TransportMode mode = TransportMode::Radiance,
            BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const
        {
            Vector3f wo = RenderToLocal(woRender);
            if (wo.z == 0 || !(bxdf.Flags() & sampleFlags))
                return {};
            // Sample _bxdf_ and return _BSDFSample_
            pstd::optional<BSDFSample> bs = bxdf.Sample_f(wo, u, u2, mode, sampleFlags);
            if (bs)
                DCHECK_GE(bs->pdf, 0);
            if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
                return {};
            bs->wi = LocalToRender(bs->wi);
            return bs;
        }

        SPECTRA_CPU_GPU
        Float PDF(Vector3f woRender, Vector3f wiRender,
                  TransportMode mode = TransportMode::Radiance,
                  BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const
        {
            Vector3f wo = RenderToLocal(woRender), wi = RenderToLocal(wiRender);
            if (wo.z == 0)
                return 0;
            return bxdf.PDF(wo, wi, mode, sampleFlags);
        }

        template <typename BxDF>
        SPECTRA_CPU_GPU pstd::optional<BSDFSample> Sample_f(
            Vector3f woRender, Float u, Point2f u2,
            TransportMode mode = TransportMode::Radiance,
            BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const
        {
            Vector3f wo = RenderToLocal(woRender);
            if (wo.z == 0)
                return {};

            const BxDF* specificBxDF = bxdf.Cast<BxDF>();
            if (!(specificBxDF->Flags() & sampleFlags))
                return {};

            pstd::optional<BSDFSample> bs =
                specificBxDF->Sample_f(wo, u, u2, mode, sampleFlags);
            if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
                return {};
            DCHECK_GT(bs->pdf, 0);

            bs->wi = LocalToRender(bs->wi);

            return bs;
        }

        template <typename BxDF>
        SPECTRA_CPU_GPU Float
        PDF(Vector3f woRender, Vector3f wiRender,
            TransportMode mode = TransportMode::Radiance,
            BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const
        {
            Vector3f wo = RenderToLocal(woRender), wi = RenderToLocal(wiRender);
            if (wo.z == 0)
                return 0;
            const BxDF* specificBxDF = bxdf.Cast<BxDF>();
            return specificBxDF->PDF(wo, wi, mode, sampleFlags);
        }


        SPECTRA_CPU_GPU
        SampledSpectrum rho(pstd::span<const Point2f> u1, pstd::span<const Float> uc,
                            pstd::span<const Point2f> u2) const
        {
            return bxdf.rho(u1, uc, u2);
        }

        SPECTRA_CPU_GPU
        SampledSpectrum rho(Vector3f woRender, pstd::span<const Float> uc,
                            pstd::span<const Point2f> u) const
        {
            Vector3f wo = RenderToLocal(woRender);
            return bxdf.rho(wo, uc, u);
        }

        SPECTRA_CPU_GPU
        void Regularize() { bxdf.Regularize(); }

    private:
        // BSDF Private Members
        BxDF bxdf;
        Frame shadingFrame;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_CORE_BSDF_H
