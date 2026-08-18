export module spectra.editor.platform.dialogs;

import spectra.editor.platform.window;
import std;

namespace spectra::editor {
    export enum class SceneReplacementDecision : std::uint8_t {
        Save,
        Discard,
        Cancel,
    };

    export struct Dialogs {
        explicit Dialogs(WindowPlatform& platform);
        ~Dialogs();

        Dialogs(const Dialogs&)            = delete;
        Dialogs(Dialogs&&)                 = delete;
        Dialogs& operator=(const Dialogs&) = delete;
        Dialogs& operator=(Dialogs&&)      = delete;

        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_file();
        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_save_path(const std::filesystem::path& current_path);
        [[nodiscard]] SceneReplacementDecision confirm_scene_replacement() const noexcept;

        WindowPlatform& platform;
    };
} // namespace spectra::editor
