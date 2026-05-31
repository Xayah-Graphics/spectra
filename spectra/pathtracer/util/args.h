#ifndef SPECTRA_PATHTRACER_UTIL_ARGS_H
#define SPECTRA_PATHTRACER_UTIL_ARGS_H

#include <spectra/pathtracer/util/float.h>

#include <string>
#include <vector>

namespace spectra
{
    std::vector<std::string> GetCommandLineArguments(char* argv[]);
} // namespace spectra

#endif  // SPECTRA_PATHTRACER_UTIL_ARGS_H
