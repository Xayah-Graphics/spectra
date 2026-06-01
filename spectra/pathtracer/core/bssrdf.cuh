#ifndef SPECTRA_PATHTRACER_CORE_BSSRDF_H
#define SPECTRA_PATHTRACER_CORE_BSSRDF_H

#include <spectra/pathtracer/base/bssrdf.cuh>
#include <spectra/pathtracer/core/bsdf.cuh>
#include <spectra/pathtracer/core/interaction.cuh>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/scattering.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    // BSSRDFSample Definition
    struct BSSRDFSample {
        SampledSpectrum Sp, pdf;
        BSDF Sw;
        Vector3f wo;
    };

    // SubsurfaceInteraction Definition
    struct SubsurfaceInteraction {
        // SubsurfaceInteraction Public Methods
        SubsurfaceInteraction() = default;
        __host__ __device__ SubsurfaceInteraction(const SurfaceInteraction& si) : pi(si.pi), n(si.n), dpdu(si.dpdu), dpdv(si.dpdv), ns(si.shading.n), dpdus(si.shading.dpdu), dpdvs(si.shading.dpdv) {}

        __host__ __device__ operator SurfaceInteraction() const {
            SurfaceInteraction si;
            si.pi           = pi;
            si.n            = n;
            si.dpdu         = dpdu;
            si.dpdv         = dpdv;
            si.shading.n    = ns;
            si.shading.dpdu = dpdus;
            si.shading.dpdv = dpdvs;
            return si;
        }

        __host__ __device__ Point3f p() const {
            return Point3f(pi);
        }

        // SubsurfaceInteraction Public Members
        Point3fi pi;
        Normal3f n, ns;
        Vector3f dpdu, dpdv, dpdus, dpdvs;
    };

    // BSSRDF Function Declarations
    Float BeamDiffusionSS(Float sigma_s, Float sigma_a, Float g, Float eta, Float r);
    Float BeamDiffusionMS(Float sigma_s, Float sigma_a, Float g, Float eta, Float r);

    void ComputeBeamDiffusionBSSRDF(Float g, Float eta, BSSRDFTable* t);

    // BSSRDFTable Definition
    struct BSSRDFTable {
        // BSSRDFTable Public Members
        pstd::vector<Float> rhoSamples, radiusSamples;
        pstd::vector<Float> profile;
        pstd::vector<Float> rhoEff;
        pstd::vector<Float> profileCDF;

        // BSSRDFTable Public Methods
        BSSRDFTable(int nRhoSamples, int nRadiusSamples, Allocator alloc);


        __host__ __device__ Float EvalProfile(int rhoIndex, int radiusIndex) const {
            CHECK(rhoIndex >= 0 && rhoIndex < rhoSamples.size());
            CHECK(radiusIndex >= 0 && radiusIndex < radiusSamples.size());
            return profile[rhoIndex * radiusSamples.size() + radiusIndex];
        }
    };

    // BSSRDFProbeSegment Definition
    struct BSSRDFProbeSegment {
        // BSSRDFProbeSegment Public Methods
        BSSRDFProbeSegment() = default;
        __host__ __device__ BSSRDFProbeSegment(Point3f p0, Point3f p1) : p0(p0), p1(p1) {}

        Point3f p0, p1;
    };

    // TabulatedBSSRDF Definition
    class TabulatedBSSRDF {
    public:
        // TabulatedBSSRDF Type Definitions
        using BxDF = NormalizedFresnelBxDF;

        // TabulatedBSSRDF Public Methods
        TabulatedBSSRDF() = default;
        __host__ __device__ TabulatedBSSRDF(Point3f po, Normal3f ns, Vector3f wo, Float eta, const SampledSpectrum& sigma_a, const SampledSpectrum& sigma_s, const BSSRDFTable* table) : po(po), wo(wo), eta(eta), ns(ns), table(table) {
            sigma_t = sigma_a + sigma_s;
            rho     = SafeDiv(sigma_s, sigma_t);
        }

        __host__ __device__ SampledSpectrum Sp(Point3f pi) const {
            return Sr(Distance(po, pi));
        }

        __host__ __device__ SampledSpectrum Sr(Float r) const {
            SampledSpectrum Sr(0.f);
            for (int i = 0; i < NSpectrumSamples; ++i) {
                // Convert $r$ into unitless optical radius $r_{\roman{optical}}$
                Float rOptical = r * sigma_t[i];

                // Compute spline weights to interpolate BSSRDF at _i_th wavelength
                int rhoOffset, radiusOffset;
                Float rhoWeights[4], radiusWeights[4];
                if (!CatmullRomWeights(table->rhoSamples, rho[i], &rhoOffset, rhoWeights) || !CatmullRomWeights(table->radiusSamples, rOptical, &radiusOffset, radiusWeights)) continue;

                // Set BSSRDF value _Sr[i]_ using tensor spline interpolation
                Float sr = 0;
                for (int j = 0; j < 4; ++j)
                    for (int k = 0; k < 4; ++k) {
                        // Accumulate contribution of $(j,k)$ table sample
                        if (Float weight = rhoWeights[j] * radiusWeights[k]; weight != 0) sr += weight * table->EvalProfile(rhoOffset + j, radiusOffset + k);
                    }
                // Cancel marginal PDF factor from tabulated BSSRDF profile
                if (rOptical != 0) sr /= 2 * Pi * rOptical;

                Sr[i] = sr;
            }
            // Transform BSSRDF value into rendering space units
            Sr *= Sqr(sigma_t);

            return ClampZero(Sr);
        }

        __host__ __device__ pstd::optional<Float> SampleSr(Float u) const {
            if (sigma_t[0] == 0) return {};
            return SampleCatmullRom2D(table->rhoSamples, table->radiusSamples, table->profile, table->profileCDF, rho[0], u) / sigma_t[0];
        }

        __host__ __device__ SampledSpectrum PDF_Sr(Float r) const {
            SampledSpectrum pdf(0.f);
            for (int i = 0; i < NSpectrumSamples; ++i) {
                // Convert $r$ into unitless optical radius $r_{\roman{optical}}$
                Float rOptical = r * sigma_t[i];

                // Compute spline weights to interpolate BSSRDF at _i_th wavelength
                int rhoOffset, radiusOffset;
                Float rhoWeights[4], radiusWeights[4];
                if (!CatmullRomWeights(table->rhoSamples, rho[i], &rhoOffset, rhoWeights) || !CatmullRomWeights(table->radiusSamples, rOptical, &radiusOffset, radiusWeights)) continue;

                // Set BSSRDF profile probability density for wavelength
                Float sr = 0, rhoEff = 0;
                for (int j = 0; j < 4; ++j)
                    if (rhoWeights[j] != 0) {
                        // Update _rhoEff_ and _sr_ for wavelength
                        rhoEff += table->rhoEff[rhoOffset + j] * rhoWeights[j];
                        for (int k = 0; k < 4; ++k)
                            if (radiusWeights[k] != 0) sr += table->EvalProfile(rhoOffset + j, radiusOffset + k) * rhoWeights[j] * radiusWeights[k];
                    }
                // Cancel marginal PDF factor from tabulated BSSRDF profile
                if (rOptical != 0) sr /= 2 * Pi * rOptical;

                pdf[i] = sr * Sqr(sigma_t[i]) / rhoEff;
            }
            return ClampZero(pdf);
        }

        __host__ __device__ pstd::optional<BSSRDFProbeSegment> SampleSp(Float u1, Point2f u2) const {
            // Choose projection axis for BSSRDF sampling
            Frame f;
            if (u1 < 0.25f)
                f = Frame::FromX(ns);
            else if (u1 < 0.5f)
                f = Frame::FromY(ns);
            else
                f = Frame::FromZ(ns);

            // Sample BSSRDF profile in polar coordinates
            pstd::optional<Float> r = SampleSr(u2[0]);
            if (!r) return {};
            Float phi = 2 * Pi * u2[1];

            // Compute BSSRDF profile bounds and intersection height
            pstd::optional<Float> r_max = SampleSr(0.999f);
            if (!r_max || *r >= *r_max) return {};
            Float l = 2 * std::sqrt(Sqr(*r_max) - Sqr(*r));

            // Return BSSRDF sampling ray segment
            Point3f pStart  = po + *r * (f.x * std::cos(phi) + f.y * std::sin(phi)) - l * f.z / 2;
            Point3f pTarget = pStart + l * f.z;
            return BSSRDFProbeSegment{pStart, pTarget};
        }

        __host__ __device__ SampledSpectrum PDF_Sp(Point3f pi, Normal3f ni) const {
            // Express $\pti-\pto$ and $\N{}_\roman{i}$ with respect to local coordinates at
            // $\pto$
            Vector3f d      = pi - po;
            Frame f         = Frame::FromZ(ns);
            Vector3f dLocal = f.ToLocal(d);
            Normal3f nLocal = f.ToLocal(ni);

            // Compute BSSRDF profile radius under projection along each axis
            Float rProj[3] = {std::sqrt(Sqr(dLocal.y) + Sqr(dLocal.z)), std::sqrt(Sqr(dLocal.z) + Sqr(dLocal.x)), std::sqrt(Sqr(dLocal.x) + Sqr(dLocal.y))};

            // Return combined probability from all BSSRDF sampling strategies
            SampledSpectrum pdf(0.f);
            Float axisProb[3] = {.25f, .25f, .5f};
            for (int axis = 0; axis < 3; ++axis) pdf += PDF_Sr(rProj[axis]) * std::abs(nLocal[axis]) * axisProb[axis];
            return pdf;
        }

        __host__ __device__ BSSRDFSample ProbeIntersectionToSample(const SubsurfaceInteraction& si, NormalizedFresnelBxDF* bxdf) const {
            *bxdf       = NormalizedFresnelBxDF(eta);
            Vector3f wo = Vector3f(si.ns);
            BSDF bsdf(si.ns, si.dpdus, bxdf);
            return BSSRDFSample{Sp(si.p()), PDF_Sp(si.p(), si.n), bsdf, wo};
        }

    private:
        friend struct SOA<TabulatedBSSRDF>;
        // TabulatedBSSRDF Private Members
        Point3f po;
        Vector3f wo;
        Normal3f ns;
        Float eta;
        SampledSpectrum sigma_t, rho;
        const BSSRDFTable* table;
    };

    // BSSRDF Inline Functions
    __host__ __device__ inline void SubsurfaceFromDiffuse(const BSSRDFTable& t, const SampledSpectrum& rhoEff, const SampledSpectrum& mfp, SampledSpectrum* sigma_a, SampledSpectrum* sigma_s) {
        for (int c = 0; c < NSpectrumSamples; ++c) {
            Float rho     = InvertCatmullRom(t.rhoSamples, t.rhoEff, rhoEff[c]);
            (*sigma_s)[c] = rho / mfp[c];
            (*sigma_a)[c] = (1 - rho) / mfp[c];
        }
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_BSSRDF_H
