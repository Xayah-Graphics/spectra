#ifndef SPECTRA_PATHTRACER_BASE_CAMERA_H
#define SPECTRA_PATHTRACER_BASE_CAMERA_H

#include <spectra/pathtracer/base/film.h>
#include <spectra/pathtracer/base/filter.h>
#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/transform.h>
#include <spectra/pathtracer/util/vecmath.h>
#include <string>

namespace spectra {
    class Interaction;
    class ParameterDictionary;
    class Ray;
    class SampledSpectrum;
    class SampledWavelengths;
    struct FileLoc;
    struct ImageMetadata;

    // Camera Declarations
    struct CameraRay;
    struct CameraRayDifferential;
    struct CameraWiSample;

    struct CameraSample;
    class CameraTransform;

    class PerspectiveCamera;
    class OrthographicCamera;
    class SphericalCamera;
    class RealisticCamera;

    // Camera Definition
    class Camera : public TaggedPointer<PerspectiveCamera, OrthographicCamera, SphericalCamera, RealisticCamera> {
    public:
        // Camera Interface
        using TaggedPointer::TaggedPointer;

        static Camera Create(const std::string& name, const ParameterDictionary& parameters, Medium medium, const CameraTransform& cameraTransform, Film film, const FileLoc* loc, Allocator alloc);


        SPECTRA_CPU_GPU pstd::optional<CameraRay> GenerateRay(CameraSample sample, SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraRayDifferential> GenerateRayDifferential(CameraSample sample, SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU Film GetFilm() const;

        SPECTRA_CPU_GPU Float SampleTime(Float u) const;

        void InitMetadata(ImageMetadata* metadata) const;

        SPECTRA_CPU_GPU const CameraTransform& GetCameraTransform() const;

        SPECTRA_CPU_GPU
        void Approximate_dp_dxy(Point3f p, Normal3f n, Float time, int samplesPerPixel, Vector3f* dpdx, Vector3f* dpdy) const;

        SPECTRA_CPU_GPU
        SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda, Point2f* pRasterOut = nullptr) const;

        SPECTRA_CPU_GPU
        void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u, SampledWavelengths& lambda) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_CAMERA_H
