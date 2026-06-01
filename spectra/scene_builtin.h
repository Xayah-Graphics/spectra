#ifndef SPECTRA_SCENE_BUILTIN_H
#define SPECTRA_SCENE_BUILTIN_H

#include <spectra/scene.h>
#include <string_view>

namespace spectra::scene {
    [[nodiscard]] Scene BuildBuiltinScene(std::string_view name);
} // namespace spectra::scene

#endif // SPECTRA_SCENE_BUILTIN_H
