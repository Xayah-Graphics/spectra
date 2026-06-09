export module spectra.pathtracer.pbrt.panel;

export import spectra.pathtracer;
export import spectra.scene;
import std;

export namespace spectra::pathtracer {
    class PbrtScenePanel final {
    public:
        enum class DisplayState {
            Checking,
            Candidate,
            Loaded,
            Unsupported,
            Invalid,
        };

        explicit PbrtScenePanel(std::shared_ptr<scene::PbrtSceneBrowserSession> browser_session);
        ~PbrtScenePanel() noexcept;

        PbrtScenePanel(const PbrtScenePanel& other)                = delete;
        PbrtScenePanel(PbrtScenePanel&& other) noexcept            = delete;
        PbrtScenePanel& operator=(const PbrtScenePanel& other)     = delete;
        PbrtScenePanel& operator=(PbrtScenePanel&& other) noexcept = delete;

        template <PathtracerHost Host>
        void attach(Host& host) {
            this->attach_sidebar_host([&host](PathtracerSidebarTab tab) { host.register_sidebar_tab(std::move(tab)); });
        }

        void detach() noexcept;

    private:
        struct ProbeSupportCacheEntry {
            std::string sceneId{};
            scene::SceneRevision revision{};
            PathtracerSceneSupportReport report{};
        };

        struct DocumentSupportCacheEntry {
            std::string sceneId{};
            scene::SceneRevision revision{};
            PathtracerSceneSupportReport report{};
        };

        struct SupportAnalysisRequestKey {
            std::string sceneId{};
            scene::SceneRevision revision{};
        };

        struct SupportAnalysisRequest {
            SupportAnalysisRequestKey key{};
            std::size_t sceneIndex{};
            scene::PbrtSceneProbeReport probe{};
        };

        void attach_sidebar_host(std::move_only_function<void(PathtracerSidebarTab)> register_tab);
        void start_support_analysis_workers();
        void stop_support_analysis_workers() noexcept;
        void stop_support_analysis_workers_if_idle() noexcept;
        void run_support_analysis_worker(std::stop_token stop_token);
        [[nodiscard]] bool has_support_analysis_work_locked() const;
        [[nodiscard]] static bool support_analysis_request_key_matches(const SupportAnalysisRequestKey& lhs, const SupportAnalysisRequestKey& rhs);
        [[nodiscard]] bool has_probe_support_cache_entry_locked(const SupportAnalysisRequestKey& key) const;
        [[nodiscard]] bool has_support_analysis_request_locked(const SupportAnalysisRequestKey& key) const;
        [[nodiscard]] PathtracerSceneSupportReport analyze_probe(const scene::PbrtSceneProbeReport& probe);
        [[nodiscard]] PathtracerSceneSupportReport analyze_document(const scene::PbrtSceneSnapshot& document);
        [[nodiscard]] DisplayState display_state(const scene::PbrtSceneCatalogEntry& entry, const std::optional<PathtracerSceneSupportReport>& report, bool loaded) const;
        [[nodiscard]] std::optional<PathtracerSceneSupportReport> cached_entry_report(const scene::PbrtSceneCatalogEntry& entry) const;
        void request_entry_report_analysis(std::size_t scene_index, const scene::PbrtSceneCatalogEntry& entry);
        void load_scene(std::size_t scene_index);
        void draw_scene_browser_panel();

        std::shared_ptr<scene::PbrtSceneBrowserSession> browser_session{};
        mutable std::mutex supportAnalysisMutex{};
        std::condition_variable_any supportAnalysisBackgroundCondition{};
        std::vector<ProbeSupportCacheEntry> probeSupportCache{};
        std::vector<DocumentSupportCacheEntry> documentSupportCache{};
        std::deque<SupportAnalysisRequest> supportAnalysisRequests{};
        std::vector<SupportAnalysisRequestKey> supportAnalysisRequestsInProgress{};
        bool attached{false};
        struct {
            std::array<char, 256> search{};
            int filter{};
            std::optional<std::string> load_error{};
        } sceneBrowser;
        std::vector<std::jthread> supportAnalysisBackgroundWorkers{};
    };
} // namespace spectra::pathtracer

