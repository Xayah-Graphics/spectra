export module spectra.editor:diagnostics.settings;

import spectra.scene;
import spectra.diagnostics;
import std;

namespace spectra {
    export struct EditorSettings {
        explicit EditorSettings(std::filesystem::path path);

        void save() const;

        std::filesystem::path path{};
        SceneDiagnosticSettings diagnostics{};
    };
} // namespace spectra
