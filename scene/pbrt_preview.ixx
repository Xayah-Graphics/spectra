export module spectra.scene.pbrt_preview;

export import spectra.scene.pbrt;
import std;

export namespace spectra::scene {
    [[nodiscard]] SceneDocument MakeSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene);
    [[nodiscard]] SceneDocument LoadSceneDocumentFromPbrt(std::string_view scene_id);
}
