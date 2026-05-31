#ifndef SPECTRA_COMPILER_H
#define SPECTRA_COMPILER_H

#if defined(__CUDA_ARCH__)
#define SPECTRA_IS_GPU_CODE
#endif

#if defined(__CUDACC__)
#define SPECTRA_CPU_GPU __host__ __device__
#define SPECTRA_GPU __device__
#if defined(SPECTRA_IS_GPU_CODE)
#define SPECTRA_CONST __device__ const
#else
#define SPECTRA_CONST const
#endif
#else
#define SPECTRA_CONST const
#define SPECTRA_CPU_GPU
#define SPECTRA_GPU
#endif

#define SPECTRA_CPU_GPU_LAMBDA(...) [ =, *this ] SPECTRA_CPU_GPU(__VA_ARGS__) mutable

#endif  // SPECTRA_COMPILER_H
