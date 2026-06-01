#ifndef SPECTRA_PATHTRACER_BASE_BXDF_H
#define SPECTRA_PATHTRACER_BASE_BXDF_H

#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>

namespace spectra {
    struct MeasuredBxDFData;

    // BxDFReflTransFlags Definition
    enum class BxDFReflTransFlags { Unset = 0, Reflection = 1 << 0, Transmission = 1 << 1, All = Reflection | Transmission };

    __host__ __device__ inline BxDFReflTransFlags operator|(BxDFReflTransFlags a, BxDFReflTransFlags b) {
        return BxDFReflTransFlags((int) a | (int) b);
    }

    __host__ __device__ inline int operator&(BxDFReflTransFlags a, BxDFReflTransFlags b) {
        return ((int) a & (int) b);
    }

    __host__ __device__ inline BxDFReflTransFlags& operator|=(BxDFReflTransFlags& a, BxDFReflTransFlags b) {
        (int&) a |= int(b);
        return a;
    }

    // BxDFFlags Definition
    enum BxDFFlags {
        Unset        = 0,
        Reflection   = 1 << 0,
        Transmission = 1 << 1,
        Diffuse      = 1 << 2,
        Glossy       = 1 << 3,
        Specular     = 1 << 4,
        // Composite _BxDFFlags_ definitions
        DiffuseReflection    = Diffuse | Reflection,
        DiffuseTransmission  = Diffuse | Transmission,
        GlossyReflection     = Glossy | Reflection,
        GlossyTransmission   = Glossy | Transmission,
        SpecularReflection   = Specular | Reflection,
        SpecularTransmission = Specular | Transmission,
        All                  = Diffuse | Glossy | Specular | Reflection | Transmission
    };

    __host__ __device__ inline BxDFFlags operator|(BxDFFlags a, BxDFFlags b) {
        return BxDFFlags((int) a | (int) b);
    }

    __host__ __device__ inline int operator&(BxDFFlags a, BxDFFlags b) {
        return ((int) a & (int) b);
    }

    __host__ __device__ inline int operator&(BxDFFlags a, BxDFReflTransFlags b) {
        return ((int) a & (int) b);
    }

    __host__ __device__ inline BxDFFlags& operator|=(BxDFFlags& a, BxDFFlags b) {
        (int&) a |= int(b);
        return a;
    }

    // BxDFFlags Inline Functions
    __host__ __device__ inline bool IsReflective(BxDFFlags f) {
        return f & Reflection;
    }

    __host__ __device__ inline bool IsTransmissive(BxDFFlags f) {
        return f & Transmission;
    }

    __host__ __device__ inline bool IsDiffuse(BxDFFlags f) {
        return f & Diffuse;
    }

    __host__ __device__ inline bool IsGlossy(BxDFFlags f) {
        return f & Glossy;
    }

    __host__ __device__ inline bool IsSpecular(BxDFFlags f) {
        return f & Specular;
    }

    __host__ __device__ inline bool IsNonSpecular(BxDFFlags f) {
        return f & (Diffuse | Glossy);
    }

    // TransportMode Definition
    enum class TransportMode { Radiance, Importance };

    __host__ __device__ inline TransportMode operator!(TransportMode mode) {
        return (mode == TransportMode::Radiance) ? TransportMode::Importance : TransportMode::Radiance;
    }

    // BSDFSample Definition
    struct BSDFSample {
        // BSDFSample Public Methods
        BSDFSample() = default;
        __host__ __device__ BSDFSample(SampledSpectrum f, Vector3f wi, Float pdf, BxDFFlags flags, Float eta = 1, bool pdfIsProportional = false) : f(f), wi(wi), pdf(pdf), flags(flags), eta(eta), pdfIsProportional(pdfIsProportional) {}

        __host__ __device__ bool IsReflection() const {
            return IsReflective(flags);
        }

        __host__ __device__ bool IsTransmission() const {
            return IsTransmissive(flags);
        }

        __host__ __device__ bool IsDiffuse() const {
            return spectra::IsDiffuse(flags);
        }

        __host__ __device__ bool IsGlossy() const {
            return spectra::IsGlossy(flags);
        }

        __host__ __device__ bool IsSpecular() const {
            return spectra::IsSpecular(flags);
        }

        SampledSpectrum f;
        Vector3f wi;
        Float pdf = 0;
        BxDFFlags flags;
        Float eta              = 1;
        bool pdfIsProportional = false;
    };

    class DiffuseBxDF;
    class DiffuseTransmissionBxDF;
    class DielectricBxDF;
    class ThinDielectricBxDF;
    class HairBxDF;
    class MeasuredBxDF;
    class ConductorBxDF;
    class NormalizedFresnelBxDF;
    class CoatedDiffuseBxDF;
    class CoatedConductorBxDF;

    // BxDF Definition
    class BxDF : public TaggedPointer<DiffuseTransmissionBxDF, DiffuseBxDF, CoatedDiffuseBxDF, CoatedConductorBxDF, DielectricBxDF, ThinDielectricBxDF, HairBxDF, MeasuredBxDF, ConductorBxDF, NormalizedFresnelBxDF> {
    public:
        // BxDF Interface
        __host__ __device__ BxDFFlags Flags() const;

        using TaggedPointer::TaggedPointer;


        __host__ __device__ SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;

        __host__ __device__ pstd::optional<BSDFSample> Sample_f(Vector3f wo, Float uc, Point2f u, TransportMode mode = TransportMode::Radiance, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

        __host__ __device__ Float PDF(Vector3f wo, Vector3f wi, TransportMode mode, BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

        __host__ __device__ SampledSpectrum rho(Vector3f wo, pstd::span<const Float> uc, pstd::span<const Point2f> u2) const;
        SampledSpectrum rho(pstd::span<const Point2f> u1, pstd::span<const Float> uc2, pstd::span<const Point2f> u2) const;

        __host__ __device__ void Regularize();
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_BXDF_H
