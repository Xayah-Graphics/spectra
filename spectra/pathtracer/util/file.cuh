#ifndef SPECTRA_PATHTRACER_UTIL_FILE_H
#define SPECTRA_PATHTRACER_UTIL_FILE_H

#include <spectra/pathtracer/util/float.cuh>
#include <string>
#include <vector>

namespace spectra {
    // File and Filename Function Declarations
    std::string ReadFileContents(std::string filename);
    bool WriteFileContents(std::string filename, const std::string& contents);

    std::vector<Float> ReadFloatFile(std::string filename);

    bool FileExists(std::string filename);

    std::string ResolveFilename(std::string filename);
    void SetSearchDirectory(std::string filename);

    bool HasExtension(std::string filename, std::string ext);

    FILE* FOpenRead(std::string filename);
    FILE* FOpenWrite(std::string filename);
} // namespace spectra

#endif // SPECTRA_PATHTRACER_UTIL_FILE_H
