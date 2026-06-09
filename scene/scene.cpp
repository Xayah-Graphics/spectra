module spectra.scene;

import std;

namespace spectra::scene {
    namespace {
        [[nodiscard]] bool finite_vector(const Vector3 vector) {
            return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
        }

        [[nodiscard]] Vector3 operator-(const Vector3 lhs, const Vector3 rhs) {
            return Vector3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
        }

        [[nodiscard]] Vector3 cross(const Vector3 lhs, const Vector3 rhs) {
            return Vector3{
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x,
            };
        }

        [[nodiscard]] float length_squared(const Vector3 vector) {
            return vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
        }

        void validate_scene_id(const std::string_view scene_id) {
            if (scene_id.empty()) throw std::runtime_error("Scene camera workspace requires a non-empty scene id");
        }

        void validate_camera_state(const SceneCameraState& state) {
            if (!finite_vector(state.eye)) throw std::runtime_error("Scene camera eye must be finite");
            if (!finite_vector(state.target)) throw std::runtime_error("Scene camera target must be finite");
            if (!finite_vector(state.up)) throw std::runtime_error("Scene camera up vector must be finite");
            const Vector3 view = state.target - state.eye;
            if (!(length_squared(view) > 1.0e-12f)) throw std::runtime_error("Scene camera eye and target must not overlap");
            if (!(length_squared(state.up) > 1.0e-12f)) throw std::runtime_error("Scene camera up vector must not be zero");
            if (!(length_squared(cross(view, state.up)) > 1.0e-12f)) throw std::runtime_error("Scene camera up vector must not be parallel to the view direction");
            if (!std::isfinite(state.verticalFovDegrees) || !(state.verticalFovDegrees > 0.0f) || !(state.verticalFovDegrees < 180.0f)) throw std::runtime_error("Scene camera vertical FOV must be inside (0, 180)");
        }
    } // namespace

    void SceneCameraWorkspace::ensure_camera(std::string scene_id, SceneCameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
        std::scoped_lock lock{this->mutex};
        if (this->cameras.contains(scene_id)) return;
        this->cameras.emplace(std::move(scene_id), SceneCameraSnapshot{
                                                       .revision = SceneRevision{1},
                                                       .state    = std::move(state),
                                                   });
    }

    SceneCameraSnapshot SceneCameraWorkspace::snapshot(const std::string_view scene_id) const {
        validate_scene_id(scene_id);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, SceneCameraSnapshot>::const_iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        return found->second;
    }

    SceneCameraSnapshot SceneCameraWorkspace::commit(const std::string_view scene_id, SceneCameraState state) {
        validate_scene_id(scene_id);
        validate_camera_state(state);
        std::scoped_lock lock{this->mutex};
        const std::map<std::string, SceneCameraSnapshot>::iterator found = this->cameras.find(std::string{scene_id});
        if (found == this->cameras.end()) throw std::runtime_error(std::format("Scene camera session \"{}\" does not exist", scene_id));
        found->second = SceneCameraSnapshot{
            .revision = SceneRevision{found->second.revision.value + 1u},
            .state    = std::move(state),
        };
        return found->second;
    }

    void SceneEditBuilder::replaceDocument(SceneDocument document) {
        this->documentReplacement = std::move(document);
        this->dirty               = this->dirty | SceneDirtyFlags::Document;
    }

    void SceneEditBuilder::replaceTimeline(SimulationTimeline timeline) {
        this->timelineReplacement = std::move(timeline);
        this->dirty               = this->dirty | SceneDirtyFlags::Timeline;
    }

    void SceneEditBuilder::replaceFrame(SceneFrameSnapshot frame) {
        this->frameReplacement = std::move(frame);
        this->dirty            = this->dirty | SceneDirtyFlags::Frame;
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
        if (this->currentDocument == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded document");
        return this->currentRevision;
    }

    std::shared_ptr<const SceneDocument> SceneWorkspace::document() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded document");
        return this->currentDocument;
    }

    SimulationTimeline SceneWorkspace::timeline() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded document");
        return this->currentTimeline;
    }

    std::optional<SceneFrameSnapshot> SceneWorkspace::frame() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded document");
        return this->currentTimeline.currentFrame;
    }

    SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentDocument == nullptr) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit an empty scene edit");

        this->currentRevision = SceneRevision{this->currentRevision.value + 1};
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

        return SceneEditBatch{
            .dirty = edit.dirty,
        };
    }
} // namespace spectra::scene

