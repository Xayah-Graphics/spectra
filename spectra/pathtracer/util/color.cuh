#ifndef SPECTRA_PATHTRACER_UTIL_COLOR_H
#define SPECTRA_PATHTRACER_UTIL_COLOR_H

#include <cmath>
#include <map>
#include <memory>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/math.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <spectra/pathtracer/util/vecmath.cuh>
#include <string>

// A special present from windgi.h on Windows...
#ifdef RGB
#undef RGB
#endif // RGB

namespace spectra {
    // RGB Definition
    class RGB {
    public:
        // RGB Public Methods
        RGB() = default;
        __host__ __device__ RGB(Float r, Float g, Float b) : r(r), g(g), b(b) {}

        __host__ __device__ RGB& operator+=(RGB s) {
            r += s.r;
            g += s.g;
            b += s.b;
            return *this;
        }

        __host__ __device__ RGB operator+(RGB s) const {
            RGB ret = *this;
            return ret += s;
        }

        __host__ __device__ RGB& operator-=(RGB s) {
            r -= s.r;
            g -= s.g;
            b -= s.b;
            return *this;
        }

        __host__ __device__ RGB operator-(RGB s) const {
            RGB ret = *this;
            return ret -= s;
        }

        __host__ __device__ friend RGB operator-(Float a, RGB s) {
            return {a - s.r, a - s.g, a - s.b};
        }

        __host__ __device__ RGB& operator*=(RGB s) {
            r *= s.r;
            g *= s.g;
            b *= s.b;
            return *this;
        }

        __host__ __device__ RGB operator*(RGB s) const {
            RGB ret = *this;
            return ret *= s;
        }

        __host__ __device__ RGB operator*(Float a) const {
            DCHECK(!IsNaN(a));
            return {a * r, a * g, a * b};
        }

        __host__ __device__ RGB& operator*=(Float a) {
            DCHECK(!IsNaN(a));
            r *= a;
            g *= a;
            b *= a;
            return *this;
        }

        __host__ __device__ friend RGB operator*(Float a, RGB s) {
            return s * a;
        }

        __host__ __device__ RGB& operator/=(RGB s) {
            r /= s.r;
            g /= s.g;
            b /= s.b;
            return *this;
        }

        __host__ __device__ RGB operator/(RGB s) const {
            RGB ret = *this;
            return ret /= s;
        }

        __host__ __device__ RGB& operator/=(Float a) {
            DCHECK(!IsNaN(a));
            DCHECK_NE(a, 0);
            r /= a;
            g /= a;
            b /= a;
            return *this;
        }

        __host__ __device__ RGB operator/(Float a) const {
            RGB ret = *this;
            return ret /= a;
        }

        __host__ __device__ RGB operator-() const {
            return {-r, -g, -b};
        }

        __host__ __device__ Float Average() const {
            return (r + g + b) / 3;
        }

        __host__ __device__ bool operator==(RGB s) const {
            return r == s.r && g == s.g && b == s.b;
        }

        __host__ __device__ bool operator!=(RGB s) const {
            return r != s.r || g != s.g || b != s.b;
        }

        __host__ __device__ Float operator[](int c) const {
            DCHECK(c >= 0 && c < 3);
            if (c == 0)
                return r;
            else if (c == 1)
                return g;
            return b;
        }

        __host__ __device__ Float& operator[](int c) {
            DCHECK(c >= 0 && c < 3);
            if (c == 0)
                return r;
            else if (c == 1)
                return g;
            return b;
        }


        // RGB Public Members
        Float r = 0, g = 0, b = 0;
    };

    __host__ __device__ inline RGB max(RGB a, RGB b) {
        return RGB(std::max(a.r, b.r), std::max(a.g, b.g), std::max(a.b, b.b));
    }

    __host__ __device__ inline Float MaxComponentValue(RGB rgb) {
        return std::max(rgb.r, std::max(rgb.g, rgb.b));
    }

    __host__ __device__ inline Float MinComponentValue(RGB rgb) {
        return std::min(rgb.r, std::min(rgb.g, rgb.b));
    }

    __host__ __device__ inline RGB Lerp(Float t, RGB s1, RGB s2) {
        return (1 - t) * s1 + t * s2;
    }

    // RGB Inline Functions
    template <typename U, typename V>
    __host__ __device__ RGB Clamp(RGB rgb, U min, V max) {
        return RGB(spectra::Clamp(rgb.r, min, max), spectra::Clamp(rgb.g, min, max), spectra::Clamp(rgb.b, min, max));
    }

    __host__ __device__ inline RGB ClampZero(RGB rgb) {
        return RGB(std::max<Float>(0, rgb.r), std::max<Float>(0, rgb.g), std::max<Float>(0, rgb.b));
    }

    // XYZ Definition
    class XYZ {
    public:
        // XYZ Public Methods
        XYZ() = default;
        __host__ __device__ XYZ(Float X, Float Y, Float Z) : X(X), Y(Y), Z(Z) {}

        __host__ __device__ Float Average() const {
            return (X + Y + Z) / 3;
        }

        __host__ __device__ Point2f xy() const {
            return Point2f(X / (X + Y + Z), Y / (X + Y + Z));
        }

        __host__ __device__ static XYZ FromxyY(Point2f xy, Float Y = 1) {
            if (xy.y == 0) return XYZ(0, 0, 0);
            return XYZ(xy.x * Y / xy.y, Y, (1 - xy.x - xy.y) * Y / xy.y);
        }

        __host__ __device__ XYZ& operator+=(const XYZ& s) {
            X += s.X;
            Y += s.Y;
            Z += s.Z;
            return *this;
        }

        __host__ __device__ XYZ operator+(const XYZ& s) const {
            XYZ ret = *this;
            return ret += s;
        }

        __host__ __device__ XYZ& operator-=(const XYZ& s) {
            X -= s.X;
            Y -= s.Y;
            Z -= s.Z;
            return *this;
        }

        __host__ __device__ XYZ operator-(const XYZ& s) const {
            XYZ ret = *this;
            return ret -= s;
        }

        __host__ __device__ friend XYZ operator-(Float a, const XYZ& s) {
            return {a - s.X, a - s.Y, a - s.Z};
        }

        __host__ __device__ XYZ& operator*=(const XYZ& s) {
            X *= s.X;
            Y *= s.Y;
            Z *= s.Z;
            return *this;
        }

        __host__ __device__ XYZ operator*(const XYZ& s) const {
            XYZ ret = *this;
            return ret *= s;
        }

        __host__ __device__ XYZ operator*(Float a) const {
            DCHECK(!IsNaN(a));
            return {a * X, a * Y, a * Z};
        }

        __host__ __device__ XYZ& operator*=(Float a) {
            DCHECK(!IsNaN(a));
            X *= a;
            Y *= a;
            Z *= a;
            return *this;
        }

        __host__ __device__ XYZ& operator/=(const XYZ& s) {
            X /= s.X;
            Y /= s.Y;
            Z /= s.Z;
            return *this;
        }

        __host__ __device__ XYZ operator/(const XYZ& s) const {
            XYZ ret = *this;
            return ret /= s;
        }

        __host__ __device__ XYZ& operator/=(Float a) {
            DCHECK(!IsNaN(a));
            DCHECK_NE(a, 0);
            X /= a;
            Y /= a;
            Z /= a;
            return *this;
        }

        __host__ __device__ XYZ operator/(Float a) const {
            XYZ ret = *this;
            return ret /= a;
        }

        __host__ __device__ XYZ operator-() const {
            return {-X, -Y, -Z};
        }

        __host__ __device__ bool operator==(const XYZ& s) const {
            return X == s.X && Y == s.Y && Z == s.Z;
        }

        __host__ __device__ bool operator!=(const XYZ& s) const {
            return X != s.X || Y != s.Y || Z != s.Z;
        }

        __host__ __device__ Float operator[](int c) const {
            DCHECK(c >= 0 && c < 3);
            if (c == 0)
                return X;
            else if (c == 1)
                return Y;
            return Z;
        }

        __host__ __device__ Float& operator[](int c) {
            DCHECK(c >= 0 && c < 3);
            if (c == 0)
                return X;
            else if (c == 1)
                return Y;
            return Z;
        }


        // XYZ Public Members
        Float X = 0, Y = 0, Z = 0;
    };

    __host__ __device__ inline XYZ operator*(Float a, const XYZ& s) {
        return s * a;
    }

    template <typename U, typename V>
    __host__ __device__ XYZ Clamp(const XYZ& xyz, U min, V max) {
        return XYZ(spectra::Clamp(xyz.X, min, max), spectra::Clamp(xyz.Y, min, max), spectra::Clamp(xyz.Z, min, max));
    }

    __host__ __device__ inline XYZ ClampZero(const XYZ& xyz) {
        return XYZ(std::max<Float>(0, xyz.X), std::max<Float>(0, xyz.Y), std::max<Float>(0, xyz.Z));
    }

    __host__ __device__ inline XYZ Lerp(Float t, const XYZ& s1, const XYZ& s2) {
        return (1 - t) * s1 + t * s2;
    }

    // RGBSigmoidPolynomial Definition
    class RGBSigmoidPolynomial {
    public:
        // RGBSigmoidPolynomial Public Methods
        RGBSigmoidPolynomial() = default;
        __host__ __device__ RGBSigmoidPolynomial(Float c0, Float c1, Float c2) : c0(c0), c1(c1), c2(c2) {}


        __host__ __device__ Float operator()(Float lambda) const {
            return s(EvaluatePolynomial(lambda, c2, c1, c0));
        }

        __host__ __device__ Float MaxValue() const {
            Float result = std::max((*this)(360), (*this)(830));
            Float lambda = -c1 / (2 * c0);
            if (lambda >= 360 && lambda <= 830) result = std::max(result, (*this)(lambda));
            return result;
        }

    private:
        // RGBSigmoidPolynomial Private Methods
        __host__ __device__ static Float s(Float x) {
            if (IsInf(x)) return x > 0 ? 1 : 0;
            return .5f + x / (2 * std::sqrt(1 + Sqr(x)));
        }

        // RGBSigmoidPolynomial Private Members
        Float c0, c1, c2;
    };

    // RGBToSpectrumTable Definition
    class RGBToSpectrumTable {
    public:
        // RGBToSpectrumTable Public Constants
        static constexpr int res = 64;

        using CoefficientArray = float[3][res][res][res][3];

        // RGBToSpectrumTable Public Methods
        RGBToSpectrumTable(const float* zNodes, const CoefficientArray* coeffs) : zNodes(zNodes), coeffs(coeffs) {}

        __host__ __device__ RGBSigmoidPolynomial operator()(RGB rgb) const;

        static void Init(Allocator alloc);

        static const RGBToSpectrumTable* sRGB;
        static const RGBToSpectrumTable* DCI_P3;
        static const RGBToSpectrumTable* Rec2020;
        static const RGBToSpectrumTable* ACES2065_1;

    private:
        // RGBToSpectrumTable Private Members
        const float* zNodes;
        const CoefficientArray* coeffs;
    };

    // ColorEncoding Definitions
    class LinearColorEncoding;
    class sRGBColorEncoding;
    class GammaColorEncoding;

    class ColorEncoding : public TaggedPointer<LinearColorEncoding, sRGBColorEncoding, GammaColorEncoding> {
    public:
        using TaggedPointer::TaggedPointer;
        // ColorEncoding Interface
        __host__ __device__ void ToLinear(pstd::span<const uint8_t> vin, pstd::span<Float> vout) const;
        __host__ __device__ void FromLinear(pstd::span<const Float> vin, pstd::span<uint8_t> vout) const;

        __host__ __device__ Float ToFloatLinear(Float v) const;


        static const ColorEncoding Get(const std::string& name, Allocator alloc);

        static ColorEncoding Linear;
        static ColorEncoding sRGB;

        static void Init(Allocator alloc);
    };

    class LinearColorEncoding {
    public:
        __host__ __device__ void ToLinear(pstd::span<const uint8_t> vin, pstd::span<Float> vout) const {
            DCHECK_EQ(vin.size(), vout.size());
            for (size_t i = 0; i < vin.size(); ++i) vout[i] = vin[i] / 255.f;
        }

        __host__ __device__ Float ToFloatLinear(Float v) const {
            return v;
        }

        __host__ __device__ void FromLinear(pstd::span<const Float> vin, pstd::span<uint8_t> vout) const {
            DCHECK_EQ(vin.size(), vout.size());
            for (size_t i = 0; i < vin.size(); ++i) vout[i] = uint8_t(Clamp(vin[i] * 255.f + 0.5f, 0, 255));
        }
    };

    class sRGBColorEncoding {
    public:
        // sRGBColorEncoding Public Methods
        __host__ __device__ void ToLinear(pstd::span<const uint8_t> vin, pstd::span<Float> vout) const;
        __host__ __device__ Float ToFloatLinear(Float v) const;
        __host__ __device__ void FromLinear(pstd::span<const Float> vin, pstd::span<uint8_t> vout) const;
    };

    class GammaColorEncoding {
    public:
        __host__ __device__ GammaColorEncoding(Float gamma);

        __host__ __device__ void ToLinear(pstd::span<const uint8_t> vin, pstd::span<Float> vout) const;
        __host__ __device__ Float ToFloatLinear(Float v) const;
        __host__ __device__ void FromLinear(pstd::span<const Float> vin, pstd::span<uint8_t> vout) const;

    private:
        Float gamma;
        pstd::array<Float, 256> applyLUT;
        pstd::array<Float, 1024> inverseLUT;
    };

    __host__ __device__ inline Float LinearToSRGB(Float value) {
        if (value <= 0.0031308f) return 12.92f * value;
        // Minimax polynomial approximation from enoki's color.h.
        float sqrtValue = SafeSqrt(value);
        float p         = EvaluatePolynomial(sqrtValue, -0.0016829072605308378f, 0.03453868659826638f, 0.7642611304733891f, 2.0041169284241644f, 0.7551545191665577f, -0.016202083165206348f);
        float q         = EvaluatePolynomial(sqrtValue, 4.178892964897981e-7f, -0.00004375359692957097f, 0.03467195408529984f, 0.6085338522168684f, 1.8970238036421054f, 1.f);
        return p / q * value;
    }

    __host__ __device__ inline uint8_t LinearToSRGB8(Float value, Float dither = 0) {
        if (value <= 0) return 0;
        if (value >= 1) return 255;
        return Clamp(pstd::round(255.f * LinearToSRGB(value) + dither), 0, 255);
    }

    __host__ __device__ inline Float SRGBToLinear(float value) {
        if (value <= 0.04045f) return value * (1 / 12.92f);
        // Minimax polynomial approximation from enoki's color.h.
        float p = EvaluatePolynomial(value, -0.0163933279112946f, -0.7386328024653209f, -11.199318357635072f, -47.46726633009393f, -36.04572663838034f);
        float q = EvaluatePolynomial(value, -0.004261480793199332f, -19.140923959601675f, -59.096406619244426f, -18.225745396846637f, 1.f);
        return p / q * value;
    }

#if defined(__CUDA_ARCH__)
    extern __device__ const Float SRGBToLinearLUT[256];
#else
    extern const Float SRGBToLinearLUT[256];
#endif

    __host__ __device__ inline Float SRGB8ToLinear(uint8_t value) {
        return SRGBToLinearLUT[value];
    }

    // White Balance Definitions
    // clang-format off
// These are the Bradford transformation matrices.
const SquareMatrix<3> LMSFromXYZ( 0.8951,  0.2664, -0.1614,
                                 -0.7502,  1.7135,  0.0367,
                                  0.0389, -0.0685,  1.0296);
const SquareMatrix<3> XYZFromLMS( 0.986993,   -0.147054,  0.159963,
                                  0.432305,    0.51836,   0.0492912,
                                 -0.00852866,  0.0400428, 0.968487);
    // clang-format on

    inline SquareMatrix<3> WhiteBalance(Point2f srcWhite, Point2f targetWhite) {
        // Find LMS coefficients for source and target white
        XYZ srcXYZ = XYZ::FromxyY(srcWhite), dstXYZ = XYZ::FromxyY(targetWhite);
        auto srcLMS = LMSFromXYZ * srcXYZ, dstLMS = LMSFromXYZ * dstXYZ;

        // Return white balancing matrix for source and target white
        SquareMatrix<3> LMScorrect = SquareMatrix<3>::Diag(dstLMS[0] / srcLMS[0], dstLMS[1] / srcLMS[1], dstLMS[2] / srcLMS[2]);
        return XYZFromLMS * LMScorrect * LMSFromXYZ;
    }
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_COLOR_H
