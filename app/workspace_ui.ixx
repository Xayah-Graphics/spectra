export module spectra.app.workspace_ui;

import spectra.workspace;
import std;

namespace spectra::app {
    export enum class CaptureFormat : std::uint8_t {
        None,
        Png,
        Exr,
    };

    export struct WorkspaceUiActions {
        bool exit_application{};
        bool open_scene{};
        bool reload_scene{};
        bool save_scene{};
        bool save_scene_as{};
        bool show_axes{};
        CaptureFormat capture{CaptureFormat::None};
        std::array<std::array<float, 4>, 2> drag_regions{};
    };

    export struct WorkspaceUi {
        WorkspaceUi();
        ~WorkspaceUi();

        WorkspaceUi(const WorkspaceUi&) = delete;
        WorkspaceUi(WorkspaceUi&&) = delete;
        WorkspaceUi& operator=(const WorkspaceUi&) = delete;
        WorkspaceUi& operator=(WorkspaceUi&&) = delete;

        [[nodiscard]] WorkspaceUiActions draw(
            workspace::Workspace& workspace,
            std::uint64_t viewport_texture);

        std::string status{};
        bool status_error{};

    private:
        struct State;
        std::unique_ptr<State> state{};
    };
}
