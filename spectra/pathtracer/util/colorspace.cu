#include <spectra/pathtracer/gpu/util.cuh>
#include <spectra/pathtracer/util/colorspace.cuh>

namespace spectra {
    namespace {
        __device__ const RGBColorSpace* rgbColorSpaceSRGBDevice;
        __device__ const RGBColorSpace* rgbColorSpaceDCIP3Device;
        __device__ const RGBColorSpace* rgbColorSpaceRec2020Device;
        __device__ const RGBColorSpace* rgbColorSpaceACES2065_1Device;
    } // namespace

    // RGBColorSpace Method Definitions
    RGBColorSpace::RGBColorSpace(Point2f r, Point2f g, Point2f b, Spectrum illuminant, const RGBToSpectrumTable* rgbToSpec, Allocator alloc) : r(r), g(g), b(b), illuminant(illuminant, alloc), rgbToSpectrumTable(rgbToSpec) {
        // Compute whitepoint primaries and XYZ coordinates
        XYZ W = SpectrumToXYZ(illuminant);
        w     = W.xy();
        XYZ R = XYZ::FromxyY(r), G = XYZ::FromxyY(g), B = XYZ::FromxyY(b);

        // Initialize XYZ color space conversion matrices
        SquareMatrix<3> rgb(R.X, G.X, B.X, R.Y, G.Y, B.Y, R.Z, G.Z, B.Z);
        XYZ C      = InvertOrExit(rgb) * W;
        XYZFromRGB = rgb * SquareMatrix<3>::Diag(C[0], C[1], C[2]);
        RGBFromXYZ = InvertOrExit(XYZFromRGB);
    }

    SquareMatrix<3> ConvertRGBColorSpace(const RGBColorSpace& from, const RGBColorSpace& to) {
        if (from == to) return {};
        return to.RGBFromXYZ * from.XYZFromRGB;
    }

    RGBSigmoidPolynomial RGBColorSpace::ToRGBCoeffs(RGB rgb) const {
        DCHECK(rgb.r >= 0 && rgb.g >= 0 && rgb.b >= 0);
        return (*rgbToSpectrumTable)(ClampZero(rgb));
    }

    const RGBColorSpace* RGBColorSpace::GetNamed(std::string n) {
        std::string name;
        std::transform(n.begin(), n.end(), std::back_inserter(name), tolower);
        if (name == "aces2065-1")
            return ACES2065_1();
        else if (name == "rec2020")
            return Rec2020();
        else if (name == "dci-p3")
            return DCI_P3();
        else if (name == "srgb")
            return SRGB();
        else
            return nullptr;
    }

    const RGBColorSpace* RGBColorSpace::Lookup(Point2f r, Point2f g, Point2f b, Point2f w) {
        auto closeEnough = [](const Point2f& a, const Point2f& b) { return ((a.x == b.x || std::abs((a.x - b.x) / b.x) < 1e-3) && (a.y == b.y || std::abs((a.y - b.y) / b.y) < 1e-3)); };
        for (const RGBColorSpace* cs : {ACES2065_1(), DCI_P3(), Rec2020(), SRGB()}) {
            if (closeEnough(r, cs->r) && closeEnough(g, cs->g) && closeEnough(b, cs->b) && closeEnough(w, cs->w)) return cs;
        }
        return nullptr;
    }

    const RGBColorSpace* RGBColorSpace::srgb;
    const RGBColorSpace* RGBColorSpace::dciP3;
    const RGBColorSpace* RGBColorSpace::rec2020;
    const RGBColorSpace* RGBColorSpace::aces2065_1;

    __host__ __device__ const RGBColorSpace* RGBColorSpace::SRGB() {
#if defined(__CUDA_ARCH__)
        return rgbColorSpaceSRGBDevice;
#else
        return srgb;
#endif
    }

    __host__ __device__ const RGBColorSpace* RGBColorSpace::DCI_P3() {
#if defined(__CUDA_ARCH__)
        return rgbColorSpaceDCIP3Device;
#else
        return dciP3;
#endif
    }

    __host__ __device__ const RGBColorSpace* RGBColorSpace::Rec2020() {
#if defined(__CUDA_ARCH__)
        return rgbColorSpaceRec2020Device;
#else
        return rec2020;
#endif
    }

    __host__ __device__ const RGBColorSpace* RGBColorSpace::ACES2065_1() {
#if defined(__CUDA_ARCH__)
        return rgbColorSpaceACES2065_1Device;
#else
        return aces2065_1;
#endif
    }

    void RGBColorSpace::Init(Allocator alloc) {
        // Rec. ITU-R BT.709.3
        srgb = alloc.new_object<RGBColorSpace>(Point2f(.64, .33), Point2f(.3, .6), Point2f(.15, .06), GetNamedSpectrum("stdillum-D65"), RGBToSpectrumTable::sRGB, alloc);
        // P3-D65 (display)
        dciP3 = alloc.new_object<RGBColorSpace>(Point2f(.68, .32), Point2f(.265, .690), Point2f(.15, .06), GetNamedSpectrum("stdillum-D65"), RGBToSpectrumTable::DCI_P3, alloc);
        // ITU-R Rec BT.2020
        rec2020    = alloc.new_object<RGBColorSpace>(Point2f(.708, .292), Point2f(.170, .797), Point2f(.131, .046), GetNamedSpectrum("stdillum-D65"), RGBToSpectrumTable::Rec2020, alloc);
        aces2065_1 = alloc.new_object<RGBColorSpace>(Point2f(.7347, .2653), Point2f(0., 1.), Point2f(.0001, -.077), GetNamedSpectrum("illum-acesD60"), RGBToSpectrumTable::ACES2065_1, alloc);
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceSRGBDevice, &srgb, sizeof(srgb)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceDCIP3Device, &dciP3, sizeof(dciP3)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceRec2020Device, &rec2020, sizeof(rec2020)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceACES2065_1Device, &aces2065_1, sizeof(aces2065_1)));
    }

    void RGBColorSpace::Reset() {
        srgb                                = nullptr;
        dciP3                               = nullptr;
        rec2020                             = nullptr;
        aces2065_1                          = nullptr;
        const RGBColorSpace* nullColorSpace = nullptr;
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceSRGBDevice, &nullColorSpace, sizeof(nullColorSpace)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceDCIP3Device, &nullColorSpace, sizeof(nullColorSpace)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceRec2020Device, &nullColorSpace, sizeof(nullColorSpace)));
        CUDA_CHECK(cudaMemcpyToSymbol(rgbColorSpaceACES2065_1Device, &nullColorSpace, sizeof(nullColorSpace)));
    }
} // namespace spectra
