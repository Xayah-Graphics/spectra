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

        template <typename Item>
        void validate_unique_scene_item_names(const std::vector<Item>& items, const std::string_view layer, const std::string_view kind) {
            std::set<std::string_view> names{};
            for (const Item& item : items) {
                if (item.name.empty()) throw std::runtime_error(std::format("{} {} item names must not be empty", layer, kind));
                if (!names.insert(std::string_view{item.name}).second) throw std::runtime_error(std::format("{} {} item \"{}\" is duplicated", layer, kind, item.name));
            }
        }

        template <typename Item>
        [[nodiscard]] std::vector<Item> resolve_scene_items(const std::vector<Item>& document_items, const std::vector<Item>& frame_items, const std::string_view kind) {
            validate_unique_scene_item_names(document_items, "Scene document", kind);
            validate_unique_scene_item_names(frame_items, "Scene frame", kind);

            std::vector<Item> resolved = document_items;
            std::map<std::string, std::size_t> document_indices{};
            for (std::size_t index = 0; index < resolved.size(); ++index) document_indices.emplace(resolved.at(index).name, index);

            for (const Item& frame_item : frame_items) {
                const std::map<std::string, std::size_t>::const_iterator found = document_indices.find(frame_item.name);
                if (found != document_indices.end()) {
                    resolved.at(found->second) = frame_item;
                    continue;
                }
                document_indices.emplace(frame_item.name, resolved.size());
                resolved.push_back(frame_item);
            }
            return resolved;
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

    SceneResolvedFrame SceneWorkspace::resolved_frame() const {
        if (this->currentDocument == nullptr) throw std::runtime_error("Scene workspace does not contain a loaded document");
        const std::optional<SceneFrameSnapshot> frame = this->currentTimeline.currentFrame;
        const SceneFrameSnapshot empty_frame{};
        const SceneFrameSnapshot& frame_value = frame.has_value() ? *frame : empty_frame;
        return SceneResolvedFrame{
            .revision        = this->currentRevision,
            .document        = this->currentDocument,
            .timeline        = this->currentTimeline,
            .frame           = frame,
            .meshes          = resolve_scene_items(this->currentDocument->meshes, frame_value.meshes, "mesh"),
            .particleSets    = resolve_scene_items(this->currentDocument->particleSets, frame_value.particleSets, "particle set"),
            .pointClouds     = resolve_scene_items(this->currentDocument->pointClouds, frame_value.pointClouds, "point cloud"),
            .volumes         = resolve_scene_items(this->currentDocument->volumes, frame_value.volumes, "volume"),
            .curveSets       = resolve_scene_items(this->currentDocument->curveSets, frame_value.curveSets, "curve set"),
            .splatSets       = resolve_scene_items(this->currentDocument->splatSets, frame_value.splatSets, "splat set"),
            .lineSets        = resolve_scene_items(this->currentDocument->lineSets, frame_value.lineSets, "line set"),
            .debugPrimitives = resolve_scene_items(this->currentDocument->debugPrimitives, frame_value.debugPrimitives, "debug primitive"),
            .vectorFields    = resolve_scene_items(this->currentDocument->vectorFields, frame_value.vectorFields, "vector field"),
            .cloths          = resolve_scene_items(this->currentDocument->cloths, frame_value.cloths, "cloth"),
            .rigidBodies     = resolve_scene_items(this->currentDocument->rigidBodies, frame_value.rigidBodies, "rigid body"),
            .colliders       = resolve_scene_items(this->currentDocument->colliders, frame_value.colliders, "collider"),
        };
    }

    SceneEditBatch SceneWorkspace::commit(SceneEditBuilder edit) {
        if (this->currentDocument == nullptr) throw std::runtime_error("Cannot edit an unloaded scene workspace");
        if (edit.dirty == SceneDirtyFlags::None) throw std::runtime_error("Cannot commit an empty scene edit");

        const SceneRevision before_revision = this->currentRevision;
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
            .beforeRevision = before_revision,
            .afterRevision  = this->currentRevision,
            .dirty          = edit.dirty,
        };
    }
} // namespace spectra::scene

