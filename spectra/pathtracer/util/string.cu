#ifdef SPECTRA_IS_WINDOWS
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#endif // SPECTRA_IS_WINDOWS

#include <ctype.h>
#include <spectra/pathtracer/core/diagnostics.cuh>
#include <spectra/pathtracer/util/string.cuh>
#include <string>
#include <utf8proc/utf8proc.h>

namespace spectra {
    bool Atoi(std::string_view str, int* ptr) {
        try {
            *ptr = std::stoi(std::string(str.begin(), str.end()));
        } catch (...) {
            return false;
        }
        return true;
    }

    bool Atoi(std::string_view str, int64_t* ptr) {
        try {
            *ptr = std::stoll(std::string(str.begin(), str.end()));
        } catch (...) {
            return false;
        }
        return true;
    }

    bool Atof(std::string_view str, float* ptr) {
        try {
            *ptr = std::stof(std::string(str.begin(), str.end()));
        } catch (...) {
            return false;
        }
        return true;
    }

    bool Atof(std::string_view str, double* ptr) {
        try {
            *ptr = std::stod(std::string(str.begin(), str.end()));
        } catch (...) {
            return false;
        }
        return true;
    }

    std::vector<std::string> SplitStringsFromWhitespace(std::string_view str) {
        std::vector<std::string> ret;

        std::string_view::iterator start = str.begin();
        do {
            // skip leading ws
            while (start != str.end() && isspace(*start)) ++start;

            // |start| is at the start of the current word
            auto end = start;
            while (end != str.end() && !isspace(*end)) ++end;

            ret.push_back(std::string(start, end));
            start = end;
        } while (start != str.end());

        return ret;
    }

#ifdef SPECTRA_IS_WINDOWS
    std::wstring WStringFromU16String(std::u16string str) {
        std::wstring ws;
        ws.reserve(str.size());
        for (char16_t c : str) ws.push_back(c);
        return ws;
    }

    std::wstring WStringFromUTF8(std::string str) {
        return WStringFromU16String(UTF16FromUTF8(str));
    }

    std::u16string U16StringFromWString(std::wstring str) {
        std::u16string su16;
        su16.reserve(str.size());
        for (wchar_t c : str) su16.push_back(c);
        return su16;
    }

    std::string UTF8FromWString(std::wstring str) {
        return UTF8FromUTF16(U16StringFromWString(str));
    }

#endif // SPECTRA_IS_WINDOWS

    std::string UTF8FromUTF16(std::u16string str) {
        std::string utf8;
        utf8.reserve(str.size());
        for (size_t i = 0; i < str.size(); ++i) {
            utf8proc_int32_t codepoint = str[i];
            if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                if (i + 1 == str.size()) throw std::runtime_error(diagnostics::Format("Invalid UTF-16 string: missing low surrogate"));
                utf8proc_int32_t low = str[++i];
                if (low < 0xdc00 || low > 0xdfff) throw std::runtime_error(diagnostics::Format("Invalid UTF-16 string: malformed surrogate pair"));
                codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
            } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff)
                throw std::runtime_error(diagnostics::Format("Invalid UTF-16 string: unexpected low surrogate"));

            utf8proc_uint8_t buffer[4];
            utf8proc_ssize_t length = utf8proc_encode_char(codepoint, buffer);
            if (length <= 0) throw std::runtime_error(diagnostics::Format("Invalid UTF-16 code point: %d", codepoint));
            utf8.append(reinterpret_cast<char*>(buffer), length);
        }
        return utf8;
    }

    std::u16string UTF16FromUTF8(std::string str) {
        std::u16string utf16;
        const utf8proc_uint8_t* bytes = reinterpret_cast<const utf8proc_uint8_t*>(str.data());
        utf8proc_ssize_t remaining    = str.size();
        while (remaining > 0) {
            utf8proc_int32_t codepoint;
            utf8proc_ssize_t length = utf8proc_iterate(bytes, remaining, &codepoint);
            if (length < 0) throw std::runtime_error(diagnostics::Format("Invalid UTF-8 string: %s", utf8proc_errmsg(length)));
            if (codepoint <= 0xffff)
                utf16.push_back(static_cast<char16_t>(codepoint));
            else {
                codepoint -= 0x10000;
                utf16.push_back(static_cast<char16_t>(0xd800 + (codepoint >> 10)));
                utf16.push_back(static_cast<char16_t>(0xdc00 + (codepoint & 0x3ff)));
            }
            bytes += length;
            remaining -= length;
        }
        return utf16;
    }

} // namespace spectra
