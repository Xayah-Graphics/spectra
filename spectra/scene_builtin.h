#ifndef SPECTRA_SCENE_BUILTIN_H
#define SPECTRA_SCENE_BUILTIN_H

#include <string_view>

namespace spectra::scene {
    class SceneBuilder;
    struct SceneInfo;

    [[nodiscard]] const SceneInfo& BuiltinSceneInfoFor(std::string_view name);
    void BuildBuiltinScene(std::string_view name, SceneBuilder& builder);
} // namespace spectra::scene

#endif // SPECTRA_SCENE_BUILTIN_H
