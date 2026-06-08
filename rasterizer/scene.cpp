module spectra.rasterizer.scene;

import std;

namespace spectra::rasterizer {
    void SceneEditBuilder::replaceDocument(SceneDocument document) {
        this->documentReplacement = std::move(document);
        this->dirty               = this->dirty | SceneDirtyFlags::Document | SceneDirtyFlags::RenderResources;
    }

    void SceneEditBuilder::replaceTimeline(SimulationTimeline timeline) {
        this->timelineReplacement = std::move(timeline);
        this->dirty               = this->dirty | SceneDirtyFlags::Timeline;
    }

    void SceneEditBuilder::replaceFrame(SceneFrameSnapshot frame) {
        this->frameReplacement = std::move(frame);
        this->dirty            = this->dirty | SceneDirtyFlags::Frame | SceneDirtyFlags::RenderResources;
    }

    SceneWorkspace::SceneWorkspace(SceneDocument document) {
        if (document.revision.value == 0) document.revision = SceneRevision{1};
        this->currentRevision = document.revision;
        this->currentDocument = std::make_shared<SceneDocument>(std::move(document));
        this->currentTimeline.framesPerSecond = this->currentDocument->framesPerSecond;
    }

    bool SceneWorkspace::loaded() const {
        return this->currentDocument != nullptr;
    }

    SceneRevision SceneWorkspace::revision() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
        return this->currentRevision;
    }

    std::shared_ptr<const SceneDocument> SceneWorkspace::document() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
        return this->currentDocument;
    }

    SimulationTimeline SceneWorkspace::timeline() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
        return this->currentTimeline;
    }

    std::optional<SceneFrameSnapshot> SceneWorkspace::frame() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Rasterizer scene workspace does not contain a loaded document");
        return this->currentTimeline.currentFrame;
    }

    SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentDocument == nullptr) throw std::runtime_error("Cannot edit an unloaded rasterizer scene workspace");
        if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit an empty rasterizer scene edit");

        const SceneRevision beforeRevision = this->currentRevision;
        this->currentRevision              = SceneRevision{beforeRevision.value + 1};
        if (edit.documentReplacement.has_value()) {
            SceneDocument next = std::move(*edit.documentReplacement);
            next.revision      = this->currentRevision;
            this->currentDocument = std::make_shared<SceneDocument>(std::move(next));
        }
        if (edit.timelineReplacement.has_value()) this->currentTimeline = std::move(*edit.timelineReplacement);
        if (edit.frameReplacement.has_value()) {
            edit.frameReplacement->revision = this->currentRevision;
            this->currentTimeline.cursor = edit.frameReplacement->cursor;
            this->currentTimeline.currentFrame = std::move(*edit.frameReplacement);
        }

        SceneEditBatch batch{
            .beforeRevision = beforeRevision,
            .afterRevision  = this->currentRevision,
            .dirty          = edit.dirty,
        };
        this->lastEdit = batch;
        return batch;
    }

    SceneEditBatch SceneWorkspace::changes_since(const SceneRevision revision) const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Cannot query rasterizer scene changes from an unloaded workspace");
        if (revision == this->currentRevision) {
            return SceneEditBatch{
                .beforeRevision = revision,
                .afterRevision  = revision,
                .dirty          = SceneDirtyFlags::None,
            };
        }
        if (revision.value == 0) return this->fullEdit(revision);
        if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
        throw std::runtime_error("Rasterizer scene edit history for the requested revision is unavailable");
    }

    SceneEditBatch SceneWorkspace::fullEdit(const SceneRevision before) const {
        return SceneEditBatch{
            .beforeRevision = before,
            .afterRevision  = this->currentRevision,
            .dirty          = SceneDirtyFlags::Document | SceneDirtyFlags::Timeline | SceneDirtyFlags::Frame | SceneDirtyFlags::RenderResources,
        };
    }
} // namespace spectra::rasterizer
