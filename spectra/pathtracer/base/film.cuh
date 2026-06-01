#ifndef SPECTRA_PATHTRACER_BASE_FILM_H
#define SPECTRA_PATHTRACER_BASE_FILM_H

#include <spectra/pathtracer/base/filter.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    class CameraTransform;
    class Image;
    class ParameterDictionary;
    class RGB;
    class SampledSpectrum;
    class SampledWavelengths;
    struct FileLoc;
    struct ImageMetadata;

    class VisibleSurface;
    class RGBFilm;
    class GBufferFilm;
    class SpectralFilm;
    class PixelSensor;

    // Film Definition
    class Film : public TaggedPointer<RGBFilm, GBufferFilm, SpectralFilm> {
    public:
        // Film Interface
        __host__ __device__ void AddSample(Point2i pFilm, SampledSpectrum L, const SampledWavelengths& lambda, const VisibleSurface* visibleSurface, Float weight);

        __host__ __device__ Bounds2f SampleBounds() const;

        __host__ __device__ bool UsesVisibleSurface() const;

        __host__ __device__ void AddSplat(Point2f p, SampledSpectrum v, const SampledWavelengths& lambda);

        __host__ __device__ SampledWavelengths SampleWavelengths(Float u) const;

        __host__ __device__ Point2i FullResolution() const;
        __host__ __device__ Bounds2i PixelBounds() const;
        __host__ __device__ Float Diagonal() const;

        void WriteImage(ImageMetadata metadata, Float splatScale = 1);

        __host__ __device__ RGB ToOutputRGB(SampledSpectrum L, const SampledWavelengths& lambda) const;

        Image GetImage(ImageMetadata* metadata, Float splatScale = 1);

        __host__ __device__ RGB GetPixelRGB(Point2i p, Float splatScale = 1) const;

        __host__ __device__ Filter GetFilter() const;
        __host__ __device__ const PixelSensor* GetPixelSensor() const;
        std::string GetFilename() const;

        using TaggedPointer::TaggedPointer;

        static Film Create(const std::string& name, const ParameterDictionary& parameters, Float exposureTime, const CameraTransform& cameraTransform, Filter filter, const FileLoc* loc, Allocator alloc);


        __host__ __device__ void ResetPixel(Point2i p);
    };
} // namespace spectra

#endif // SPECTRA_PATHTRACER_BASE_FILM_H
