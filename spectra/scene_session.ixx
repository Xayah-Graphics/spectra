export module xayah.spectra.scene_session;

export import spectra.scene;
export import xayah.spectra;
import std;

export namespace xayah {
    template <typename Host>
    concept SpectraSceneSessionHost = SpectraSceneHost<Host>;

    class SpectraSceneSession final {
    public:
        enum class DisplayState {
            Checking,
            Ready,
            Unsupported,
            Invalid,
        };

        SpectraSceneSession();
        ~SpectraSceneSession() noexcept;

        SpectraSceneSession(const SpectraSceneSession& other)                = delete;
        SpectraSceneSession(SpectraSceneSession&& other) noexcept            = delete;
        SpectraSceneSession& operator=(const SpectraSceneSession& other)     = delete;
        SpectraSceneSession& operator=(SpectraSceneSession&& other) noexcept = delete;

        template <SpectraSceneSessionHost Host>
        void attach(Host& host) {
            this->attach_panel_host([&host](SpectraPanel panel) { host.register_panel(std::move(panel)); });
        }

        template <typename Host>
        void before_imgui_shutdown(Host&) noexcept {
            this->before_imgui_shutdown();
        }

        template <typename Host>
        void after_imgui_created(Host&) {
            this->after_imgui_created();
        }

        void detach() noexcept;
        void before_imgui_shutdown() noexcept;
        void after_imgui_created();

        [[nodiscard]] std::shared_ptr<spectra::scene::SceneWorkspace> document_workspace() const;
        void register_translation_target(spectra::scene::SceneTranslationTarget target);
        void set_active_renderer(std::string_view renderer_name);
        void load_first_supported_scene(std::string_view renderer_name);
        [[nodiscard]] SpectraRendererAvailability renderer_availability(std::string_view renderer_name);

    private:
        struct TranslationCacheEntry {
            std::string rendererName{};
            std::string sceneId{};
            spectra::scene::SceneRevision revision{};
            spectra::scene::SceneTranslationReport report{};
        };

        struct TranslationRequestKey {
            std::string rendererName{};
            std::string sceneId{};
            spectra::scene::SceneRevision revision{};
        };

        struct TranslationRequest {
            TranslationRequestKey key{};
            std::size_t sceneIndex{};
            std::shared_ptr<const spectra::scene::SceneSnapshot> document{};
        };

        void attach_panel_host(std::move_only_function<void(SpectraPanel)> register_panel);
        void start_scene_background_workers();
        void run_scene_background_worker(std::stop_token stop_token);
        void refresh_scene_catalog_counts();
        void clear_translation_cache_for_scene(std::string_view scene_id);
        void ensure_translation_target_exists(std::string_view renderer_name) const;
        [[nodiscard]] bool has_scene_background_work_locked() const;
        [[nodiscard]] std::optional<std::size_t> next_catalog_validation_index_locked() const;
        [[nodiscard]] static bool translation_request_key_matches(const TranslationRequestKey& lhs, const TranslationRequestKey& rhs);
        [[nodiscard]] bool has_translation_cache_entry_locked(const TranslationRequestKey& key) const;
        [[nodiscard]] bool has_translation_request_locked(const TranslationRequestKey& key) const;
        [[nodiscard]] spectra::scene::SceneTranslationReport analyze_document(std::string_view renderer_name, const spectra::scene::SceneSnapshot& document);
        [[nodiscard]] SpectraRendererAvailability availability_from_report(std::string_view renderer_name, const spectra::scene::SceneTranslationReport& report) const;
        [[nodiscard]] DisplayState display_state(const spectra::scene::SceneCatalogEntry& entry, const std::optional<spectra::scene::SceneTranslationReport>& report, bool renderer_report_required) const;
        [[nodiscard]] std::optional<spectra::scene::SceneTranslationReport> cached_entry_report(std::string_view renderer_name, const spectra::scene::SceneCatalogEntry& entry) const;
        void request_entry_report_analysis(std::string_view renderer_name, std::size_t scene_index, const spectra::scene::SceneCatalogEntry& entry);
        void commit_document(std::size_t scene_index, const spectra::scene::SceneSnapshot& document);
        void load_scene(std::size_t scene_index);
        void draw_scene_library_window();

        std::shared_ptr<spectra::scene::SceneWorkspace> workspace{};
        mutable std::mutex scene_catalog_mutex{};
        std::condition_variable_any scene_background_condition{};
        spectra::scene::SceneCatalog scene_catalog{};
        std::vector<bool> scene_catalog_validation_claimed{};
        std::vector<spectra::scene::SceneTranslationTarget> translation_targets{};
        std::vector<TranslationCacheEntry> translation_cache{};
        std::deque<TranslationRequest> translation_requests{};
        std::vector<TranslationRequestKey> translation_requests_in_progress{};
        std::string active_renderer{};
        std::size_t active_scene_index{};
        bool attached{false};
        struct {
            std::array<char, 256> search{};
            int filter{};
            std::size_t selected_index{};
            std::optional<std::string> load_error{};
        } scene_library;
        std::vector<std::jthread> scene_background_workers{};
    };
} // namespace xayah
