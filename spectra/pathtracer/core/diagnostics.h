#ifndef SPECTRA_DIAGNOSTICS_H
#define SPECTRA_DIAGNOSTICS_H

#include <spectra/pathtracer/core/compiler.h>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace spectra::diagnostics
{
    [[noreturn]] inline void Fatal(const char* file, int line, const char* format, ...)
    {
        std::fprintf(stderr, "Spectra fatal: %s:%d: ", file, line);
        va_list args;
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        va_end(args);
        std::fprintf(stderr, "\n");
        std::fflush(stderr);
        std::abort();
    }
} // namespace spectra::diagnostics

#ifdef SPECTRA_IS_GPU_CODE
#define SPECTRA_FATAL(...) \
    do {                   \
        assert(false);     \
        __trap();          \
    } while (false)
#define SPECTRA_CHECK(EXPR) assert(EXPR)
#define SPECTRA_CHECK_IMPL(A, B, OP) assert((A) OP (B))
#else
#define SPECTRA_FATAL(...) spectra::diagnostics::Fatal(__FILE__, __LINE__, __VA_ARGS__)
#define SPECTRA_CHECK(EXPR) \
    do { if (!(EXPR)) SPECTRA_FATAL("Check failed: %s", #EXPR); } while (false)
#define SPECTRA_CHECK_IMPL(A, B, OP)                                             \
    do {                                                                         \
        const auto spectra_check_a = (A);                                        \
        const auto spectra_check_b = (B);                                        \
        if (!(spectra_check_a OP spectra_check_b)) SPECTRA_FATAL("Check failed: %s " #OP " %s", #A, #B); \
    } while (false)
#endif

#ifndef SPECTRA_FATAL
#define SPECTRA_FATAL(...) spectra::diagnostics::Fatal(__FILE__, __LINE__, __VA_ARGS__)
#endif

#define SPECTRA_CHECK_EQ(A, B) SPECTRA_CHECK_IMPL(A, B, ==)
#define SPECTRA_CHECK_NE(A, B) SPECTRA_CHECK_IMPL(A, B, !=)
#define SPECTRA_CHECK_GT(A, B) SPECTRA_CHECK_IMPL(A, B, >)
#define SPECTRA_CHECK_GE(A, B) SPECTRA_CHECK_IMPL(A, B, >=)
#define SPECTRA_CHECK_LT(A, B) SPECTRA_CHECK_IMPL(A, B, <)
#define SPECTRA_CHECK_LE(A, B) SPECTRA_CHECK_IMPL(A, B, <=)

#ifdef NDEBUG
#define SPECTRA_DCHECK(EXPR) ((void)0)
#define SPECTRA_DCHECK_EQ(A, B) ((void)0)
#define SPECTRA_DCHECK_NE(A, B) ((void)0)
#define SPECTRA_DCHECK_GT(A, B) ((void)0)
#define SPECTRA_DCHECK_GE(A, B) ((void)0)
#define SPECTRA_DCHECK_LT(A, B) ((void)0)
#define SPECTRA_DCHECK_LE(A, B) ((void)0)
#else
#define SPECTRA_DCHECK(EXPR) SPECTRA_CHECK(EXPR)
#define SPECTRA_DCHECK_EQ(A, B) SPECTRA_CHECK_EQ(A, B)
#define SPECTRA_DCHECK_NE(A, B) SPECTRA_CHECK_NE(A, B)
#define SPECTRA_DCHECK_GT(A, B) SPECTRA_CHECK_GT(A, B)
#define SPECTRA_DCHECK_GE(A, B) SPECTRA_CHECK_GE(A, B)
#define SPECTRA_DCHECK_LT(A, B) SPECTRA_CHECK_LT(A, B)
#define SPECTRA_DCHECK_LE(A, B) SPECTRA_CHECK_LE(A, B)
#endif

#define SPECTRA_CUDA_CHECK(EXPR)                                                \
    do {                                                                        \
        cudaError_t spectra_cuda_result = (EXPR);                               \
        if (spectra_cuda_result != cudaSuccess) SPECTRA_FATAL("CUDA call %s failed: %s", #EXPR, cudaGetErrorString(spectra_cuda_result)); \
    } while (false)

#define SPECTRA_CU_CHECK(EXPR)                                                  \
    do {                                                                        \
        CUresult spectra_cu_result = (EXPR);                                    \
        if (spectra_cu_result != CUDA_SUCCESS)                                  \
        {                                                                       \
            const char* spectra_cu_error = nullptr;                             \
            CUresult spectra_cu_error_result = cuGetErrorString(spectra_cu_result, &spectra_cu_error); \
            if (spectra_cu_error_result != CUDA_SUCCESS || spectra_cu_error == nullptr) SPECTRA_FATAL("CUDA driver call %s failed with error code %d; cuGetErrorString failed with error code %d", #EXPR, int(spectra_cu_result), int(spectra_cu_error_result)); \
            SPECTRA_FATAL("CUDA driver call %s failed: %s", #EXPR,             \
                          spectra_cu_error);                                    \
        }                                                                       \
    } while (false)

#endif  // SPECTRA_DIAGNOSTICS_H
