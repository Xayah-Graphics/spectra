#include <spectra/pathtracer/util/args.h>

#include <spectra/pathtracer/util/check.h>
#include <spectra/pathtracer/util/string.h>

#ifdef SPECTRA_IS_WINDOWS
#include <Windows.h>
#include <shellapi.h>
#endif

namespace spectra
{
    std::vector<std::string> GetCommandLineArguments(char* argv[])
    {
        std::vector<std::string> argStrings;
#ifdef SPECTRA_IS_WINDOWS
        // Handle UTF16-encoded arguments on Windows
        int argc;
        LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
        CHECK(argvw != nullptr);
        for (int i = 1; i < argc; ++i)
            argStrings.push_back(UTF8FromWString(argvw[i]));
        CHECK(LocalFree(argvw) == nullptr);
#else
        ++argv; // skip executable name
        while (*argv)
        {
            argStrings.push_back(*argv);
            ++argv;
        }
#endif  // SPECTRA_IS_WINDOWS
        return argStrings;
    }
} // namespace spectra
