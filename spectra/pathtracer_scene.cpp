module;

#include <spectra/pathtracer/compiled_scene.cuh>

module xayah.spectra.pathtracer.scene;

import std;

namespace xayah {
    std::string_view PathtracerSceneTranslator::target_name() {
        return "Spectra Pathtracer";
    }

    spectra::scene::SceneTranslationTarget PathtracerSceneTranslator::translation_target() {
        return spectra::scene::SceneTranslationTarget{
            .rendererName = std::string{PathtracerSceneTranslator::target_name()},
            .analyze      = [](const spectra::scene::SceneSnapshot& document) { return PathtracerSceneTranslator::analyze(document); },
        };
    }

    spectra::scene::SceneTranslationReport PathtracerSceneTranslator::analyze(const spectra::scene::SceneSnapshot& document) {
        spectra::scene::SceneTranslationReport report = spectra::pathtracer::AnalyzePathtracerSceneSupport(document);
        if (report.target.empty()) report.target = std::string{PathtracerSceneTranslator::target_name()};
        return report;
    }

    PathtracerScene::PathtracerScene(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace) : source_workspace(std::move(source_workspace)) {
        if (this->source_workspace == nullptr) throw std::runtime_error("Spectra pathtracer scene requires a source document workspace");
        this->synchronize();
    }

    PathtracerScene::~PathtracerScene() noexcept = default;

    void PathtracerScene::synchronize() {
        const std::shared_ptr<const spectra::scene::SceneSnapshot> source_document = this->source_workspace->snapshot();
        if (source_document == nullptr) throw std::runtime_error("Spectra pathtracer source document workspace returned an empty document");
        if (this->translated_workspace.loaded() && this->source_revision == source_document->revision) return;

        const spectra::scene::SceneTranslationReport report = PathtracerSceneTranslator::analyze(*source_document);
        if (!report.supported) {
            std::string message = std::format("{} cannot translate scene \"{}\"", PathtracerSceneTranslator::target_name(), source_document->name);
            if (!report.diagnostics.empty()) message = std::format("{}: {}", message, report.diagnostics.front().message);
            throw std::runtime_error(message);
        }

        if (!this->translated_workspace.loaded()) {
            this->translated_workspace = spectra::scene::SceneWorkspace{*source_document};
        } else {
            spectra::scene::SceneEditBuilder edit{};
            edit.replaceSnapshot(*source_document, spectra::scene::SceneDirtyFlags::Snapshot);
            const spectra::scene::SceneEditBatch edit_batch = this->translated_workspace.commit(std::move(edit));
            if (edit_batch.afterRevision != source_document->revision) throw std::runtime_error("Spectra pathtracer scene translation revision is out of sync with the source document");
        }
        this->source_revision = source_document->revision;
    }

    std::shared_ptr<const spectra::scene::SceneSnapshot> PathtracerScene::snapshot() const {
        return this->translated_workspace.snapshot();
    }

    spectra::scene::SceneEditBatch PathtracerScene::changes_since(const spectra::scene::SceneRevision revision) const {
        return this->translated_workspace.changes_since(revision);
    }
} // namespace xayah
