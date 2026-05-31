#ifndef SPECTRA_PATHTRACER_UTIL_CHECK_H
#define SPECTRA_PATHTRACER_UTIL_CHECK_H

#include <spectra/pathtracer/core/diagnostics.h>

#include <functional>
#include <string>
#include <vector>

namespace spectra
{
    void PrintStackTrace();

#ifdef SPECTRA_IS_GPU_CODE

#define CHECK(x) assert(x)
#define CHECK_IMPL(a, b, op) assert((a)op(b))

#define CHECK_EQ(a, b) CHECK_IMPL(a, b, ==)
#define CHECK_NE(a, b) CHECK_IMPL(a, b, !=)
#define CHECK_GT(a, b) CHECK_IMPL(a, b, >)
#define CHECK_GE(a, b) CHECK_IMPL(a, b, >=)
#define CHECK_LT(a, b) CHECK_IMPL(a, b, <)
#define CHECK_LE(a, b) CHECK_IMPL(a, b, <=)

#else

    // CHECK Macro Definitions
#define CHECK(x) (!(!(x) && (SPECTRA_FATAL("Check failed: %s", #x), true)))

#define CHECK_EQ(a, b) CHECK_IMPL(a, b, ==)
#define CHECK_NE(a, b) CHECK_IMPL(a, b, !=)
#define CHECK_GT(a, b) CHECK_IMPL(a, b, >)
#define CHECK_GE(a, b) CHECK_IMPL(a, b, >=)
#define CHECK_LT(a, b) CHECK_IMPL(a, b, <)
#define CHECK_LE(a, b) CHECK_IMPL(a, b, <=)

    // CHECK\_IMPL Macro Definition
#define CHECK_IMPL(a, b, op)                                                           \
    do {                                                                               \
        auto va = a;                                                                   \
        auto vb = b;                                                                   \
        if (!(va op vb))                                                               \
            SPECTRA_FATAL("Check failed: %s " #op " %s with %s = %s, %s = %s", #a, #b, #a, \
                      va, #b, vb);                                                     \
    } while (false) /* swallow semicolon */

#endif  // SPECTRA_IS_GPU_CODE

#ifdef SPECTRA_DEBUG_BUILD

#define DCHECK(x) (CHECK(x))
#define DCHECK_EQ(a, b) CHECK_EQ(a, b)
#define DCHECK_NE(a, b) CHECK_NE(a, b)
#define DCHECK_GT(a, b) CHECK_GT(a, b)
#define DCHECK_GE(a, b) CHECK_GE(a, b)
#define DCHECK_LT(a, b) CHECK_LT(a, b)
#define DCHECK_LE(a, b) CHECK_LE(a, b)

#else

#define EMPTY_CHECK \
    do {            \
    } while (false) /* swallow semicolon */

    // Use an empty check (rather than expanding the macros to nothing) to swallow the
    // semicolon at the end, and avoid empty if-statements.
#define DCHECK(x) EMPTY_CHECK

#define DCHECK_EQ(a, b) EMPTY_CHECK
#define DCHECK_NE(a, b) EMPTY_CHECK
#define DCHECK_GT(a, b) EMPTY_CHECK
#define DCHECK_GE(a, b) EMPTY_CHECK
#define DCHECK_LT(a, b) EMPTY_CHECK
#define DCHECK_LE(a, b) EMPTY_CHECK

#endif

#define CHECK_RARE(freq, condition)
#define DCHECK_RARE(freq, condition)

    // CheckCallbackScope Definition
    class CheckCallbackScope
    {
    public:
        // CheckCallbackScope Public Methods
        CheckCallbackScope(std::function<std::string(void)> callback);

        ~CheckCallbackScope();

        CheckCallbackScope(const CheckCallbackScope&) = delete;
        CheckCallbackScope& operator=(const CheckCallbackScope&) = delete;

        static void Fail();

    private:
        // CheckCallbackScope Private Members
        static std::vector<std::function<std::string(void)>> callbacks;
    };
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_CHECK_H
