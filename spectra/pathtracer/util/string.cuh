#ifndef SPECTRA_PATHTRACER_UTIL_STRING_H
#define SPECTRA_PATHTRACER_UTIL_STRING_H

#include <string>
#include <string_view>
#include <vector>

namespace spectra {
    bool Atoi(std::string_view str, int*);
    bool Atoi(std::string_view str, int64_t*);
    bool Atof(std::string_view str, float*);
    bool Atof(std::string_view str, double*);

    std::vector<std::string> SplitStringsFromWhitespace(std::string_view str);

    // String Utility Function Declarations
    std::string UTF8FromUTF16(std::u16string str);
    std::u16string UTF16FromUTF8(std::string str);

#ifdef SPECTRA_IS_WINDOWS
    std::wstring WStringFromUTF8(std::string str);
    std::string UTF8FromWString(std::wstring str);
#endif // SPECTRA_IS_WINDOWS
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_STRING_H
