#ifndef SPECTRA_PATHTRACER_CORE_CAMERAS_H
#define SPECTRA_PATHTRACER_CORE_CAMERAS_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/base/camera.h>
#include <spectra/pathtracer/base/film.h>
#include <spectra/pathtracer/core/film.h>
#include <spectra/pathtracer/core/interaction.h>
#include <spectra/pathtracer/core/ray.h>
#include <spectra/pathtracer/core/samplers.h>
#include <spectra/pathtracer/util/image.h>
#include <spectra/pathtracer/util/scattering.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace spectra
{
    // CameraTransform Definition
    class CameraTransform
    {
    public:
        // CameraTransform Public Methods
        CameraTransform() = default;
        explicit CameraTransform(const AnimatedTransform& worldFromCamera);

        SPECTRA_CPU_GPU
        Point3f RenderFromCamera(Point3f p, Float time) const
        {
            return renderFromCamera(p, time);
        }

        SPECTRA_CPU_GPU
        Point3f CameraFromRender(Point3f p, Float time) const
        {
            return renderFromCamera.ApplyInverse(p, time);
        }

        SPECTRA_CPU_GPU
        Point3f RenderFromWorld(Point3f p) const { return worldFromRender.ApplyInverse(p); }

        SPECTRA_CPU_GPU
        Transform RenderFromWorld() const { return Inverse(worldFromRender); }

        SPECTRA_CPU_GPU
        Transform CameraFromRender(Float time) const
        {
            return Inverse(renderFromCamera.Interpolate(time));
        }

        SPECTRA_CPU_GPU
        Transform CameraFromWorld(Float time) const
        {
            return Inverse(worldFromRender * renderFromCamera.Interpolate(time));
        }

        SPECTRA_CPU_GPU
        bool CameraFromRenderHasScale() const { return renderFromCamera.HasScale(); }

        SPECTRA_CPU_GPU
        Vector3f RenderFromCamera(Vector3f v, Float time) const
        {
            return renderFromCamera(v, time);
        }

        SPECTRA_CPU_GPU
        Normal3f RenderFromCamera(Normal3f n, Float time) const
        {
            return renderFromCamera(n, time);
        }

        SPECTRA_CPU_GPU
        Ray RenderFromCamera(const Ray& r) const { return renderFromCamera(r); }

        SPECTRA_CPU_GPU
        RayDifferential RenderFromCamera(const RayDifferential& r) const
        {
            return renderFromCamera(r);
        }

        SPECTRA_CPU_GPU
        Vector3f CameraFromRender(Vector3f v, Float time) const
        {
            return renderFromCamera.ApplyInverse(v, time);
        }

        SPECTRA_CPU_GPU
        Normal3f CameraFromRender(Normal3f v, Float time) const
        {
            return renderFromCamera.ApplyInverse(v, time);
        }

        SPECTRA_CPU_GPU
        const AnimatedTransform& RenderFromCamera() const { return renderFromCamera; }

        SPECTRA_CPU_GPU
        const Transform& WorldFromRender() const { return worldFromRender; }

    private:
        // CameraTransform Private Members
        AnimatedTransform renderFromCamera;
        Transform worldFromRender;
    };

    // CameraWiSample Definition
    struct CameraWiSample
    {
        // CameraWiSample Public Methods
        CameraWiSample() = default;
        SPECTRA_CPU_GPU
        CameraWiSample(const SampledSpectrum& Wi, const Vector3f& wi, Float pdf,
                       Point2f pRaster, const Interaction& pRef, const Interaction& pLens)
            : Wi(Wi), wi(wi), pdf(pdf), pRaster(pRaster), pRef(pRef), pLens(pLens)
        {
        }

        SampledSpectrum Wi;
        Vector3f wi;
        Float pdf;
        Point2f pRaster;
        Interaction pRef, pLens;
    };

    // CameraRay Definition
    struct CameraRay
    {
        Ray ray;
        SampledSpectrum weight = SampledSpectrum(1);
    };

    // CameraRayDifferential Definition
    struct CameraRayDifferential
    {
        RayDifferential ray;
        SampledSpectrum weight = SampledSpectrum(1);
    };

    // CameraBaseParameters Definition
    struct CameraBaseParameters
    {
        CameraTransform cameraTransform;
        Float shutterOpen = 0, shutterClose = 1;
        Film film;
        Medium medium;
        CameraBaseParameters() = default;
        CameraBaseParameters(const CameraTransform& cameraTransform, Film film, Medium medium,
                             const ParameterDictionary& parameters, const FileLoc* loc);
    };

    // CameraBase Definition
    class CameraBase
    {
    public:
        // CameraBase Public Methods
        SPECTRA_CPU_GPU
        Film GetFilm() const { return film; }

        SPECTRA_CPU_GPU
        const CameraTransform& GetCameraTransform() const { return cameraTransform; }

        SPECTRA_CPU_GPU
        Float SampleTime(Float u) const { return Lerp(u, shutterOpen, shutterClose); }

        void InitMetadata(ImageMetadata* metadata) const;

        SPECTRA_CPU_GPU
        void Approximate_dp_dxy(Point3f p, Normal3f n, Float time, int samplesPerPixel,
                                Vector3f* dpdx, Vector3f* dpdy) const
        {
            // Compute tangent plane equation for ray differential intersections
            Point3f pCamera = CameraFromRender(p, time);
            Transform DownZFromCamera =
                RotateFromTo(Normalize(Vector3f(pCamera)), Vector3f(0, 0, 1));
            Point3f pDownZ = DownZFromCamera(pCamera);
            Normal3f nDownZ = DownZFromCamera(CameraFromRender(n, time));
            Float d = nDownZ.z * pDownZ.z;

            // Find intersection points for approximated camera differential rays
            Ray xRay(Point3f(0, 0, 0) + minPosDifferentialX,
                     Vector3f(0, 0, 1) + minDirDifferentialX);
            Float tx = -(Dot(nDownZ, Vector3f(xRay.o)) - d) / Dot(nDownZ, xRay.d);
            Ray yRay(Point3f(0, 0, 0) + minPosDifferentialY,
                     Vector3f(0, 0, 1) + minDirDifferentialY);
            Float ty = -(Dot(nDownZ, Vector3f(yRay.o)) - d) / Dot(nDownZ, yRay.d);
            Point3f px = xRay(tx), py = yRay(ty);

            // Estimate $\dpdx$ and $\dpdy$ in tangent plane at intersection point
            Float sppScale =
                GetOptions().disablePixelJitter
                    ? 1
                    : std::max<Float>(.125, 1 / std::sqrt((Float)samplesPerPixel));
            *dpdx =
                sppScale * RenderFromCamera(DownZFromCamera.ApplyInverse(px - pDownZ), time);
            *dpdy =
                sppScale * RenderFromCamera(DownZFromCamera.ApplyInverse(py - pDownZ), time);
        }

    protected:
        // CameraBase Protected Members
        CameraTransform cameraTransform;
        Float shutterOpen, shutterClose;
        Film film;
        Medium medium;
        Vector3f minPosDifferentialX, minPosDifferentialY;
        Vector3f minDirDifferentialX, minDirDifferentialY;

        // CameraBase Protected Methods
        CameraBase() = default;
        CameraBase(CameraBaseParameters p);

        SPECTRA_CPU_GPU
        static pstd::optional<CameraRayDifferential> GenerateRayDifferential(
            Camera camera, CameraSample sample, SampledWavelengths& lambda);

        SPECTRA_CPU_GPU
        Ray RenderFromCamera(const Ray& r) const
        {
            return cameraTransform.RenderFromCamera(r);
        }

        SPECTRA_CPU_GPU
        RayDifferential RenderFromCamera(const RayDifferential& r) const
        {
            return cameraTransform.RenderFromCamera(r);
        }

        SPECTRA_CPU_GPU
        Vector3f RenderFromCamera(Vector3f v, Float time) const
        {
            return cameraTransform.RenderFromCamera(v, time);
        }

        SPECTRA_CPU_GPU
        Normal3f RenderFromCamera(Normal3f v, Float time) const
        {
            return cameraTransform.RenderFromCamera(v, time);
        }

        SPECTRA_CPU_GPU
        Point3f RenderFromCamera(Point3f p, Float time) const
        {
            return cameraTransform.RenderFromCamera(p, time);
        }

        SPECTRA_CPU_GPU
        Vector3f CameraFromRender(Vector3f v, Float time) const
        {
            return cameraTransform.CameraFromRender(v, time);
        }

        SPECTRA_CPU_GPU
        Normal3f CameraFromRender(Normal3f v, Float time) const
        {
            return cameraTransform.CameraFromRender(v, time);
        }

        SPECTRA_CPU_GPU
        Point3f CameraFromRender(Point3f p, Float time) const
        {
            return cameraTransform.CameraFromRender(p, time);
        }

        void FindMinimumDifferentials(Camera camera);
    };

    // ProjectiveCamera Definition
    class ProjectiveCamera : public CameraBase
    {
    public:
        // ProjectiveCamera Public Methods
        ProjectiveCamera() = default;
        void InitMetadata(ImageMetadata* metadata) const;


        ProjectiveCamera(CameraBaseParameters baseParameters,
                         const Transform& screenFromCamera, Bounds2f screenWindow,
                         Float lensRadius, Float focalDistance)
            : CameraBase(baseParameters),
              screenFromCamera(screenFromCamera),
              lensRadius(lensRadius),
              focalDistance(focalDistance)
        {
            // Compute projective camera transformations
            // Compute projective camera screen transformations
            Transform NDCFromScreen =
                Scale(1 / (screenWindow.pMax.x - screenWindow.pMin.x),
                      1 / (screenWindow.pMax.y - screenWindow.pMin.y), 1) *
                Translate(Vector3f(-screenWindow.pMin.x, -screenWindow.pMax.y, 0));
            Transform rasterFromNDC =
                Scale(film.FullResolution().x, -film.FullResolution().y, 1);
            rasterFromScreen = rasterFromNDC * NDCFromScreen;
            screenFromRaster = Inverse(rasterFromScreen);

            cameraFromRaster = Inverse(screenFromCamera) * screenFromRaster;
        }

    protected:
        // ProjectiveCamera Protected Members
        Transform screenFromCamera, cameraFromRaster;
        Transform rasterFromScreen, screenFromRaster;
        Float lensRadius, focalDistance;
    };

    // OrthographicCamera Definition
    class OrthographicCamera : public ProjectiveCamera
    {
    public:
        // OrthographicCamera Public Methods
        OrthographicCamera(CameraBaseParameters baseParameters, Bounds2f screenWindow,
                           Float lensRadius, Float focalDist)
            : ProjectiveCamera(baseParameters, Orthographic(0, 1), screenWindow, lensRadius,
                               focalDist)
        {
            // Compute differential changes in origin for orthographic camera rays
            dxCamera = cameraFromRaster(Vector3f(1, 0, 0));
            dyCamera = cameraFromRaster(Vector3f(0, 1, 0));

            // Compute minimum differentials for orthographic camera
            minDirDifferentialX = minDirDifferentialY = Vector3f(0, 0, 0);
            minPosDifferentialX = dxCamera;
            minPosDifferentialY = dyCamera;
        }

        SPECTRA_CPU_GPU
        pstd::optional<CameraRay> GenerateRay(CameraSample sample,
                                              SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraRayDifferential> GenerateRayDifferential(
            CameraSample sample, SampledWavelengths& lambda) const;

        static OrthographicCamera* Create(const ParameterDictionary& parameters,
                                          const CameraTransform& cameraTransform, Film film,
                                          Medium medium, const FileLoc* loc,
                                          Allocator alloc = {});

        SPECTRA_CPU_GPU
        SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda,
                           Point2f* pRaster2 = nullptr) const
        {
            SPECTRA_FATAL("We() unimplemented for OrthographicCamera");
            return {};
        }

        SPECTRA_CPU_GPU
        void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const
        {
            SPECTRA_FATAL("PDF_We() unimplemented for OrthographicCamera");
        }

        SPECTRA_CPU_GPU
        pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u,
                                                SampledWavelengths& lambda) const
        {
            SPECTRA_FATAL("SampleWi() unimplemented for OrthographicCamera");
            return {};
        }

    private:
        // OrthographicCamera Private Members
        Vector3f dxCamera, dyCamera;
    };

    // PerspectiveCamera Definition
    class PerspectiveCamera : public ProjectiveCamera
    {
    public:
        // PerspectiveCamera Public Methods
        PerspectiveCamera(CameraBaseParameters baseParameters, Float fov,
                          Bounds2f screenWindow, Float lensRadius, Float focalDist)
            : ProjectiveCamera(baseParameters, Perspective(fov, 1e-2f, 1000.f), screenWindow,
                               lensRadius, focalDist)
        {
            // Compute differential changes in origin for perspective camera rays
            dxCamera =
                cameraFromRaster(Point3f(1, 0, 0)) - cameraFromRaster(Point3f(0, 0, 0));
            dyCamera =
                cameraFromRaster(Point3f(0, 1, 0)) - cameraFromRaster(Point3f(0, 0, 0));

            // Compute _cosTotalWidth_ for perspective camera
            Point2f radius = Point2f(film.GetFilter().Radius());
            Point3f pCorner(-radius.x, -radius.y, 0.f);
            Vector3f wCornerCamera = Normalize(Vector3f(cameraFromRaster(pCorner)));
            cosTotalWidth = wCornerCamera.z;
            DCHECK_LT(.9999 * cosTotalWidth, std::cos(Radians(fov / 2)));

            // Compute image plane area at $z=1$ for _PerspectiveCamera_
            Point2i res = film.FullResolution();
            Point3f pMin = cameraFromRaster(Point3f(0, 0, 0));
            Point3f pMax = cameraFromRaster(Point3f(res.x, res.y, 0));
            pMin /= pMin.z;
            pMax /= pMax.z;
            A = std::abs((pMax.x - pMin.x) * (pMax.y - pMin.y));

            // Compute minimum differentials for _PerspectiveCamera_
            FindMinimumDifferentials(this);
        }

        PerspectiveCamera() = default;

        static PerspectiveCamera* Create(const ParameterDictionary& parameters,
                                         const CameraTransform& cameraTransform, Film film,
                                         Medium medium, const FileLoc* loc,
                                         Allocator alloc = {});

        SPECTRA_CPU_GPU
        pstd::optional<CameraRay> GenerateRay(CameraSample sample,
                                              SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraRayDifferential> GenerateRayDifferential(
            CameraSample sample, SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda,
                           Point2f* pRaster2 = nullptr) const;
        SPECTRA_CPU_GPU
        void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const;
        SPECTRA_CPU_GPU
        pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u,
                                                SampledWavelengths& lambda) const;

    private:
        // PerspectiveCamera Private Members
        Vector3f dxCamera, dyCamera;
        Float cosTotalWidth;
        Float A;
    };

    // SphericalCamera Definition
    class SphericalCamera : public CameraBase
    {
    public:
        // SphericalCamera::Mapping Definition
        enum Mapping { EquiRectangular, EqualArea };

        // SphericalCamera Public Methods
        SphericalCamera(CameraBaseParameters baseParameters, Mapping mapping)
            : CameraBase(baseParameters), mapping(mapping)
        {
            // Compute minimum differentials for _SphericalCamera_
            FindMinimumDifferentials(this);
        }

        static SphericalCamera* Create(const ParameterDictionary& parameters,
                                       const CameraTransform& cameraTransform, Film film,
                                       Medium medium, const FileLoc* loc,
                                       Allocator alloc = {});

        SPECTRA_CPU_GPU
        pstd::optional<CameraRay> GenerateRay(CameraSample sample,
                                              SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraRayDifferential> GenerateRayDifferential(
            CameraSample sample, SampledWavelengths& lambda) const
        {
            return CameraBase::GenerateRayDifferential(this, sample, lambda);
        }

        SPECTRA_CPU_GPU
        SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda,
                           Point2f* pRaster2 = nullptr) const
        {
            SPECTRA_FATAL("We() unimplemented for SphericalCamera");
            return {};
        }

        SPECTRA_CPU_GPU
        void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const
        {
            SPECTRA_FATAL("PDF_We() unimplemented for SphericalCamera");
        }

        SPECTRA_CPU_GPU
        pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u,
                                                SampledWavelengths& lambda) const
        {
            SPECTRA_FATAL("SampleWi() unimplemented for SphericalCamera");
            return {};
        }

    private:
        // SphericalCamera Private Members
        Mapping mapping;
    };

    // ExitPupilSample Definition
    struct ExitPupilSample
    {
        Point3f pPupil;
        Float pdf;
    };

    // RealisticCamera Definition
    class RealisticCamera : public CameraBase
    {
    public:
        // RealisticCamera Public Methods
        RealisticCamera(CameraBaseParameters baseParameters,
                        std::vector<Float>& lensParameters, Float focusDistance,
                        Float apertureDiameter, Image apertureImage, Allocator alloc);

        static RealisticCamera* Create(const ParameterDictionary& parameters,
                                       const CameraTransform& cameraTransform, Film film,
                                       Medium medium, const FileLoc* loc,
                                       Allocator alloc = {});

        SPECTRA_CPU_GPU
        pstd::optional<CameraRay> GenerateRay(CameraSample sample,
                                              SampledWavelengths& lambda) const;

        SPECTRA_CPU_GPU
        pstd::optional<CameraRayDifferential> GenerateRayDifferential(
            CameraSample sample, SampledWavelengths& lambda) const
        {
            return CameraBase::GenerateRayDifferential(this, sample, lambda);
        }

        SPECTRA_CPU_GPU
        SampledSpectrum We(const Ray& ray, SampledWavelengths& lambda,
                           Point2f* pRaster2 = nullptr) const
        {
            SPECTRA_FATAL("We() unimplemented for RealisticCamera");
            return {};
        }

        SPECTRA_CPU_GPU
        void PDF_We(const Ray& ray, Float* pdfPos, Float* pdfDir) const
        {
            SPECTRA_FATAL("PDF_We() unimplemented for RealisticCamera");
        }

        SPECTRA_CPU_GPU
        pstd::optional<CameraWiSample> SampleWi(const Interaction& ref, Point2f u,
                                                SampledWavelengths& lambda) const
        {
            SPECTRA_FATAL("SampleWi() unimplemented for RealisticCamera");
            return {};
        }

    private:
        // RealisticCamera Private Declarations
        struct LensElementInterface
        {
            Float curvatureRadius;
            Float thickness;
            Float eta;
            Float apertureRadius;
        };

        // RealisticCamera Private Methods
        SPECTRA_CPU_GPU
        Float LensRearZ() const { return elementInterfaces.back().thickness; }

        SPECTRA_CPU_GPU
        Float LensFrontZ() const
        {
            Float zSum = 0;
            for (const LensElementInterface& element : elementInterfaces)
                zSum += element.thickness;
            return zSum;
        }

        SPECTRA_CPU_GPU
        Float RearElementRadius() const { return elementInterfaces.back().apertureRadius; }

        SPECTRA_CPU_GPU
        Float TraceLensesFromFilm(const Ray& rCamera, Ray* rOut) const;

        SPECTRA_CPU_GPU
        static bool IntersectSphericalElement(Float radius, Float zCenter, const Ray& ray,
                                              Float* t, Normal3f* n)
        {
            // Compute _t0_ and _t1_ for ray--element intersection
            Point3f o = ray.o - Vector3f(0, 0, zCenter);
            Float A = ray.d.x * ray.d.x + ray.d.y * ray.d.y + ray.d.z * ray.d.z;
            Float B = 2 * (ray.d.x * o.x + ray.d.y * o.y + ray.d.z * o.z);
            Float C = o.x * o.x + o.y * o.y + o.z * o.z - radius * radius;
            Float t0, t1;
            if (!Quadratic(A, B, C, &t0, &t1))
                return false;

            // Select intersection $t$ based on ray direction and element curvature
            bool useCloserT = (ray.d.z > 0) ^ (radius < 0);
            *t = useCloserT ? std::min(t0, t1) : std::max(t0, t1);
            if (*t < 0)
                return false;

            // Compute surface normal of element at ray intersection point
            *n = Normal3f(Vector3f(o + *t * ray.d));
            *n = FaceForward(Normalize(*n), -ray.d);

            return true;
        }

        SPECTRA_CPU_GPU
        Float TraceLensesFromScene(const Ray& rCamera, Ray* rOut) const;

        void DrawLensSystem() const;
        void DrawRayPathFromFilm(const Ray& r, bool arrow, bool toOpticalIntercept) const;
        void DrawRayPathFromScene(const Ray& r, bool arrow, bool toOpticalIntercept) const;

        static void ComputeCardinalPoints(Ray rIn, Ray rOut, Float* p, Float* f);
        void ComputeThickLensApproximation(Float pz[2], Float f[2]) const;
        Float FocusThickLens(Float focusDistance);
        Bounds2f BoundExitPupil(Float filmX0, Float filmX1) const;
        void RenderExitPupil(Float sx, Float sy, const char* filename) const;

        SPECTRA_CPU_GPU
        pstd::optional<ExitPupilSample> SampleExitPupil(Point2f pFilm, Point2f uLens) const;

        void TestExitPupilBounds() const;

        // RealisticCamera Private Members
        Bounds2f physicalExtent;
        pstd::vector<LensElementInterface> elementInterfaces;
        Image apertureImage;
        pstd::vector<Bounds2f> exitPupilBounds;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_CORE_CAMERAS_H
