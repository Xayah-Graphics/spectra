#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <pathtracer/core/diagnostics.cuh>
#include <pathtracer/util/file.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace spectra {
    [[nodiscard]] static std::string SystemErrorString() {
        int error           = errno;
        std::string message = strerror(error);
        message += " (";
        message += std::to_string(error);
        message += ")";
        return message;
    }

    static std::string NormalizedExtension(std::string extension) {
        if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension;
    }

    static bool ParseFloatToken(std::string_view text, Float* value) {
        const char* begin             = text.data();
        const char* end               = begin + text.size();
        std::from_chars_result result = std::from_chars(begin, end, *value);
        return result.ec == std::errc{} && result.ptr == end;
    }

    bool HasExtension(std::string filename, std::string e) {
        return NormalizedExtension(std::filesystem::path(filename).extension().string()) == NormalizedExtension(e);
    }

    bool FileExists(std::string filename) {
        return std::filesystem::exists(std::filesystem::path(filename));
    }

    std::string ReadFileContents(std::string filename) {
        std::ifstream ifs(filename, std::ios::binary);
        if (!ifs) throw std::runtime_error(diagnostics::Format("%s: %s", filename, SystemErrorString()));
        return std::string((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    }

    FILE* FOpenRead(std::string filename) {
        return fopen(filename.c_str(), "rb");
    }

    FILE* FOpenWrite(std::string filename) {
        return fopen(filename.c_str(), "wb");
    }

    std::vector<Float> ReadFloatFile(std::string filename) {
        FILE* f = FOpenRead(filename);
        if (f == nullptr) throw std::runtime_error(diagnostics::Format("%s: unable to open file", filename));

        int c;
        bool inNumber = false;
        char curNumber[32];
        int curNumberPos = 0;
        int lineNumber   = 1;
        std::vector<Float> values;
        while ((c = getc(f)) != EOF) {
            if (c == '\n') ++lineNumber;
            if (inNumber) {
                if (curNumberPos >= static_cast<int>(sizeof(curNumber))) throw std::runtime_error(diagnostics::Format("%s: overflowed number buffer at line %d", filename, lineNumber));
                if ((isdigit(c) != 0) || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                    curNumber[curNumberPos++] = c;
                } else {
                    curNumber[curNumberPos++] = '\0';
                    Float v;
                    if (!ParseFloatToken(curNumber, &v)) throw std::runtime_error(diagnostics::Format("%s: unable to parse float value \"%s\"", filename, curNumber));
                    values.push_back(v);
                    inNumber     = false;
                    curNumberPos = 0;
                }
            } else {
                if ((isdigit(c) != 0) || c == '.' || c == '-' || c == '+') {
                    inNumber                  = true;
                    curNumber[curNumberPos++] = c;
                } else if (c == '#') {
                    while ((c = getc(f)) != '\n' && c != EOF);
                    ++lineNumber;
                } else if (isspace(c) == 0) {
                    throw std::runtime_error(diagnostics::Format("%s: unexpected character \"%c\" found at line %d.", filename, c, lineNumber));
                }
            }
        }
        fclose(f);
        return values;
    }

    bool WriteFileContents(std::string filename, const std::string& contents) {
        std::ofstream out(filename, std::ios::binary);
        out << contents;
        out.close();
        if (!out.good()) {
            throw std::runtime_error(diagnostics::Format("%s: %s", filename, SystemErrorString()));
        }
        return true;
    }
} // namespace spectra
