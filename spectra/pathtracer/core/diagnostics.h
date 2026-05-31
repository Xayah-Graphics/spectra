#ifndef SPECTRA_DIAGNOSTICS_H
#define SPECTRA_DIAGNOSTICS_H

#include <spectra/scene_location.h>
#include <spectra/pathtracer/core/compiler.h>

#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace spectra::diagnostics
{
    [[nodiscard]] inline bool IsFormatConversion(char ch)
    {
        return std::string_view("diuoxXfFeEgGaAcsp").find(ch) != std::string_view::npos;
    }

    [[nodiscard]] inline std::string NextFormatSpecifier(const char** format, std::string* output)
    {
        while (**format)
        {
            if (**format != '%')
            {
                output->push_back(**format);
                ++*format;
                continue;
            }

            ++*format;
            if (**format == '%')
            {
                output->push_back('%');
                ++*format;
                continue;
            }

            std::string specifier = "%";
            while (**format)
            {
                specifier.push_back(**format);
                if (IsFormatConversion(**format))
                {
                    ++*format;
                    return specifier;
                }
                ++*format;
            }
            break;
        }

        return {};
    }

    inline void AppendRemainingFormat(std::string* output, const char* format)
    {
        while (*format)
        {
            if (*format == '%' && *(format + 1) == '%')
            {
                output->push_back('%');
                format += 2;
                continue;
            }
            if (*format == '%') throw std::runtime_error("Not enough values passed to Spectra formatter.");
            output->push_back(*format);
            ++format;
        }
    }

    inline void AppendFormat(std::string* output, const char* format)
    {
        AppendRemainingFormat(output, format);
    }

    [[nodiscard]] inline std::string StringFromStringVector(const std::vector<std::string>& values)
    {
        std::string result = "[ ";
        bool first = true;
        for (const std::string& value : values)
        {
            if (!first) result += ", ";
            result += value;
            first = false;
        }
        result += " ]";
        return result;
    }

    template <typename T>
    [[nodiscard]] inline std::string StringFromValue(T&& value)
    {
        if constexpr (std::is_same_v<std::decay_t<T>, std::vector<std::string>>)
        {
            return StringFromStringVector(value);
        }
        else if constexpr (std::is_convertible_v<T, const std::string&>)
        {
            const std::string& stringValue = value;
            return stringValue;
        }
        else if constexpr (std::is_convertible_v<T, std::string_view>)
        {
            return std::string(std::string_view(value));
        }
        else
        {
            std::ostringstream stream;
            stream << std::boolalpha << std::forward<T>(value);
            return stream.str();
        }
    }

    template <typename T>
    [[nodiscard]] inline std::string FormatValue(const std::string& specifier, T&& value)
    {
        if (specifier.empty()) throw std::runtime_error("Excess values passed to Spectra formatter.");

        if (specifier.find('s') != std::string::npos)
        {
            std::string stringValue = StringFromValue(std::forward<T>(value));
            int size = std::snprintf(nullptr, 0, specifier.c_str(), stringValue.c_str());
            if (size < 0) throw std::runtime_error("Invalid Spectra format string.");
            std::string result(std::size_t(size) + 1, '\0');
            std::snprintf(result.data(), result.size(), specifier.c_str(), stringValue.c_str());
            result.pop_back();
            return result;
        }

        if constexpr (std::is_arithmetic_v<std::decay_t<T>> || std::is_pointer_v<std::decay_t<T>>)
        {
            int size = std::snprintf(nullptr, 0, specifier.c_str(), value);
            if (size < 0) throw std::runtime_error("Invalid Spectra format string.");
            std::string result(std::size_t(size) + 1, '\0');
            std::snprintf(result.data(), result.size(), specifier.c_str(), value);
            result.pop_back();
            return result;
        }
        else
        {
            return StringFromValue(std::forward<T>(value));
        }
    }

    template <typename T, typename... Args>
    inline void AppendFormat(std::string* output, const char* format, T&& value, Args&&... args)
    {
        std::string specifier = NextFormatSpecifier(&format, output);
        output->append(FormatValue(specifier, std::forward<T>(value)));
        AppendFormat(output, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    [[nodiscard]] inline std::string Format(const char* format, Args&&... args)
    {
        std::string output;
        AppendFormat(&output, format, std::forward<Args>(args)...);
        return output;
    }

    template <typename... Args>
    [[nodiscard]] inline std::string Format(const FileLoc* location, const char* format, Args&&... args)
    {
        std::string message;
        if (location != nullptr)
        {
            message = FormatFileLocation(*location);
            message += ": ";
        }
        message += Format(format, std::forward<Args>(args)...);
        return message;
    }

    template <typename... Args>
    inline void PrintWarning(const char* format, Args&&... args)
    {
        std::string message = Format(format, std::forward<Args>(args)...);
        std::fprintf(stderr, "Warning: %s\n", message.c_str());
    }

    template <typename... Args>
    inline void PrintWarning(const FileLoc* location, const char* format, Args&&... args)
    {
        std::string message = Format(location, format, std::forward<Args>(args)...);
        std::fprintf(stderr, "Warning: %s\n", message.c_str());
    }

    [[noreturn]] inline void Fatal(const char* file, int line, const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        va_list args_copy;
        va_copy(args_copy, args);
        int size = std::vsnprintf(nullptr, 0, format, args_copy);
        va_end(args_copy);
        if (size < 0)
        {
            va_end(args);
            throw std::runtime_error("Invalid Spectra fatal format string.");
        }

        std::string message(std::size_t(size) + 1, '\0');
        std::vsnprintf(message.data(), message.size(), format, args);
        va_end(args);
        message.pop_back();

        std::string result = "Spectra fatal: ";
        result += file;
        result += ":";
        result += std::to_string(line);
        result += ": ";
        result += message;
        throw std::runtime_error(result);
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
