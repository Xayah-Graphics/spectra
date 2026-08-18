export module spectra.editor.ui.simulation;

import spectra.editor.document;
import spectra.simulation.runtime;
import spectra.scene;
import std;

namespace spectra::editor {
    export struct SimulationPanel {
        SimulationPanel(Document& document, simulation::Runtime& simulation) noexcept;

        void reset();
        [[nodiscard]] bool synchronize();
        void draw();

        std::size_t selected_system{};
        std::string notification{};
        bool notification_error{};
        bool rebuild_rendering{};

    private:
        Document& document;
        simulation::Runtime& simulation;
        std::uint64_t observed_revision{};
        std::vector<scene::SimulationParameterSetting> parameter_drafts{};
        bool reset_pending{};
        bool recreate_pending{};

        void apply_parameters(std::vector<scene::SimulationParameterSetting> parameters, bool reset_simulation);
    };
} // namespace spectra::editor
