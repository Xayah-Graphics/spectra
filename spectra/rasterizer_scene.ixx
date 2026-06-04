export module xayah.spectra.rasterizer.scene;

export import spectra.scene;
import std;

export namespace xayah {
    class RasterizerSceneTranslator final {
    public:
        [[nodiscard]] static std::string_view target_name();
        [[nodiscard]] static spectra::scene::SceneTranslationTarget translation_target();
        [[nodiscard]] static spectra::scene::SceneTranslationReport analyze(const spectra::scene::SceneSnapshot& document);
    };

    class RasterizerScene final {
    public:
        explicit RasterizerScene(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace);
        ~RasterizerScene() noexcept;

        RasterizerScene(const RasterizerScene& other)                = delete;
        RasterizerScene(RasterizerScene&& other) noexcept            = delete;
        RasterizerScene& operator=(const RasterizerScene& other)     = delete;
        RasterizerScene& operator=(RasterizerScene&& other) noexcept = delete;

        void synchronize();
        [[nodiscard]] std::shared_ptr<const spectra::scene::SceneSnapshot> snapshot() const;
        [[nodiscard]] const spectra::scene::SceneInfo& info() const;

    private:
        std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace{};
        std::shared_ptr<const spectra::scene::SceneSnapshot> translated_document{};
        std::optional<spectra::scene::SceneInfo> translated_info{};
        spectra::scene::SceneRevision source_revision{};
    };
} // namespace xayah
