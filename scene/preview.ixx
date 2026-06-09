export module spectra.scene.preview;

export import spectra.scene.pbrt;
import std;

namespace spectra::scene {
    export [[nodiscard]] SceneDocument MakePreviewSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene);
    export [[nodiscard]] SceneDocument LoadPreviewSceneDocumentFromPbrt(std::string_view scene_id);
}
