#ifndef SPECTRA_PATHTRACER_BASE_BXDF_H
#define SPECTRA_PATHTRACER_BASE_BXDF_H

#include <spectra/pathtracer/util/float.h>

#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <string>

namespace spectra
{
    struct MeasuredBxDFData;

    // BxDFReflTransFlags Definition
    enum class BxDFReflTransFlags
    {
        Unset = 0,
        Reflection = 1 << 0,
        Transmission = 1 << 1,
        All = Reflection | Transmission
    };

    SPECTRA_CPU_GPU
    inline BxDFReflTransFlags operator|(BxDFReflTransFlags a, BxDFReflTransFlags b)
    {
        return BxDFReflTransFlags((int)a | (int)b);
    }

    SPECTRA_CPU_GPU
    inline int operator&(BxDFReflTransFlags a, BxDFReflTransFlags b)
    {
        return ((int)a & (int)b);
    }

    SPECTRA_CPU_GPU
    inline BxDFReflTransFlags& operator|=(BxDFReflTransFlags& a, BxDFReflTransFlags b)
    {
        (int&)a |= int(b);
        return a;
    }

    std::string ToString(BxDFReflTransFlags flags);

    // BxDFFlags Definition
    enum BxDFFlags
    {
        Unset = 0,
        Reflection = 1 << 0,
        Transmission = 1 << 1,
        Diffuse = 1 << 2,
        Glossy = 1 << 3,
        Specular = 1 << 4,
        // Composite _BxDFFlags_ definitions
        DiffuseReflection = Diffuse | Reflection,
        DiffuseTransmission = Diffuse | Transmission,
        GlossyReflection = Glossy | Reflection,
        GlossyTransmission = Glossy | Transmission,
        SpecularReflection = Specular | Reflection,
        SpecularTransmission = Specular | Transmission,
        All = Diffuse | Glossy | Specular | Reflection | Transmission
    };

    SPECTRA_CPU_GPU
    inline BxDFFlags operator|(BxDFFlags a, BxDFFlags b)
    {
        return BxDFFlags((int)a | (int)b);
    }

    SPECTRA_CPU_GPU
    inline int operator&(BxDFFlags a, BxDFFlags b)
    {
        return ((int)a & (int)b);
    }

    SPECTRA_CPU_GPU
    inline int operator&(BxDFFlags a, BxDFReflTransFlags b)
    {
        return ((int)a & (int)b);
    }

    SPECTRA_CPU_GPU
    inline BxDFFlags& operator|=(BxDFFlags& a, BxDFFlags b)
    {
        (int&)a |= int(b);
        return a;
    }

    // BxDFFlags Inline Functions
    SPECTRA_CPU_GPU inline bool IsReflective(BxDFFlags f)
    {
        return f & Reflection;
    }

    SPECTRA_CPU_GPU inline bool IsTransmissive(BxDFFlags f)
    {
        return f & Transmission;
    }

    SPECTRA_CPU_GPU inline bool IsDiffuse(BxDFFlags f)
    {
        return f & Diffuse;
    }

    SPECTRA_CPU_GPU inline bool IsGlossy(BxDFFlags f)
    {
        return f & Glossy;
    }

    SPECTRA_CPU_GPU inline bool IsSpecular(BxDFFlags f)
    {
        return f & Specular;
    }

    SPECTRA_CPU_GPU inline bool IsNonSpecular(BxDFFlags f)
    {
        return f & (Diffuse | Glossy);
    }

    std::string ToString(BxDFFlags flags);

    // TransportMode Definition
    enum class TransportMode { Radiance, Importance };

    SPECTRA_CPU_GPU
    inline TransportMode operator!(TransportMode mode)
    {
        return (mode == TransportMode::Radiance)
                   ? TransportMode::Importance
                   : TransportMode::Radiance;
    }

    std::string ToString(TransportMode mode);

    // BSDFSample Definition
    struct BSDFSample
    {
        // BSDFSample Public Methods
        BSDFSample() = default;
        SPECTRA_CPU_GPU
        BSDFSample(SampledSpectrum f, Vector3f wi, Float pdf, BxDFFlags flags, Float eta = 1,
                   bool pdfIsProportional = false)
            : f(f),
              wi(wi),
              pdf(pdf),
              flags(flags),
              eta(eta),
              pdfIsProportional(pdfIsProportional)
        {
        }

        SPECTRA_CPU_GPU
        bool IsReflection() const { return IsReflective(flags); }
        SPECTRA_CPU_GPU
        bool IsTransmission() const { return IsTransmissive(flags); }
        SPECTRA_CPU_GPU
        bool IsDiffuse() const { return spectra::IsDiffuse(flags); }
        SPECTRA_CPU_GPU
        bool IsGlossy() const { return spectra::IsGlossy(flags); }
        SPECTRA_CPU_GPU
        bool IsSpecular() const { return spectra::IsSpecular(flags); }

        SampledSpectrum f;
        Vector3f wi;
        Float pdf = 0;
        BxDFFlags flags;
        Float eta = 1;
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
    class BxDF
        : public TaggedPointer<DiffuseTransmissionBxDF, DiffuseBxDF, CoatedDiffuseBxDF,
                               CoatedConductorBxDF, DielectricBxDF, ThinDielectricBxDF,
                               HairBxDF, MeasuredBxDF, ConductorBxDF, NormalizedFresnelBxDF>
    {
    public:
        // BxDF Interface
        SPECTRA_CPU_GPU inline BxDFFlags Flags() const;

        using TaggedPointer::TaggedPointer;


        SPECTRA_CPU_GPU inline SampledSpectrum f(Vector3f wo, Vector3f wi,
                                              TransportMode mode) const;

        SPECTRA_CPU_GPU inline pstd::optional<BSDFSample> Sample_f(
            Vector3f wo, Float uc, Point2f u, TransportMode mode = TransportMode::Radiance,
            BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

        SPECTRA_CPU_GPU inline Float PDF(
            Vector3f wo, Vector3f wi, TransportMode mode,
            BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

        SPECTRA_CPU_GPU
        SampledSpectrum rho(Vector3f wo, pstd::span<const Float> uc,
                            pstd::span<const Point2f> u2) const;
        SampledSpectrum rho(pstd::span<const Point2f> u1, pstd::span<const Float> uc2,
                            pstd::span<const Point2f> u2) const;

        SPECTRA_CPU_GPU inline void Regularize();
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_BXDF_H
