#ifndef SPECTRA_PATHTRACER_UTIL_COLORSPACE_H
#define SPECTRA_PATHTRACER_UTIL_COLORSPACE_H

#include <pathtracer/util/color.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/math.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/spectrum.cuh>
#include <pathtracer/util/vecmath.cuh>
#include <string>

namespace spectra {
    // RGBColorSpace Definition
    class RGBColorSpace {
    public:
        // RGBColorSpace Public Methods
        RGBColorSpace(Point2f r, Point2f g, Point2f b, Spectrum illuminant, const RGBToSpectrumTable* rgbToSpectrumTable, Allocator alloc);

        __host__ __device__ RGBSigmoidPolynomial ToRGBCoeffs(RGB rgb) const;

        static void Init(Allocator alloc);
        static void Reset();

        // RGBColorSpace Public Members
        Point2f r, g, b, w;
        DenselySampledSpectrum illuminant;
        SquareMatrix<3> XYZFromRGB, RGBFromXYZ;
        __host__ __device__ static const RGBColorSpace* SRGB();
        __host__ __device__ static const RGBColorSpace* DCI_P3();
        __host__ __device__ static const RGBColorSpace* Rec2020();
        __host__ __device__ static const RGBColorSpace* ACES2065_1();

        __host__ __device__ bool operator==(const RGBColorSpace& cs) const {
            return (r == cs.r && g == cs.g && b == cs.b && w == cs.w && rgbToSpectrumTable == cs.rgbToSpectrumTable);
        }

        __host__ __device__ bool operator!=(const RGBColorSpace& cs) const {
            return (r != cs.r || g != cs.g || b != cs.b || w != cs.w || rgbToSpectrumTable != cs.rgbToSpectrumTable);
        }


        __host__ __device__ RGB LuminanceVector() const {
            return RGB(XYZFromRGB[1][0], XYZFromRGB[1][1], XYZFromRGB[1][2]);
        }

        __host__ __device__ RGB ToRGB(XYZ xyz) const {
            return Mul<RGB>(RGBFromXYZ, xyz);
        }

        __host__ __device__ XYZ ToXYZ(RGB rgb) const {
            return Mul<XYZ>(XYZFromRGB, rgb);
        }

        static const RGBColorSpace* GetNamed(std::string name);
        static const RGBColorSpace* Lookup(Point2f r, Point2f g, Point2f b, Point2f w);

    private:
        // RGBColorSpace Private Members
        const RGBToSpectrumTable* rgbToSpectrumTable;
        static const RGBColorSpace *srgb, *dciP3, *rec2020, *aces2065_1;
    };

    SquareMatrix<3> ConvertRGBColorSpace(const RGBColorSpace& from, const RGBColorSpace& to);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_COLORSPACE_H
