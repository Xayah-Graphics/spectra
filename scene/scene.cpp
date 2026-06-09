module;

#ifndef SPECTRA_SCENES_ROOT
#error "SPECTRA_SCENES_ROOT must point to the project-local scene asset directory."
#endif

#include <zlib.h>

module spectra.scene;

import :math;
import std;

namespace spectra::scene {
    namespace {
        void validate_scene_id(const std::string_view scene_id) {
            if (scene_id.empty()) throw std::runtime_error("Scene camera workspace requires a non-empty scene id");
        }

        void validate_camera_state(const SceneCameraState& state) {
            if (!is_finite(state.eye)) throw std::runtime_error("Scene camera eye must be finite");
            if (!is_finite(state.target)) throw std::runtime_error("Scene camera target must be finite");
            if (!is_finite(state.up)) throw std::runtime_error("Scene camera up vector must be finite");
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

    namespace {
        void CommitFrame(SceneWorkspace& workspace, SceneFrameSnapshot frame, const bool reset_timeline) {
            SceneEditBuilder edit{};
            if (reset_timeline) {
                const std::shared_ptr<const SceneDocument> document = workspace.document();
                edit.replaceTimeline(SimulationTimeline{
                    .mode            = SimulationTimelineMode::Live,
                    .framesPerSecond = document->framesPerSecond,
                    .playing         = true,
                    .loop            = true,
                    .cursor          = frame.cursor,
                });
            }
            edit.replaceFrame(std::move(frame));
            const SceneEditBatch batch = workspace.commit(std::move(edit));
            if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void CommitTimelineAndFrame(SceneWorkspace& workspace, SimulationTimeline timeline, SceneFrameSnapshot frame) {
            timeline.cursor = frame.cursor;
            timeline.currentFrame = frame;
            SceneEditBuilder edit{};
            edit.replaceTimeline(std::move(timeline));
            edit.replaceFrame(std::move(frame));
            const SceneEditBatch batch = workspace.commit(std::move(edit));
            if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
            if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Frame)) throw std::runtime_error("Scene frame commit did not mark the frame dirty");
        }

        void CommitTimeline(SceneWorkspace& workspace, SimulationTimeline timeline) {
            SceneEditBuilder edit{};
            edit.replaceTimeline(std::move(timeline));
            const SceneEditBatch batch = workspace.commit(std::move(edit));
            if (!HasSceneDirtyFlag(batch.dirty, SceneDirtyFlags::Timeline)) throw std::runtime_error("Scene timeline commit did not mark the timeline dirty");
        }
    } // namespace

    FrameCursor MakeFrameCursor(const SceneFrameInfo& info) {
        return FrameCursor{
            .frameIndex  = info.frame_index,
            .timeSeconds = info.time_seconds,
        };
    }

    void SceneSourceRegistry::register_static_scene(std::string id, std::string title, std::move_only_function<SceneDocument()> create_document) {
        if (!create_document) throw std::runtime_error("Static scene entry requires a document factory");
        this->ensure_unique_scene_id(id);
        this->entries.push_back(SceneSourceEntry{
            .id                     = std::move(id),
            .title                  = std::move(title),
            .kind                   = SceneSourceKind::Static,
            .create_static_document = std::move(create_document),
        });
    }

    std::unique_ptr<SceneSourceRuntime> SceneSourceRegistry::create_simulation_runtime(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Scene registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Scene source index is out of range");
        SceneSourceEntry& scene = this->entries.at(index);
        if (scene.kind != SceneSourceKind::Simulation) throw std::runtime_error("Scene source entry is not a simulation");
        if (!scene.create_simulation_runtime) throw std::runtime_error("Simulation scene entry has no runtime factory");
        return scene.create_simulation_runtime();
    }

    SceneDocument SceneSourceRegistry::create_static_document(const std::size_t index) {
        if (this->entries.empty()) throw std::runtime_error("Scene registry is empty");
        if (index >= this->entries.size()) throw std::runtime_error("Scene source index is out of range");
        SceneSourceEntry& scene = this->entries.at(index);
        if (scene.kind != SceneSourceKind::Static) throw std::runtime_error("Scene source entry is not static");
        if (!scene.create_static_document) throw std::runtime_error("Static scene entry has no document factory");
        return scene.create_static_document();
    }

    const SceneSourceEntry& SceneSourceRegistry::entry(const std::size_t index) const {
        if (index >= this->entries.size()) throw std::runtime_error("Scene source index is out of range");
        return this->entries.at(index);
    }

    std::size_t SceneSourceRegistry::size() const {
        return this->entries.size();
    }

    void SceneSourceRegistry::ensure_unique_scene_id(const std::string& id) const {
        if (id.empty()) throw std::runtime_error("Scene source id must not be empty");
        for (const SceneSourceEntry& entry : this->entries) {
            if (entry.id == id) throw std::runtime_error("Duplicate scene source id: " + id);
        }
    }

    SceneSession::SceneSession(SceneSourceRegistry registry) : registry(std::move(registry)) {
        if (this->registry.size() == 0u) throw std::runtime_error("Scene session requires at least one scene source");
        this->slots.resize(this->registry.size());
        this->ensure_slot(0u);
    }

    std::shared_ptr<SceneWorkspace> SceneSession::active_workspace() {
        return this->ensure_slot(this->currentActiveIndex).workspace;
    }

    const SceneSourceEntry& SceneSession::entry(const std::size_t index) const {
        return this->registry.entry(index);
    }

    std::size_t SceneSession::size() const {
        return this->registry.size();
    }

    std::size_t SceneSession::active_index() const {
        return this->currentActiveIndex;
    }

    std::size_t SceneSession::selected_index() const {
        return this->pendingActiveIndex.value_or(this->currentActiveIndex);
    }

    bool SceneSession::pending_switch() const {
        return this->pendingActiveIndex.has_value() && *this->pendingActiveIndex != this->currentActiveIndex;
    }

    void SceneSession::request_activate(const std::size_t index) {
        if (index >= this->slots.size()) throw std::runtime_error("Scene source activation index is out of range");
        if (index == this->currentActiveIndex) {
            this->pendingActiveIndex.reset();
            return;
        }
        this->pendingActiveIndex = index;
    }

    bool SceneSession::apply_pending_scene() {
        if (!this->pendingActiveIndex.has_value()) return false;
        const std::size_t next_index = *this->pendingActiveIndex;
        this->pendingActiveIndex.reset();
        if (next_index >= this->slots.size()) throw std::runtime_error("Pending scene source index is out of range");
        if (next_index == this->currentActiveIndex) return false;
        this->ensure_slot(next_index);
        this->currentActiveIndex = next_index;
        return true;
    }

    void SceneSession::update_active_scene(const double delta_seconds) {
        const SceneSourceEntry& entry = this->registry.entry(this->currentActiveIndex);
        if (entry.kind == SceneSourceKind::Static) return;
        SceneSlot& slot = this->ensure_slot(this->currentActiveIndex);
        if (slot.runtime == nullptr) throw std::runtime_error("Simulation scene slot has no runtime");
        const std::shared_ptr<const SceneDocument> document = slot.workspace->document();
        if (!document->timelineEnabled) throw std::runtime_error("Simulation scene must enable timeline");
        if (document->framesPerSecond <= 0.0) throw std::runtime_error("Simulation scene frame rate must be positive");
        const double fixed_delta_seconds = 1.0 / document->framesPerSecond;
        SimulationTimeline timeline = slot.workspace->timeline();
        if (timeline.framesPerSecond <= 0.0) throw std::runtime_error("Simulation timeline frame rate must be positive");
        if (timeline.resetRequestSerial != slot.observed_reset_request_serial) {
            this->reset_simulation(slot, std::move(timeline));
            slot.observed_reset_request_serial = slot.workspace->timeline().resetRequestSerial;
            slot.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.clearRecordingRequestSerial != slot.observed_clear_recording_request_serial) {
            timeline.recordedFrames.clear();
            timeline.selectedFrameIndex = 0;
            CommitTimeline(*slot.workspace, std::move(timeline));
            slot.observed_clear_recording_request_serial = slot.workspace->timeline().clearRecordingRequestSerial;
            slot.committed_playback_frame_index.reset();
            return;
        }
        if (timeline.mode == SimulationTimelineMode::Playback) {
            if (timeline.recordedFrames.empty()) return;
            if (timeline.selectedFrameIndex >= timeline.recordedFrames.size()) throw std::runtime_error("Scene playback selected frame is out of range");
            if (slot.committed_playback_frame_index.has_value() && *slot.committed_playback_frame_index == timeline.selectedFrameIndex) return;
            SceneFrameSnapshot selected_frame = timeline.recordedFrames.at(timeline.selectedFrameIndex);
            CommitFrame(*slot.workspace, std::move(selected_frame), false);
            slot.committed_playback_frame_index = timeline.selectedFrameIndex;
            return;
        }
        slot.committed_playback_frame_index.reset();
        if (!timeline.playing) return;
        if (!std::isfinite(delta_seconds) || delta_seconds < 0.0) throw std::runtime_error("Scene frame delta time is invalid");
        slot.simulation_accumulator_seconds += delta_seconds;
        bool advanced = false;
        SceneFrameSnapshot snapshot{};
        while (slot.simulation_accumulator_seconds >= fixed_delta_seconds) {
            slot.simulation_accumulator_seconds -= fixed_delta_seconds;
            ++slot.simulation_frame_index;
            slot.simulation_time_seconds += fixed_delta_seconds;
            snapshot = slot.runtime->step(SceneFrameInfo{
                .delta_seconds = fixed_delta_seconds,
                .time_seconds  = slot.simulation_time_seconds,
                .frame_index   = slot.simulation_frame_index,
            });
            advanced = true;
        }
        if (!advanced) return;
        if (timeline.mode == SimulationTimelineMode::Record) {
            timeline.recordedFrames.push_back(snapshot);
            timeline.selectedFrameIndex = timeline.recordedFrames.size() - 1u;
            CommitTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
            return;
        }
        CommitFrame(*slot.workspace, std::move(snapshot), false);
    }

    SceneSession::SceneSlot& SceneSession::ensure_slot(const std::size_t index) {
        if (index >= this->slots.size()) throw std::runtime_error("Scene session slot index is out of range");
        SceneSlot& slot = this->slots.at(index);
        const SceneSourceEntry& entry = this->registry.entry(index);
        if (slot.workspace != nullptr) {
            if (entry.kind == SceneSourceKind::Simulation && slot.runtime == nullptr) throw std::runtime_error("Simulation scene slot has a workspace but no runtime");
            return slot;
        }
        SceneDocument document = entry.kind == SceneSourceKind::Static ? this->registry.create_static_document(index) : this->create_simulation_slot(index, &slot);
        slot.workspace = std::make_shared<SceneWorkspace>(std::move(document));
        if (entry.kind == SceneSourceKind::Static) return slot;
        if (slot.runtime == nullptr) throw std::runtime_error("Simulation scene slot was not initialized");
        SceneFrameSnapshot snapshot = slot.runtime->reset();
        const std::shared_ptr<const SceneDocument> scene = slot.workspace->document();
        if (!scene->timelineEnabled) throw std::runtime_error("Simulation scene must enable timeline");
        SimulationTimeline timeline{
            .mode               = SimulationTimelineMode::Live,
            .framesPerSecond    = scene->framesPerSecond,
            .playing            = true,
            .loop               = true,
            .cursor             = snapshot.cursor,
            .selectedFrameIndex = 0,
            .currentFrame       = snapshot,
        };
        CommitTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
        return slot;
    }

    SceneDocument SceneSession::create_simulation_slot(const std::size_t index, SceneSlot* slot) {
        if (slot == nullptr) throw std::runtime_error("Simulation scene slot pointer must not be null");
        slot->runtime = this->registry.create_simulation_runtime(index);
        SceneDocument document = slot->runtime->create_document();
        if (!document.timelineEnabled) throw std::runtime_error("Simulation scene document must enable timeline");
        return document;
    }

    void SceneSession::reset_simulation(SceneSlot& slot, SimulationTimeline timeline) {
        slot.simulation_accumulator_seconds = 0.0;
        slot.simulation_time_seconds = 0.0;
        slot.simulation_frame_index = 0;
        SceneFrameSnapshot snapshot = slot.runtime->reset();
        timeline.cursor = snapshot.cursor;
        timeline.currentFrame = snapshot;
        timeline.selectedFrameIndex = 0;
        CommitTimelineAndFrame(*slot.workspace, std::move(timeline), std::move(snapshot));
    }

    namespace {
        struct Bounds {
            Vector3 minimum{};
            Vector3 maximum{};
            bool valid{false};
        };

        [[nodiscard]] SceneSourceLocation to_scene_source(const SceneSourceLocation& source) {
            return SceneSourceLocation{
                .filename = source.filename,
                .line     = source.line,
                .column   = source.column,
            };
        }

        [[nodiscard]] float matrix_value(const std::array<float, 16>& matrix, const std::size_t row, const std::size_t column) {
            return matrix.at(row * 4u + column);
        }

        void include_point(Bounds& bounds, const Vector3 point) {
            if (!is_finite(point)) throw std::runtime_error("PBRT preview mesh contains a non-finite point");
            if (!bounds.valid) {
                bounds.minimum = point;
                bounds.maximum = point;
                bounds.valid   = true;
                return;
            }
            bounds.minimum.x = std::min(bounds.minimum.x, point.x);
            bounds.minimum.y = std::min(bounds.minimum.y, point.y);
            bounds.minimum.z = std::min(bounds.minimum.z, point.z);
            bounds.maximum.x = std::max(bounds.maximum.x, point.x);
            bounds.maximum.y = std::max(bounds.maximum.y, point.y);
            bounds.maximum.z = std::max(bounds.maximum.z, point.z);
        }

        [[nodiscard]] Vector3 center(const Bounds& bounds) {
            if (!bounds.valid) throw std::runtime_error("Cannot compute PBRT preview bounds center without mesh geometry");
            return Vector3{
                (bounds.minimum.x + bounds.maximum.x) * 0.5f,
                (bounds.minimum.y + bounds.maximum.y) * 0.5f,
                (bounds.minimum.z + bounds.maximum.z) * 0.5f,
            };
        }

        [[nodiscard]] float radius(const Bounds& bounds) {
            if (!bounds.valid) throw std::runtime_error("Cannot compute PBRT preview bounds radius without mesh geometry");
            return length(Vector3{
                (bounds.maximum.x - bounds.minimum.x) * 0.5f,
                (bounds.maximum.y - bounds.minimum.y) * 0.5f,
                (bounds.maximum.z - bounds.minimum.z) * 0.5f,
            });
        }

        [[nodiscard]] const std::vector<float>& required_float_values(const PbrtSceneEntity& entity, const std::string_view type, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != type || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain float values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"{} {}\"", context, type, name));
        }

        [[nodiscard]] const std::vector<int>& required_int_values(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != "integer" || parameter.name != name) continue;
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr) throw std::runtime_error(std::format("{} parameter \"{}\" must contain integer values", context, name));
                return *values;
            }
            throw std::runtime_error(std::format("{} requires \"integer {}\"", context, name));
        }

        [[nodiscard]] Vector3 required_rgb_value(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            const std::vector<float>& values = required_float_values(entity, "rgb", name, context);
            if (values.size() != 3u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly three RGB values", context, name));
            return Vector3{values.at(0), values.at(1), values.at(2)};
        }

        [[nodiscard]] float required_one_float_value(const PbrtSceneEntity& entity, const std::string_view name, const std::string_view context) {
            for (const PbrtSceneParameter& parameter : entity.parameters) {
                if (parameter.type != "float" || parameter.name != name) continue;
                const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                if (values == nullptr || values->size() != 1u) throw std::runtime_error(std::format("{} parameter \"{}\" must contain exactly one float", context, name));
                return values->front();
            }
            throw std::runtime_error(std::format("{} requires \"float {}\"", context, name));
        }

        [[nodiscard]] Vector3 transform_point(const PbrtSceneTransform& transform, const Vector3 point) {
            const std::array<float, 16>& matrix = transform.matrix;
            const float x = matrix_value(matrix, 0u, 0u) * point.x + matrix_value(matrix, 0u, 1u) * point.y + matrix_value(matrix, 0u, 2u) * point.z + matrix_value(matrix, 0u, 3u);
            const float y = matrix_value(matrix, 1u, 0u) * point.x + matrix_value(matrix, 1u, 1u) * point.y + matrix_value(matrix, 1u, 2u) * point.z + matrix_value(matrix, 1u, 3u);
            const float z = matrix_value(matrix, 2u, 0u) * point.x + matrix_value(matrix, 2u, 1u) * point.y + matrix_value(matrix, 2u, 2u) * point.z + matrix_value(matrix, 2u, 3u);
            const float w = matrix_value(matrix, 3u, 0u) * point.x + matrix_value(matrix, 3u, 1u) * point.y + matrix_value(matrix, 3u, 2u) * point.z + matrix_value(matrix, 3u, 3u);
            if (!std::isfinite(w) || w == 0.0f) throw std::runtime_error("PBRT preview mesh transform produced an invalid homogeneous point");
            return Vector3{x / w, y / w, z / w};
        }

        [[nodiscard]] Vector3 transform_normal(const PbrtSceneTransform& transform, const Vector3 normal) {
            const std::array<float, 16>& inverse = transform.inverse;
            const Vector3 transformed{
                matrix_value(inverse, 0u, 0u) * normal.x + matrix_value(inverse, 1u, 0u) * normal.y + matrix_value(inverse, 2u, 0u) * normal.z,
                matrix_value(inverse, 0u, 1u) * normal.x + matrix_value(inverse, 1u, 1u) * normal.y + matrix_value(inverse, 2u, 1u) * normal.z,
                matrix_value(inverse, 0u, 2u) * normal.x + matrix_value(inverse, 1u, 2u) * normal.y + matrix_value(inverse, 2u, 2u) * normal.z,
            };
            return normalize(transformed, "PBRT preview mesh normal transform");
        }

        void require_static_transform(const PbrtSceneTransformSet& transform, const std::string_view context) {
            if (transform.animated) throw std::runtime_error(std::format("{} uses animated transforms, which are not supported by the PBRT preview scene loader", context));
        }

        [[nodiscard]] std::string object_source_prefix(const PbrtSceneSnapshot& scene) {
            if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
            if (scene.source.starts_with("pbrt://")) return scene.source;
            return std::format("pbrt://{}", scene.source);
        }

        [[nodiscard]] std::string make_shape_object_name(const PbrtSceneSnapshot& scene, const std::size_t shape_index) {
            return std::format("{}#shape:{}", object_source_prefix(scene), shape_index);
        }

        [[nodiscard]] std::set<std::string> referenced_shape_material_names(const PbrtSceneSnapshot& scene) {
            std::set<std::string> names{};
            for (const PbrtSceneShape& shape : scene.shapes) {
                if (shape.materialName.empty()) throw std::runtime_error("PBRT preview shape references an empty material name");
                names.insert(shape.materialName);
            }
            return names;
        }

        [[nodiscard]] std::map<std::string, std::size_t> append_materials(const PbrtSceneSnapshot& scene, const std::set<std::string>& referenced_material_names, SceneDocument& document) {
            std::map<std::string, std::size_t> material_indices{};
            for (const PbrtSceneMaterial& material : scene.materials) {
                if (!referenced_material_names.contains(material.name)) continue;
                if (material.name.empty()) throw std::runtime_error("PBRT preview material name must not be empty");
                if (material.entity.type != "diffuse") throw std::runtime_error(std::format("PBRT preview material \"{}\" uses unsupported type \"{}\"", material.name, material.entity.type));
                const Vector3 reflectance = required_rgb_value(material.entity, "reflectance", std::format("PBRT preview material \"{}\"", material.name));
                const bool inserted = material_indices.emplace(material.name, document.materials.size()).second;
                if (!inserted) throw std::runtime_error(std::format("PBRT preview material \"{}\" is duplicated", material.name));
                document.materials.push_back(SceneMaterial{
                    .name      = material.name,
                    .baseColor = Vector4{reflectance.x, reflectance.y, reflectance.z, 1.0f},
                    .roughness = 0.72f,
                });
            }
            for (const std::string& material_name : referenced_material_names) {
                if (!material_indices.contains(material_name)) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", material_name));
            }
            return material_indices;
        }

        [[nodiscard]] SceneMaterial& material_for_name(SceneDocument& document, const std::map<std::string, std::size_t>& material_indices, const std::string& name) {
            const std::map<std::string, std::size_t>::const_iterator iter = material_indices.find(name);
            if (iter == material_indices.end()) throw std::runtime_error(std::format("PBRT preview shape references unknown material \"{}\"", name));
            return document.materials.at(iter->second);
        }

        void apply_area_light_material(const PbrtSceneShape& shape, const std::size_t shape_index, SceneDocument& document, const std::map<std::string, std::size_t>& material_indices) {
            if (!shape.areaLight.has_value()) return;
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.areaLight->entity.type != "diffuse") throw std::runtime_error(std::format("{} uses unsupported area light type \"{}\"", context, shape.areaLight->entity.type));
            SceneMaterial& material = material_for_name(document, material_indices, shape.materialName);
            const Vector3 radiance = required_rgb_value(shape.areaLight->entity, "L", context);
            if (material.emissionStrength != 0.0f && (material.emissionColor.x != radiance.x || material.emissionColor.y != radiance.y || material.emissionColor.z != radiance.z)) throw std::runtime_error(std::format("PBRT preview material \"{}\" is reused by area lights with different radiance", material.name));
            material.emissionColor    = radiance;
            material.emissionStrength = 1.0f;
        }

        [[nodiscard]] SceneMesh make_mesh(const PbrtSceneSnapshot& scene, const PbrtSceneShape& shape, const std::size_t shape_index, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            const std::string context = std::format("PBRT preview shape #{}", shape_index);
            if (shape.entity.type != "trianglemesh") throw std::runtime_error(std::format("PBRT preview scene loader only supports trianglemesh shapes, got \"{}\"", shape.entity.type));
            require_static_transform(shape.transform, context);
            if (shape.reverseOrientation) throw std::runtime_error(std::format("{} uses ReverseOrientation, which is not supported by the PBRT preview scene loader", context));
            if (!shape.mediumInterface.inside.empty() || !shape.mediumInterface.outside.empty()) throw std::runtime_error(std::format("{} uses MediumInterface, which is not supported by the PBRT preview scene loader", context));
            if (!material_indices.contains(shape.materialName)) throw std::runtime_error(std::format("{} references unknown material \"{}\"", context, shape.materialName));
            const std::string object_name = make_shape_object_name(scene, shape_index);
            const std::vector<float>& positions = required_float_values(shape.entity, "point3", "P", context);
            const std::vector<float>& normals = required_float_values(shape.entity, "normal", "N", context);
            const std::vector<int>& indices = required_int_values(shape.entity, "indices", context);
            if (positions.empty() || positions.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid point3 P data", object_name));
            if (normals.size() != positions.size()) throw std::runtime_error(std::format("PBRT preview shape \"{}\" normal count does not match position count", object_name));
            if (indices.empty() || indices.size() % 3u != 0u) throw std::runtime_error(std::format("PBRT preview shape \"{}\" has invalid triangle index data", object_name));

            SceneMesh mesh{
                .name         = object_name,
                .materialName = shape.materialName,
                .dynamic      = false,
                .source       = to_scene_source(shape.entity.source),
            };
            const std::size_t vertex_count = positions.size() / 3u;
            mesh.positions.reserve(vertex_count);
            mesh.normals.reserve(vertex_count);
            for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
                const Vector3 point{positions.at(vertex_index * 3u), positions.at(vertex_index * 3u + 1u), positions.at(vertex_index * 3u + 2u)};
                const Vector3 normal{normals.at(vertex_index * 3u), normals.at(vertex_index * 3u + 1u), normals.at(vertex_index * 3u + 2u)};
                const Vector3 transformed_point = transform_point(shape.transform.start, point);
                mesh.positions.push_back(transformed_point);
                mesh.normals.push_back(transform_normal(shape.transform.start, normal));
                include_point(bounds, transformed_point);
            }
            mesh.indices.reserve(indices.size());
            for (const int index : indices) {
                if (index < 0) throw std::runtime_error(std::format("PBRT preview shape \"{}\" contains a negative vertex index", object_name));
                const std::uint32_t converted_index = static_cast<std::uint32_t>(index);
                if (converted_index >= vertex_count) throw std::runtime_error(std::format("PBRT preview shape \"{}\" contains an out-of-range vertex index", object_name));
                mesh.indices.push_back(converted_index);
            }
            return mesh;
        }

        void append_meshes(const PbrtSceneSnapshot& scene, SceneDocument& document, const std::map<std::string, std::size_t>& material_indices, Bounds& bounds) {
            std::map<std::string, bool> material_used_by_area_light{};
            for (std::size_t shape_index = 0; shape_index < scene.shapes.size(); ++shape_index) {
                const PbrtSceneShape& shape = scene.shapes.at(shape_index);
                const bool is_area_light = shape.areaLight.has_value();
                const std::pair<std::map<std::string, bool>::iterator, bool> material_usage = material_used_by_area_light.emplace(shape.materialName, is_area_light);
                if (!material_usage.second && material_usage.first->second != is_area_light) throw std::runtime_error(std::format("PBRT preview material \"{}\" is shared by emissive and non-emissive shapes", shape.materialName));
                apply_area_light_material(shape, shape_index, document, material_indices);
                SceneMesh mesh = make_mesh(scene, shape, shape_index, material_indices, bounds);
                document.meshes.push_back(std::move(mesh));
            }
            if (document.meshes.empty()) throw std::runtime_error("PBRT preview scene loader did not find any trianglemesh shapes");
        }

        [[nodiscard]] SceneCamera make_camera(const PbrtSceneSnapshot& scene, const Bounds& bounds) {
            if (scene.renderSettings.camera.type != "perspective") throw std::runtime_error(std::format("PBRT preview scene loader only supports perspective cameras, got \"{}\"", scene.renderSettings.camera.type));
            require_static_transform(scene.renderSettings.cameraTransform, "PBRT preview camera");
            const std::array<float, 16>& world_from_camera = scene.renderSettings.cameraTransform.start.matrix;
            const Vector3 eye{matrix_value(world_from_camera, 0u, 3u), matrix_value(world_from_camera, 1u, 3u), matrix_value(world_from_camera, 2u, 3u)};
            const Vector3 up = normalize(Vector3{matrix_value(world_from_camera, 0u, 1u), matrix_value(world_from_camera, 1u, 1u), matrix_value(world_from_camera, 2u, 1u)}, "PBRT preview camera up vector");
            const Vector3 target = center(bounds);
            const float scene_radius = radius(bounds);
            const float camera_distance = length(eye - target);
            const float far_plane = std::max(20.0f, camera_distance + scene_radius * 4.0f);
            return SceneCamera{
                .name               = "camera.main",
                .transform          = Transform{.position = eye},
                .target             = target,
                .up                 = up,
                .verticalFovDegrees = required_one_float_value(scene.renderSettings.camera, "fov", "PBRT preview camera"),
                .nearPlane          = 0.01f,
                .farPlane           = far_plane,
                .source             = to_scene_source(scene.renderSettings.camera.source),
            };
        }

        void reject_unsupported_scene_content(const PbrtSceneSnapshot& scene) {
            if (!scene.textures.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT textures");
            if (!scene.media.empty()) throw std::runtime_error("PBRT preview scene loader does not support PBRT media");
            if (!scene.lights.empty()) throw std::runtime_error("PBRT preview scene loader only supports mesh area lights");
            if (!scene.objectDefinitions.empty() || !scene.objectInstances.empty()) throw std::runtime_error("PBRT preview scene loader only supports top-level trianglemesh shapes");
        }
    } // namespace

    SceneDocument MakePreviewSceneDocumentFromPbrt(const PbrtSceneSnapshot& scene) {
        reject_unsupported_scene_content(scene);
        if (scene.revision.value == 0u) throw std::runtime_error("PBRT preview scene revision must not be zero");
        if (scene.name.empty()) throw std::runtime_error("PBRT preview scene name must not be empty");
        if (scene.title.empty()) throw std::runtime_error("PBRT preview scene title must not be empty");
        if (scene.source.empty()) throw std::runtime_error("PBRT preview scene source must not be empty");
        SceneDocument document{
            .revision        = SceneRevision{scene.revision.value},
            .name            = scene.name,
            .title           = scene.title,
            .source          = object_source_prefix(scene),
            .framesPerSecond = 24.0,
            .timelineEnabled = false,
        };
        Bounds bounds{};
        const std::set<std::string> referenced_material_names = referenced_shape_material_names(scene);
        const std::map<std::string, std::size_t> material_indices = append_materials(scene, referenced_material_names, document);
        append_meshes(scene, document, material_indices, bounds);
        document.camera = make_camera(scene, bounds);
        document.lights.push_back(SceneLight{
            .name      = "preview.key",
            .kind      = SceneLightKind::Directional,
            .color     = Vector3{1.0f, 0.96f, 0.86f},
            .intensity = 1.8f,
        });
        return document;
    }

    SceneDocument LoadPreviewSceneDocumentFromPbrt(const std::string_view scene_id) {
        PbrtSceneWorkspace workspace = BuildPbrtScene(scene_id);
        const std::shared_ptr<const PbrtSceneSnapshot> scene = workspace.snapshot();
        if (scene == nullptr) throw std::runtime_error("PBRT workspace did not produce a scene snapshot");
        return MakePreviewSceneDocumentFromPbrt(*scene);
    }

    namespace {
        constexpr char DefaultMaterialName[] = "__pbrt_default_material";

        enum class TokenKind { Word, QuotedString, LeftBracket, RightBracket };
        enum class EntityUse { Generic, Film, Camera, Texture, Material, Medium, Light, AreaLight, Shape };

        struct Token {
            TokenKind kind{TokenKind::Word};
            std::string text{};
            SceneSourceLocation source{};
        };

        [[nodiscard]] std::string Lowercase(std::string value) {
            for (char& character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return value;
        }

        [[nodiscard]] std::string SourceString(const SceneSourceLocation& source) {
            return std::format("{}:{}:{}", source.filename, source.line, source.column);
        }

        [[nodiscard]] std::runtime_error ParseError(const SceneSourceLocation& source, const std::string_view message) {
            return std::runtime_error(std::format("{}: {}", SourceString(source), message));
        }

        class SceneOperationCancelled final : public std::exception {
        public:
            [[nodiscard]] const char* what() const noexcept override {
                return "Spectra scene operation cancelled";
            }
        };

        class PbrtSceneNotTopLevel final : public std::exception {
        public:
            [[nodiscard]] const char* what() const noexcept override {
                return "PBRT file is not a top-level scene";
            }
        };

        void CheckSceneStop(const std::stop_token* stopToken) {
            if (stopToken != nullptr && stopToken->stop_requested()) throw SceneOperationCancelled{};
        }

        [[nodiscard]] bool HasExtension(const std::filesystem::path& path, const std::string_view extension) {
            return Lowercase(path.extension().string()) == Lowercase(std::string(extension));
        }

        [[nodiscard]] bool IsPbrtSceneFile(const std::filesystem::path& path) {
            if (HasExtension(path, ".pbrt")) return true;
            if (!HasExtension(path, ".gz")) return false;
            return HasExtension(path.stem(), ".pbrt");
        }

        [[nodiscard]] bool IsAbsolutePathString(const std::string& value) {
            if (value.empty()) return false;
            if (std::filesystem::path(value).is_absolute()) return true;
            return value.size() >= 3 && std::isalpha(static_cast<unsigned char>(value[0])) && value[1] == ':' && (value[2] == '/' || value[2] == '\\');
        }

        [[nodiscard]] bool IsPathLike(const std::string& value) {
            if (value.find('/') != std::string::npos || value.find('\\') != std::string::npos) return true;
            const std::filesystem::path path(value);
            return !path.extension().empty();
        }

        [[nodiscard]] std::string ReadPlainFile(const std::filesystem::path& path, const std::stop_token* stopToken) {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error(std::format("{}: unable to open PBRT scene file", path.string()));

            std::string result;
            std::array<char, 1 << 15> buffer{};
            while (input) {
                CheckSceneStop(stopToken);
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const std::streamsize count = input.gcount();
                if (count > 0) result.append(buffer.data(), static_cast<std::size_t>(count));
            }
            if (input.bad()) throw std::runtime_error(std::format("{}: PBRT scene file read failed", path.string()));
            CheckSceneStop(stopToken);
            return result;
        }

        [[nodiscard]] std::string ReadGzipFile(const std::filesystem::path& path, const std::stop_token* stopToken) {
            gzFile file = gzopen(path.string().c_str(), "rb");
            if (file == nullptr) throw std::runtime_error(std::format("{}: unable to open gzip PBRT scene file", path.string()));
            const auto checkStop = [file, stopToken] {
                if (stopToken != nullptr && stopToken->stop_requested()) {
                    gzclose(file);
                    throw SceneOperationCancelled{};
                }
            };

            std::string result;
            std::array<char, 1 << 15> buffer{};
            while (true) {
                checkStop();
                const int count = gzread(file, buffer.data(), static_cast<unsigned int>(buffer.size()));
                if (count < 0) {
                    int errorNumber = 0;
                    const char* message = gzerror(file, &errorNumber);
                    gzclose(file);
                    throw std::runtime_error(std::format("{}: gzip read failed: {}", path.string(), message == nullptr ? "unknown zlib error" : message));
                }
                if (count == 0) break;
                result.append(buffer.data(), static_cast<std::size_t>(count));
            }

            checkStop();
            const int closeStatus = gzclose(file);
            if (closeStatus != Z_OK) throw std::runtime_error(std::format("{}: gzip close failed", path.string()));
            return result;
        }

        [[nodiscard]] std::string ReadSceneFile(const std::filesystem::path& path, const std::stop_token* stopToken) {
            if (HasExtension(path, ".gz")) return ReadGzipFile(path, stopToken);
            return ReadPlainFile(path, stopToken);
        }

        class PbrtTokenStream {
        public:
            explicit PbrtTokenStream(std::filesystem::path filename, const std::stop_token* stopToken) : stopToken(stopToken) {
                this->PushFile(std::move(filename));
            }

            [[nodiscard]] std::optional<Token> Next() {
                CheckSceneStop(this->stopToken);
                if (this->pushedToken.has_value()) return std::exchange(this->pushedToken, {});

                while (!this->fileStack.empty()) {
                    CheckSceneStop(this->stopToken);
                    PbrtTokenFile& file = this->fileStack.back();
                    this->SkipIgnored(&file);
                    if (file.offset >= file.content.size()) {
                        this->fileStack.pop_back();
                        continue;
                    }

                    const SceneSourceLocation source{
                        .filename = file.filename.string(),
                        .line     = file.line,
                        .column   = file.column,
                    };

                    const char character = file.content[file.offset];
                    if (character == '[') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::LeftBracket, .text = "[", .source = source};
                    }
                    if (character == ']') {
                        this->Advance(&file);
                        return Token{.kind = TokenKind::RightBracket, .text = "]", .source = source};
                    }
                    if (character == '"') return this->ReadString(&file, source);
                    return this->ReadWord(&file, source);
                }

                return {};
            }

            void PushBack(Token token) {
                if (this->pushedToken.has_value()) throw std::runtime_error("PBRT parser internal error: token pushback overflow");
                this->pushedToken = std::move(token);
            }

            void PushFile(std::filesystem::path path) {
                CheckSceneStop(this->stopToken);
                if (!std::filesystem::exists(path)) throw std::runtime_error(std::format("{}: PBRT scene file does not exist", path.string()));
                std::string content = ReadSceneFile(path, this->stopToken);
                CheckSceneStop(this->stopToken);
                this->fileStack.push_back(PbrtTokenFile{
                    .filename = std::move(path),
                    .content  = std::move(content),
                });
            }

        private:
            struct PbrtTokenFile {
                std::filesystem::path filename;
                std::string content;
                std::size_t offset{};
                int line{1};
                int column{1};
            };

            void Advance(PbrtTokenFile* file) {
                if (file->offset >= file->content.size()) return;
                if (file->content[file->offset] == '\n') {
                    ++file->line;
                    file->column = 1;
                } else {
                    ++file->column;
                }
                ++file->offset;
            }

            void SkipIgnored(PbrtTokenFile* file) {
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character))) {
                        this->Advance(file);
                        continue;
                    }
                    if (character == '#') {
                        while (file->offset < file->content.size() && file->content[file->offset] != '\n') this->Advance(file);
                        continue;
                    }
                    return;
                }
            }

            [[nodiscard]] Token ReadString(PbrtTokenFile* file, const SceneSourceLocation& source) {
                this->Advance(file);
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (character == '"') {
                        this->Advance(file);
                        return Token{.kind = TokenKind::QuotedString, .text = std::move(text), .source = source};
                    }
                    if (character == '\\') {
                        this->Advance(file);
                        if (file->offset >= file->content.size()) throw ParseError(source, "unterminated escape sequence in quoted string");
                        text.push_back(file->content[file->offset]);
                        this->Advance(file);
                        continue;
                    }
                    text.push_back(character);
                    this->Advance(file);
                }
                throw ParseError(source, "unterminated quoted string");
            }

            [[nodiscard]] Token ReadWord(PbrtTokenFile* file, const SceneSourceLocation& source) {
                std::string text;
                while (file->offset < file->content.size()) {
                    const char character = file->content[file->offset];
                    if (std::isspace(static_cast<unsigned char>(character)) || character == '[' || character == ']' || character == '#') break;
                    text.push_back(character);
                    this->Advance(file);
                }
                if (text.empty()) throw ParseError(source, "unexpected character in PBRT scene file");
                return Token{.kind = TokenKind::Word, .text = std::move(text), .source = source};
            }

            std::vector<PbrtTokenFile> fileStack{};
            std::optional<Token> pushedToken{};
            const std::stop_token* stopToken{};
        };

        [[nodiscard]] Token RequireToken(PbrtTokenStream& stream, const std::string_view context) {
            std::optional<Token> token = stream.Next();
            if (!token.has_value()) throw std::runtime_error(std::format("Unexpected end of PBRT scene file while parsing {}", context));
            return std::move(*token);
        }

        [[nodiscard]] std::string RequireStringToken(PbrtTokenStream& stream, const std::string_view context) {
            Token token = RequireToken(stream, context);
            if (token.kind != TokenKind::QuotedString) throw ParseError(token.source, std::format("{} expects a quoted string", context));
            return std::move(token.text);
        }

        [[nodiscard]] float ParseFloatToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const float value = std::strtof(begin, &end);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not a floating-point value", token.text));
            return value;
        }

        [[nodiscard]] int ParseIntegerToken(const Token& token) {
            const char* begin = token.text.c_str();
            char* end         = nullptr;
            const long value  = std::strtol(begin, &end, 10);
            if (end == begin || *end != '\0') throw ParseError(token.source, std::format("\"{}\" is not an integer value", token.text));
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) throw ParseError(token.source, std::format("\"{}\" is outside integer range", token.text));
            return static_cast<int>(value);
        }

        [[nodiscard]] std::uint8_t ParseBoolToken(const Token& token) {
            if (token.text == "true") return 1;
            if (token.text == "false") return 0;
            throw ParseError(token.source, std::format("\"{}\" is not a Boolean value", token.text));
        }

        [[nodiscard]] std::array<float, 16> MultiplyMatrix(const std::array<float, 16>& a, const std::array<float, 16>& b) {
            std::array<float, 16> result{};
            for (std::size_t row = 0; row < 4; ++row) {
                for (std::size_t column = 0; column < 4; ++column) {
                    for (std::size_t index = 0; index < 4; ++index) result[row * 4 + column] += a[row * 4 + index] * b[index * 4 + column];
                }
            }
            return result;
        }

        [[nodiscard]] std::array<float, 16> TransposeMatrix(const std::array<float, 16>& matrix) {
            return {
                matrix[0],
                matrix[4],
                matrix[8],
                matrix[12],
                matrix[1],
                matrix[5],
                matrix[9],
                matrix[13],
                matrix[2],
                matrix[6],
                matrix[10],
                matrix[14],
                matrix[3],
                matrix[7],
                matrix[11],
                matrix[15],
            };
        }

        [[nodiscard]] std::array<float, 16> InverseMatrix(const std::array<float, 16>& matrix, const SceneSourceLocation& source) {
            std::array<std::array<double, 8>, 4> augmented{};
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)] = matrix[static_cast<std::size_t>(row * 4 + column)];
                augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + row)] = 1.0;
            }

            for (int column = 0; column < 4; ++column) {
                int pivotRow = column;
                double pivot = std::abs(augmented[static_cast<std::size_t>(pivotRow)][static_cast<std::size_t>(column)]);
                for (int row = column + 1; row < 4; ++row) {
                    const double candidate = std::abs(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)]);
                    if (candidate > pivot) {
                        pivot    = candidate;
                        pivotRow = row;
                    }
                }
                if (!(pivot > 0.0)) throw ParseError(source, "Transform matrix is singular");
                if (pivotRow != column) std::swap(augmented[static_cast<std::size_t>(pivotRow)], augmented[static_cast<std::size_t>(column)]);

                const double denominator = augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(column)];
                for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)] /= denominator;

                for (int row = 0; row < 4; ++row) {
                    if (row == column) continue;
                    const double factor = augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
                    for (int index = 0; index < 8; ++index) augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(index)] -= factor * augmented[static_cast<std::size_t>(column)][static_cast<std::size_t>(index)];
                }
            }

            std::array<float, 16> inverse{};
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column) inverse[static_cast<std::size_t>(row * 4 + column)] = static_cast<float>(augmented[static_cast<std::size_t>(row)][static_cast<std::size_t>(4 + column)]);
            return inverse;
        }

        [[nodiscard]] PbrtSceneTransform Multiply(const PbrtSceneTransform& a, const PbrtSceneTransform& b) {
            return PbrtSceneTransform{
                .matrix  = MultiplyMatrix(a.matrix, b.matrix),
                .inverse = MultiplyMatrix(b.inverse, a.inverse),
            };
        }

        [[nodiscard]] PbrtSceneTransform Inverse(const PbrtSceneTransform& transform) {
            return PbrtSceneTransform{
                .matrix  = transform.inverse,
                .inverse = transform.matrix,
            };
        }

        [[nodiscard]] PbrtSceneTransform Translate(const Vector3 delta) {
            return PbrtSceneTransform{
                .matrix =
                    {
                        1.0f,
                        0.0f,
                        0.0f,
                        delta.x,
                        0.0f,
                        1.0f,
                        0.0f,
                        delta.y,
                        0.0f,
                        0.0f,
                        1.0f,
                        delta.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
                .inverse =
                    {
                        1.0f,
                        0.0f,
                        0.0f,
                        -delta.x,
                        0.0f,
                        1.0f,
                        0.0f,
                        -delta.y,
                        0.0f,
                        0.0f,
                        1.0f,
                        -delta.z,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
            };
        }

        [[nodiscard]] PbrtSceneTransform Scale(float x, float y, float z) {
            return PbrtSceneTransform{
                .matrix =
                    {
                        x,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        y,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        z,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
                .inverse =
                    {
                        1.0f / x,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f / y,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f / z,
                        0.0f,
                        0.0f,
                        0.0f,
                        0.0f,
                        1.0f,
                    },
            };
        }

        [[nodiscard]] PbrtSceneTransform Rotate(float degrees, Vector3 axis) {
            constexpr float radiansPerDegree = 0.017453292519943295769f;
            axis                             = normalize(axis, "PBRT rotate axis");
            const float sinTheta             = std::sin(degrees * radiansPerDegree);
            const float cosTheta             = std::cos(degrees * radiansPerDegree);
            const float oneMinusCosTheta     = 1.0f - cosTheta;
            const std::array<float, 16> matrix{
                axis.x * axis.x + (1.0f - axis.x * axis.x) * cosTheta,
                axis.x * axis.y * oneMinusCosTheta - axis.z * sinTheta,
                axis.x * axis.z * oneMinusCosTheta + axis.y * sinTheta,
                0.0f,
                axis.x * axis.y * oneMinusCosTheta + axis.z * sinTheta,
                axis.y * axis.y + (1.0f - axis.y * axis.y) * cosTheta,
                axis.y * axis.z * oneMinusCosTheta - axis.x * sinTheta,
                0.0f,
                axis.x * axis.z * oneMinusCosTheta - axis.y * sinTheta,
                axis.y * axis.z * oneMinusCosTheta + axis.x * sinTheta,
                axis.z * axis.z + (1.0f - axis.z * axis.z) * cosTheta,
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            return PbrtSceneTransform{
                .matrix  = matrix,
                .inverse = TransposeMatrix(matrix),
            };
        }

        [[nodiscard]] PbrtSceneTransform LookAt(const Vector3 position, const Vector3 look, const Vector3 up) {
            const Vector3 direction = normalize(look - position, "PBRT LookAt direction");
            const Vector3 right     = normalize(cross(normalize(up, "PBRT LookAt up vector"), direction), "PBRT LookAt right vector");
            const Vector3 newUp     = cross(direction, right);
            const std::array<float, 16> worldFromCamera{
                right.x,
                newUp.x,
                direction.x,
                position.x,
                right.y,
                newUp.y,
                direction.y,
                position.y,
                right.z,
                newUp.z,
                direction.z,
                position.z,
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            const std::array<float, 16> cameraFromWorld{
                right.x,
                right.y,
                right.z,
                -dot(right, position),
                newUp.x,
                newUp.y,
                newUp.z,
                -dot(newUp, position),
                direction.x,
                direction.y,
                direction.z,
                -dot(direction, position),
                0.0f,
                0.0f,
                0.0f,
                1.0f,
            };
            return PbrtSceneTransform{
                .matrix  = cameraFromWorld,
                .inverse = worldFromCamera,
            };
        }

        [[nodiscard]] PbrtSceneTransform TransformFromPbrtMatrix(const std::array<float, 16>& pbrtMatrix, const SceneSourceLocation& source) {
            const std::array<float, 16> matrix = TransposeMatrix(pbrtMatrix);
            return PbrtSceneTransform{
                .matrix  = matrix,
                .inverse = InverseMatrix(matrix, source),
            };
        }

        [[nodiscard]] bool TransformDiffers(const PbrtSceneTransform& left, const PbrtSceneTransform& right) {
            return left.matrix != right.matrix || left.inverse != right.inverse;
        }

        void RefreshAnimatedFlag(PbrtSceneTransformSet* transform) {
            transform->animated = TransformDiffers(transform->start, transform->end);
        }

        void ApplyTransform(PbrtSceneTransformSet* transform, const PbrtSceneTransform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = Multiply(transform->start, value);
            if (endActive) transform->end = Multiply(transform->end, value);
            RefreshAnimatedFlag(transform);
        }

        void SetTransform(PbrtSceneTransformSet* transform, const PbrtSceneTransform& value, const bool startActive, const bool endActive) {
            if (startActive) transform->start = value;
            if (endActive) transform->end = value;
            RefreshAnimatedFlag(transform);
        }

        [[nodiscard]] PbrtColorSpace ParseColorSpaceName(const std::string& name, const SceneSourceLocation& source) {
            const std::string lower = Lowercase(name);
            if (lower == "srgb") return PbrtColorSpace::sRGB;
            if (lower == "dci-p3") return PbrtColorSpace::DCI_P3;
            if (lower == "rec2020") return PbrtColorSpace::Rec2020;
            if (lower == "aces2065-1") return PbrtColorSpace::ACES2065_1;
            throw ParseError(source, std::format("\"{}\" is not a supported PBRT color space", name));
        }

        [[nodiscard]] std::vector<std::string>* ParameterStringValues(PbrtSceneParameter* parameter) {
            return std::get_if<std::vector<std::string>>(&parameter->values);
        }

        [[nodiscard]] const std::vector<std::string>* ParameterStringValues(const PbrtSceneParameter& parameter) {
            return std::get_if<std::vector<std::string>>(&parameter.values);
        }

        [[nodiscard]] const std::vector<float>* ParameterFloatValues(const PbrtSceneParameter& parameter) {
            return std::get_if<std::vector<float>>(&parameter.values);
        }

        [[nodiscard]] std::string OneStringParameter(const std::vector<PbrtSceneParameter>& parameters, const std::string& name, std::string default_value) {
            for (const PbrtSceneParameter& parameter : parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                const std::vector<std::string>* values = ParameterStringValues(parameter);
                if (values == nullptr || values->size() != 1) throw ParseError(parameter.source, std::format("PBRT string parameter \"{}\" must contain exactly one string value", name));
                return values->front();
            }
            return default_value;
        }

        [[nodiscard]] float OneFloatParameter(const std::vector<PbrtSceneParameter>& parameters, const std::string& name, const float default_value) {
            for (const PbrtSceneParameter& parameter : parameters) {
                if (parameter.name != name) continue;
                if (parameter.type != "float") throw ParseError(parameter.source, std::format("PBRT parameter \"{}\" must be declared as float", name));
                const std::vector<float>* values = ParameterFloatValues(parameter);
                if (values == nullptr || values->size() != 1) throw ParseError(parameter.source, std::format("PBRT float parameter \"{}\" must contain exactly one float value", name));
                return values->front();
            }
            return default_value;
        }

        [[nodiscard]] bool IsBuiltInApertureName(const std::string& value) {
            return value == "gaussian" || value == "square" || value == "pentagon" || value == "star";
        }

        struct GraphicsState {
            PbrtSceneTransformSet transform{};
            bool activeStart{true};
            bool activeEnd{true};
            PbrtColorSpace colorSpace{PbrtColorSpace::sRGB};
            std::string currentMaterialName{DefaultMaterialName};
            std::optional<PbrtSceneAreaLight> areaLight{};
            PbrtSceneMediumInterface mediumInterface{};
            bool reverseOrientation{false};
            std::vector<PbrtSceneParameter> shapeAttributes{};
            std::vector<PbrtSceneParameter> lightAttributes{};
            std::vector<PbrtSceneParameter> materialAttributes{};
            std::vector<PbrtSceneParameter> mediumAttributes{};
            std::vector<PbrtSceneParameter> textureAttributes{};
        };

        struct ProbeParameter {
            std::string type{};
            std::string name{};
            std::vector<std::string> stringValues{};
            SceneSourceLocation source{};
        };

        [[nodiscard]] ProbeParameter ParseProbeParameterDeclaration(const Token& declaration) {
            const std::string& text = declaration.text;
            std::size_t typeBegin = 0;
            while (typeBegin < text.size() && std::isspace(static_cast<unsigned char>(text[typeBegin]))) ++typeBegin;
            if (typeBegin == text.size()) throw ParseError(declaration.source, "PBRT parameter declaration does not contain a type");

            std::size_t typeEnd = typeBegin;
            while (typeEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[typeEnd]))) ++typeEnd;

            std::size_t nameBegin = typeEnd;
            while (nameBegin < text.size() && std::isspace(static_cast<unsigned char>(text[nameBegin]))) ++nameBegin;
            if (nameBegin == text.size()) throw ParseError(declaration.source, std::format("\"{}\" does not contain a parameter name", text));

            std::size_t nameEnd = nameBegin;
            while (nameEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;

            return ProbeParameter{
                .type   = text.substr(typeBegin, typeEnd - typeBegin),
                .name   = text.substr(nameBegin, nameEnd - nameBegin),
                .source = declaration.source,
            };
        }

        [[nodiscard]] std::string ProbeStringParameter(const std::vector<ProbeParameter>& parameters, const std::string& name, std::string default_value) {
            for (const ProbeParameter& parameter : parameters) {
                if (parameter.type != "string" || parameter.name != name) continue;
                if (parameter.stringValues.size() != 1) throw ParseError(parameter.source, std::format("PBRT probe string parameter \"{}\" must contain exactly one value", name));
                return parameter.stringValues.front();
            }
            return default_value;
        }

        class PbrtSceneProbeBuilder {
        public:
            explicit PbrtSceneProbeBuilder(std::filesystem::path inputFile, const std::stop_token* stopToken = nullptr) : stopToken(stopToken), inputFile(std::filesystem::absolute(std::move(inputFile)).lexically_normal()), searchDirectory(this->inputFile.parent_path()) {
                CheckSceneStop(this->stopToken);
                const SceneSourceLocation source{.filename = this->inputFile.string(), .line = 1, .column = 1};
                this->report.name     = this->inputFile.stem().string();
                this->report.title    = this->inputFile.stem().string();
                this->report.source   = this->inputFile.string();
                this->report.revision = SceneRevision{1};
                this->AddFeature(PbrtSceneProbeFeatureCategory::PixelFilter, "gaussian", {}, source);
                this->AddFeature(PbrtSceneProbeFeatureCategory::Film, "rgb", {}, source);
                this->AddFeature(PbrtSceneProbeFeatureCategory::Camera, "perspective", {}, source);
                this->AddFeature(PbrtSceneProbeFeatureCategory::Sampler, "zsobol", {}, source);
                this->AddFeature(PbrtSceneProbeFeatureCategory::Integrator, "volpath", {}, source);
                this->AddFeature(PbrtSceneProbeFeatureCategory::Accelerator, "bvh", {}, source);
            }

            [[nodiscard]] PbrtSceneProbeReport Probe() {
                CheckSceneStop(this->stopToken);
                this->ParseFile(this->inputFile);
                CheckSceneStop(this->stopToken);
                this->Finish();
                return std::move(this->report);
            }

        private:
            enum class BlockState { Options, World };

            void AddFeature(const PbrtSceneProbeFeatureCategory category, std::string type, std::string kind, const SceneSourceLocation& source) {
                this->report.features.push_back(PbrtSceneProbeFeature{
                    .category = category,
                    .type     = Lowercase(std::move(type)),
                    .kind     = Lowercase(std::move(kind)),
                    .source   = source,
                });
            }

            void AddTransformFeatureIfNeeded(const std::string_view owner, const SceneSourceLocation& source) {
                if (!this->graphicsState.transform.animated) return;
                this->AddFeature(PbrtSceneProbeFeatureCategory::AnimatedTransform, std::string{owner}, {}, source);
            }

            [[nodiscard]] std::filesystem::path ResolveIncludePath(const std::string& value, const SceneSourceLocation& source) const {
                if (value.empty()) throw ParseError(source, "Include filename must not be empty");
                const std::filesystem::path path = IsAbsolutePathString(value) ? std::filesystem::path(value) : this->searchDirectory / std::filesystem::path(value);
                return std::filesystem::absolute(path).lexically_normal();
            }

            void RequireExistingProbeFile(const std::filesystem::path& path, const SceneSourceLocation& source, const std::string_view directive) const {
                if (!std::filesystem::exists(path)) throw ParseError(source, std::format("{} file \"{}\" does not exist", directive, path.string()));
                if (!std::filesystem::is_regular_file(path)) throw ParseError(source, std::format("{} path \"{}\" is not a regular file", directive, path.string()));
            }

            void ParseFile(const std::filesystem::path& path) {
                CheckSceneStop(this->stopToken);
                PbrtTokenStream stream(path, this->stopToken);
                while (std::optional<Token> directive = stream.Next()) {
                    CheckSceneStop(this->stopToken);
                    this->ParseDirective(stream, *directive);
                }
            }

            std::vector<ProbeParameter> ParseProbeParameters(PbrtTokenStream& stream) {
                std::vector<ProbeParameter> parameters;
                while (true) {
                    CheckSceneStop(this->stopToken);
                    std::optional<Token> declaration = stream.Next();
                    if (!declaration.has_value()) return parameters;
                    if (declaration->kind != TokenKind::QuotedString) {
                        stream.PushBack(std::move(*declaration));
                        return parameters;
                    }

                    ProbeParameter parameter = ParseProbeParameterDeclaration(*declaration);
                    Token value = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                    if (value.kind == TokenKind::LeftBracket) {
                        while (true) {
                            CheckSceneStop(this->stopToken);
                            Token element = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                            if (element.kind == TokenKind::RightBracket) break;
                            this->AppendProbeParameterValue(&parameter, element);
                        }
                    } else {
                        this->AppendProbeParameterValue(&parameter, value);
                    }
                    parameters.push_back(std::move(parameter));
                }
            }

            void AppendProbeParameterValue(ProbeParameter* parameter, const Token& value) const {
                if (parameter->type == "string" || parameter->type == "texture") {
                    if (value.kind != TokenKind::QuotedString) throw ParseError(value.source, std::format("\"{} {}\" expects quoted string values", parameter->type, parameter->name));
                    parameter->stringValues.push_back(value.text);
                    return;
                }
                if (value.kind == TokenKind::QuotedString) parameter->stringValues.push_back(value.text);
            }

            void ParseDirective(PbrtTokenStream& stream, const Token& directive) {
                CheckSceneStop(this->stopToken);
                if (directive.kind != TokenKind::Word) throw ParseError(directive.source, "PBRT directive must be an unquoted identifier");

                if (directive.text == "AttributeBegin" || directive.text == "TransformBegin") {
                    this->RequireWorld(directive, directive.text);
                    this->stateStack.push_back(this->graphicsState);
                    this->stackKinds.push_back('a');
                    return;
                }
                if (directive.text == "AttributeEnd" || directive.text == "TransformEnd") {
                    this->RequireWorld(directive, directive.text);
                    this->PopGraphicsState(directive);
                    return;
                }
                if (directive.text == "ActiveTransform") {
                    this->ActiveTransform(RequireToken(stream, "ActiveTransform"), directive.source);
                    return;
                }
                if (directive.text == "AreaLightSource") {
                    this->RequireWorld(directive, "AreaLightSource");
                    const std::string type = RequireStringToken(stream, "AreaLightSource");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::AreaLight, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Accelerator") {
                    this->RequireOptions(directive, "Accelerator");
                    const std::string type = RequireStringToken(stream, "Accelerator");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Accelerator, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Attribute") {
                    static_cast<void>(RequireStringToken(stream, "Attribute"));
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Camera") {
                    this->RequireOptions(directive, "Camera");
                    const std::string type = RequireStringToken(stream, "Camera");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Camera, type, {}, directive.source);
                    this->AddTransformFeatureIfNeeded("camera", directive.source);
                    this->ParseProbeParameters(stream);
                    this->namedCoordinateSystems["camera"] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "PbrtColorSpace") {
                    this->graphicsState.colorSpace = ParseColorSpaceName(RequireStringToken(stream, "PbrtColorSpace"), directive.source);
                    return;
                }
                if (directive.text == "ConcatTransform") {
                    this->ConcatTransform(stream, directive.source);
                    return;
                }
                if (directive.text == "CoordinateSystem") {
                    this->namedCoordinateSystems[RequireStringToken(stream, "CoordinateSystem")] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "CoordSysTransform") {
                    const std::string name = RequireStringToken(stream, "CoordSysTransform");
                    const std::map<std::string, PbrtSceneTransformSet>::const_iterator iter = this->namedCoordinateSystems.find(name);
                    if (iter == this->namedCoordinateSystems.end()) throw ParseError(directive.source, std::format("Unknown coordinate system \"{}\"", name));
                    this->graphicsState.transform = iter->second;
                    return;
                }
                if (directive.text == "Film") {
                    this->RequireOptions(directive, "Film");
                    const std::string type = RequireStringToken(stream, "Film");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Film, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Identity") {
                    this->SetActiveTransform(PbrtSceneTransform{});
                    return;
                }
                if (directive.text == "Import") {
                    this->RequireWorld(directive, "Import");
                    const std::filesystem::path importPath = this->ResolveIncludePath(RequireStringToken(stream, "Import"), directive.source);
                    this->RequireExistingProbeFile(importPath, directive.source, "Import");
                    return;
                }
                if (directive.text == "Include") {
                    const std::filesystem::path includePath = this->ResolveIncludePath(RequireStringToken(stream, "Include"), directive.source);
                    this->RequireExistingProbeFile(includePath, directive.source, "Include");
                    return;
                }
                if (directive.text == "Integrator") {
                    this->RequireOptions(directive, "Integrator");
                    const std::string type = RequireStringToken(stream, "Integrator");
                    std::vector<ProbeParameter> parameters = this->ParseProbeParameters(stream);
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Integrator, type, {}, directive.source);
                    const std::string lightSampler = ProbeStringParameter(parameters, "lightsampler", {});
                    if (!lightSampler.empty()) this->AddFeature(PbrtSceneProbeFeatureCategory::LightSampler, lightSampler, {}, directive.source);
                    return;
                }
                if (directive.text == "LightSource") {
                    this->RequireWorld(directive, "LightSource");
                    const std::string type = RequireStringToken(stream, "LightSource");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Light, type, {}, directive.source);
                    this->AddTransformFeatureIfNeeded(std::format("light \"{}\"", type), directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "LookAt") {
                    std::array<float, 9> values{};
                    for (float& value : values) value = ParseFloatToken(RequireToken(stream, "LookAt"));
                    this->ApplyActiveTransform(LookAt(Vector3{values[0], values[1], values[2]}, Vector3{values[3], values[4], values[5]}, Vector3{values[6], values[7], values[8]}));
                    return;
                }
                if (directive.text == "MakeNamedMaterial") {
                    this->RequireWorld(directive, "MakeNamedMaterial");
                    const std::string name = RequireStringToken(stream, "MakeNamedMaterial");
                    std::vector<ProbeParameter> parameters = this->ParseProbeParameters(stream);
                    const std::string type = ProbeStringParameter(parameters, "type", {});
                    if (type.empty()) throw ParseError(directive.source, std::format("MakeNamedMaterial \"{}\" requires \"string type\"", name));
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Material, type, {}, directive.source);
                    return;
                }
                if (directive.text == "MakeNamedMedium") {
                    const std::string name = RequireStringToken(stream, "MakeNamedMedium");
                    std::vector<ProbeParameter> parameters = this->ParseProbeParameters(stream);
                    const std::string type = ProbeStringParameter(parameters, "type", {});
                    if (type.empty()) throw ParseError(directive.source, std::format("MakeNamedMedium \"{}\" requires \"string type\"", name));
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Medium, type, {}, directive.source);
                    this->AddTransformFeatureIfNeeded(std::format("medium \"{}\"", name), directive.source);
                    return;
                }
                if (directive.text == "Material") {
                    this->RequireWorld(directive, "Material");
                    std::string type = RequireStringToken(stream, "Material");
                    if (type.empty()) type = "interface";
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Material, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "MediumInterface") {
                    this->MediumInterface(stream);
                    return;
                }
                if (directive.text == "NamedMaterial") {
                    this->RequireWorld(directive, "NamedMaterial");
                    static_cast<void>(RequireStringToken(stream, "NamedMaterial"));
                    return;
                }
                if (directive.text == "ObjectBegin") {
                    this->ObjectBegin(RequireStringToken(stream, "ObjectBegin"), directive.source);
                    return;
                }
                if (directive.text == "ObjectEnd") {
                    this->ObjectEnd(directive.source);
                    return;
                }
                if (directive.text == "ObjectInstance") {
                    this->ObjectInstance(RequireStringToken(stream, "ObjectInstance"), directive.source);
                    return;
                }
                if (directive.text == "Option") {
                    const std::string name = RequireStringToken(stream, "Option");
                    Token value = RequireToken(stream, "Option");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Option, name, value.text, directive.source);
                    return;
                }
                if (directive.text == "PixelFilter") {
                    this->RequireOptions(directive, "PixelFilter");
                    const std::string type = RequireStringToken(stream, "PixelFilter");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::PixelFilter, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "ReverseOrientation") {
                    this->RequireWorld(directive, "ReverseOrientation");
                    return;
                }
                if (directive.text == "Rotate") {
                    const float angle = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float x = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float y = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float z = ParseFloatToken(RequireToken(stream, "Rotate"));
                    this->ApplyActiveTransform(Rotate(angle, Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "Sampler") {
                    this->RequireOptions(directive, "Sampler");
                    const std::string type = RequireStringToken(stream, "Sampler");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Sampler, type, {}, directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Scale") {
                    const float x = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float y = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float z = ParseFloatToken(RequireToken(stream, "Scale"));
                    this->ApplyActiveTransform(Scale(x, y, z));
                    return;
                }
                if (directive.text == "Shape") {
                    this->RequireWorld(directive, "Shape");
                    const std::string type = RequireStringToken(stream, "Shape");
                    this->AddFeature(PbrtSceneProbeFeatureCategory::Shape, type, {}, directive.source);
                    this->AddTransformFeatureIfNeeded(std::format("shape \"{}\"", type), directive.source);
                    this->ParseProbeParameters(stream);
                    return;
                }
                if (directive.text == "Texture") {
                    this->RequireWorld(directive, "Texture");
                    this->Texture(stream, directive.source);
                    return;
                }
                if (directive.text == "Transform") {
                    this->Transform(stream, directive.source);
                    return;
                }
                if (directive.text == "TransformTimes") {
                    this->RequireOptions(directive, "TransformTimes");
                    this->graphicsState.transform.startTime = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    this->graphicsState.transform.endTime = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    return;
                }
                if (directive.text == "Translate") {
                    const float x = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float y = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float z = ParseFloatToken(RequireToken(stream, "Translate"));
                    this->ApplyActiveTransform(Translate(Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "WorldBegin") {
                    this->RequireOptions(directive, "WorldBegin");
                    const float startTime = this->graphicsState.transform.startTime;
                    const float endTime = this->graphicsState.transform.endTime;
                    this->currentBlock = BlockState::World;
                    this->graphicsState.transform = PbrtSceneTransformSet{.startTime = startTime, .endTime = endTime};
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd = true;
                    this->namedCoordinateSystems["world"] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "WorldEnd") throw ParseError(directive.source, "WorldEnd is not used by PBRT v4 scene files");
                throw ParseError(directive.source, std::format("Unknown PBRT directive \"{}\"", directive.text));
            }

            void RequireOptions(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::Options) throw ParseError(directive.source, std::format("{} is only valid before WorldBegin", name));
            }

            void RequireWorld(const Token& directive, const std::string_view name) const {
                if (this->currentBlock == BlockState::Options) throw PbrtSceneNotTopLevel{};
                if (this->currentBlock != BlockState::World) throw ParseError(directive.source, std::format("{} is only valid after WorldBegin", name));
            }

            void RequireWorld(const SceneSourceLocation& source, const std::string_view name) const {
                if (this->currentBlock == BlockState::Options) throw PbrtSceneNotTopLevel{};
                if (this->currentBlock != BlockState::World) throw ParseError(source, std::format("{} is only valid after WorldBegin", name));
            }

            void ApplyActiveTransform(const PbrtSceneTransform& transform) {
                ApplyTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void SetActiveTransform(const PbrtSceneTransform& transform) {
                SetTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void ActiveTransform(const Token& token, const SceneSourceLocation& source) {
                if (token.kind != TokenKind::Word) throw ParseError(token.source, "ActiveTransform expects StartTime, EndTime, or All");
                if (token.text == "StartTime") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd = false;
                    return;
                }
                if (token.text == "EndTime") {
                    this->graphicsState.activeStart = false;
                    this->graphicsState.activeEnd = true;
                    return;
                }
                if (token.text == "All") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd = true;
                    return;
                }
                throw ParseError(source, std::format("Unknown ActiveTransform target \"{}\"", token.text));
            }

            void PopGraphicsState(const Token& directive) {
                if (this->stateStack.empty() || this->stackKinds.empty() || this->stackKinds.back() != 'a') throw ParseError(directive.source, std::format("{} without matching begin", directive.text));
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
            }

            void ReadBracketedMatrix(PbrtTokenStream& stream, const std::string_view context, std::array<float, 16>* values) const {
                Token open = RequireToken(stream, context);
                if (open.kind != TokenKind::LeftBracket) throw ParseError(open.source, std::format("{} expects '['", context));
                for (float& value : *values) value = ParseFloatToken(RequireToken(stream, context));
                Token close = RequireToken(stream, context);
                if (close.kind != TokenKind::RightBracket) throw ParseError(close.source, std::format("{} expects ']'", context));
            }

            void Transform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "Transform", &values);
                this->SetActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void ConcatTransform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "ConcatTransform", &values);
                this->ApplyActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void MediumInterface(PbrtTokenStream& stream) {
                static_cast<void>(RequireStringToken(stream, "MediumInterface"));
                std::optional<Token> outsideToken = stream.Next();
                if (!outsideToken.has_value()) return;
                if (outsideToken->kind == TokenKind::QuotedString) return;
                stream.PushBack(std::move(*outsideToken));
            }

            void ObjectBegin(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectBegin");
                if (this->activeObjectDefinition) throw ParseError(source, "ObjectBegin cannot be nested inside another ObjectBegin");
                this->stateStack.push_back(this->graphicsState);
                this->stackKinds.push_back('o');
                this->activeObjectDefinition = true;
                this->activeObjectDefinitionName = std::move(name);
            }

            void ObjectEnd(const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectEnd");
                if (!this->activeObjectDefinition) throw ParseError(source, "ObjectEnd without ObjectBegin");
                if (this->stateStack.empty() || this->stackKinds.empty() || this->stackKinds.back() != 'o') throw ParseError(source, "ObjectEnd does not match the current graphics state stack");
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
                this->activeObjectDefinition = false;
                this->activeObjectDefinitionName.clear();
            }

            void ObjectInstance(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectInstance");
                if (this->activeObjectDefinition) throw ParseError(source, "ObjectInstance cannot be used inside ObjectBegin");
                this->AddFeature(PbrtSceneProbeFeatureCategory::Shape, "objectinstance", std::move(name), source);
                this->AddTransformFeatureIfNeeded("object instance", source);
            }

            void Texture(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                static_cast<void>(RequireStringToken(stream, "Texture"));
                const std::string kind = RequireStringToken(stream, "Texture");
                const std::string type = RequireStringToken(stream, "Texture");
                if (kind != "float" && kind != "spectrum") throw ParseError(source, std::format("Texture has unsupported value type \"{}\"", kind));
                this->AddFeature(PbrtSceneProbeFeatureCategory::Texture, type, kind, source);
                this->AddTransformFeatureIfNeeded(std::format("texture \"{}\"", type), source);
                this->ParseProbeParameters(stream);
            }

            void Finish() const {
                if (!this->stateStack.empty()) throw std::runtime_error(std::format("{}: missing AttributeEnd/ObjectEnd for scene probe stack", this->report.source));
                if (this->activeObjectDefinition) throw std::runtime_error(std::format("{}: missing ObjectEnd", this->report.source));
                if (this->currentBlock != BlockState::World) throw PbrtSceneNotTopLevel{};
            }

            PbrtSceneProbeReport report{};
            const std::stop_token* stopToken{};
            std::filesystem::path inputFile;
            std::filesystem::path searchDirectory;
            GraphicsState graphicsState{};
            BlockState currentBlock{BlockState::Options};
            std::vector<GraphicsState> stateStack{};
            std::vector<char> stackKinds{};
            std::map<std::string, PbrtSceneTransformSet> namedCoordinateSystems{};
            bool activeObjectDefinition{false};
            std::string activeObjectDefinitionName{};
        };

        class PbrtSceneBuilder {
        public:
            explicit PbrtSceneBuilder(std::filesystem::path inputFile, const std::stop_token* stopToken = nullptr) : stopToken(stopToken), inputFile(std::filesystem::absolute(std::move(inputFile)).lexically_normal()), searchDirectory(this->inputFile.parent_path()) {
                CheckSceneStop(this->stopToken);
                this->scene.name   = this->inputFile.stem().string();
                this->scene.title  = this->inputFile.stem().string();
                this->scene.source = this->inputFile.string();

                const SceneSourceLocation source{.filename = this->inputFile.string(), .line = 1, .column = 1};
                this->SetDefaultEntitySources(source);
                this->scene.materials.push_back(PbrtSceneMaterial{
                    .name   = DefaultMaterialName,
                    .entity = PbrtSceneEntity{.type = "diffuse", .colorSpace = PbrtColorSpace::sRGB, .source = source},
                });
                this->materialNames.insert(DefaultMaterialName);
                this->namedCoordinateSystems["world"] = this->graphicsState.transform;
            }

            [[nodiscard]] PbrtSceneSnapshot Parse() {
                CheckSceneStop(this->stopToken);
                this->ParseFile(this->inputFile);
                CheckSceneStop(this->stopToken);
                this->Finish();
                return std::move(this->scene);
            }

        private:
            enum class BlockState { Options, World };

            void SetDefaultEntitySources(const SceneSourceLocation& source) {
                this->scene.renderSettings.filter.source      = source;
                this->scene.renderSettings.film.source        = source;
                this->scene.renderSettings.camera.source      = source;
                this->scene.renderSettings.sampler.source     = source;
                this->scene.renderSettings.integrator.source  = source;
                this->scene.renderSettings.accelerator.source = source;
            }

            [[nodiscard]] std::string ResolveResourcePath(const std::string& value) const {
                if (value.empty() || IsAbsolutePathString(value)) return value;
                return (this->searchDirectory / std::filesystem::path(value)).lexically_normal().string();
            }

            [[nodiscard]] std::filesystem::path ResolveIncludePath(const std::string& value, const SceneSourceLocation& source) const {
                if (value.empty()) throw ParseError(source, "Include filename must not be empty");
                const std::filesystem::path path = IsAbsolutePathString(value) ? std::filesystem::path(value) : this->searchDirectory / std::filesystem::path(value);
                return std::filesystem::absolute(path).lexically_normal();
            }

            void ResolveParameterPaths(std::vector<PbrtSceneParameter>* parameters, const EntityUse entityUse) const {
                for (PbrtSceneParameter& parameter : *parameters) {
                    CheckSceneStop(this->stopToken);
                    std::vector<std::string>* values = ParameterStringValues(&parameter);
                    if (values == nullptr) continue;
                    if (entityUse == EntityUse::Film && parameter.name == "filename") continue;

                    const bool directFileParameter = parameter.name == "filename" || parameter.name == "normalmap" || parameter.name == "lensfile" || parameter.name == "emissionfilename";
                    const bool apertureParameter   = parameter.name == "aperture";
                    const bool spectrumParameter   = parameter.type == "spectrum";
                    if (!directFileParameter && !apertureParameter && !spectrumParameter) continue;

                    for (std::string& value : *values) {
                        CheckSceneStop(this->stopToken);
                        if (value.empty()) continue;
                        if (apertureParameter && IsBuiltInApertureName(value)) continue;
                        if (spectrumParameter && !IsPathLike(value) && !std::filesystem::exists(this->searchDirectory / std::filesystem::path(value))) continue;
                        value = this->ResolveResourcePath(value);
                    }
                }
            }

            [[nodiscard]] std::vector<PbrtSceneParameter> MergeParameters(const std::vector<PbrtSceneParameter>& attributes, std::vector<PbrtSceneParameter> parameters, const EntityUse entityUse) const {
                std::vector<PbrtSceneParameter> merged;
                merged.reserve(attributes.size() + parameters.size());
                for (PbrtSceneParameter parameter : attributes) {
                    CheckSceneStop(this->stopToken);
                    parameter.mayBeUnused = true;
                    merged.push_back(std::move(parameter));
                }
                for (PbrtSceneParameter& parameter : parameters) {
                    CheckSceneStop(this->stopToken);
                    merged.push_back(std::move(parameter));
                }
                this->ResolveParameterPaths(&merged, entityUse);
                return merged;
            }

            [[nodiscard]] PbrtSceneEntity Entity(std::string type, std::vector<PbrtSceneParameter> parameters, const EntityUse entityUse, const SceneSourceLocation& source, const PbrtColorSpace colorSpace) const {
                this->ResolveParameterPaths(&parameters, entityUse);
                return PbrtSceneEntity{
                    .type       = std::move(type),
                    .parameters = std::move(parameters),
                    .colorSpace = colorSpace,
                    .source     = source,
                };
            }

            [[nodiscard]] PbrtSceneEntity EntityWithAttributes(std::string type, std::vector<PbrtSceneParameter> parameters, const std::vector<PbrtSceneParameter>& attributes, const EntityUse entityUse, const SceneSourceLocation& source, const PbrtColorSpace colorSpace) const {
                return PbrtSceneEntity{
                    .type       = std::move(type),
                    .parameters = this->MergeParameters(attributes, std::move(parameters), entityUse),
                    .colorSpace = colorSpace,
                    .source     = source,
                };
            }

            void ParseFile(const std::filesystem::path& path) {
                CheckSceneStop(this->stopToken);
                PbrtTokenStream stream(path, this->stopToken);
                while (std::optional<Token> directive = stream.Next()) {
                    CheckSceneStop(this->stopToken);
                    this->ParseDirective(stream, *directive);
                }
            }

            [[nodiscard]] std::vector<PbrtSceneParameter> ParseParameters(PbrtTokenStream& stream) {
                std::vector<PbrtSceneParameter> parameters;
                while (true) {
                    CheckSceneStop(this->stopToken);
                    std::optional<Token> declaration = stream.Next();
                    if (!declaration.has_value()) return parameters;
                    if (declaration->kind != TokenKind::QuotedString) {
                        stream.PushBack(std::move(*declaration));
                        return parameters;
                    }

                    PbrtSceneParameter parameter = this->ParseParameterDeclaration(*declaration);
                    Token value              = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                    if (value.kind == TokenKind::LeftBracket) {
                        while (true) {
                            CheckSceneStop(this->stopToken);
                            Token element = RequireToken(stream, std::format("parameter \"{} {}\"", parameter.type, parameter.name));
                            if (element.kind == TokenKind::RightBracket) break;
                            this->AppendParameterValue(&parameter, element);
                        }
                    } else {
                        this->AppendParameterValue(&parameter, value);
                    }
                    parameters.push_back(std::move(parameter));
                }
            }

            [[nodiscard]] PbrtSceneParameter ParseParameterDeclaration(const Token& declaration) const {
                const std::string& text = declaration.text;
                std::size_t typeBegin = 0;
                while (typeBegin < text.size() && std::isspace(static_cast<unsigned char>(text[typeBegin]))) ++typeBegin;
                if (typeBegin == text.size()) throw ParseError(declaration.source, "PBRT parameter declaration does not contain a type");

                std::size_t typeEnd = typeBegin;
                while (typeEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[typeEnd]))) ++typeEnd;

                std::size_t nameBegin = typeEnd;
                while (nameBegin < text.size() && std::isspace(static_cast<unsigned char>(text[nameBegin]))) ++nameBegin;
                if (nameBegin == text.size()) throw ParseError(declaration.source, std::format("\"{}\" does not contain a parameter name", text));

                std::size_t nameEnd = nameBegin;
                while (nameEnd < text.size() && !std::isspace(static_cast<unsigned char>(text[nameEnd]))) ++nameEnd;

                return PbrtSceneParameter{
                    .type       = text.substr(typeBegin, typeEnd - typeBegin),
                    .name       = text.substr(nameBegin, nameEnd - nameBegin),
                    .colorSpace = this->graphicsState.colorSpace,
                    .source     = declaration.source,
                };
            }

            void AppendParameterValue(PbrtSceneParameter* parameter, const Token& value) const {
                if (parameter->type == "integer") {
                    if (value.kind == TokenKind::QuotedString) throw ParseError(value.source, std::format("\"integer {}\" expects numeric values", parameter->name));
                    if (!std::holds_alternative<std::vector<int>>(parameter->values)) parameter->values = std::vector<int>{};
                    std::get<std::vector<int>>(parameter->values).push_back(ParseIntegerToken(value));
                    return;
                }
                if (parameter->type == "bool") {
                    if (!std::holds_alternative<std::vector<std::uint8_t>>(parameter->values)) parameter->values = std::vector<std::uint8_t>{};
                    std::get<std::vector<std::uint8_t>>(parameter->values).push_back(ParseBoolToken(value));
                    return;
                }
                if (parameter->type == "string" || parameter->type == "texture" || value.kind == TokenKind::QuotedString) {
                    if (value.kind != TokenKind::QuotedString) throw ParseError(value.source, std::format("\"{} {}\" expects quoted string values", parameter->type, parameter->name));
                    if (!std::holds_alternative<std::vector<std::string>>(parameter->values)) parameter->values = std::vector<std::string>{};
                    std::get<std::vector<std::string>>(parameter->values).push_back(value.text);
                    return;
                }

                if (!std::holds_alternative<std::vector<float>>(parameter->values)) parameter->values = std::vector<float>{};
                std::get<std::vector<float>>(parameter->values).push_back(ParseFloatToken(value));
            }

            void ParseDirective(PbrtTokenStream& stream, const Token& directive) {
                CheckSceneStop(this->stopToken);
                if (directive.kind != TokenKind::Word) throw ParseError(directive.source, "PBRT directive must be an unquoted identifier");

                if (directive.text == "AttributeBegin" || directive.text == "TransformBegin") {
                    this->RequireWorld(directive, directive.text);
                    this->stateStack.push_back(this->graphicsState);
                    this->stackKinds.push_back('a');
                    return;
                }
                if (directive.text == "AttributeEnd" || directive.text == "TransformEnd") {
                    this->RequireWorld(directive, directive.text);
                    this->PopGraphicsState(directive);
                    return;
                }
                if (directive.text == "ActiveTransform") {
                    this->ActiveTransform(RequireToken(stream, "ActiveTransform"), directive.source);
                    return;
                }
                if (directive.text == "AreaLightSource") {
                    this->RequireWorld(directive, "AreaLightSource");
                    const std::string type = RequireStringToken(stream, "AreaLightSource");
                    this->graphicsState.areaLight = PbrtSceneAreaLight{.entity = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::AreaLight, directive.source, this->graphicsState.colorSpace)};
                    return;
                }
                if (directive.text == "Accelerator") {
                    this->RequireOptions(directive, "Accelerator");
                    const std::string type = RequireStringToken(stream, "Accelerator");
                    this->scene.renderSettings.accelerator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Attribute") {
                    std::string target = RequireStringToken(stream, "Attribute");
                    std::vector<PbrtSceneParameter> parameters = this->ParseParameters(stream);
                    this->Attribute(std::move(target), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Camera") {
                    this->RequireOptions(directive, "Camera");
                    const std::string type = RequireStringToken(stream, "Camera");
                    this->scene.renderSettings.camera          = this->Entity(type, this->ParseParameters(stream), EntityUse::Camera, directive.source, this->graphicsState.colorSpace);
                    this->scene.renderSettings.cameraTransform = this->WorldFromCameraTransform();
                    this->scene.renderSettings.cameraMedium    = this->graphicsState.mediumInterface.outside;
                    this->namedCoordinateSystems["camera"]     = this->scene.renderSettings.cameraTransform;
                    return;
                }
                if (directive.text == "PbrtColorSpace") {
                    this->graphicsState.colorSpace = ParseColorSpaceName(RequireStringToken(stream, "PbrtColorSpace"), directive.source);
                    return;
                }
                if (directive.text == "ConcatTransform") {
                    this->ConcatTransform(stream, directive.source);
                    return;
                }
                if (directive.text == "CoordinateSystem") {
                    this->namedCoordinateSystems[RequireStringToken(stream, "CoordinateSystem")] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "CoordSysTransform") {
                    const std::string name = RequireStringToken(stream, "CoordSysTransform");
                    const std::map<std::string, PbrtSceneTransformSet>::const_iterator iter = this->namedCoordinateSystems.find(name);
                    if (iter == this->namedCoordinateSystems.end()) throw ParseError(directive.source, std::format("Unknown coordinate system \"{}\"", name));
                    this->graphicsState.transform = iter->second;
                    return;
                }
                if (directive.text == "Film") {
                    this->RequireOptions(directive, "Film");
                    const std::string type = RequireStringToken(stream, "Film");
                    this->scene.renderSettings.film = this->Entity(type, this->ParseParameters(stream), EntityUse::Film, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Identity") {
                    this->SetActiveTransform(PbrtSceneTransform{});
                    return;
                }
                if (directive.text == "Import") {
                    this->RequireWorld(directive, "Import");
                    this->Import(stream, directive.source);
                    return;
                }
                if (directive.text == "Include") {
                    stream.PushFile(this->ResolveIncludePath(RequireStringToken(stream, "Include"), directive.source));
                    return;
                }
                if (directive.text == "Integrator") {
                    this->RequireOptions(directive, "Integrator");
                    const std::string type = RequireStringToken(stream, "Integrator");
                    this->scene.renderSettings.integrator = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "LightSource") {
                    this->RequireWorld(directive, "LightSource");
                    this->LightSource(stream, directive.source);
                    return;
                }
                if (directive.text == "LookAt") {
                    std::array<float, 9> values{};
                    for (float& value : values) value = ParseFloatToken(RequireToken(stream, "LookAt"));
                    this->ApplyActiveTransform(LookAt(Vector3{values[0], values[1], values[2]}, Vector3{values[3], values[4], values[5]}, Vector3{values[6], values[7], values[8]}));
                    return;
                }
                if (directive.text == "MakeNamedMaterial") {
                    this->RequireWorld(directive, "MakeNamedMaterial");
                    std::string name = RequireStringToken(stream, "MakeNamedMaterial");
                    std::vector<PbrtSceneParameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMaterial(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MakeNamedMedium") {
                    std::string name = RequireStringToken(stream, "MakeNamedMedium");
                    std::vector<PbrtSceneParameter> parameters = this->ParseParameters(stream);
                    this->MakeNamedMedium(std::move(name), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Material") {
                    this->RequireWorld(directive, "Material");
                    std::string type = RequireStringToken(stream, "Material");
                    std::vector<PbrtSceneParameter> parameters = this->ParseParameters(stream);
                    this->Material(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "MediumInterface") {
                    this->MediumInterface(stream);
                    return;
                }
                if (directive.text == "NamedMaterial") {
                    this->RequireWorld(directive, "NamedMaterial");
                    this->graphicsState.currentMaterialName = RequireStringToken(stream, "NamedMaterial");
                    return;
                }
                if (directive.text == "ObjectBegin") {
                    this->ObjectBegin(RequireStringToken(stream, "ObjectBegin"), directive.source);
                    return;
                }
                if (directive.text == "ObjectEnd") {
                    this->ObjectEnd(directive.source);
                    return;
                }
                if (directive.text == "ObjectInstance") {
                    this->ObjectInstance(RequireStringToken(stream, "ObjectInstance"), directive.source);
                    return;
                }
                if (directive.text == "Option") {
                    std::string name = RequireStringToken(stream, "Option");
                    Token value = RequireToken(stream, "Option");
                    this->Option(std::move(name), std::move(value.text), directive.source);
                    return;
                }
                if (directive.text == "PixelFilter") {
                    this->RequireOptions(directive, "PixelFilter");
                    const std::string type = RequireStringToken(stream, "PixelFilter");
                    this->scene.renderSettings.filter = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "ReverseOrientation") {
                    this->RequireWorld(directive, "ReverseOrientation");
                    this->graphicsState.reverseOrientation = !this->graphicsState.reverseOrientation;
                    return;
                }
                if (directive.text == "Rotate") {
                    const float angle = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float x     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float y     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    const float z     = ParseFloatToken(RequireToken(stream, "Rotate"));
                    this->ApplyActiveTransform(Rotate(angle, Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "Sampler") {
                    this->RequireOptions(directive, "Sampler");
                    const std::string type = RequireStringToken(stream, "Sampler");
                    this->scene.renderSettings.sampler = this->Entity(type, this->ParseParameters(stream), EntityUse::Generic, directive.source, this->graphicsState.colorSpace);
                    return;
                }
                if (directive.text == "Scale") {
                    const float x = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float y = ParseFloatToken(RequireToken(stream, "Scale"));
                    const float z = ParseFloatToken(RequireToken(stream, "Scale"));
                    this->ApplyActiveTransform(Scale(x, y, z));
                    return;
                }
                if (directive.text == "Shape") {
                    this->RequireWorld(directive, "Shape");
                    std::string type = RequireStringToken(stream, "Shape");
                    std::vector<PbrtSceneParameter> parameters = this->ParseParameters(stream);
                    this->Shape(std::move(type), std::move(parameters), directive.source);
                    return;
                }
                if (directive.text == "Texture") {
                    this->RequireWorld(directive, "Texture");
                    this->Texture(stream, directive.source);
                    return;
                }
                if (directive.text == "Transform") {
                    this->Transform(stream, directive.source);
                    return;
                }
                if (directive.text == "TransformTimes") {
                    this->RequireOptions(directive, "TransformTimes");
                    this->graphicsState.transform.startTime = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    this->graphicsState.transform.endTime   = ParseFloatToken(RequireToken(stream, "TransformTimes"));
                    return;
                }
                if (directive.text == "Translate") {
                    const float x = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float y = ParseFloatToken(RequireToken(stream, "Translate"));
                    const float z = ParseFloatToken(RequireToken(stream, "Translate"));
                    this->ApplyActiveTransform(Translate(Vector3{x, y, z}));
                    return;
                }
                if (directive.text == "WorldBegin") {
                    this->RequireOptions(directive, "WorldBegin");
                    const float startTime = this->graphicsState.transform.startTime;
                    const float endTime   = this->graphicsState.transform.endTime;
                    this->currentBlock = BlockState::World;
                    this->graphicsState.transform   = PbrtSceneTransformSet{.startTime = startTime, .endTime = endTime};
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    this->namedCoordinateSystems["world"] = this->graphicsState.transform;
                    return;
                }
                if (directive.text == "WorldEnd") throw ParseError(directive.source, "WorldEnd is not used by PBRT v4 scene files");
                throw ParseError(directive.source, std::format("Unknown PBRT directive \"{}\"", directive.text));
            }

            void RequireOptions(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::Options) throw ParseError(directive.source, std::format("{} is only valid before WorldBegin", name));
            }

            void RequireWorld(const Token& directive, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(directive.source, std::format("{} is only valid after WorldBegin", name));
            }

            void RequireWorld(const SceneSourceLocation& source, const std::string_view name) const {
                if (this->currentBlock != BlockState::World) throw ParseError(source, std::format("{} is only valid after WorldBegin", name));
            }

            void ApplyActiveTransform(const PbrtSceneTransform& transform) {
                ApplyTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void SetActiveTransform(const PbrtSceneTransform& transform) {
                SetTransform(&this->graphicsState.transform, transform, this->graphicsState.activeStart, this->graphicsState.activeEnd);
            }

            void ActiveTransform(const Token& token, const SceneSourceLocation& source) {
                if (token.kind != TokenKind::Word) throw ParseError(token.source, "ActiveTransform expects StartTime, EndTime, or All");
                if (token.text == "StartTime") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = false;
                    return;
                }
                if (token.text == "EndTime") {
                    this->graphicsState.activeStart = false;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                if (token.text == "All") {
                    this->graphicsState.activeStart = true;
                    this->graphicsState.activeEnd   = true;
                    return;
                }
                throw ParseError(source, std::format("Unknown ActiveTransform target \"{}\"", token.text));
            }

            [[nodiscard]] PbrtSceneTransformSet WorldFromCameraTransform() const {
                PbrtSceneTransformSet result{
                    .start     = Inverse(this->graphicsState.transform.start),
                    .end       = Inverse(this->graphicsState.transform.end),
                    .startTime = this->graphicsState.transform.startTime,
                    .endTime   = this->graphicsState.transform.endTime,
                };
                RefreshAnimatedFlag(&result);
                return result;
            }

            void ReadBracketedMatrix(PbrtTokenStream& stream, const std::string_view context, std::array<float, 16>* values) const {
                Token open = RequireToken(stream, context);
                if (open.kind != TokenKind::LeftBracket) throw ParseError(open.source, std::format("{} expects '['", context));
                for (float& value : *values) value = ParseFloatToken(RequireToken(stream, context));
                Token close = RequireToken(stream, context);
                if (close.kind != TokenKind::RightBracket) throw ParseError(close.source, std::format("{} expects ']'", context));
            }

            void Transform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "Transform", &values);
                this->SetActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void ConcatTransform(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::array<float, 16> values{};
                this->ReadBracketedMatrix(stream, "ConcatTransform", &values);
                this->ApplyActiveTransform(TransformFromPbrtMatrix(values, source));
            }

            void PopGraphicsState(const Token& directive) {
                if (this->stateStack.empty()) throw ParseError(directive.source, std::format("Unmatched {}", directive.text));
                if (this->stackKinds.empty() || this->stackKinds.back() != 'a') throw ParseError(directive.source, std::format("{} does not match the current graphics state stack", directive.text));
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
            }

            void Attribute(std::string target, std::vector<PbrtSceneParameter> parameters, const SceneSourceLocation& source) {
                std::vector<PbrtSceneParameter>* currentAttributes = nullptr;
                if (target == "shape")
                    currentAttributes = &this->graphicsState.shapeAttributes;
                else if (target == "light")
                    currentAttributes = &this->graphicsState.lightAttributes;
                else if (target == "material")
                    currentAttributes = &this->graphicsState.materialAttributes;
                else if (target == "medium")
                    currentAttributes = &this->graphicsState.mediumAttributes;
                else if (target == "texture")
                    currentAttributes = &this->graphicsState.textureAttributes;
                else
                    throw ParseError(source, std::format("Unknown Attribute target \"{}\"", target));

                for (PbrtSceneParameter& parameter : parameters) {
                    parameter.mayBeUnused = true;
                    parameter.colorSpace  = this->graphicsState.colorSpace;
                    currentAttributes->push_back(std::move(parameter));
                }
            }

            void Option(std::string name, std::string value, const SceneSourceLocation& source) {
                this->scene.renderSettings.options.push_back(PbrtSceneOption{
                    .name   = std::move(name),
                    .value  = std::move(value),
                    .source = source,
                });
            }

            void MediumInterface(PbrtTokenStream& stream) {
                const std::string inside = RequireStringToken(stream, "MediumInterface");
                std::optional<Token> outsideToken = stream.Next();
                if (!outsideToken.has_value()) {
                    this->graphicsState.mediumInterface = PbrtSceneMediumInterface{.inside = inside, .outside = inside};
                    return;
                }
                if (outsideToken->kind == TokenKind::QuotedString) {
                    this->graphicsState.mediumInterface = PbrtSceneMediumInterface{.inside = inside, .outside = std::move(outsideToken->text)};
                    return;
                }
                stream.PushBack(std::move(*outsideToken));
                this->graphicsState.mediumInterface = PbrtSceneMediumInterface{.inside = inside, .outside = inside};
            }

            void MakeNamedMedium(std::string name, std::vector<PbrtSceneParameter> parameters, const SceneSourceLocation& source) {
                this->RequireUniqueName(this->mediumNames, "medium", name, source);
                PbrtSceneEntity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.mediumAttributes, EntityUse::Medium, source, this->graphicsState.colorSpace);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMedium \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->mediumNames.insert(name);
                this->scene.media.push_back(PbrtSceneMedium{
                    .name      = std::move(name),
                    .entity    = std::move(entity),
                    .transform = this->graphicsState.transform,
                });
            }

            void LightSource(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                const std::string type = RequireStringToken(stream, "LightSource");
                this->scene.lights.push_back(PbrtSceneLight{
                    .name      = std::format("__light_{}", this->scene.lights.size()),
                    .entity    = this->EntityWithAttributes(type, this->ParseParameters(stream), this->graphicsState.lightAttributes, EntityUse::Light, source, this->graphicsState.colorSpace),
                    .transform = this->graphicsState.transform,
                    .medium    = this->graphicsState.mediumInterface.outside,
                });
            }

            void Material(std::string type, std::vector<PbrtSceneParameter> parameters, const SceneSourceLocation& source) {
                if (type.empty()) type = "interface";
                const std::string name = std::format("__inline_material_{}", this->inlineMaterialCount);
                ++this->inlineMaterialCount;
                this->scene.materials.push_back(PbrtSceneMaterial{
                    .name   = name,
                    .entity = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.colorSpace),
                });
                this->materialNames.insert(name);
                this->graphicsState.currentMaterialName = name;
            }

            void MakeNamedMaterial(std::string name, std::vector<PbrtSceneParameter> parameters, const SceneSourceLocation& source) {
                this->RequireUniqueName(this->materialNames, "material", name, source);
                PbrtSceneEntity entity = this->EntityWithAttributes("", std::move(parameters), this->graphicsState.materialAttributes, EntityUse::Material, source, this->graphicsState.colorSpace);
                const std::string type = OneStringParameter(entity.parameters, "type", "");
                if (type.empty()) throw ParseError(source, std::format("MakeNamedMaterial \"{}\" requires \"string type\"", name));
                entity.type = type;
                this->materialNames.insert(name);
                this->scene.materials.push_back(PbrtSceneMaterial{
                    .name   = std::move(name),
                    .entity = std::move(entity),
                });
            }

            void Texture(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                std::string name = RequireStringToken(stream, "Texture");
                std::string kind = RequireStringToken(stream, "Texture");
                std::string type = RequireStringToken(stream, "Texture");
                if (kind != "float" && kind != "spectrum") throw ParseError(source, std::format("Texture \"{}\" has unsupported value type \"{}\"", name, kind));
                this->RequireUniqueName(kind == "float" ? this->floatTextureNames : this->spectrumTextureNames, "texture", name, source);
                if (kind == "float")
                    this->floatTextureNames.insert(name);
                else
                    this->spectrumTextureNames.insert(name);
                this->scene.textures.push_back(PbrtSceneTexture{
                    .name      = std::move(name),
                    .kind      = std::move(kind),
                    .entity    = this->EntityWithAttributes(std::move(type), this->ParseParameters(stream), this->graphicsState.textureAttributes, EntityUse::Texture, source, this->graphicsState.colorSpace),
                    .transform = this->graphicsState.transform,
                });
            }

            void Shape(std::string type, std::vector<PbrtSceneParameter> parameters, const SceneSourceLocation& source) {
                PbrtSceneShape shape{
                    .name               = std::format("__shape_{}", this->shapeCount),
                    .entity             = this->EntityWithAttributes(std::move(type), std::move(parameters), this->graphicsState.shapeAttributes, EntityUse::Shape, source, this->graphicsState.colorSpace),
                    .transform          = this->graphicsState.transform,
                    .reverseOrientation = this->graphicsState.reverseOrientation,
                    .materialName       = this->graphicsState.currentMaterialName,
                    .areaLight          = this->graphicsState.areaLight,
                    .mediumInterface    = this->graphicsState.mediumInterface,
                };
                ++this->shapeCount;

                if (this->activeObjectDefinition.has_value())
                    this->activeObjectDefinition->shapes.push_back(std::move(shape));
                else
                    this->scene.shapes.push_back(std::move(shape));
            }

            void ObjectBegin(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectBegin");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectBegin cannot be nested inside another ObjectBegin");
                this->RequireUniqueName(this->objectDefinitionNames, "object definition", name, source);
                this->stateStack.push_back(this->graphicsState);
                this->stackKinds.push_back('o');
                this->objectDefinitionNames.insert(name);
                this->activeObjectDefinition = PbrtSceneObjectDefinition{.name = std::move(name), .source = source};
            }

            void ObjectEnd(const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectEnd");
                if (!this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectEnd without ObjectBegin");
                if (this->stateStack.empty() || this->stackKinds.empty() || this->stackKinds.back() != 'o') throw ParseError(source, "ObjectEnd does not match the current graphics state stack");
                this->graphicsState = std::move(this->stateStack.back());
                this->stateStack.pop_back();
                this->stackKinds.pop_back();
                this->scene.objectDefinitions.push_back(std::move(*this->activeObjectDefinition));
                this->activeObjectDefinition.reset();
            }

            void ObjectInstance(std::string name, const SceneSourceLocation& source) {
                this->RequireWorld(source, "ObjectInstance");
                if (this->activeObjectDefinition.has_value()) throw ParseError(source, "ObjectInstance cannot be used inside ObjectBegin");
                this->scene.objectInstances.push_back(PbrtSceneObjectInstance{
                    .name           = std::format("__instance_{}", this->scene.objectInstances.size()),
                    .definitionName = std::move(name),
                    .transform      = this->graphicsState.transform,
                    .source         = source,
                });
            }

            void Import(PbrtTokenStream& stream, const SceneSourceLocation& source) {
                const std::filesystem::path importPath = this->ResolveIncludePath(RequireStringToken(stream, "Import"), source);
                const GraphicsState savedState         = this->graphicsState;
                this->ParseFile(importPath);
                this->graphicsState = savedState;
            }

            void RequireUniqueName(std::set<std::string>& names, const std::string_view kind, const std::string& name, const SceneSourceLocation& source) {
                if (name.empty()) throw ParseError(source, std::format("PBRT {} name must not be empty", kind));
                if (!names.insert(name).second) throw ParseError(source, std::format("PBRT {} \"{}\" is already defined", kind, name));
            }

            void Finish() const {
                if (!this->stateStack.empty()) throw std::runtime_error(std::format("{}: missing AttributeEnd/ObjectEnd for scene parser stack", this->scene.source));
                if (this->activeObjectDefinition.has_value()) throw std::runtime_error(std::format("{}: missing ObjectEnd", this->scene.source));
            }

            PbrtSceneSnapshot scene{};
            const std::stop_token* stopToken{};
            std::filesystem::path inputFile;
            std::filesystem::path searchDirectory;
            GraphicsState graphicsState{};
            BlockState currentBlock{BlockState::Options};
            std::vector<GraphicsState> stateStack{};
            std::vector<char> stackKinds{};
            std::map<std::string, PbrtSceneTransformSet> namedCoordinateSystems{};
            std::optional<PbrtSceneObjectDefinition> activeObjectDefinition{};
            std::set<std::string> materialNames{};
            std::set<std::string> mediumNames{};
            std::set<std::string> floatTextureNames{};
            std::set<std::string> spectrumTextureNames{};
            std::set<std::string> objectDefinitionNames{};
            std::size_t inlineMaterialCount{};
            std::size_t shapeCount{};
        };

        [[nodiscard]] std::filesystem::path SceneRoot() {
            return std::filesystem::absolute(std::filesystem::path(SPECTRA_SCENES_ROOT)).lexically_normal();
        }

        [[nodiscard]] std::string PathId(std::filesystem::path path) {
            return path.generic_string();
        }

        [[nodiscard]] std::filesystem::path PbrtSceneFilenameStem(const std::filesystem::path& path) {
            std::filesystem::path filename = path.filename();
            if (HasExtension(filename, ".gz")) filename = filename.stem();
            if (HasExtension(filename, ".pbrt")) filename = filename.stem();
            return filename;
        }

        [[nodiscard]] std::string SceneDisplayName(const std::filesystem::path& relativePath) {
            return PbrtSceneFilenameStem(relativePath).string();
        }

        [[nodiscard]] std::string SceneGroupName(const std::filesystem::path& relativePath) {
            if (relativePath.empty()) return "scene";
            const std::filesystem::path::iterator begin = relativePath.begin();
            if (begin == relativePath.end()) return "scene";
            return begin->string();
        }

        void RefreshCatalogCounts(PbrtSceneCatalog* catalog) {
            catalog->pending_count     = 0;
            catalog->candidate_count   = 0;
            catalog->non_scene_count   = 0;
            catalog->invalid_count     = 0;
            for (const PbrtSceneCatalogEntry& entry : catalog->entries) {
                switch (entry.state) {
                case PbrtSceneCatalogEntryState::Pending: ++catalog->pending_count; break;
                case PbrtSceneCatalogEntryState::Candidate: ++catalog->candidate_count; break;
                case PbrtSceneCatalogEntryState::NonScene: ++catalog->non_scene_count; break;
                case PbrtSceneCatalogEntryState::Invalid: ++catalog->invalid_count; break;
                }
            }
        }

        [[nodiscard]] std::filesystem::path ResolveScenePathByUniqueStem(const std::filesystem::path& root, const std::string& name) {
            std::optional<std::filesystem::path> match;
            if (!std::filesystem::exists(root)) throw std::runtime_error(std::format("{}: scene root does not exist", root.string()));
            for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(root)) {
                if (!entry.is_regular_file()) continue;
                const std::filesystem::path path = entry.path();
                if (!IsPbrtSceneFile(path)) continue;
                if (PbrtSceneFilenameStem(path).string() != name) continue;
                if (match.has_value()) throw std::runtime_error(std::format("Scene alias \"{}\" is ambiguous; pass a scene-root-relative .pbrt path", name));
                match = path;
            }
            if (match.has_value()) return std::filesystem::absolute(*match).lexically_normal();
            return {};
        }

        [[nodiscard]] std::filesystem::path ResolveScenePath(const std::string_view requestedName) {
            const std::string requested(requestedName);
            const std::filesystem::path root = SceneRoot();
            if (requested == "default") return (root / "pbrt-book" / "book.pbrt").lexically_normal();

            const std::filesystem::path asPath(requested);
            if (std::filesystem::is_regular_file(asPath)) return std::filesystem::absolute(asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / asPath)) return std::filesystem::absolute(root / asPath).lexically_normal();
            if (std::filesystem::is_regular_file(root / (requested + ".pbrt"))) return std::filesystem::absolute(root / (requested + ".pbrt")).lexically_normal();
            if (std::filesystem::is_regular_file(root / requested / (requested + ".pbrt"))) return std::filesystem::absolute(root / requested / (requested + ".pbrt")).lexically_normal();

            const std::filesystem::path uniqueStem = ResolveScenePathByUniqueStem(root, requested);
            if (!uniqueStem.empty()) return uniqueStem;

            throw std::runtime_error(std::format("Unknown Spectra scene \"{}\".", requested));
        }

        constexpr std::size_t PbrtCatalogBackgroundWorkerCount = 2u;
    } // namespace

    PbrtSceneWorkspace::PbrtSceneWorkspace(PbrtSceneSnapshot snapshot) {
        if (snapshot.revision.value == 0) snapshot.revision = SceneRevision{1};
        this->currentSnapshot = std::make_shared<PbrtSceneSnapshot>(std::move(snapshot));
    }

    bool PbrtSceneWorkspace::loaded() const {
        return this->currentSnapshot != nullptr;
    }

    std::shared_ptr<const PbrtSceneSnapshot> PbrtSceneWorkspace::snapshot() const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("PBRT scene workspace does not contain a loaded snapshot");
        return this->currentSnapshot;
    }

    PbrtSceneEditBatch PbrtSceneWorkspace::replace_snapshot(PbrtSceneSnapshot snapshot) {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot edit an unloaded PBRT scene workspace");

        PbrtSceneSnapshot next = std::move(snapshot);
        const SceneRevision before_revision = this->currentSnapshot->revision;
        next.revision = SceneRevision{before_revision.value + 1};
        this->currentSnapshot = std::make_shared<PbrtSceneSnapshot>(std::move(next));

        PbrtSceneEditBatch batch = this->fullEdit(before_revision);
        this->lastEdit = batch;
        return batch;
    }

    PbrtSceneEditBatch PbrtSceneWorkspace::changes_since(const SceneRevision revision) const {
        if (this->currentSnapshot == nullptr) throw std::runtime_error("Cannot query PBRT scene changes from an unloaded workspace");
        if (revision == this->currentSnapshot->revision) {
            return PbrtSceneEditBatch{
                .beforeRevision = revision,
                .afterRevision  = revision,
                .dirty          = PbrtSceneDirtyFlags::None,
            };
        }
        if (revision.value == 0) return this->fullEdit(revision);
        if (this->lastEdit.has_value() && this->lastEdit->beforeRevision == revision) return *this->lastEdit;
        throw std::runtime_error("PBRT scene edit history for the requested revision is unavailable");
    }

    PbrtSceneEditBatch PbrtSceneWorkspace::fullEdit(const SceneRevision before) const {
        return PbrtSceneEditBatch{
            .beforeRevision = before,
            .afterRevision  = this->currentSnapshot->revision,
            .dirty          = PbrtSceneDirtyFlags::Snapshot,
        };
    }

    PbrtSceneInfo DescribeScene(const PbrtSceneSnapshot& scene) {
        const auto one_float_parameter = [](const std::vector<PbrtSceneParameter>& parameters, const std::string& name, const float default_value) {
            for (const PbrtSceneParameter& parameter : parameters) {
                if (parameter.type != "float" && parameter.type != "integer") continue;
                if (parameter.name != name) continue;
                if (parameter.type == "float") {
                    const std::vector<float>* values = std::get_if<std::vector<float>>(&parameter.values);
                    if (values == nullptr || values->empty()) throw std::runtime_error(std::format("PBRT parameter \"{}\" must contain at least one float value", name));
                    return values->front();
                }
                const std::vector<int>* values = std::get_if<std::vector<int>>(&parameter.values);
                if (values == nullptr || values->empty()) throw std::runtime_error(std::format("PBRT parameter \"{}\" must contain at least one integer value", name));
                return static_cast<float>(values->front());
            }
            return default_value;
        };

        std::size_t definition_shape_count = 0;
        std::size_t definition_area_light_count = 0;
        for (const PbrtSceneObjectDefinition& definition : scene.objectDefinitions) {
            definition_shape_count += definition.shapes.size();
            for (const PbrtSceneShape& shape : definition.shapes)
                if (shape.areaLight.has_value()) ++definition_area_light_count;
        }

        std::size_t area_light_count = definition_area_light_count;
        for (const PbrtSceneShape& shape : scene.shapes)
            if (shape.areaLight.has_value()) ++area_light_count;

        std::size_t infinite_light_count = 0;
        for (const PbrtSceneLight& light : scene.lights)
            if (light.entity.type == "infinite") ++infinite_light_count;

        const float camera_fov = one_float_parameter(scene.renderSettings.camera.parameters, "fov", scene.renderSettings.camera.type == "perspective" ? 90.0f : 45.0f);
        if (!(camera_fov > 0.0f && camera_fov < 180.0f)) throw std::runtime_error(std::format("PBRT scene \"{}\" has invalid camera FOV {}", scene.name, camera_fov));

        return PbrtSceneInfo{
            .name                    = scene.name,
            .title                   = scene.title,
            .camera                  = scene.renderSettings.camera.type,
            .sampler                 = scene.renderSettings.sampler.type,
            .integrator              = scene.renderSettings.integrator.type,
            .accelerator             = scene.renderSettings.accelerator.type,
            .shape_count             = scene.shapes.size() + definition_shape_count,
            .material_count          = scene.materials.size(),
            .texture_count           = scene.textures.size(),
            .medium_count            = scene.media.size(),
            .light_count             = scene.lights.size(),
            .area_light_count        = area_light_count,
            .infinite_light_count    = infinite_light_count,
            .object_definition_count = scene.objectDefinitions.size(),
            .object_instance_count   = scene.objectInstances.size(),
            .camera_fov_degrees      = camera_fov,
        };
    }

    PbrtSceneBrowserSession::PbrtSceneBrowserSession(std::string initial_scene_id) : currentWorkspace(std::make_shared<PbrtSceneWorkspace>()) {
        if (initial_scene_id.empty()) throw std::runtime_error("PBRT catalog session initial scene id must not be empty");
        this->catalog = DiscoverPbrtSceneCatalog();
        this->catalogProbeClaimed.assign(this->catalog.entries.size(), false);
        const std::vector<PbrtSceneCatalogEntry>::iterator active_scene_iter = std::ranges::find_if(this->catalog.entries, [&initial_scene_id](const PbrtSceneCatalogEntry& entry) { return entry.id == initial_scene_id; });
        if (active_scene_iter == this->catalog.entries.end()) throw std::runtime_error(std::format("PBRT scene catalog does not contain required initial scene \"{}\"", initial_scene_id));
        ProbePbrtSceneCatalogEntry(*active_scene_iter);
        if (active_scene_iter->state == PbrtSceneCatalogEntryState::Invalid) throw std::runtime_error(std::format("PBRT initial scene \"{}\" is not probeable: {}", initial_scene_id, active_scene_iter->issues.empty() ? "no diagnostics" : active_scene_iter->issues.front().message));
        if (active_scene_iter->state == PbrtSceneCatalogEntryState::NonScene) throw std::runtime_error(std::format("PBRT initial scene \"{}\" is not a top-level scene", initial_scene_id));
        if (active_scene_iter->state != PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("PBRT initial scene \"{}\" did not produce a candidate scene probe", initial_scene_id));
        PbrtSceneSnapshot initial_scene = ParsePbrtSceneCatalogEntry(*active_scene_iter);
        active_scene_iter->revision = initial_scene.revision;
        if (active_scene_iter->probe.has_value()) active_scene_iter->probe->revision = initial_scene.revision;
        active_scene_iter->issues.clear();
        this->activeSceneIndex = static_cast<std::size_t>(std::distance(this->catalog.entries.begin(), active_scene_iter));
        this->selectedSceneIndex = this->activeSceneIndex;
        *this->currentWorkspace = PbrtSceneWorkspace{std::move(initial_scene)};
        this->refresh_catalog_counts();
    }

    PbrtSceneBrowserSession::~PbrtSceneBrowserSession() noexcept {
        this->stop_background_probe_workers();
    }

    void PbrtSceneBrowserSession::start_background_probe_workers() {
        if (!this->backgroundWorkers.empty()) throw std::runtime_error("PBRT catalog background probe workers are already running");
        {
            std::scoped_lock lock{this->catalogMutex};
            this->catalogProbeClaimed.assign(this->catalog.entries.size(), false);
        }
        this->backgroundWorkers.reserve(PbrtCatalogBackgroundWorkerCount);
        for (std::size_t worker_index = 0; worker_index < PbrtCatalogBackgroundWorkerCount; ++worker_index) {
            this->backgroundWorkers.emplace_back([this](const std::stop_token stop_token) { this->run_background_probe_worker(stop_token); });
        }
        this->backgroundCondition.notify_all();
    }

    void PbrtSceneBrowserSession::stop_background_probe_workers() noexcept {
        for (std::jthread& worker : this->backgroundWorkers) worker.request_stop();
        this->backgroundCondition.notify_all();
        for (std::jthread& worker : this->backgroundWorkers) {
            if (worker.joinable()) worker.join();
        }
        this->backgroundWorkers.clear();
        std::scoped_lock lock{this->catalogMutex};
        this->catalogProbeClaimed.assign(this->catalog.entries.size(), false);
    }

    void PbrtSceneBrowserSession::stop_background_probe_workers_if_idle() noexcept {
        bool should_stop = false;
        {
            std::scoped_lock lock{this->catalogMutex};
            should_stop = !this->backgroundWorkers.empty() && this->catalog.pending_count == 0;
        }
        if (should_stop) this->stop_background_probe_workers();
    }

    std::shared_ptr<PbrtSceneWorkspace> PbrtSceneBrowserSession::workspace() const {
        return this->currentWorkspace;
    }

    PbrtSceneCatalog PbrtSceneBrowserSession::catalog_snapshot() const {
        std::scoped_lock lock{this->catalogMutex};
        return this->catalog;
    }

    std::size_t PbrtSceneBrowserSession::active_scene_index() const {
        std::scoped_lock lock{this->catalogMutex};
        return this->activeSceneIndex;
    }

    std::size_t PbrtSceneBrowserSession::selected_scene_index() const {
        std::scoped_lock lock{this->catalogMutex};
        return this->selectedSceneIndex;
    }

    void PbrtSceneBrowserSession::select_scene(const std::size_t scene_index) {
        std::scoped_lock lock{this->catalogMutex};
        if (scene_index >= this->catalog.entries.size()) throw std::runtime_error("PBRT scene selection index is out of range");
        this->selectedSceneIndex = scene_index;
    }

    PbrtSceneSnapshot PbrtSceneBrowserSession::parse_selected_scene() const {
        std::size_t selected_scene_index{};
        {
            std::scoped_lock lock{this->catalogMutex};
            selected_scene_index = this->selectedSceneIndex;
        }
        return this->parse_scene(selected_scene_index);
    }

    PbrtSceneEditBatch PbrtSceneBrowserSession::commit_selected_scene(PbrtSceneSnapshot snapshot) {
        std::size_t selected_scene_index{};
        {
            std::scoped_lock lock{this->catalogMutex};
            selected_scene_index = this->selectedSceneIndex;
        }
        return this->commit_scene(selected_scene_index, std::move(snapshot));
    }

    PbrtSceneSnapshot PbrtSceneBrowserSession::parse_scene(const std::size_t scene_index) const {
        PbrtSceneCatalogEntry entry{};
        {
            std::scoped_lock lock{this->catalogMutex};
            if (scene_index >= this->catalog.entries.size()) throw std::runtime_error("PBRT scene load index is out of range");
            entry = this->catalog.entries.at(scene_index);
        }
        if (entry.state != PbrtSceneCatalogEntryState::Candidate) throw std::runtime_error(std::format("Cannot load disabled PBRT scene \"{}\"", entry.id));
        return ParsePbrtSceneCatalogEntry(entry);
    }

    PbrtSceneEditBatch PbrtSceneBrowserSession::commit_scene(const std::size_t scene_index, PbrtSceneSnapshot snapshot) {
        if (this->currentWorkspace == nullptr) throw std::runtime_error("PBRT catalog session workspace is unavailable");
        if (snapshot.name.empty()) throw std::runtime_error("PBRT catalog session cannot commit a snapshot with an empty scene id");
        {
            std::scoped_lock lock{this->catalogMutex};
            if (scene_index >= this->catalog.entries.size()) throw std::runtime_error("PBRT scene load index is out of range");
            if (this->catalog.entries.at(scene_index).id != snapshot.name) throw std::runtime_error(std::format("PBRT catalog entry \"{}\" cannot commit snapshot \"{}\"", this->catalog.entries.at(scene_index).id, snapshot.name));
        }
        PbrtSceneEditBatch edit_batch{};
        if (!this->currentWorkspace->loaded()) {
            *this->currentWorkspace = PbrtSceneWorkspace{std::move(snapshot)};
            edit_batch = PbrtSceneEditBatch{
                .beforeRevision = SceneRevision{},
                .afterRevision  = this->currentWorkspace->snapshot()->revision,
                .dirty          = PbrtSceneDirtyFlags::Snapshot,
            };
        } else {
            edit_batch = this->currentWorkspace->replace_snapshot(std::move(snapshot));
            if (edit_batch.dirty != PbrtSceneDirtyFlags::Snapshot) throw std::runtime_error("PBRT catalog session failed to commit a scene replacement");
        }
        {
            std::scoped_lock lock{this->catalogMutex};
            this->activeSceneIndex = scene_index;
            this->selectedSceneIndex = scene_index;
            this->catalog.entries.at(scene_index).revision = edit_batch.afterRevision;
            if (this->catalog.entries.at(scene_index).probe.has_value()) this->catalog.entries.at(scene_index).probe->revision = edit_batch.afterRevision;
            this->catalog.entries.at(scene_index).state = PbrtSceneCatalogEntryState::Candidate;
            this->catalog.entries.at(scene_index).issues.clear();
            this->refresh_catalog_counts();
        }
        return edit_batch;
    }

    void PbrtSceneBrowserSession::run_background_probe_worker(const std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
            std::optional<std::size_t> probe_index{};
            PbrtSceneCatalogEntry probe_entry{};
            {
                std::unique_lock lock{this->catalogMutex};
                if (!this->backgroundCondition.wait(lock, stop_token, [this] { return this->has_background_probe_work_locked(); })) return;
                probe_index = this->next_catalog_probe_index_locked();
                if (!probe_index.has_value()) continue;
                this->catalogProbeClaimed[*probe_index] = true;
                probe_entry = this->catalog.entries.at(*probe_index);
            }

            if (stop_token.stop_requested()) return;
            ProbePbrtSceneCatalogEntry(probe_entry, stop_token);
            {
                std::scoped_lock lock{this->catalogMutex};
                if (stop_token.stop_requested()) return;
                if (*probe_index >= this->catalog.entries.size()) throw std::runtime_error("PBRT catalog probe index is out of range");
                if (this->catalog.entries.at(*probe_index).id != probe_entry.id) throw std::runtime_error("PBRT catalog changed while probing");
                this->catalog.entries.at(*probe_index) = std::move(probe_entry);
                this->catalogProbeClaimed.at(*probe_index) = false;
                this->refresh_catalog_counts();
            }
            this->backgroundCondition.notify_all();
        }
    }

    void PbrtSceneBrowserSession::refresh_catalog_counts() {
        RefreshCatalogCounts(&this->catalog);
    }

    bool PbrtSceneBrowserSession::has_background_probe_work_locked() const {
        return this->next_catalog_probe_index_locked().has_value();
    }

    std::optional<std::size_t> PbrtSceneBrowserSession::next_catalog_probe_index_locked() const {
        if (this->catalogProbeClaimed.size() != this->catalog.entries.size()) throw std::runtime_error("PBRT catalog probe claim table is out of sync");
        for (std::size_t scene_index = 0; scene_index < this->catalog.entries.size(); ++scene_index) {
            if (this->catalog.entries.at(scene_index).state != PbrtSceneCatalogEntryState::Pending) continue;
            if (this->catalogProbeClaimed.at(scene_index)) continue;
            return scene_index;
        }
        return {};
    }

    PbrtSceneCatalog DiscoverPbrtSceneCatalog() {
        PbrtSceneCatalog catalog{.root = SceneRoot()};
        if (!std::filesystem::exists(catalog.root)) throw std::runtime_error(std::format("{}: scene root does not exist", catalog.root.string()));
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(catalog.root)) {
            if (!entry.is_regular_file()) continue;
            const std::filesystem::path sourcePath = std::filesystem::absolute(entry.path()).lexically_normal();
            if (!IsPbrtSceneFile(sourcePath)) continue;

            const std::filesystem::path relativePath = sourcePath.lexically_relative(catalog.root);
            PbrtSceneCatalogEntry catalogEntry{
                .id          = PathId(relativePath),
                .displayName = SceneDisplayName(relativePath),
                .group       = SceneGroupName(relativePath),
                .relativePath = relativePath,
                .sourcePath  = sourcePath,
                .state       = PbrtSceneCatalogEntryState::Pending,
            };
            catalog.entries.push_back(std::move(catalogEntry));
        }
        std::ranges::sort(catalog.entries, {}, &PbrtSceneCatalogEntry::id);
        RefreshCatalogCounts(&catalog);
        return catalog;
    }

    PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry) {
        return ProbePbrtSceneCatalogEntry(entry, std::stop_token{});
    }

    PbrtSceneProbeReport ProbePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, const std::stop_token stopToken) {
        CheckSceneStop(&stopToken);
        PbrtSceneProbeBuilder builder(entry.sourcePath, &stopToken);
        PbrtSceneProbeReport report = builder.Probe();
        report.name             = entry.id;
        report.title            = entry.displayName;
        if (report.revision.value == 0) report.revision = SceneRevision{1};
        return report;
    }

    PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry) {
        return ParsePbrtSceneCatalogEntry(entry, std::stop_token{});
    }

    PbrtSceneSnapshot ParsePbrtSceneCatalogEntry(const PbrtSceneCatalogEntry& entry, const std::stop_token stopToken) {
        CheckSceneStop(&stopToken);
        PbrtSceneBuilder builder(entry.sourcePath, &stopToken);
        PbrtSceneSnapshot scene = builder.Parse();
        scene.name          = entry.id;
        scene.title         = entry.displayName;
        if (scene.revision.value == 0) scene.revision = SceneRevision{1};
        return scene;
    }

    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry) {
        ProbePbrtSceneCatalogEntry(entry, std::stop_token{});
    }

    void ProbePbrtSceneCatalogEntry(PbrtSceneCatalogEntry& entry, const std::stop_token stopToken) {
        entry.state = PbrtSceneCatalogEntryState::Pending;
        entry.revision = SceneRevision{};
        entry.probe.reset();
        entry.issues.clear();
        try {
            PbrtSceneProbeReport report = ProbePbrtSceneCatalogEntry(static_cast<const PbrtSceneCatalogEntry&>(entry), stopToken);
            entry.revision          = report.revision;
            entry.probe             = std::move(report);
            entry.state             = PbrtSceneCatalogEntryState::Candidate;
        } catch (const SceneOperationCancelled&) {
            entry.state = PbrtSceneCatalogEntryState::Pending;
            entry.revision = SceneRevision{};
            entry.probe.reset();
            entry.issues.clear();
        } catch (const PbrtSceneNotTopLevel&) {
            entry.state = PbrtSceneCatalogEntryState::NonScene;
            entry.revision = SceneRevision{};
            entry.probe.reset();
            entry.issues.clear();
        } catch (const std::exception& error) {
            entry.state = PbrtSceneCatalogEntryState::Invalid;
            entry.revision = SceneRevision{};
            entry.probe.reset();
            entry.issues.push_back(PbrtSceneDiagnostic{
                .source  = SceneSourceLocation{.filename = entry.sourcePath.string(), .line = 1, .column = 1},
                .message = error.what(),
            });
        }
    }

    PbrtSceneWorkspace BuildPbrtScene(const std::string_view name) {
        const std::filesystem::path scenePath = ResolveScenePath(name);
        PbrtSceneBuilder builder(scenePath);
        PbrtSceneSnapshot scene = builder.Parse();
        if (name.empty()) throw std::runtime_error("PBRT scene build requires a non-empty scene id");
        scene.name = std::string{name};
        return PbrtSceneWorkspace{std::move(scene)};
    }
} // namespace spectra::scene
