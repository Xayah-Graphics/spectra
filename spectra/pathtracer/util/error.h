#ifndef SPECTRA_PATHTRACER_UTIL_ERROR_H
#define SPECTRA_PATHTRACER_UTIL_ERROR_H

#include <spectra/pathtracer/util/float.h>

#include <spectra/pathtracer/util/print.h>
#include <spectra/pathtracer/util/pstd.h>

#include <string>
#include <string_view>

namespace spectra
{
    // FileLoc Definition
    struct FileLoc
    {
        FileLoc() = default;

        FileLoc(std::string_view filename) : filename(filename)
        {
        }

        std::string ToString() const;

        std::string_view filename;
        int line = 1, column = 0;
    };

    void SuppressErrorMessages();

    // Error Reporting Function Declarations
    void Warning(const FileLoc* loc, const char* message);
    void Error(const FileLoc* loc, const char* message);
    [[noreturn]] void ErrorExit(const FileLoc* loc, const char* message);

    template <typename... Args>
    inline void Warning(const char* fmt, Args&&... args);
    template <typename... Args>
    inline void Error(const char* fmt, Args&&... args);
    template <typename... Args>
    [[noreturn]] inline void ErrorExit(const char* fmt, Args&&... args);

    // Error Reporting Inline Functions
    template <typename... Args>
    inline void Warning(const FileLoc* loc, const char* fmt, Args&&... args)
    {
        Warning(loc, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    template <typename... Args>
    inline void Warning(const char* fmt, Args&&... args)
    {
        Warning(nullptr, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    template <typename... Args>
    inline void Error(const char* fmt, Args&&... args)
    {
        Error(nullptr, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    template <typename... Args>
    inline void Error(const FileLoc* loc, const char* fmt, Args&&... args)
    {
        Error(loc, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    template <typename... Args>
    [[noreturn]] inline void ErrorExit(const char* fmt, Args&&... args)
    {
        ErrorExit(nullptr, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    template <typename... Args>
    [[noreturn]] inline void ErrorExit(const FileLoc* loc, const char* fmt, Args&&... args)
    {
        ErrorExit(loc, StringPrintf(fmt, std::forward<Args>(args)...).c_str());
    }

    int LastError();
    std::string ErrorString(int errorId = LastError());
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_ERROR_H
