#ifndef SPECTRA_PATHTRACER_UTIL_SOA_H
#define SPECTRA_PATHTRACER_UTIL_SOA_H

#include <pathtracer/base/bssrdf.cuh>
#include <pathtracer/base/material.cuh>
#include <pathtracer/base/medium.cuh>
#include <pathtracer/core/bsdf.cuh>
#include <pathtracer/core/bssrdf.cuh>
#include <pathtracer/core/interaction.cuh>
#include <pathtracer/core/ray.cuh>
#include <pathtracer/util/float.cuh>
#include <pathtracer/util/math.cuh>
#include <pathtracer/util/memory.cuh>
#include <pathtracer/util/pstd.cuh>
#include <pathtracer/util/spectrum.cuh>
#include <pathtracer/util/vecmath.cuh>

namespace spectra {
    struct alignas(16) Float4 {
        Float v[4];
    };

    __host__ __device__ inline Float4 Load4(const Float4* p) {
#if defined(__CUDA_ARCH__) && !defined(SPECTRA_FLOAT_AS_DOUBLE)
        float4 v = *(const float4*) p;
        return {{v.x, v.y, v.z, v.w}};
#else
        return *p;
#endif
    }

    __host__ __device__ inline void Store4(Float4* p, Float4 v) {
#if defined(__CUDA_ARCH__) && !defined(SPECTRA_FLOAT_AS_DOUBLE)
        *(float4*) p = make_float4(v.v[0], v.v[1], v.v[2], v.v[3]);
#else
        *p = v;
#endif
    }

    template <>
    struct SOA<SampledSpectrum> {
        SOA() = default;

        SOA(int size, Allocator alloc) {
            if constexpr ((NSpectrumSamples % 4) == 0) {
                nAlloc = n4 * size;
                ptr4   = alloc.allocate_object<Float4>(nAlloc);
            } else {
                nAlloc = size * NSpectrumSamples;
                ptr1   = alloc.allocate_object<Float>(nAlloc);
            }
        }

        SOA& operator=(const SOA& s) {
            nAlloc = s.nAlloc;
            ptr4   = s.ptr4;
            ptr1   = s.ptr1;
            return *this;
        }

        __host__ __device__ SampledSpectrum operator[](int i) const {
            SampledSpectrum s;
            if constexpr ((NSpectrumSamples % 4) == 0) {
                int offset = n4 * i;
                DCHECK_LT(offset, nAlloc);
                for (int i = 0; i < n4; ++i, ++offset) {
                    Float4 v4 = Load4(ptr4 + offset);
                    for (int j = 0; j < 4; ++j) s[4 * i + j] = v4.v[j];
                }
            } else {
                int offset = i * NSpectrumSamples;
                DCHECK_LT(offset, nAlloc);
                for (int i = 0; i < NSpectrumSamples; ++i) s[i] = ptr1[offset + i];
            }
            return s;
        }

        struct GetSetIndirector {
            __host__ __device__ operator SampledSpectrum() const {
                const SOA<SampledSpectrum>* constSoa = soa;
                return (*constSoa)[index];
            }

            __host__ __device__ void operator=(const SampledSpectrum& s) {
                if constexpr ((NSpectrumSamples % 4) == 0) {
                    int offset = n4 * index;
                    DCHECK_LT(offset, soa->nAlloc);
                    for (int i = 0; i < n4; ++i, ++offset) Store4(soa->ptr4 + offset, {s[4 * i], s[4 * i + 1], s[4 * i + 2], s[4 * i + 3]});
                } else {
                    int offset = index * NSpectrumSamples;
                    DCHECK_LT(offset, soa->nAlloc);
                    for (int i = 0; i < NSpectrumSamples; ++i) soa->ptr1[offset + i] = s[i];
                }
            }

            static constexpr int n4 = (NSpectrumSamples + 3) / 4;
            SOA<SampledSpectrum>* soa;
            int index;
        };

        __host__ __device__ GetSetIndirector operator[](int i) {
            return GetSetIndirector{this, i};
        }

    private:
        // number of float4s needed per SampledSpectrum
        static constexpr int n4 = (NSpectrumSamples + 3) / 4;

        int nAlloc;
        Float4* SPECTRA_RESTRICT ptr4 = nullptr;
        Float* SPECTRA_RESTRICT ptr1  = nullptr;
    };

    template <>
    struct SOA<SampledWavelengths> {
        SOA() = default;

        SOA(int size, Allocator alloc) {
            if constexpr ((NSpectrumSamples % 4) == 0) {
                nAlloc  = n4 * size;
                lambda4 = alloc.allocate_object<Float4>(nAlloc);
                pdf4    = alloc.allocate_object<Float4>(nAlloc);
            } else {
                nAlloc  = size * NSpectrumSamples;
                lambda1 = alloc.allocate_object<Float>(nAlloc);
                pdf1    = alloc.allocate_object<Float>(nAlloc);
            }
        }

        SOA& operator=(const SOA& s) {
            nAlloc  = s.nAlloc;
            lambda4 = s.lambda4;
            pdf4    = s.pdf4;
            lambda1 = s.lambda1;
            pdf1    = s.pdf1;
            return *this;
        }

        __host__ __device__ SampledWavelengths operator[](int i) const {
            SampledWavelengths l;
            if constexpr ((NSpectrumSamples % 4) == 0) {
                int offset = n4 * i;
                for (int i = 0; i < n4; ++i, ++offset) {
                    DCHECK_LT(offset, nAlloc);
                    Float4 l4 = Load4(lambda4 + offset);
                    Float4 p4 = Load4(pdf4 + offset);
                    for (int j = 0; j < 4; ++j) {
                        l.lambda[4 * i + j] = l4.v[j];
                        l.pdf[4 * i + j]    = p4.v[j];
                    }
                }
            } else {
                int offset = NSpectrumSamples * i;
                for (int i = 0; i < NSpectrumSamples; ++i) l.lambda[i] = lambda1[offset + i];
                for (int i = 0; i < NSpectrumSamples; ++i) l.pdf[i] = pdf1[offset + i];
            }
            return l;
        }

        struct GetSetIndirector {
            __host__ __device__ operator SampledWavelengths() const {
                const SOA<SampledWavelengths>* constSoa = soa;
                return (*constSoa)[index];
            }

            __host__ __device__ void operator=(const SampledWavelengths& s) {
                if constexpr ((NSpectrumSamples % 4) == 0) {
                    int offset = n4 * index;
                    for (int i = 0; i < n4; ++i, ++offset) {
                        Store4(soa->lambda4 + offset, {s.lambda[4 * i], s.lambda[4 * i + 1], s.lambda[4 * i + 2], s.lambda[4 * i + 3]});
                        Store4(soa->pdf4 + offset, {s.pdf[4 * i], s.pdf[4 * i + 1], s.pdf[4 * i + 2], s.pdf[4 * i + 3]});
                    }
                } else {
                    int offset = index * NSpectrumSamples;
                    for (int i = 0; i < NSpectrumSamples; ++i) soa->lambda1[offset + i] = s.lambda[i];
                    for (int i = 0; i < NSpectrumSamples; ++i) soa->pdf1[offset + i] = s.pdf[i];
                }
            }

            static constexpr int n4 = (NSpectrumSamples + 3) / 4;
            SOA<SampledWavelengths>* soa;
            int index;
        };

        __host__ __device__ GetSetIndirector operator[](int i) {
            return GetSetIndirector{this, i};
        }

    private:
        static constexpr int n4 = (NSpectrumSamples + 3) / 4;

        int nAlloc;
        Float4* SPECTRA_RESTRICT lambda4 = nullptr;
        Float4* SPECTRA_RESTRICT pdf4    = nullptr;
        Float* SPECTRA_RESTRICT lambda1  = nullptr;
        Float* SPECTRA_RESTRICT pdf1     = nullptr;
    };

#include "spectra_soa.cuh"
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_SOA_H
