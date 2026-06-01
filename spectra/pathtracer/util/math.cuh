#ifndef SPECTRA_PATHTRACER_UTIL_MATH_H
#define SPECTRA_PATHTRACER_UTIL_MATH_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <string>
#include <type_traits>

#ifdef SPECTRA_HAS_INTRIN_H
#include <intrin.h>
#endif // SPECTRA_HAS_INTRIN_H

namespace spectra {
    template <typename T>
    struct SOA;

#if defined(__CUDA_ARCH__)

#define ShadowEpsilon 0.0001f
#define Pi            Float(3.14159265358979323846)
#define InvPi         Float(0.31830988618379067154)
#define Inv2Pi        Float(0.15915494309189533577)
#define Inv4Pi        Float(0.07957747154594766788)
#define PiOver2       Float(1.57079632679489661923)
#define PiOver4       Float(0.78539816339744830961)
#define Sqrt2         Float(1.41421356237309504880)

#else

    // Mathematical Constants
    constexpr Float ShadowEpsilon = 0.0001f;

    constexpr Float Pi      = 3.14159265358979323846;
    constexpr Float InvPi   = 0.31830988618379067154;
    constexpr Float Inv2Pi  = 0.15915494309189533577;
    constexpr Float Inv4Pi  = 0.07957747154594766788;
    constexpr Float PiOver2 = 1.57079632679489661923;
    constexpr Float PiOver4 = 0.78539816339744830961;
    constexpr Float Sqrt2   = 1.41421356237309504880;

#endif

    // Bit Operation Inline Functions
    __host__ __device__ inline uint32_t ReverseBits32(uint32_t n) {
#if defined(__CUDA_ARCH__)
        return __brev(n);
#else
        n = (n << 16) | (n >> 16);
        n = ((n & 0x00ff00ff) << 8) | ((n & 0xff00ff00) >> 8);
        n = ((n & 0x0f0f0f0f) << 4) | ((n & 0xf0f0f0f0) >> 4);
        n = ((n & 0x33333333) << 2) | ((n & 0xcccccccc) >> 2);
        n = ((n & 0x55555555) << 1) | ((n & 0xaaaaaaaa) >> 1);
        return n;
#endif
    }

    __host__ __device__ inline uint64_t ReverseBits64(uint64_t n) {
#if defined(__CUDA_ARCH__)
        return __brevll(n);
#else
        uint64_t n0 = ReverseBits32((uint32_t) n);
        uint64_t n1 = ReverseBits32((uint32_t) (n >> 32));
        return (n0 << 32) | n1;
#endif
    }

    // https://fgiesen.wordpress.com/2009/12/13/decoding-morton-codes/
    // updated to 64 bits.
    __host__ __device__ inline uint64_t LeftShift2(uint64_t x) {
        x &= 0xffffffff;
        x = (x ^ (x << 16)) & 0x0000ffff0000ffff;
        x = (x ^ (x << 8)) & 0x00ff00ff00ff00ff;
        x = (x ^ (x << 4)) & 0x0f0f0f0f0f0f0f0f;
        x = (x ^ (x << 2)) & 0x3333333333333333;
        x = (x ^ (x << 1)) & 0x5555555555555555;
        return x;
    }

    __host__ __device__ inline uint64_t EncodeMorton2(uint32_t x, uint32_t y) {
        return (LeftShift2(y) << 1) | LeftShift2(x);
    }

    __host__ __device__ inline uint32_t LeftShift3(uint32_t x) {
        DCHECK_LE(x, (1u << 10));
        if (x == (1 << 10)) --x;
        x = (x | (x << 16)) & 0b00000011000000000000000011111111;
        // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x | (x << 8)) & 0b00000011000000001111000000001111;
        // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x | (x << 4)) & 0b00000011000011000011000011000011;
        // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x | (x << 2)) & 0b00001001001001001001001001001001;
        // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        return x;
    }

    __host__ __device__ inline uint32_t EncodeMorton3(float x, float y, float z) {
        DCHECK_GE(x, 0);
        DCHECK_GE(y, 0);
        DCHECK_GE(z, 0);
        return (LeftShift3(z) << 2) | (LeftShift3(y) << 1) | LeftShift3(x);
    }

    __host__ __device__ inline uint32_t Compact1By1(uint64_t x) {
        // TODO: as of Haswell, the PEXT instruction could do all this in a
        // single instruction.
        // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
        x &= 0x5555555555555555;
        // x = --fe --dc --ba --98 --76 --54 --32 --10
        x = (x ^ (x >> 1)) & 0x3333333333333333;
        // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
        x = (x ^ (x >> 2)) & 0x0f0f0f0f0f0f0f0f;
        // x = ---- ---- fedc ba98 ---- ---- 7654 3210
        x = (x ^ (x >> 4)) & 0x00ff00ff00ff00ff;
        // x = ---- ---- ---- ---- fedc ba98 7654 3210
        x = (x ^ (x >> 8)) & 0x0000ffff0000ffff;
        // ...
        x = (x ^ (x >> 16)) & 0xffffffff;
        return x;
    }

    __host__ __device__ inline void DecodeMorton2(uint64_t v, uint32_t* x, uint32_t* y) {
        *x = Compact1By1(v);
        *y = Compact1By1(v >> 1);
    }

    __host__ __device__ inline uint32_t Compact1By2(uint32_t x) {
        x &= 0x09249249; // x = ---- 9--8 --7- -6-- 5--4 --3- -2-- 1--0
        x = (x ^ (x >> 2)) & 0x030c30c3; // x = ---- --98 ---- 76-- --54 ---- 32-- --10
        x = (x ^ (x >> 4)) & 0x0300f00f; // x = ---- --98 ---- ---- 7654 ---- ---- 3210
        x = (x ^ (x >> 8)) & 0xff0000ff; // x = ---- --98 ---- ---- ---- ---- 7654 3210
        x = (x ^ (x >> 16)) & 0x000003ff; // x = ---- ---- ---- ---- ---- --98 7654 3210
        return x;
    }

    // CompensatedSum Definition
    template <typename Float>
    class CompensatedSum {
    public:
        // CompensatedSum Public Methods
        CompensatedSum() = default;
        __host__ __device__ explicit CompensatedSum(Float v) : sum(v) {}

        __host__ __device__ CompensatedSum& operator=(Float v) {
            sum = v;
            c   = 0;
            return *this;
        }

        __host__ __device__ CompensatedSum& operator+=(Float v) {
            Float delta  = v - c;
            Float newSum = sum + delta;
            c            = (newSum - sum) - delta;
            sum          = newSum;
            return *this;
        }

        __host__ __device__ explicit operator Float() const {
            return sum;
        }

    private:
        Float sum = 0, c = 0;
    };

    // CompensatedFloat Definition
    struct CompensatedFloat {
    public:
        // CompensatedFloat Public Methods
        __host__ __device__ CompensatedFloat(Float v, Float err = 0) : v(v), err(err) {}

        __host__ __device__ explicit operator float() const {
            return v + err;
        }

        __host__ __device__ explicit operator double() const {
            return double(v) + double(err);
        }


        Float v, err;
    };

    template <int N>
    class SquareMatrix;
    __host__ __device__ inline Float SinXOverX(Float x);

    // Math Inline Functions
    __host__ __device__ inline Float Lerp(Float x, Float a, Float b) {
        return (1 - x) * a + x * b;
    }

    template <typename T>
    __host__ __device__ typename std::enable_if_t<std::is_integral_v<T>, T> FMA(T a, T b, T c) {
        return a * b + c;
    }

    __host__ __device__ inline Float Sinc(Float);
    __host__ __device__ inline Float WindowedSinc(Float x, Float radius, Float tau) {
        if (std::abs(x) > radius) return 0;
        return Sinc(x) * Sinc(x / tau);
    }

    __host__ __device__ inline Float Sinc(Float x) {
        return SinXOverX(Pi * x);
    }

#ifdef SPECTRA_IS_MSVC
#pragma warning(push)
#pragma warning(disable : 4018) // signed/unsigned mismatch
#endif

    template <typename T, typename U, typename V>
    __host__ __device__ inline constexpr T Clamp(T val, U low, V high) {
        if (val < low)
            return T(low);
        else if (val > high)
            return T(high);
        else
            return val;
    }

#ifdef SPECTRA_IS_MSVC
#pragma warning(pop)
#endif

    template <typename T>
    __host__ __device__ T Mod(T a, T b) {
        T result = a - (a / b) * b;
        return (T) ((result < 0) ? result + b : result);
    }

    template <>
    __host__ __device__ inline Float Mod(Float a, Float b) {
        return std::fmod(a, b);
    }

    __host__ __device__ inline Float Radians(Float deg) {
        return (Pi / 180) * deg;
    }

    __host__ __device__ inline Float Degrees(Float rad) {
        return (180 / Pi) * rad;
    }

    __host__ __device__ inline Float SmoothStep(Float x, Float a, Float b) {
        if (a == b) return (x < a) ? 0 : 1;
        DCHECK_LT(a, b);
        Float t = Clamp((x - a) / (b - a), 0, 1);
        return t * t * (3 - 2 * t);
    }

    __host__ __device__ inline float SafeSqrt(float x) {
        DCHECK_GE(x, -1e-3f); // not too negative
        return std::sqrt(std::max(0.f, x));
    }

    __host__ __device__ inline double SafeSqrt(double x) {
        DCHECK_GE(x, -1e-3); // not too negative
        return std::sqrt(std::max(0., x));
    }

    template <typename T>
    __host__ __device__ inline constexpr T Sqr(T v) {
        return v * v;
    }

    // Would be nice to allow Float to be a template type here, but it is tricky:
    // https://stackoverflow.com/questions/5101516/why-function-template-cannot-be-partially-specialized
    template <int n>
    __host__ __device__ inline constexpr float Pow(float v) {
        if constexpr (n < 0) return 1 / Pow<-n>(v);
        float n2 = Pow<n / 2>(v);
        return n2 * n2 * Pow<n & 1>(v);
    }

    template <>
    __host__ __device__ inline constexpr float Pow<1>(float v) {
        return v;
    }

    template <>
    __host__ __device__ inline constexpr float Pow<0>(float v) {
        return 1;
    }

    template <int n>
    __host__ __device__ inline constexpr double Pow(double v) {
        if constexpr (n < 0) return 1 / Pow<-n>(v);
        double n2 = Pow<n / 2>(v);
        return n2 * n2 * Pow<n & 1>(v);
    }

    template <>
    __host__ __device__ inline constexpr double Pow<1>(double v) {
        return v;
    }

    template <>
    __host__ __device__ inline constexpr double Pow<0>(double v) {
        return 1;
    }

    template <typename Float, typename C>
    __host__ __device__ inline constexpr Float EvaluatePolynomial(Float t, C c) {
        return c;
    }

    template <typename Float, typename C, typename... Args>
    __host__ __device__ inline constexpr Float EvaluatePolynomial(Float t, C c, Args... cRemaining) {
        return FMA(t, EvaluatePolynomial(t, cRemaining...), c);
    }

    // http://www.plunk.org/~hatch/rightway.html
    __host__ __device__ inline Float SinXOverX(Float x) {
        if (1 - x * x == 1) return 1;
        return std::sin(x) / x;
    }

    __host__ __device__ inline float SafeASin(float x) {
        DCHECK(x >= -1.0001 && x <= 1.0001);
        return std::asin(Clamp(x, -1, 1));
    }

    __host__ __device__ inline float SafeACos(float x) {
        DCHECK(x >= -1.0001 && x <= 1.0001);
        return std::acos(Clamp(x, -1, 1));
    }

    __host__ __device__ inline double SafeASin(double x) {
        DCHECK(x >= -1.0001 && x <= 1.0001);
        return std::asin(Clamp(x, -1, 1));
    }

    __host__ __device__ inline double SafeACos(double x) {
        DCHECK(x >= -1.0001 && x <= 1.0001);
        return std::acos(Clamp(x, -1, 1));
    }

    __host__ __device__ inline Float Log2(Float x) {
        const Float invLog2 = 1.442695040888963387004650940071;
        return std::log(x) * invLog2;
    }

    __host__ __device__ inline int Log2Int(float v) {
        DCHECK_GT(v, 0);
        if (v < 1) return -Log2Int(1 / v);
        // https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
        // (With an additional check of the significant to get round-to-nearest
        // rather than round down.)
        // midsignif = Significand(std::pow(2., 1.5))
        // i.e. grab the significand of a value halfway between two exponents,
        // in log space.
        const uint32_t midsignif = 0b00000000001101010000010011110011;
        return Exponent(v) + ((Significand(v) >= midsignif) ? 1 : 0);
    }

    __host__ __device__ inline int Log2Int(double v) {
        DCHECK_GT(v, 0);
        if (v < 1) return -Log2Int(1 / v);
        // https://graphics.stanford.edu/~seander/bithacks.html#IntegerLog
        // (With an additional check of the significant to get round-to-nearest
        // rather than round down.)
        // midsignif = Significand(std::pow(2., 1.5))
        // i.e. grab the significand of a value halfway between two exponents,
        // in log space.
        const uint64_t midsignif = 0b110101000001001111001100110011111110011101111001101;
        return Exponent(v) + ((Significand(v) >= midsignif) ? 1 : 0);
    }

    __host__ __device__ inline int Log2Int(uint32_t v) {
#if defined(__CUDA_ARCH__)
        return 31 - __clz(v);
#elif defined(SPECTRA_HAS_INTRIN_H)
        unsigned long lz = 0;
        if (_BitScanReverse(&lz, v)) return lz;
        return 0;
#else
        return 31 - __builtin_clz(v);
#endif
    }

    __host__ __device__ inline int Log2Int(int32_t v) {
        return Log2Int((uint32_t) v);
    }

    __host__ __device__ inline int Log2Int(uint64_t v) {
#if defined(__CUDA_ARCH__)
        return 64 - __clzll(v);
#elif defined(SPECTRA_HAS_INTRIN_H)
        unsigned long lz = 0;
#if defined(_WIN64)
        _BitScanReverse64(&lz, v);
#else
        if (_BitScanReverse(&lz, v >> 32))
            lz += 32;
        else
            _BitScanReverse(&lz, v & 0xffffffff);
#endif // _WIN64
        return lz;
#else // SPECTRA_HAS_INTRIN_H
        return 63 - __builtin_clzll(v);
#endif
    }

    __host__ __device__ inline int Log2Int(int64_t v) {
        return Log2Int((uint64_t) v);
    }

    template <typename T>
    __host__ __device__ int Log4Int(T v) {
        return Log2Int(v) / 2;
    }

    // https://stackoverflow.com/a/10792321
    __host__ __device__ inline float FastExp(float x) {
#if defined(__CUDA_ARCH__)
        return __expf(x);
#else
        // Compute $x'$ such that $\roman{e}^x = 2^{x'}$
        float xp = x * 1.442695041f;

        // Find integer and fractional components of $x'$
        float fxp = pstd::floor(xp), f = xp - fxp;
        int i = (int) fxp;

        // Evaluate polynomial approximation of $2^f$
        float twoToF = EvaluatePolynomial(f, 1.f, 0.695556856f, 0.226173572f, 0.0781455737f);

        // Scale $2^f$ by $2^i$ and return final result
        int exponent = Exponent(twoToF) + i;
        if (exponent < -126) return 0;
        if (exponent > 127) return Infinity;
        uint32_t bits = FloatToBits(twoToF);
        bits &= 0b10000000011111111111111111111111u;
        bits |= (exponent + 127) << 23;
        return BitsToFloat(bits);
#endif
    }

    __host__ __device__ inline Float Gaussian(Float x, Float mu = 0, Float sigma = 1) {
        return 1 / std::sqrt(2 * Pi * sigma * sigma) * FastExp(-Sqr(x - mu) / (2 * sigma * sigma));
    }

    __host__ __device__ inline Float GaussianIntegral(Float x0, Float x1, Float mu = 0, Float sigma = 1) {
        DCHECK_GT(sigma, 0);
        Float sigmaRoot2 = sigma * Float(1.414213562373095);
        return 0.5f * (std::erf((mu - x0) / sigmaRoot2) - std::erf((mu - x1) / sigmaRoot2));
    }

    __host__ __device__ inline Float Logistic(Float x, Float s) {
        x = std::abs(x);
        return std::exp(-x / s) / (s * Sqr(1 + std::exp(-x / s)));
    }

    __host__ __device__ inline Float LogisticCDF(Float x, Float s) {
        return 1 / (1 + std::exp(-x / s));
    }

    __host__ __device__ inline Float TrimmedLogistic(Float x, Float s, Float a, Float b) {
        DCHECK_LT(a, b);
        return Logistic(x, s) / (LogisticCDF(b, s) - LogisticCDF(a, s));
    }

    __host__ __device__ inline Float ErfInv(Float a);
    __host__ __device__ inline Float I0(Float x);
    __host__ __device__ inline Float LogI0(Float x);

    template <typename Predicate>
    __host__ __device__ size_t FindInterval(size_t sz, const Predicate& pred) {
        using ssize_t = std::make_signed_t<size_t>;
        ssize_t size = (ssize_t) sz - 2, first = 1;
        while (size > 0) {
            // Evaluate predicate at midpoint and update _first_ and _size_
            size_t half = (size_t) size >> 1, middle = first + half;
            bool predResult = pred(middle);
            first           = predResult ? middle + 1 : first;
            size            = predResult ? size - (half + 1) : half;
        }
        return (size_t) Clamp((ssize_t) first - 1, 0, sz - 2);
    }

    template <typename T>
    __host__ __device__ inline constexpr bool IsPowerOf2(T v) {
        return v && !(v & (v - 1));
    }

    template <typename T>
    __host__ __device__ bool IsPowerOf4(T v) {
        return v == 1 << (2 * Log4Int(v));
    }

    __host__ __device__ inline constexpr int32_t RoundUpPow2(int32_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        return v + 1;
    }

    __host__ __device__ inline constexpr int64_t RoundUpPow2(int64_t v) {
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        return v + 1;
    }

    template <typename T>
    __host__ __device__ T RoundUpPow4(T v) {
        return IsPowerOf4(v) ? v : (1 << (2 * (1 + Log4Int(v))));
    }

    __host__ __device__ inline CompensatedFloat TwoProd(Float a, Float b) {
        Float ab = a * b;
        return {ab, FMA(a, b, -ab)};
    }

    __host__ __device__ inline CompensatedFloat TwoSum(Float a, Float b) {
        Float s = a + b, delta = s - a;
        return {s, (a - (s - delta)) + (b - delta)};
    }

    template <typename Ta, typename Tb, typename Tc, typename Td>
    __host__ __device__ auto DifferenceOfProducts(Ta a, Tb b, Tc c, Td d) {
        auto cd                   = c * d;
        auto differenceOfProducts = FMA(a, b, -cd);
        auto error                = FMA(-c, d, cd);
        return differenceOfProducts + error;
    }

    template <typename Ta, typename Tb, typename Tc, typename Td>
    __host__ __device__ auto SumOfProducts(Ta a, Tb b, Tc c, Td d) {
        auto cd            = c * d;
        auto sumOfProducts = FMA(a, b, cd);
        auto error         = FMA(c, d, -cd);
        return sumOfProducts + error;
    }

    namespace internal {
        // InnerProduct Helper Functions
        template <typename Float>
        __host__ __device__ CompensatedFloat InnerProduct(Float a, Float b) {
            return TwoProd(a, b);
        }

        // Accurate dot products with FMA: Graillat et al.,
        // https://www-pequan.lip6.fr/~graillat/papers/posterRNC7.pdf
        //
        // Accurate summation, dot product and polynomial evaluation in complex
        // floating point arithmetic, Graillat and Menissier-Morain.
        template <typename Float, typename... T>
        __host__ __device__ CompensatedFloat InnerProduct(Float a, Float b, T... terms) {
            CompensatedFloat ab  = TwoProd(a, b);
            CompensatedFloat tp  = InnerProduct(terms...);
            CompensatedFloat sum = TwoSum(ab.v, tp.v);
            return {sum.v, ab.err + (tp.err + sum.err)};
        }
    } // namespace internal

    template <typename... T>
    __host__ __device__ std::enable_if_t<std::conjunction_v<std::is_arithmetic<T>...>, Float> InnerProduct(T... terms) {
        CompensatedFloat ip = internal::InnerProduct(terms...);
        return Float(ip);
    }

    __host__ __device__ inline bool Quadratic(float a, float b, float c, float* t0, float* t1) {
        // Handle case of $a=0$ for quadratic solution
        if (a == 0) {
            if (b == 0) return false;
            *t0 = *t1 = -c / b;
            return true;
        }

        // Find quadratic discriminant
        float discrim = DifferenceOfProducts(b, b, 4 * a, c);
        if (discrim < 0) return false;
        float rootDiscrim = std::sqrt(discrim);

        // Compute quadratic _t_ values
        float q = -0.5f * (b + pstd::copysign(rootDiscrim, b));
        *t0     = q / a;
        *t1     = c / q;
        if (*t0 > *t1) pstd::swap(*t0, *t1);

        return true;
    }

    __host__ __device__ inline bool Quadratic(double a, double b, double c, double* t0, double* t1) {
        // Find quadratic discriminant
        double discrim = DifferenceOfProducts(b, b, 4 * a, c);
        if (discrim < 0) return false;
        double rootDiscrim = std::sqrt(discrim);

        if (a == 0) {
            *t0 = *t1 = -c / b;
            return true;
        }

        // Compute quadratic _t_ values
        double q = -0.5 * (b + pstd::copysign(rootDiscrim, b));
        *t0      = q / a;
        *t1      = c / q;
        if (*t0 > *t1) pstd::swap(*t0, *t1);
        return true;
    }

    template <typename Func>
    __host__ __device__ Float NewtonBisection(Float x0, Float x1, Func f, Float xEps = 1e-6f, Float fEps = 1e-6f) {
        // Check function endpoints for roots
        DCHECK_LT(x0, x1);
        Float fx0 = f(x0).first, fx1 = f(x1).first;
        if (std::abs(fx0) < fEps) return x0;
        if (std::abs(fx1) < fEps) return x1;
        bool startIsNegative = fx0 < 0;

        // Set initial midpoint using linear approximation of _f_
        Float xMid = x0 + (x1 - x0) * -fx0 / (fx1 - fx0);

        while (true) {
            // Fall back to bisection if _xMid_ is out of bounds
            if (!(x0 < xMid && xMid < x1)) xMid = (x0 + x1) / 2;

            // Evaluate function and narrow bracket range _[x0, x1]_
            std::pair<Float, Float> fxMid = f(xMid);
            DCHECK(!IsNaN(fxMid.first));
            if (startIsNegative == (fxMid.first < 0))
                x0 = xMid;
            else
                x1 = xMid;

            // Stop the iteration if converged
            if ((x1 - x0) < xEps || std::abs(fxMid.first) < fEps) return xMid;

            // Perform a Newton step
            xMid -= fxMid.first / fxMid.second;
        }
    }

    template <int N>
    pstd::optional<SquareMatrix<N>> LinearLeastSquares(const Float A[][N], const Float B[][N], int rows);

    template <int N>
    pstd::optional<SquareMatrix<N>> LinearLeastSquares(const Float A[][N], const Float B[][N], int rows) {
        SquareMatrix<N> AtA = SquareMatrix<N>::Zero();
        SquareMatrix<N> AtB = SquareMatrix<N>::Zero();

        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                for (int r = 0; r < rows; ++r) {
                    AtA[i][j] += A[r][i] * A[r][j];
                    AtB[i][j] += A[r][i] * B[r][j];
                }

        auto AtAi = Inverse(AtA);
        if (!AtAi) return {};
        return Transpose(*AtAi * AtB);
    }

    // Math Function Declarations
    int NextPrime(int x);

    // Permutation Inline Function Declarations
    __host__ __device__ inline int PermutationElement(uint32_t i, uint32_t n, uint32_t seed);

    __host__ __device__ inline int PermutationElement(uint32_t i, uint32_t l, uint32_t p) {
        uint32_t w = l - 1;
        w |= w >> 1;
        w |= w >> 2;
        w |= w >> 4;
        w |= w >> 8;
        w |= w >> 16;
        do {
            i ^= p;
            i *= 0xe170893d;
            i ^= p >> 16;
            i ^= (i & w) >> 4;
            i ^= p >> 8;
            i *= 0x0929eb3f;
            i ^= p >> 23;
            i ^= (i & w) >> 1;
            i *= 1 | p >> 27;
            i *= 0x6935fa69;
            i ^= (i & w) >> 11;
            i *= 0x74dcb303;
            i ^= (i & w) >> 2;
            i *= 0x9e501cc3;
            i ^= (i & w) >> 2;
            i *= 0xc860a3df;
            i &= w;
            i ^= i >> 5;
        } while (i >= l);
        return (i + p) % l;
    }

    __host__ __device__ inline Float ErfInv(Float a) {
#if defined(__CUDA_ARCH__)
        return erfinv(a);
#else
        // https://stackoverflow.com/a/49743348
        float p;
        float t = std::log(std::max(FMA(a, -a, 1), std::numeric_limits<Float>::min()));
        CHECK(!IsNaN(t) && !IsInf(t));
        if (std::abs(t) > 6.125f) {
            // maximum ulp error = 2.35793
            p = 3.03697567e-10f; //  0x1.4deb44p-32
            p = FMA(p, t, 2.93243101e-8f); //  0x1.f7c9aep-26
            p = FMA(p, t, 1.22150334e-6f); //  0x1.47e512p-20
            p = FMA(p, t, 2.84108955e-5f); //  0x1.dca7dep-16
            p = FMA(p, t, 3.93552968e-4f); //  0x1.9cab92p-12
            p = FMA(p, t, 3.02698812e-3f); //  0x1.8cc0dep-9
            p = FMA(p, t, 4.83185798e-3f); //  0x1.3ca920p-8
            p = FMA(p, t, -2.64646143e-1f); // -0x1.0eff66p-2
            p = FMA(p, t, 8.40016484e-1f); //  0x1.ae16a4p-1
        } else {
            // maximum ulp error = 2.35456
            p = 5.43877832e-9f; //  0x1.75c000p-28
            p = FMA(p, t, 1.43286059e-7f); //  0x1.33b458p-23
            p = FMA(p, t, 1.22775396e-6f); //  0x1.49929cp-20
            p = FMA(p, t, 1.12962631e-7f); //  0x1.e52bbap-24
            p = FMA(p, t, -5.61531961e-5f); // -0x1.d70c12p-15
            p = FMA(p, t, -1.47697705e-4f); // -0x1.35be9ap-13
            p = FMA(p, t, 2.31468701e-3f); //  0x1.2f6402p-9
            p = FMA(p, t, 1.15392562e-2f); //  0x1.7a1e4cp-7
            p = FMA(p, t, -2.32015476e-1f); // -0x1.db2aeep-3
            p = FMA(p, t, 8.86226892e-1f); //  0x1.c5bf88p-1
        }
        return a * p;
#endif // __CUDA_ARCH__
    }

    __host__ __device__ inline Float I0(Float x) {
        Float val     = 0;
        Float x2i     = 1;
        int64_t ifact = 1;
        int i4        = 1;
        // I0(x) \approx Sum_i x^(2i) / (4^i (i!)^2)
        for (int i = 0; i < 10; ++i) {
            if (i > 1) ifact *= i;
            val += x2i / (i4 * Sqr(ifact));
            x2i *= x * x;
            i4 *= 4;
        }
        return val;
    }

    __host__ __device__ inline Float LogI0(Float x) {
        if (x > 12)
            return x + 0.5f * (-std::log(2 * Pi) + std::log(1 / x) + 1 / (8 * x));
        else
            return std::log(I0(x));
    }

    __host__ __device__ inline Float Min4(Float a, Float b, Float c, Float d) {
        return std::min(std::min(a, b), std::min(c, d));
    }

    __host__ __device__ inline Float Max4(Float a, Float b, Float c, Float d) {
        return std::max(std::max(a, b), std::max(c, d));
    }

    // Interval Definition
    class Interval {
    public:
        // Interval Public Methods
        Interval() = default;
        __host__ __device__ explicit Interval(Float v) : low(v), high(v) {}

        __host__ __device__ constexpr Interval(Float low, Float high) : low(std::min(low, high)), high(std::max(low, high)) {}

        __host__ __device__ static Interval FromValueAndError(Float v, Float err) {
            Interval i;
            if (err == 0)
                i.low = i.high = v;
            else {
                i.low  = SubRoundDown(v, err);
                i.high = AddRoundUp(v, err);
            }
            return i;
        }

        __host__ __device__ Interval& operator=(Float v) {
            low = high = v;
            return *this;
        }

        __host__ __device__ Float UpperBound() const {
            return high;
        }

        __host__ __device__ Float LowerBound() const {
            return low;
        }

        __host__ __device__ Float Midpoint() const {
            return (low + high) / 2;
        }

        __host__ __device__ Float Width() const {
            return high - low;
        }

        __host__ __device__ Float operator[](int i) const {
            DCHECK(i == 0 || i == 1);
            return (i == 0) ? low : high;
        }

        __host__ __device__ explicit operator Float() const {
            return Midpoint();
        }

        __host__ __device__ bool Exactly(Float v) const {
            return low == v && high == v;
        }

        __host__ __device__ bool operator==(Float v) const {
            return Exactly(v);
        }

        __host__ __device__ Interval operator-() const {
            return {-high, -low};
        }

        __host__ __device__ Interval operator+(Interval i) const {
            return {AddRoundDown(low, i.low), AddRoundUp(high, i.high)};
        }

        __host__ __device__ Interval operator-(Interval i) const {
            return {SubRoundDown(low, i.high), SubRoundUp(high, i.low)};
        }

        __host__ __device__ Interval operator*(Interval i) const {
            Float lp[4] = {MulRoundDown(low, i.low), MulRoundDown(high, i.low), MulRoundDown(low, i.high), MulRoundDown(high, i.high)};
            Float hp[4] = {MulRoundUp(low, i.low), MulRoundUp(high, i.low), MulRoundUp(low, i.high), MulRoundUp(high, i.high)};
            return {Min4(lp[0], lp[1], lp[2], lp[3]), Max4(hp[0], hp[1], hp[2], hp[3])};
        }

        __host__ __device__ Interval operator/(Interval i) const;

        __host__ __device__ bool operator==(Interval i) const {
            return low == i.low && high == i.high;
        }

        __host__ __device__ bool operator!=(Float f) const {
            return f < low || f > high;
        }


        __host__ __device__ Interval& operator+=(Interval i) {
            *this = Interval(*this + i);
            return *this;
        }

        __host__ __device__ Interval& operator-=(Interval i) {
            *this = Interval(*this - i);
            return *this;
        }

        __host__ __device__ Interval& operator*=(Interval i) {
            *this = Interval(*this * i);
            return *this;
        }

        __host__ __device__ Interval& operator/=(Interval i) {
            *this = Interval(*this / i);
            return *this;
        }

        __host__ __device__ Interval& operator+=(Float f) {
            return *this += Interval(f);
        }

        __host__ __device__ Interval& operator-=(Float f) {
            return *this -= Interval(f);
        }

        __host__ __device__ Interval& operator*=(Float f) {
            if (f > 0)
                *this = Interval(MulRoundDown(f, low), MulRoundUp(f, high));
            else
                *this = Interval(MulRoundDown(f, high), MulRoundUp(f, low));
            return *this;
        }

        __host__ __device__ Interval& operator/=(Float f) {
            if (f > 0)
                *this = Interval(DivRoundDown(low, f), DivRoundUp(high, f));
            else
                *this = Interval(DivRoundDown(high, f), DivRoundUp(low, f));
            return *this;
        }

#if !defined(__CUDA_ARCH__)
        static const Interval Pi;
#endif

    private:
        friend struct SOA<Interval>;
        // Interval Private Members
        Float low, high;
    };

    // Interval Inline Functions
    __host__ __device__ inline bool InRange(Float v, Interval i) {
        return v >= i.LowerBound() && v <= i.UpperBound();
    }

    __host__ __device__ inline bool InRange(Interval a, Interval b) {
        return a.LowerBound() <= b.UpperBound() && a.UpperBound() >= b.LowerBound();
    }

    __host__ __device__ inline Interval Interval::operator/(Interval i) const {
        if (InRange(0, i))
            // The interval we're dividing by straddles zero, so just
            // return an interval of everything.
            return Interval(-Infinity, Infinity);

        Float lowQuot[4]  = {DivRoundDown(low, i.low), DivRoundDown(high, i.low), DivRoundDown(low, i.high), DivRoundDown(high, i.high)};
        Float highQuot[4] = {DivRoundUp(low, i.low), DivRoundUp(high, i.low), DivRoundUp(low, i.high), DivRoundUp(high, i.high)};
        return {Min4(lowQuot[0], lowQuot[1], lowQuot[2], lowQuot[3]), Max4(highQuot[0], highQuot[1], highQuot[2], highQuot[3])};
    }

    __host__ __device__ inline Interval Sqr(Interval i) {
        Float alow = std::abs(i.LowerBound()), ahigh = std::abs(i.UpperBound());
        if (alow > ahigh) pstd::swap(alow, ahigh);
        if (InRange(0, i)) return Interval(0, MulRoundUp(ahigh, ahigh));
        return Interval(MulRoundDown(alow, alow), MulRoundUp(ahigh, ahigh));
    }

    __host__ __device__ inline Interval MulPow2(Float s, Interval i);
    __host__ __device__ inline Interval MulPow2(Interval i, Float s);

    __host__ __device__ inline Interval operator+(Float f, Interval i) {
        return Interval(f) + i;
    }

    __host__ __device__ inline Interval operator-(Float f, Interval i) {
        return Interval(f) - i;
    }

    __host__ __device__ inline Interval operator*(Float f, Interval i) {
        if (f > 0)
            return Interval(MulRoundDown(f, i.LowerBound()), MulRoundUp(f, i.UpperBound()));
        else
            return Interval(MulRoundDown(f, i.UpperBound()), MulRoundUp(f, i.LowerBound()));
    }

    __host__ __device__ inline Interval operator/(Float f, Interval i) {
        if (InRange(0, i))
            // The interval we're dividing by straddles zero, so just
            // return an interval of everything.
            return Interval(-Infinity, Infinity);

        if (f > 0)
            return Interval(DivRoundDown(f, i.UpperBound()), DivRoundUp(f, i.LowerBound()));
        else
            return Interval(DivRoundDown(f, i.LowerBound()), DivRoundUp(f, i.UpperBound()));
    }

    __host__ __device__ inline Interval operator+(Interval i, Float f) {
        return i + Interval(f);
    }

    __host__ __device__ inline Interval operator-(Interval i, Float f) {
        return i - Interval(f);
    }

    __host__ __device__ inline Interval operator*(Interval i, Float f) {
        if (f > 0)
            return Interval(MulRoundDown(f, i.LowerBound()), MulRoundUp(f, i.UpperBound()));
        else
            return Interval(MulRoundDown(f, i.UpperBound()), MulRoundUp(f, i.LowerBound()));
    }

    __host__ __device__ inline Interval operator/(Interval i, Float f) {
        if (f == 0) return Interval(-Infinity, Infinity);

        if (f > 0)
            return Interval(DivRoundDown(i.LowerBound(), f), DivRoundUp(i.UpperBound(), f));
        else
            return Interval(DivRoundDown(i.UpperBound(), f), DivRoundUp(i.LowerBound(), f));
    }

    __host__ __device__ inline Float Floor(Interval i) {
        return pstd::floor(i.LowerBound());
    }

    __host__ __device__ inline Float Ceil(Interval i) {
        return pstd::ceil(i.UpperBound());
    }

    __host__ __device__ inline Float floor(Interval i) {
        return Floor(i);
    }

    __host__ __device__ inline Float ceil(Interval i) {
        return Ceil(i);
    }

    __host__ __device__ inline Float Min(Interval a, Interval b) {
        return std::min(a.LowerBound(), b.LowerBound());
    }

    __host__ __device__ inline Float Max(Interval a, Interval b) {
        return std::max(a.UpperBound(), b.UpperBound());
    }

    __host__ __device__ inline Float min(Interval a, Interval b) {
        return Min(a, b);
    }

    __host__ __device__ inline Float max(Interval a, Interval b) {
        return Max(a, b);
    }

    __host__ __device__ inline Interval Sqrt(Interval i) {
        return {SqrtRoundDown(i.LowerBound()), SqrtRoundUp(i.UpperBound())};
    }

    __host__ __device__ inline Interval sqrt(Interval i) {
        return Sqrt(i);
    }

    __host__ __device__ inline Interval FMA(Interval a, Interval b, Interval c) {
        Float low  = Min4(FMARoundDown(a.LowerBound(), b.LowerBound(), c.LowerBound()), FMARoundDown(a.UpperBound(), b.LowerBound(), c.LowerBound()), FMARoundDown(a.LowerBound(), b.UpperBound(), c.LowerBound()), FMARoundDown(a.UpperBound(), b.UpperBound(), c.LowerBound()));
        Float high = Max4(FMARoundUp(a.LowerBound(), b.LowerBound(), c.UpperBound()), FMARoundUp(a.UpperBound(), b.LowerBound(), c.UpperBound()), FMARoundUp(a.LowerBound(), b.UpperBound(), c.UpperBound()), FMARoundUp(a.UpperBound(), b.UpperBound(), c.UpperBound()));
        return Interval(low, high);
    }

    __host__ __device__ inline Interval DifferenceOfProducts(Interval a, Interval b, Interval c, Interval d) {
        Float ab[4]     = {a.LowerBound() * b.LowerBound(), a.UpperBound() * b.LowerBound(), a.LowerBound() * b.UpperBound(), a.UpperBound() * b.UpperBound()};
        Float abLow     = Min4(ab[0], ab[1], ab[2], ab[3]);
        Float abHigh    = Max4(ab[0], ab[1], ab[2], ab[3]);
        int abLowIndex  = abLow == ab[0] ? 0 : (abLow == ab[1] ? 1 : (abLow == ab[2] ? 2 : 3));
        int abHighIndex = abHigh == ab[0] ? 0 : (abHigh == ab[1] ? 1 : (abHigh == ab[2] ? 2 : 3));

        Float cd[4]     = {c.LowerBound() * d.LowerBound(), c.UpperBound() * d.LowerBound(), c.LowerBound() * d.UpperBound(), c.UpperBound() * d.UpperBound()};
        Float cdLow     = Min4(cd[0], cd[1], cd[2], cd[3]);
        Float cdHigh    = Max4(cd[0], cd[1], cd[2], cd[3]);
        int cdLowIndex  = cdLow == cd[0] ? 0 : (cdLow == cd[1] ? 1 : (cdLow == cd[2] ? 2 : 3));
        int cdHighIndex = cdHigh == cd[0] ? 0 : (cdHigh == cd[1] ? 1 : (cdHigh == cd[2] ? 2 : 3));

        // Invert cd Indices since it's subtracted...
        Float low  = DifferenceOfProducts(a[abLowIndex & 1], b[abLowIndex >> 1], c[cdHighIndex & 1], d[cdHighIndex >> 1]);
        Float high = DifferenceOfProducts(a[abHighIndex & 1], b[abHighIndex >> 1], c[cdLowIndex & 1], d[cdLowIndex >> 1]);
        DCHECK_LE(low, high);

        return {NextFloatDown(NextFloatDown(low)), NextFloatUp(NextFloatUp(high))};
    }

    __host__ __device__ inline Interval SumOfProducts(Interval a, Interval b, Interval c, Interval d) {
        return DifferenceOfProducts(a, b, -c, d);
    }

    __host__ __device__ inline Interval MulPow2(Float s, Interval i) {
        return MulPow2(i, s);
    }

    __host__ __device__ inline Interval MulPow2(Interval i, Float s) {
        Float as = std::abs(s);
        if (as < 1)
            DCHECK_EQ(1 / as, 1ull << Log2Int(1 / as));
        else
            DCHECK_EQ(as, 1ull << Log2Int(as));

        // Multiplication by powers of 2 is exaact
        return Interval(std::min(i.LowerBound() * s, i.UpperBound() * s), std::max(i.LowerBound() * s, i.UpperBound() * s));
    }

    __host__ __device__ inline Interval Abs(Interval i) {
        if (i.LowerBound() >= 0)
            // The entire interval is greater than zero, so we're all set.
            return i;
        else if (i.UpperBound() <= 0)
            // The entire interval is less than zero.
            return Interval(-i.UpperBound(), -i.LowerBound());
        else
            // The interval straddles zero.
            return Interval(0, std::max(-i.LowerBound(), i.UpperBound()));
    }

    __host__ __device__ inline Interval abs(Interval i) {
        return Abs(i);
    }

    __host__ __device__ inline Interval ACos(Interval i) {
        Float low  = std::acos(std::min<Float>(1, i.UpperBound()));
        Float high = std::acos(std::max<Float>(-1, i.LowerBound()));

        return Interval(std::max<Float>(0, NextFloatDown(low)), NextFloatUp(high));
    }

    __host__ __device__ inline Interval Sin(Interval i) {
        CHECK_GE(i.LowerBound(), -1e-16);
        CHECK_LE(i.UpperBound(), 2.0001 * Pi);
        Float low  = std::sin(std::max<Float>(0, i.LowerBound()));
        Float high = std::sin(i.UpperBound());
        if (low > high) pstd::swap(low, high);
        low  = std::max<Float>(-1, NextFloatDown(low));
        high = std::min<Float>(1, NextFloatUp(high));
        if (InRange(Pi / 2, i)) high = 1;
        if (InRange((3.f / 2.f) * Pi, i)) low = -1;

        return Interval(low, high);
    }

    __host__ __device__ inline Interval Cos(Interval i) {
        CHECK_GE(i.LowerBound(), -1e-16);
        CHECK_LE(i.UpperBound(), 2.0001 * Pi);
        Float low  = std::cos(std::max<Float>(0, i.LowerBound()));
        Float high = std::cos(i.UpperBound());
        if (low > high) pstd::swap(low, high);
        low  = std::max<Float>(-1, NextFloatDown(low));
        high = std::min<Float>(1, NextFloatUp(high));
        if (InRange(Pi, i)) low = -1;

        return Interval(low, high);
    }

    __host__ __device__ inline bool Quadratic(Interval a, Interval b, Interval c, Interval* t0, Interval* t1) {
        // Find quadratic discriminant
        Interval discrim = DifferenceOfProducts(b, b, MulPow2(4, a), c);
        if (discrim.LowerBound() < 0) return false;
        Interval floatRootDiscrim = Sqrt(discrim);

        // Compute quadratic _t_ values
        Interval q;
        if ((Float) b < 0)
            q = MulPow2(-.5, b - floatRootDiscrim);
        else
            q = MulPow2(-.5, b + floatRootDiscrim);
        *t0 = q / a;
        *t1 = c / q;
        if (t0->LowerBound() > t1->LowerBound()) pstd::swap(*t0, *t1);
        return true;
    }

    __host__ __device__ inline Interval SumSquares(Interval i) {
        return Sqr(i);
    }

    template <typename... Args>
    __host__ __device__ Interval SumSquares(Interval i, Args... args) {
        Interval ss = FMA(i, i, SumSquares(args...));
        return Interval(std::max<Float>(0, ss.LowerBound()), ss.UpperBound());
    }

    // Spline Interpolation Declarations
    __host__ __device__ Float CatmullRom(pstd::span<const Float> nodes, pstd::span<const Float> values, Float x);
    __host__ __device__ bool CatmullRomWeights(pstd::span<const Float> nodes, Float x, int* offset, pstd::span<Float> weights);
    __host__ __device__ Float IntegrateCatmullRom(pstd::span<const Float> nodes, pstd::span<const Float> values, pstd::span<Float> cdf);
    __host__ __device__ Float InvertCatmullRom(pstd::span<const Float> x, pstd::span<const Float> values, Float u);

    namespace {
        template <int N>
        __host__ __device__ inline void init(Float m[N][N], int i, int j) {}

        template <int N, typename... Args>
        __host__ __device__ inline void init(Float m[N][N], int i, int j, Float v, Args... args) {
            m[i][j] = v;
            if (++j == N) {
                ++i;
                j = 0;
            }
            init<N>(m, i, j, args...);
        }

        template <int N>
        __host__ __device__ inline void initDiag(Float m[N][N], int i) {}

        template <int N, typename... Args>
        __host__ __device__ inline void initDiag(Float m[N][N], int i, Float v, Args... args) {
            m[i][i] = v;
            initDiag<N>(m, i + 1, args...);
        }
    } // namespace

    // SquareMatrix Definition
    template <int N>
    class SquareMatrix {
    public:
        // SquareMatrix Public Methods
        __host__ __device__ static SquareMatrix Zero() {
            SquareMatrix m;
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) m.m[i][j] = 0;
            return m;
        }

        __host__ __device__ SquareMatrix() {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) m[i][j] = (i == j) ? 1 : 0;
        }

        __host__ __device__ SquareMatrix(const Float mat[N][N]) {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) m[i][j] = mat[i][j];
        }

        __host__ __device__ SquareMatrix(pstd::span<const Float> t);

        template <typename... Args>
        __host__ __device__ SquareMatrix(Float v, Args... args) {
            static_assert(1 + sizeof...(Args) == N * N, "Incorrect number of values provided to SquareMatrix constructor");
            init<N>(m, 0, 0, v, args...);
        }

        template <typename... Args>
        __host__ __device__ static SquareMatrix Diag(Float v, Args... args) {
            static_assert(1 + sizeof...(Args) == N, "Incorrect number of values provided to SquareMatrix::Diag");
            SquareMatrix m;
            initDiag<N>(m.m, 0, v, args...);
            return m;
        }

        __host__ __device__ SquareMatrix operator+(const SquareMatrix& m) const {
            SquareMatrix r = *this;
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) r.m[i][j] += m.m[i][j];
            return r;
        }

        __host__ __device__ SquareMatrix operator*(Float s) const {
            SquareMatrix r = *this;
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) r.m[i][j] *= s;
            return r;
        }

        __host__ __device__ SquareMatrix operator/(Float s) const {
            DCHECK_NE(s, 0);
            SquareMatrix r = *this;
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) r.m[i][j] /= s;
            return r;
        }

        __host__ __device__ bool operator==(const SquareMatrix<N>& m2) const {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    if (m[i][j] != m2.m[i][j]) return false;
            return true;
        }

        __host__ __device__ bool operator!=(const SquareMatrix<N>& m2) const {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j)
                    if (m[i][j] != m2.m[i][j]) return true;
            return false;
        }

        __host__ __device__ bool operator<(const SquareMatrix<N>& m2) const {
            for (int i = 0; i < N; ++i)
                for (int j = 0; j < N; ++j) {
                    if (m[i][j] < m2.m[i][j]) return true;
                    if (m[i][j] > m2.m[i][j]) return false;
                }
            return false;
        }

        __host__ __device__ bool IsIdentity() const;


        __host__ __device__ pstd::span<const Float> operator[](int i) const {
            return m[i];
        }

        __host__ __device__ pstd::span<Float> operator[](int i) {
            return pstd::span<Float>(m[i]);
        }

    private:
        Float m[N][N];
    };

    // SquareMatrix Inline Methods
    template <int N>
    __host__ __device__ bool SquareMatrix<N>::IsIdentity() const {
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                if (i == j) {
                    if (m[i][j] != 1) return false;
                } else if (m[i][j] != 0)
                    return false;
            }
        return true;
    }

    // SquareMatrix Inline Functions
    template <int N>
    __host__ __device__ SquareMatrix<N> operator*(Float s, const SquareMatrix<N>& m) {
        return m * s;
    }

    template <typename Tresult, int N, typename T>
    __host__ __device__ Tresult Mul(const SquareMatrix<N>& m, const T& v) {
        Tresult result;
        for (int i = 0; i < N; ++i) {
            result[i] = 0;
            for (int j = 0; j < N; ++j) result[i] += m[i][j] * v[j];
        }
        return result;
    }

    template <int N>
    __host__ __device__ Float Determinant(const SquareMatrix<N>& m);

    template <>
    __host__ __device__ inline Float Determinant(const SquareMatrix<3>& m) {
        Float minor12 = DifferenceOfProducts(m[1][1], m[2][2], m[1][2], m[2][1]);
        Float minor02 = DifferenceOfProducts(m[1][0], m[2][2], m[1][2], m[2][0]);
        Float minor01 = DifferenceOfProducts(m[1][0], m[2][1], m[1][1], m[2][0]);
        return FMA(m[0][2], minor01, DifferenceOfProducts(m[0][0], minor12, m[0][1], minor02));
    }

    template <int N>
    __host__ __device__ SquareMatrix<N> Transpose(const SquareMatrix<N>& m);
    template <int N>
    __host__ __device__ pstd::optional<SquareMatrix<N>> Inverse(const SquareMatrix<N>&);

    template <int N>
    __host__ __device__ SquareMatrix<N> InvertOrExit(const SquareMatrix<N>& m) {
        pstd::optional<SquareMatrix<N>> inv = Inverse(m);
        CHECK(inv.has_value());
        return *inv;
    }

    template <int N>
    __host__ __device__ SquareMatrix<N> Transpose(const SquareMatrix<N>& m) {
        SquareMatrix<N> r;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) r[i][j] = m[j][i];
        return r;
    }

    template <>
    __host__ __device__ inline pstd::optional<SquareMatrix<3>> Inverse(const SquareMatrix<3>& m) {
        Float det = Determinant(m);
        if (det == 0) return {};
        Float invDet = 1 / det;

        SquareMatrix<3> r;

        r[0][0] = invDet * DifferenceOfProducts(m[1][1], m[2][2], m[1][2], m[2][1]);
        r[1][0] = invDet * DifferenceOfProducts(m[1][2], m[2][0], m[1][0], m[2][2]);
        r[2][0] = invDet * DifferenceOfProducts(m[1][0], m[2][1], m[1][1], m[2][0]);
        r[0][1] = invDet * DifferenceOfProducts(m[0][2], m[2][1], m[0][1], m[2][2]);
        r[1][1] = invDet * DifferenceOfProducts(m[0][0], m[2][2], m[0][2], m[2][0]);
        r[2][1] = invDet * DifferenceOfProducts(m[0][1], m[2][0], m[0][0], m[2][1]);
        r[0][2] = invDet * DifferenceOfProducts(m[0][1], m[1][2], m[0][2], m[1][1]);
        r[1][2] = invDet * DifferenceOfProducts(m[0][2], m[1][0], m[0][0], m[1][2]);
        r[2][2] = invDet * DifferenceOfProducts(m[0][0], m[1][1], m[0][1], m[1][0]);

        return r;
    }

    template <int N, typename T>
    __host__ __device__ T operator*(const SquareMatrix<N>& m, const T& v) {
        return Mul<T>(m, v);
    }

    template <>
    __host__ __device__ inline SquareMatrix<4> operator*(const SquareMatrix<4>& m1, const SquareMatrix<4>& m2) {
        SquareMatrix<4> r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) r[i][j] = InnerProduct(m1[i][0], m2[0][j], m1[i][1], m2[1][j], m1[i][2], m2[2][j], m1[i][3], m2[3][j]);
        return r;
    }

    template <>
    __host__ __device__ inline SquareMatrix<3> operator*(const SquareMatrix<3>& m1, const SquareMatrix<3>& m2) {
        SquareMatrix<3> r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) r[i][j] = InnerProduct(m1[i][0], m2[0][j], m1[i][1], m2[1][j], m1[i][2], m2[2][j]);
        return r;
    }

    template <int N>
    __host__ __device__ SquareMatrix<N> operator*(const SquareMatrix<N>& m1, const SquareMatrix<N>& m2) {
        SquareMatrix<N> r;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                r[i][j] = 0;
                for (int k = 0; k < N; ++k) r[i][j] = FMA(m1[i][k], m2[k][j], r[i][j]);
            }
        return r;
    }

    template <int N>
    __host__ __device__ SquareMatrix<N>::SquareMatrix(pstd::span<const Float> t) {
        CHECK_EQ(N * N, t.size());
        for (int i = 0; i < N * N; ++i) m[i / N][i % N] = t[i];
    }

    template <int N>
    __host__ __device__ SquareMatrix<N> operator*(const SquareMatrix<N>& m1, const SquareMatrix<N>& m2);

    template <>
    __host__ __device__ inline Float Determinant(const SquareMatrix<1>& m) {
        return m[0][0];
    }

    template <>
    __host__ __device__ inline Float Determinant(const SquareMatrix<2>& m) {
        return DifferenceOfProducts(m[0][0], m[1][1], m[0][1], m[1][0]);
    }

    template <>
    __host__ __device__ inline Float Determinant(const SquareMatrix<4>& m) {
        Float s0 = DifferenceOfProducts(m[0][0], m[1][1], m[1][0], m[0][1]);
        Float s1 = DifferenceOfProducts(m[0][0], m[1][2], m[1][0], m[0][2]);
        Float s2 = DifferenceOfProducts(m[0][0], m[1][3], m[1][0], m[0][3]);

        Float s3 = DifferenceOfProducts(m[0][1], m[1][2], m[1][1], m[0][2]);
        Float s4 = DifferenceOfProducts(m[0][1], m[1][3], m[1][1], m[0][3]);
        Float s5 = DifferenceOfProducts(m[0][2], m[1][3], m[1][2], m[0][3]);

        Float c0 = DifferenceOfProducts(m[2][0], m[3][1], m[3][0], m[2][1]);
        Float c1 = DifferenceOfProducts(m[2][0], m[3][2], m[3][0], m[2][2]);
        Float c2 = DifferenceOfProducts(m[2][0], m[3][3], m[3][0], m[2][3]);

        Float c3 = DifferenceOfProducts(m[2][1], m[3][2], m[3][1], m[2][2]);
        Float c4 = DifferenceOfProducts(m[2][1], m[3][3], m[3][1], m[2][3]);
        Float c5 = DifferenceOfProducts(m[2][2], m[3][3], m[3][2], m[2][3]);

        return (DifferenceOfProducts(s0, c5, s1, c4) + DifferenceOfProducts(s2, c3, -s3, c2) + DifferenceOfProducts(s5, c0, s4, c1));
    }

    template <int N>
    __host__ __device__ Float Determinant(const SquareMatrix<N>& m) {
        SquareMatrix<N - 1> sub;
        Float det = 0;
        // Inefficient, but we don't currently use N>4 anyway..
        for (int i = 0; i < N; ++i) {
            // Sub-matrix without row 0 and column i
            for (int j = 0; j < N - 1; ++j)
                for (int k = 0; k < N - 1; ++k) sub[j][k] = m[j + 1][k < i ? k : k + 1];

            Float sign = (i & 1) ? -1 : 1;
            det += sign * m[0][i] * Determinant(sub);
        }
        return det;
    }

    template <>
    __host__ __device__ inline pstd::optional<SquareMatrix<4>> Inverse(const SquareMatrix<4>& m) {
        // Via: https://github.com/google/ion/blob/master/ion/math/matrixutils.cc,
        // (c) Google, Apache license.

        // For 4x4 do not compute the adjugate as the transpose of the cofactor
        // matrix, because this results in extra work. Several calculations can be
        // shared across the sub-determinants.
        //
        // This approach is explained in David Eberly's Geometric Tools book,
        // excerpted here:
        //   http://www.geometrictools.com/Documentation/LaplaceExpansionTheorem.pdf
        Float s0 = DifferenceOfProducts(m[0][0], m[1][1], m[1][0], m[0][1]);
        Float s1 = DifferenceOfProducts(m[0][0], m[1][2], m[1][0], m[0][2]);
        Float s2 = DifferenceOfProducts(m[0][0], m[1][3], m[1][0], m[0][3]);

        Float s3 = DifferenceOfProducts(m[0][1], m[1][2], m[1][1], m[0][2]);
        Float s4 = DifferenceOfProducts(m[0][1], m[1][3], m[1][1], m[0][3]);
        Float s5 = DifferenceOfProducts(m[0][2], m[1][3], m[1][2], m[0][3]);

        Float c0 = DifferenceOfProducts(m[2][0], m[3][1], m[3][0], m[2][1]);
        Float c1 = DifferenceOfProducts(m[2][0], m[3][2], m[3][0], m[2][2]);
        Float c2 = DifferenceOfProducts(m[2][0], m[3][3], m[3][0], m[2][3]);

        Float c3 = DifferenceOfProducts(m[2][1], m[3][2], m[3][1], m[2][2]);
        Float c4 = DifferenceOfProducts(m[2][1], m[3][3], m[3][1], m[2][3]);
        Float c5 = DifferenceOfProducts(m[2][2], m[3][3], m[3][2], m[2][3]);

        Float determinant = InnerProduct(s0, c5, -s1, c4, s2, c3, s3, c2, s5, c0, -s4, c1);
        if (determinant == 0) return {};
        Float s = 1 / determinant;

        Float inv[4][4] = {{s * InnerProduct(m[1][1], c5, m[1][3], c3, -m[1][2], c4), s * InnerProduct(-m[0][1], c5, m[0][2], c4, -m[0][3], c3), s * InnerProduct(m[3][1], s5, m[3][3], s3, -m[3][2], s4), s * InnerProduct(-m[2][1], s5, m[2][2], s4, -m[2][3], s3)},

            {s * InnerProduct(-m[1][0], c5, m[1][2], c2, -m[1][3], c1), s * InnerProduct(m[0][0], c5, m[0][3], c1, -m[0][2], c2), s * InnerProduct(-m[3][0], s5, m[3][2], s2, -m[3][3], s1), s * InnerProduct(m[2][0], s5, m[2][3], s1, -m[2][2], s2)},

            {s * InnerProduct(m[1][0], c4, m[1][3], c0, -m[1][1], c2), s * InnerProduct(-m[0][0], c4, m[0][1], c2, -m[0][3], c0), s * InnerProduct(m[3][0], s4, m[3][3], s0, -m[3][1], s2), s * InnerProduct(-m[2][0], s4, m[2][1], s2, -m[2][3], s0)},

            {s * InnerProduct(-m[1][0], c3, m[1][1], c1, -m[1][2], c0), s * InnerProduct(m[0][0], c3, m[0][2], c0, -m[0][1], c1), s * InnerProduct(-m[3][0], s3, m[3][1], s1, -m[3][2], s0), s * InnerProduct(m[2][0], s3, m[2][2], s0, -m[2][1], s1)}};

        return SquareMatrix<4>(inv);
    }

    extern template class SquareMatrix<2>;
    extern template class SquareMatrix<3>;
    extern template class SquareMatrix<4>;
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_MATH_H
