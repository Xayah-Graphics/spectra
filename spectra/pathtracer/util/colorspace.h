#ifndef SPECTRA_PATHTRACER_UTIL_COLORSPACE_H
#define SPECTRA_PATHTRACER_UTIL_COLORSPACE_H

#include <spectra/pathtracer/util/float.h>
#include <spectra/pathtracer/util/memory.h>

#include <spectra/pathtracer/util/color.h>
#include <spectra/pathtracer/util/math.h>
#include <spectra/pathtracer/util/spectrum.h>
#include <spectra/pathtracer/util/vecmath.h>

#include <string>

namespace spectra
{
    // RGBColorSpace Definition
    class RGBColorSpace
    {
    public:
        // RGBColorSpace Public Methods
        RGBColorSpace(Point2f r, Point2f g, Point2f b, Spectrum illuminant,
                      const RGBToSpectrumTable* rgbToSpectrumTable, Allocator alloc);

        SPECTRA_CPU_GPU
        RGBSigmoidPolynomial ToRGBCoeffs(RGB rgb) const;

        static void Init(Allocator alloc);

        // RGBColorSpace Public Members
        Point2f r, g, b, w;
        DenselySampledSpectrum illuminant;
        SquareMatrix<3> XYZFromRGB, RGBFromXYZ;
        static const RGBColorSpace *sRGB, *DCI_P3, *Rec2020, *ACES2065_1;

        SPECTRA_CPU_GPU
        bool operator==(const RGBColorSpace& cs) const
        {
            return (r == cs.r && g == cs.g && b == cs.b && w == cs.w &&
                rgbToSpectrumTable == cs.rgbToSpectrumTable);
        }

        SPECTRA_CPU_GPU
        bool operator!=(const RGBColorSpace& cs) const
        {
            return (r != cs.r || g != cs.g || b != cs.b || w != cs.w ||
                rgbToSpectrumTable != cs.rgbToSpectrumTable);
        }


        SPECTRA_CPU_GPU
        RGB LuminanceVector() const
        {
            return RGB(XYZFromRGB[1][0], XYZFromRGB[1][1], XYZFromRGB[1][2]);
        }

        SPECTRA_CPU_GPU
        RGB ToRGB(XYZ xyz) const { return Mul<RGB>(RGBFromXYZ, xyz); }

        SPECTRA_CPU_GPU
        XYZ ToXYZ(RGB rgb) const { return Mul<XYZ>(XYZFromRGB, rgb); }

        static const RGBColorSpace* GetNamed(std::string name);
        static const RGBColorSpace* Lookup(Point2f r, Point2f g, Point2f b, Point2f w);

    private:
        // RGBColorSpace Private Members
        const RGBToSpectrumTable* rgbToSpectrumTable;
    };

    extern SPECTRA_CONST RGBColorSpace* RGBColorSpace_sRGB;
    extern SPECTRA_CONST RGBColorSpace* RGBColorSpace_DCI_P3;
    extern SPECTRA_CONST RGBColorSpace* RGBColorSpace_Rec2020;
    extern SPECTRA_CONST RGBColorSpace* RGBColorSpace_ACES2065_1;

    SquareMatrix<3> ConvertRGBColorSpace(const RGBColorSpace& from, const RGBColorSpace& to);
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_COLORSPACE_H
