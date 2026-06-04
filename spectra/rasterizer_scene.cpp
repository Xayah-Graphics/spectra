module xayah.spectra.rasterizer.scene;

import std;

namespace xayah {
    std::string_view RasterizerSceneTranslator::target_name() {
        return "Spectra Rasterizer";
    }

    spectra::scene::SceneTranslationTarget RasterizerSceneTranslator::translation_target() {
        return spectra::scene::SceneTranslationTarget{
            .rendererName = std::string{RasterizerSceneTranslator::target_name()},
            .analyze      = [](const spectra::scene::SceneSnapshot& document) { return RasterizerSceneTranslator::analyze(document); },
        };
    }

    spectra::scene::SceneTranslationReport RasterizerSceneTranslator::analyze(const spectra::scene::SceneSnapshot& document) {
        spectra::scene::SceneTranslationReport report{.target = std::string{RasterizerSceneTranslator::target_name()}, .supported = false};
        if (document.name.empty()) {
            report.diagnostics.push_back(spectra::scene::SceneDiagnostic{
                .source  = spectra::scene::SceneSourceLocation{.filename = document.source, .line = 1, .column = 1},
                .message = "Rasterizer scene translation requires a named scene document",
            });
        } else {
            report.diagnostics.push_back(spectra::scene::SceneDiagnostic{
                .source  = spectra::scene::SceneSourceLocation{.filename = document.source, .line = 1, .column = 1},
                .message = "Rasterizer backend does not currently provide PBRT scene rasterization translation",
            });
        }
        return report;
    }

    RasterizerScene::RasterizerScene(std::shared_ptr<spectra::scene::SceneWorkspace> source_workspace) : source_workspace(std::move(source_workspace)) {
        if (this->source_workspace == nullptr) throw std::runtime_error("Spectra rasterizer scene requires a source document workspace");
    }

    RasterizerScene::~RasterizerScene() noexcept = default;

    void RasterizerScene::synchronize() {
        const std::shared_ptr<const spectra::scene::SceneSnapshot> source_document = this->source_workspace->snapshot();
        if (source_document == nullptr) throw std::runtime_error("Spectra rasterizer source document workspace returned an empty document");
        if (this->translated_document != nullptr && this->source_revision == source_document->revision) return;

        const spectra::scene::SceneTranslationReport report = RasterizerSceneTranslator::analyze(*source_document);
        if (!report.supported) {
            std::string message = std::format("{} cannot translate scene \"{}\"", RasterizerSceneTranslator::target_name(), source_document->name);
            if (!report.diagnostics.empty()) message = std::format("{}: {}", message, report.diagnostics.front().message);
            throw std::runtime_error(message);
        }

        this->translated_document = source_document;
        this->translated_info     = spectra::scene::DescribeScene(*source_document);
        this->source_revision     = source_document->revision;
    }

    std::shared_ptr<const spectra::scene::SceneSnapshot> RasterizerScene::snapshot() const {
        if (this->translated_document == nullptr) throw std::runtime_error("Spectra rasterizer scene document is not loaded");
        return this->translated_document;
    }

    const spectra::scene::SceneInfo& RasterizerScene::info() const {
        if (!this->translated_info.has_value()) throw std::runtime_error("Spectra rasterizer scene info is not loaded");
        return *this->translated_info;
    }
} // namespace xayah
