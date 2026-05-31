#ifndef SPECTRA_PATHTRACER_BASE_LIGHT_H
#define SPECTRA_PATHTRACER_BASE_LIGHT_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/base/medium.h>
#include <spectra/pathtracer/base/shape.h>
#include <spectra/pathtracer/base/texture.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/taggedptr.h>

#include <string>

namespace spectra
{
    class CameraTransform;
    class Interaction;
    class ParameterDictionary;
    class Ray;
    class SampledSpectrum;
    class SampledWavelengths;
    class Transform;
    struct FileLoc;

    // LightType Definition
    enum class LightType { DeltaPosition, DeltaDirection, Area, Infinite };

    class PointLight;
    class DistantLight;
    class ProjectionLight;
    class GoniometricLight;
    class DiffuseAreaLight;
    class UniformInfiniteLight;
    class ImageInfiniteLight;
    class PortalImageInfiniteLight;
    class SpotLight;

    class LightSampleContext;
    class LightBounds;
    class CompactLightBounds;
    struct LightLiSample;
    struct LightLeSample;

    // Light Definition
    class Light : public TaggedPointer< // Light Source Types
            PointLight, DistantLight, ProjectionLight, GoniometricLight, SpotLight,
            DiffuseAreaLight, UniformInfiniteLight, ImageInfiniteLight,
            PortalImageInfiniteLight

        >
    {
    public:
        // Light Interface
        using TaggedPointer::TaggedPointer;

        static Light Create(const std::string& name, const ParameterDictionary& parameters,
                            const Transform& renderFromLight,
                            const CameraTransform& cameraTransform, Medium outsideMedium,
                            const FileLoc* loc, Allocator alloc);
        static Light CreateArea(const std::string& name,
                                const ParameterDictionary& parameters,
                                const Transform& renderFromLight,
                                const MediumInterface& mediumInterface, const Shape shape,
                                FloatTexture alpha, const FileLoc* loc, Allocator alloc);

        SampledSpectrum Phi(SampledWavelengths lambda) const;

        SPECTRA_CPU_GPU inline LightType Type() const;

        SPECTRA_CPU_GPU inline pstd::optional<LightLiSample> SampleLi(
            LightSampleContext ctx, Point2f u, SampledWavelengths lambda,
            bool allowIncompletePDF = false) const;

        SPECTRA_CPU_GPU inline Float PDF_Li(LightSampleContext ctx, Vector3f wi,
                                         bool allowIncompletePDF = false) const;


        // AreaLights only
        SPECTRA_CPU_GPU inline SampledSpectrum L(Point3f p, Normal3f n, Point2f uv, Vector3f w,
                                              const SampledWavelengths& lambda) const;

        // InfiniteLights only
        SPECTRA_CPU_GPU inline SampledSpectrum Le(const Ray& ray,
                                               const SampledWavelengths& lambda) const;

        void Preprocess(const Bounds3f& sceneBounds);

        pstd::optional<LightBounds> Bounds() const;

        SPECTRA_CPU_GPU
        pstd::optional<LightLeSample> SampleLe(Point2f u1, Point2f u2,
                                               SampledWavelengths& lambda, Float time) const;

        SPECTRA_CPU_GPU
        void PDF_Le(const Ray& ray, Float* pdfPos, Float* pdfDir) const;

        // AreaLights only
        SPECTRA_CPU_GPU
        void PDF_Le(const Interaction& intr, Vector3f w, Float* pdfPos, Float* pdfDir) const;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_LIGHT_H
