#ifndef SPECTRA_PATHTRACER_CORE_FILM_H
#define SPECTRA_PATHTRACER_CORE_FILM_H

// PhysLight code contributed by Anders Langlands and Luca Fascione
// Copyright (c) 2020, Weta Digital, Ltd.
// SPDX-License-Identifier: Apache-2.0

#include <atomic>
#include <map>
#include <spectra/pathtracer/base/bxdf.cuh>
#include <spectra/pathtracer/base/camera.cuh>
#include <spectra/pathtracer/base/film.cuh>
#include <spectra/pathtracer/core/bsdf.cuh>
#include <spectra/pathtracer/util/color.cuh>
#include <spectra/pathtracer/util/colorspace.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/parallel.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/sampling.cuh>
#include <spectra/pathtracer/util/spectrum.cuh>
#include <spectra/pathtracer/util/transform.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>
#include <thread>
#include <vector>

namespace spectra {
    // PixelSensor Definition
    class PixelSensor {
    public:
        // PixelSensor Public Methods
        static PixelSensor* Create(const ParameterDictionary& parameters, const RGBColorSpace* colorSpace, Float exposureTime, const FileLoc* loc, Allocator alloc);

        static PixelSensor* CreateDefault(Allocator alloc = {});

        PixelSensor(Spectrum r, Spectrum g, Spectrum b, const RGBColorSpace* outputColorSpace, Spectrum sensorIllum, Float imagingRatio, Allocator alloc) : r_bar(r, alloc), g_bar(g, alloc), b_bar(b, alloc), imagingRatio(imagingRatio) {
            // Compute XYZ from camera RGB matrix
            // Compute _rgbCamera_ values for training swatches
            Float rgbCamera[nSwatchReflectances][3];
            for (int i = 0; i < nSwatchReflectances; ++i) {
                RGB rgb = ProjectReflectance<RGB>(swatchReflectances[i], sensorIllum, &r_bar, &g_bar, &b_bar);
                for (int c = 0; c < 3; ++c) rgbCamera[i][c] = rgb[c];
            }

            // Compute _xyzOutput_ values for training swatches
            Float xyzOutput[24][3];
            Float sensorWhiteG = InnerProduct(sensorIllum, &g_bar);
            Float sensorWhiteY = InnerProduct(sensorIllum, &Spectra::Y());
            for (size_t i = 0; i < nSwatchReflectances; ++i) {
                Spectrum s = swatchReflectances[i];
                XYZ xyz    = ProjectReflectance<XYZ>(s, &outputColorSpace->illuminant, &Spectra::X(), &Spectra::Y(), &Spectra::Z()) * (sensorWhiteY / sensorWhiteG);
                for (int c = 0; c < 3; ++c) xyzOutput[i][c] = xyz[c];
            }

            // Initialize _XYZFromSensorRGB_ using linear least squares
            pstd::optional<SquareMatrix<3>> m = LinearLeastSquares(rgbCamera, xyzOutput, nSwatchReflectances);
            if (!m) throw std::runtime_error(spectra::diagnostics::Format("Sensor XYZ from RGB matrix could not be solved."));
            XYZFromSensorRGB = *m;
        }

        PixelSensor(const RGBColorSpace* outputColorSpace, Spectrum sensorIllum, Float imagingRatio, Allocator alloc) : r_bar(&Spectra::X(), alloc), g_bar(&Spectra::Y(), alloc), b_bar(&Spectra::Z(), alloc), imagingRatio(imagingRatio) {
            // Compute white balancing matrix for XYZ _PixelSensor_
            if (sensorIllum) {
                Point2f sourceWhite = SpectrumToXYZ(sensorIllum).xy();
                Point2f targetWhite = outputColorSpace->w;
                XYZFromSensorRGB    = WhiteBalance(sourceWhite, targetWhite);
            }
        }

        __host__ __device__ RGB ToSensorRGB(SampledSpectrum L, const SampledWavelengths& lambda) const {
            L = SafeDiv(L, lambda.PDF());
            return imagingRatio * RGB((r_bar.Sample(lambda) * L).Average(), (g_bar.Sample(lambda) * L).Average(), (b_bar.Sample(lambda) * L).Average());
        }

        // PixelSensor Public Members
        SquareMatrix<3> XYZFromSensorRGB;

    private:
        // PixelSensor Private Methods
        template <typename Triplet>
        static Triplet ProjectReflectance(Spectrum r, Spectrum illum, Spectrum b1, Spectrum b2, Spectrum b3);

        // PixelSensor Private Members
        DenselySampledSpectrum r_bar, g_bar, b_bar;
        Float imagingRatio;
        static constexpr int nSwatchReflectances = 24;
        static Spectrum swatchReflectances[nSwatchReflectances];
    };

    // PixelSensor Inline Methods
    template <typename Triplet>
    inline Triplet PixelSensor::ProjectReflectance(Spectrum refl, Spectrum illum, Spectrum b1, Spectrum b2, Spectrum b3) {
        Triplet result;
        Float g_integral = 0;
        for (Float lambda = Lambda_min; lambda <= Lambda_max; ++lambda) {
            g_integral += b2(lambda) * illum(lambda);
            result[0] += b1(lambda) * refl(lambda) * illum(lambda);
            result[1] += b2(lambda) * refl(lambda) * illum(lambda);
            result[2] += b3(lambda) * refl(lambda) * illum(lambda);
        }
        return result / g_integral;
    }

    // VisibleSurface Definition
    class VisibleSurface {
    public:
        // VisibleSurface Public Methods
        __host__ __device__ VisibleSurface(const SurfaceInteraction& si, SampledSpectrum albedo, const SampledWavelengths& lambda);

        __host__ __device__ operator bool() const {
            return set;
        }

        VisibleSurface() = default;


        // VisibleSurface Public Members
        Point3f p;
        Normal3f n, ns;
        Point2f uv;
        Float time = 0;
        Vector3f dpdx, dpdy;
        SampledSpectrum albedo;
        bool set = false;
    };

    // FilmBaseParameters Definition
    struct FilmBaseParameters {
        FilmBaseParameters(const ParameterDictionary& parameters, Filter filter, const PixelSensor* sensor, const FileLoc* loc);

        FilmBaseParameters(Point2i fullResolution, Bounds2i pixelBounds, Filter filter, Float diagonal, const PixelSensor* sensor, std::string filename) : fullResolution(fullResolution), pixelBounds(pixelBounds), filter(filter), diagonal(diagonal), sensor(sensor), filename(filename) {}

        Point2i fullResolution;
        Bounds2i pixelBounds;
        Filter filter;
        Float diagonal;
        const PixelSensor* sensor;
        std::string filename;
    };

    // FilmBase Definition
    class FilmBase {
    public:
        // FilmBase Public Methods
        FilmBase(FilmBaseParameters p) : fullResolution(p.fullResolution), pixelBounds(p.pixelBounds), filter(p.filter), diagonal(p.diagonal * .001f), sensor(p.sensor), filename(p.filename) {
            CHECK(!pixelBounds.IsEmpty());
            CHECK_GE(pixelBounds.pMin.x, 0);
            CHECK_LE(pixelBounds.pMax.x, fullResolution.x);
            CHECK_GE(pixelBounds.pMin.y, 0);
            CHECK_LE(pixelBounds.pMax.y, fullResolution.y);
        }

        __host__ __device__ Point2i FullResolution() const {
            return fullResolution;
        }

        __host__ __device__ Bounds2i PixelBounds() const {
            return pixelBounds;
        }

        __host__ __device__ Float Diagonal() const {
            return diagonal;
        }

        __host__ __device__ Filter GetFilter() const {
            return filter;
        }

        __host__ __device__ const PixelSensor* GetPixelSensor() const {
            return sensor;
        }

        std::string GetFilename() const {
            return filename;
        }

        __host__ __device__ SampledWavelengths SampleWavelengths(Float u) const {
            return SampledWavelengths::SampleVisible(u);
        }

        __host__ __device__ Bounds2f SampleBounds() const;

    protected:
        // FilmBase Protected Members
        Point2i fullResolution;
        Bounds2i pixelBounds;
        Filter filter;
        Float diagonal;
        const PixelSensor* sensor;
        std::string filename;
    };

    // RGBFilm Definition
    class RGBFilm : public FilmBase {
    public:
        // RGBFilm Public Methods
        __host__ __device__ bool UsesVisibleSurface() const {
            return false;
        }

        __host__ __device__ void AddSample(Point2i pFilm, SampledSpectrum L, const SampledWavelengths& lambda, const VisibleSurface*, Float weight) {
            // Convert sample radiance to _PixelSensor_ RGB
            RGB rgb = sensor->ToSensorRGB(L, lambda);

            // Optionally clamp sensor RGB value
            Float m = MaxComponentValue(rgb);
            if (m > maxComponentValue) rgb *= maxComponentValue / m;

            DCHECK(InsideExclusive(pFilm, pixelBounds));
            // Update pixel values with filtered sample contribution
            Pixel& pixel = pixels[pFilm];
            for (int c = 0; c < 3; ++c) pixel.rgbSum[c] += weight * rgb[c];
            pixel.weightSum += weight;
        }

        __host__ __device__ RGB GetPixelRGB(Point2i p, Float splatScale = 1) const {
            const Pixel& pixel = pixels[p];
            RGB rgb(pixel.rgbSum[0], pixel.rgbSum[1], pixel.rgbSum[2]);
            // Normalize _rgb_ with weight sum
            Float weightSum = pixel.weightSum;
            if (weightSum != 0) rgb /= weightSum;

            // Add splat value at pixel
            for (int c = 0; c < 3; ++c) rgb[c] += splatScale * pixel.rgbSplat[c] / filterIntegral;

            // Convert _rgb_ to output RGB color space
            rgb = outputRGBFromSensorRGB * rgb;

            return rgb;
        }

        RGBFilm(FilmBaseParameters p, const RGBColorSpace* colorSpace, Float maxComponentValue = Infinity, bool writeFP16 = true, Allocator alloc = {});

        static RGBFilm* Create(const ParameterDictionary& parameters, Float exposureTime, Filter filter, const RGBColorSpace* colorSpace, const FileLoc* loc, Allocator alloc);

        __host__ __device__ void AddSplat(Point2f p, SampledSpectrum v, const SampledWavelengths& lambda);

        void WriteImage(ImageMetadata metadata, Float splatScale = 1);
        Image GetImage(ImageMetadata* metadata, Float splatScale = 1);


        __host__ __device__ RGB ToOutputRGB(SampledSpectrum L, const SampledWavelengths& lambda) const {
            RGB sensorRGB = sensor->ToSensorRGB(L, lambda);
            return outputRGBFromSensorRGB * sensorRGB;
        }

        __host__ __device__ void ResetPixel(Point2i p) {
            memset(&pixels[p], 0, sizeof(Pixel));
        }

    private:
        // RGBFilm::Pixel Definition
        struct Pixel {
            Pixel()          = default;
            double rgbSum[3] = {0., 0., 0.};
            double weightSum = 0.;
            AtomicDouble rgbSplat[3];
        };

        // RGBFilm Private Members
        const RGBColorSpace* colorSpace;
        Float maxComponentValue;
        bool writeFP16;
        Float filterIntegral;
        SquareMatrix<3> outputRGBFromSensorRGB;
        Array2D<Pixel> pixels;
    };

    // GBufferFilm Definition
    class GBufferFilm : public FilmBase {
    public:
        // GBufferFilm Public Methods
        GBufferFilm(FilmBaseParameters p, const AnimatedTransform& outputFromRender, bool applyInverse, const RGBColorSpace* colorSpace, Float maxComponentValue = Infinity, bool writeFP16 = true, Allocator alloc = {});

        static GBufferFilm* Create(const ParameterDictionary& parameters, Float exposureTime, const CameraTransform& cameraTransform, Filter filter, const RGBColorSpace* colorSpace, const FileLoc* loc, Allocator alloc);

        __host__ __device__ void AddSample(Point2i pFilm, SampledSpectrum L, const SampledWavelengths& lambda, const VisibleSurface* visibleSurface, Float weight);

        __host__ __device__ void AddSplat(Point2f p, SampledSpectrum v, const SampledWavelengths& lambda);

        __host__ __device__ RGB ToOutputRGB(SampledSpectrum L, const SampledWavelengths& lambda) const {
            RGB cameraRGB = sensor->ToSensorRGB(L, lambda);
            return outputRGBFromSensorRGB * cameraRGB;
        }

        __host__ __device__ bool UsesVisibleSurface() const {
            return true;
        }

        __host__ __device__ RGB GetPixelRGB(Point2i p, Float splatScale = 1) const {
            const Pixel& pixel = pixels[p];
            RGB rgb(pixel.rgbSum[0], pixel.rgbSum[1], pixel.rgbSum[2]);

            // Normalize pixel with weight sum
            Float weightSum = pixel.weightSum;
            if (weightSum != 0) rgb /= weightSum;

            // Add splat value at pixel
            for (int c = 0; c < 3; ++c) rgb[c] += splatScale * pixel.rgbSplat[c] / filterIntegral;

            rgb = outputRGBFromSensorRGB * rgb;

            return rgb;
        }

        void WriteImage(ImageMetadata metadata, Float splatScale = 1);
        Image GetImage(ImageMetadata* metadata, Float splatScale = 1);


        __host__ __device__ void ResetPixel(Point2i p) {
            memset(&pixels[p], 0, sizeof(Pixel));
        }

    private:
        // GBufferFilm::Pixel Definition
        struct Pixel {
            Pixel()          = default;
            double rgbSum[3] = {0., 0., 0.};
            double weightSum = 0., gBufferWeightSum = 0.;
            AtomicDouble rgbSplat[3];
            Point3f pSum;
            Float dzdxSum = 0, dzdySum = 0;
            Normal3f nSum, nsSum;
            Point2f uvSum;
            double rgbAlbedoSum[3] = {0., 0., 0.};
            VarianceEstimator<Float> rgbVariance[3];
        };

        // GBufferFilm Private Members
        AnimatedTransform outputFromRender;
        bool applyInverse;
        Array2D<Pixel> pixels;
        const RGBColorSpace* colorSpace;
        Float maxComponentValue;
        bool writeFP16;
        Float filterIntegral;
        SquareMatrix<3> outputRGBFromSensorRGB;
    };

    // SpectralFilm Definition
    class SpectralFilm : public FilmBase {
    public:
        // SpectralFilm Public Methods
        __host__ __device__ bool UsesVisibleSurface() const {
            return false;
        }

        __host__ __device__ SampledWavelengths SampleWavelengths(Float u) const {
            return SampledWavelengths::SampleUniform(u, lambdaMin, lambdaMax);
        }

        __host__ __device__ void AddSample(Point2i pFilm, SampledSpectrum L, const SampledWavelengths& lambda, const VisibleSurface*, Float weight) {
            // Start by doing more or less what RGBFilm::AddSample() does so
            // that we can maintain accurate RGB values.

            // Convert sample radiance to _PixelSensor_ RGB
            RGB rgb = sensor->ToSensorRGB(L, lambda);

            // Optionally clamp sensor RGB value
            Float m = MaxComponentValue(rgb);
            if (m > maxComponentValue) rgb *= maxComponentValue / m;

            DCHECK(InsideExclusive(pFilm, pixelBounds));
            // Update RGB fields in Pixel structure.
            Pixel& pixel = pixels[pFilm];
            for (int c = 0; c < 3; ++c) pixel.rgbSum[c] += weight * rgb[c];
            pixel.rgbWeightSum += weight;

            // Spectral processing starts here.
            // Optionally clamp spectral value. (TODO: for spectral should we
            // just clamp channels individually?)
            Float lm = L.MaxComponentValue();
            if (lm > maxComponentValue) L *= maxComponentValue / lm;

            // The CIE_Y_integral factor effectively cancels out the effect of
            // the conversion of light sources to use photometric units for
            // specification.  We then do *not* divide by the PDF in |lambda|
            // but take advantage of the fact that we know that it is uniform
            // in SampleWavelengths(), the fact that the buckets all have the
            // same extend, and can then just average radiance in buckets
            // below.
            L *= weight * CIE_Y_integral;

            // Accumulate contributions in spectral buckets.
            for (int i = 0; i < NSpectrumSamples; ++i) {
                int b = LambdaToBucket(lambda[i]);
                pixel.bucketSums[b] += L[i];
                pixel.weightSums[b] += weight;
            }
        }

        __host__ __device__ RGB GetPixelRGB(Point2i p, Float splatScale = 1) const;

        SpectralFilm(FilmBaseParameters p, Float lambdaMin, Float lambdaMax, int nBuckets, const RGBColorSpace* colorSpace, Float maxComponentValue = Infinity, bool writeFP16 = true, Allocator alloc = {});

        static SpectralFilm* Create(const ParameterDictionary& parameters, Float exposureTime, Filter filter, const RGBColorSpace* colorSpace, const FileLoc* loc, Allocator alloc);

        __host__ __device__ void AddSplat(Point2f p, SampledSpectrum v, const SampledWavelengths& lambda);

        void WriteImage(ImageMetadata metadata, Float splatScale = 1);

        // Returns an image with both RGB and spectral components, following
        // the layout proposed in "An OpenEXR Layout for Sepctral Images" by
        // Fichet et al., https://jcgt.org/published/0010/03/01/.
        Image GetImage(ImageMetadata* metadata, Float splatScale = 1);


        __host__ __device__ RGB ToOutputRGB(SampledSpectrum L, const SampledWavelengths& lambda) const {
            SPECTRA_FATAL("ToOutputRGB() is unimplemented. But that's ok since it's only used "
                          "in the SPPM integrator, which is inherently very much based on "
                          "RGB output.");
            return {};
        }

        __host__ __device__ void ResetPixel(Point2i p) {
            Pixel& pix    = pixels[p];
            pix.rgbSum[0] = pix.rgbSum[1] = pix.rgbSum[2] = 0.;
            pix.rgbWeightSum                              = 0.;
            pix.rgbSplat[0] = pix.rgbSplat[1] = pix.rgbSplat[2] = 0.;
            memset(pix.bucketSums, 0, nBuckets * sizeof(double));
            memset(pix.weightSums, 0, nBuckets * sizeof(double));
            memset(pix.bucketSplats, 0, nBuckets * sizeof(AtomicDouble));
        }

    private:
        __host__ __device__ int LambdaToBucket(Float lambda) const {
            DCHECK_RARE(1e6f, lambda < lambdaMin || lambda > lambdaMax);
            int bucket = nBuckets * (lambda - lambdaMin) / (lambdaMax - lambdaMin);
            return Clamp(bucket, 0, nBuckets - 1);
        }

        // SpectralFilm::Pixel Definition
        struct Pixel {
            Pixel() = default;
            // Continue to store RGB for final-image and intermediate image reads.
            double rgbSum[3]    = {0., 0., 0.};
            double rgbWeightSum = 0.;
            AtomicDouble rgbSplat[3];
            // The following will all have nBuckets entries.
            double *bucketSums, *weightSums;
            AtomicDouble* bucketSplats;
        };

        // SpectralFilm Private Members
        const RGBColorSpace* colorSpace;
        Float lambdaMin, lambdaMax;
        int nBuckets;
        Float maxComponentValue;
        bool writeFP16;
        Float filterIntegral;
        Array2D<Pixel> pixels;
        SquareMatrix<3> outputRGBFromSensorRGB;
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_CORE_FILM_H
