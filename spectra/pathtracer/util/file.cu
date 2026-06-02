#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <spectra/pathtracer/core/diagnostics.cuh>
#include <spectra/pathtracer/util/check.cuh>
#include <spectra/pathtracer/util/file.cuh>
#include <spectra/pathtracer/util/string.cuh>
#include <system_error>
#ifdef SPECTRA_IS_WINDOWS
#include <windows.h>
#endif
#ifndef SPECTRA_IS_WINDOWS
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace spectra {
    [[nodiscard]] static std::string SystemErrorString() {
#ifdef SPECTRA_IS_WINDOWS
        int error  = GetLastError();
        char* text = nullptr;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), reinterpret_cast<LPSTR>(&text), 0, nullptr);
        std::string message = text != nullptr ? text : "Unknown Windows system error";
        if (text != nullptr) LocalFree(text);
#else
        int error           = errno;
        std::string message = strerror(error);
#endif
        message += " (";
        message += std::to_string(error);
        message += ")";
        return message;
    }

    static std::filesystem::path PathFromUTF8(const std::string& filename) {
#ifdef SPECTRA_IS_WINDOWS
        return std::filesystem::path(WStringFromUTF8(filename));
#else
        return std::filesystem::path(filename);
#endif
    }

    static std::string PathToUTF8(const std::filesystem::path& path) {
#ifdef SPECTRA_IS_WINDOWS
        return UTF8FromWString(path.wstring());
#else
        return path.string();
#endif
    }

    static std::string ExtensionFromPath(const std::filesystem::path& path) {
        std::string filename = PathToUTF8(path.filename());
        size_t pos           = filename.find_last_of('.');
        if (pos == std::string::npos) return "";
        return filename.substr(pos + 1);
    }

    static std::filesystem::path searchDirectory;

    void SetSearchDirectory(std::string filename) {
        std::filesystem::path path = PathFromUTF8(filename);
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) path = path.parent_path();
        searchDirectory = path;
    }

    static bool IsAbsolutePath(std::string filename) {
        if (filename.empty()) return false;
        return PathFromUTF8(filename).is_absolute();
    }

    bool HasExtension(std::string filename, std::string e) {
        std::string ext = e;
        if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);

        std::string filenameExtension = ExtensionFromPath(PathFromUTF8(filename));
        if (ext.size() > filenameExtension.size()) return false;
        return std::equal(ext.rbegin(), ext.rend(), filenameExtension.rbegin(), [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
    }

    std::string ResolveFilename(std::string filename) {
        if (searchDirectory.empty() || filename.empty() || IsAbsolutePath(filename)) return filename;

        std::filesystem::path filepath = searchDirectory / PathFromUTF8(filename);
        std::error_code ec;
        if (std::filesystem::exists(filepath, ec) && !ec) {
            std::filesystem::path absolute = std::filesystem::canonical(filepath, ec);
            if (!ec) return PathToUTF8(absolute);
        }
        return filename;
    }

    bool FileExists(std::string filename) {
#ifdef SPECTRA_IS_WINDOWS
        std::ifstream ifs(WStringFromUTF8(filename).c_str());
#else
        std::ifstream ifs(filename);
#endif
        return (bool) ifs;
    }

    std::string ReadFileContents(std::string filename) {
#ifdef SPECTRA_IS_WINDOWS
        std::ifstream ifs(WStringFromUTF8(filename).c_str(), std::ios::binary);
        if (!ifs) throw std::runtime_error(diagnostics::Format("%s: %s", filename, SystemErrorString()));
        return std::string((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
#else
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd == -1) throw std::runtime_error(spectra::diagnostics::Format("%s: %s", filename, SystemErrorString()));

        struct stat stat;
        if (fstat(fd, &stat) != 0) throw std::runtime_error(spectra::diagnostics::Format("%s: %s", filename, SystemErrorString()));

        std::string contents(stat.st_size, '\0');
        if (read(fd, contents.data(), stat.st_size) == -1) throw std::runtime_error(spectra::diagnostics::Format("%s: %s", filename, SystemErrorString()));

        close(fd);
        return contents;
#endif
    }

    FILE* FOpenRead(std::string filename) {
#ifdef SPECTRA_IS_WINDOWS
        return _wfopen(WStringFromUTF8(filename).c_str(), L"rb");
#else
        return fopen(filename.c_str(), "rb");
#endif
    }

    FILE* FOpenWrite(std::string filename) {
#ifdef SPECTRA_IS_WINDOWS
        return _wfopen(WStringFromUTF8(filename).c_str(), L"wb");
#else
        return fopen(filename.c_str(), "wb");
#endif
    }

    std::vector<Float> ReadFloatFile(std::string filename) {
        FILE* f = FOpenRead(filename);
        if (f == nullptr) {
            throw std::runtime_error(diagnostics::Format("%s: unable to open file", filename));
            return {};
        }

        int c;
        bool inNumber = false;
        char curNumber[32];
        int curNumberPos = 0;
        int lineNumber   = 1;
        std::vector<Float> values;
        while ((c = getc(f)) != EOF) {
            if (c == '\n') ++lineNumber;
            if (inNumber) {
                if (curNumberPos >= (int) sizeof(curNumber))
                    SPECTRA_FATAL("Overflowed buffer for parsing number in file: %s at "
                                  "line %d",
                        filename, lineNumber);
                // Note: this is not very robust, and would accept something
                // like 0.0.0.0eeee-+--2 as a valid number.
                if ((isdigit(c) != 0) || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                    CHECK_LT(curNumberPos, sizeof(curNumber));
                    curNumber[curNumberPos++] = c;
                } else {
                    curNumber[curNumberPos++] = '\0';
                    Float v;
                    if (!Atof(curNumber, &v)) throw std::runtime_error(diagnostics::Format("%s: unable to parse float value \"%s\"", filename, curNumber));
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
                    return {};
                }
            }
        }
        fclose(f);
        return values;
    }

    bool WriteFileContents(std::string filename, const std::string& contents) {
#ifdef SPECTRA_IS_WINDOWS
        std::ofstream out(WStringFromUTF8(filename).c_str(), std::ios::binary);
#else
        std::ofstream out(filename, std::ios::binary);
#endif
        out << contents;
        out.close();
        if (!out.good()) {
            throw std::runtime_error(diagnostics::Format("%s: %s", filename, SystemErrorString()));
            return false;
        }
        return true;
    }
} // namespace spectra
