export module spectra.editor.ui.inspector;

import spectra.editor.document;
import spectra.editor.viewport;
import spectra.render.display;
import spectra.scene;

namespace spectra::editor {
    export struct InspectorPanel {
        InspectorPanel(Document& document, Viewport& viewport) noexcept;

        void draw(scene::EntityReference entity);

    private:
        Document& document;
        Viewport& viewport;

        void draw_particle(const scene::ParticleSet& particles);
        void draw_volume(const scene::Volume& volume);
    };
} // namespace spectra::editor
