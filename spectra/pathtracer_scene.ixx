export module xayah.spectra.pathtracer.scene;

export import spectra.scene;
import std;

export namespace xayah {
    class PathtracerSceneTranslator final {
    public:
        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] static spectra::scene::SceneTranslationTarget translation_target();
        [[nodiscard]] static spectra::scene::SceneTranslationReport analyze(const spectra::scene::SceneSnapshot& document);
    };

    class PathtracerScene final {
    public:
        explicit PathtracerScene(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace);
        ~PathtracerScene() noexcept;

        PathtracerScene(const PathtracerScene& other)                = delete;
        PathtracerScene(PathtracerScene&& other) noexcept            = delete;
        PathtracerScene& operator=(const PathtracerScene& other)     = delete;
        PathtracerScene& operator=(PathtracerScene&& other) noexcept = delete;

        void synchronize();
        [[nodiscard]] std::shared_ptr<const spectra::scene::SceneSnapshot> snapshot() const;
        [[nodiscard]] spectra::scene::SceneEditBatch changes_since(spectra::scene::SceneRevision revision) const;

    private:
        std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace{};
        spectra::scene::SceneWorkspace translated_workspace{};
        spectra::scene::SceneRevision source_revision{};
    };
} // namespace xayah
