module xayah.scene;

import std;

extern "C++" {
namespace xayah::scene {
    namespace {
        [[nodiscard]] float OneFloatParameter(const std::vector<SceneParameter>& parameters, const std::string& name, const float fallback) {
            for (const SceneParameter& parameter : parameters) {
                if (parameter.type != "float" && parameter.type != "integer") continue;
                if (parameter.name != name) continue;
                return std::visit(
                    [fallback](const auto& values) -> float {
                        if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<float>>) {
                            if (!values.empty()) return values.front();
                        } else if constexpr (std::same_as<std::remove_cvref_t<decltype(values)>, std::vector<int>>) {
                            if (!values.empty()) return static_cast<float>(values.front());
                        }
                        return fallback;
                    },
                    parameter.values);
            }
            return fallback;
        }
    } // namespace

    void SceneEditBuilder::replaceSnapshot(SceneSnapshot snapshot, const SceneDirtyFlags dirty) {
        if (dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Scene snapshot replacement must use snapshot dirty state");
        this->replacement = std::move(snapshot);
        this->dirty       = dirty;
    }

    SceneWorkspace::SceneWorkspace(SceneSnapshot snapshot) {
        if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
        this->currentSnapshot = std::make_shared<SceneSnapshot>(std::move(snapshot));
    }

    bool SceneWorkspace::loaded() const {
        return this->currentSnapshot != nullptr;
    }

    std::shared_ptr<const SceneSnapshot> SceneWorkspace::snapshot() const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded snapshot");
        return this->currentSnapshot;
    }

    SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (!edit.replacement.has_value()) throw std::runtime_error("Cannot commit an empty scene edit");
        if (edit.dirty != SceneDirtyFlags::Snapshot) throw std::runtime_error("Scene edit commit must use snapshot dirty state");

        SceneSnapshot next                 = std::move(*edit.replacement);
        const SceneRevision beforeRevision = this->currentSnapshot->revision;
        next.revision                      = SceneRevision{beforeRevision.value + 1};
        this->currentSnapshot              = std::make_shared<SceneSnapshot>(std::move(next));

        SceneEditBatch batch = this->fullEdit(beforeRevision);
        batch.dirty          = edit.dirty;
        this->lastEdit       = batch;
        return batch;
    }

    SceneEditBatch SceneWorkspace::changes_since(const SceneRevision revision) const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query scene changes from an unloaded workspace");
        if (revision == this->currentSnapshot->revision) {
            return SceneEditBatch{
                .beforeRevision = revision,
                .afterRevision  = revision,
                .dirty          = SceneDirtyFlags::None,
            };
        }
        if (revision.value == 0) return this->fullEdit(revision);
        if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
        throw std::runtime_error("Scene edit history for the requested revision is unavailable");
    }

    SceneEditBatch SceneWorkspace::fullEdit(const SceneRevision before) const {
        return SceneEditBatch{
            .beforeRevision = before,
            .afterRevision  = this->currentSnapshot->revision,
            .dirty          = SceneDirtyFlags::Snapshot,
        };
    }

    SceneInfo DescribeScene(const SceneSnapshot& scene) {
        std::size_t definitionShapeCount     = 0;
        std::size_t definitionAreaLightCount = 0;
        for (const SceneObjectDefinition& definition : scene.objectDefinitions) {
            definitionShapeCount += definition.shapes.size();
            for (const SceneShape& shape : definition.shapes)
                if (shape.areaLight.has_value()) ++definitionAreaLightCount;
        }

        std::size_t areaLightCount = definitionAreaLightCount;
        for (const SceneShape& shape : scene.shapes)
            if (shape.areaLight.has_value()) ++areaLightCount;

        std::size_t infiniteLightCount = 0;
        for (const SceneLight& light : scene.lights)
            if (light.entity.type == "infinite") ++infiniteLightCount;

        float cameraFov = OneFloatParameter(scene.renderSettings.camera.parameters, "fov", scene.renderSettings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(cameraFov > 0.0f && cameraFov < 180.0f)) cameraFov = 45.0f;

        return SceneInfo{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = scene.renderSettings.camera.type,
            .sampler                 = scene.renderSettings.sampler.type,
            .integrator              = scene.renderSettings.integrator.type,
            .accelerator             = scene.renderSettings.accelerator.type,
            .shape_count             = scene.shapes.size() + definitionShapeCount,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = areaLightCount,
            .infinite_light_count    = infiniteLightCount,
            .object_definition_count = scene.objectDefinitions.size(),
            .object_instance_count   = scene.objectInstances.size(),
            .camera_fov_degrees      = cameraFov,
        };
    }
} // namespace xayah::scene
}
