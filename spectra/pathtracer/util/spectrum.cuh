#ifndef SPECTRA_PATHTRACER_UTIL_SPECTRUM_H
#define SPECTRA_PATHTRACER_UTIL_SPECTRUM_H

// PhysLight code contributed by Anders Langlands and Luca Fascione
// Copyright (c) 2020, Weta Digital, Ltd.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <numeric>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/color.cuh>
#include <spectra/pathtracer/util/float.cuh>
#include <spectra/pathtracer/util/hash.cuh>
#include <spectra/pathtracer/util/math.cuh>
#include <spectra/pathtracer/util/memory.cuh>
#include <spectra/pathtracer/util/pstd.cuh>
#include <spectra/pathtracer/util/sampling.cuh>
#include <spectra/pathtracer/util/taggedptr.cuh>
#include <string>
#include <vector>

namespace spectra {
    template <typename T>
    struct SOA;

    class RGBColorSpace;
    class SampledSpectrum;
    class SampledWavelengths;
    class XYZ;

    // Spectrum Constants
    constexpr Float Lambda_min = 360, Lambda_max = 830;

    static constexpr int NSpectrumSamples = 4;

    static constexpr Float CIE_Y_integral = 106.856895;

    // Spectrum Definition
    class BlackbodySpectrum;
    class ConstantSpectrum;
    class PiecewiseLinearSpectrum;
    class DenselySampledSpectrum;
    class RGBAlbedoSpectrum;
    class RGBUnboundedSpectrum;
    class RGBIlluminantSpectrum;

    class Spectrum : public TaggedPointer<ConstantSpectrum, DenselySampledSpectrum, PiecewiseLinearSpectrum, RGBAlbedoSpectrum, RGBUnboundedSpectrum, RGBIlluminantSpectrum, BlackbodySpectrum> {
    public:
        // Spectrum Interface
        using TaggedPointer::TaggedPointer;

        __host__ __device__ Float operator()(Float lambda) const;

        __host__ __device__ Float MaxValue() const;

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const;
    };

    // Spectrum Function Declarations
    __host__ __device__ inline Float Blackbody(Float lambda, Float T) {
        if (T <= 0) return 0;
        const Float c  = 299792458.f;
        const Float h  = 6.62606957e-34f;
        const Float kb = 1.3806488e-23f;
        // Return emitted radiance for blackbody at wavelength _lambda_
        Float l  = lambda * 1e-9f;
        Float Le = (2 * h * c * c) / (Pow<5>(l) * (FastExp((h * c) / (l * kb * T)) - 1));
        CHECK(!IsNaN(Le));
        return Le;
    }

    namespace Spectra {
        DenselySampledSpectrum D(Float T, Allocator alloc);
    } // namespace Spectra

    Float SpectrumToPhotometric(Spectrum s);

    XYZ SpectrumToXYZ(Spectrum s);

    // SampledSpectrum Definition
    class SampledSpectrum {
    public:
        // SampledSpectrum Public Methods
        __host__ __device__ SampledSpectrum operator+(const SampledSpectrum& s) const {
            SampledSpectrum ret = *this;
            return ret += s;
        }

        __host__ __device__ SampledSpectrum& operator-=(const SampledSpectrum& s) {
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] -= s.values[i];
            return *this;
        }

        __host__ __device__ SampledSpectrum operator-(const SampledSpectrum& s) const {
            SampledSpectrum ret = *this;
            return ret -= s;
        }

        __host__ __device__ friend SampledSpectrum operator-(Float a, const SampledSpectrum& s) {
            DCHECK(!IsNaN(a));
            SampledSpectrum ret;
            for (int i = 0; i < NSpectrumSamples; ++i) ret.values[i] = a - s.values[i];
            return ret;
        }

        __host__ __device__ SampledSpectrum& operator*=(const SampledSpectrum& s) {
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] *= s.values[i];
            return *this;
        }

        __host__ __device__ SampledSpectrum operator*(const SampledSpectrum& s) const {
            SampledSpectrum ret = *this;
            return ret *= s;
        }

        __host__ __device__ SampledSpectrum operator*(Float a) const {
            DCHECK(!IsNaN(a));
            SampledSpectrum ret = *this;
            for (int i = 0; i < NSpectrumSamples; ++i) ret.values[i] *= a;
            return ret;
        }

        __host__ __device__ SampledSpectrum& operator*=(Float a) {
            DCHECK(!IsNaN(a));
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] *= a;
            return *this;
        }

        __host__ __device__ friend SampledSpectrum operator*(Float a, const SampledSpectrum& s) {
            return s * a;
        }

        __host__ __device__ SampledSpectrum& operator/=(const SampledSpectrum& s) {
            for (int i = 0; i < NSpectrumSamples; ++i) {
                DCHECK_NE(0, s.values[i]);
                values[i] /= s.values[i];
            }
            return *this;
        }

        __host__ __device__ SampledSpectrum operator/(const SampledSpectrum& s) const {
            SampledSpectrum ret = *this;
            return ret /= s;
        }

        __host__ __device__ SampledSpectrum& operator/=(Float a) {
            DCHECK_NE(a, 0);
            DCHECK(!IsNaN(a));
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] /= a;
            return *this;
        }

        __host__ __device__ SampledSpectrum operator/(Float a) const {
            SampledSpectrum ret = *this;
            return ret /= a;
        }

        __host__ __device__ SampledSpectrum operator-() const {
            SampledSpectrum ret;
            for (int i = 0; i < NSpectrumSamples; ++i) ret.values[i] = -values[i];
            return ret;
        }

        __host__ __device__ bool operator==(const SampledSpectrum& s) const {
            return values == s.values;
        }

        __host__ __device__ bool operator!=(const SampledSpectrum& s) const {
            return values != s.values;
        }


        __host__ __device__ bool HasNaNs() const {
            for (int i = 0; i < NSpectrumSamples; ++i)
                if (IsNaN(values[i])) return true;
            return false;
        }

        __host__ __device__ XYZ ToXYZ(const SampledWavelengths& lambda) const;
        __host__ __device__ RGB ToRGB(const SampledWavelengths& lambda, const RGBColorSpace& cs) const;
        __host__ __device__ Float y(const SampledWavelengths& lambda) const;

        SampledSpectrum() = default;
        __host__ __device__ explicit SampledSpectrum(Float c) {
            values.fill(c);
        }

        __host__ __device__ SampledSpectrum(pstd::span<const Float> v) {
            DCHECK_EQ(NSpectrumSamples, v.size());
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] = v[i];
        }

        __host__ __device__ Float operator[](int i) const {
            DCHECK(i >= 0 && i < NSpectrumSamples);
            return values[i];
        }

        __host__ __device__ Float& operator[](int i) {
            DCHECK(i >= 0 && i < NSpectrumSamples);
            return values[i];
        }

        __host__ __device__ explicit operator bool() const {
            for (int i = 0; i < NSpectrumSamples; ++i)
                if (values[i] != 0) return true;
            return false;
        }

        __host__ __device__ SampledSpectrum& operator+=(const SampledSpectrum& s) {
            for (int i = 0; i < NSpectrumSamples; ++i) values[i] += s.values[i];
            return *this;
        }

        __host__ __device__ Float MinComponentValue() const {
            Float m = values[0];
            for (int i = 1; i < NSpectrumSamples; ++i) m = std::min(m, values[i]);
            return m;
        }

        __host__ __device__ Float MaxComponentValue() const {
            Float m = values[0];
            for (int i = 1; i < NSpectrumSamples; ++i) m = std::max(m, values[i]);
            return m;
        }

        __host__ __device__ Float Average() const {
            Float sum = values[0];
            for (int i = 1; i < NSpectrumSamples; ++i) sum += values[i];
            return sum / NSpectrumSamples;
        }

    private:
        friend struct SOA<SampledSpectrum>;
        pstd::array<Float, NSpectrumSamples> values;
    };

    // SampledWavelengths Definitions
    class SampledWavelengths {
    public:
        // SampledWavelengths Public Methods
        __host__ __device__ bool operator==(const SampledWavelengths& swl) const {
            return lambda == swl.lambda && pdf == swl.pdf;
        }

        __host__ __device__ bool operator!=(const SampledWavelengths& swl) const {
            return lambda != swl.lambda || pdf != swl.pdf;
        }


        __host__ __device__ static SampledWavelengths SampleUniform(Float u, Float lambda_min = Lambda_min, Float lambda_max = Lambda_max) {
            SampledWavelengths swl;
            // Sample first wavelength using _u_
            swl.lambda[0] = Lerp(u, lambda_min, lambda_max);

            // Initialize _lambda_ for remaining wavelengths
            Float delta = (lambda_max - lambda_min) / NSpectrumSamples;
            for (int i = 1; i < NSpectrumSamples; ++i) {
                swl.lambda[i] = swl.lambda[i - 1] + delta;
                if (swl.lambda[i] > lambda_max) swl.lambda[i] = lambda_min + (swl.lambda[i] - lambda_max);
            }

            // Compute PDF for sampled wavelengths
            for (int i = 0; i < NSpectrumSamples; ++i) swl.pdf[i] = 1 / (lambda_max - lambda_min);

            return swl;
        }

        __host__ __device__ Float operator[](int i) const {
            return lambda[i];
        }

        __host__ __device__ Float& operator[](int i) {
            return lambda[i];
        }

        __host__ __device__ SampledSpectrum PDF() const {
            return SampledSpectrum(pdf);
        }

        __host__ __device__ void TerminateSecondary() {
            if (SecondaryTerminated()) return;
            // Update wavelength probabilities for termination
            for (int i = 1; i < NSpectrumSamples; ++i) pdf[i] = 0;
            pdf[0] /= NSpectrumSamples;
        }

        __host__ __device__ bool SecondaryTerminated() const {
            for (int i = 1; i < NSpectrumSamples; ++i)
                if (pdf[i] != 0) return false;
            return true;
        }

        __host__ __device__ static SampledWavelengths SampleVisible(Float u) {
            SampledWavelengths swl;
            for (int i = 0; i < NSpectrumSamples; ++i) {
                // Compute _up_ for $i$th wavelength sample
                Float up = u + Float(i) / NSpectrumSamples;
                if (up > 1) up -= 1;

                swl.lambda[i] = SampleVisibleWavelengths(up);
                swl.pdf[i]    = VisibleWavelengthsPDF(swl.lambda[i]);
            }
            return swl;
        }

    private:
        // SampledWavelengths Private Members
        friend struct SOA<SampledWavelengths>;
        pstd::array<Float, NSpectrumSamples> lambda, pdf;
    };

    // Spectrum Definitions
    class ConstantSpectrum {
    public:
        __host__ __device__ ConstantSpectrum(Float c) : c(c) {}

        __host__ __device__ Float operator()(Float lambda) const {
            return c;
        }

        // ConstantSpectrum Public Methods
        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths&) const;

        __host__ __device__ Float MaxValue() const {
            return c;
        }

    private:
        Float c;
    };

    class DenselySampledSpectrum {
    public:
        // DenselySampledSpectrum Public Methods
        DenselySampledSpectrum(int lambda_min = Lambda_min, int lambda_max = Lambda_max, Allocator alloc = {}) : lambda_min(lambda_min), lambda_max(lambda_max), values(lambda_max - lambda_min + 1, alloc) {}

        DenselySampledSpectrum(Spectrum s, Allocator alloc) : DenselySampledSpectrum(s, Lambda_min, Lambda_max, alloc) {}

        DenselySampledSpectrum(const DenselySampledSpectrum& s, Allocator alloc) : lambda_min(s.lambda_min), lambda_max(s.lambda_max), values(s.values.begin(), s.values.end(), alloc) {}

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) {
                int offset = std::lround(lambda[i]) - lambda_min;
                if (offset < 0 || offset >= values.size())
                    s[i] = 0;
                else
                    s[i] = values[offset];
            }
            return s;
        }

        __host__ __device__ void Scale(Float s) {
            for (Float& v : values) v *= s;
        }

        __host__ __device__ Float MaxValue() const {
            return *std::max_element(values.begin(), values.end());
        }


        DenselySampledSpectrum(Spectrum spec, int lambda_min = Lambda_min, int lambda_max = Lambda_max, Allocator alloc = {}) : lambda_min(lambda_min), lambda_max(lambda_max), values(lambda_max - lambda_min + 1, alloc) {
            CHECK_GE(lambda_max, lambda_min);
            if (spec)
                for (int lambda = lambda_min; lambda <= lambda_max; ++lambda) values[lambda - lambda_min] = spec(lambda);
        }

        template <typename F>
        static DenselySampledSpectrum SampleFunction(F func, int lambda_min = Lambda_min, int lambda_max = Lambda_max, Allocator alloc = {}) {
            DenselySampledSpectrum s(lambda_min, lambda_max, alloc);
            for (int lambda = lambda_min; lambda <= lambda_max; ++lambda) s.values[lambda - lambda_min] = func(lambda);
            return s;
        }

        __host__ __device__ Float operator()(Float lambda) const {
            DCHECK_GT(lambda, 0);
            int offset = std::lround(lambda) - lambda_min;
            if (offset < 0 || offset >= values.size()) return 0;
            return values[offset];
        }

        __host__ __device__ bool operator==(const DenselySampledSpectrum& d) const {
            if (lambda_min != d.lambda_min || lambda_max != d.lambda_max || values.size() != d.values.size()) return false;
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i] != d.values[i]) return false;
            return true;
        }

    private:
        friend struct std::hash<DenselySampledSpectrum>;
        // DenselySampledSpectrum Private Members
        int lambda_min, lambda_max;
        pstd::vector<Float> values;
    };

    class PiecewiseLinearSpectrum {
    public:
        // PiecewiseLinearSpectrum Public Methods
        PiecewiseLinearSpectrum() = default;

        __host__ __device__ void Scale(Float s) {
            for (Float& v : values) v *= s;
        }

        __host__ __device__ Float MaxValue() const;

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) s[i] = (*this)(lambda[i]);
            return s;
        }

        __host__ __device__ Float operator()(Float lambda) const;


        PiecewiseLinearSpectrum(pstd::span<const Float> lambdas, pstd::span<const Float> values, Allocator alloc = {});

        static pstd::optional<Spectrum> Read(const std::string& filename, Allocator alloc);

        static PiecewiseLinearSpectrum* FromInterleaved(pstd::span<const Float> samples, bool normalize, Allocator alloc);

    private:
        // PiecewiseLinearSpectrum Private Members
        pstd::vector<Float> lambdas, values;
    };

    class BlackbodySpectrum {
    public:
        // BlackbodySpectrum Public Methods
        __host__ __device__ BlackbodySpectrum(Float T) : T(T) {
            // Compute blackbody normalization constant for given temperature
            Float lambdaMax     = 2.8977721e-3f / T;
            normalizationFactor = 1 / Blackbody(lambdaMax * 1e9f, T);
        }

        __host__ __device__ Float operator()(Float lambda) const {
            return Blackbody(lambda, T) * normalizationFactor;
        }

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) s[i] = Blackbody(lambda[i], T) * normalizationFactor;
            return s;
        }

        __host__ __device__ Float MaxValue() const {
            return 1.f;
        }

    private:
        // BlackbodySpectrum Private Members
        Float T;
        Float normalizationFactor;
    };

    class RGBAlbedoSpectrum {
    public:
        // RGBAlbedoSpectrum Public Methods
        __host__ __device__ Float operator()(Float lambda) const {
            return rsp(lambda);
        }

        __host__ __device__ Float MaxValue() const {
            return rsp.MaxValue();
        }

        __host__ __device__ RGBAlbedoSpectrum(const RGBColorSpace& cs, RGB rgb);

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) s[i] = rsp(lambda[i]);
            return s;
        }

    private:
        // RGBAlbedoSpectrum Private Members
        RGBSigmoidPolynomial rsp;
    };

    class RGBUnboundedSpectrum {
    public:
        // RGBUnboundedSpectrum Public Methods
        __host__ __device__ Float operator()(Float lambda) const {
            return scale * rsp(lambda);
        }

        __host__ __device__ Float MaxValue() const {
            return scale * rsp.MaxValue();
        }

        __host__ __device__ RGBUnboundedSpectrum(const RGBColorSpace& cs, RGB rgb);

        __host__ __device__ RGBUnboundedSpectrum() : rsp(0, 0, 0), scale(0) {}

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) s[i] = scale * rsp(lambda[i]);
            return s;
        }

    private:
        // RGBUnboundedSpectrum Private Members
        Float scale = 1;
        RGBSigmoidPolynomial rsp;
    };

    class RGBIlluminantSpectrum {
    public:
        // RGBIlluminantSpectrum Public Methods
        RGBIlluminantSpectrum() = default;
        __host__ __device__ RGBIlluminantSpectrum(const RGBColorSpace& cs, RGB rgb);

        __host__ __device__ Float operator()(Float lambda) const {
            if (!illuminant) return 0;
            return scale * rsp(lambda) * (*illuminant)(lambda);
        }

        __host__ __device__ Float MaxValue() const {
            if (!illuminant) return 0;
            return scale * rsp.MaxValue() * illuminant->MaxValue();
        }

        __host__ __device__ const DenselySampledSpectrum* Illuminant() const {
            return illuminant;
        }

        __host__ __device__ SampledSpectrum Sample(const SampledWavelengths& lambda) const {
            if (!illuminant) return SampledSpectrum(0);
            SampledSpectrum s;
            for (int i = 0; i < NSpectrumSamples; ++i) s[i] = scale * rsp(lambda[i]);
            return s * illuminant->Sample(lambda);
        }

    private:
        // RGBIlluminantSpectrum Private Members
        Float scale;
        RGBSigmoidPolynomial rsp;
        const DenselySampledSpectrum* illuminant;
    };

    // SampledSpectrum Inline Functions
    __host__ __device__ inline SampledSpectrum SafeDiv(SampledSpectrum a, SampledSpectrum b) {
        SampledSpectrum r;
        for (int i = 0; i < NSpectrumSamples; ++i) r[i] = (b[i] != 0) ? a[i] / b[i] : 0.;
        return r;
    }

    template <typename U, typename V>
    __host__ __device__ SampledSpectrum Clamp(const SampledSpectrum& s, U low, V high) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = spectra::Clamp(s[i], low, high);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum ClampZero(const SampledSpectrum& s) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = std::max<Float>(0, s[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum Sqrt(const SampledSpectrum& s) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = std::sqrt(s[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum SafeSqrt(const SampledSpectrum& s) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = SafeSqrt(s[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum Pow(const SampledSpectrum& s, Float e) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = std::pow(s[i], e);
        return ret;
    }

    __host__ __device__ inline SampledSpectrum Exp(const SampledSpectrum& s) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = std::exp(s[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum FastExp(const SampledSpectrum& s) {
        SampledSpectrum ret;
        for (int i = 0; i < NSpectrumSamples; ++i) ret[i] = FastExp(s[i]);
        DCHECK(!ret.HasNaNs());
        return ret;
    }

    __host__ __device__ inline SampledSpectrum Bilerp(pstd::array<Float, 2> p, pstd::span<const SampledSpectrum> v) {
        return ((1 - p[0]) * (1 - p[1]) * v[0] + p[0] * (1 - p[1]) * v[1] + (1 - p[0]) * p[1] * v[2] + p[0] * p[1] * v[3]);
    }

    __host__ __device__ inline SampledSpectrum Lerp(Float t, const SampledSpectrum& s1, const SampledSpectrum& s2) {
        return (1 - t) * s1 + t * s2;
    }

    // Spectral Data Declarations
    namespace Spectra {
        void Init(Allocator alloc);
        void Reset();

        __host__ __device__ const DenselySampledSpectrum& X();
        __host__ __device__ const DenselySampledSpectrum& Y();
        __host__ __device__ const DenselySampledSpectrum& Z();
    } // namespace Spectra

    // Spectral Function Declarations
    Spectrum GetNamedSpectrum(std::string name);

    std::string FindMatchingNamedSpectrum(Spectrum s);

    __host__ __device__ Float InnerProduct(Spectrum f, Spectrum g);

} // namespace spectra

namespace std {
    template <>
    struct hash<spectra::DenselySampledSpectrum> {
        __host__ __device__ size_t operator()(const spectra::DenselySampledSpectrum& s) const {
            return spectra::HashBuffer(s.values.data(), s.values.size());
        }
    };
} // namespace std

#endif // SPECTRA_PATHTRACER_UTIL_SPECTRUM_H
