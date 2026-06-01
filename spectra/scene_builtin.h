#ifndef SPECTRA_SCENE_BUILTIN_H
#define SPECTRA_SCENE_BUILTIN_H

#include <memory>
#include <optional>
#include <spectra/scene.h>
#include <string_view>

namespace spectra::scene {
    [[nodiscard]] SceneInfo BuiltinSceneInfoFor(std::string_view name);
    [[nodiscard]] std::unique_ptr<Scene> BuildBuiltinScene(std::string_view name, std::optional<Point2i> filmResolutionOverride = {});
} // namespace spectra::scene

#endif // SPECTRA_SCENE_BUILTIN_H
