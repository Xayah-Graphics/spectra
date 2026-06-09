module spectra.scene.session;

import std;

namespace spectra::scene {
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
} // namespace spectra::scene
