#ifndef SPECTRA_SCENE_LOCATION_H
#define SPECTRA_SCENE_LOCATION_H

#include <string>
#include <string_view>

namespace spectra
{
    struct FileLoc
    {
        FileLoc() = default;

        FileLoc(std::string_view filename) : filename(filename)
        {
        }

        std::string_view filename;
        int line = 1;
        int column = 0;
    };

    [[nodiscard]] inline std::string FormatFileLocation(const FileLoc& location)
    {
        std::string result(location.filename.data(), location.filename.size());
        result += ":";
        result += std::to_string(location.line);
        result += ":";
        result += std::to_string(location.column);
        return result;
    }
} // namespace spectra

#endif // SPECTRA_SCENE_LOCATION_H
