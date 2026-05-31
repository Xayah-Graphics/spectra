#ifndef SPECTRA_PATHTRACER_BASE_FILM_H
#define SPECTRA_PATHTRACER_BASE_FILM_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/base/filter.h>
#include <spectra/pathtracer/util/pstd.h>
#include <spectra/pathtracer/util/taggedptr.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <string>

namespace spectra
{
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
    class Film : public TaggedPointer<RGBFilm, GBufferFilm, SpectralFilm>
    {
    public:
        // Film Interface
        SPECTRA_CPU_GPU void AddSample(Point2i pFilm, SampledSpectrum L,
                                       const SampledWavelengths& lambda,
                                       const VisibleSurface* visibleSurface,
                                       Float weight);

        SPECTRA_CPU_GPU Bounds2f SampleBounds() const;

        SPECTRA_CPU_GPU
        bool UsesVisibleSurface() const;

        SPECTRA_CPU_GPU
        void AddSplat(Point2f p, SampledSpectrum v, const SampledWavelengths& lambda);

        SPECTRA_CPU_GPU SampledWavelengths SampleWavelengths(Float u) const;

        SPECTRA_CPU_GPU Point2i FullResolution() const;
        SPECTRA_CPU_GPU Bounds2i PixelBounds() const;
        SPECTRA_CPU_GPU Float Diagonal() const;

        void WriteImage(ImageMetadata metadata, Float splatScale = 1);

        SPECTRA_CPU_GPU RGB ToOutputRGB(SampledSpectrum L,
                                        const SampledWavelengths& lambda) const;

        Image GetImage(ImageMetadata* metadata, Float splatScale = 1);

        SPECTRA_CPU_GPU
        RGB GetPixelRGB(Point2i p, Float splatScale = 1) const;

        SPECTRA_CPU_GPU Filter GetFilter() const;
        SPECTRA_CPU_GPU const PixelSensor* GetPixelSensor() const;
        std::string GetFilename() const;

        using TaggedPointer::TaggedPointer;

        static Film Create(const std::string& name, const ParameterDictionary& parameters,
                           Float exposureTime, const CameraTransform& cameraTransform,
                           Filter filter, const FileLoc* loc, Allocator alloc);


        SPECTRA_CPU_GPU void ResetPixel(Point2i p);
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_BASE_FILM_H
