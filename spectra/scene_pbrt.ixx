export module spectra.scene.pbrt;

export import spectra.scene;
import std;

export namespace spectra::scene {
    enum class PbrtSceneCatalogEntryState {
        Pending,
        Candidate,
        NonScene,
        Invalid,
    };

    struct PbrtSceneCatalogEntry {
        std::string id{};
        std::string displayName{};
        std::string group{};
        std::filesystem::path relativePath{};
        std::filesystem::path sourcePath{};
        PbrtSceneCatalogEntryState state{PbrtSceneCatalogEntryState::Pending};
        SceneRevision revision{};
        std::optional<SceneProbeReport> probe{};
        std::vector<SceneDiagnostic> issues{};
    };

    struct PbrtSceneCatalog {
        std::filesystem::path root{};
        std::vector<PbrtSceneCatalogEntry> entries{};
        std::size_t pending_count{};
        std::size_t candidate_count{};
        std::size_t non_scene_count{};
        std::size_t invalid_count{};
    };

    [[nodiscard]] PbrtSceneCatalog DiscoverPbrtSceneCatalog();
    [[nodiscard]] SceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    [[nodiscard]] SceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    [[nodiscard]] SceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    [[nodiscard]] SceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry);
    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    [[nodiscard]] SceneWorkspace BuildPbrtScene(std::string_view name);
} // namespace spectra::scene
