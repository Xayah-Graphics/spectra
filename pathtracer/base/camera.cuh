#ifndef SPECTRA_PATHTRACER_BASE_CAMERA_H
#define SPECTRA_PATHTRACER_BASE_CAMERA_H

#include <pathtracer/base/film.cuh>
#include <pathtracer/base/filter.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/taggedptr.cuh>
#include <pathtracer/util/transform.cuh>
#include <pathtracer/util/vecmath.cuh>
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


        __host__ __device__ pstd::optional<CameraRay> GenerateRay(CameraSample sample, SampledWavelengths& lambda) const;

        __host__ __device__ pstd::optional<CameraRayDifferential> GenerateRayDifferential(CameraSample sample, SampledWavelengths& lambda) const;

        __host__ __device__ Film GetFilm() const;

        __host__ __device__ Float SampleTime(Float u) const;

        void InitMetadata(ImageMetadata* metadata) const;

        __host__ __device__ const CameraTransform& GetCameraTransform() const;

        __host__ __device__ void Approximate_dp_dxy(Point3f p, Normal3f n, Float time, int samplesPerPixel, Vector3f* dpdx, Vector3f* dpdy) const;

        __host__ __device__ SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda, Point2f* pRasterOut = nullptr) const;

        __host__ __device__ void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const;

        __host__ __device__ pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u, SampledWavelengths& lambda) const;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_CAMERA_H
