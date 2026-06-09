export module spectra.pathtracer.pbrt.library;

export import spectra.pathtracer;
export import spectra.scene.pbrt;
import std;

export namespace spectra::pathtracer {
    class PbrtSceneLibrary final {
    public:
        enum class DisplayState {
            Checking,
            Candidate,
            Loaded,
            Unsupported,
            Invalid,
        };

        explicit PbrtSceneLibrary(std::string initial_scene_id);
        ~PbrtSceneLibrary() noexcept;

        PbrtSceneLibrary(const PbrtSceneLibrary& other)                = delete;
        PbrtSceneLibrary(PbrtSceneLibrary&& other) noexcept            = delete;
        PbrtSceneLibrary& operator=(const PbrtSceneLibrary& other)     = delete;
        PbrtSceneLibrary& operator=(PbrtSceneLibrary&& other) noexcept = delete;

        template <PathtracerHost Host>
        void attach(Host& host) {
            this->attach_sidebar_host([&host](PathtracerSidebarTab tab) { host.register_sidebar_tab(std::move(tab)); });
        }

        void detach() noexcept;

        [[nodiscard]] std::shared_ptr<scene::PbrtSceneWorkspace> scene_workspace() const;

    private:
        struct ProbeTranslationCacheEntry {
            std::string sceneId{};
            scene::SceneRevision revision{};
            scene::PbrtSceneTranslationReport report{};
        };

        struct DocumentTranslationCacheEntry {
            std::string sceneId{};
            scene::SceneRevision revision{};
            scene::PbrtSceneTranslationReport report{};
        };

        struct TranslationRequestKey {
            std::string sceneId{};
            scene::SceneRevision revision{};
        };

        struct TranslationRequest {
            TranslationRequestKey key{};
            std::size_t sceneIndex{};
            scene::PbrtSceneProbeReport probe{};
        };

        void attach_sidebar_host(std::move_only_function<void(PathtracerSidebarTab)> register_tab);
        void start_translation_background_workers();
        void stop_translation_background_workers() noexcept;
        void stop_translation_background_workers_if_idle() noexcept;
        void run_translation_background_worker(std::stop_token stop_token);
        [[nodiscard]] bool has_translation_background_work_locked() const;
        [[nodiscard]] static bool translation_request_key_matches(const TranslationRequestKey& lhs, const TranslationRequestKey& rhs);
        [[nodiscard]] bool has_probe_translation_cache_entry_locked(const TranslationRequestKey& key) const;
        [[nodiscard]] bool has_translation_request_locked(const TranslationRequestKey& key) const;
        [[nodiscard]] scene::PbrtSceneTranslationReport analyze_probe(const scene::PbrtSceneProbeReport& probe);
        [[nodiscard]] scene::PbrtSceneTranslationReport analyze_document(const scene::PbrtSceneSnapshot& document);
        [[nodiscard]] DisplayState display_state(const scene::PbrtSceneCatalogEntry& entry, const std::optional<scene::PbrtSceneTranslationReport>& report, bool loaded) const;
        [[nodiscard]] std::optional<scene::PbrtSceneTranslationReport> cached_entry_report(const scene::PbrtSceneCatalogEntry& entry) const;
        void request_entry_report_analysis(std::size_t scene_index, const scene::PbrtSceneCatalogEntry& entry);
        void commit_document(std::size_t scene_index, scene::PbrtSceneSnapshot document);
        void load_scene(std::size_t scene_index);
        void draw_scene_library_window();

        std::shared_ptr<scene::PbrtCatalogSession> catalog_session{};
        std::string initial_scene_id{};
        mutable std::mutex translation_mutex{};
        std::condition_variable_any translation_background_condition{};
        std::vector<ProbeTranslationCacheEntry> probe_translation_cache{};
        std::vector<DocumentTranslationCacheEntry> document_translation_cache{};
        std::deque<TranslationRequest> translation_requests{};
        std::vector<TranslationRequestKey> translation_requests_in_progress{};
        bool attached{false};
        struct {
            std::array<char, 256> search{};
            int filter{};
            std::size_t selected_index{};
            std::optional<std::string> load_error{};
        } scene_library;
        std::vector<std::jthread> translation_background_workers{};
    };
} // namespace spectra::pathtracer
