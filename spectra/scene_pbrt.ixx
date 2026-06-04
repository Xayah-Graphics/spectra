export module xayah.scene.pbrt;

export import xayah.scene;
import std;

export namespace xayah::scene {
    enum class PbrtSceneCatalogEntryState {
        Pending,
        Ready,
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
        std::optional<SceneInfo> info{};
        std::vector<SceneDiagnostic> issues{};
    };

    struct PbrtSceneCatalog {
        std::filesystem::path root{};
        std::vector<PbrtSceneCatalogEntry> entries{};
        std::size_t pending_count{};
        std::size_t ready_count{};
        std::size_t invalid_count{};
    };

    [[nodiscard]] PbrtSceneCatalog DiscoverPbrtSceneCatalog();
    [[nodiscard]] SceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry);
    [[nodiscard]] SceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    void ValidatePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry);
    void ValidatePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry, std::stop_token stop_token);
    [[nodiscard]] SceneWorkspace BuildPbrtScene(std::string_view name);
} // namespace xayah::scene
