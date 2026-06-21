export module spectra.scene.ui;

export import spectra;
export import spectra.scene;
import std;

namespace spectra::scene {
    export class SceneUi final {
    public:
        SceneUi();
        SceneUi(const SceneUi& other) = delete;
        SceneUi(SceneUi&& other) noexcept;
        SceneUi& operator=(const SceneUi& other) = delete;
        SceneUi& operator=(SceneUi&& other) noexcept;
        ~SceneUi() noexcept;

        [[nodiscard]] std::shared_ptr<Scene> scene() const;
        [[nodiscard]] std::shared_ptr<CameraWorkspace> camera_workspace() const;
        void register_to(Spectra& application);
        void open_startup_file(Spectra& application, const std::optional<std::string>& initial_scene_path);

    private:
        struct State;
        std::shared_ptr<State> state{};
    };
} // namespace spectra::scene
