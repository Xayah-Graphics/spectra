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

        void attach_panel_host(std::move_only_function<void(SpectraPanel)> register_panel);
        void start_scene_catalog_validation();
        void validate_scene_catalog(std::stop_token stop_token);
        void refresh_scene_catalog_counts();
        void clear_translation_cache_for_scene(std::string_view scene_id);
        void ensure_translation_target_exists(std::string_view renderer_name) const;
        [[nodiscard]] spectra::scene::SceneTranslationReport analyze_document(std::string_view renderer_name, const spectra::scene::SceneSnapshot& document);
        [[nodiscard]] SpectraRendererAvailability availability_from_report(std::string_view renderer_name, const spectra::scene::SceneTranslationReport& report) const;
        [[nodiscard]] DisplayState display_state(const spectra::scene::SceneCatalogEntry& entry, const std::optional<spectra::scene::SceneTranslationReport>& report) const;
        [[nodiscard]] std::optional<spectra::scene::SceneTranslationReport> cached_entry_report(const spectra::scene::SceneCatalogEntry& entry);
        void commit_document(std::size_t scene_index, const spectra::scene::SceneSnapshot& document);
        void load_scene(std::size_t scene_index);
        void draw_scene_library_window();

        std::shared_ptr<spectra::scene::SceneWorkspace> workspace{};
        mutable std::mutex scene_catalog_mutex{};
        spectra::scene::SceneCatalog scene_catalog{};
        std::vector<spectra::scene::SceneTranslationTarget> translation_targets{};
        std::vector<TranslationCacheEntry> translation_cache{};
        std::string active_renderer{};
        std::size_t active_scene_index{};
        bool attached{false};
        struct {
            std::array<char, 256> search{};
            int filter{};
            std::size_t selected_index{};
            std::optional<std::string> load_error{};
        } scene_library;
        std::jthread scene_catalog_validator{};
    };
} // namespace xayah
