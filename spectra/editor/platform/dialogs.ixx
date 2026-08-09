export module spectra.editor:platform.dialogs;

import :platform.window;
import std;

namespace spectra {
    export enum class SceneReplacementDecision : std::uint8_t {
        Save,
        Discard,
        Cancel,
    };

    export struct EditorDialogs {
        explicit EditorDialogs(WindowPlatform& platform);
        ~EditorDialogs();

        EditorDialogs(const EditorDialogs&)            = delete;
        EditorDialogs(EditorDialogs&&)                 = delete;
        EditorDialogs& operator=(const EditorDialogs&) = delete;
        EditorDialogs& operator=(EditorDialogs&&)      = delete;

        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_file();
        [[nodiscard]] std::optional<std::filesystem::path> choose_scene_save_path(const std::filesystem::path& current_path, bool frozen_scene = false);
        [[nodiscard]] SceneReplacementDecision confirm_scene_replacement() const noexcept;

        WindowPlatform& platform;
    };
} // namespace spectra
